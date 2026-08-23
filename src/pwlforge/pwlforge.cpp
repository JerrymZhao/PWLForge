#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <limits>
#include <functional>
#include <cstdlib>
#include <stdexcept>
#include <sys/stat.h>

#include "exprtk.hpp" // Bundled third-party parser; see THIRD_PARTY_NOTICES.md.
#include "common_types.hpp"
#include "common_utils.hpp"
#include "interval_types.hpp"
#include "interval_optimizer.hpp"
#include "function_fitter.hpp"
#include "quantization_eval.hpp"
#include "group_types.hpp"
#include "group_grouping.hpp"
#include "group_quantization.hpp"
#include "group_symmetry.hpp"
#include "group_encode.hpp"
#include "hw_mapping.hpp"

//================================================================================
// Ablation mode enums
//================================================================================

enum class IntervalMode {
    OPTIMIZED,
    UNIFORM
};

enum class GroupingMode {
    FULL,      // original behavior: grouping + symmetry
    NO_GROUP,  // disable effective grouping and symmetry
    NO_SYM     // grouping enabled, symmetry disabled
};

std::string intervalModeToString(IntervalMode mode) {
    switch (mode) {
        case IntervalMode::OPTIMIZED: return "optimized";
        case IntervalMode::UNIFORM:   return "uniform";
        default: return "unknown";
    }
}

std::string groupingModeToString(GroupingMode mode) {
    switch (mode) {
        case GroupingMode::FULL:     return "full";
        case GroupingMode::NO_GROUP: return "nogroup";
        case GroupingMode::NO_SYM:   return "nosym";
        default: return "unknown";
    }
}

IntervalMode parseIntervalMode(const std::string& s) {
    if (s == "optimized") return IntervalMode::OPTIMIZED;
    if (s == "uniform")   return IntervalMode::UNIFORM;
    throw std::runtime_error("Invalid --interval-mode: " + s + " (expected optimized|uniform)");
}

GroupingMode parseGroupingMode(const std::string& s) {
    if (s == "full")    return GroupingMode::FULL;
    if (s == "nogroup") return GroupingMode::NO_GROUP;
    if (s == "nosym")   return GroupingMode::NO_SYM;
    throw std::runtime_error("Invalid --grouping-mode: " + s + " (expected full|nogroup|nosym)");
}

//================================================================================
// Utilities
//================================================================================

void createDirectory(const std::string& path) {
    int ret;
    #ifdef _WIN32
        ret = system(("mkdir " + path + " 2>nul").c_str());
    #else
        ret = system(("mkdir -p " + path).c_str());
    #endif

    if (ret != 0) {
        std::cerr << "Warning: Failed to create directory: " << path << "\n";
    }
}

std::string sanitizeName(const std::string& name) {
    std::string result = name;

    std::replace(result.begin(), result.end(), '.', '_');
    std::replace(result.begin(), result.end(), '(', '_');
    std::replace(result.begin(), result.end(), ')', '_');
    std::replace(result.begin(), result.end(), ' ', '_');
    std::replace(result.begin(), result.end(), ',', '_');
    std::replace(result.begin(), result.end(), '/', '_');
    std::replace(result.begin(), result.end(), '\\', '_');
    std::replace(result.begin(), result.end(), '*', 'x');
    std::replace(result.begin(), result.end(), '+', 'p');
    std::replace(result.begin(), result.end(), '-', 'm');
    std::replace(result.begin(), result.end(), '^', '_');
    std::replace(result.begin(), result.end(), ':', '_');
    std::replace(result.begin(), result.end(), ';', '_');
    std::replace(result.begin(), result.end(), '=', '_');

    auto new_end = std::unique(result.begin(), result.end(),
        [](char a, char b) { return a == '_' && b == '_'; });
    result.erase(new_end, result.end());

    if (!result.empty() && result.front() == '_') {
        result.erase(result.begin());
    }
    if (!result.empty() && result.back() == '_') {
        result.pop_back();
    }

    return result;
}

std::string extractFunctionName(const std::string& expr) {
    // Normalize: lowercase + remove all whitespace
    std::string s = expr;
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    s.erase(std::remove_if(s.begin(), s.end(),
                           [](unsigned char c) { return std::isspace(c); }),
            s.end());

    //--------------------------------------------------------------------------
    // Helpers for explicit formula detection
    //--------------------------------------------------------------------------
    auto contains = [&](const std::string& token) -> bool {
        return s.find(token) != std::string::npos;
    };

    auto contains_all = [&](std::initializer_list<const char*> tokens) -> bool {
        for (const char* t : tokens) {
            if (s.find(t) == std::string::npos) return false;
        }
        return true;
    };

    //--------------------------------------------------------------------------
    // 1) Most specific aliases / canonical names first
    //--------------------------------------------------------------------------

    if (contains("hardswish") || contains("hswish")) {
        return "hswish";
    }

    if (contains("hardsigmoid") || contains("hsigmoid")) {
        return "hsigmoid";
    }

    // SiLU / Swish named forms
    if (contains("silu") || contains("swish")) {
        return "silu";
    }

    // GELU named forms
    if (contains("gelu")) {
        return "gelu";
    }

    // IMPORTANT: check SELU before ELU because "selu" contains "elu"
    if (contains("selu")) {
        return "selu";
    }

    if (contains("softplus")) {
        return "softplus";
    }

    if (contains("mish")) {
        return "mish";
    }

    if (contains("elu")) {
        return "elu";
    }

    //--------------------------------------------------------------------------
    // 2) Explicit formula pattern detection for common activations
    //--------------------------------------------------------------------------

    // GELU tanh approximation:
    // 0.5*x*(1+tanh(0.7978845608*(x+0.044715*x*x*x)))
    // Allow minor formatting/constant variations by checking key structural pieces.
    if (contains_all({
            "0.5*x",
            "1+tanh(",
            "x+0.044715*x*x*x"
        })) {
        return "gelu";
    }

    // GELU erf form:
    // 0.5*x*(1+erf(x/sqrt(2)))
    if (contains_all({
            "0.5*x",
            "1+erf(",
            "sqrt(2)"
        })) {
        return "gelu";
    }

    // SiLU / Swish explicit form:
    // x/(1+exp(-x))
    if (contains("x/(1+exp(-x))") ||
        contains("(x/(1+exp(-x)))")) {
        return "silu";
    }

    // Sigmoid explicit form:
    // 1/(1+exp(-x))
    if (contains("1/(1+exp(-x))") ||
        contains("(1/(1+exp(-x)))")) {
        return "sigmoid";
    }

    // HardSigmoid common forms
    // e.g. max(0,min(1,(x+3)/6)) or min(1,max(0,(x+3)/6))
    if ((contains("max(0,min(1,") || contains("min(1,max(0,")) &&
        (contains("(x+3)/6") || contains("(x+3.0)/6") || contains("(x+3)/6.0"))) {
        return "hsigmoid";
    }

    // HardSwish common form: x * hard-sigmoid
    // e.g. x*max(0,min(1,(x+3)/6))
    if ((contains("x*max(0,min(1,") || contains("x*min(1,max(0,")) &&
        (contains("(x+3)/6") || contains("(x+3.0)/6") || contains("(x+3)/6.0"))) {
        return "hswish";
    }

    //--------------------------------------------------------------------------
    // 3) Standard named function fallback
    //--------------------------------------------------------------------------

    if (contains("tanh"))    return "tanh";
    if (contains("sigmoid")) return "sigmoid";
    if (contains("relu"))    return "relu";
    if (contains("sin"))     return "sin";
    if (contains("cos"))     return "cos";
    if (contains("exp"))     return "exp";
    if (contains("log"))     return "log";
    if (contains("sqrt"))    return "sqrt";
    if (contains("erf"))     return "erf";

    //--------------------------------------------------------------------------
    // 4) Final fallback: sanitize original expression
    //--------------------------------------------------------------------------

    return sanitizeName(expr);
}

std::string normalizeExpression(const std::string& expr) {
    std::string result = expr;

    size_t pos = 0;
    while ((pos = result.find("x^2", pos)) != std::string::npos) {
        result.replace(pos, 3, "(x*x)");
        pos += 5;
    }
    pos = 0;
    while ((pos = result.find("x^3", pos)) != std::string::npos) {
        result.replace(pos, 3, "(x*x*x)");
        pos += 7;
    }
    pos = 0;
    while ((pos = result.find("x^4", pos)) != std::string::npos) {
        result.replace(pos, 3, "(x*x*x*x)");
        pos += 9;
    }

    return result;
}

bool fileExists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

//================================================================================
// Uniform interval builder for ablation
//================================================================================

std::vector<Interval> buildUniformIntervals(double x_start, double x_end, int n_intervals) {
    if (n_intervals <= 0) {
        throw std::runtime_error("Uniform interval count must be > 0");
    }
    if (!(x_end > x_start)) {
        throw std::runtime_error("Invalid range for uniform intervals");
    }

    std::vector<Interval> intervals;
    intervals.reserve(static_cast<size_t>(n_intervals));

    double dx = (x_end - x_start) / static_cast<double>(n_intervals);

    for (int i = 0; i < n_intervals; ++i) {
        double s = x_start + i * dx;
        double e = (i == n_intervals - 1) ? x_end : (x_start + (i + 1) * dx);

        Interval iv;
        iv.start = s;
        iv.end = e;

        // Keep quantization-related fields in a safe default state
        iv.is_quantized = false;
        iv.start_quantized = s;
        iv.end_quantized = e;

        intervals.push_back(iv);
    }

    return intervals;
}

//================================================================================
// Quantization result structure
//================================================================================

struct QuantizationEvalResult {
    std::vector<QuantizedInterval> quantized_intervals;
    QuantizationConfig config;
    QuantizationErrorStats stats;
    double fitting_max_mae;
    std::string config_name;
};

const QuantizationEvalResult& selectPreferredValidConfig(
    const std::vector<QuantizationEvalResult>& results,
    double average_mae_target) {

    const QuantizationEvalResult* selected = nullptr;
    for (const auto& result : results) {
        if (result.stats.avg_mae > average_mae_target) {
            continue;
        }

        if (selected == nullptr ||
            result.stats.quantized_bits < selected->stats.quantized_bits ||
            (result.stats.quantized_bits == selected->stats.quantized_bits &&
             result.stats.avg_mae < selected->stats.avg_mae)) {
            selected = &result;
        }
    }

    if (selected == nullptr) {
        throw std::runtime_error(
            "No evaluated numeric format satisfies the target sampled average MAE");
    }
    return *selected;
}

//================================================================================
// Stage3 summary structure
//================================================================================

struct Stage3Summary {
    std::string quant_config_name;
    size_t total_groups = 0;
    size_t normal_groups = 0;
    size_t orphan_groups = 0;
    size_t total_intervals = 0;
    size_t orphan_intervals = 0;
    size_t min_group_size = 0;
    size_t max_group_size = 0;
    double avg_group_size = 0.0;
    double compression_ratio = 0.0;
    std::string output_dir;
};

//================================================================================
// Helper functions for quantization
//================================================================================

QuantizationEvalResult processQuantizationConfig(
    const std::vector<Interval>& intervals,
    const std::vector<FitParameters>& params,
    const QuantizationConfig& config,
    std::function<double(double)> original_function,
    bool verbose) {

    QuantizationEvaluator evaluator(original_function, 500);

    auto quantized = evaluator.quantize_intervals(intervals, params, config);
    auto stats = evaluator.evaluate_errors(intervals, params, quantized, config);

    if (verbose) {
        std::cout << "  Config: " << config.name() << "\n";
        std::cout << "    Max MAE:          " << std::scientific << stats.max_mae << "\n";
        std::cout << "    Avg MAE:          " << stats.avg_mae << "\n";
        std::cout << "    RMSE:             " << stats.rmse << "\n";
        std::cout << "    Max param error:  " << stats.max_param_error << "\n";
        std::cout << "    Compression:      " << std::fixed << std::setprecision(2)
                  << stats.compression_ratio << "x\n";
    }

    return QuantizationEvalResult{
        quantized,
        config,
        stats,
        stats.max_mae,
        config.name()
    };
}

std::vector<QuantizationEvalResult> processAllQuantizationConfigs(
    const std::vector<Interval>& intervals,
    const std::vector<FitParameters>& params,
    std::function<double(double)> original_function,
    bool verbose) {

    DataRange range = QuantizationEvaluator::analyze_data_range(intervals, params);

    if (verbose) {
        std::cout << "\n=== Phase 2.5: Processing All Quantization Configurations ===\n";
        range.print();
        std::cout << "\n";
    }

    std::vector<QuantizationConfig> candidate_configs;

    candidate_configs.push_back({
        PrecisionConfig{NumericFormat::FP64, 0, 0},
        PrecisionConfig{NumericFormat::FP64, 0, 0}
    });

    candidate_configs.push_back({
        PrecisionConfig{NumericFormat::FP32, 0, 0},
        PrecisionConfig{NumericFormat::FP32, 0, 0}
    });

    candidate_configs.push_back({
        PrecisionConfig{NumericFormat::FP16, 0, 0},
        PrecisionConfig{NumericFormat::FP16, 0, 0}
    });

    candidate_configs.push_back({
        PrecisionConfig{NumericFormat::FP64, 0, 0},
        PrecisionConfig{NumericFormat::FP32, 0, 0}
    });

    candidate_configs.push_back({
        PrecisionConfig{NumericFormat::FP64, 0, 0},
        PrecisionConfig{NumericFormat::FP16, 0, 0}
    });

    candidate_configs.push_back({
        PrecisionConfig{NumericFormat::FP32, 0, 0},
        PrecisionConfig{NumericFormat::FP16, 0, 0}
    });

    candidate_configs.push_back(
        QuantizationConfig::create_auto_fixed(16, 16, range)
    );

    candidate_configs.push_back(
        QuantizationConfig::create_auto_fixed(24, 24, range)
    );

    candidate_configs.push_back(
        QuantizationConfig::create_auto_fixed(32, 32, range)
    );

    candidate_configs.push_back(
        QuantizationConfig::create_auto_fixed(40, 40, range)
    );

    candidate_configs.push_back(
        QuantizationConfig::create_auto_fixed(48, 48, range)
    );

    candidate_configs.push_back(
        QuantizationConfig::create_auto_fixed(64, 64, range)
    );

    candidate_configs.push_back(
        QuantizationConfig::create_auto_fixed(64, 32, range)
    );

    candidate_configs.push_back(
        QuantizationConfig::create_auto_fixed(48, 24, range)
    );

    candidate_configs.push_back(
        QuantizationConfig::create_auto_fixed(32, 16, range)
    );

    std::vector<QuantizationEvalResult> results;

    if (verbose) {
        std::cout << "Total configurations: " << candidate_configs.size() << "\n\n";
    }

    for (const auto& config : candidate_configs) {
        auto result = processQuantizationConfig(
            intervals, params, config, original_function, verbose);
        results.push_back(result);

        if (verbose) std::cout << "\n";
    }

    if (verbose) {
        std::cout << "==========================================\n\n";
    }

    return results;
}

void saveQuantizedIntervalsToFile(
    const std::vector<QuantizedInterval>& quantized,
    const std::string& filename) {

    std::ofstream file(filename);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + filename);
    }

    file << "index,start_orig,end_orig,start_q,end_q,length_orig,length_q\n";
    file << std::scientific << std::setprecision(15);

    for (const auto& qi : quantized) {
        double len_orig = qi.end_orig - qi.start_orig;
        double len_q = qi.end_q - qi.start_q;

        file << qi.index << ","
             << qi.start_orig << "," << qi.end_orig << ","
             << qi.start_q << "," << qi.end_q << ","
             << len_orig << "," << len_q << "\n";
    }
}

void saveQuantizedParametersToFile(
    const std::vector<QuantizedInterval>& quantized,
    const std::string& filename) {

    std::ofstream file(filename);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + filename);
    }

    file << "index,a_orig,b_orig,c_orig,a_q,b_q,c_q\n";
    file << std::scientific << std::setprecision(15);

    for (const auto& qi : quantized) {
        file << qi.index << ","
             << qi.a_orig << "," << qi.b_orig << "," << qi.c_orig << ","
             << qi.a_q << "," << qi.b_q << "," << qi.c_q << "\n";
    }
}

void saveQuantizationReport(
    const QuantizationEvalResult& result,
    const std::string& filename) {

    std::ofstream file(filename);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + filename);
    }

    file << "=== Quantization Report ===\n\n";

    file << "Configuration:\n";
    file << "  " << result.config.name() << "\n\n";

    file << "Statistics:\n";
    file << std::scientific << std::setprecision(6);

    file << "Parameter Quantization:\n";
    file << "  Max param error:     " << result.stats.max_param_error << "\n";
    file << "  Avg param error:     " << result.stats.avg_param_error << "\n\n";

    file << "Function Approximation (vs True Function):\n";
    file << "  Max MAE:             " << result.stats.max_mae << "\n";
    file << "  Avg MAE:             " << result.stats.avg_mae << "\n";
    file << "  RMSE:                " << result.stats.rmse << "\n\n";

    file << std::fixed << std::setprecision(2);
    file << "Compression:\n";
    file << "  Compression ratio:   " << result.stats.compression_ratio << "x\n";
}

void saveAllConfigsSummary(
    const std::vector<QuantizationEvalResult>& all_results,
    const std::string& filename) {

    std::ofstream file(filename);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + filename);
    }

    file << "config_name,max_mae,avg_mae,rmse,max_param_error,avg_param_error,compression_ratio\n";
    file << std::scientific << std::setprecision(15);

    for (const auto& result : all_results) {
        file << result.config_name << ","
             << result.stats.max_mae << ","
             << result.stats.avg_mae << ","
             << result.stats.rmse << ","
             << result.stats.max_param_error << ","
             << result.stats.avg_param_error << ","
             << std::fixed << std::setprecision(6)
             << result.stats.compression_ratio << "\n";
    }
}

void saveRunMetadata(const std::string& filename,
                     const std::string& function_expr,
                     const std::string& function_name,
                     double x_start,
                     double x_end,
                     double max_error,
                     IntervalMode interval_mode,
                     GroupingMode grouping_mode,
                     int requested_uniform_intervals,
                     size_t final_interval_count,
                     bool enable_hardware,
                     int pos_bits,
                     int a_bits,
                     int b_bits,
                     int c_bits) {
    std::ofstream file(filename);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + filename);
    }

    file << "function_expr=" << function_expr << "\n";
    file << "function_name=" << function_name << "\n";
    file << std::scientific << std::setprecision(15);
    file << "x_start=" << x_start << "\n";
    file << "x_end=" << x_end << "\n";
    file << "max_error=" << max_error << "\n";
    file << "interval_mode=" << intervalModeToString(interval_mode) << "\n";
    file << "grouping_mode=" << groupingModeToString(grouping_mode) << "\n";
    file << "requested_uniform_intervals=" << requested_uniform_intervals << "\n";
    file << "final_interval_count=" << final_interval_count << "\n";
    file << "enable_hardware=" << (enable_hardware ? 1 : 0) << "\n";
    file << "pos_bits=" << pos_bits << "\n";
    file << "a_bits=" << a_bits << "\n";
    file << "b_bits=" << b_bits << "\n";
    file << "c_bits=" << c_bits << "\n";
}

void saveStage3SingleSummary(const Stage3Summary& s, const std::string& filename) {
    std::ofstream file(filename);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + filename);
    }

    file << "quant_config,total_groups,normal_groups,orphan_groups,total_intervals,orphan_intervals,"
            "min_group_size,max_group_size,avg_group_size,final_compression_ratio,output_dir\n";
    file << std::fixed << std::setprecision(10);
    file << s.quant_config_name << ","
         << s.total_groups << ","
         << s.normal_groups << ","
         << s.orphan_groups << ","
         << s.total_intervals << ","
         << s.orphan_intervals << ","
         << s.min_group_size << ","
         << s.max_group_size << ","
         << s.avg_group_size << ","
         << s.compression_ratio << ","
         << s.output_dir << "\n";
}

void saveStage3AllConfigsSummary(const std::vector<Stage3Summary>& summaries,
                                 const std::string& filename) {
    std::ofstream file(filename);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + filename);
    }

    file << "quant_config,total_groups,normal_groups,orphan_groups,total_intervals,orphan_intervals,"
            "min_group_size,max_group_size,avg_group_size,final_compression_ratio,output_dir\n";
    file << std::fixed << std::setprecision(10);

    for (const auto& s : summaries) {
        file << s.quant_config_name << ","
             << s.total_groups << ","
             << s.normal_groups << ","
             << s.orphan_groups << ","
             << s.total_intervals << ","
             << s.orphan_intervals << ","
             << s.min_group_size << ","
             << s.max_group_size << ","
             << s.avg_group_size << ","
             << s.compression_ratio << ","
             << s.output_dir << "\n";
    }
}

//================================================================================
// Pipeline configuration
//================================================================================

struct PipelineConfig {
    std::string function_expr;
    std::string function_name;
    double x_start, x_end, max_error;
    std::string result_dir;
    bool enable_hardware = false;
    int pos_bits = 16, a_bits = 16, b_bits = 16, c_bits = 16;

    // New optional ablation controls
    IntervalMode interval_mode = IntervalMode::OPTIMIZED;
    GroupingMode grouping_mode = GroupingMode::FULL;
    int uniform_intervals = 0; // 0 => auto choose based on optimized result
};

//================================================================================
// Main pipeline
//================================================================================

class Pipeline {
private:
    PipelineConfig config_;

    std::vector<Interval> intervals_;
    std::vector<FitParameters> fit_params_;
    std::vector<double> samples_;

    std::vector<QuantizationEvalResult> all_quantization_results_;
    QuantizationEvalResult selected_quantization_result_;
    std::vector<Stage3Summary> all_stage3_summaries_;

public:
    Pipeline(const PipelineConfig& config) : config_(config) {}

    bool run() {
        try {
            std::cout << "\n╔════════════════════════════════════════════════════════════╗\n";
            std::cout << "║  Function Approximation Pipeline                          ║\n";
            std::cout << "╚════════════════════════════════════════════════════════════╝\n\n";

            std::cout << "Function:         " << config_.function_expr << "\n";
            std::cout << "Range:            [" << config_.x_start << ", " << config_.x_end << "]\n";
            std::cout << "Max Error:        " << std::scientific << config_.max_error << "\n";
            std::cout << "Interval Mode:    " << intervalModeToString(config_.interval_mode) << "\n";
            std::cout << "Grouping Mode:    " << groupingModeToString(config_.grouping_mode) << "\n";
            if (config_.interval_mode == IntervalMode::UNIFORM) {
                std::cout << "Uniform Ints.:    "
                          << (config_.uniform_intervals > 0 ? std::to_string(config_.uniform_intervals) : std::string("auto"))
                          << "\n";
            }
            std::cout << "Output Dir:       " << config_.result_dir << "\n\n";

            createDirectory("results");
            createDirectory(config_.result_dir);

            runPhase1and2();
            runPhase2_5();
            runStage3ForSelectedConfig();

            std::cout << "\n╔════════════════════════════════════════════════════════════╗\n";
            std::cout << "║  Pipeline Complete ✓                                      ║\n";
            std::cout << "╚════════════════════════════════════════════════════════════╝\n\n";
            std::cout << "Total Intervals:     " << intervals_.size() << "\n";
            std::cout << "Quant Configs Eval.: " << all_quantization_results_.size() << "\n";
            std::cout << "Selected Config:     " << selected_quantization_result_.config_name << "\n";
            std::cout << "Output Directory:    " << config_.result_dir << "/\n\n";

            return true;
        } catch (const std::exception& e) {
            std::cerr << "\n✗ Error: " << e.what() << "\n";
            return false;
        }
    }

private:
    void runPhase1and2() {
        std::cout << "=== Phase 1&2: Interval Optimization & Polynomial Fitting ===\n";

        std::string expr = normalizeExpression(config_.function_expr);

        FittingParametersConfig fit_config;
        fit_config.epsilon_start = config_.max_error / 10.0;
        fit_config.epsilon_end = config_.max_error / 2.0;
        fit_config.acceptable_error = config_.max_error;
        fit_config.min_unit_length = (config_.x_end - config_.x_start) / 100000.0;
        fit_config.merge_relax_factor = 1.0;
        fit_config.epsilon_steps = 1;

        if (config_.interval_mode == IntervalMode::OPTIMIZED) {
            OptimizationResult opt_result = optimizeIntervals(
                config_.x_start, config_.x_end, expr, fit_config);
            intervals_ = opt_result.intervals;
        } else {
            int n_uniform = config_.uniform_intervals;

            if (n_uniform <= 0) {
                // Auto choose: first run optimized once, then reuse the same interval count
                // so uniform-vs-optimized is directly comparable at similar storage scale.
                OptimizationResult ref_result = optimizeIntervals(
                    config_.x_start, config_.x_end, expr, fit_config);
                n_uniform = static_cast<int>(ref_result.intervals.size());
                std::cout << "  [Uniform auto] Using optimized interval count as reference: "
                          << n_uniform << "\n";
            }

            intervals_ = buildUniformIntervals(config_.x_start, config_.x_end, n_uniform);
        }

        fitAllSegments(expr, intervals_, fit_params_, config_.max_error);

        saveIntervalsToFile(intervals_, config_.result_dir + "/intervals_fp64.csv");
        saveFitParametersToFile(fit_params_, config_.result_dir + "/fit_params_fp64.csv");

        int num_samples = std::max(1000, static_cast<int>(intervals_.size()) * 10);
        double dx = (config_.x_end - config_.x_start) / (num_samples - 1);

        samples_.clear();
        std::ofstream sf(config_.result_dir + "/samples.csv");
        if (!sf) {
            throw std::runtime_error("Cannot open samples.csv for writing");
        }

        sf << "index,x,y\n" << std::scientific << std::setprecision(15);
        for (int i = 0; i < num_samples; ++i) {
            double x = config_.x_start + i * dx;
            double y = computeFunctionValue(expr, x);
            samples_.push_back(y);
            sf << i << "," << x << "," << y << "\n";
        }

        saveRunMetadata(
            config_.result_dir + "/run_metadata.txt",
            config_.function_expr,
            config_.function_name,
            config_.x_start,
            config_.x_end,
            config_.max_error,
            config_.interval_mode,
            config_.grouping_mode,
            config_.uniform_intervals,
            intervals_.size(),
            config_.enable_hardware,
            config_.pos_bits,
            config_.a_bits,
            config_.b_bits,
            config_.c_bits
        );

        std::cout << "✓ Generated " << intervals_.size()
                  << " intervals (FP64 baseline, mode="
                  << intervalModeToString(config_.interval_mode) << ")\n\n";
    }

    void runPhase2_5() {
        std::cout << "=== Phase 2.5: Quantization Evaluation ===\n";

        std::string expr = normalizeExpression(config_.function_expr);

        auto original_function = [expr](double x) -> double {
            return computeFunctionValue(expr, x);
        };

        all_quantization_results_ = processAllQuantizationConfigs(
            intervals_, fit_params_, original_function, true);

        saveAllConfigsSummary(all_quantization_results_,
                             config_.result_dir + "/quantization_summary.csv");

        selected_quantization_result_ = selectPreferredValidConfig(
            all_quantization_results_, config_.max_error);

        std::ofstream selected_file(config_.result_dir + "/selected_config.txt");
        if (!selected_file) {
            throw std::runtime_error("Cannot open selected_config.txt for writing");
        }
        selected_file << "selection_metric=sampled_average_mae\n";
        selected_file << "selection_target=" << std::scientific << std::setprecision(15)
                      << config_.max_error << "\n";
        selected_file << "selected_config=" << selected_quantization_result_.config_name << "\n";
        selected_file << "selected_avg_mae=" << selected_quantization_result_.stats.avg_mae << "\n";
        selected_file << "selected_quantized_bits=" << selected_quantization_result_.stats.quantized_bits << "\n";

        std::cout << "  Selected valid configuration: "
                  << selected_quantization_result_.config_name
                  << " (sampled average MAE=" << std::scientific
                  << selected_quantization_result_.stats.avg_mae
                  << ", representation bits=" << selected_quantization_result_.stats.quantized_bits
                  << ")\n\n";

        for (const auto& result : all_quantization_results_) {
            std::string safe_cfg = sanitizeName(result.config_name);
            std::string cfg_dir = config_.result_dir + "/quant_eval_" + safe_cfg;
            createDirectory(cfg_dir);

            saveQuantizedIntervalsToFile(
                result.quantized_intervals,
                cfg_dir + "/quantized_intervals.csv"
            );
            saveQuantizedParametersToFile(
                result.quantized_intervals,
                cfg_dir + "/quantized_parameters.csv"
            );
            saveQuantizationReport(
                result,
                cfg_dir + "/quantization_report.txt"
            );
        }

        std::cout << "\n✓ Quantization evaluation complete\n";
        std::cout << "  Tested " << all_quantization_results_.size() << " configurations\n\n";
    }

    void runStage3ForSelectedConfig() {
        std::cout << "=== Stage 3: Group Encoding & Compression ===\n";

        std::string expr = normalizeExpression(config_.function_expr);

        auto original_function = [expr](double x) -> double {
            return computeFunctionValue(expr, x);
        };

        const auto& quant_result = selected_quantization_result_;
        std::string config_output_dir = config_.result_dir + "/stage3_" + sanitizeName(quant_result.config_name);
        createDirectory(config_output_dir);

        std::vector<Interval> quantized_intervals = intervals_;
        std::vector<FitParameters> quantized_params = fit_params_;

        for (size_t j = 0; j < quantized_intervals.size(); ++j) {
            const auto& qi = quant_result.quantized_intervals[j];

            quantized_intervals[j].is_quantized = true;
            quantized_intervals[j].start_quantized = qi.start_q;
            quantized_intervals[j].end_quantized = qi.end_q;

            quantized_params[j].is_quantized = true;
            quantized_params[j].a_quantized = qi.a_q;
            quantized_params[j].b_quantized = qi.b_q;
            quantized_params[j].c_quantized = qi.c_q;
        }

        all_stage3_summaries_.clear();
        Stage3Summary summary = runStage3Single(
            quantized_intervals,
            quantized_params,
            original_function,
            config_output_dir,
            quant_result.config);
        summary.quant_config_name = quant_result.config_name;
        summary.output_dir = config_output_dir;
        all_stage3_summaries_.push_back(summary);

        saveStage3AllConfigsSummary(
            all_stage3_summaries_,
            config_.result_dir + "/stage3_selected_config_summary.csv"
        );

        std::cout << "\n✓ Stage 3 complete for the selected configuration\n";
    }

    void printGroupingStatistics(const std::vector<QuantizedGroup>& groups) {
        size_t total_intervals = 0;
        size_t orphan_intervals = 0;
        size_t normal_groups = 0;
        std::vector<size_t> group_sizes;

        for (const auto& group : groups) {
            size_t group_size = group.members.size();
            total_intervals += group_size;

            bool is_orphan = (group.storage_type == GroupStorageType::ORPHAN_GROUP);

            if (is_orphan) {
                orphan_intervals += group_size;
            } else {
                normal_groups++;
                group_sizes.push_back(group_size);
            }
        }

        std::cout << "  ┌─ Grouping Statistics ─────────────────────┐\n";
        std::cout << "  │ Total Groups:        " << std::setw(6) << groups.size() << "              │\n";
        std::cout << "  │ Normal Groups:       " << std::setw(6) << normal_groups << "              │\n";
        std::cout << "  │ Orphan Groups:       " << std::setw(6) << (groups.size() - normal_groups) << "              │\n";
        std::cout << "  │ Total Intervals:     " << std::setw(6) << total_intervals << "              │\n";
        std::cout << "  │ Orphan Intervals:    " << std::setw(6) << orphan_intervals << "              │\n";

        if (!group_sizes.empty()) {
            size_t min_size = *std::min_element(group_sizes.begin(), group_sizes.end());
            size_t max_size = *std::max_element(group_sizes.begin(), group_sizes.end());
            double avg_size = std::accumulate(group_sizes.begin(), group_sizes.end(), 0.0) / group_sizes.size();

            std::cout << "  ├─ Normal Group Size ───────────────────────┤\n";
            std::cout << "  │ Min:                 " << std::setw(6) << min_size << "              │\n";
            std::cout << "  │ Max:                 " << std::setw(6) << max_size << "              │\n";
            std::cout << "  │ Average:             " << std::setw(6) << std::fixed << std::setprecision(1)
                      << avg_size << "              │\n";
        }

        std::cout << "  └───────────────────────────────────────────┘\n";
    }

    Stage3Summary summarizeGroups(const std::vector<QuantizedGroup>& groups, double compression_ratio) {
        Stage3Summary s;
        s.total_groups = groups.size();
        s.compression_ratio = compression_ratio;

        std::vector<size_t> normal_group_sizes;

        for (const auto& group : groups) {
            size_t group_size = group.members.size();
            s.total_intervals += group_size;

            bool is_orphan = (group.storage_type == GroupStorageType::ORPHAN_GROUP);
            if (is_orphan) {
                s.orphan_groups += 1;
                s.orphan_intervals += group_size;
            } else {
                s.normal_groups += 1;
                normal_group_sizes.push_back(group_size);
            }
        }

        if (!normal_group_sizes.empty()) {
            s.min_group_size = *std::min_element(normal_group_sizes.begin(), normal_group_sizes.end());
            s.max_group_size = *std::max_element(normal_group_sizes.begin(), normal_group_sizes.end());
            s.avg_group_size = std::accumulate(normal_group_sizes.begin(), normal_group_sizes.end(), 0.0)
                             / static_cast<double>(normal_group_sizes.size());
        }

        return s;
    }

    Stage3Summary runStage3Single(
        const std::vector<Interval>& intervals,
        const std::vector<FitParameters>& params,
        const std::function<double(double)>& original_function,
        const std::string& output_dir,
        const QuantizationConfig& quant_config) {

        Stage3Config config;

        double auto_tolerance = computeRecommendedLengthTolerance(intervals);
        config.length_tolerance = std::max(1e-6, std::min(0.01, auto_tolerance));

        // Preserve old defaults whenever grouping_mode == FULL
        if (config_.grouping_mode == GroupingMode::NO_GROUP) {
            // Practical way to suppress actual grouping without modifying Stage3 internals:
            // require absurdly large group size so almost all intervals stay orphan/singleton.
            config.min_group_size = static_cast<int>(intervals.size()) + 1;
            config.enable_symmetry = false;
        } else if (config_.grouping_mode == GroupingMode::NO_SYM) {
            config.min_group_size = 3;
            config.enable_symmetry = false;
        } else {
            config.min_group_size = 3;
            config.enable_symmetry = true;
        }

        config.symmetry_tolerance = 1e-6;
        config.symmetry_position_tol = 1e-3;
        config.delta_position_bits = config_.pos_bits;
        config.delta_a_bits = config_.a_bits;
        config.delta_b_bits = config_.b_bits;
        config.delta_c_bits = config_.c_bits;
        config.adaptive_bitwidth = false;
        config.bitwidth_error_threshold = 1e-10;
        config.verbose = false;

        Stage3Encoder encoder;
        encoder.initialize(intervals, params, samples_, original_function, config);
        encoder.groupIntervals();

        if (config.enable_symmetry) {
            encoder.detectSymmetry();
        }

        encoder.quantizeGroups();

        auto quantized_groups = encoder.getQuantizedGroups();

        printGroupingStatistics(quantized_groups);

        auto compressed = compressIntervalGroups(quantized_groups, config);
        saveCompressedData(compressed, output_dir);
        saveCompressionStats(encoder.getQuantizationStats(),
                            encoder.getGroupingStats(), output_dir);

        if (config_.enable_hardware) {
            generateHardwareMappingForConfig(
                output_dir,
                config_.function_name,
                quantized_groups,
                quant_config,
                false);
        }

        std::cout << "  Groups: " << compressed.total_groups
                  << " | Compression: " << std::fixed << std::setprecision(2)
                  << compressed.compression_ratio << "x\n";

        Stage3Summary summary = summarizeGroups(quantized_groups, compressed.compression_ratio);
        summary.quant_config_name = quant_config.name();
        summary.output_dir = output_dir;

        saveStage3SingleSummary(summary, output_dir + "/stage3_summary.csv");

        return summary;
    }

    double computeRecommendedLengthTolerance(const std::vector<Interval>& intervals) {
        if (intervals.empty()) return 0.001;

        std::vector<double> lengths;
        for (const auto& iv : intervals) {
            double len = iv.get_end() - iv.get_start();
            lengths.push_back(len);
        }

        std::sort(lengths.begin(), lengths.end());

        double min_diff = std::numeric_limits<double>::max();
        for (size_t i = 0; i + 1 < lengths.size(); ++i) {
            double diff = lengths[i + 1] - lengths[i];
            if (diff > 1e-12) {
                min_diff = std::min(min_diff, diff);
            }
        }

        if (min_diff == std::numeric_limits<double>::max()) {
            return 0.001;
        }

        return min_diff * 0.1;
    }
};

//================================================================================
// Main
//================================================================================

void printUsage() {
    std::cout << "\nUsage: optimize_intervals <func> <start> <end> <error> [options]\n\n";
    std::cout << "Supported Functions:\n";
    std::cout << "  Activation: gelu, silu, swish, hardswish (hswish), mish, softplus\n";
    std::cout << "  Standard:   tanh, sigmoid, sin, cos, exp, log, sqrt, erf\n";
    std::cout << "  Variants:   gelu_tanh, gelu_erf, gelu_quick, elu, selu\n";
    std::cout << "  Composite:  hardsigmoid, any arithmetic expression\n\n";

    std::cout << "Options:\n";
    std::cout << "  hw                          Enable hardware file generation\n";
    std::cout << "  -p BITS                     Position bits (default: 16)\n";
    std::cout << "  -a BITS                     Parameter A bits (default: 16)\n";
    std::cout << "  -b BITS                     Parameter B bits (default: 16)\n";
    std::cout << "  -c BITS                     Parameter C bits (default: 16)\n";
    std::cout << "  --interval-mode MODE        optimized | uniform (default: optimized)\n";
    std::cout << "  --uniform-intervals N       Number of uniform intervals when mode=uniform\n";
    std::cout << "                              If omitted, auto-uses optimized interval count\n";
    std::cout << "  --grouping-mode MODE        full | nogroup | nosym (default: full)\n\n";

    std::cout << "Examples:\n";
    std::cout << "  ./optimize_intervals \"gelu(x)\" -5 5 1e-4\n";
    std::cout << "  ./optimize_intervals \"silu(x)\" -6 6 1e-5 hw\n";
    std::cout << "  ./optimize_intervals \"hardswish(x)\" -3 3 1e-4 hw -p 12\n";
    std::cout << "  ./optimize_intervals \"tanh(x)\" 0 3 1e-6\n";
    std::cout << "  ./optimize_intervals \"exp(x)\" 0 1 1e-4 --interval-mode uniform --uniform-intervals 16\n";
    std::cout << "  ./optimize_intervals \"gelu(x)\" -3 3 1e-4 hw --grouping-mode nosym\n\n";
}

int main(int argc, char* argv[]) {
    if (argc < 5) {
        printUsage();
        return 1;
    }

    try {
        PipelineConfig config;
        config.function_expr = argv[1];
        config.x_start = std::stod(argv[2]);
        config.x_end = std::stod(argv[3]);
        config.max_error = std::stod(argv[4]);

        for (int i = 5; i < argc; ++i) {
            std::string arg = argv[i];

            if (arg == "hw") {
                config.enable_hardware = true;
            } else if (arg == "-p" && i + 1 < argc) {
                config.pos_bits = std::stoi(argv[++i]);
            } else if (arg == "-a" && i + 1 < argc) {
                config.a_bits = std::stoi(argv[++i]);
            } else if (arg == "-b" && i + 1 < argc) {
                config.b_bits = std::stoi(argv[++i]);
            } else if (arg == "-c" && i + 1 < argc) {
                config.c_bits = std::stoi(argv[++i]);
            } else if (arg == "--interval-mode" && i + 1 < argc) {
                config.interval_mode = parseIntervalMode(argv[++i]);
            } else if (arg == "--uniform-intervals" && i + 1 < argc) {
                config.uniform_intervals = std::stoi(argv[++i]);
                if (config.uniform_intervals <= 0) {
                    throw std::runtime_error("--uniform-intervals must be > 0");
                }
            } else if (arg == "--grouping-mode" && i + 1 < argc) {
                config.grouping_mode = parseGroupingMode(argv[++i]);
            } else {
                throw std::runtime_error("Unknown option: " + arg);
            }
        }

        std::string normalized_expr = normalizeExpression(config.function_expr);

        // Validate expression early
        double test_val = computeFunctionValue(normalized_expr, 0.0);
        (void)test_val;

        std::cout << "✓ Expression ready: " << config.function_expr << "\n";

        // Canonical function naming for result directory construction
        config.function_name = extractFunctionName(config.function_expr);

        std::cout << "[INFO] Canonical function name: " << config.function_name << "\n";

        std::ostringstream run_name;
        run_name << config.function_name
            << "_" << config.x_start
            << "_" << config.x_end
            << "_" << std::scientific << config.max_error
            << "_" << intervalModeToString(config.interval_mode)
            << "_" << groupingModeToString(config.grouping_mode);

        if (config.interval_mode == IntervalMode::UNIFORM && config.uniform_intervals > 0) {
            run_name << "_u" << config.uniform_intervals;
        }

        config.result_dir = "results/" + sanitizeName(run_name.str());

        std::cout << "[INFO] Result directory: " << config.result_dir << "\n";

        Pipeline pipeline(config);
        return pipeline.run() ? 0 : 1;

    } catch (const std::exception& e) {
        std::cerr << "\n✗ Fatal Error: " << e.what() << "\n\n";
        return 1;
    }
}

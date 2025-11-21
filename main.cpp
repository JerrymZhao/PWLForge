#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <iomanip>
#include <numeric> 
#include <algorithm>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

#include "exprtk.hpp"
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
    
    // Replace special characters with safe equivalents
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
    
    // Remove consecutive underscores
    auto new_end = std::unique(result.begin(), result.end(), 
        [](char a, char b) { return a == '_' && b == '_'; });
    result.erase(new_end, result.end());
    
    // Remove leading/trailing underscores
    if (!result.empty() && result.front() == '_') {
        result.erase(result.begin());
    }
    if (!result.empty() && result.back() == '_') {
        result.pop_back();
    }
    
    return result;
}

std::string extractFunctionName(const std::string& expr) {
    // Extract main function name
    std::string lower_expr = expr;
    std::transform(lower_expr.begin(), lower_expr.end(), lower_expr.begin(), ::tolower);
    
    // Known function patterns
    std::vector<std::string> known_functions = {
        "gelu", "silu", "swish", "hardswish", "hswish", 
        "hardsigmoid", "hsigmoid", "mish", "softplus",
        "elu", "selu", "tanh", "sigmoid", "relu", "sin", "cos",
        "exp", "log", "sqrt", "erf"
    };
    
    for (const auto& func : known_functions) {
        if (lower_expr.find(func) != std::string::npos) {
            return func;
        }
    }
    
    // If no known function, use sanitized expression
    return sanitizeName(expr);
}

std::string normalizeExpression(const std::string& expr) {
    std::string result = expr;
    
    // Replace x^n with (x*x*...) for small integers
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
    
    // Analyze data range first
    DataRange range = QuantizationEvaluator::analyze_data_range(intervals, params);
    
    if (verbose) {
        std::cout << "\n=== Phase 2.5: Processing All Quantization Configurations ===\n";
        range.print();
        std::cout << "\n";
    }

    std::vector<QuantizationConfig> candidate_configs;

    // ========================================================================
    // 1. Floating-Point Standards (6 configurations)
    // ========================================================================

    // Full precision baseline
    candidate_configs.push_back({
        PrecisionConfig{NumericFormat::FP64, 0, 0},
        PrecisionConfig{NumericFormat::FP64, 0, 0}
    });

    // Standard floating-point
    candidate_configs.push_back({
        PrecisionConfig{NumericFormat::FP32, 0, 0}, 
        PrecisionConfig{NumericFormat::FP32, 0, 0}
    });

    candidate_configs.push_back({
        PrecisionConfig{NumericFormat::FP16, 0, 0}, 
        PrecisionConfig{NumericFormat::FP16, 0, 0}
    });

    // Mixed floating-point (endpoint precision >= parameter precision)
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

    // ========================================================================
    // 2. Uniform Fixed-Point (6 configurations, exclude 8-bit)
    // ========================================================================

    // 16-bit: Embedded audio DSP, simple control systems
    candidate_configs.push_back(
        QuantizationConfig::create_auto_fixed(16, 16, range)
    );

    // 24-bit: Professional audio processing, 24-bit DSP chips
    candidate_configs.push_back(
        QuantizationConfig::create_auto_fixed(24, 24, range)
    );

    // 32-bit: Standard DSP processing, graphics, financial computing
    candidate_configs.push_back(
        QuantizationConfig::create_auto_fixed(32, 32, range)
    );

    // 40-bit: Extended precision DSP, scientific instruments
    candidate_configs.push_back(
        QuantizationConfig::create_auto_fixed(40, 40, range)
    );

    // 48-bit: High-precision fixed-point (FP32 replacement)
    candidate_configs.push_back(
        QuantizationConfig::create_auto_fixed(48, 48, range)
    );

    // 64-bit: Ultra-high precision fixed-point (FP64 replacement)
    candidate_configs.push_back(
        QuantizationConfig::create_auto_fixed(64, 64, range)
    );

    // ========================================================================
    // 3. Mixed Fixed-Point (3 representative configurations)
    //    Principle: endpoint_bits >= param_bits (endpoint needs higher precision)
    // ========================================================================

    // Aggressive parameter compression
    candidate_configs.push_back(
        QuantizationConfig::create_auto_fixed(64, 32, range)  // 64-bit endpoint + 32-bit param
    );

    candidate_configs.push_back(
        QuantizationConfig::create_auto_fixed(48, 24, range)  // 48-bit endpoint + 24-bit param
    );

    candidate_configs.push_back(
        QuantizationConfig::create_auto_fixed(32, 16, range)  // 32-bit endpoint + 16-bit param
    );

    // ========================================================================
    // Total: 15 configurations
    //   - 6 floating-point (3 uniform + 3 mixed)
    //   - 6 uniform fixed-point (16/24/32/40/48/64-bit)
    //   - 3 mixed fixed-point (strategic compression)
    // ========================================================================

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
    
public:
    Pipeline(const PipelineConfig& config) : config_(config) {}
    
    bool run() {
        try {
            std::cout << "\n╔════════════════════════════════════════════════════════════╗\n";
            std::cout << "║  Function Approximation Pipeline                          ║\n";
            std::cout << "╚════════════════════════════════════════════════════════════╝\n\n";
            
            std::cout << "Function:    " << config_.function_expr << "\n";
            std::cout << "Range:       [" << config_.x_start << ", " << config_.x_end << "]\n";
            std::cout << "Max Error:   " << std::scientific << config_.max_error << "\n";
            std::cout << "Output Dir:  " << config_.result_dir << "\n\n";
            
            createDirectory("results");
            createDirectory(config_.result_dir);
            
            runPhase1and2();
            runPhase2_5();
            runStage3ForAllConfigs();
            
            std::cout << "\n╔════════════════════════════════════════════════════════════╗\n";
            std::cout << "║  Pipeline Complete ✓                                       ║\n";
            std::cout << "╚════════════════════════════════════════════════════════════╝\n\n";
            std::cout << "Total Intervals:     " << intervals_.size() << "\n";
            std::cout << "Quant Configs:       " << all_quantization_results_.size() << "\n";
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
        
        // Normalize expression
        std::string expr = normalizeExpression(config_.function_expr);
        
        FittingParametersConfig fit_config;
        fit_config.epsilon_start = config_.max_error / 10.0;
        fit_config.epsilon_end = config_.max_error / 2.0;
        fit_config.acceptable_error = config_.max_error;
        fit_config.min_unit_length = (config_.x_end - config_.x_start) / 100000.0;
        fit_config.merge_relax_factor = 1.0;
        fit_config.epsilon_steps = 1;
        
        OptimizationResult opt_result = optimizeIntervals(
            config_.x_start, config_.x_end, expr, fit_config);
        
        intervals_ = opt_result.intervals;
        
        fitAllSegments(expr, intervals_, fit_params_, config_.max_error);
        
        saveIntervalsToFile(intervals_, config_.result_dir + "/intervals_fp64.csv");
        saveFitParametersToFile(fit_params_, config_.result_dir + "/fit_params_fp64.csv");
        
        // Generate samples
        int num_samples = std::max(1000, (int)intervals_.size() * 10);
        double dx = (config_.x_end - config_.x_start) / (num_samples - 1);
        
        std::ofstream sf(config_.result_dir + "/samples.csv");
        sf << "index,x,y\n" << std::scientific << std::setprecision(15);
        for (int i = 0; i < num_samples; ++i) {
            double x = config_.x_start + i * dx;
            double y = computeFunctionValue(expr, x);
            samples_.push_back(y);
            sf << i << "," << x << "," << y << "\n";
        }
        
        std::cout << "✓ Generated " << intervals_.size() << " intervals (FP64 baseline)\n\n";
    }
    
    void runPhase2_5() {
        std::cout << "=== Phase 2.5: Quantization Evaluation ===\n";
        
        // Normalize expression
        std::string expr = normalizeExpression(config_.function_expr);
        
        // Use enhanced parser via lambda
        auto original_function = [expr](double x) -> double {
            return computeFunctionValue(expr, x);
        };
        
        // Process all quantization configs (auto bit allocation)
        all_quantization_results_ = processAllQuantizationConfigs(
            intervals_, fit_params_, original_function, true);
        
        // Save summary
        saveAllConfigsSummary(all_quantization_results_, 
                             config_.result_dir + "/quantization_summary.csv");
        
        std::cout << "\n✓ Quantization evaluation complete\n";
        std::cout << "  Tested " << all_quantization_results_.size() << " configurations\n\n";
    }
    
    void runStage3ForAllConfigs() {
        std::cout << "=== Stage 3: Group Encoding & Compression ===\n";
        
        // Normalize expression
        std::string expr = normalizeExpression(config_.function_expr);
        
        // Use enhanced parser via lambda
        auto original_function = [expr](double x) -> double {
            return computeFunctionValue(expr, x);
        };
        
        for (size_t i = 0; i < all_quantization_results_.size(); ++i) {
            const auto& quant_result = all_quantization_results_[i];
            
            std::cout << "\n[" << (i+1) << "/" << all_quantization_results_.size()
                      << "] " << quant_result.config_name << "\n";
            
            std::string config_output_dir = config_.result_dir + "/stage3_" + quant_result.config_name;
            createDirectory(config_output_dir);
            
            // Prepare quantized intervals
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
            
            runStage3Single(
                quantized_intervals, 
                quantized_params, 
                original_function, 
                config_output_dir,
                quant_result.config);
        }
        
        std::cout << "\n✓ All Stage 3 configurations complete\n";
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

    void runStage3Single(
        const std::vector<Interval>& intervals,
        const std::vector<FitParameters>& params,
        const std::function<double(double)>& original_function,
        const std::string& output_dir,
        const QuantizationConfig& quant_config) {
        
        Stage3Config config;
        
        // Auto compute tolerance
        double auto_tolerance = computeRecommendedLengthTolerance(intervals);
        config.length_tolerance = std::max(1e-6, std::min(0.01, auto_tolerance));
        
        config.min_group_size = 3;
        config.enable_symmetry = true;
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
        encoder.detectSymmetry();
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
        for (size_t i = 0; i < lengths.size() - 1; ++i) {
            double diff = lengths[i+1] - lengths[i];
            if (diff > 1e-12) {
                min_diff = std::min(min_diff, diff);
            }
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
    std::cout << "  hw          Enable hardware file generation\n";
    std::cout << "  -p BITS     Position bits (default: 16)\n";
    std::cout << "  -a BITS     Parameter A bits (default: 16)\n";
    std::cout << "  -b BITS     Parameter B bits (default: 16)\n";
    std::cout << "  -c BITS     Parameter C bits (default: 16)\n\n";
    std::cout << "Examples:\n";
    std::cout << "  ./optimize_intervals \"gelu(x)\" -5 5 1e-4\n";
    std::cout << "  ./optimize_intervals \"silu(x)\" -6 6 1e-5 hw\n";
    std::cout << "  ./optimize_intervals \"hardswish(x)\" -3 3 1e-4 hw -p 12\n";
    std::cout << "  ./optimize_intervals \"tanh(x)\" 0 3 1e-6\n\n";
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
        
        // Parse optional args
        for (int i = 5; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "hw") config.enable_hardware = true;
            else if (arg == "-p" && i+1 < argc) config.pos_bits = std::stoi(argv[++i]);
            else if (arg == "-a" && i+1 < argc) config.a_bits = std::stoi(argv[++i]);
            else if (arg == "-b" && i+1 < argc) config.b_bits = std::stoi(argv[++i]);
            else if (arg == "-c" && i+1 < argc) config.c_bits = std::stoi(argv[++i]);
        }

        // Test expression - use enhanced parser from common_utils.hpp
        std::string normalized_expr = normalizeExpression(config.function_expr);
        double test_val = computeFunctionValue(normalized_expr, 0.0);
        (void)test_val;
        std::cout << "✓ Expression ready: " << config.function_expr << "\n";
        
        // Extract function name
        config.function_name = extractFunctionName(config.function_expr);
        
        // Create output directory
        std::ostringstream oss;
        oss << "results/" << config.function_name 
            << "_" << config.x_start 
            << "_" << config.x_end 
            << "_" << std::scientific << config.max_error;
        
        config.result_dir = sanitizeName(oss.str());
        
        // Run pipeline
        Pipeline pipeline(config);
        return pipeline.run() ? 0 : 1;
        
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Fatal Error: " << e.what() << "\n\n";
        return 1;
    }
}
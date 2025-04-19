#include <iostream>
#include <vector>
#include <functional>
#include <cmath>
#include <sys/stat.h>
#include "interval_optimizer.hpp"
#include "function_fitter.hpp"
#include "interval_group_compressor.hpp"
#include "hw_mapping.hpp"

// double relu(double x) { return x > 0 ? x : 0; }
// double gelu(double x) { return 0.5 * x * (1.0 + std::erf(x / std::sqrt(2.0))); }
// double swishglu(double x) { return x / (1.0 + std::exp(-x)); }

template <typename T>
struct relu_fn : public exprtk::igeneric_function<T>
{
    std::size_t min_param_count() { return 1; }
    std::size_t max_param_count() { return 1; }
    inline T operator()(const std::vector<T>& params)
    {
        T x = params[0];
        return (x > T(0)) ? x : T(0);
    }
};

template <typename T>
struct gelu_fn : public exprtk::igeneric_function<T>
{
    std::size_t min_param_count() { return 1; }
    std::size_t max_param_count() { return 1; }
    inline T operator()(const std::vector<T>& params)
    {
        T x = params[0];
        return T(0.5) * x * (T(1.0) + std::erf(x / std::sqrt(T(2.0))));
    }
};

template <typename T>
struct swishglu_fn : public exprtk::igeneric_function<T>
{
    std::size_t min_param_count() { return 1; }
    std::size_t max_param_count() { return 1; }
    inline T operator()(const std::vector<T>& params)
    {
        T x = params[0];
        return x / (T(1.0) + std::exp(-x));
    }
};

void replaceAll(std::string& str, const std::string& from, const std::string& to) {
    if(from.empty())
        return;
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        size_t open_paren = str.find("(", start_pos);
        if(open_paren == std::string::npos) break;

        int count = 1;
        size_t end_pos = open_paren + 1;
        while(end_pos < str.size() && count > 0) {
            if(str[end_pos] == '(') count++;
            else if(str[end_pos] == ')') count--;
            end_pos++;
        }

        if(count != 0) {
            std::cerr << "Mismatched parentheses in the expression!" << std::endl;
            return;
        }

        std::string arg = str.substr(open_paren + 1, end_pos - open_paren - 2);

        if(from == "relu") {
            // relu(x) => (x > 0 ? x : 0)
            std::string replacement = "(" + arg + " > 0 ? " + arg + " : 0)";
            str.replace(start_pos, end_pos - start_pos, replacement);
            start_pos += replacement.length();
        } else if(from == "gelu") {
            // gelu(x) => 0.5 * x * (1.0 + erf(x / sqrt(2.0)))
            std::string replacement = "0.5 * " + arg + " * (1.0 + erf(" + arg + " / sqrt(2.0)))";
            str.replace(start_pos, end_pos - start_pos, replacement);
            start_pos += replacement.length();
        } else if(from == "swishglu") {
            // swishglu(x) => x / (1.0 + exp(-x))
            std::string replacement = "(" + arg + " / (1.0 + exp(-" + arg + "))";
            str.replace(start_pos, end_pos - start_pos, replacement);
            start_pos += replacement.length();
        }
        else {
            start_pos = end_pos;
        }
    }
}

void createDirectory(const std::string& path) {
    mkdir(path.c_str(), 0777);
}

struct RangeCoverage {
    double start;
    double end;
    bool is_covered;
};

inline void verifyIntervalCoverage(
    const std::string& stage,
    const std::vector<Interval>& intervals,
    const std::vector<IntervalGroup>& groups,
    double function_start,
    double function_end) {
    
    std::vector<std::pair<double, double>> ranges;
    
    if (stage == "Initial" || stage == "Split" || stage == "Merge") {
        // Process raw intervals
        for (const auto& interval : intervals) {
            ranges.emplace_back(interval.start, interval.end);
        }
    } else {
        // Process compressed groups
        for (const auto& group : groups) {
            if (group.storage_type == ORPHAN_GROUP) {
                // Handle ORPHAN_GROUP: Use original intervals directly
                for (const auto& delta : group.delta_encodings) {
                    size_t idx = delta.original_index;
                    if (idx < intervals.size()) {
                        const auto& orig_iv = intervals[idx];
                        ranges.emplace_back(orig_iv.start, orig_iv.end);
                    } else {
                        std::cerr << "Invalid original_index in ORPHAN_GROUP: " 
                                  << idx << "/" << intervals.size() << "\n";
                    }
                }
            } else {
                // Handle normal compressed groups
                for (const auto& delta : group.delta_encodings) {
                    // Quantize and dequantize delta_start to simulate compression effect
                    int quantized_delta_start = static_cast<int>(
                        std::round(delta.delta_start / group.start_scale_factor));
                    double dequantized_delta_start = quantized_delta_start * group.start_scale_factor;
                    
                    double current_start = group.base_interval.start + dequantized_delta_start;
                    double current_end = current_start + group.length;
                    
                    ranges.emplace_back(current_start, current_end);
                }
            }
        }
    }
    
    // Sort ranges by start
    std::sort(ranges.begin(), ranges.end(),
              [](const std::pair<double, double>& a, const std::pair<double, double>& b) -> bool {
                  return a.first < b.first;
              });
    
    // Identify gaps
    std::vector<std::pair<double, double>> gaps;
    double current_end = function_start;
    
    for (const auto& range : ranges) {
        if (range.first > current_end + 1e-10) {
            gaps.emplace_back(current_end, range.first);
        }
        current_end = std::max(current_end, range.second);
    }
    
    if (current_end < function_end - 1e-10) {
        gaps.emplace_back(current_end, function_end);
    }
    
    // Print coverage analysis
    std::cout << "\nInterval Coverage Analysis (" << stage << "):\n"
              << "------------------------\n"
              << "Function range: [" << function_start << ", " << function_end << "]\n"
              << "Total intervals: " << intervals.size() << "\n";
    
    if (!gaps.empty()) {
        std::cout << "WARNING: Found " << gaps.size() << " gaps in " << stage << " stage:\n";
        // for (const auto& gap : gaps) {
        //     std::cout << "Gap: [" << gap.first << ", " << gap.second << "]\n";
        // }
    } else {
        std::cout << "Complete coverage achieved in " << stage << " stage!\n";
    }
}

struct ParetoResult {
    double epsilon;
    double min_unit_length;
    size_t final_interval_count;
    double final_error;
    double compression_ratio;
};

std::string getFunctionName(const std::string& expression_str) {
    // Find the first occurrence of '('
    size_t pos = expression_str.find('(');
    if (pos != std::string::npos) {
        // Return everything before the first '('
        return expression_str.substr(0, pos);
    }
    // If there are no parentheses, return the original string
    return expression_str;
}

struct FunctionProcessingResult {
    std::vector<IntervalGroup> compressed_groups;
    std::vector<Interval> intervals;
    std::vector<FitParameters> fit_params;

    // Hardware implementation details
    int optimized_frac_bits;
    double optimized_scale_factor;
    double final_error;
    bool hw_verification_success;
    int input_width;
    int output_width;
};

int roundToStandardWidth(int width, bool allow_nonstandard = false) {
    // If non-standard widths are allowed (for special cases), return as-is
    if (allow_nonstandard)
        return width;
    
    // Standard hardware-friendly bit widths
    // For widths up to 10 bits, use 8 or 10 based on proximity
    if (width <= 8) return 8;
    if (width <= 10) return 10;  // Added 10-bit option for resource efficiency
    
    // For widths between 10 and 16 bits, use the more appropriate option
    if (width <= 13) return 10;  // Closer to 10 than 16
    if (width <= 16) return 16;
    
    // For widths between 16 and 24 bits, consider whether 24 is more appropriate
    if (width <= 24) {
        // If close to 24 bits, use 24 instead of 32 to reduce resource waste
        return 24;
    }
    
    // For widths close to 32 bits, use the 32-bit standard
    if (width <= 32) return 32;
    
    // For rare cases exceeding 32 bits, align to 32-bit boundaries
    return ((width + 31) / 32) * 32;
}

// 计算最佳位宽的改进函数
int calculateOptimalBitWidths(const std::string& expression_str, 
                             double start, double end, 
                             double target_error, 
                             int& input_width, 
                             int& output_width) {
    
    // 检查是否为窄范围函数 (例如 tanh、sin、cos在小范围内)
    bool is_narrow_range = false;
    double range_size = end - start;
    
    if (range_size <= 1.0 && 
        (expression_str.find("tanh") != std::string::npos || 
         expression_str.find("sin") != std::string::npos || 
         expression_str.find("cos") != std::string::npos || 
         expression_str.find("sigmoid") != std::string::npos ||
         expression_str.find("1/(1+exp") != std::string::npos)) {
        is_narrow_range = true;
    }
    
    // 基于误差目标计算所需分数位
    int frac_bits;
    bool is_transcendental = (expression_str.find("tanh") != std::string::npos || 
                            expression_str.find("sin") != std::string::npos || 
                            expression_str.find("cos") != std::string::npos ||
                            expression_str.find("exp") != std::string::npos ||
                            expression_str.find("log") != std::string::npos);
    
    // 理论上需要的分数位：log2(1/target_error)
    int theory_bits = static_cast<int>(std::ceil(std::log2(1.0 / target_error)));
    
    // 为超越函数提供额外保护，但避免过度分配
    if (is_transcendental) {
        // 修改位宽分配逻辑，避免过度分配
        if (target_error <= 1e-7) {
            frac_bits = std::min(24, theory_bits + 2);
        } else if (target_error <= 1e-6) {
            frac_bits = std::min(20, theory_bits + 2);
        } else if (target_error <= 1e-5) {
            frac_bits = std::min(18, theory_bits + 1);
        } else if (target_error <= 1e-4) {
            // 对于10^-4级别的误差，理论上需要14位左右
            frac_bits = std::min(16, theory_bits + 1);
        } else if (target_error <= 1e-3) {
            frac_bits = std::min(14, theory_bits + 1);
        } else {
            frac_bits = std::min(12, theory_bits);
        }
        
        // 对于窄范围函数，可以进一步减少位宽
        if (is_narrow_range) {
            frac_bits = std::min(frac_bits, theory_bits + 2);
        }
    } else {
        // 非超越函数通常需要更少的额外位
        if (target_error <= 1e-7) {
            frac_bits = std::min(20, theory_bits + 1);
        } else if (target_error <= 1e-6) {
            frac_bits = std::min(18, theory_bits + 1);
        } else if (target_error <= 1e-5) {
            frac_bits = std::min(16, theory_bits);
        } else if (target_error <= 1e-4) {
            frac_bits = std::min(14, theory_bits);
        } else if (target_error <= 1e-3) {
            frac_bits = std::min(12, theory_bits);
        } else {
            frac_bits = std::min(10, theory_bits);
        }
    }
    
    // 根据函数类型和范围进行调整
    double function_range = 0.0;
    
    if (expression_str.find("tanh") != std::string::npos) {
        function_range = 2.0; // tanh的范围是[-1, 1]
    } else if (expression_str.find("sin") != std::string::npos || 
            expression_str.find("cos") != std::string::npos) {
        function_range = 2.0; // sin/cos的范围是[-1, 1]
    } else if (expression_str.find("exp") != std::string::npos) {
        function_range = std::exp(end) - std::exp(start);
        // 仅对大范围指数增加位数
        if (function_range > 1000) frac_bits += 3;
        else if (function_range > 100) frac_bits += 1;
    } else if (expression_str.find("log") != std::string::npos) {
        if (start <= 0.01) frac_bits += 1; // 对于接近0的对数，增加精度
    }
    
    // 对于非常小或非常大的范围，适度调整
    if (range_size > 10.0) {
        frac_bits += 1;
    } else if (range_size < 0.1) {
        frac_bits += 1;
    }

    // 确保超越函数的最小精度
    if (is_transcendental && frac_bits < 10) {
        frac_bits = 10;
    }
    
    // 计算输入整数位
    int input_int_bits;
    double abs_max_input = std::max(std::abs(start), std::abs(end));
    
    if (abs_max_input < 1.0) {
        input_int_bits = 2; // 1符号位 + 1整数位
    } else {
        input_int_bits = std::ceil(std::log2(abs_max_input)) + 1; // +1用于符号位
    }
    
    // 计算输出整数位
    int output_int_bits;
    
    if (expression_str.find("tanh") != std::string::npos ||
        expression_str.find("sin") != std::string::npos ||
        expression_str.find("cos") != std::string::npos ||
        expression_str.find("sigmoid") != std::string::npos ||
        expression_str.find("1/(1+exp") != std::string::npos) {
        // 范围是[-1,1]或[0,1]
        output_int_bits = 2; // 1符号位 + 1整数位
    }
    else if (expression_str.find("exp") != std::string::npos) {
        // 指数函数
        double max_output = std::exp(end);
        output_int_bits = std::ceil(std::log2(max_output)) + 1;
    }
    else if (expression_str.find("log") != std::string::npos) {
        // 对数函数
        double min_val = start > 0 ? start : 0.00001; // 避免log(0)
        double max_output = std::max(std::abs(std::log(min_val)), std::abs(std::log(end)));
        output_int_bits = std::ceil(std::log2(max_output)) + 1;
    }
    else if (expression_str.find("relu") != std::string::npos) {
        // ReLU: [0, max(0,x)]
        double max_output = std::max(0.0, end);
        output_int_bits = max_output < 1.0 ? 2 : std::ceil(std::log2(max_output)) + 1;
    }
    else {
        // 默认情况，稍微保守一点
        output_int_bits = input_int_bits + 1;
    }
    
    // 对于特殊的窄范围函数，可以使用更紧凑的表示
    bool use_custom_width = false;
    if (is_narrow_range && target_error >= 1e-5) {
        use_custom_width = true;
    }
    
    // 计算总位宽
    input_width = input_int_bits + frac_bits;
    output_width = output_int_bits + frac_bits;
    
    // 对于窄范围函数允许非标准位宽，否则取整到标准宽度
    input_width = roundToStandardWidth(input_width, use_custom_width);
    output_width = roundToStandardWidth(output_width, use_custom_width);
    
    // 对于tanh(x)这样的函数，确保在简单情况下不会超过16位
    if (is_narrow_range && range_size <= 1.0 && target_error >= 1e-5) {
        if (input_width > 16) input_width = 16;
        if (output_width > 16) output_width = 16;
    }
    
    return frac_bits;
}

FunctionProcessingResult processFunction(const std::string& expression_str,
                                        const std::string& results_dir,
                                        double start, double end,
                                        size_t num_points,
                                        const FittingParametersConfig& config,
                                        const std::vector<std::string>& custom_functions) {
    FunctionProcessingResult result;

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║ PROCESSING FUNCTION: " << std::left << std::setw(40) << expression_str << "║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    
    // ====== STAGE 1: SETUP AND INITIALIZATION ======
    std::cout << "\n[STAGE 1: SETUP AND INITIALIZATION]" << std::endl;
    
    // Create a clean directory name
    std::string clean_name = getFunctionName(expression_str);
    std::string func_dir = results_dir + "/" + clean_name;
    createDirectory(func_dir);
    std::cout << "• Created directory: " << func_dir << std::endl;

    // Determine if this is a custom function
    bool is_custom = false;
    std::string parsed_expr = expression_str;
    for (const auto& func : custom_functions) {
        if (parsed_expr.find(func + "(") != std::string::npos) {
            is_custom = true;
            break;
        }
    }
    std::cout << "• Function type: " << (is_custom ? "Custom" : "Built-in") << std::endl;

    // Setup symbol table with function definitions
    double x = 0.0;
    exprtk::symbol_table<double> symbol_table;
    symbol_table.add_constants();
    symbol_table.add_variable("x", x);

    // Add custom functions
    for (const auto& func : custom_functions) {
        if (func == "relu") {
            relu_fn<double> relu_f;
            symbol_table.add_function("relu", relu_f);
        } else if (func == "gelu") {
            gelu_fn<double> gelu_f;
            symbol_table.add_function("gelu", gelu_f);
        } else if (func == "swishglu") {
            swishglu_fn<double> swish_f;
            symbol_table.add_function("swishglu", swish_f);
        }
    }
    
    // Parse the expression
    exprtk::expression<double> expression;
    expression.register_symbol_table(symbol_table);
    exprtk::parser<double> parser;

    if (!parser.compile(parsed_expr, expression)) {
        std::cerr << "ERROR: Failed to parse expression: " << parser.error() << std::endl;
        for (std::size_t i = 0; i < parser.error_count(); ++i) {
            exprtk::parser_error::type error = parser.get_error(i);
            std::cerr << "  - " << std::string(error.diagnostic) << std::endl;
        }
        return result; // Return empty result on error
    }
    std::cout << "• Successfully parsed expression" << std::endl;
    std::cout << "• Target accuracy: " << config.acceptable_error << std::endl;
    std::cout << "• Input range: [" << start << ", " << end << "]" << std::endl;

    // Initialize merge parameters
    MergeParams merge_params;
    merge_params.base_len_tol = 0.15 + 0.05 * log10(config.acceptable_error * 1e4);
    merge_params.base_continuity_tol = 0.05 * (config.acceptable_error * 1e3);
    merge_params.curvature_base = 0.8 / (1.0 + 5.0 * config.acceptable_error);
    merge_params.slope_base = 0.3 * sqrt(config.acceptable_error * 1e3);
    merge_params.curvature_sensitivity = 1.5 + 0.5 * sin(config.acceptable_error * 1e4);

    // ====== STAGE 2: INITIAL INTERVAL GENERATION ======
    std::cout << "\n[STAGE 2: INITIAL INTERVAL GENERATION]" << std::endl;
    double initial_unit_length = (end - start) / num_points;
    OptimizationConfig opt_config;
    
    std::cout << "• Generating initial intervals with unit length " << initial_unit_length << "..." << std::endl;
    std::vector<Interval> initial_intervals = 
        generateInitialIntervals(start, end, num_points, initial_unit_length, parsed_expr, opt_config);
    verifyIntervalCoverage("Initial", initial_intervals, {}, start, end);
    
    size_t initial_interval_count = initial_intervals.size();
    std::cout << "• Generated " << initial_interval_count << " initial intervals" << std::endl;

    // ====== STAGE 3: INTERVAL REFINEMENT ======
    std::cout << "\n[STAGE 3: INTERVAL REFINEMENT]" << std::endl;
    std::cout << "• Splitting intervals to improve accuracy..." << std::endl;
    
    // Generate fine intervals
    std::vector<Interval> fine_intervals;
    for (const auto& interval : initial_intervals) {
        splitInterval(interval, config.epsilon_start, config.min_unit_length, 
                     parsed_expr, fine_intervals, config.acceptable_error, 1.0);
    }
    verifyIntervalCoverage("Refined", fine_intervals, {}, start, end);
    std::cout << "• Generated " << fine_intervals.size() << " refined intervals" << std::endl;

    // ====== STAGE 4: INTERVAL OPTIMIZATION ======
    std::cout << "\n[STAGE 4: INTERVAL OPTIMIZATION]" << std::endl;
    std::cout << "• Starting optimization with:" << std::endl;
    std::cout << "  - Initial intervals: " << initial_interval_count << std::endl;
    std::cout << "  - Epsilon range: " << config.epsilon_start << " to " << config.epsilon_end << std::endl;
    std::cout << "  - Steps: " << config.epsilon_steps << std::endl;
    std::cout << "  - Target approximation error: " << config.acceptable_error << std::endl;

    // Initialize optimization variables
    double best_epsilon = config.epsilon_start;
    double best_compression_ratio = 1.0;
    double best_approximation_error = std::numeric_limits<double>::max();
    std::vector<Interval> best_intervals;
    std::vector<FitParameters> best_fit_params_list;
    std::vector<CompressedFitParameters> best_compressed_params_list;

    std::vector<ParetoResult> pareto_results;
    
    std::cout << "• Running optimization iterations..." << std::endl;
    // Optimization loop
    for (size_t i = 0; i < config.epsilon_steps; ++i) {
        // Handle single step case to avoid division by zero
        double epsilon = (config.epsilon_steps == 1) ? 
                        config.epsilon_start :
                        config.epsilon_start + (config.epsilon_end - config.epsilon_start) * 
                        i / (config.epsilon_steps - 1);
        
        std::cout << "  [Iteration " << (i+1) << "/" << config.epsilon_steps << "] Testing epsilon=" << epsilon << std::endl;
        
        std::vector<Interval> merged_intervals = initial_intervals;
        mergeIntervals(merged_intervals, epsilon, config.acceptable_error, parsed_expr, merge_params, config.min_unit_length, 1.0);
        std::cout << "    - Merged intervals: " << merged_intervals.size() << std::endl;
        verifyIntervalCoverage("Merged", merged_intervals, {}, start, end);
        
        std::vector<FitParameters> fit_params_list;
        fitAllSegments(parsed_expr, merged_intervals, fit_params_list, config.acceptable_error);
        
        std::vector<CompressedFitParameters> compressed_params_list;
        compressFitParameters(fit_params_list, merged_intervals, 
                            compressed_params_list, config.acceptable_error);
        
        double approximation_error = 
            evaluateCompressedError(parsed_expr, merged_intervals, compressed_params_list);
        double compression_ratio = 
            static_cast<double>(merged_intervals.size()) / initial_interval_count;
        
        ParetoResult pr;
        pr.epsilon = epsilon;
        pr.min_unit_length = config.min_unit_length;
        pr.final_interval_count = merged_intervals.size();
        pr.final_error = approximation_error;
        pr.compression_ratio = compression_ratio;
        pareto_results.push_back(pr);

        std::cout << "    - Compression ratio: " << compression_ratio << "x" << std::endl;
        std::cout << "    - Approximation error: " << approximation_error;
        
        if (approximation_error <= config.acceptable_error && 
            compression_ratio < best_compression_ratio) {
            best_epsilon = epsilon;
            best_compression_ratio = compression_ratio;
            best_approximation_error = approximation_error;
            best_intervals = merged_intervals;
            best_fit_params_list = fit_params_list;
            best_compressed_params_list = compressed_params_list;
            std::cout << " ✓ (New best solution)" << std::endl;
        } else {
            std::cout << std::endl;
        }
    }

    // ====== STAGE 5: PARETO ANALYSIS ======
    std::cout << "\n[STAGE 5: PARETO ANALYSIS]" << std::endl;
    
    // Calculate Pareto front results
    std::vector<ParetoResult> pareto_front;
    for (size_t i = 0; i < pareto_results.size(); i++) {
        bool dominated = false;
        for (size_t j = 0; j < pareto_results.size(); j++) {
            if (j != i) {
                if (pareto_results[j].final_error <= pareto_results[i].final_error &&
                    pareto_results[j].compression_ratio <= pareto_results[i].compression_ratio &&
                    (pareto_results[j].final_error < pareto_results[i].final_error ||
                     pareto_results[j].compression_ratio < pareto_results[i].compression_ratio)) {
                    dominated = true;
                    break;
                }
            }
        }
        if (!dominated) {
            pareto_front.push_back(pareto_results[i]);
        }
    }

    std::cout << "• Pareto-Optimal Solutions:" << std::endl;
    std::cout << "  ----------------------------------------------------------------------" << std::endl;
    std::cout << "  | Epsilon | Intervals | Compression Ratio | Approximation Error      |" << std::endl;
    std::cout << "  ----------------------------------------------------------------------" << std::endl;
    
    for (const auto& pr : pareto_front) {
        std::cout << "  | " << std::setw(7) << pr.epsilon << " | " 
                  << std::setw(9) << pr.final_interval_count << " | "
                  << std::setw(17) << pr.compression_ratio << " | "
                  << std::setw(24) << pr.final_error << " |" << std::endl;
    }
    std::cout << "  ----------------------------------------------------------------------" << std::endl;

    // Save Pareto results to file
    std::string pareto_file = func_dir + "/pareto_front.csv";
    std::ofstream pareto_out(pareto_file);
    if (pareto_out.is_open()) {
        pareto_out << "Epsilon,FinalIntervals,CompressionRatio,ApproximationError\n";
        for (const auto& pr : pareto_results) {
            pareto_out << pr.epsilon << ","
                       << pr.final_interval_count << "," 
                       << pr.compression_ratio << ","
                       << pr.final_error << "\n";
        }
        pareto_out.close();
        std::cout << "• Pareto front results saved to: " << pareto_file << std::endl;
    }

    // ====== STAGE 6: SOLUTION SELECTION ======
    std::cout << "\n[STAGE 6: SOLUTION SELECTION]" << std::endl;
    
    // Use the best solution or find the best if none was found
    if (best_intervals.empty()) {
        auto min_error_it = std::min_element(pareto_results.begin(), pareto_results.end(),
            [](const ParetoResult& a, const ParetoResult& b) { return a.final_error < b.final_error; });
    
        // Use the precomputed values directly from the best ParetoResult
        best_epsilon = min_error_it->epsilon;
        best_compression_ratio = min_error_it->compression_ratio;
        best_approximation_error = min_error_it->final_error;
        
        std::cout << "• No solution met target error. Using best error solution:" << std::endl;
        std::cout << "  - Epsilon: " << best_epsilon << std::endl; 
        std::cout << "  - Approximation error: " << best_approximation_error << std::endl;
        
        // Retrieve the best result by running just one iteration with the selected epsilon
        std::vector<Interval> merged_intervals = initial_intervals;
        mergeIntervals(merged_intervals, best_epsilon, config.acceptable_error, 
                      parsed_expr, merge_params, config.min_unit_length, 1.0);
        best_intervals = merged_intervals;
        
        // Only compute fit parameters once
        fitAllSegments(parsed_expr, best_intervals, best_fit_params_list, config.acceptable_error);
        
        // Save parameters and log results
        saveFitParametersToFile(best_fit_params_list, func_dir + "/fit_params.csv");
        double fit_error = evaluateError(parsed_expr, best_intervals, best_fit_params_list);
        std::cout << "  - Raw fit error: " << fit_error << std::endl;
    } else {
        std::cout << "• Using best solution found:" << std::endl;
        std::cout << "  - Epsilon: " << best_epsilon << std::endl;
        std::cout << "  - Intervals: " << best_intervals.size() << " (from " << initial_interval_count << ")" << std::endl;
        std::cout << "  - Compression ratio: " << best_compression_ratio << "x" << std::endl;
        std::cout << "  - Approximation error: " << best_approximation_error << std::endl;
        std::cout << "  - Target error: " << config.acceptable_error << std::endl;
        std::cout << "  - Status: " << (best_approximation_error <= config.acceptable_error ? "PASS" : "FAIL") << std::endl;
    }

    // ====== STAGE 7: HARDWARE MODEL GENERATION ======
    // Save results with compressed groups
    if (!best_intervals.empty()) {
        std::cout << "\n[STAGE 7: HARDWARE MODEL GENERATION]" << std::endl;
        
        // Save compressed parameters
        std::string params_file = func_dir + "/compressed_params.csv";
        std::string groups_file = func_dir + "/compressed_groups.csv";
        std::string metrics_file = func_dir + "/optimization_metrics.txt";

        std::cout << "• Creating parameter groups for hardware implementation..." << std::endl;

        // std::cout << "\n===== Debug: Detailed intervals before compression =====\n";
        // std::cout << "Total intervals: " << best_intervals.size() << std::endl;
        // for (size_t i = 0; i < best_intervals.size(); i++) {
        //     const auto& interval = best_intervals[i];
        //     const auto& params = best_fit_params_list[i];
            
        //     std::cout << "Interval[" << i << "]: ["
        //               << std::fixed << std::setprecision(6) << interval.start << ", " 
        //               << interval.end << "], "
        //               << "Length: " << (interval.end - interval.start) << ", "
        //               << "Hessian: " << interval.hessian << ", "
        //               << "Level: " << interval.level << std::endl;
            
        //     std::cout << "  Fit Parameters: method=" << static_cast<int>(params.method)
        //               << ", a=" << std::setprecision(8) << params.a
        //               << ", b=" << params.b 
        //               << ", c=" << params.c
        //               << ", order=" << params.order
        //               << ", range=[" << params.range_start << ", " << params.range_end << "]" << std::endl;
        // }
        // std::cout << "=================================================\n" << std::endl;
        
        std::vector<IntervalGroup> compressed_groups;
        groupAndCompressIntervals(expression_str, best_intervals, best_fit_params_list, compressed_groups, config.acceptable_error);
        std::cout << "\n===== Debug: Compressed interval groups =====\n";
        std::cout << "Total groups: " << compressed_groups.size() << std::endl;
        for (size_t i = 0; i < compressed_groups.size(); i++) {
            const auto& group = compressed_groups[i];
            bool is_orphan = (group.storage_type == ORPHAN_GROUP);
            
            // 计算量化后的base_b和base_c值
            int16_t q_base_b = static_cast<int16_t>(std::round(group.base_params.b * 65536.0));
            int16_t q_base_c = static_cast<int16_t>(std::round(group.base_params.c * 65536.0));
            
            // 计算group_length的量化值
            int16_t q_group_length = 0;
            if (!is_orphan) {
                q_group_length = static_cast<int16_t>(std::round(group.length * 65536.0));
            }
            
            // 计算flags_size (orphan标志位 + 区间数)
            uint16_t flags_size = (is_orphan ? 0x1 : 0x0) | (static_cast<uint16_t>(group.delta_encodings.size()) << 1);
            
            std::cout << "\nGroup[" << i << "]:" << std::endl;
            std::cout << "  ID: " << group.id << std::endl;
            std::cout << "  Storage Type: " << (is_orphan ? "ORPHAN" : "NORMAL") << std::endl;
            std::cout << "  Group Length: " << std::fixed << std::setprecision(6) << group.length 
                      << " (hex: 0x" << std::hex << std::setw(4) << std::setfill('0') << (q_group_length & 0xFFFF) << std::dec << std::setfill(' ') << ")" << std::endl;
            std::cout << "  Base Length: " << group.base_length << std::endl;
            std::cout << "  Base Interval: [" << group.base_interval.start << ", " << group.base_interval.end << "]" << std::endl;
            
            std::cout << "  Base Parameters: method=" << static_cast<int>(group.base_params.method)
                      << ", a=" << std::setprecision(8) << group.base_params.a
                      << ", b=" << group.base_params.b << " (hex: 0x" << std::hex << std::setw(4) << std::setfill('0') << (q_base_b & 0xFFFF) << std::dec << std::setfill(' ') << ")"
                      << ", c=" << group.base_params.c << " (hex: 0x" << std::hex << std::setw(4) << std::setfill('0') << (q_base_c & 0xFFFF) << std::dec << std::setfill(' ') << ")"
                      << ", order=" << group.base_params.order
                      << ", range=[" << group.base_params.range_start << ", " << group.base_params.range_end << "]" << std::endl;
            
            std::cout << "  FLAGS_SIZE: 0x" << std::hex << std::setw(4) << std::setfill('0') << flags_size 
                      << " (type=" << (is_orphan ? "1" : "0") << ", size=" << std::dec << group.delta_encodings.size() << ")" << std::endl;
            
            std::cout << "  Scale Factors: start=65536.00000000, slope=65536.00000000, intercept=65536.00000000, primary=65536.00000000"
                      << " (hex: 0x" << std::hex << std::setw(4) << std::setfill('0') << 0x10000 << std::dec << std::setfill(' ') << ")" << std::endl;
            
            std::cout << "  Delta Encodings (" << group.delta_encodings.size() << " intervals):" << std::dec << std::endl;
        
            for (size_t j = 0; j < group.delta_encodings.size(); j++) {
                const auto& delta = group.delta_encodings[j];
                
                // 统一使用65536.0作为缩放因子进行量化
                int16_t q_delta_start = static_cast<int16_t>(std::round(delta.delta_start * 65536.0));
                
                // 对于orphan组，计算interval的end
                int16_t q_delta_end = 0;
                if (is_orphan && delta.original_interval.end > 0) {
                    q_delta_end = static_cast<int16_t>(std::round(delta.original_interval.end * 65536.0));
                }
                
                // 计算reflection flags (仅用于normal组)
                uint16_t refl_flags = 0;
                if (!is_orphan) {
                    if (delta.is_y_reflected) refl_flags |= 0x2;
                    if (delta.is_x_reflected) refl_flags |= 0x1;
                }
                
                // 直接转换delta_slope和delta_intercept为整数，因为它们在代码中应该已经是量化的整数值
                int16_t q_slope = static_cast<int16_t>(delta.delta_slope);
                int16_t q_intercept = static_cast<int16_t>(delta.delta_intercept);
                
                std::cout << "    Delta[" << j << "]: "
                          << "start=" << std::fixed << std::setprecision(6) << delta.delta_start 
                          << " (hex: 0x" << std::hex << std::setw(4) << std::setfill('0') << (q_delta_start & 0xFFFF) << std::dec << std::setfill(' ') << "), "
                          << "slope=" << delta.delta_slope 
                          << " (hex: 0x" << std::hex << std::setw(4) << std::setfill('0') << (q_slope & 0xFFFF) << std::dec << std::setfill(' ') << "), "
                          << "intercept=" << delta.delta_intercept 
                          << " (hex: 0x" << std::hex << std::setw(4) << std::setfill('0') << (q_intercept & 0xFFFF) << std::dec << std::setfill(' ') << "), ";
                
                if (is_orphan) {
                    std::cout << "end=" << std::fixed << std::setprecision(6) << delta.original_interval.end
                              << " (hex: 0x" << std::hex << std::setw(4) << std::setfill('0') << (q_delta_end & 0xFFFF) << std::dec << std::setfill(' ') << ")";
                } else {
                    std::cout << "x_refl=" << delta.is_x_reflected
                              << ", y_refl=" << delta.is_y_reflected
                              << " (refl_flags=0x" << std::hex << std::setw(4) << std::setfill('0') << refl_flags << std::dec << std::setfill(' ') << ")";
                }
                std::cout << std::endl;
                
                // 如果有原始区间信息，也显示出来
                if (delta.original_interval.start != 0 || delta.original_interval.end != 0) {
                    std::cout << "      Original interval: [" << delta.original_interval.start 
                              << ", " << delta.original_interval.end << "]" << std::endl;
                }
            }
        }
        std::cout << "=================================================\n" << std::endl;
        // printRecoveredIntervals(compressed_groups, best_fit_params_list); 
        verifyIntervalCoverage("Final", best_intervals, compressed_groups, start, end);
        std::cout << "• Created " << compressed_groups.size() << " interval groups" << std::endl;

        // ====== STAGE 8: BIT WIDTH OPTIMIZATION ======
        std::cout << "\n[STAGE 8: BIT WIDTH OPTIMIZATION]" << std::endl;
        
        // Use the function to calculate optimal bit widths based on function properties
        int input_width, output_width;
        int initial_frac_bits = calculateOptimalBitWidths(
            expression_str, start, end, config.acceptable_error, 
            input_width, output_width
        );
        
        double scale_factor = 1 << initial_frac_bits;
        
        std::cout << "• Initial bit width analysis:" << std::endl;
        std::cout << "  - Fractional bits: " << initial_frac_bits << " (scale_factor = " << scale_factor << ")" << std::endl;
        std::cout << "  - Input width: " << input_width << " bits" << std::endl;
        std::cout << "  - Output width: " << output_width << " bits" << std::endl;

        // Save the compressed groups to file
        saveCompressedGroupsToFile(compressed_groups, groups_file, best_intervals,
                                best_fit_params_list, expression_str, start, end);
        std::cout << "• Saved compressed groups to: " << groups_file << std::endl;

        // ====== STAGE 9: HARDWARE SIMULATION ======
        std::cout << "\n[STAGE 9: HARDWARE SIMULATION]" << std::endl;
        std::cout << "• Simulating hardware implementation with initial precision..." << std::endl;
        
        // Fine-tune the fractional bits based on actual error measurement
        double max_hw_error = 0.0;
        int suggested_frac_bits = initial_frac_bits; // Start with calculated bits
        double hardware_error = evaluateCompressedErrorWithQuantization(
                                expression_str,
                                best_intervals,
                                best_fit_params_list,
                                compressed_groups,
                                initial_frac_bits,
                                &max_hw_error,    // Track max error
                                config.acceptable_error,
                                input_width,
                                output_width);
        
        std::cout << "• Hardware simulation results:" << std::endl;
        std::cout << "  - Average hardware error: " << hardware_error << std::endl;
        std::cout << "  - Maximum hardware error: " << max_hw_error << std::endl;
        std::cout << "  - Target error: " << config.acceptable_error << std::endl;
        std::cout << "  - Status: " << (hardware_error <= config.acceptable_error ? "PASS" : "FAIL") << std::endl;
        
        // If error is too high, increase precision
        if (hardware_error > config.acceptable_error) {
            std::cout << "• Hardware error exceeds target. Trying increased precision..." << std::endl;
            
            int min_required_bits = static_cast<int>(std::ceil(std::log2(1.0 / max_hw_error)));
            if (min_required_bits > initial_frac_bits && min_required_bits <= 24) {
                suggested_frac_bits = min_required_bits;
                std::cout << "• Increasing fractional bits from " << initial_frac_bits 
                          << " to " << suggested_frac_bits 
                          << " based on error analysis" << std::endl;
                
                // Recalculate error with new precision
                hardware_error = evaluateCompressedErrorWithQuantization(
                                 expression_str,
                                 best_intervals,
                                 best_fit_params_list,
                                 compressed_groups,
                                 suggested_frac_bits,
                                 &max_hw_error,
                                 config.acceptable_error,
                                 input_width,
                                 output_width);
                
                std::cout << "• Updated hardware error: " << hardware_error << std::endl;
            }
        } 
        // If error is much lower than needed, try to reduce precision
        else if (hardware_error < config.acceptable_error / 4 && initial_frac_bits > 10) {
            std::cout << "• Hardware error well below target. Trying reduced precision..." << std::endl;
            
            int reduced_bits = initial_frac_bits - 1;
            double test_error = evaluateCompressedErrorWithQuantization(
                               expression_str,
                               best_intervals,
                               best_fit_params_list,
                               compressed_groups,
                               reduced_bits,
                               nullptr,
                               config.acceptable_error,
                               input_width,
                               output_width);
            
            if (test_error <= config.acceptable_error) {
                suggested_frac_bits = reduced_bits;
                std::cout << "• Reduced fractional bits from " << initial_frac_bits 
                          << " to " << suggested_frac_bits 
                          << " (error: " << test_error << " still meets target)" << std::endl;

                // Try to reduce further if possible
                if (test_error < config.acceptable_error / 2 && reduced_bits > 10) {
                    int further_reduced = reduced_bits - 1;
                    test_error = evaluateCompressedErrorWithQuantization(
                                 expression_str,
                                 best_intervals,
                                 best_fit_params_list,
                                 compressed_groups,
                                 further_reduced,
                                 nullptr,
                                 config.acceptable_error,
                                 input_width,
                                 output_width);
                                 
                    if (test_error <= config.acceptable_error) {
                        suggested_frac_bits = further_reduced;
                        std::cout << "• Further reduced to " << suggested_frac_bits 
                                  << " bits (error: " << test_error << ")" << std::endl;
                        hardware_error = test_error;
                    }
                } else {
                    hardware_error = test_error;
                }
            }
        }

        // Use the optimized fractional bits
        int hw_frac_bits = suggested_frac_bits;
        int hw_scale_factor = 1 << hw_frac_bits;
        
        // Recalculate bit widths if fractional bits changed
        if (hw_frac_bits != initial_frac_bits) {
            // Update total widths based on new fractional bits
            int input_int_bits = input_width - initial_frac_bits;
            int output_int_bits = output_width - initial_frac_bits;
                        
            input_width = input_int_bits + hw_frac_bits;
            output_width = output_int_bits + hw_frac_bits;
            
            // Re-round to standard widths
            input_width = roundToStandardWidth(input_width);
            output_width = roundToStandardWidth(output_width);
        }
        
        std::cout << "\n[STAGE 10: FINAL HARDWARE PARAMETERS]" << std::endl;
        std::cout << "• Function: " << expression_str << std::endl;
        std::cout << "• Target error: " << config.acceptable_error << std::endl;
        std::cout << "• Optimized parameters:" << std::endl;
        std::cout << "  - Fractional bits: " << hw_frac_bits << " (scale factor: " << hw_scale_factor << ")" << std::endl;
        std::cout << "  - Input width: " << input_width << " bits" << std::endl;
        std::cout << "  - Output width: " << output_width << " bits" << std::endl;
        std::cout << "  - Final hardware error: " << hardware_error << std::endl;
        std::cout << "  - Status: " << (hardware_error <= config.acceptable_error ? "PASS" : "FAIL") << std::endl;
        
        // ====== STAGE 11: TEST VECTOR GENERATION ======
        std::cout << "\n[STAGE 11: TEST VECTOR GENERATION]" << std::endl;
        std::cout << "• Generating test vectors for hardware verification..." << std::endl;
        generateSimulationVectors(
            expression_str, 
            func_dir, 
            clean_name, 
            start, 
            end, 
            hw_scale_factor,
            hw_frac_bits,
            input_width,
            output_width,
            100  // 100 test vectors
        );
        
        // ====== STAGE 12: HARDWARE VERIFICATION ======
        std::cout << "\n[STAGE 12: HARDWARE VERIFICATION]" << std::endl;
        bool hw_verification_success = verifyHardwareImplementation(
            expression_str,
            compressed_groups,
            hw_frac_bits,
            config.acceptable_error,
            func_dir,
            clean_name,
            start, end,
            input_width,
            output_width,
            false  // Use average-based verification
        );
        
        // Store verification result and bit widths in the return structure
        result.hw_verification_success = hw_verification_success;
        result.compressed_groups = compressed_groups;
        result.intervals = best_intervals;
        result.fit_params = best_fit_params_list;
        result.optimized_frac_bits = hw_frac_bits;
        result.optimized_scale_factor = hw_scale_factor;
        result.final_error = hardware_error;
        result.input_width = input_width;
        result.output_width = output_width;
        
        // Save comprehensive metrics
        std::ofstream metrics(metrics_file);
        metrics << "Function Analysis for: " << expression_str << "\n"
                << "================================\n"
                << "Type: " << (is_custom ? "Custom" : "Built-in") << "\n"
                << "Best Epsilon: " << best_epsilon << "\n"
                << "Initial Intervals: " << initial_interval_count << "\n"
                << "Final Intervals: " << best_intervals.size() << "\n"
                << "Compression Ratio: " << best_compression_ratio << "\n"
                << "Error Analysis:\n"
                << "- Acceptable Error Target: " << config.acceptable_error << "\n"
                << "- Approximation Error: " << best_approximation_error << "\n"
                << "- Hardware Error: " << hardware_error << "\n"
                << "- Error/Target Ratio: " << (hardware_error/config.acceptable_error) << "\n"
                << "- Status: " << (hardware_error <= config.acceptable_error ? "PASS" : "FAIL") << "\n"
                << "Hardware Parameters:\n"
                << "- Initial Fractional Bits: " << initial_frac_bits << "\n"
                << "- Optimized Fractional Bits: " << hw_frac_bits << "\n"
                << "- Scale Factor: " << hw_scale_factor << "\n"
                << "- Input Width: " << input_width << " bits\n"
                << "- Output Width: " << output_width << " bits\n"
                << "ROM Generation:\n"
                << "- Parameters Count: " << best_fit_params_list.size() << "\n"
                << "- Memory Footprint: " << (best_fit_params_list.size() * sizeof(FitParameters)) << " bytes\n";
        metrics.close();
        
        // Print summary to console
        std::cout << "\n╔════════════════════════════ SUMMARY ════════════════════════════╗" << std::endl;
        std::cout << "║ Function: " << std::left << std::setw(54) << expression_str << "║" << std::endl;
        std::cout << "╟─────────────────────────────────────────────────────────────────╢" << std::endl;
        std::cout << "║ Compression Ratio:         " << std::left << std::setw(34) << best_compression_ratio << "x" << "║" << std::endl;
        std::cout << "║ Approximation Error:       " << std::left << std::setw(35) << best_approximation_error << "║" << std::endl;
        std::cout << "║ Hardware Error:            " << std::left << std::setw(35) << hardware_error << "║" << std::endl;
        std::cout << "║ Target Error:              " << std::left << std::setw(35) << config.acceptable_error << "║" << std::endl;
        std::cout << "║ Hardware Implementation:   " << std::left << std::setw(35) << (hw_verification_success ? "PASS" : "FAIL") << "║" << std::endl;
        std::cout << "║ Results Directory:         " << std::left << std::setw(35) << func_dir << "║" << std::endl;
        std::cout << "╚═════════════════════════════════════════════════════════════════╝" << std::endl;
    }
    
    return result;
}

int main(int argc, char* argv[]) {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║             FUNCTION APPROXIMATION TOOLKIT (FAT)             ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    
    std::string results_dir = "results";
    createDirectory(results_dir);
    std::cout << "• Created results directory: " << results_dir << std::endl;
    std::vector<std::string> test_expressions = {
        "relu(x)", "gelu(x)", "swishglu(x)",
        "tanh(x)", "sin(x)", "cos(x)", 
        "exp(x)", "log(x)", "sqrt(x)",
        "1/(1+exp(-x))", "log(1+x)"
    };

    std::vector<std::string> custom_functions = {"relu", "gelu", "swishglu"};
    
    // Default configuration
    double start = 0.0;  // Start Point
    double end = 1.0;    // End Point
    size_t num_points = 2048;  // Initial Number of Points
    double initial_unit_length = (end - start) / num_points;
    bool generate_hw = false;  // Flag to generate hardware files

    // Parse command-line arguments if provided
    if (argc >= 3) {
        start = std::stod(argv[1]);
        end = std::stod(argv[2]);
    }

    // Initialize configuration
    FittingParametersConfig config;
    config.min_unit_length = initial_unit_length / 16;
    config.epsilon_start = 1e-4;
    config.epsilon_end = 2e-3;
    config.epsilon_steps = 1;
    config.acceptable_error = 1e-4;

    // Prompt the user for input
    std::cout << "\n[INPUT CONFIGURATION]" << std::endl;
    std::cout << "• Default settings:" << std::endl;
    std::cout << "  - Range: [" << start << ", " << end << "]" << std::endl;
    std::cout << "  - Target error: " << config.acceptable_error << std::endl;
    std::cout << "• Please enter the function expression and options" << std::endl;
    std::cout << "  Format: <function> <start> <end> <target_error> [hw]" << std::endl;
    std::cout << "  Example: tanh(x) -3 4 1e-7 hw" << std::endl;
    std::cout << "  Enter 'test_all' to run all test functions" << std::endl;
    std::cout << "• Function: ";
    
    std::string input_line;
    std::getline(std::cin, input_line);
    std::istringstream iss(input_line);
    std::string expression_str;
    
    if (iss >> expression_str) {
        if (expression_str == "test_all") {
            std::cout << "\n[BATCH TEST MODE]" << std::endl;
            std::cout << "• Running tests for " << test_expressions.size() << " functions..." << std::endl;
            
            int test_num = 1;
            for (const auto& expr : test_expressions) {
                std::cout << "\n• Test " << test_num << "/" << test_expressions.size() 
                          << ": " << expr << std::endl;
                
                bool is_custom = false;
                std::string parsed_expr = expr;
                // Check if custom function
                for (const auto& func : custom_functions) {
                    if (parsed_expr.find(func + "(") != std::string::npos) {
                        is_custom = true;
                        replaceAll(parsed_expr, func, func);
                    }
                }
                
                // Process function and get results including optimal bit width parameters
                FunctionProcessingResult result = processFunction(
                    parsed_expr, results_dir, start, end, num_points, config, custom_functions);

                std::string func_name = expr.substr(0, expr.find('('));
                
                if (generate_hw) {
                    // Use bit width parameters calculated by processFunction
                    generateHardwareImplementation(
                        results_dir, func_name, result.compressed_groups, 
                        result.intervals, result.fit_params, expr,
                        start, end, result.optimized_scale_factor, config.acceptable_error,
                        result.input_width, result.output_width
                    );
                }
                
                test_num++;
            }
            std::cout << "\n[BATCH TEST COMPLETED]" << std::endl;
        } else {
            // Parse individual function options
            double input_start, input_end, error_acceptable;
            std::string hw_option;
            
            if (iss >> input_start >> input_end >> error_acceptable) {
                if (input_start < input_end && error_acceptable > 0) {
                    std::cout << "• Using custom configuration:" << std::endl;
                    std::cout << "  - Range: [" << input_start << ", " << input_end << "]" << std::endl;
                    std::cout << "  - Target error: " << error_acceptable << std::endl;
                    
                    start = input_start;
                    end = input_end;
                    config.acceptable_error = error_acceptable;
                    config.min_unit_length = (end - start) / (num_points * 16);
                    
                    // Check if hardware option is provided
                    if (iss >> hw_option) {
                        if (hw_option == "hw" || hw_option == "HW") {
                            generate_hw = true;
                            std::cout << "  - Hardware generation: Enabled" << std::endl;
                        }
                    }
                } else {
                    std::cout << "• Invalid range or error value. Using default configuration." << std::endl;
                    std::cout << "  - Range: [" << start << ", " << end << "]" << std::endl;
                    std::cout << "  - Target error: " << config.acceptable_error << std::endl;
                }
            }

            // If no expression is provided, use the default function 'tanh(x)'
            if (expression_str.empty()) {
                expression_str = "tanh(x)";
                std::cout << "• No function provided. Using default: tanh(x)" << std::endl;
            }
            
            // Check if input is custom function
            bool is_custom = false;
            for (const auto& func : custom_functions) {
                if (expression_str.find(func + "(") != std::string::npos) {
                    is_custom = true;
                    replaceAll(expression_str, func, func);
                }
            }
            
            // Core processing function, returns results including optimized bit widths
            FunctionProcessingResult result = processFunction(
                expression_str, results_dir, start, end, num_points, config, custom_functions);
            
            std::string func_name = expression_str.substr(0, expression_str.find('('));
            
            // Hardware implementation using optimal parameters calculated in processFunction
            if (generate_hw) {
                std::cout << "\n[HARDWARE IMPLEMENTATION GENERATION]" << std::endl;
                std::cout << "• Generating hardware files..." << std::endl;
                
                generateHardwareImplementation(
                    results_dir, func_name, result.compressed_groups, 
                    result.intervals, result.fit_params, expression_str,
                    start, end, result.optimized_scale_factor, config.acceptable_error,
                    result.input_width, result.output_width
                );
                
                std::cout << "• Hardware files generated successfully." << std::endl;
            }
        }
    } else {
        std::cout << "• No input provided. Exiting program." << std::endl;
    }

    return 0;
}

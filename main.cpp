#include <iostream>
#include <vector>
#include <functional>
#include <cmath>
#include <sys/stat.h>
#include "interval_optimizer.hpp"
#include "function_fitter.hpp"
#include "interval_group_compressor.hpp"
// #include "./tb/hls_lut_mapper.hpp"

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

void saveCompressedFitParametersToFile(const std::vector<CompressedFitParameters>& compressed_params_list,
                                       const std::string& filename) {
    std::ofstream file(filename);
    if (file.is_open()) {
        // File format:
        // ParamsID,Order,a,b,c,IntervalIndices,Offsets
        file << "ParamsID,Order,a,b,c,IntervalIndices,Offsets\n";
        size_t params_id = 0;
        for (const auto& comp_param : compressed_params_list) {
            file << params_id << "," << comp_param.params.order << ","
                 << comp_param.params.a << "," << comp_param.params.b << "," << comp_param.params.c << ",";
    
            // Save the corresponding interval indices
            file << "\"";
            for (size_t idx = 0; idx < comp_param.interval_indices.size(); ++idx) {
                file << comp_param.interval_indices[idx];
                if (idx != comp_param.interval_indices.size() - 1) {
                    file << ";";
                }
            }
            file << "\",";
    
            // Save the offsets
            file << "\"";
            for (size_t idx = 0; idx < comp_param.offsets.size(); ++idx) {
                file << comp_param.offsets[idx];
                if (idx != comp_param.offsets.size() - 1) {
                    file << ";";
                }
            }
            file << "\"\n";
    
            params_id++;
        }
        file.close();
        std::cout << "Compressed fitting parameters saved to file: " << filename << std::endl;
    } else {
        std::cout << "Failed to open file to save compressed fitting parameters" << std::endl;
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

void processFunction(const std::string& expression_str,
                    const std::string& results_dir,
                    double start, double end,
                    size_t num_points,
                    const FittingParametersConfig& config,
                    const std::vector<std::string>& custom_functions) {

    std::cout << "\nStarting process for: " << expression_str << std::endl;
    std::string func_dir = results_dir + "/" + expression_str;
    createDirectory(func_dir);
    std::cout << "Created directory: " << func_dir << std::endl;

    // Setup symbol table
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

    // Check if custom function
    bool is_custom = false;
    std::string parsed_expr = expression_str;
    for (const auto& func : custom_functions) {
        if (parsed_expr.find(func + "(") != std::string::npos) {
            is_custom = true;
            break;
        }
    }

    std::cout << "Processing function: " << (is_custom ? "Custom" : "built-in")
            << " function: " << expression_str << std::endl;
    
    // Parse the expression
    exprtk::expression<double> expression;
    expression.register_symbol_table(symbol_table);
    exprtk::parser<double> parser;

    if (!parser.compile(parsed_expr, expression)) {
        std::cerr << "Error parsing the expression: " << parser.error() << std::endl;
        for (std::size_t i = 0; i < parser.error_count(); ++i) {
            exprtk::parser_error::type error = parser.get_error(i);
            std::cerr << "Error: " << std::string(error.diagnostic) << std::endl;
        }
        return;
    }
    std::cout << "Successfully parsed expression\n";

    // Initial setup
    MergeParams merge_params;
    merge_params.base_len_tol = 0.15 + 0.05 * log10(config.acceptable_error * 1e4);
    
    // Continuity tolerance
    merge_params.base_continuity_tol = 0.05 * (config.acceptable_error * 1e3);
    // Curvature base
    merge_params.curvature_base = 0.8 / (1.0 + 5.0 * config.acceptable_error);
    // Slope base
    merge_params.slope_base = 0.3 * sqrt(config.acceptable_error * 1e3);
    // Curvature sensitivity
    merge_params.curvature_sensitivity = 1.5 + 0.5 * sin(config.acceptable_error * 1e4);

    double initial_unit_length = (end - start) / num_points;
    OptimizationConfig opt_config;
    std::vector<Interval> initial_intervals = 
        generateInitialIntervals(start, end, num_points, initial_unit_length, parsed_expr, opt_config);
    verifyIntervalCoverage("Initial", initial_intervals, {}, start, end);
    size_t initial_interval_count = initial_intervals.size();
    std::cout << "Generated " << initial_interval_count << " initial intervals\n";

    std::cout << "Starting optimization with:"
          << "\n- Initial intervals: " << initial_interval_count
          << "\n- Epsilon range: " << config.epsilon_start << " to " << config.epsilon_end
          << "\n- Steps: " << config.epsilon_steps << std::endl;

    // Generate fine intervals
    std::vector<Interval> fine_intervals;
    for (const auto& interval : initial_intervals) {
        splitInterval(interval, config.epsilon_start, config.min_unit_length, 
                     parsed_expr, fine_intervals, config.acceptable_error, 1.0);
    }
    verifyIntervalCoverage("Split", fine_intervals, {}, start, end);
    std::cout << "Generated " << fine_intervals.size() << " fine intervals\n";

    // Initialize optimization variables
    double best_epsilon = config.epsilon_start;
    double best_compression_ratio = 1.0;
    double best_error = std::numeric_limits<double>::max();
    std::vector<Interval> best_intervals;
    std::vector<FitParameters> best_fit_params_list;
    std::vector<CompressedFitParameters> best_compressed_params_list;

    std::vector<ParetoResult> pareto_results;
    // Optimization loop
    for (size_t i = 0; i < config.epsilon_steps; ++i) {
        // Handle single step case to avoid division by zero
        double epsilon = (config.epsilon_steps == 1) ? 
                        config.epsilon_start :
                        config.epsilon_start + (config.epsilon_end - config.epsilon_start) * 
                        i / (config.epsilon_steps - 1);
        
        std::vector<Interval> merged_intervals = initial_intervals;
        mergeIntervals(merged_intervals, epsilon, config.acceptable_error, parsed_expr, merge_params, config.min_unit_length, 1.0);
        std::cout << "Merged intervals: " << merged_intervals.size() << std::endl;
        verifyIntervalCoverage("Merge", merged_intervals, {}, start, end);
        
        std::vector<FitParameters> fit_params_list;
        fitAllSegments(parsed_expr, merged_intervals, fit_params_list, config.acceptable_error);
        
        std::vector<CompressedFitParameters> compressed_params_list;
        compressFitParameters(fit_params_list, merged_intervals, 
                            compressed_params_list, config.acceptable_error);
        
        double compressed_error = 
            evaluateCompressedError(parsed_expr, merged_intervals, compressed_params_list);
        double compression_ratio = 
            static_cast<double>(merged_intervals.size()) / initial_interval_count;
        
        ParetoResult pr;
        pr.epsilon = epsilon;
        pr.min_unit_length = config.min_unit_length;
        pr.final_interval_count = merged_intervals.size();
        pr.final_error = compressed_error;
        pr.compression_ratio = compression_ratio;
        pareto_results.push_back(pr);

        if (compressed_error <= config.acceptable_error && 
            compression_ratio < best_compression_ratio) {
            best_epsilon = epsilon;
            best_compression_ratio = compression_ratio;
            best_error = compressed_error;
            best_intervals = merged_intervals;
            best_fit_params_list = fit_params_list;
            best_compressed_params_list = compressed_params_list;
            std::cout << "Found better solution with epsilon: " << best_epsilon
                    << ", compression ratio: " << best_compression_ratio
                    << ", error: " << best_error << std::endl;
        } else {
            std::cout << "Epsilon: " << epsilon
                    << ", compression ratio: " << compression_ratio
                    << ", error: " << compressed_error << std::endl;
        }
    }

    // Pareto front results
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

    std::cout << "Pareto Front Results:\n";
    for (const auto& pr : pareto_front) {
        std::cout << "Epsilon: " << pr.epsilon
                << ", Final Intervals: " << pr.final_interval_count
                << ", Error: " << pr.final_error
                << ", Compression Ratio: " << pr.compression_ratio << std::endl;
    }

    std::string pareto_file = func_dir + "/pareto_front.csv";
    std::ofstream pareto_out(pareto_file);
    if (pareto_out.is_open()) {
        pareto_out << "Epsilon,FinalIntervals,FinalError,CompressionRatio\n";
        for (const auto& pr : pareto_results) {
            pareto_out << pr.epsilon << ","
                       << pr.final_interval_count << "," 
                       << pr.final_error << ","
                       << pr.compression_ratio << "\n";
        }
        pareto_out.close();
        std::cout << "Pareto front results saved to: " << pareto_file << std::endl;
    }

    // Save results
    if (best_intervals.empty()) {
        auto min_error_it = std::min_element(pareto_results.begin(), pareto_results.end(),
            [](const ParetoResult& a, const ParetoResult& b) { return a.final_error < b.final_error; });
    
        // Use the precomputed values directly from the best ParetoResult
        best_epsilon = min_error_it->epsilon;
        best_compression_ratio = min_error_it->compression_ratio;
        best_error = min_error_it->final_error;
        
        std::cout << "Using best solution with epsilon: " << best_epsilon 
                  << " (error: " << best_error << ")" << std::endl;
        
        // Retrieve the best result by running just one iteration with the selected epsilon
        std::vector<Interval> merged_intervals = initial_intervals;
        mergeIntervals(merged_intervals, best_epsilon, config.acceptable_error, 
                      parsed_expr, merge_params, config.min_unit_length, 1.0);
        best_intervals = merged_intervals;
        
        // Only compute fit parameters once
        fitAllSegments(parsed_expr, best_intervals, best_fit_params_list, config.acceptable_error);
        
        // Save parameters and log results
        saveFitParametersToFile(best_fit_params_list, func_dir + "/fit_params.csv");
        double FitError = evaluateError(parsed_expr, best_intervals, best_fit_params_list);
        std::cout << "Best Fit Error: " << FitError << std::endl;
    }

    // Save results with compressed groups
    if (!best_intervals.empty()) {
        std::cout << "\nSaving results...\n";
        // Save compressed parameters
        std::string params_file = func_dir + "/compressed_params.csv";
        std::string groups_file = func_dir + "/compressed_groups.csv";
        std::string metrics_file = func_dir + "/optimization_metrics.txt";

        std::cout << "Before entering groupAndCompressIntervals:\n";
        for (size_t i = 0; i < best_intervals.size(); ++i) {
            std::cout << "Interval [" << best_intervals[i].start << ", " << best_intervals[i].end
                    << "], b=" << best_fit_params_list[i].b << ", c=" << best_fit_params_list[i].c << "\n";
        }
        // Save intervals groups
        std::vector<IntervalGroup> compressed_groups;
        groupAndCompressIntervals(expression_str, best_intervals, best_fit_params_list, compressed_groups, config.acceptable_error);
        verifyIntervalCoverage("Final", best_intervals, compressed_groups, start, end);
        saveCompressedGroupsToFile(compressed_groups, groups_file);

        double compressed_error = evaluateCompressedErrorWithQuantization(
                                expression_str,
                                best_intervals,
                                best_fit_params_list,
                                compressed_groups);

        // Save metrics
        std::ofstream metrics(metrics_file);
        metrics << "Function Analysis for: " << expression_str << "\n"
                << "================================\n"
                << "Type: " << (is_custom ? "Custom" : "Built-in") << "\n"
                << "Best Epsilon: " << best_epsilon << "\n"
                << "Initial Intervals: " << initial_interval_count << "\n"
                << "Final Intervals: " << best_intervals.size() << "\n"
                << "Compression Ratio: " << best_compression_ratio << "\n"
                << "Final Error: " << best_error << "\n"
                << "Error Analysis:\n"
                << "- Acceptable Error Threshold: " << config.acceptable_error << "\n"
                << "- Error/Threshold Ratio: " << (best_error/config.acceptable_error) << "\n"
                << "- Status: " << (best_error <= config.acceptable_error ? "PASS" : "FAIL") << "\n"
                << "ROM Generation:\n"
                << "- Parameters Count: " << best_fit_params_list.size() << "\n"
                << "- Memory Footprint: " << (best_fit_params_list.size() * sizeof(FitParameters)) << " bytes\n";
        metrics.close();
        
        // Print summary to console
        std::cout << "\nResults for " << expression_str << ":\n"
                << "Compression: " << best_compression_ratio << "x\n"
                << "Error: " << compressed_error << "\n" 
                << (compressed_error <= config.acceptable_error ? " (PASS)" : " (FAIL)") << "\n"
                << "Results saved to: " << func_dir << std::endl;
    }
}

int main(int argc, char* argv[]) {
    std::string results_dir = "results";
    createDirectory(results_dir);

    std::vector<std::string> test_expressions = {
        "relu(x)", "gelu(x)", "swishglu(x)",
        "tanh(x)", "sin(x)", "cos(x)", 
        "exp(x)", "log(x)", "sqrt(x)",
        "1/(1+exp(-x))", "log(1+x)"
    };

    std::vector<std::string> custom_functions = {"relu", "gelu", "swishglu"};
    
    // defalut start and end points
    double start = 0.0;  // Start Point
    double end = 1.0;     // End Point
    if (argc >= 3) {
        start = std::stod(argv[1]);
        end = std::stod(argv[2]);
    }

    size_t num_points = 2048;  // Initial Number of Points
    double initial_unit_length = (end - start) / num_points;

    FittingParametersConfig config;
    config.min_unit_length = initial_unit_length / 16;
    config.epsilon_start = 1e-4;
    config.epsilon_end = 2e-3;
    config.epsilon_steps = 1;
    // config.error_threshold = 1e-7;
    config.acceptable_error = 1e-4;

    // Prompt the user to enter the function expression
    std::string expression_str;
    std::cout << "Please enter the function expression and range (e.g., tanh(x) -3 4 1e-7): ";
    
    std::string input_line;
    std::getline(std::cin, input_line);
    std::istringstream iss(input_line);
    if (iss >> expression_str) {
        if (expression_str == "test_all") {
            std::cout << "Running batch test mode...\n";
            for (const auto& expr : test_expressions) {
                bool is_custom = false;
                std::string parsed_expr = expr;
                // Check if custom function
                for (const auto& func : custom_functions) {
                    if (parsed_expr.find(func + "(") != std::string::npos) {
                        is_custom = true;
                        replaceAll(parsed_expr, func, func);
                    }
                }
                if(is_custom) {
                    std::cout << "Custom function detected: " << expression_str << std::endl;
                } else {
                    std::cout << "No Custom Function Detected: " << expression_str << std::endl;
                }

                std::cout << "\nProcessing function: " << (is_custom ? "Custom" : "built-in")
                        << " function: " << expr << std::endl;
                processFunction(parsed_expr, results_dir, start, end, num_points, config, custom_functions);
            }
            std::cout << "Batch test mode completed.\n";
        } else {
            double input_start, input_end, error_acceptable;
            if (iss >> input_start >> input_end >> error_acceptable) {
                if (input_start < input_end && error_acceptable > 0) {
                    start = input_start;
                    end = input_end;
                    config.acceptable_error = error_acceptable;
                    config.min_unit_length = (end - start) / (num_points * 16);
                } else {
                    std::cout << "Invalid range. Using defalut:\n [" 
                            << "Range: [" << start << ", " << end << "]\n"
                            << "Acceptable error: " << config.acceptable_error << "\n";
                }
            }

            // If no expression is provided, use the default function 'tanh(x)'
            if (expression_str.empty()) {
                expression_str = "tanh(x)";
                std::cout << "Use the default example function 'tanh(x)' " << std::endl;
            }
            // Check if input is custom function
            bool is_custom = false;
            for (const auto& func : custom_functions) {
                if (expression_str.find(func + "(") != std::string::npos) {
                    is_custom = true;
                    replaceAll(expression_str, func, func);
                }
            }

            std::cout << "Processing " << (is_custom ? "custom" : "built-in") 
                    << " function: " << expression_str << std::endl;
            processFunction(expression_str, results_dir, start, end, num_points, config, custom_functions);
        }
    }

    // **Generate Verilog ROM Module**
    // std::string verilog_filename = "FitParametersROM.v";

    return 0;
}

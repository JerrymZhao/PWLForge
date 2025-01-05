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
    relu_fn<double> relu_f;
    symbol_table.add_function("relu", relu_f);
    gelu_fn<double> gelu_f;
    symbol_table.add_function("gelu", gelu_f);
    swishglu_fn<double> swish_f;
    symbol_table.add_function("swishglu", swish_f);

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
    double initial_unit_length = (end - start) / num_points;
    std::vector<Interval> initial_intervals = 
        generateInitialIntervals(start, end, num_points, initial_unit_length, parsed_expr);
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
                     parsed_expr, fine_intervals);
    }
    std::cout << "Generated " << fine_intervals.size() << " fine intervals\n";

    // Initialize optimization variables
    double best_epsilon = config.epsilon_start;
    double best_compression_ratio = 1.0;
    double best_error = std::numeric_limits<double>::max();
    std::vector<Interval> best_intervals;
    std::vector<FitParameters> best_fit_params_list;
    std::vector<CompressedFitParameters> best_compressed_params_list;

    // Optimization loop
    for (size_t i = 0; i < config.epsilon_steps; ++i) {
        double epsilon = config.epsilon_start + 
            (config.epsilon_end - config.epsilon_start) * i / (config.epsilon_steps - 1);
        
        std::vector<Interval> merged_intervals = initial_intervals;
        mergeIntervals(merged_intervals, epsilon, parsed_expr);
        std::cout << "Merged intervals: " << merged_intervals.size() << std::endl;
        
        std::vector<FitParameters> fit_params_list;
        fitAllSegments(parsed_expr, merged_intervals, fit_params_list);
        
        std::vector<CompressedFitParameters> compressed_params_list;
        compressFitParameters(fit_params_list, merged_intervals, 
                            compressed_params_list, 1e-5);
        
        double compressed_error = 
            evaluateCompressedError(parsed_expr, merged_intervals, compressed_params_list);
        double compression_ratio = 
            static_cast<double>(merged_intervals.size()) / initial_interval_count;

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

    // Save results
    if (!best_intervals.empty()) {
        std::cout << "\nSaving results...\n";
        // Save compressed parameters
        std::string params_file = func_dir + "/compressed_params.csv";
        std::string groups_file = func_dir + "/compressed_groups.csv";
        std::string metrics_file = func_dir + "/optimization_metrics.txt";

        saveCompressedFitParametersToFile(best_compressed_params_list, params_file);

        // Save intervals groups
        std::vector<IntervalGroup> compressed_groups;
        groupAndCompressIntervals(best_intervals, best_fit_params_list, compressed_groups);
        saveCompressedGroupsToFile(compressed_groups, groups_file);

        double compressed_error_with_quant = 
            evaluateCompressedErrorWithQuantization(expression_str, 
                                                best_intervals, 
                                                compressed_groups, 
                                                best_compressed_params_list);

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
                << "- Parameters Count: " << best_compressed_params_list.size() << "\n"
                << "- Memory Footprint: " << (best_compressed_params_list.size() * sizeof(FitParameters)) << " bytes\n";
        metrics.close();
        
        // Print summary to console
        std::cout << "\nResults for " << expression_str << ":\n"
                  << "Compression: " << best_compression_ratio << "x\n"
                  << "Error: " << compressed_error_with_quant << "\n" 
                  << (compressed_error_with_quant <= config.acceptable_error ? " (PASS)" : " (FAIL)") << "\n"
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

    double start = -3.0;  // Start Point
    double end = 4.0;     // End Point
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
    config.epsilon_steps = 20;
    config.error_threshold = 1e-7;
    config.acceptable_error = 1e-4;

    // Prompt the user to enter the function expression
    std::string expression_str;
    std::cout << "Please enter the function expression (e.g., tanh(x)): ";
    std::getline(std::cin, expression_str);

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

    // **Generate Verilog ROM Module**
    // std::string verilog_filename = "FitParametersROM.v";

    return 0;
}

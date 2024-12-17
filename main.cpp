#include <iostream>
#include <vector>
#include <functional>
#include <cmath>
#include "interval_optimizer.hpp"
#include "function_fitter.hpp"
#include "interval_group_compressor.hpp"
// #include "./tb/hls_lut_mapper.hpp"

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


int main(int argc, char* argv[]) {

    double start = 0.0;  // Start Point
    double end = 1.0;     // End Point
    if (argc >= 3) {
        start = std::stod(argv[1]);
        end = std::stod(argv[2]);
    }

    size_t num_points = 1024;  // Initial Number of Points
    double initial_unit_length = (end - start) / num_points;
    double min_unit_length = initial_unit_length / 16; // Minimum Unit Length

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

    // If no expression is provided, use the default function 'tanh(x)'
    if (expression_str.empty()) {
        expression_str = "tanh(x)";
        std::cout << "Use the default example function 'tanh(x)' " << std::endl;
    }

    // Define the error threshold parameters
    double epsilon_start = 1e-4;
    double epsilon_end = 2e-3;
    size_t epsilon_steps = 20; // Number of epsilon steps

    // Generate initial intervals
    std::vector<Interval> initial_intervals = generateInitialIntervals(start, end, num_points, initial_unit_length, expression_str);
    size_t initial_interval_count = initial_intervals.size(); // Initial number of intervals

    double best_compression_ratio = 1.0;
    double best_epsilon = epsilon_start;
    std::vector<Interval> best_intervals;
    std::vector<FitParameters> best_fit_params_list;
    std::vector<CompressedFitParameters> best_compressed_params_list;
    double best_error = std::numeric_limits<double>::max();

    double acceptable_error = 1e-4; // Acceptable total error threshold

    // **One-time splitting to obtain the finest interval list**
    std::vector<Interval> fine_intervals;
    for (const auto& interval : initial_intervals) {
        splitInterval(interval, epsilon_start, min_unit_length, expression_str, fine_intervals);
    }

    // **Iterate over different epsilon values, only performing merging**

    for (size_t step = 0; step <= epsilon_steps; ++step) {
        // Use exponential stepping to cover multiple orders of magnitude of epsilon
        double epsilon = epsilon_start * std::pow(epsilon_end / epsilon_start, static_cast<double>(step) / epsilon_steps);

        // Merge intervals based on current epsilon
        std::vector<Interval> merged_intervals = fine_intervals;

        // Merge intervals based on the new epsilon value
        mergeIntervals(merged_intervals, epsilon, expression_str);
        size_t optimized_interval_count = merged_intervals.size(); // Optimized number of intervals

        // Fit the current intervals
        std::vector<FitParameters> fit_params_list;
        fitAllSegments(expression_str, merged_intervals, fit_params_list);

        // Compress fit parameters by checking for symmetry and sharing parameters
        std::vector<CompressedFitParameters> compressed_params_list;
        compressFitParameters(fit_params_list, merged_intervals, compressed_params_list, 1e-5);

        // Evaluate the error after compression
        double compressed_error = evaluateCompressedError(expression_str, merged_intervals, compressed_params_list);

        // Calculate the compression ratio
        double compression_ratio = static_cast<double>(optimized_interval_count) / initial_interval_count;

        // Output the current epsilon's results
        std::cout << "Epsilon: " << epsilon << ", Optimized Interval Count: " << optimized_interval_count
                  << ", Compression Ratio: " << compression_ratio << ", Compressed Error: " << compressed_error << std::endl;

        // Select the case where error is within acceptable range and compression ratio is better than the current best
        if (compressed_error <= acceptable_error && compression_ratio < best_compression_ratio) {
            best_compression_ratio = compression_ratio;
            best_epsilon = epsilon;
            best_intervals = merged_intervals;
            best_fit_params_list = fit_params_list;
            best_compressed_params_list = compressed_params_list;
            best_error = compressed_error;
        }
    }

    if (best_intervals.empty()) {
        std::cout << "No optimized results found within the acceptable error range." << std::endl;
        return 0;
    }

    // **Group and Delta Encode**
    saveCompressedFitParametersToFile(best_compressed_params_list, "compressed_fit_parameters.csv");

    std::vector<IntervalGroup> compressed_groups;
    groupAndCompressIntervals(best_intervals, compressed_groups);
    saveCompressedGroupsToFile(compressed_groups, "compressed_groups.csv");

    // **Generate Verilog ROM Module**
    // std::string verilog_filename = "FitParametersROM.v";
    // saveCompressedParametersToVerilogFile(best_compressed_params_list, best_intervals, verilog_filename);

    // Print the best results
    std::cout << "\nBest Epsilon: " << best_epsilon << std::endl;
    std::cout << "Initial Interval Count: " << initial_interval_count << std::endl;
    std::cout << "Optimized Interval Count: " << best_intervals.size() << std::endl;
    std::cout << "Compressed Parameter Count: " << best_compressed_params_list.size() << std::endl;
    std::cout << "Best Compression Ratio: " << best_compression_ratio << std::endl;
    std::cout << "Average Error: " << best_error << std::endl;

    // **Evaluate the compressed error**
    double compressed_error_with_quant = evaluateCompressedErrorWithQuantization(
        expression_str, best_intervals, compressed_groups, best_compressed_params_list);
    std::cout << "Compressed Error with Quantization: " << compressed_error_with_quant << std::endl;
    
    return 0;
}

#ifndef FUNCTION_FITTER_HPP
#define FUNCTION_FITTER_HPP

#include <vector>
#include <string>
#include <cmath>
#include <iostream>
#include <fstream>
#include "exprtk.hpp"
#include "interval_optimizer.hpp"

// // Define the Interval structure
// struct Interval {
//     double start; // Start point of the interval
//     double end; // End point of the interval
//     // double hessian; // Hessian value
//     // double error; // Error value
// };

// Define the FitParameters structure
struct FitParameters {
    double a; // Second order term coefficient
    double b; // One order term coefficient
    double c; // Constant term
    int order; // Order of the fitting function
};

// Data structure for the shared parameters and offsets
struct CompressedFitParameters {
    FitParameters params;                  // Shared parameters
    std::vector<double> interval_indices;  // Interval indices sharing the parameters
    std::vector<double> offsets;           // Offsets
};

// Evaluate the error of the fitting function
inline double evaluateError(const std::string& expression_str, const std::vector<Interval>& intervals, const std::vector<FitParameters>& fit_params_list) {
    double total_error = 0.0;
    size_t total_points = 0;

    for (size_t idx = 0; idx < intervals.size(); ++idx) {
        const Interval& interval = intervals[idx];
        const FitParameters& params = fit_params_list[idx];

        // Sample points in the interval and calculate the error
        size_t num_samples = 10; // Count of sample points within the interval
        double step = (interval.end - interval.start) / (num_samples - 1);

        for (size_t i = 0; i < num_samples; ++i) {
            double x = interval.start + i * step;

            // Original function value
            double y_true = computeFunctionValue(expression_str, x);

            // Fitted function value
            double y_pred;
            if (params.order == 1) {
                // Linear fitting
                y_pred = params.b * x + params.c;
            } else {
                // Quadratic fitting
                y_pred = params.a * x * x + params.b * x + params.c;
            }

            // Calculate the error
            double error = std::abs(y_true - y_pred);
            total_error += error;
            total_points++;
        }
    }

    // Calculate the average error
    double average_error = total_error / total_points;
    return average_error;
}

// Compute error weight based on sensitivity
inline double computeErrorWeight(const std::string& expression_str, double x) {
    // Numerical derivative using central difference
    double h = 1e-6; // Small step size
    double y_plus = computeFunctionValue(expression_str, x + h);
    double y_minus = computeFunctionValue(expression_str, x - h);

    if (std::isnan(y_plus) || std::isnan(y_minus)) {
        return 1.0; // Default weight if function evaluation fails
    }

    double derivative = (y_plus - y_minus) / (2 * h);
    return std::abs(derivative) + 1e-6; // Avoid division by zero
}

inline std::vector<Interval> generateAdaptiveIntervals(double start, double end, size_t num_intervals, const std::string& expression_str) {
    std::vector<Interval> intervals;
    double total_length = end - start;
    double x = start;

    // First, estimate total weight
    size_t sample_points = num_intervals * 10; // Increase sample points for better estimation
    double dx = total_length / sample_points;
    double total_weight = 0.0;
    std::vector<double> weights(sample_points + 1);

    for (size_t i = 0; i <= sample_points; ++i) {
        double xi = start + i * dx;
        weights[i] = computeErrorWeight(expression_str, xi);
        total_weight += weights[i];
    }

    // Now, distribute intervals based on weights
    size_t interval_index = 0;
    double accumulated_weight = 0.0;
    double weight_per_interval = total_weight / num_intervals;
    double prev_x = x;

    for (size_t i = 0; i < sample_points; ++i) {
        accumulated_weight += weights[i];

        if (accumulated_weight >= weight_per_interval || i == sample_points - 1) {
            double next_x = start + (i + 1) * dx;
            intervals.push_back(Interval(prev_x, next_x));

            prev_x = next_x;
            accumulated_weight = 0.0;
            interval_index++;

            if (interval_index >= num_intervals) {
                break;
            }
        }
    }

    // Ensure the last interval reaches the end
    if (intervals.empty() || intervals.back().end < end) {
        intervals.push_back(Interval(prev_x, end));
    }

    return intervals;
}

// Fitting for a single segment
inline FitParameters fitSegment(const std::string& expression_str, const Interval& interval) {
    double x0 = interval.start;
    double x1 = interval.end;
    double xm = (x0 + x1) / 2.0;

    // Compute the function values at the interval points
    double y0 = computeFunctionValue(expression_str, x0);
    double y1 = computeFunctionValue(expression_str, x1);
    double ym = computeFunctionValue(expression_str, xm);

    // Fit parameters
    FitParameters params;

    // Determine whether to use linear or quadratic fitting
    double linear_error = std::abs((y0 + y1) / 2.0 - ym);
    double quadratic_error = 0.0;

    if (linear_error < 1e-3) {
        // Linear fitting: y = b * x + c
        params.order = 1;
        params.a = 0.0;
        params.b = (y1 - y0) / (x1 - x0);
        params.c = y0 - params.b * x0;
    } else {
        // Quadratic fitting: y = a * x^2 + b * x + c
        params.order = 2;
        // Parameters for the quadratic function
        double denom = (x0 - x1) * (x0 - xm) * (x1 - xm);
        params.a = (x1 * (ym - y0) + x0 * (y1 - ym) + xm * (y0 - y1)) / denom;
        params.b = ( (x1*x1)*(y0 - ym) + (x0*x0)*(ym - y1) + (xm*xm)*(y1 - y0) ) / denom;
        params.c = ( x0*(x1*ym - xm*y1) + x1*xm*y0 - x0*xm*y1 ) / denom;
    }

    return params;
}

// Function: Compress the fitting parameters by checking for symmetry and sharing parameters
inline void compressFitParameters(const std::vector<FitParameters>& fit_params_list,
                                  const std::vector<Interval>& intervals,
                                  std::vector<CompressedFitParameters>& compressed_params_list,
                                  double error_threshold = 1e-7) {
    size_t n = fit_params_list.size();
    std::vector<bool> visited(n, false);

    for (size_t i = 0; i < n; ++i) {
        if (visited[i]) continue;

        CompressedFitParameters comp_param;
        comp_param.params = fit_params_list[i];
        comp_param.interval_indices.push_back(i);
        comp_param.offsets.push_back(0.0); // Initial offset

        visited[i] = true;

        // Check for symmetry with other parameters
        for (size_t j = i + 1; j < n; ++j) {
            if (visited[j]) continue;

            bool is_symmetric = false;
            double offset = 0.0;

            // Check for translation symmetry
            if (std::abs(fit_params_list[i].a - fit_params_list[j].a) < error_threshold &&
                std::abs(fit_params_list[i].b - fit_params_list[j].b) < error_threshold) {
                double c_diff = fit_params_list[j].c - fit_params_list[i].c;

                // If constant term is close, consider it as symmetric
                if (std::abs(c_diff) < error_threshold) {
                    is_symmetric = true;
                    offset = 0.0;
                } else {
                    // Translation symmetry with offset
                    is_symmetric = true;
                    offset = c_diff;
                }
            }

            // Check for reflection symmetry
            if (!is_symmetric &&
                std::abs(fit_params_list[i].a - fit_params_list[j].a) < error_threshold &&
                std::abs(fit_params_list[i].c - fit_params_list[j].c) < error_threshold &&
                std::abs(fit_params_list[i].b + fit_params_list[j].b) < error_threshold) {
                is_symmetric = true;
                offset = 0.0; // Consider when recovering the function value
            }

            if (is_symmetric) {
                visited[j] = true;
                comp_param.interval_indices.push_back(j);
                comp_param.offsets.push_back(offset);
            }
        }

        compressed_params_list.push_back(comp_param);
    }
}

// Fitting for all segments
inline void fitAllSegments(const std::string& expression_str, const std::vector<Interval>& intervals, std::vector<FitParameters>& fit_params_list) {
    for (const auto& interval : intervals) {
        FitParameters params = fitSegment(expression_str, interval);
        fit_params_list.push_back(params);
    }
}

// Save the compressed fit parameters to a file
inline void saveFitParametersToFile(const std::vector<FitParameters>& fit_params_list, const std::string& filename) {
    std::ofstream file(filename);
    if (file.is_open()) {
        file << "Order,a,b,c\n";
        for (const auto& params : fit_params_list) {
            file << params.order << "," << params.a << "," << params.b << "," << params.c << "\n";
        }
        file.close();
        std::cout << "Paramters saved to: " << filename << std::endl;
    } else {
        std::cout << "Fit Parameters Failed to Save!" << std::endl;
    }
}

// Checking the symmetry of the fitting parameters
inline void checkSymmetry(const std::vector<FitParameters>& fit_params_list, const std::vector<Interval>& intervals) {
    for (size_t i = 1; i < fit_params_list.size(); ++i) {
        const auto& params_prev = fit_params_list[i - 1];
        const auto& params_curr = fit_params_list[i];

        // Checking if the parameters are symmetric
        if (params_prev.order == params_curr.order &&
            std::abs(params_prev.a - params_curr.a) < 1e-5 &&
            std::abs(params_prev.b - params_curr.b) < 1e-5 &&
            std::abs(params_prev.c - params_curr.c) < 1e-5) {
            std::cout << "Interval [" << intervals[i - 1].start << ", " << intervals[i - 1].end << "] and Interval [" << intervals[i].start << ", " << intervals[i].end << "] 具有对称性。" << std::endl;
        }
    }
}

inline double RecoveredFunctionValue(const CompressedFitParameters& comp_param, double x, double offset) {
    const FitParameters& shared_params = comp_param.params;
    double y_pred;
    if (shared_params.order == 1) {
        // Linear fitting
        y_pred = shared_params.b * x + shared_params.c + offset;
    } else {
        // Quadratic fitting
        y_pred = shared_params.a * x * x + shared_params.b * x + shared_params.c + offset;
    }
    return y_pred;
}

// Evaluate the compressed error
inline double evaluateCompressedError(const std::string& expression_str,
                                      const std::vector<Interval>& intervals,
                                      const std::vector<CompressedFitParameters>& compressed_params_list) {
    double total_error = 0.0;
    size_t total_points = 0;

    for (const auto& comp_param : compressed_params_list) {
        const FitParameters& params = comp_param.params;

        for (size_t idx = 0; idx < comp_param.interval_indices.size(); ++idx) {
            size_t interval_idx = comp_param.interval_indices[idx];
            double offset = comp_param.offsets[idx];
            const Interval& interval = intervals[interval_idx];

            // Sample several points within the interval to evaluate the error
            size_t num_samples = 10; // Sample points within the interval
            double step = (interval.end - interval.start) / (num_samples - 1);

            for (size_t i = 0; i < num_samples; ++i) {
                double x = interval.start + i * step;

                // Calculate the original function value
                double y_true = computeFunctionValue(expression_str, x);

                // Calculate the recovered function value
                double y_pred = RecoveredFunctionValue(comp_param, x, offset);

                // Calculate the error
                double error = std::abs(y_true - y_pred);
                total_error += error;
                total_points++;
            }
        }
    }

    // Calculate the average error
    double average_error = total_error / total_points;
    return average_error;
}

// Save the compressed fit parameters to a file
inline void saveCompressedParametersToFile(
    const std::vector<CompressedFitParameters>& compressed_params_list,
    const std::string& filename) {
    std::ofstream file(filename);
    if (file.is_open()) {
        file << "Order,a,b,c,IntervalIndices,Offsets\n";
        for (const auto& comp_params : compressed_params_list) {
            const FitParameters& params = comp_params.params;
            file << params.order << "," << params.a << "," << params.b << "," << params.c << ",";

            // 将 interval_indices 和 offsets 序列化为字符串
            // 输出 interval_indices
            file << "\"";
            for (size_t i = 0; i < comp_params.interval_indices.size(); ++i) {
                file << comp_params.interval_indices[i];
                if (i < comp_params.interval_indices.size() - 1)
                    file << " "; // 用空格分隔
            }
            file << "\",";

            // 输出 offsets
            file << "\"";
            for (size_t i = 0; i < comp_params.offsets.size(); ++i) {
                file << comp_params.offsets[i];
                if (i < comp_params.offsets.size() - 1)
                    file << " "; // 用空格分隔
            }
            file << "\"\n";
        }
        file.close();
        std::cout << "Compressed parameters saved to: " << filename << std::endl;
    } else {
        std::cout << "Failed to save compressed parameters!" << std::endl;
    }
}

#endif // FUNCTION_FITTER_HPP

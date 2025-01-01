#ifndef FUNCTION_FITTER_HPP
#define FUNCTION_FITTER_HPP

#include <vector>
#include <string>
#include <cmath>
#include <iostream>
#include <fstream>
#include <mutex>
// #include <Eigen/Dense>
// #include <unsupported/Eigen/Splines> // Comment out if you don't want B-spline support at all
#include "exprtk.hpp"
#include "interval_optimizer.hpp"

enum class FittingMethod {
    Linear,
    Quadratic
    // BSpline // Comment out if you don't want even the enum value
};

// Define the FitParameters structure
struct FitParameters {
    FittingMethod method;
    double a; // Second order term coefficient
    double b; // One order term coefficient
    double c; // Constant term
    int order; // Order of the fitting function

    // Comment out B-spline related fields
    // Eigen::Spline<double, 1, 3> spline; // B-spline fitting
    // int degree; // Degree of the B-spline

    FitParameters(FittingMethod m = FittingMethod::Linear) : method(m), a(0.0), b(0.0), c(0.0), order(2)/*, degree(3)*/ {}
};

// Data structure for the shared parameters and offsets
struct CompressedFitParameters {
    FitParameters params;                  // Shared parameters
    std::vector<double> interval_indices;  // Interval indices sharing the parameters
    std::vector<double> offsets;           // Offsets
};

struct FittingParametersConfig {
    double error_threshold;
    double min_unit_length;
    double epsilon_start;
    double epsilon_end;
    size_t epsilon_steps;

    double acceptable_error; // Acceptable total error threshold
    // size_t num_control_points;
    // int bspline_degree;

    FittingParametersConfig() : error_threshold(1e-5), min_unit_length(1e-5), epsilon_start(1e-4), epsilon_end(2e-3), epsilon_steps(20), acceptable_error(1e-4) {}
};

// Evaluate the error of the fitting function
inline double evaluateError(const std::string& expression_str, const std::vector<Interval>& intervals, const std::vector<FitParameters>& fit_params_list) {
    double total_error = 0.0;
    size_t total_points = 0;

    std::mutex error_mutex;

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
            switch (params.method) {
                case FittingMethod::Linear:
                    y_pred = params.b * x + params.c;
                    break;
                case FittingMethod::Quadratic:
                    y_pred = params.a * x * x + params.b * x + params.c;
                    break;
                // case FittingMethod::BSpline:
                //     y_pred = params.spline(x);
                //     break;
                default:
                    y_pred = 0.0;
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

// Set an error threshold for model selection if needed
static const double error_threshold = 1e-5;

// Fitting for a single segment (linear or quadratic)
inline FitParameters fitSegment(const std::string& expression_str, const Interval& interval) {
    FitParameters params;
    double x0 = interval.start;
    double x1 = interval.end;
    double xm = (x0 + x1) / 2.0;

    double y0 = computeFunctionValue(expression_str, x0);
    double y1 = computeFunctionValue(expression_str, x1);
    double ym = computeFunctionValue(expression_str, xm);

    double linear_error = std::abs((y1 + y0) / 2.0 - ym);

    if (linear_error < error_threshold) {
        // Linear fitting: y = b * x + c
        params.method = FittingMethod::Linear;
        params.a = 0.0;
        params.b = (y1 - y0) / (x1 - x0);
        params.c = y0 - params.b * x0;
        params.order = 1;
    } else {
        // Quadratic fitting: y = a * x^2 + b * x + c
        params.method = FittingMethod::Quadratic;
        double denom = (x0 - x1) * (x0 - xm) * (x1 - xm);
        if (denom == 0.0) {
            // Linear fitting if denominator is zero
            params.method = FittingMethod::Linear;
            params.a = 0.0;
            params.b = (y1 - y0) / (x1 - x0);
            params.c = y0 - params.b * x0;
            params.order = 1;
        } else {
            params.a = (x1 * (ym - y0) + x0 * (y1 - ym) + xm * (y0 - y1)) / denom;
            params.b = ((x1 * x1) * (y0 - ym) + (x0 * x0) * (ym - y1) + (xm * xm) * (y1 - y0)) / denom;
            params.c = (x0 * (x1 * ym - xm * y1) + x1 * xm * y0 - x0 * xm * y1) / denom;
            params.order = 2;
        }
    }

    return params;
}

// Comment out B-spline fitting function
/*
inline FitParameters fitBSplineSegment(const std::string& expression_str, const Interval& interval, int bspline_degree = 3, size_t num_control_points = 4) {
    FitParameters params;
    params.method = FittingMethod::BSpline;
    params.degree = bspline_degree;

    double x0 = interval.start;
    double x1 = interval.end;

    std::vector<double> x_vals;
    std::vector<double> y_vals;
    size_t num_samples = num_control_points;
    for (size_t i = 0; i < num_samples; ++i) {
        double xi = x0 + i * (x1 - x0) / (num_samples - 1);
        x_vals.push_back(xi);
        y_vals.push_back(computeFunctionValue(expression_str, xi));
    }

    size_t K = bspline_degree;
    size_t N = num_control_points;
    size_t num_knots = N + K + 1;
    Eigen::VectorXd knots(num_knots);
    for (size_t i = 0; i < num_knots; ++i) {
        if (i < K + 1)
            knots(i) = x0;
        else if (i >= num_knots - K - 1)
            knots(i) = x1;
        else
            knots(i) = x0 + (x1 - x0) * (i - K) / static_cast<double>(num_knots - 2 * K - 1);
    }

    Eigen::MatrixXd data(x_vals.size(), 1);
    for (size_t i = 0; i < x_vals.size(); ++i) {
        data(i, 0) = y_vals[i];
    }

    try {
        params.spline = Eigen::SplineFitting<Eigen::Spline<double, 1>>::Interpolate(data, K, knots);
    } catch (const std::exception& e) {
        std::cerr << "B-spline fitting failed: " << e.what() << std::endl;
        // Fallback to linear fitting
        params.method = FittingMethod::Linear;
        params.a = 0.0;
        params.b = (y_vals.back() - y_vals.front()) / (x1 - x0);
        params.c = y_vals.front() - params.b * x0;
        params.order = 1;
    }

    return params;
}
*/

inline double estimateSegmentError(const std::string& expression_str, const Interval& interval, const FitParameters& params) {
    size_t num_samples = 10;
    double step = (interval.end - interval.start) / (num_samples - 1);
    double total_error = 0.0;

    for (size_t i = 0; i < num_samples; ++i) {
        double x = interval.start + i * step;
        double y_true = computeFunctionValue(expression_str, x);
        double y_pred;

        switch (params.method) {
            case FittingMethod::Linear:
                y_pred = params.b * x + params.c;
                break;
            case FittingMethod::Quadratic:
                y_pred = params.a * x * x + params.b * x + params.c;
                break;
            // case FittingMethod::BSpline:
            //     y_pred = params.spline(x);
            //     break;
            default:
                y_pred = 0.0;
        }
        
        total_error += std::abs(y_true - y_pred);
    }

    return total_error / num_samples;
}

inline FitParameters fitSegmentWithModels(const std::string& expression_str, const Interval& interval) {
    std::vector<std::pair<FitParameters,double>> candidate_models;

    // Fit polynomial model (linear or quadratic)
    FitParameters poly_params = fitSegment(expression_str, interval);
    double poly_error = estimateSegmentError(expression_str, interval, poly_params);
    candidate_models.emplace_back(std::make_pair(poly_params, poly_error));

    // Comment out B-spline model fitting
    /*
    FitParameters bspline_params = fitBSplineSegment(expression_str, interval);
    double bspline_error = estimateSegmentError(expression_str, interval, bspline_params);
    candidate_models.emplace_back(std::make_pair(bspline_params, bspline_error));
    */

    // Select the best model (now only polynomial)
    double best_score = std::numeric_limits<double>::max();
    FitParameters best_params;

    for (const auto& cand : candidate_models) {
        double error = cand.second;
        int param_count = 0;

        switch (cand.first.method) {
            case FittingMethod::Linear:
                param_count = 2; // b, c
                break;
            case FittingMethod::Quadratic:
                param_count = 3; // a, b, c
                break;
            // case FittingMethod::BSpline:
            //     // param_count = number_of_control_points; (not used now)
            //     break;
            default:
                param_count = 0;
                break;
        }

        double score = error + 1e-4 * param_count;
        if (score < best_score) {
            best_score = score;
            best_params = cand.first;
        }
    }

    return best_params;
}

inline void fitAllSegmentsMultiModel(const std::string& expression_str, const std::vector<Interval>& intervals, std::vector<FitParameters>& fit_params_list) {
    fit_params_list.clear();
    for (const auto& interval : intervals) {
        FitParameters params = fitSegmentWithModels(expression_str, interval);
        fit_params_list.push_back(params);
    }
}

// Compress fit parameters
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
        comp_param.interval_indices.push_back((double)i);
        comp_param.offsets.push_back(0.0); // Initial offset

        visited[i] = true;

        // Check for symmetry with other parameters
        for (size_t j = i + 1; j < n; ++j) {
            if (visited[j]) continue;

            bool is_symmetric = false;
            double offset = 0.0;

            // Only for linear/quadratic
            if (fit_params_list[i].method != FittingMethod::Quadratic &&
                fit_params_list[i].method != FittingMethod::Linear) {
                // If there's a method other than linear/quadratic, skip
            } else {
                if (std::abs(fit_params_list[i].a - fit_params_list[j].a) < error_threshold &&
                    std::abs(fit_params_list[i].b - fit_params_list[j].b) < error_threshold) {
                    double c_diff = fit_params_list[j].c - fit_params_list[i].c;
                    if (std::abs(c_diff) < error_threshold) {
                        is_symmetric = true;
                        offset = 0.0;
                    } else {
                        is_symmetric = true;
                        offset = c_diff;
                    }
                }
            }

            if (!is_symmetric &&
                std::abs(fit_params_list[i].a - fit_params_list[j].a) < error_threshold &&
                std::abs(fit_params_list[i].c - fit_params_list[j].c) < error_threshold &&
                std::abs(fit_params_list[i].b + fit_params_list[j].b) < error_threshold) {
                is_symmetric = true;
                offset = 0.0;
            }

            if (is_symmetric) {
                visited[j] = true;
                comp_param.interval_indices.push_back((double)j);
                comp_param.offsets.push_back(offset);
            }
        }

        compressed_params_list.push_back(comp_param);
    }
}

// Fit all segments (linear/quadratic)
inline void fitAllSegments(const std::string& expression_str, const std::vector<Interval>& intervals, std::vector<FitParameters>& fit_params_list) {
    fit_params_list.clear();
    for (const auto& interval : intervals) {
        FitParameters params = fitSegment(expression_str, interval);
        fit_params_list.push_back(params);
    }
}

// Save fit parameters to file
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

// Check symmetry
inline void checkSymmetry(const std::vector<FitParameters>& fit_params_list, const std::vector<Interval>& intervals) {
    for (size_t i = 1; i < fit_params_list.size(); ++i) {
        const auto& params_prev = fit_params_list[i - 1];
        const auto& params_curr = fit_params_list[i];

        // Only linear and quadratic
        if ((params_prev.method == FittingMethod::Linear || params_prev.method == FittingMethod::Quadratic) &&
            (params_curr.method == FittingMethod::Linear || params_curr.method == FittingMethod::Quadratic)) {
            if (params_prev.order == params_curr.order &&
                std::abs(params_prev.a - params_curr.a) < 1e-5 &&
                std::abs(params_prev.b - params_curr.b) < 1e-5 &&
                std::abs(params_prev.c - params_curr.c) < 1e-5) {
                std::cout << "Intervals [" << intervals[i - 1].start << ", " << intervals[i - 1].end << "] and ["
                          << intervals[i].start << ", " << intervals[i].end << "] are symmetric." << std::endl;
            }
        }
    }
}

inline double RecoveredFunctionValue(const CompressedFitParameters& comp_param, double x, double offset) {
    const FitParameters& shared_params = comp_param.params;
    double y_pred;
    if (shared_params.order == 1) {
        // Linear
        y_pred = shared_params.b * x + shared_params.c + offset;
    } else {
        // Quadratic
        y_pred = shared_params.a * x * x + shared_params.b * x + shared_params.c + offset;
    }
    return y_pred;
}

// Evaluate compressed error
inline double evaluateCompressedError(const std::string& expression_str,
                                      const std::vector<Interval>& intervals,
                                      const std::vector<CompressedFitParameters>& compressed_params_list) {
    double total_error = 0.0;
    size_t total_points = 0;

    for (const auto& comp_param : compressed_params_list) {
        const FitParameters& params = comp_param.params;

        for (size_t idx = 0; idx < comp_param.interval_indices.size(); ++idx) {
            size_t interval_idx = static_cast<size_t>(comp_param.interval_indices[idx]);
            double offset = comp_param.offsets[idx];
            const Interval& interval = intervals[interval_idx];

            // Sample several points
            size_t num_samples = 10; 
            double step = (interval.end - interval.start) / (num_samples - 1);

            for (size_t i = 0; i < num_samples; ++i) {
                double x = interval.start + i * step;
                double y_true = computeFunctionValue(expression_str, x);
                double y_pred = RecoveredFunctionValue(comp_param, x, offset);

                double error = std::abs(y_true - y_pred);
                total_error += error;
                total_points++;
            }
        }
    }

    double average_error = total_error / total_points;
    return average_error;
}

// Save compressed fit parameters to a file
inline void saveCompressedParametersToFile(
    const std::vector<CompressedFitParameters>& compressed_params_list,
    const std::string& filename) {
    std::ofstream file(filename);
    if (file.is_open()) {
        file << "Method,a,b,c,IntervalIndices,Offsets\n";
        for (const auto& comp_params : compressed_params_list) {
            const FitParameters& params = comp_params.params;
            std::string method_str;
            switch (params.method) {
                case FittingMethod::Linear:
                    method_str = "Linear";
                    break;
                case FittingMethod::Quadratic:
                    method_str = "Quadratic";
                    break;
                // case FittingMethod::BSpline:
                //     method_str = "BSpline";
                //     break;
                default:
                    method_str = "Unknown";
            }

            file << method_str << "," << params.a << "," << params.b << "," << params.c << ",";

            file << "\"";
            for (size_t i = 0; i < comp_params.interval_indices.size(); ++i) {
                file << comp_params.interval_indices[i];
                if (i < comp_params.interval_indices.size() - 1)
                    file << " ";
            }
            file << "\",";

            file << "\"";
            for (size_t i = 0; i < comp_params.offsets.size(); ++i) {
                file << comp_params.offsets[i];
                if (i < comp_params.offsets.size() - 1)
                    file << " ";
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

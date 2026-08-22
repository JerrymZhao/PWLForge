#ifndef FUNCTION_FITTER_HPP
#define FUNCTION_FITTER_HPP

#include <vector>
#include <string>
#include <cmath>
#include <iostream>
#include <fstream>
#include <limits>
#include <algorithm>
#include "common_utils.hpp"
#include "interval_types.hpp"

// Note: FitParameters, CompressedFitParameters, Interval are defined in interval_types.hpp

// ============================================================================
// Segment Evaluation
// ============================================================================

inline double evaluateSegment(double x, const FitParameters& params) {
    switch (params.method) {
        case FittingMethod::Linear:
            return params.b * x + params.c;
        case FittingMethod::Quadratic:
            return params.a * x * x + params.b * x + params.c;
        default:
            return 0.0;
    }
}

// ============================================================================
// Error Evaluation
// ============================================================================

inline double estimateSegmentError(const std::string& expression_str,
                                  const Interval& interval,
                                  const FitParameters& params) {
    size_t num_samples = 10;
    double step = (interval.end - interval.start) / (num_samples - 1);
    double total_error = 0.0;

    for (size_t i = 0; i < num_samples; ++i) {
        double x = interval.start + i * step;
        double y_true = computeFunctionValue(expression_str, x);
        double y_pred = evaluateSegment(x, params);
        total_error += std::abs(y_true - y_pred);
    }

    return total_error / num_samples;
}

inline double evaluateError(const std::string& expression_str,
                           const std::vector<Interval>& intervals,
                           const std::vector<FitParameters>& fit_params_list) {
    if (intervals.size() != fit_params_list.size()) {
        std::cerr << "Error: Size mismatch between intervals and fit parameters\n";
        return std::numeric_limits<double>::max();
    }

    double total_error = 0.0;
    size_t total_points = 0;

    for (size_t idx = 0; idx < intervals.size(); ++idx) {
        const Interval& interval = intervals[idx];
        const FitParameters& params = fit_params_list[idx];

        size_t num_samples = 10;
        for (size_t i = 0; i < num_samples; ++i) {
            double t = static_cast<double>(i) / (num_samples - 1);
            double x = interval.start * (1 - t) + interval.end * t;

            double y_true = computeFunctionValue(expression_str, x);
            if (std::isnan(y_true)) continue;

            double y_pred = evaluateSegment(x, params);
            total_error += std::abs(y_true - y_pred);
            total_points++;
        }
    }

    return (total_points == 0) ? 0.0 : total_error / total_points;
}

inline double evaluateCompressedError(const std::string& expression_str,
                                     const std::vector<Interval>& intervals,
                                     const std::vector<CompressedFitParameters>& compressed_params_list) {
    if (intervals.empty() || compressed_params_list.empty()) {
        return 0.0;
    }

    double total_error = 0.0;
    size_t valid_points = 0;
    const size_t num_samples = 10;

    for (const auto& comp_param : compressed_params_list) {
        for (size_t idx = 0; idx < comp_param.interval_indices.size(); ++idx) {
            if (idx >= comp_param.offsets.size()) continue;

            size_t interval_idx = static_cast<size_t>(comp_param.interval_indices[idx]);
            if (interval_idx >= intervals.size()) continue;

            const Interval& interval = intervals[interval_idx];
            double offset = comp_param.offsets[idx];
            double step = (interval.end - interval.start) / (num_samples - 1);

            for (size_t i = 0; i < num_samples; ++i) {
                double x = interval.start + i * step;
                double y_true = computeFunctionValue(expression_str, x);

                const FitParameters& shared_params = comp_param.params;
                double y_pred;
                if (shared_params.order == 1) {
                    y_pred = shared_params.b * x + shared_params.c + offset;
                } else {
                    y_pred = shared_params.a * x * x + shared_params.b * x + shared_params.c + offset;
                }

                if (!std::isnan(y_true) && !std::isnan(y_pred)) {
                    total_error += std::abs(y_true - y_pred);
                    valid_points++;
                }
            }
        }
    }

    return (valid_points == 0) ? 0.0 : total_error / valid_points;
}

// ============================================================================
// Polynomial Fitting
// ============================================================================

inline FitParameters fitSegment(const std::string& expression_str,
                               const Interval& interval,
                               double error_threshold) {
    FitParameters params;
    params.range_start = interval.start;
    params.range_end = interval.end;
    double xm = (params.range_start + params.range_end) / 2.0;

    double y0 = computeFunctionValue(expression_str, params.range_start);
    double y1 = computeFunctionValue(expression_str, params.range_end);
    double ym = computeFunctionValue(expression_str, xm);

    // Try linear fit first
    params.method = FittingMethod::Linear;
    params.a = 0.0;
    params.b = (y1 - y0) / (params.range_end - params.range_start);
    params.c = y0 - params.b * params.range_start;
    params.order = 1;

    double linear_error = std::abs((y1 + y0) / 2.0 - ym);

    // Try quadratic if linear error is too high
    if (linear_error > error_threshold * 0.8) {
        params.method = FittingMethod::Quadratic;
        double denom = (params.range_start - params.range_end) *
                      (params.range_start - xm) * (params.range_end - xm);

        if (std::abs(denom) > 1e-7) {
            params.a = (params.range_end * (ym - y0) +
                       params.range_start * (y1 - ym) +
                       xm * (y0 - y1)) / denom;
            params.b = ((params.range_end * params.range_end) * (y0 - ym) +
                       (params.range_start * params.range_start) * (ym - y1) +
                       (xm * xm) * (y1 - y0)) / denom;
            params.c = (params.range_start * (params.range_end * ym - xm * y1) +
                       params.range_end * xm * y0 -
                       params.range_start * xm * y1) / denom;
            params.order = 2;

            double quad_error = estimateSegmentError(expression_str, interval, params);

            // Use linear if quadratic doesn't improve much
            if (quad_error >= linear_error * 1.2) {
                params.method = FittingMethod::Linear;
                params.a = 0.0;
                params.b = (y1 - y0) / (params.range_end - params.range_start);
                params.c = y0 - params.b * params.range_start;
                params.order = 1;
            }
        }
    }

    return params;
}

inline void fitAllSegments(const std::string& expression_str,
                          const std::vector<Interval>& intervals,
                          std::vector<FitParameters>& fit_params_list,
                          double error_threshold) {
    fit_params_list.clear();
    fit_params_list.reserve(intervals.size());

    for (const auto& interval : intervals) {
        FitParameters params = fitSegment(expression_str, interval, error_threshold);
        fit_params_list.push_back(params);
    }
}

// ============================================================================
// Compression (Symmetry-based parameter sharing)
// ============================================================================

inline void compressFitParameters(const std::vector<FitParameters>& fit_params_list,
                                  const std::vector<Interval>& intervals,
                                  std::vector<CompressedFitParameters>& compressed_params_list,
                                  double error_threshold) {
    size_t n = fit_params_list.size();
    std::vector<bool> visited(n, false);

    for (size_t i = 0; i < n; ++i) {
        if (visited[i]) continue;

        CompressedFitParameters comp_param;
        comp_param.params = fit_params_list[i];
        comp_param.interval_indices.push_back(static_cast<double>(i));
        comp_param.offsets.push_back(0.0);

        visited[i] = true;

        // Find similar parameters that can share coefficients
        for (size_t j = i + 1; j < n; ++j) {
            if (visited[j]) continue;

            bool is_symmetric = false;
            double offset = 0.0;

            // Check if methods match
            if (fit_params_list[i].method == fit_params_list[j].method) {
                // Check if a and b coefficients are similar
                if (std::abs(fit_params_list[i].a - fit_params_list[j].a) < error_threshold &&
                    std::abs(fit_params_list[i].b - fit_params_list[j].b) < error_threshold) {
                    // c can differ by an offset
                    double c_diff = fit_params_list[j].c - fit_params_list[i].c;
                    if (std::abs(c_diff) < error_threshold * 10) {
                        is_symmetric = true;
                        offset = c_diff;
                    }
                }
            }

            if (is_symmetric) {
                visited[j] = true;
                comp_param.interval_indices.push_back(static_cast<double>(j));
                comp_param.offsets.push_back(offset);
            }
        }

        compressed_params_list.push_back(comp_param);
    }
}

// ============================================================================
// File I/O
// ============================================================================

inline void saveFitParametersToFile(const std::vector<FitParameters>& fit_params_list,
                                   const std::string& filename) {
    std::vector<FitParameters> sorted_params = fit_params_list;
    std::sort(sorted_params.begin(), sorted_params.end(),
             [](const FitParameters &a, const FitParameters &b) {
                 return a.range_start < b.range_start;
             });

    std::ofstream file(filename);
    if (file.is_open()) {
        file << "Order,a,b,c,RangeStart,RangeEnd\n";
        for (const auto& params : sorted_params) {
            file << params.order << ","
                 << params.a << ","
                 << params.b << ","
                 << params.c << ","
                 << params.range_start << ","
                 << params.range_end << "\n";
        }
        file.close();
        std::cout << "Saved fit parameters to: " << filename << "\n";
    } else {
        std::cerr << "Failed to open file for writing: " << filename << "\n";
    }
}

inline void saveCompressedParametersToFile(
    const std::vector<CompressedFitParameters>& compressed_params_list,
    const std::string& filename) {

    std::ofstream file(filename);
    if (file.is_open()) {
        file << "GroupID,Order,a,b,c,IntervalIndices,Offsets\n";

        for (size_t group_id = 0; group_id < compressed_params_list.size(); ++group_id) {
            const auto& comp_param = compressed_params_list[group_id];

            file << group_id << ","
                 << comp_param.params.order << ","
                 << comp_param.params.a << ","
                 << comp_param.params.b << ","
                 << comp_param.params.c << ",\"";

            // Write interval indices
            for (size_t i = 0; i < comp_param.interval_indices.size(); ++i) {
                if (i > 0) file << ";";
                file << comp_param.interval_indices[i];
            }
            file << "\",\"";

            // Write offsets
            for (size_t i = 0; i < comp_param.offsets.size(); ++i) {
                if (i > 0) file << ";";
                file << comp_param.offsets[i];
            }
            file << "\"\n";
        }

        file.close();
        std::cout << "Saved compressed parameters to: " << filename << "\n";
    } else {
        std::cerr << "Failed to open file for writing: " << filename << "\n";
    }
}

#endif // FUNCTION_FITTER_HPP
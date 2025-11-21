#ifndef INTERVAL_SPLIT_HPP
#define INTERVAL_SPLIT_HPP

#include <vector>
#include <future>
#include <limits>
#include <unordered_set>
#include <algorithm>
#include "common_utils.hpp"
#include "interval_types.hpp"

// Forward declarations from function_fitter.hpp
struct FitParameters;
FitParameters fitSegment(const std::string& expression_str, const Interval& interval, double error_threshold);
double estimateSegmentError(const std::string& expression_str, const Interval& interval, const FitParameters& params);
double evaluateSegment(double x, const FitParameters& params);

// ============================================================================
// Split Decision Logic
// ============================================================================

inline bool shouldSplit(const Interval& interval,
                       double epsilon,
                       const std::string& expression_str) {
    double start = interval.start;
    double mid = (interval.start + interval.end) / 2.0;
    double end = interval.end;

    double f_start = computeFunctionValue(expression_str, start);
    double f_mid = computeFunctionValue(expression_str, mid);
    double f_end = computeFunctionValue(expression_str, end);

    // Adaptive threshold based on curvature and length
    double curvature_factor = 1.0 + std::abs(interval.hessian);
    double length_factor = 1.0 + (interval.end - interval.start);
    double adaptive_epsilon = epsilon / (curvature_factor + length_factor);

    // Linearity check
    double linear_interp = (f_start + f_end) / 2.0;
    double linearity_error = std::abs(f_mid - linear_interp);
    double value_range = std::max(std::abs(f_end - f_start), 1e-6);
    double normalized_error = linearity_error / value_range;

    // Slope check
    double left_slope = (f_mid - f_start) / (mid - start);
    double right_slope = (f_end - f_mid) / (end - mid);
    double slope_diff = std::abs(right_slope - left_slope);
    double slope_threshold = adaptive_epsilon * (1.0 + std::abs(interval.hessian));

    bool should_split = (normalized_error > adaptive_epsilon * 1.2) ||
                       (slope_diff > slope_threshold * 0.8) ||
                       (std::abs(interval.hessian) > adaptive_epsilon * 8.0);

    if (!should_split) {
        double left_hessian = computeHessian(expression_str, interval.start + (mid - interval.start) / 4.0);
        double right_hessian = computeHessian(expression_str, mid + (interval.end - mid) / 4.0);
        should_split |= std::abs(left_hessian - right_hessian) > adaptive_epsilon * 4.0;
    }

    return should_split;
}

// ============================================================================
// Basic Binary Split
// ============================================================================

inline void splitInterval(const Interval& interval,
                         double epsilon,
                         double min_unit_length,
                         const std::string& expression_str,
                         std::vector<Interval>& result,
                         double target_error,
                         double relax_factor = 1.0) {
    double length = interval.end - interval.start;
    double normalized_diff = computeNormalizedFunctionDifference(expression_str, 
                                                                 interval.start, interval.end);

    // Check termination conditions
    if (length <= min_unit_length || normalized_diff < relax_factor * epsilon) {
        FitParameters temp_params = fitSegment(expression_str, interval, target_error);
        double temp_error = estimateSegmentError(expression_str, interval, temp_params);
        if (temp_error <= target_error) {
            result.push_back(interval);
            return;
        }
    }

    const double adaptive_epsilon = std::min(epsilon, target_error * 0.8);
    if (!shouldSplit(interval, adaptive_epsilon * relax_factor, expression_str)) {
        result.push_back(interval);
        return;
    }

    // Binary split
    double mid = (interval.start + interval.end) / 2.0;
    double left_hessian = computeHessian(expression_str, (interval.start + mid) / 2.0);
    double right_hessian = computeHessian(expression_str, (mid + interval.end) / 2.0);

    Interval left = {interval.start, mid, left_hessian, interval.level + 1};
    Interval right = {mid, interval.end, right_hessian, interval.level + 1};

    std::vector<Interval> left_result, right_result;

    auto left_future = std::async(std::launch::async, [&]() {
        splitInterval(left, epsilon, min_unit_length, expression_str, 
                     left_result, target_error, relax_factor);
    });

    auto right_future = std::async(std::launch::async, [&]() {
        splitInterval(right, epsilon, min_unit_length, expression_str, 
                     right_result, target_error, relax_factor);
    });

    left_future.wait();
    right_future.wait();

    result.insert(result.end(), left_result.begin(), left_result.end());
    result.insert(result.end(), right_result.begin(), right_result.end());
}

// ============================================================================
// Adaptive High-Error Interval Refinement
// ============================================================================

inline void adaptiveSplitHighErrorInterval(const Interval& interval,
                                          double target_error,
                                          double min_unit_length,
                                          const std::string& expression_str,
                                          std::vector<Interval>& result) {
    double adaptive_min_unit_length = std::min(min_unit_length, target_error * 0.01);

    if (interval.end - interval.start <= adaptive_min_unit_length) {
        result.push_back(interval);
        return;
    }

    // Evaluate current error
    double current_error;
    try {
        FitParameters params = fitSegment(expression_str, interval, target_error);
        current_error = estimateSegmentError(expression_str, interval, params);

        if (current_error <= target_error) {
            result.push_back(interval);
            return;
        }
    } catch (const std::exception&) {
        current_error = std::numeric_limits<double>::max();
    }

    double error_ratio = current_error / target_error;
    double adaptive_epsilon = std::min(0.001, target_error / (error_ratio * 4));

    int min_split_count = std::min(8, std::max(2,
        static_cast<int>((interval.end - interval.start) / adaptive_min_unit_length / 2)));

    // High error ratio: aggressive multi-way split
    if (error_ratio > 10.0) {
        for (int i = 0; i < min_split_count; ++i) {
            double sub_start = interval.start + i * (interval.end - interval.start) / min_split_count;
            double sub_end = interval.start + (i + 1) * (interval.end - interval.start) / min_split_count;
            double mid_hessian = computeHessian(expression_str, (sub_start + sub_end) / 2.0);

            Interval sub_interval = {sub_start, sub_end, mid_hessian, interval.level + 1};

            std::vector<Interval> sub_result;
            splitInterval(sub_interval, adaptive_epsilon, adaptive_min_unit_length, 
                         expression_str, sub_result, target_error, 0.4);

            result.insert(result.end(), sub_result.begin(), sub_result.end());
        }
        return;
    }

    // Moderate error: feature-based splitting
    const size_t num_samples = 31;
    std::vector<double> sample_points;
    std::vector<double> sample_errors;

    for (size_t i = 0; i <= num_samples; ++i) {
        double t = static_cast<double>(i) / num_samples;
        double x = interval.start + t * (interval.end - interval.start);
        sample_points.push_back(x);
    }

    try {
        FitParameters params = fitSegment(expression_str, interval, target_error);
        sample_errors.resize(sample_points.size());

        // Parallel error computation
        std::vector<std::future<void>> futures;
        for (size_t i = 0; i < sample_points.size(); ++i) {
            futures.push_back(std::async(std::launch::async, [&, i]() {
                double x = sample_points[i];
                double actual = computeFunctionValue(expression_str, x);
                double approx = evaluateSegment(x, params);
                sample_errors[i] = std::abs(actual - approx);
            }));
        }

        for (auto& future : futures) {
            future.wait();
        }

        double avg_error = 0.0;
        for (double err : sample_errors) {
            avg_error += err;
        }
        avg_error /= sample_errors.size();

        // Find error peaks
        std::vector<size_t> peak_indices;
        double peak_threshold = std::max(target_error * 0.5, avg_error * 0.8);

        for (size_t i = 1; i < sample_errors.size() - 1; ++i) {
            if (sample_errors[i] > peak_threshold &&
                sample_errors[i] > sample_errors[i-1] &&
                sample_errors[i] > sample_errors[i+1]) {
                peak_indices.push_back(i);
            }
        }

        // If no peaks, select highest error points
        if (peak_indices.empty()) {
            std::vector<std::pair<double, size_t>> error_idx_pairs;
            for (size_t i = 1; i < sample_errors.size() - 1; ++i) {
                error_idx_pairs.push_back({sample_errors[i], i});
            }

            std::sort(error_idx_pairs.begin(), error_idx_pairs.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });

            int max_points = std::min(min_split_count - 1, static_cast<int>(error_idx_pairs.size()));
            for (int i = 0; i < max_points; ++i) {
                peak_indices.push_back(error_idx_pairs[i].second);
            }

            std::sort(peak_indices.begin(), peak_indices.end());
        }

        // Split at peaks
        if (!peak_indices.empty()) {
            std::vector<double> split_points = {interval.start};
            for (size_t idx : peak_indices) {
                if (idx > 0 && idx < sample_points.size() - 1) {
                    split_points.push_back(sample_points[idx]);
                }
            }
            split_points.push_back(interval.end);

            // Filter too-close split points
            std::vector<double> filtered_points = {split_points[0]};
            for (size_t i = 1; i < split_points.size(); ++i) {
                if (split_points[i] - filtered_points.back() >= adaptive_min_unit_length) {
                    filtered_points.push_back(split_points[i]);
                }
            }

            // Create sub-intervals
            for (size_t i = 0; i < filtered_points.size() - 1; ++i) {
                double start = filtered_points[i];
                double end = filtered_points[i+1];

                double mid_hessian = computeHessian(expression_str, (start + end) / 2.0);
                Interval sub_interval = {start, end, mid_hessian, interval.level + 1};

                std::vector<Interval> sub_result;
                splitInterval(sub_interval, adaptive_epsilon, adaptive_min_unit_length,
                             expression_str, sub_result, target_error, 0.5);

                result.insert(result.end(), sub_result.begin(), sub_result.end());
            }

            if (result.size() > 1) return;
        }
    } catch (const std::exception&) {
        // Fall through to standard split
    }

    // Try standard split
    std::vector<Interval> split_result;
    splitInterval(interval, adaptive_epsilon, adaptive_min_unit_length,
                 expression_str, split_result, target_error, 0.4);

    if (split_result.size() > 1) {
        result = std::move(split_result);
        return;
    }

    // Forced multi-way split
    result.clear();
    for (int i = 0; i < min_split_count; ++i) {
        double sub_start = interval.start + i * (interval.end - interval.start) / min_split_count;
        double sub_end = interval.start + (i + 1) * (interval.end - interval.start) / min_split_count;

        if (sub_end - sub_start < adaptive_min_unit_length) continue;

        double mid_hessian = computeHessian(expression_str, (sub_start + sub_end) / 2.0);
        result.push_back({sub_start, sub_end, mid_hessian, interval.level + 1});
    }

    if (result.empty()) {
        result.push_back(interval);
    }
}

// ============================================================================
// High Error Interval Detection and Refinement
// ============================================================================

inline void identifyAndRefineHighErrorIntervals(std::vector<Interval>& intervals,
                                               double target_error,
                                               const std::string& expression_str,
                                               double min_unit_length) {
    if (intervals.empty()) return;

    // Compute errors for all intervals
    std::vector<std::pair<size_t, double>> interval_errors;
    interval_errors.reserve(intervals.size());

    std::mutex error_mutex;
    std::vector<std::future<void>> futures;

    for (size_t i = 0; i < intervals.size(); ++i) {
        futures.push_back(std::async(std::launch::async, [&, i]() {
            try {
                const auto& interval = intervals[i];
                FitParameters params = fitSegment(expression_str, interval, target_error);
                double error = estimateSegmentError(expression_str, interval, params);

                std::lock_guard<std::mutex> lock(error_mutex);
                interval_errors.push_back({i, error});
            } catch (const std::exception&) {
                std::lock_guard<std::mutex> lock(error_mutex);
                interval_errors.push_back({i, std::numeric_limits<double>::max()});
            }
        }));
    }

    for (auto& future : futures) {
        future.wait();
    }

    // Sort by error
    std::sort(interval_errors.begin(), interval_errors.end(),
             [](const std::pair<size_t, double>& a, const std::pair<size_t, double>& b) {
                 return a.second < b.second;
             });

    // Identify outliers using IQR method
    const size_t n = interval_errors.size();
    double q1 = interval_errors[n / 4].second;
    double q3 = interval_errors[3 * n / 4].second;
    double iqr = q3 - q1;

    double outlier_threshold = q3 + 2.0 * iqr;
    outlier_threshold = std::max(outlier_threshold, target_error * 5.0);

    std::vector<size_t> high_error_indices;
    for (auto& pair : interval_errors) {
        if (pair.second > outlier_threshold) {
            high_error_indices.push_back(pair.first);
        }
    }

    if (high_error_indices.empty()) return;

    // Refine high error intervals
    std::vector<Interval> refined_intervals;
    refined_intervals.reserve(intervals.size() + high_error_indices.size());

    std::unordered_set<size_t> indices_set(high_error_indices.begin(), high_error_indices.end());

    for (size_t i = 0; i < intervals.size(); ++i) {
        if (indices_set.find(i) != indices_set.end()) {
            std::vector<Interval> split_results;
            adaptiveSplitHighErrorInterval(intervals[i], target_error, min_unit_length,
                                          expression_str, split_results);
            refined_intervals.insert(refined_intervals.end(), split_results.begin(), split_results.end());
        } else {
            refined_intervals.push_back(intervals[i]);
        }
    }

    intervals = std::move(refined_intervals);
    ensureNoGaps(intervals);
}

#endif // INTERVAL_SPLIT_HPP
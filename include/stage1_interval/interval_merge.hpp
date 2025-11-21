#ifndef INTERVAL_MERGE_HPP
#define INTERVAL_MERGE_HPP

#include <vector>
#include <future>
#include <algorithm>
#include <map>
#include <sstream>
#include <iomanip>
#include "common_utils.hpp"
#include "interval_types.hpp"

// Forward declarations
struct FitParameters;
FitParameters fitSegment(const std::string& expression_str, const Interval& interval, double error_threshold);
double estimateSegmentError(const std::string& expression_str, const Interval& interval, const FitParameters& params);

// ============================================================================
// Merge Feasibility Checks
// ============================================================================

inline bool quickMergeCheck(const Interval& a, const Interval& b,
                           double target_error,
                           const std::string& expression_str) {
    const double min_len_threshold = 1e-8;
    if ((a.end - a.start) < min_len_threshold || (b.end - b.start) < min_len_threshold) {
        return true;
    }

    if (std::abs(a.end - b.start) > 1e-9) {
        return false;
    }

    if (std::abs(a.hessian - b.hessian) > 100.0 * target_error) {
        return false;
    }

    double f_a_start = computeFunctionValue(expression_str, a.start);
    double f_b_end = computeFunctionValue(expression_str, b.end);
    double f_mid = computeFunctionValue(expression_str, (a.end + b.start) / 2.0);

    double t = 0.5;
    double f_linear = f_a_start * (1-t) + f_b_end * t;

    double error = std::abs(f_mid - f_linear);
    return error < 5.0 * target_error;
}

inline bool canMerge(const Interval& a, const Interval& b,
                    double target_error,
                    const std::string& expression_str,
                    double error_factor = 1.0) {
    if (!quickMergeCheck(a, b, target_error, expression_str)) {
        return false;
    }

    Interval merged{a.start, b.end, 0.0, std::max(a.level, b.level)};

    double interval_length = merged.end - merged.start;
    double domain_length = 1.0;
    double length_factor = std::min(2.0, 1.0 + interval_length/domain_length * 10.0);
    double adaptive_error_factor = error_factor * length_factor;

    // Quick linear check
    const std::vector<double> key_points = {
        merged.start,
        merged.start + (merged.end - merged.start) * 0.25,
        (merged.start + merged.end) * 0.5,
        merged.end - (merged.end - merged.start) * 0.25,
        merged.end
    };

    double f_start = computeFunctionValue(expression_str, merged.start);
    double f_end = computeFunctionValue(expression_str, merged.end);
    double max_linear_error = 0.0;

    std::vector<double> errors(key_points.size() - 2);
    std::vector<std::future<void>> futures;

    for (size_t i = 1; i < key_points.size() - 1; ++i) {
        futures.push_back(std::async(std::launch::async, [&, i]() {
            double x = key_points[i];
            double actual = computeFunctionValue(expression_str, x);
            double t = (x - merged.start) / (merged.end - merged.start);
            double predicted = f_start * (1-t) + f_end * t;
            errors[i-1] = std::abs(actual - predicted);
        }));
    }

    for (auto& f : futures) {
        f.wait();
    }

    for (double error : errors) {
        max_linear_error = std::max(max_linear_error, error);
    }

    if (max_linear_error < 0.5 * target_error * adaptive_error_factor) {
        return true;
    }

    // Detailed fit check
    try {
        FitParameters params = fitSegment(expression_str, merged, target_error);
        double fit_error = estimateSegmentError(expression_str, merged, params);
        return fit_error <= target_error * adaptive_error_factor;
    } catch (...) {
        return false;
    }
}

inline bool canMergeWindow(const std::vector<Interval>& intervals,
                          size_t start_idx,
                          size_t window_size,
                          double target_error,
                          const std::string& expression_str,
                          double error_factor = 1.0) {
    if (start_idx + window_size > intervals.size()) return false;

    const Interval& first = intervals[start_idx];
    const Interval& last = intervals[start_idx + window_size - 1];

    // Check continuity
    for (size_t i = start_idx; i < start_idx + window_size - 1; ++i) {
        if (std::abs(intervals[i].end - intervals[i+1].start) > 1e-9) {
            return false;
        }
    }

    // Large window optimization: coarse check first
    if (window_size > 8) {
        double max_hessian_diff = 0.0;

        for (size_t i = start_idx; i < start_idx + window_size - 1; ++i) {
            max_hessian_diff = std::max(max_hessian_diff,
                                       std::abs(intervals[i].hessian - intervals[i+1].hessian));
        }

        if (max_hessian_diff > target_error * error_factor * 10.0) {
            return false;
        }

        // Sparse sampling check
        double merged_length = last.end - first.start;
        std::vector<double> sample_points = {
            first.start,
            first.start + merged_length * 0.25,
            first.start + merged_length * 0.5,
            first.start + merged_length * 0.75,
            last.end
        };

        double f_start = computeFunctionValue(expression_str, first.start);
        double f_end = computeFunctionValue(expression_str, last.end);
        double max_linear_error = 0.0;

        std::vector<double> errors(3);
        std::vector<std::future<void>> futures;

        for (size_t i = 1; i < 4; ++i) {
            futures.push_back(std::async(std::launch::async, [&, i]() {
                double x = sample_points[i];
                double actual = computeFunctionValue(expression_str, x);
                double t = (x - first.start) / (last.end - first.start);
                double predicted = f_start * (1-t) + f_end * t;
                errors[i-1] = std::abs(actual - predicted);
            }));
        }

        for (auto& f : futures) {
            f.wait();
        }

        for (double error : errors) {
            max_linear_error = std::max(max_linear_error, error);
        }

        if (max_linear_error > target_error * error_factor * 2.0) {
            return false;
        }
    }

    // Create merged interval
    Interval merged{first.start, last.end, 0.0, 0};

    for (size_t i = start_idx; i < start_idx + window_size; ++i) {
        merged.level = std::max(merged.level, intervals[i].level);
    }

    // Precise fit check
    try {
        FitParameters params = fitSegment(expression_str, merged, target_error);
        double fit_error = estimateSegmentError(expression_str, merged, params);
        return fit_error <= target_error * error_factor;
    } catch (...) {
        return false;
    }
}

// ============================================================================
// Interval Creation
// ============================================================================

inline Interval createMergedPair(const Interval& iv1, const Interval& iv2,
                                const std::string& expression_str, double tol = 1e-9) {
    if (std::abs(iv1.end - iv2.start) > tol) {
        throw std::runtime_error("Non-contiguous intervals");
    }

    Interval merged;
    merged.start = iv1.start;
    merged.end = iv2.end;
    merged.level = std::max(iv1.level, iv2.level);

    const double mid_point = (merged.start + merged.end) / 2.0;
    merged.hessian = computeHessian(expression_str, mid_point);

    return merged;
}

// ============================================================================
// Merge Execution
// ============================================================================

struct MergeConfig {
    size_t max_iterations = 20;
    size_t max_window_size = 128;
    double base_error_factor = 4.0;
    double final_error_factor = 2.5;
    double verification_error_factor = 3.0;
    double continuity_threshold = 1e-12;
};

inline void executeMergePass(std::vector<Interval>& intervals,
                            double target_error,
                            const std::string& expression_str,
                            double current_error_factor,
                            const MergeConfig& config,
                            double domain_start,
                            double domain_end,
                            bool& merged_flag) {
    std::vector<Interval> merged_intervals;
    merged_intervals.reserve(intervals.size());

    size_t i = 0;
    while (i < intervals.size()) {
        // Handle last interval
        if (i == intervals.size() - 1) {
            Interval last_iv = intervals[i];
            last_iv.end = domain_end;
            merged_intervals.push_back(last_iv);
            i++;
            continue;
        }

        // Fix gaps
        if (i < intervals.size() - 1 &&
            intervals[i+1].start > intervals[i].end + config.continuity_threshold) {
            Interval extended = intervals[i];
            extended.end = intervals[i+1].start;
            merged_intervals.push_back(extended);
            i++;
            continue;
        }

        // Find best merge window
        size_t best_window = 1;
        size_t max_possible_window = intervals.size() - i;
        
        double avg_hessian = 0.0;
        for (size_t j = 0; j < std::min(size_t(4), max_possible_window); j++) {
            avg_hessian += std::abs(intervals[i+j].hessian);
        }
        avg_hessian /= std::min(size_t(4), max_possible_window);

        // Select window sizes based on curvature
        std::vector<size_t> window_sizes;
        if (avg_hessian < target_error * 10) {
            window_sizes = {64, 32, 16, 8, 4, 2};
        } else if (avg_hessian < target_error * 100) {
            window_sizes = {32, 16, 8, 4, 2};
        } else {
            window_sizes = {8, 4, 2};
        }

        // Parallel window checks
        std::vector<std::future<std::pair<bool, size_t>>> merge_results;
        for (size_t window : window_sizes) {
            if (i + window > intervals.size()) continue;

            merge_results.push_back(std::async(std::launch::async,
                [&, window]() -> std::pair<bool, size_t> {
                    bool result = canMergeWindow(intervals, i, window, target_error,
                                                 expression_str, current_error_factor * 1.2);
                    return {result, window};
                }
            ));

            if (merge_results.size() >= std::thread::hardware_concurrency()) {
                break;
            }
        }

        for (auto& future : merge_results) {
            auto result = future.get();
            if (result.first && result.second > best_window) {
                best_window = result.second;
            }
        }

        // Binary search for optimal window
        if (best_window > 1) {
            size_t low = best_window + 1;
            size_t high = std::min(config.max_window_size, intervals.size() - i);

            while (low <= high && high - low > 4) {
                size_t mid = low + (high - low) / 2;

                if (canMergeWindow(intervals, i, mid, target_error,
                                  expression_str, current_error_factor)) {
                    best_window = mid;
                    low = mid + 1;
                } else {
                    high = mid - 1;
                }
            }

            // Execute best merge
            try {
                Interval final_merged = intervals[i];
                for (size_t j = 1; j < best_window; j++) {
                    final_merged = createMergedPair(final_merged, intervals[i+j], expression_str);
                }
                
                if (i + best_window < intervals.size()) {
                    if (final_merged.end < intervals[i + best_window].start) {
                        final_merged.end = intervals[i + best_window].start;
                    }
                }

                FitParameters verified_params = fitSegment(expression_str, final_merged, target_error);
                double verified_error = estimateSegmentError(expression_str, final_merged, verified_params);

                if (verified_error <= target_error * config.verification_error_factor) {
                    merged_intervals.push_back(final_merged);
                    i += best_window;
                    merged_flag = true;
                } else {
                    merged_intervals.push_back(intervals[i]);
                    i++;
                }
            } catch (const std::exception&) {
                merged_intervals.push_back(intervals[i]);
                i++;
            }
        }
        // Try pair merge
        else if (i + 1 < intervals.size() &&
                 canMerge(intervals[i], intervals[i+1], target_error,
                         expression_str, current_error_factor * 1.5)) {
            try {
                Interval merged_iv = createMergedPair(intervals[i], intervals[i+1], expression_str);

                if (i + 2 < intervals.size()) {
                    if (merged_iv.end < intervals[i + 2].start) {
                        merged_iv.end = intervals[i + 2].start;
                    }
                }

                merged_intervals.push_back(merged_iv);
                i += 2;
                merged_flag = true;
            } catch (const std::exception&) {
                merged_intervals.push_back(intervals[i]);
                i++;
            }
        } else {
            Interval current = intervals[i];
            if (i + 1 < intervals.size()) {
                if (current.end < intervals[i + 1].start) {
                    current.end = intervals[i + 1].start;
                }
            }
            merged_intervals.push_back(current);
            i++;
        }
    }

    // Fix gaps after merge
    for (size_t j = 0; j < merged_intervals.size() - 1; j++) {
        if (merged_intervals[j+1].start > merged_intervals[j].end + config.continuity_threshold) {
            merged_intervals[j].end = merged_intervals[j+1].start;
        }
    }

    if (!merged_intervals.empty()) {
        merged_intervals.front().start = domain_start;
        merged_intervals.back().end = domain_end;
    }

    intervals.swap(merged_intervals);
}

// interval_merge.hpp

inline std::vector<Interval> mergeIntervalsAdaptive(
    const std::vector<Interval>& intervals,
    const std::string& expression_str,
    const FittingParametersConfig& config,
    double spike_factor) {
    
    if (intervals.size() <= 1) return intervals;
    
    std::vector<Interval> result;
    Interval current = intervals[0];
    
    for (size_t i = 1; i < intervals.size(); ++i) {
        Interval candidate{current.start, intervals[i].end, 0.0, 0};
        candidate.hessian = (current.hessian + intervals[i].hessian) / 2.0;
        
        IntervalMetrics metrics = calculateMetrics(candidate, expression_str, 100);
        
        // Primary constraint: merged interval must meet error threshold
        bool mae_ok = metrics.avg_abs_error <= config.acceptable_error;
        bool spike_ok = metrics.max_abs_error <= config.acceptable_error * spike_factor;
        
        if (mae_ok && spike_ok) {
            current = candidate;
        } else {
            result.push_back(current);
            current = intervals[i];
        }
    }
    
    result.push_back(current);
    return result;
}

// ============================================================================
// Utilities
// ============================================================================

inline void printDistribution(const std::vector<Interval>& intervals, int precision) {
    std::map<std::string, size_t> dist_map;
    const double round_factor = std::pow(10.0, precision);

    for (const auto& iv : intervals) {
        const double raw_len = iv.end - iv.start;
        const double rounded_len = std::round(raw_len * round_factor) / round_factor;

        std::ostringstream oss;
        oss << std::scientific << std::setprecision(precision) << rounded_len;
        dist_map[oss.str()]++;
    }

    std::cout << "Distribution:\n";
    for (const auto& entry : dist_map) {
        std::cout << "  " << entry.first << ": " << entry.second << "\n";
    }
}

#endif // INTERVAL_MERGE_HPP
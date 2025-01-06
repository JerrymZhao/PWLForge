// interval_optimizer.hpp

#ifndef INTERVAL_OPTIMIZER_HPP
#define INTERVAL_OPTIMIZER_HPP

#include <vector>
#include <functional>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <mutex>
#include <future>
#include <unordered_map>
#include "exprtk.hpp"

// Define the Interval structure
struct Interval {
    double start;
    double end;
    double hessian;
    size_t level; // Represents the level of the interval

    Interval() : start(0.0), end(0.0), hessian(0.0), level(0) {}

    Interval(double s, double e) : start(s), end(e), hessian(0.0), level(0) {}

    // Constructor
    Interval(double s, double e, double h, size_t l)
        : start(s), end(e), hessian(h), level(l) {}
};

inline double computeFunctionValue(const std::string& expression_str, double x) {
    // Define the symbol table
    exprtk::symbol_table<double> symbol_table;
    double var_x = x;
    symbol_table.add_variable("x", var_x);
    symbol_table.add_constant("pi", 3.14159265358979323846);

    // Define the lambda function for the expression
    exprtk::expression<double> expression;
    expression.register_symbol_table(symbol_table);

    // Parse the expression
    exprtk::parser<double> parser;
    if (!parser.compile(expression_str, expression)) {
        std::cerr << "Error parsing the expression: " << expression_str << std::endl;
        for (std::size_t i = 0; i < parser.error_count(); ++i) {
            exprtk::parser_error::type error = parser.get_error(i);
            std::cerr << "Error: " << std::string(error.diagnostic) << std::endl;
        }
        return 0.0;
    }

    // Evaluate the expression
    double funtion_value = expression.value();

    return funtion_value;
}

// Calculate the Hessian (second derivative)
inline double computeHessian(const std::function<double(double)>& f, double x, double h = 1e-5) {
    return (f(x + h) - 2 * f(x) + f(x - h)) / (h * h);
}

// Generate initial intervals and calculate the Hessian
inline std::vector<Interval> generateInitialIntervals(double start, 
                                                    double end, 
                                                    size_t num_points, 
                                                    double initial_unit_length, 
                                                    const std::string& expression_str) {
    std::cout << "Starting interval generation with " << num_points << " points...\n";
    std::vector<Interval> intervals;

    double step = (end - start) / static_cast<double>(num_points - 1);
    double eps = step * 1e-6;  // Relative epsilon based on step size

    std::mutex interval_mutex;
    std::vector<std::future<void>> futures;

    size_t total_points = num_points + 1;
    intervals.reserve(total_points);

    const size_t batch_size = 100; // Process in batches
    // std::cout << "Processing in batches of " << batch_size << std::endl;
    for (size_t batch_start = 0; batch_start < num_points; batch_start += batch_size) {
        size_t batch_end = std::min(batch_start + batch_size, num_points);
        // std::cout << "Processing batch " << batch_start/batch_size + 1 
                //   << "/" << (num_points + batch_size - 1)/batch_size << "\n";
        futures.clear();

        for (size_t i = batch_start; i < batch_end; ++i) {
            double current_start = (i == 0) ? start : start + (i * step) - eps;
            double current_end = (i == total_points - 1) ? end : 
                               std::min(start + ((i + 1) * step) + eps, end);

            futures.emplace_back(std::async(std::launch::async, 
                [&interval_mutex, &intervals, &expression_str]
                (double s, double e) {
                    double mid = (s + e) / 2.0;
                    double hessian = computeHessian(
                        [&](double val) { 
                            return computeFunctionValue(expression_str, val); 
                        }, mid);
                    
                    std::lock_guard<std::mutex> lock(interval_mutex);
                    intervals.push_back(Interval{s, e, hessian, 0});
                }, current_start, current_end));
        }

        // Wait for batch completion with timeout
        for (auto& future : futures) {
            auto status = future.wait_for(std::chrono::seconds(5));
            if (status != std::future_status::ready) {
                std::cerr << "Warning: Task timeout detected\n";
            }
        }
    }

    // Ensure final interval reaches the end
    if (!intervals.empty() && intervals.back().end < end) {
        double last_start = intervals.back().end - eps;
        double hessian = computeHessian(
            [&](double val) { 
                return computeFunctionValue(expression_str, val); 
            }, (last_start + end) / 2.0);
        intervals.push_back(Interval{last_start, end, hessian, 0});
    }

    // Sort intervals by start point
    std::sort(intervals.begin(), intervals.end(), 
        [](const Interval& a, const Interval& b) {
            return a.start < b.start;
        });

    return intervals;
}

// Calcluate the normalized function difference
inline double computeNormalizedFunctionDifference(const std::string& expression_str, double start, double end) {
    double f_start = computeFunctionValue(expression_str, start);
    double f_end = computeFunctionValue(expression_str, end);
    double max_value = std::max(std::abs(f_start), std::abs(f_end));
    if (max_value == 0) {
        return 0.0;
    }
    double normalized_diff = std::abs(f_start - f_end) / max_value;
    return normalized_diff;
}

inline bool shouldSplit(const Interval& interval, double epsilon, 
                       const std::string& expression_str) {
    // Sample points
    double start = interval.start;
    double mid = (interval.start + interval.end) / 2.0;
    double end = interval.end;
    double length = end - start;
    
    // Function values
    double f_start = computeFunctionValue(expression_str, start);
    double f_mid = computeFunctionValue(expression_str, mid);
    double f_end = computeFunctionValue(expression_str, end);

    // Adaptive thresholds based on interval length
    double length_factor = std::min(1.0 + length, 2.0);
    double adaptive_epsilon = epsilon * length_factor;

    // Enhanced linearity check
    double linear_interp = (f_start + f_end) / 2.0;
    double linearity_error = std::abs(f_mid - linear_interp);
    double value_range = std::max(std::abs(f_end - f_start), 1e-6);
    double normalized_error = linearity_error / value_range;

    // Relaxed slope check
    double left_slope = (f_mid - f_start) / (mid - start);
    double right_slope = (f_end - f_mid) / (end - mid);
    double slope_diff = std::abs(right_slope - left_slope);
    double slope_threshold = adaptive_epsilon * (1.0 + std::abs(interval.hessian));

    // Combined criteria with relaxed thresholds
    bool should_split = 
        (normalized_error > adaptive_epsilon * 1.5) ||  // Relaxed linearity check
        (slope_diff > slope_threshold) ||              // Adaptive slope check
        (std::abs(interval.hessian) > adaptive_epsilon * 10.0);  // Hessian check

    return should_split;
}

// Split the interval function
inline void splitInterval(const Interval& interval, double epsilon, double min_unit_length, const std::string& expression_str, std::vector<Interval>& result) {
    double length = interval.end - interval.start;

    // Compute the normalized difference of the function values of the interval
    double normalized_diff = computeNormalizedFunctionDifference(expression_str, interval.start, interval.end);

    // if the interval length is less than the minimum unit length or the normalized difference is less than epsilon, add the interval to the result
    if (length <= min_unit_length || normalized_diff < epsilon) {
        result.push_back(interval);
        return;
    }

   if (!shouldSplit(interval, epsilon, expression_str)) {
        result.push_back(interval);
        return;
    }

    double mid = (interval.start + interval.end) / 2.0;
    double hessian = computeHessian(
        [&](double x) { return computeFunctionValue(expression_str, x); }, 
        mid);

    Interval left = {interval.start, mid, hessian, interval.level + 1};
    Interval right = {mid, interval.end, hessian, interval.level + 1};

    // Recursively split the sub-intervals
    splitInterval(left, epsilon, min_unit_length, expression_str, result);
    splitInterval(right, epsilon, min_unit_length, expression_str, result);
}

inline bool canMerge(const Interval& a, const Interval& b, 
                    double epsilon, 
                    const std::string& expression_str) {
    // Length-based adaptive threshold
    double avg_length = (a.end - a.start + b.end - b.start) / 2.0;
    double length_factor = std::min(1.0 + avg_length, 2.0);
    double adaptive_epsilon = epsilon * length_factor;

    // Relaxed continuity check
    if (std::abs(a.end - b.start) > adaptive_epsilon * 1.5) return false;

    // Function values with error weighting
    double f_start = computeFunctionValue(expression_str, a.start);
    double f_mid = computeFunctionValue(expression_str, a.end);
    double f_end = computeFunctionValue(expression_str, b.end);

    // Enhanced slope check with adaptive threshold
    double slope1 = (f_mid - f_start) / (a.end - a.start);
    double slope2 = (f_end - f_mid) / (b.end - b.start);
    double avg_slope = std::abs((slope1 + slope2) / 2.0);
    double slope_threshold = adaptive_epsilon * (1.0 + avg_slope);
    
    // Relaxed curvature check
    double hessian_threshold = adaptive_epsilon * (1.0 + std::abs(a.hessian));
    bool similar_hessian = std::abs(a.hessian - b.hessian) < hessian_threshold;
    bool similar_slopes = std::abs(slope1 - slope2) < slope_threshold;

    return similar_hessian && similar_slopes;
}

// Merge the interval Funtion
inline void mergeIntervals(std::vector<Interval>& intervals, double epsilon, const std::string& expression_str) {
    if (intervals.empty()) return;

    // Updated merged intervals
    std::sort(intervals.begin(), intervals.end(), 
        [](const Interval& a, const Interval& b) -> bool {
            return a.start < b.start;
        });

    std::vector<Interval> merged_intervals;
    std::map<double, size_t> length_distribution;
    merged_intervals.reserve(intervals.size());

    // Cache for function values
    std::unordered_map<double, double> function_cache;
    auto cached_compute = [&](double x) {
        auto it = function_cache.find(x);
        if (it != function_cache.end()) return it->second;
        double val = computeFunctionValue(expression_str, x);
        function_cache[x] = val;
        return val;
    };

    size_t i = 0;
    while (i < intervals.size()) {
        Interval current = intervals[i];
        size_t j = i + 1;
        double max_error = 0.0;

        while (j < intervals.size()) {
            // Length-based adaptive threshold
            double avg_length = (current.end - current.start + 
                               intervals[j].end - intervals[j].start) / 2.0;
            double length_factor = std::min(1.0 + avg_length, 2.0);
            double adaptive_epsilon = epsilon * length_factor;

            // Basic continuity check
            if (std::abs(current.end - intervals[j].start) > adaptive_epsilon) {
                break;
            }

            // Level check and merge criteria
            if (current.level == intervals[j].level && 
                canMerge(current, intervals[j], adaptive_epsilon, expression_str)) {
                
                // Calculate merge error
                double mid_point = (current.start + intervals[j].end) / 2.0;
                double f_start = cached_compute(current.start);
                double f_mid = cached_compute(mid_point);
                double f_end = cached_compute(intervals[j].end);
                double error = std::abs(f_mid - (f_start + f_end) / 2.0);

                // Update current interval
                current.end = intervals[j].end;
                current.hessian = (current.hessian + intervals[j].hessian) / 2.0;
                max_error = std::max(max_error, error);
                j++;
            } else {
                break;
            }
        }

        // Length distribution
        double final_length = current.end - current.start;
        length_distribution[final_length]++;

        if (max_error > 0.0) {
            std::cout << "Merged interval [" << current.start << "," << current.end 
                      << "] max error: " << max_error << std::endl;
        }

        merged_intervals.push_back(current);
        i = j;
    }

    // Output length distribution statistics
    std::cout << "\nInterval Length Distribution after merging:\n";
    std::cout << "Length\t\tCount\n";
    std::cout << "------------------------\n";
    for (const auto& entry : length_distribution) {
        std::cout << entry.first << "\t\t" << entry.second << "\n";
    }
    std::cout << "------------------------\n";
    std::cout << "Total unique lengths: " << length_distribution.size() << "\n";

    intervals = merged_intervals;
}

// Save the intervals
inline void saveIntervalsToFile(const std::vector<Interval>& intervals, const std::string& filename) {
    std::ofstream file(filename);
    if (file.is_open()) {
        file << "Start,End,Level,Hessian\n";
        for (const auto& interval : intervals) {
            file << interval.start << "," << interval.end << "," << interval.level << "," << interval.hessian << "\n";
        }
        file.close();
        std::cout << "Interval Results saved to file: " << filename << std::endl;
    } else {
        std::cout << "Failed to open file!" << std::endl;
    }
}

#endif // INTERVAL_OPTIMIZER_HPP

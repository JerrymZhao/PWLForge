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
inline std::vector<Interval> generateInitialIntervals(double start, double end, size_t num_points, double initial_unit_length, const std::string& expression_str) {
    std::cout << "Starting interval generation with " << num_points << " points...\n";
    std::vector<Interval> intervals;
    double step = initial_unit_length;

    std::mutex interval_mutex;
    std::vector<std::future<void>> futures;

    size_t total_intervals = static_cast<size_t>((end - start) / step);
    intervals.reserve(total_intervals);

    const size_t batch_size = 100; // Process in batches
    // std::cout << "Processing in batches of " << batch_size << std::endl;
    for (size_t batch_start = 0; batch_start < num_points; batch_start += batch_size) {
        size_t batch_end = std::min(batch_start + batch_size, num_points);
        // std::cout << "Processing batch " << batch_start/batch_size + 1 
                //   << "/" << (num_points + batch_size - 1)/batch_size << "\n";

        futures.clear();
        for (size_t i = batch_start; i < batch_end; ++i) {
            double current_start = start + i * step;
            double current_end = std::min(current_start + step, end);

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
 
    // for (double x = start; x < end; x += step) {
    //     double mid = x + step / 2.0;
    //     // double hessian = 0.0;
    //     double hessian = computeHessian([&](double val) { return computeFunctionValue(expression_str, val); }, mid);

    //     intervals.push_back(Interval{x, x + step, hessian, 0}); // level 0 represents the initial intervals
    // }

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

    // Split the interval in half
    double mid = (interval.start + interval.end) / 2.0;

    // Sub-intervals at the next level
    Interval left = {interval.start, mid, 0.0, interval.level + 1};
    Interval right = {mid, interval.end, 0.0, interval.level + 1};

    // Recursively split the sub-intervals
    splitInterval(left, epsilon, min_unit_length, expression_str, result);
    splitInterval(right, epsilon, min_unit_length, expression_str, result);
}

// Merge the interval Funtion
inline void mergeIntervals(std::vector<Interval>& intervals, double epsilon, const std::string& expression_str) {
    if (intervals.empty()) return;

    std::vector<Interval> merged_intervals;
    size_t i = 0;

    while (i < intervals.size()) {
        Interval current = intervals[i];
        size_t j = i + 1;
        double max_error = 0.0;

        while (j < intervals.size()) {
            // Compute the normalized difference of the function values of the intervals
            double normalized_diff = computeNormalizedFunctionDifference(
                expression_str, current.end, intervals[j].end);

            // Add error bound check
            double mid_point = (current.start + intervals[j].end) / 2.0;
            double actual = computeFunctionValue(expression_str, mid_point);
            double interpolated = (computeFunctionValue(expression_str, current.start) + 
                                 computeFunctionValue(expression_str, intervals[j].end)) / 2.0;
            double error = std::abs(actual - interpolated);

            // If the levels are the same and the normalized difference is less than epsilon, merge the intervals
            if (current.level == intervals[j].level && normalized_diff < epsilon && error < epsilon) {
                current.end = intervals[j].end;
                max_error = std::max(max_error, error);
                j++;
            } else {
                break;
            }
        }

        if (max_error > 0.0) {
            std::cout << "Merged interval [" << current.start << "," << current.end 
                      << "] max error: " << max_error << std::endl;
        }

        merged_intervals.push_back(current);
        i = j;
    }

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

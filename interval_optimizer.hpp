// interval_optimizer.hpp

#ifndef INTERVAL_OPTIMIZER_HPP
#define INTERVAL_OPTIMIZER_HPP

#include <vector>
#include <functional>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <iostream>
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

// Calculate the Hessian (second derivative)
inline double computeHessian(const std::function<double(double)>& f, double x, double h = 1e-5) {
    return (f(x + h) - 2 * f(x) + f(x - h)) / (h * h);
}

// Generate initial intervals and calculate the Hessian
inline std::vector<Interval> generateInitialIntervals(double start, double end, size_t num_points, double initial_unit_length, const std::string& expression_str) {
    std::vector<Interval> intervals;
    double step = initial_unit_length;

    for (double x = start; x < end; x += step) {
        double mid = x + step / 2.0;
        double hessian = 0.0;

        intervals.push_back(Interval{x, x + step, hessian, 0}); // level 0 represents the initial intervals
    }

    return intervals;
}

inline double computeFunctionValue(const std::string& expression_str, double x) {
    // Define the symbol table
    exprtk::symbol_table<double> symbol_table;
    double var_x = x;
    symbol_table.add_variable("x", var_x);
    symbol_table.add_constants();

    // Define the lambda function for the expression
    exprtk::expression<double> expression;
    expression.register_symbol_table(symbol_table);

    // Parse the expression
    exprtk::parser<double> parser;
    if (!parser.compile(expression_str, expression)) {
        std::cerr << "Error parsing the expression: " << expression_str << std::endl;
        return 0.0;
    }

    // Evaluate the expression
    double funtion_value = expression.value();

    return funtion_value;
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

        while (j < intervals.size()) {
            // Compute the normalized difference of the function values of the intervals
            double normalized_diff = computeNormalizedFunctionDifference(expression_str, current.end, intervals[j].end);

            // if the levels are the same and the normalized difference is less than epsilon, merge the intervals
            if (current.level == intervals[j].level && normalized_diff < epsilon) {
                current.end = intervals[j].end;
                j++;
            } else {
                break;
            }
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

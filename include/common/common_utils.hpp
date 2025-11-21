#ifndef COMMON_UTILS_HPP
#define COMMON_UTILS_HPP

#include <string>
#include <cmath>
#include <vector>
#include <iostream>
#include <algorithm>
#include "exprtk.hpp"
#include "interval_types.hpp"

//================================================================================
// Custom activation functions
//================================================================================

namespace CustomFunctions {
    const double PI = 3.14159265358979323846;
    const double SQRT_2 = 1.41421356237309504880;

    inline double gelu(double x) {
        double x3 = x * x * x;
        return 0.5 * x * (1.0 + std::tanh(std::sqrt(2.0 / PI) * (x + 0.044715 * x3)));
    }

    inline double gelu_tanh(double x) {
        double x3 = x * x * x;
        return 0.5 * x * (1.0 + std::tanh(0.7978845608 * (x + 0.044715 * x3)));
    }

    inline double gelu_erf(double x) {
        return 0.5 * x * (1.0 + std::erf(x / SQRT_2));
    }

    inline double silu(double x) {
        return x / (1.0 + std::exp(-x));
    }

    inline double swish(double x) {
        return silu(x);
    }

    inline double hardswish(double x) {
        if (x <= -3.0) return 0.0;
        if (x >= 3.0) return x;
        return x * (x + 3.0) / 6.0;
    }

    inline double hardsigmoid(double x) {
        if (x <= -3.0) return 0.0;
        if (x >= 3.0) return 1.0;
        return (x + 3.0) / 6.0;
    }

    inline double mish(double x) {
        return x * std::tanh(std::log(1.0 + std::exp(x)));
    }

    inline double softplus(double x) {
        if (x > 20.0) return x;
        return std::log(1.0 + std::exp(x));
    }

    inline double elu(double x, double alpha = 1.0) {
        return (x > 0.0) ? x : alpha * (std::exp(x) - 1.0);
    }

    inline double selu(double x) {
        const double alpha = 1.6732632423543772848170429916717;
        const double scale = 1.0507009873554804934193349852946;
        return scale * ((x > 0.0) ? x : alpha * (std::exp(x) - 1.0));
    }

    inline double gelu_quick(double x) {
        return x / (1.0 + std::exp(-1.702 * x));
    }
}

// ============================================================================
// Function Evaluation using exprtk with custom functions
// ============================================================================

inline double computeFunctionValue(const std::string& expression_str, double x_val) {
    static thread_local exprtk::symbol_table<double> symbol_table;
    static thread_local exprtk::expression<double> expression;
    static thread_local exprtk::parser<double> parser;
    static thread_local std::string last_expr = "";
    static thread_local bool initialized = false;
    
    static thread_local double x = 0.0;
    
    if (!initialized) {
        // Add standard constants
        symbol_table.add_constants();
        symbol_table.add_constant("pi", CustomFunctions::PI);
        symbol_table.add_constant("e", M_E);
        
        // Add variable
        symbol_table.add_variable("x", x);
        
        // Register custom activation functions
        symbol_table.add_function("gelu", CustomFunctions::gelu);
        symbol_table.add_function("gelu_tanh", CustomFunctions::gelu_tanh);
        symbol_table.add_function("gelu_erf", CustomFunctions::gelu_erf);
        symbol_table.add_function("silu", CustomFunctions::silu);
        symbol_table.add_function("swish", CustomFunctions::swish);
        symbol_table.add_function("hardswish", CustomFunctions::hardswish);
        symbol_table.add_function("hswish", CustomFunctions::hardswish);
        symbol_table.add_function("hardsigmoid", CustomFunctions::hardsigmoid);
        symbol_table.add_function("hsigmoid", CustomFunctions::hardsigmoid);
        symbol_table.add_function("mish", CustomFunctions::mish);
        symbol_table.add_function("softplus", CustomFunctions::softplus);
        symbol_table.add_function("selu", CustomFunctions::selu);
        symbol_table.add_function("gelu_quick", CustomFunctions::gelu_quick);
        symbol_table.add_function("elu", [](double x) { return CustomFunctions::elu(x, 1.0); });
        
        expression.register_symbol_table(symbol_table);
        initialized = true;
    }
    
    if (expression_str != last_expr) {
        if (!parser.compile(expression_str, expression)) {
            std::cerr << "Error parsing expression: " << expression_str << std::endl;
            return 0.0;
        }
        last_expr = expression_str;
    }
    
    x = x_val;
    return expression.value();
}

// ============================================================================
// Normalized Function Difference (for splitting criteria)
// ============================================================================

inline double computeNormalizedFunctionDifference(const std::string& expression_str,
                                                  double start, 
                                                  double end) {
    if (std::abs(end - start) < 1e-12) {
        return 0.0;
    }
    
    double f_start = computeFunctionValue(expression_str, start);
    double f_end = computeFunctionValue(expression_str, end);
    
    // Avoid division by zero
    double denominator = std::max(std::abs(f_start), std::abs(f_end));
    if (denominator < 1e-12) {
        denominator = 1.0;
    }
    
    double diff = std::abs(f_end - f_start);
    return diff / denominator;
}

// ============================================================================
// Hessian Computation (Second Derivative)
// ============================================================================

inline double computeHessian(const std::string& expression_str, double x) {
    const double h = 1e-5;
    
    double f_xph = computeFunctionValue(expression_str, x + h);
    double f_x = computeFunctionValue(expression_str, x);
    double f_xmh = computeFunctionValue(expression_str, x - h);
    
    double hessian = (f_xph - 2.0 * f_x + f_xmh) / (h * h);
    
    return hessian;
}

// ============================================================================
// Coverage Verification
// ============================================================================

inline void checkCoverage(const std::vector<std::pair<double, double>>& ranges,
                         double domain_start,
                         double domain_end,
                         const std::string& stage_name) {
    
    std::vector<std::pair<double, double>> sorted_ranges = ranges;
    std::sort(sorted_ranges.begin(), sorted_ranges.end());
    
    const double tolerance = 1e-10;
    std::vector<std::pair<double, double>> gaps;
    
    if (sorted_ranges.empty() || sorted_ranges.front().first > domain_start + tolerance) {
        double gap_end = sorted_ranges.empty() ? domain_end : sorted_ranges.front().first;
        gaps.emplace_back(domain_start, gap_end);
    }
    
    for (size_t i = 0; i < sorted_ranges.size() - 1; ++i) {
        if (sorted_ranges[i].second < sorted_ranges[i + 1].first - tolerance) {
            gaps.emplace_back(sorted_ranges[i].second, sorted_ranges[i + 1].first);
        }
    }
    
    if (!sorted_ranges.empty() && sorted_ranges.back().second < domain_end - tolerance) {
        gaps.emplace_back(sorted_ranges.back().second, domain_end);
    }
    
    if (!gaps.empty()) {
        std::cout << "WARNING [" << stage_name << "]: " << gaps.size() << " coverage gaps detected\n";
        for (size_t i = 0; i < std::min(gaps.size(), size_t(3)); ++i) {
            std::cout << "  Gap " << i+1 << ": [" << gaps[i].first << ", " << gaps[i].second << "]\n";
        }
    }
}

// ============================================================================
// Gap Removal
// ============================================================================

inline void ensureNoGaps(std::vector<Interval>& intervals) {
    if (intervals.size() <= 1) return;
    
    std::sort(intervals.begin(), intervals.end(),
             [](const Interval& a, const Interval& b) { return a.start < b.start; });
    
    const double tolerance = 1e-10;
    
    for (size_t i = 0; i < intervals.size() - 1; ++i) {
        if (intervals[i].end < intervals[i + 1].start - tolerance) {
            intervals[i].end = intervals[i + 1].start;
        } else if (intervals[i].end > intervals[i + 1].start + tolerance) {
            double midpoint = (intervals[i].end + intervals[i + 1].start) / 2.0;
            intervals[i].end = midpoint;
            intervals[i + 1].start = midpoint;
        }
    }
}

// ============================================================================
// Interval Validation
// ============================================================================

inline bool validateIntervals(const std::vector<Interval>& intervals,
                              double domain_start,
                              double domain_end) {
    if (intervals.empty()) {
        std::cerr << "Error: No intervals to validate\n";
        return false;
    }
    
    for (size_t i = 0; i < intervals.size(); ++i) {
        if (intervals[i].start >= intervals[i].end) {
            std::cerr << "Error: Invalid interval at index " << i << "\n";
            return false;
        }
        
        if (i > 0 && intervals[i].start < intervals[i-1].end - 1e-10) {
            std::cerr << "Error: Overlapping intervals at index " << i << "\n";
            return false;
        }
    }
    
    const double tolerance = 1e-10;
    if (intervals.front().start > domain_start + tolerance) {
        std::cerr << "Error: Gap at domain start\n";
        return false;
    }
    
    if (intervals.back().end < domain_end - tolerance) {
        std::cerr << "Error: Gap at domain end\n";
        return false;
    }
    
    return true;
}

// ============================================================================
// Interval Metrics
// ============================================================================

inline IntervalMetrics calculateMetrics(const Interval& interval, 
                                       const std::string& expression_str, 
                                       int sample_points = 10) {
    IntervalMetrics metrics;
    double f_start = computeFunctionValue(expression_str, interval.start);
    double f_end = computeFunctionValue(expression_str, interval.end);

    metrics.entropy = std::abs(f_start - f_end) / (f_start + f_end + 1e-6);
    metrics.complexity = std::log(interval.end - interval.start + 1.0);
    metrics.error_sensitivity = std::abs(f_start - f_end) / std::max(std::abs(f_start), std::abs(f_end) + 1e-6);
    metrics.merge_score = metrics.entropy + metrics.complexity + metrics.error_sensitivity;

    if (sample_points < 2) sample_points = 2;
    const double step = (interval.end - interval.start) / (sample_points - 1);

    double sum_abs_error = 0.0;
    
    for (int i = 0; i < sample_points; ++i) {
        double x = interval.start + i * step;
        double f_val = computeFunctionValue(expression_str, x);
        double approx = f_start + (x - interval.start) * (f_end - f_start) / (interval.end - interval.start);
        double error = std::abs(f_val - approx);
        
        metrics.max_abs_error = std::max(metrics.max_abs_error, error);
        sum_abs_error += error;
    }
    
    metrics.avg_abs_error = sum_abs_error / sample_points;

    return metrics;
}

#endif // COMMON_UTILS_HPP
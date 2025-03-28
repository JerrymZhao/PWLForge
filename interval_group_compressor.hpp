// interval_group_compressor.hpp

#ifndef INTERVAL_GROUP_COMPRESSOR_HPP
#define INTERVAL_GROUP_COMPRESSOR_HPP

#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include "interval_optimizer.hpp"
#include "function_fitter.hpp"

struct DeltaEncoding {
    double delta_start;
    bool is_x_reflected;
    bool is_y_reflected;
    double delta_intercept;
    double delta_slope;
    size_t original_index;

    DeltaEncoding() : delta_start(0.0), is_x_reflected(false), is_y_reflected(false), 
                    delta_intercept(0.0), delta_slope(0.0), original_index(0) {}
    DeltaEncoding(double ds, bool xrefl = false, bool refl = false, 
                double di = 0.0, double dss = 0.0, size_t orig_idx = 0)
        : delta_start(ds), is_x_reflected(xrefl), is_y_reflected(refl), 
            delta_intercept(di), delta_slope(dss), original_index(orig_idx) {}
};

struct GroupStatistics {
    size_t total_intervals;
    size_t symmetry_pairs;
    size_t translation_pairs;
    size_t x_reflection_pairs;
    size_t gap_count;
    double max_gap_length;
    double avg_delta;
    double max_delta;
    int optimal_bitwidth{8};
    double compression_ratio{1.0};
    double actual_error{0.0};
};

enum GroupStorageType {
    NORMAL_GROUP,
    ORPHAN_GROUP
};

// Define the IntervalGroup structure
struct IntervalGroup {
    static size_t global_group_id;
    size_t id;                              // Group identifier
    double length;                          // All intervals in the group have the same length
    Interval base_interval;                 // Base interval
    FitParameters base_params;              // Fitting parameters of the base interval
    std::vector<DeltaEncoding> delta_encodings; // Delta encodings for member intervals
    // double delta_scale_factor;              // Scale factor for quantization
    double start_scale_factor;              // Scale factor for start position differences
    double intercept_scale_factor;          // Scale factor for intercept differences
    double slope_scale_factor;              // Scale factor for slope differences
    int bitwidth_start;                     // Bitwidth for start position differences
    int bitwidth_intercept;                 // Bitwidth for intercept differences
    int bitwidth_slope;                     // Bitwidth for slope differences
    GroupStatistics stats;                  // Group statistics
    bool has_prefix_gap{false};
    double gap_length{0.0};
    double base_length;
    uint8_t length_type; // 0: Normal, 1: Prefix, 2: Suffix
    GroupStorageType storage_type{NORMAL_GROUP};

    IntervalGroup() : id(global_group_id++), length(0.0), 
                    base_interval(Interval()), base_params(), 
                    start_scale_factor(1.0), intercept_scale_factor(1.0),
                    slope_scale_factor(1.0), bitwidth_start(8), 
                    bitwidth_intercept(8), bitwidth_slope(8), stats(),
                    base_length(0.0), length_type(0) {}
};
size_t IntervalGroup::global_group_id = 0;

struct SymmetryInfo {
    bool is_symmetric;
    bool is_translated;
    bool is_y_reflected;
    bool is_x_reflected;
    double y_offset;
    double x_reflection_point;
    double dynamic_tolerance;

    SymmetryInfo() : is_symmetric(false), is_translated(false), 
                    is_y_reflected(false), is_x_reflected(false), 
                    y_offset(0.0), x_reflection_point(0.0), dynamic_tolerance(1.0) {}
};

double calculateCurvature(const std::vector<double>& deltas) {
    if (deltas.size() < 3) return 0.0;
    
    double sum = 0.0;
    for (size_t i = 1; i < deltas.size() - 1; ++i) {
        const double diff2 = deltas[i+1] - 2*deltas[i] + deltas[i-1];
        sum += std::abs(diff2);
    }
    return sum / deltas.size();
}

inline int calculateOptimalBitwidth(const std::vector<double>& deltas, 
                                    double max_error,
                                    double &scale_factor) {
    if (deltas.empty()) {
        scale_factor = 1.0;
        return 8; // 默认位宽
    }

    const auto minmax = std::minmax_element(deltas.begin(), deltas.end());
    double delta_min = *minmax.first;
    double delta_max = *minmax.second;
    double dynamic_range = std::max(delta_max - delta_min, 1e-12);

    int low = 8, high = 16;
    int optimal = 16;
    while (low <= high) {
        int mid = (low + high) / 2;
        scale_factor = dynamic_range / ((1 << mid) - 1);
        double quant_error = scale_factor * 0.5;
        if (quant_error <= max_error) {
            optimal = mid;
            high = mid - 1;
        } else {
            low = mid + 1;
        }
    }

    scale_factor = dynamic_range / ((1 << optimal) - 1);
    return optimal;
}

inline int inferFitOrder(const FittingMethod& method) {
    switch (method) {
        case FittingMethod::Linear :
            return 0;
        case FittingMethod::Quadratic :
            return 1;
        default:
            return 0;
    }
}

inline SymmetryInfo checkIntervalEquivalence(const FitParameters& params1, 
                                           const FitParameters& params2,
                                           double error_threshold) {
    SymmetryInfo info;
    // constexpr double epsilon = 1e-12;
    // constexpr double MIN_QUADRATIC_COEFF = 1e-8;

    if (params1.method != params2.method || 
        !std::isfinite(params1.a) || !std::isfinite(params1.b) || !std::isfinite(params1.c) ||
        !std::isfinite(params2.a) || !std::isfinite(params2.b) || !std::isfinite(params2.c)) {
        return info;
    }

    // Dynamic threshold calculation
    auto calc_threshold = [error_threshold](double coeff){
        return std::max(error_threshold * 0.5, 
            std::min(0.01*std::abs(coeff), error_threshold*2.0));
    };

    if (params1.method == FittingMethod::Linear) {
        const double b_thresh = calc_threshold(params1.b);
        if (std::abs(params1.b + params2.b) < b_thresh) {
            const double delta_c = params2.c - params1.c;
            if (std::abs(delta_c) < calc_threshold(params1.c)) {
                info.is_symmetric = true;
                info.is_y_reflected = true;
                info.y_offset = delta_c;
            }
        }
        else if (std::abs(params1.b - params2.b) < b_thresh) {
            const double delta_c = params2.c - params1.c;
            if (std::abs(delta_c) < calc_threshold(params1.c)) {
                info.is_symmetric = true;
                info.is_translated = true;
                info.y_offset = delta_c;
            }
        }
    } 
    else if (params1.method == FittingMethod::Quadratic) {
        const double a_thresh = calc_threshold(params1.a);
        const double b_thresh = calc_threshold(params1.b);

        if (std::abs(params1.a - params2.a) < a_thresh &&
            std::abs(params1.b + params2.b) < b_thresh) {
            info.is_symmetric = true;
            info.is_y_reflected = true;
            info.y_offset = params2.c - params1.c;
        }
        else if (std::abs(params1.a + params2.a) < a_thresh) {
            const double h = (params1.b + params2.b) / (-4 * params1.a); // 修正顶点计算
            const double k1 = params1.c - params1.b*params1.b/(4*params1.a);
            const double k2 = params2.c - params2.b*params2.b/(4*params2.a);
            if (std::abs(k1 - k2) < calc_threshold(k1)) {
                info.is_symmetric = true;
                info.is_x_reflected = true;
                info.x_reflection_point = h;
            }
        }
        else if (std::abs(params1.a - params2.a) < a_thresh &&
                 std::abs(params1.b - params2.b) < b_thresh) {
            info.is_symmetric = true;
            info.is_translated = true;
            info.y_offset = params2.c - params1.c;
        }
    }
    
    return info;
}

inline std::string serializeDeltas(const std::vector<DeltaEncoding>& deltas) {
    std::string serialized = "\"";
    for (size_t i = 0; i < deltas.size(); ++i) {
        const auto& delta = deltas[i];
        serialized += std::to_string(delta.delta_start) + ":" +
                      (delta.is_y_reflected ? "1" : "0") + ":" +
                      std::to_string(delta.delta_intercept);
        if (i != deltas.size() - 1) {
            serialized += ";";
        }
    }
    serialized += "\"";
    return serialized;
}

inline double quantizeLength(double length) {
    return std::round(length * 1e9) / 1e9;
}

// Generate sampling points for interval optimization
std::vector<double> generateSamplingPoints(const Interval& iv, 
                                        const std::string& expression_str,
                                        size_t base_samples) {
    std::set<double> unique_points;
    const double step = (iv.end - iv.start) / (base_samples - 1);
    
    // Basic sampling points
    for (size_t i=0; i<base_samples; ++i) {
        const double x = iv.start + i*step + (rand()%100)*1e-12;
        unique_points.insert(x);
    }
    // 数值微分计算（中心差分法）
    auto computeDerivative = [&](double x) -> double {
        const double h = 1e-6;
        try {
            const double f_plus = computeFunctionValue(expression_str, x + h);
            const double f_minus = computeFunctionValue(expression_str, x - h);
            return (f_plus - f_minus) / (2 * h);
        } catch (...) {
            return std::numeric_limits<double>::quiet_NaN();
        }
    };
    
    // 极值点检测
    const size_t scan_steps = 50;
    const double scan_step = (iv.end - iv.start)/scan_steps;
    double prev_deriv = computeDerivative(iv.start);
    for(size_t i=1; i<scan_steps; ++i){
        double x = iv.start + i*scan_step;
        double deriv = computeDerivative(x);
        
        // 检测导数符号变化
        if(deriv * prev_deriv < 0){
            // 简单线性插值定位极值
            double t = prev_deriv/(prev_deriv - deriv);
            double ext_x = x - scan_step*(1-t);
            unique_points.insert(ext_x);
        }
        prev_deriv = deriv;
    }

    // 改进高曲率检测阈值（动态调整）
    const double length_factor = 1.0/(iv.end - iv.start);
    // 函数二阶导检测高曲率区域
    auto second_deriv = [&](double x) {
        const double h = 1e-6;
        try {
            double a = computeFunctionValue(expression_str, x+h);
            double b = computeFunctionValue(expression_str, x);
            double c = computeFunctionValue(expression_str, x-h);
            return (a - 2*b + c) / (h*h) * length_factor;
        } catch (...) {
            return 0.0;
        }
    };

    // 高曲率区域增加采样
    for (size_t i=0; i<base_samples-1; ++i) {
        const double x = iv.start + i*step;
        if (std::abs(second_deriv(x)) > 1e3) {
            for (int j=1; j<=3; ++j) { // 添加额外采样点
                unique_points.insert(x + j*(step/4));
            }
        }
    }

    // 强制包含端点
    unique_points.insert(iv.start);
    unique_points.insert(iv.end - 1e-12);

    return std::vector<double>(unique_points.begin(), unique_points.end());
}

// Error metric types
enum class ErrorMetric {
    MaxAbsolute,
    MeanAbsolute,
    RootMeanSquared
};

struct ErrorEvaluationResult {
    double max_error;
    double mean_error;
    double rms_error;
    bool is_valid;
    std::map<std::string, int> error_buckets;

    void print() const {
        using namespace std;
        cout << "Error Evaluation Results:\n"
             << "  Max Error:    " << scientific << max_error << '\n'
             << "  Mean Error:   " << mean_error << '\n'
             << "  RMS Error:    " << rms_error << '\n'
             << "  Validity:     " << (is_valid ? "PASS" : "FAIL") << "\n\n"
             << "Error Distribution:\n";
        
        for (const auto& entry : error_buckets) {
            const std::string& bucket = entry.first;
            int count = entry.second;
            cout << "  " << setw(12) << left << bucket 
                 << ": " << count << " points\n";
        }
    }
};

inline double RecoveredFunctionValue(const IntervalGroup& group, 
                                    const DeltaEncoding& delta, 
                                    double x, 
                                    double start_scale_factor,
                                    double intercept_scale_factor,
                                    const std::vector<FitParameters>& fit_params) {
    // Special case for orphan groups - use original parameters directly
    if (group.storage_type == ORPHAN_GROUP) {
        if (delta.original_index >= fit_params.size()) {
            throw std::out_of_range("Invalid original index in orphan group: " + 
                                    std::to_string(delta.original_index));
        }
        // For orphans, the original_index field points directly to the interval
        const FitParameters& p = fit_params[delta.original_index];
        
        // Return direct evaluation without transformations
        switch (p.method) {
            case FittingMethod::Linear:
                return p.b * x + p.c;
            case FittingMethod::Quadratic:
                return p.a * x * x + p.b * x + p.c;
            default:
                throw std::runtime_error("Unsupported fitting method");
        }
    }
 
    if (start_scale_factor <= 1e-12) {
        throw std::invalid_argument("scale_factor must be positive");
    }

    FitParameters p = group.base_params;    // Copy the base parameters
    Interval base_iv = group.base_interval; // Copy the base interval
    
    // Apply the delta values to the base parameters after quantization
    // SIMULATION of the quantization that happens during saving
    int quantized_delta_start = static_cast<int>(std::round(delta.delta_start / start_scale_factor));
    int quantized_delta_slope = static_cast<int>(std::round(delta.delta_slope / group.slope_scale_factor));
    int quantized_delta_intercept = static_cast<int>(std::round(delta.delta_intercept / intercept_scale_factor));

    // Now dequantize as you would when loading
    double quant_delta_start = static_cast<double>(quantized_delta_start) * start_scale_factor;
    double quant_delta_slope = static_cast<double>(quantized_delta_slope) * group.slope_scale_factor;
    double quant_delta_intercept = static_cast<double>(quantized_delta_intercept) * intercept_scale_factor;

    base_iv.start += quant_delta_start;
    p.b += quant_delta_slope;
    p.c += quant_delta_intercept;

    double interval_length = base_iv.end - base_iv.start;
    double mid_point = base_iv.start + interval_length / 2.0;

    // X_reflection point
    if (delta.is_x_reflected) {
        double h = mid_point;
        x = 2 * h - x;
        if (p.method == FittingMethod::Quadratic) {
            p.b = -2 * p.a * h; 
        } /*else if (p.method == FittingMethod::Linear) {
            const double original_b = p.b;
            p.b = -original_b;
            p.c += 2 * original_b * h;
        }*/
    }

    if (delta.is_y_reflected) {
        if (p.method == FittingMethod::Quadratic) {
            p.a = -p.a;
            p.b = -p.b;
            p.c = -p.c;
        } else if (p.method == FittingMethod::Linear) {
            p.b = -p.b;
            p.c = -p.c;
        }
    }

    // std::cout << "RecoveredFunctionValue (Normal): x=" << x 
    //           << ", Interval index=" << delta.original_index 
    //           << ", b=" << p.b << ", c=" << p.c << "\n";

    // Calculate the predicted value using the adjusted parameters.
    switch (p.method) {
        case FittingMethod::Linear:
            return p.b * x + p.c;
        case FittingMethod::Quadratic: {
            return p.a * x * x + p.b * x + p.c;
        }
        default:
            throw std::runtime_error("Unsupported fitting method");
    }
}

inline double evaluateCompressedErrorWithQuantization(
    const std::string& expression_str,
    const std::vector<Interval>& intervals,
    const std::vector<FitParameters>& fit_params,
    const std::vector<IntervalGroup>& groups) {
    if (groups.empty() || intervals.empty()) {
        std::cerr << "Error: Empty input data\n";
        return NAN;
    }

    // Initialize error metrics
    std::vector<double> absolute_errors;
    double max_error = 0.0;
    size_t valid_samples = 0;

    // Precaution: parse the expression string
    // auto func = FunctionParser::Parse(expression_str);

    // Iteration on all groups
    for (const auto& group : groups) {
        if (group.storage_type == ORPHAN_GROUP) {
            // ORPHAN_GROUP: Direct evaluation using original parameters
            std::cout << "Processing orphan group with " << group.delta_encodings.size() << " intervals\n";
            for (const auto& delta : group.delta_encodings) {
                if (delta.original_index >= intervals.size()) {
                    std::cerr << "Invalid original_index: " << delta.original_index 
                              << "/" << intervals.size() << "\n";
                    continue;
                }
                const Interval& orig_iv = intervals[delta.original_index];
                const FitParameters& orig_params = fit_params[delta.original_index];

                std::cout << "Orignal params for orphan group interval [" << orig_iv.start << ", " << orig_iv.end 
                        << "], b=" << orig_params.b << ", c=" << orig_params.c << " (Type: Orphan)\n";

                if (orig_iv.end <= orig_iv.start) {
                    std::cerr << "Invalid interval [" << orig_iv.start 
                              << "," << orig_iv.end << "]\n";
                    continue;
                }

                auto samples = generateSamplingPoints(orig_iv, expression_str, 20);
                for (double x : samples) {
                    if (x < orig_iv.start || x >= orig_iv.end) continue;
                    double y_true = computeFunctionValue(expression_str, x);
                    double y_pred = (orig_params.method == FittingMethod::Linear) ?
                                    orig_params.b * x + orig_params.c :
                                    orig_params.a * x * x + orig_params.b * x + orig_params.c;

                    if (std::isnan(y_pred) || std::isnan(y_true)) continue;

                    const double abs_err = std::abs(y_true - y_pred);
                    absolute_errors.push_back(abs_err);
                    max_error = std::max(max_error, abs_err);
                    valid_samples++;
                }
            }
            continue;
        }

        // Check for invalid scale factor
        if (group.start_scale_factor < 1e-12) {
            std::cerr << "Invalid scale factor in group\n";
            continue;
        }


        // Process all delta encodings in normal groups
        for (const auto& delta : group.delta_encodings) {
            if (delta.original_index >= intervals.size()) {
                std::cerr << "Invalid original_index: " << delta.original_index 
                        << "/" << intervals.size() << "\n";
                continue;
            }
            const Interval& orig_iv = intervals[delta.original_index];
            if (orig_iv.end <= orig_iv.start) {
                std::cerr << "Invalid interval [" << orig_iv.start 
                        << "," << orig_iv.end << "]\n";
                continue;
            }
            const FitParameters& orig_params = fit_params[delta.original_index];

            // Output original parameters once per interval
            std::cout << "Original params for interval [" << orig_iv.start << ", " << orig_iv.end 
                    << "], b=" << orig_params.b << ", c=" << orig_params.c 
                    << " (Type: " << (group.storage_type == ORPHAN_GROUP ? "Orphan" : "Normal") << ")\n";

            // Compute recovered parameters
            FitParameters recovered_params = group.base_params;
            int q_start = static_cast<int>(std::round(delta.delta_start / group.start_scale_factor));
            int q_slope = static_cast<int>(std::round(delta.delta_slope / group.slope_scale_factor));
            int q_intercept = static_cast<int>(std::round(delta.delta_intercept / group.intercept_scale_factor));
            double quant_delta_start = static_cast<double>(q_start) * group.start_scale_factor;
            double quant_delta_slope = static_cast<double>(q_slope) * group.slope_scale_factor;
            double quant_delta_intercept = static_cast<double>(q_intercept) * group.intercept_scale_factor;
            recovered_params.b += quant_delta_slope;  // Apply delta_slope to recover b
            recovered_params.c += quant_delta_intercept;

            // Handle symmetry adjustments
            Interval base_iv = group.base_interval;
            base_iv.start += quant_delta_start;
            double mid_point = base_iv.start + (base_iv.end - base_iv.start) / 2.0;
            if (delta.is_x_reflected && recovered_params.method == FittingMethod::Quadratic) {
                double h = mid_point;
                recovered_params.b = -2 * recovered_params.a * h;
            }
            if (delta.is_y_reflected) {
                if (recovered_params.method == FittingMethod::Quadratic) {
                    recovered_params.a = -recovered_params.a;
                    recovered_params.b = -recovered_params.b;
                    recovered_params.c = -recovered_params.c;
                } else if (recovered_params.method == FittingMethod::Linear) {
                    recovered_params.b = -recovered_params.b;
                    recovered_params.c = -recovered_params.c;
                }
            }

            // Output recovered parameters once per interval
            std::cout << "Recovered params for interval [" << orig_iv.start << ", " << orig_iv.end 
                    << "], b=" << recovered_params.b << ", c=" << recovered_params.c << "\n";

            // Generate sampling points and evaluate
            auto samples = generateSamplingPoints(orig_iv, expression_str, 20);
            for (double x : samples) {
                if (x < orig_iv.start || x >= orig_iv.end) continue;

                double y_true = computeFunctionValue(expression_str, x);
                double x_adjusted = x;
                if (delta.is_x_reflected) {
                    x_adjusted = 2 * mid_point - x;
                }
                double y_pred = recovered_params.method == FittingMethod::Linear ?
                                recovered_params.b * x_adjusted + recovered_params.c :
                                recovered_params.a * x_adjusted * x_adjusted + recovered_params.b * x_adjusted + recovered_params.c;

                if (std::isnan(y_pred) || std::isnan(y_true)) continue;

                const double abs_err = std::abs(y_true - y_pred);
                absolute_errors.push_back(abs_err);
                max_error = std::max(max_error, abs_err);
                valid_samples++;
            }
        }
    }

    if (valid_samples < 1) {
        std::cerr << "No valid samples collected\n";
        return NAN;
    }

    // Final error report
    const double avg_error = std::accumulate(
        absolute_errors.begin(), absolute_errors.end(), 0.0
    ) / absolute_errors.size();
    const double rms = std::sqrt(std::inner_product(
        absolute_errors.begin(), absolute_errors.end(),
        absolute_errors.begin(), 0.0
    ) / absolute_errors.size());

    std::cout << "Error Statistics (samples=" << valid_samples << "):\n"
              << "  Max Absolute: " << max_error << '\n'
              << "  Average Absolute: " << avg_error << '\n'
              << "  RMS: " << rms << '\n';

    return avg_error;
}

inline void analyzeSymmetryAndEncoding(std::vector<IntervalGroup>& groups,
                                const std::vector<Interval>& intervals,
                                const std::vector<FitParameters>& fit_params,
                                double target_error) {
    for (auto& group : groups) {
        if (group.storage_type == ORPHAN_GROUP) {
            continue;
        }

        for (size_t i = 1; i < group.delta_encodings.size(); ++i) {
            auto& delta = group.delta_encodings[i];
            size_t orig_idx = delta.original_index;

            // Symmetry check
            SymmetryInfo sym = checkIntervalEquivalence(group.base_params, fit_params[orig_idx], target_error * 0.5);
            delta.is_y_reflected = sym.is_y_reflected;
            delta.is_x_reflected = sym.is_x_reflected;
            group.stats.symmetry_pairs += sym.is_symmetric ? 1 : 0;
            group.stats.translation_pairs += sym.is_translated ? 1 : 0;
            group.stats.x_reflection_pairs += sym.is_x_reflected ? 1 : 0;
        }
    }
}

/*
inline void groupAndCompressIntervals(const std::string& expression_str,
                                    const std::vector<Interval>& intervals,
                                    const std::vector<FitParameters>& fit_params,
                                    std::vector<IntervalGroup>& groups,
                                    double target_error) {
    
    std::cout << "Entering groupAndCompressIntervals:\n";
    for (size_t i = 0; i < intervals.size(); ++i) {
        std::cout << "Interval [" << intervals[i].start << ", " << intervals[i].end
                << "], b=" << fit_params[i].b << ", c=" << fit_params[i].c << "\n";
    }
    
    // Preprocess intervals, group by length
    struct IntervalWithIndex {
        Interval interval;
        size_t index; // Store the original index of the interval
        bool operator<(const IntervalWithIndex& other) const {
            return interval.start < other.interval.start;
        }
    };

    // Preprocess intervals
    std::vector<double> lengths;
    lengths.reserve(intervals.size());
    double full_start = INFINITY, full_end = -INFINITY;

    // std::vector<IntervalWithIndex> sorted_intervals;
    // sorted_intervals.reserve(intervals.size());

    for (size_t i = 0; i < intervals.size(); ++i) {
        const auto& iv = intervals[i];
        // sorted_intervals.push_back({iv, i});
        lengths.push_back(iv.end - iv.start);
        full_start = std::min(full_start, iv.start);
        full_end = std::max(full_end, iv.end);
        // lengths.push_back(iv.end - iv.start);
    }
    std::sort(lengths.begin(), lengths.end());
    size_t percentile_idx = lengths.size() / 10;
    double base_length = lengths[percentile_idx];

    // std::sort(sorted_intervals.begin(), sorted_intervals.end());
    std::map<int, std::vector<IntervalWithIndex>> length_groups;
    for (size_t i = 0; i < intervals.size(); ++i) {
        const auto& iv = intervals[i];
        double len = iv.end - iv.start;
        int k = static_cast<int>(std::round(len / base_length));
        length_groups[k].push_back({iv, i});
    }

    // Initialize group map
    std::vector<IntervalGroup> temp_groups;
    // double prev_end = sorted_intervals.empty() ? 0.0 : sorted_intervals[0].interval.start - 1e-10;
    double prev_end = full_start -  1e-10;
    size_t global_gap_count = 0;
    // int current_k = -1;

    for (const auto& pair : length_groups) {
        int k = pair.first;
        const auto& group_intervals = pair.second;
        if (group_intervals.empty()) continue;

        IntervalGroup group;
        group.length = k * base_length;
        group.base_interval = group_intervals[0].interval;
        group.base_params = fit_params[group_intervals[0].index];
        group.stats.total_intervals = group_intervals.size();
        group.delta_encodings.emplace_back(0.0, false, 0.0, false, group_intervals[0].index);

        // Delta encoding for all intervals in the group
        for (size_t i = 1; i < group_intervals.size(); ++i) {
            const auto& iv = group_intervals[i].interval;
            size_t original_idx = group_intervals[i].index;
            double delta_start = iv.start - group.base_interval.start;
            double delta_intercept = fit_params[original_idx].c - group.base_params.c;
            SymmetryInfo sym = checkIntervalEquivalence(group.base_params, fit_params[original_idx], target_error);
            group.delta_encodings.emplace_back(delta_start, sym.is_y_reflected, delta_intercept, sym.is_x_reflected, original_idx);
        }

        // Gap detection
        if (group.base_interval.start > prev_end + 5 * target_error) {
            global_gap_count++;
            std::cout << "Gap detected: " << (group.base_interval.start - prev_end) << "\n";
        }
        prev_end = group.base_interval.end;
        temp_groups.push_back(group);
    }

    // Process the ORPHAN intervals
    constexpr size_t MIN_GROUP_MEMBERS = 3; // Minimum members for a valid group
    std::vector<IntervalGroup> valid_groups;
    std::vector<size_t> orphan_indices;

    // Filter out groups with insufficient members
    for (auto& group : temp_groups) {
        if (group.delta_encodings.size() >= MIN_GROUP_MEMBERS) {
            valid_groups.push_back(std::move(group));
        } else {
            // Collect orphan indices
            for (const auto& delta : group.delta_encodings) {
                if (delta.original_index < intervals.size()) {
                    orphan_indices.push_back(delta.original_index);
                }
            }
        }
    }

    // Create orphan groups
    if (!orphan_indices.empty()) {
        IntervalGroup orphan_group;
        orphan_group.storage_type = ORPHAN_GROUP;
        orphan_group.length = 0.0; // Special length tag for orphan group
        
        // Filter out duplicate indices
        std::sort(orphan_indices.begin(), orphan_indices.end());
        auto last = std::unique(orphan_indices.begin(), orphan_indices.end());
        orphan_indices.erase(last, orphan_indices.end());
        for (auto idx : orphan_indices) {
            if (idx < intervals.size() && idx < fit_params.size()) {
                orphan_group.delta_encodings.emplace_back(
                    0.0, 
                    false, 
                    0.0,
                    false,
                    idx
                );
            }
        }

        // if (!orphan_group.delta_encodings.empty()) {
        //     size_t first_idx = orphan_group.delta_encodings[0].original_index;
        //     if (first_idx < fit_params.size()) {
        //         orphan_group.base_params = fit_params[first_idx];
        //     } else {
        //         throw std::out_of_range("Orphan group invalid first index");
        //     }
        // }
        orphan_group.base_params = FitParameters();
        orphan_group.storage_type = ORPHAN_GROUP;

        orphan_group.stats.total_intervals = orphan_indices.size();
        valid_groups.push_back(orphan_group);
    }

    groups.swap(valid_groups); // Replace the original group list

    // Symmetry check and encoding analysis
    analyzeSymmetryAndEncoding(groups, intervals, fit_params, target_error);

    // Post processing: calculate optimal bitwidths
    auto max_abs = [](const std::vector<double>& vec) {
        return vec.empty() ? 0.0 : 
            std::abs(*std::max_element(vec.begin(), vec.end(),
                [](double a, double b) { return std::abs(a) < std::abs(b); }));
    };
    for (auto& group : groups) {
        if (group.storage_type == ORPHAN_GROUP) continue;

        std::vector<double> start_deltas, intercept_deltas;
        for (size_t i = 1; i < group.delta_encodings.size(); ++i) {
            start_deltas.push_back(group.delta_encodings[i].delta_start);
            intercept_deltas.push_back(group.delta_encodings[i].delta_intercept);
        }
        // Calculate quantization bitwidth
        group.bitwidth_start = start_deltas.empty() ? 8 : 
            calculateOptimalBitwidth(start_deltas, target_error, group.start_scale_factor);
        group.bitwidth_intercept = intercept_deltas.empty() ? 8 : 
            calculateOptimalBitwidth(intercept_deltas, target_error, group.intercept_scale_factor);
        // Calculate the maximum delta value
        group.stats.max_delta = std::max(max_abs(start_deltas), max_abs(intercept_deltas));

        group.stats.actual_error = evaluateCompressedErrorWithQuantization(expression_str, intervals, fit_params, {group});
        while (group.stats.actual_error > target_error && 
               (group.bitwidth_start < 16 || group.bitwidth_intercept < 16)) {
            group.bitwidth_start = std::min(group.bitwidth_start + 1, 16);
            group.bitwidth_intercept = std::min(group.bitwidth_intercept + 1, 16);
            calculateOptimalBitwidth(start_deltas, target_error, group.start_scale_factor);
            calculateOptimalBitwidth(intercept_deltas, target_error, group.intercept_scale_factor);
            group.stats.actual_error = evaluateCompressedErrorWithQuantization(expression_str, intervals, fit_params, {group});
        }
    }

    // Final compression report
    size_t orphan_groups = 0;
    for (const auto& g : groups) {
        if (g.storage_type == ORPHAN_GROUP) {
            ++orphan_groups;
        }
    }
    const size_t normal_groups = groups.size() - orphan_groups;
    size_t total_orphan_intervals = 0;
    double max_group_error = 0.0;
    for (auto& g : groups) {
        if (g.storage_type == ORPHAN_GROUP) {
            total_orphan_intervals += g.stats.total_intervals;
        } else {
            g.stats.actual_error = evaluateCompressedErrorWithQuantization(
                expression_str, intervals, fit_params, {g});
            if (!std::isnan(g.stats.actual_error)) {
                max_group_error = std::max(max_group_error, g.stats.actual_error);
            }
        }
    }

    std::cout << "\nFinal Compression Report (Target Error: " << target_error << "):\n"
              << "============================================================\n"
              << std::left << std::setw(25) << "Processed intervals:" << intervals.size() << "\n"
              << std::setw(25) << "Domain range:" << "[" << full_start << ", " << full_end << "]\n"
              << std::setw(25) << "Normal groups:" << normal_groups << "\n"
              << std::setw(25) << "Orphan groups:" << orphan_groups << "\n"
              << std::setw(25) << "Orphan intervals:" << total_orphan_intervals << "\n"
              << std::setw(25) << "Significant gaps:" << global_gap_count << "\n"
              << std::setw(25) << "Max group error:" << max_group_error << "\n\n";

    // Grouping statistics
    std::cout << "| Group ID | Type    | Members | Base Length | Max Δ | Error     | Bits (S/I) |\n"
              << "|----------|---------|---------|-------------|-------|-----------|------------|\n";
    
    for (const auto& g : groups) {
        std::cout << "| " << std::setw(8) << g.id << " | "
                  << std::setw(7) << (g.storage_type == ORPHAN_GROUP ? "Orphan" : "Normal") << " | "
                  << std::setw(7) << g.stats.total_intervals << " | "
                  << std::setw(11) << std::fixed << std::setprecision(3) << g.length << " | "
                  << std::setw(5) << std::scientific << g.stats.max_delta << " | "
                  << std::setw(9) << (g.storage_type == ORPHAN_GROUP ? "N/A" : std::to_string(g.stats.actual_error)) << " | "
                  << std::setw(4) << g.bitwidth_start << "/" << std::setw(4) << g.bitwidth_intercept << " |\n";
    }
}
*/

inline double max_abs(const std::vector<double>& vec) {
    if (vec.empty()) return 0.0;
    auto max_it = std::max_element(vec.begin(), vec.end(),
                                   [](double a, double b) { return std::abs(a) < std::abs(b); });
    return std::abs(*max_it);
}

inline void groupAndCompressIntervals(const std::string& expression_str,
                                      const std::vector<Interval>& intervals,
                                      const std::vector<FitParameters>& fit_params,
                                      std::vector<IntervalGroup>& groups,
                                      double target_error) {
    std::cout << "Entering groupAndCompressIntervals:\n";
    for (size_t i = 0; i < intervals.size(); ++i) {
        std::cout << "Interval [" << intervals[i].start << ", " << intervals[i].end
                  << "], b=" << fit_params[i].b << ", c=" << fit_params[i].c << "\n";
    }

    // **Step 1：Grouping based on length**
    struct IntervalWithIndex {
        Interval interval;
        size_t index;
    };
    std::vector<IntervalWithIndex> sorted_intervals;
    for (size_t i = 0; i < intervals.size(); ++i) {
        sorted_intervals.push_back({intervals[i], i});
    }
    // Sort by interval length
    std::sort(sorted_intervals.begin(), sorted_intervals.end(),
              [](const IntervalWithIndex& a, const IntervalWithIndex& b) {
                  return (a.interval.end - a.interval.start) < (b.interval.end - b.interval.start);
              });

    // Grouping by length
    std::vector<std::vector<size_t>> group_indices;
    std::vector<size_t> current_group;
    double tolerance = target_error; // Length tolerance
    for (const auto& item : sorted_intervals) {
        double len = item.interval.end - item.interval.start;
        if (current_group.empty()) {
            current_group.push_back(item.index);
        } else {
            double first_len = intervals[current_group[0]].end - intervals[current_group[0]].start;
            if (std::abs(len - first_len) <= tolerance) {
                current_group.push_back(item.index);
            } else {
                group_indices.push_back(current_group);
                current_group = {item.index};
            }
        }
    }
    if (!current_group.empty()) {
        group_indices.push_back(current_group);
    }

    // **步骤 2：处理每个组**
    constexpr size_t MIN_GROUP_MEMBERS = 3;
    std::vector<size_t> orphan_indices;
    double full_start = INFINITY, full_end = -INFINITY;
    size_t global_gap_count = 0;
    double prev_end = sorted_intervals[0].interval.start - 1e-10;

    for (const auto& indices : group_indices) {
        IntervalGroup group;
        if (indices.size() < MIN_GROUP_MEMBERS) {
            for (size_t idx : indices) {
                orphan_indices.push_back(idx);
            }
            continue;
        }

        // **Step 3：Select base interval**
        double b_sum = 0.0, c_sum = 0.0;
        for (size_t idx : indices) {
            b_sum += fit_params[idx].b;
            c_sum += fit_params[idx].c;
        }
        double b_avg = b_sum / indices.size();
        double c_avg = c_sum / indices.size();
        size_t base_idx = indices[0];
        double min_dist = std::numeric_limits<double>::max();
        for (size_t idx : indices) {
            double dist = std::abs(fit_params[idx].b - b_avg) + std::abs(fit_params[idx].c - c_avg);
            if (dist < min_dist) {
                min_dist = dist;
                base_idx = idx;
            }
        }
        group.base_interval = intervals[base_idx];
        group.base_params = fit_params[base_idx];
        group.length = group.base_interval.end - group.base_interval.start;

        full_start = std::min(full_start, group.base_interval.start);
        full_end = std::max(full_end, group.base_interval.end);
        if (group.base_interval.start > prev_end + 5 * target_error) {
            global_gap_count++;
            std::cout << "Gap detected: " << (group.base_interval.start - prev_end) << "\n";
        }
        prev_end = group.base_interval.end;

        // **Step 4：Parameters delta encoding**
        for (size_t idx : indices) {
            DeltaEncoding de;
            de.original_index = idx;
            de.delta_start = intervals[idx].start - group.base_interval.start;
            de.delta_slope = fit_params[idx].b - group.base_params.b;
            de.delta_intercept = fit_params[idx].c - group.base_params.c;
            SymmetryInfo sym = checkIntervalEquivalence(group.base_params, fit_params[idx], target_error);
            de.is_x_reflected = sym.is_x_reflected;
            de.is_y_reflected = sym.is_y_reflected;
            group.delta_encodings.push_back(de);
        }

        // **Step 5: Bitwidth Selection**
        std::vector<double> start_deltas, slope_deltas, intercept_deltas;
        for (const auto& de : group.delta_encodings) {
            start_deltas.push_back(de.delta_start);
            slope_deltas.push_back(de.delta_slope);
            intercept_deltas.push_back(de.delta_intercept);
        }
        group.bitwidth_start = calculateOptimalBitwidth(start_deltas, target_error, group.start_scale_factor);
        group.bitwidth_slope = calculateOptimalBitwidth(slope_deltas, target_error, group.slope_scale_factor);
        group.bitwidth_intercept = calculateOptimalBitwidth(intercept_deltas, target_error, group.intercept_scale_factor);

        group.stats.total_intervals = indices.size();
        group.stats.max_delta = std::max({max_abs(start_deltas), max_abs(slope_deltas), max_abs(intercept_deltas)});
        groups.push_back(group);
    }

    // Process orphan intervals
    if (!orphan_indices.empty()) {
        IntervalGroup orphan_group;
        orphan_group.storage_type = ORPHAN_GROUP;
        for (size_t idx : orphan_indices) {
            orphan_group.delta_encodings.emplace_back(0.0, false, false, 0.0, 0.0, idx);
        }
        orphan_group.stats.total_intervals = orphan_indices.size();
        groups.push_back(orphan_group);
    }

    // Final compression report
    size_t orphan_groups = 0;
    for (auto& g : groups) {
        if (g.storage_type == ORPHAN_GROUP) ++orphan_groups;
    }
    size_t total_orphan_intervals = orphan_indices.size();
    double max_group_error = 0.0;
    for (auto& g : groups) {
        if (g.storage_type != ORPHAN_GROUP) {
            double error = evaluateCompressedErrorWithQuantization(expression_str, intervals, fit_params, {g});
            g.stats.actual_error = error;
            max_group_error = std::max(max_group_error, error);
        }
    }

    std::cout << "\nFinal Compression Report (Target Error: " << target_error << "):\n"
              << "Processed intervals: " << intervals.size() << "\n"
              << "Domain range: [" << full_start << ", " << full_end << "]\n"
              << "Normal groups: " << (groups.size() - orphan_groups) << "\n"
              << "Orphan groups: " << orphan_groups << "\n"
              << "Orphan intervals: " << total_orphan_intervals << "\n"
              << "Significant gaps: " << global_gap_count << "\n"
              << "Max group error: " << max_group_error << "\n";
}


inline std::string serializeFitParameters(const FitParameters& params) {
    std::stringstream ss;
    ss << (params.method == FittingMethod::Linear ? "0," : "1,")
       << params.a << "," << params.b << "," << params.c;
    return ss.str();
}

// Save the compressed groups to a file
inline void saveCompressedGroupsToFile(const std::vector<IntervalGroup>& groups, const std::string& filename) {
    if (groups.empty()) {
        std::cout << "No groups to save.\n";
        return;
    }

    // Initialize data structures for statistics
    std::map<double, std::vector<size_t>> length_groups; // Map from length to group indices
    std::vector<std::pair<double, double>> gaps; // List of gaps between groups
    size_t total_intervals = 0;
    size_t total_bits = 0;
    const double eps = 1e-10;

    // Initialize previous end to a value slightly less than the first group's start to avoid initial gap
    double prev_end = -std::numeric_limits<double>::infinity();
    if (!groups.empty()) {
        prev_end = groups[0].base_interval.start - eps;
    }
    std::cout << "\nGroup and LUT Mapping Analysis:\n";
    std::cout << "------------------------\n";

    // Iterate over each group to calculate statistics and identify gaps
    try {
        // Process groups
        for (size_t i = 0; i < groups.size(); i++) {
            const auto& curr_group = groups[i];
            double length = curr_group.length;
            length_groups[length].push_back(i);

            // Calculate bits required for this group
            size_t group_bits = 0;
            // Check for gaps between the current group's start and the previous group's end
            if (curr_group.base_interval.start > prev_end + eps && i > 0) {
                gaps.emplace_back(prev_end, curr_group.base_interval.start);
                std::cout << "Warning: Gap detected before group " << i 
                        << " [" << prev_end << " -> " << curr_group.base_interval.start << "]\n";
                
                const auto& prev_group = groups[i - 1];
                double gap_length = curr_group.base_interval.start - prev_end;

                bool merge_with_prev = prev_group.length <= curr_group.length;
                double length_ratio = merge_with_prev ? 
                    gap_length / prev_group.length : 
                    gap_length / curr_group.length;

                if (merge_with_prev) {
                    group_bits += (length_ratio > 0.5) ? 16 : 0; // Extra param if gap is large
                    std::cout << "Gap merged into prev group " << (i-1) 
                        << " [ratio=" << length_ratio << "]\n";
                } else {
                    group_bits += (length_ratio > 0.5) ? 16 : 0;
                    std::cout << "Gap merged into curr group " << i 
                        << " [ratio=" << length_ratio << "]\n";
                }
            }

            // Update previous end to the current group's end
            prev_end = curr_group.base_interval.end;
            length_groups[curr_group.length].push_back(i);

            // Bits for base interval parameters:
            // - BaseStart (16 bits)
            // - BaseEnd (16 bits)
            // - FitMethod (1 bit for Linear/Qudratic)
            // - FitOrder (assume 2 bits to accommodate future methods)
            // - FitA, FitB, FitC (each 16 bits)
            group_bits += 16 + 16 + 1 + 16 + 16; // + 16;

            // Bits for Delta Encodings
            for (const auto& delta : curr_group.delta_encodings) {
                (void)delta;
                // For each delta encoding:
                // - DeltaStart (16 bits)
                // - IsReflected (1 bit)
                // - DeltaIntercept (16 bits)
                // - IntervalIdx (16 bits)
                group_bits += 16 + 1 + 16;
            }

            // Accumulate total bits and intervals
            total_bits += group_bits;
            total_intervals += curr_group.delta_encodings.size();

            // Calculate average bits per interval
            double bits_per_interval = (curr_group.delta_encodings.size() > 0) ? 
                static_cast<double>(group_bits) / curr_group.delta_encodings.size() : 0.0;

            // Print group statistics
            std::cout << "Group " << i << ":\n"
                    << "  Length: " << length << "\n"
                    << "  Base interval: [" << curr_group.base_interval.start 
                    << ", " << curr_group.base_interval.end << "]\n"
                    << "  Fit Method: " << (curr_group.base_params.method == FittingMethod::Linear ? "Linear" : "Quadratic") << "\n"
                    << "  Members: " << curr_group.delta_encodings.size() << "\n"
                    << "  Bits required: " << group_bits << "\n"
                    << "  Bits per interval: " << bits_per_interval << "\n";
            // Add gap information if present
            if (curr_group.base_interval.start > prev_end + eps && i > 0) {
                std::cout << "  Gap covered: [" << prev_end << ", " 
                        << curr_group.base_interval.start << "]\n";
            } 
        }
    
        // Print LUT Mapping Summary
        std::cout << "\nLUT Mapping Summary:\n";
        std::cout << "------------------------\n";
        std::cout << "Total intervals: " << total_intervals << "\n";
        std::cout << "Total bits: " << total_bits << "\n";
        double average_bits_per_interval = (total_intervals > 0) ? 
            static_cast<double>(total_bits) / total_intervals : 0.0;
        std::cout << "Average bits per interval: " << average_bits_per_interval << "\n";

        // Print Length Distribution
        std::cout << "\nLength Distribution:\n";
        std::cout << "------------------------\n";
        for (const auto& kv : length_groups) {
            double length = kv.first;
            const std::vector<size_t>& group_ids = kv.second;
            std::cout << "Length " << length << ":\n"
                    << "  Count: " << group_ids.size() << "\n"
                    << "  Groups: ";
            for (size_t id : group_ids) {
                std::cout << id << " ";
            }
            std::cout << "\n";
        }
        std::cout << "------------------------\n";

        // Open file for writing compressed groups
        std::ofstream file(filename);
        if (file.is_open()) {
            // Write CSV header
            file << "GroupID,Length,BaseStart,BaseEnd,FitOrder,FitA,FitB,FitC,"
                << "StartScaleFactor,SlopeScaleFactor,InterceptScaleFactor,"
                << "BitWidthStart,BitBitWidthIntercept,DeltaEncodings\n";

            size_t group_id = 0;
            for (const auto& group : groups) {
                // Write group metadata and base parameters
                file << group_id << ","
                    << std::scientific 
                    << group.length << ","
                    << group.base_interval.start << ","
                    << group.base_interval.end << ","
                    // << (group.base_params.method == FittingMethod::Linear ? "0" : "1") << ","
                    << inferFitOrder(group.base_params.method) << ","
                    << group.base_params.a << ","
                    << group.base_params.b << ","
                    << group.base_params.c << ","
                    << group.start_scale_factor << ","
                    << group.slope_scale_factor << ","
                    << group.intercept_scale_factor << ","
                    << group.bitwidth_start << ","
                    << group.bitwidth_intercept << ",\"";

                // Serialize Delta Encodings
                for (size_t i = 0; i < group.delta_encodings.size(); ++i) {
                    const auto& delta = group.delta_encodings[i];
                    int quant_delta_start = static_cast<int>(std::round(delta.delta_start / group.start_scale_factor));
                    int quant_delta_slope = static_cast<int>(std::round(delta.delta_slope / group.slope_scale_factor));
                    int quant_delta_intercept = static_cast<int>(std::round(delta.delta_intercept / group.intercept_scale_factor));
                    // Serialize each delta as: delta_start:is_reflected:delta_intercept:interval_idx
                    file << quant_delta_start << ":"
                        << quant_delta_slope << ":"
                        << quant_delta_intercept << ":"
                        << (delta.is_y_reflected ? "1" : "0") << ":"
                        << (delta.is_x_reflected ? "1" : "0");
                    if (i < group.delta_encodings.size() - 1) {
                        file << ";";
                    }
                }
                file << "\"\n";
                group_id++;
            }
            file.close();
            std::cout << "Compressed groups saved to: " << filename << "\n";

            // Print reconstruction instructions
            std::cout << "\nTo reconstruct function f(x):\n"
                    << "1. Find the group containing x.\n"
                    << "2. If x is in the base interval:\n"
                    << "   - For Linear: f(x) = b*x + c\n"
                    << "   - For Quadratic: f(x) = a*x^2 + b*x + c\n"
                    << "3. If x is in a delta interval:\n"
                    << "   - Apply delta_start to adjust the interval's position.\n"
                    << "   - If is_reflected is true, invert the relevant coefficients.\n"
                    << "   - Apply delta_intercept to adjust the intercept.\n"
                    << "4. Compute f(x) using the reconstructed parameters.\n";
        } else {
            std::cout << "Failed to open file to save compressed Interval Groups!\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Error saving compressed groups: " << e.what() << "\n";
    }

    // FPGA LUT Mapping
    struct RecoveredInterval {
        double start, slope, intercept;
    };
    std::vector<RecoveredInterval> recovered_intervals;

    for (const auto& group : groups) {
        double base_start = group.base_interval.start;
        double base_slope = group.base_params.b;
        double base_intercept = group.base_params.c;

        for (const auto& delta : group.delta_encodings) {
            int quant_delta_start = static_cast<int>(std::round(delta.delta_start / group.start_scale_factor));
            int quant_delta_slope = static_cast<int>(std::round(delta.delta_slope / group.slope_scale_factor));
            int quant_delta_intercept = static_cast<int>(std::round(delta.delta_intercept / group.intercept_scale_factor));
            double start = base_start + quant_delta_start * group.start_scale_factor;
            double slope = base_slope + quant_delta_slope * group.slope_scale_factor;
            double intercept = base_intercept + quant_delta_intercept * group.intercept_scale_factor;

            if (delta.is_y_reflected) {
                slope = -slope;
                intercept = -intercept;
            }
            recovered_intervals.push_back({start, slope, intercept});
        }
    }

    std::sort(recovered_intervals.begin(), recovered_intervals.end(), 
              [](const RecoveredInterval& a, const RecoveredInterval& b) {
                  return a.start < b.start;
              });
    
    // Organize into PWL format
    const size_t num_segments = recovered_intervals.size();
    
    // For each segment, start point and parameters
    std::vector<double> breakpoints;
    std::vector<double> slopes(num_segments);
    std::vector<double> intercepts(num_segments);

    for (size_t i = 0; i < num_segments; ++i) {
        if (i < num_segments - 1) {
            breakpoints.push_back(recovered_intervals[i].start);
        }
        slopes[i] = recovered_intervals[i].slope;
        intercepts[i] = recovered_intervals[i].intercept;
    }

    const size_t num_breakpoints = breakpoints.size();

    // Quantize parameters
    double max_slope = 0, max_intercept = 0, max_breakpoint = 0;
    for (const auto& s : slopes) max_slope = std::max(max_slope, std::abs(s));
    for (const auto& c : intercepts) max_intercept = std::max(max_intercept, std::abs(c));
    for (const auto& b : breakpoints) max_breakpoint = std::max(max_breakpoint, std::abs(b));

    int slope_int_bits = max_slope <= 0 ? 1 : std::ceil(std::log2(max_slope)) + 1;
    int intercept_int_bits = max_intercept <= 0 ? 1 : std::ceil(std::log2(max_intercept)) + 1;
    int breakpoint_int_bits = max_breakpoint <= 0 ? 1 : std::ceil(std::log2(max_breakpoint)) + 1;

    int slope_frac_bits = std::max(1, std::min(15 - slope_int_bits, 15));
    int intercept_frac_bits = std::max(1, std::min(15 - intercept_int_bits, 15));
    int breakpoint_frac_bits = std::max(1, std::min(15 - breakpoint_int_bits, 15));
    int frac_bits = std::min({slope_frac_bits, intercept_frac_bits, breakpoint_frac_bits});
    const int scale_factor = 1 << frac_bits;

    std::cout << "\nFixed-point representation analysis:\n"
          << "  Max slope: " << max_slope << " (requires " << slope_int_bits << " integer bits)\n"
          << "  Max intercept: " << max_intercept << " (requires " << intercept_int_bits << " integer bits)\n"
          << "  Max breakpoint: " << max_breakpoint << " (requires " << breakpoint_int_bits << " integer bits)\n"
          << "  Available fractional bits: slope=" << slope_frac_bits 
          << ", intercept=" << intercept_frac_bits 
          << ", breakpoint=" << breakpoint_frac_bits << "\n"
          << "  Selected " << frac_bits << " fractional bits (scale_factor = " << scale_factor << ")\n"
          << "  Quantization precision: " << (1.0/scale_factor) << "\n"
          << "  Maximum quantization error: " << (1.0/(2*scale_factor)) << "\n";

    std::vector<int> q_breakpoints;
    for (double b : breakpoints) {
        q_breakpoints.push_back(static_cast<int>(std::round(b * scale_factor)));
    }
    std::vector<int> q_slopes;
    for (double s : slopes) {
        q_slopes.push_back(static_cast<int>(std::round(s * scale_factor)));
    }
    std::vector<int> q_intercepts;
    for (double c : intercepts) {
        q_intercepts.push_back(static_cast<int>(std::round(c * scale_factor)));
    }

    // Write to file
    std::string verilog_filename = filename + "_lut.v";
    std::ofstream verilog_file(verilog_filename);
    if (verilog_file.is_open()) {
        verilog_file << "module pwl_recovery (\n"
                    << "    input wire [15:0] x,\n"
                    << "    output reg [15:0] y\n"
                    << ");\n"
                    << "    localparam NUM_BREAKPOINTS = " << num_breakpoints << ";\n"
                    << "    localparam NUM_SEGMENTS = " << num_segments << ";\n"
                    << "    localparam FRAC_BITS = " << frac_bits << ";\n";
        
        if (num_breakpoints > 0) {
            verilog_file << "    reg [15:0] breakpoints [0:" << (num_breakpoints - 1) << "];\n";
        } else {
            verilog_file << "    // No breakpoints needed for single segment\n";
        }
        
        verilog_file << "    reg [15:0] slopes [0:" << (num_segments - 1) << "];\n"
                    << "    reg [15:0] intercepts [0:" << (num_segments - 1) << "];\n";
        
        // 计算索引所需的最小位数
        int index_bits = (num_segments <= 1) ? 1 : static_cast<int>(std::ceil(std::log2(num_segments)));
        verilog_file << "    reg [" << (index_bits - 1) << ":0] idx;\n"
                    << "    integer i;\n\n"
                    << "    initial begin\n";
        
        // 初始化断点
        for (size_t i = 0; i < num_breakpoints; ++i) {
            verilog_file << "        breakpoints[" << i << "] = 16'd" << q_breakpoints[i] << ";\n";
        }
        
        // 初始化斜率和截距
        for (size_t i = 0; i < num_segments; ++i) {
            verilog_file << "        slopes[" << i << "] = 16'd" << q_slopes[i] << ";\n";
        }
        for (size_t i = 0; i < num_segments; ++i) {
            verilog_file << "        intercepts[" << i << "] = 16'd" << q_intercepts[i] << ";\n";
        }
        
        verilog_file << "    end\n\n"
                    << "    always @(*) begin\n"
                    << "        idx = 0;\n";
        
        if (num_breakpoints > 0) {
            verilog_file << "        for (i = 0; i < NUM_BREAKPOINTS; i = i + 1) begin\n"
                        << "            if (x >= breakpoints[i]) idx = i + 1;\n"
                        << "            else break;\n"
                        << "        end\n";
        }
        
        // 使用计算得到的小数位数进行右移
        verilog_file << "        y = (slopes[idx] * x + intercepts[idx]) >> FRAC_BITS;\n"
                    << "    end\n"
                    << "endmodule\n";
        
        verilog_file.close();
        std::cout << "\nFPGA LUT Verilog code saved to: " << verilog_filename << "\n";
        std::cout << "Generated LUT with " << num_segments << " segments";
        if (num_breakpoints > 0) {
            std::cout << " and " << num_breakpoints << " breakpoints";
        }
        std::cout << ".\n";
        
        // 额外的分析：估计 LUT 资源使用
        size_t total_16bit_registers = num_breakpoints + num_segments * 2; // breakpoints + slopes + intercepts
        size_t total_bits = total_16bit_registers * 16 + index_bits;
        std::cout << "Estimated FPGA resource usage:\n"
                << "  16-bit registers: " << total_16bit_registers << "\n"
                << "  Index register bits: " << index_bits << "\n"
                << "  Total storage bits: " << total_bits << "\n";
                
        // 验证量化过程中的误差
        std::cout << "\nQuantization validation:\n";
        for (size_t i = 0; i < std::min(size_t(5), num_segments); ++i) {
            double reconstructed_slope = static_cast<double>(q_slopes[i]) / scale_factor;
            double reconstructed_intercept = static_cast<double>(q_intercepts[i]) / scale_factor;
            double slope_error = std::abs(slopes[i] - reconstructed_slope);
            double intercept_error = std::abs(intercepts[i] - reconstructed_intercept);
            
            std::cout << "  Segment " << i << ":\n"
                    << "    Slope: original=" << slopes[i] 
                    << ", quantized=" << reconstructed_slope
                    << ", error=" << slope_error 
                    << " (" << (slope_error / std::abs(slopes[i]) * 100) << "%)\n"
                    << "    Intercept: original=" << intercepts[i] 
                    << ", quantized=" << reconstructed_intercept
                    << ", error=" << intercept_error
                    << " (" << (intercept_error / std::abs(intercepts[i] + 1e-10) * 100) << "%)\n";
        }
        if (num_segments > 5) {
            std::cout << "  ... (showing only first 5 segments)\n";
        }
        
    } else {
        std::cout << "Failed to open Verilog file for writing.\n";
        throw std::runtime_error("Verilog file opening failed");
    }
}

#endif // INTERVAL_GROUP_COMPRESSOR_HPP

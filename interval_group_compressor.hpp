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
#include <iomanip>
#include "interval_optimizer.hpp"
#include "function_fitter.hpp"

extern void createDirectory(const std::string& dirPath);

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

// Generate HEX files for FPGA implementation
void generateFPGAHexFiles(const std::string& directory, const std::string& cleanName,
                        const std::vector<int>& q_breakpoints,
                        const std::vector<int>& q_slopes,
                        const std::vector<int>& q_intercepts) {
    
    // Generate breakpoints HEX file for Verilog simulation
    std::string bp_hex_filename = directory + "/" + cleanName + "_breakpoints.hex";
    std::ofstream bp_hex_file(bp_hex_filename);
    if (bp_hex_file.is_open()) {
        for (const auto& value : q_breakpoints) {
            // Convert to hex format with 4 digits (16-bit)
            bp_hex_file << std::hex << std::setfill('0') << std::setw(4) << (value & 0xFFFF) << "\n";
        }
        bp_hex_file.close();
        std::cout << "Breakpoints HEX file saved to: " << bp_hex_filename << "\n";
    } else {
        std::cerr << "Failed to open file: " << bp_hex_filename << "\n";
    }

    // Generate slopes HEX file for Verilog simulation
    std::string slopes_hex_filename = directory + "/" + cleanName + "_slopes.hex";
    std::ofstream slopes_hex_file(slopes_hex_filename);
    if (slopes_hex_file.is_open()) {
        for (const auto& value : q_slopes) {
            // Convert to hex format with 4 digits (16-bit)
            slopes_hex_file << std::hex << std::setfill('0') << std::setw(4) << (value & 0xFFFF) << "\n";
        }
        slopes_hex_file.close();
        std::cout << "Slopes HEX file saved to: " << slopes_hex_filename << "\n";
    } else {
        std::cerr << "Failed to open file: " << slopes_hex_filename << "\n";
    }

    // Generate intercepts HEX file for Verilog simulation
    std::string intercepts_hex_filename = directory + "/" + cleanName + "_intercepts.hex";
    std::ofstream intercepts_hex_file(intercepts_hex_filename);
    if (intercepts_hex_file.is_open()) {
        for (const auto& value : q_intercepts) {
            // Convert to hex format with 4 digits (16-bit)
            intercepts_hex_file << std::hex << std::setfill('0') << std::setw(4) << (value & 0xFFFF) << "\n";
        }
        intercepts_hex_file.close();
        std::cout << "Intercepts HEX file saved to: " << intercepts_hex_filename << "\n";
    } else {
        std::cerr << "Failed to open file: " << intercepts_hex_filename << "\n";
    }
}

// Generate simulation test vectors for hardware verification
void generateSimulationVectors(const std::string& expression_str,
                              const std::string& directory,
                              const std::string& cleanName,
                              double start, double end,
                              int scale_factor, int frac_bits,
                              size_t num_vectors = 100) {
    
    // Create 'sim/test_vectors' subdirectory if it doesn't exist
    std::string test_dir = directory + "/sim/test_vectors";
    createDirectory(directory + "/sim");
    createDirectory(test_dir);
    
    // Create test vector file
    std::string vectors_filename = test_dir + "/" + cleanName + "_vectors.txt";
    std::ofstream vectors_file(vectors_filename);
    
    if (!vectors_file.is_open()) {
        std::cerr << "Failed to open test vectors file: " << vectors_filename << std::endl;
        return;
    }

    if (scale_factor <= 16) {
        // Use a more reasonable scale factor based on fractional bits
        frac_bits = std::max(10, frac_bits); // At least 10 fractional bits for good precision
        scale_factor = 1 << frac_bits;      // 2^frac_bits
        std::cout << "Warning: Scale factor too small (" << scale_factor << 
                  "). Using " << scale_factor << " (2^" << frac_bits << ") instead.\n";
    } 
    
    // Setup for expression evaluation
    double x = 0.0;
    exprtk::symbol_table<double> symbol_table;
    symbol_table.add_constants();
    symbol_table.add_variable("x", x);
    
    // Add custom functions if needed
    /*
    relu_fn<double> relu_f;
    gelu_fn<double> gelu_f;
    swishglu_fn<double> swish_f;
    symbol_table.add_function("relu", relu_f);
    symbol_table.add_function("gelu", gelu_f);
    symbol_table.add_function("swishglu", swish_f);
    */
    
    // Parse the expression
    exprtk::expression<double> expression;
    expression.register_symbol_table(symbol_table);
    exprtk::parser<double> parser;
    
    if (!parser.compile(expression_str, expression)) {
        std::cerr << "Error parsing the expression for test vector generation: " << parser.error() << std::endl;
        return;
    }
    
    // Generate evenly spaced test points
    double step = (end - start) / (num_vectors - 1);
    
    std::cout << "Generating " << num_vectors << " test vectors for simulation...\n";
    std::cout << "Using scale factor: " << scale_factor << " (2^" << frac_bits << ")\n";
    std::cout << "Range: [" << start << ", " << end << "]\n";
    
    // Print header for debug info
    std::cout << "\nFirst 10 test vectors (for verification):\n";
    std::cout << "------------------------------------------\n";
    std::cout << "| Index |     x     |     y     |  q_x (hex)  |  q_y (hex)  |\n";
    std::cout << "------------------------------------------\n";
    
    for (size_t i = 0; i < num_vectors; ++i) {
        // Calculate input value
        x = start + i * step;
        
        // Evaluate the function
        double y = expression.value();
        
        // Quantize to fixed-point representation
        int16_t q_x = static_cast<int>(std::round(x * scale_factor)) & 0xFFFF;
        int16_t q_y = static_cast<int>(std::round(y * scale_factor)) & 0xFFFF;
        
        // Print debug info for first few samples
        if (i < 10 || i == num_vectors-1) {
            std::cout << "| " << std::setw(5) << i << " | " 
                      << std::fixed << std::setprecision(6) << std::setw(10) << x << " | " 
                      << std::setw(10) << y << " | "
                      << "0x" << std::hex << std::setfill('0') << std::setw(4) << (q_x & 0xFFFF) << " | "
                      << "0x" << std::hex << std::setfill('0') << std::setw(4) << (q_y & 0xFFFF) << " |\n";
            if (i == 9 && num_vectors > 11) {
                std::cout << "| ...    | ...        | ...        | ...         | ...         |\n";
            }
        }

        // Write as 32-bit value (16-bit input, 16-bit expected output)
        // Write as 8-character hex string (4 for input, 4 for output)
        vectors_file << std::hex << std::setfill('0') 
                    << std::setw(4) << (q_x & 0xFFFF) 
                    << std::setw(4) << (q_y & 0xFFFF)
                    << "\n";
    }
    std::cout << "------------------------------------------\n";
    
    vectors_file.close();
    std::cout << "Test vectors saved to: " << vectors_filename << "\n";
    
    // Also generate a CSV file for easier verification
    std::string csv_filename = test_dir + "/" + cleanName + "_vectors.csv";
    std::ofstream csv_file(csv_filename);
    
    if (csv_file.is_open()) {
        csv_file << "Input,Expected,Input(Hex),Expected(Hex),Vector\n";
        
        // Reset and regenerate for CSV
        for (size_t i = 0; i < num_vectors; ++i) {
            x = start + i * step;
            double y = expression.value();
            
            int16_t q_x = static_cast<int16_t>(std::round(x * scale_factor));
            int16_t q_y = static_cast<int16_t>(std::round(y * scale_factor));
            
            // Improved CSV generation - use decimal for values and proper hex formatting
            csv_file << std::fixed << std::setprecision(6) << std::dec 
                    << x << "," << y << ","
                    << "0x" << std::hex << std::setfill('0') << std::setw(4) << (q_x & 0xFFFF) << ","
                    << "0x" << std::hex << std::setfill('0') << std::setw(4) << (q_y & 0xFFFF) << ","
                    << "0x" << std::hex << std::setfill('0') << std::setw(4) << (q_x & 0xFFFF) 
                    << std::setw(4) << (q_y & 0xFFFF) << "\n";
        }
        
        csv_file.close();
        std::cout << "CSV test vectors saved to: " << csv_filename << "\n";
    }

    // Verify precision distribution
    std::cout << "\nVerifying precision distribution...\n";
    std::set<int16_t> unique_x_values, unique_y_values;
    
    // Reset and count unique values
    for (size_t i = 0; i < num_vectors; ++i) {
        x = start + i * step;
        double y = expression.value();
        
        int16_t q_x = static_cast<int16_t>(std::round(x * scale_factor));
        int16_t q_y = static_cast<int16_t>(std::round(y * scale_factor));
        
        unique_x_values.insert(q_x);
        unique_y_values.insert(q_y);
    }
    
    std::cout << "Number of unique quantized input values: " << unique_x_values.size() 
              << " of " << num_vectors << " (" 
              << (static_cast<double>(unique_x_values.size()) / num_vectors * 100.0) 
              << "%)\n";
    std::cout << "Number of unique quantized output values: " << unique_y_values.size() 
              << " (" << (static_cast<double>(unique_y_values.size()) / num_vectors * 100.0) 
              << "%)\n";
    
    if (unique_y_values.size() < 10) {
        std::cout << "WARNING: Very few unique output values detected. Consider increasing scale_factor.\n";
    }
    
    std::cout << "Test vector generation complete.\n";
}

// Function to output implementation metrics for the FPGA design
void outputFPGAMetrics(const std::string& directory, const std::string& cleanName,
                      int num_breakpoints, int num_segments, int frac_bits,
                      int slope_bits, int intercept_bits) {
    
    std::string metrics_filename = directory + "/" + cleanName + "_fpga_metrics.txt";
    std::ofstream metrics_file(metrics_filename);
    
    if (!metrics_file.is_open()) {
        std::cerr << "Failed to open FPGA metrics file: " << metrics_filename << std::endl;
        return;
    }
    
    int index_bits = (num_segments <= 1) ? 1 : static_cast<int>(std::ceil(std::log2(num_segments)));
    int total_memory_bits = num_breakpoints * 16 + num_segments * 16 * 2; // breakpoints + slopes + intercepts
    
    metrics_file << "FPGA Implementation Metrics for " << cleanName << "\n"
                << "=============================================\n\n"
                << "ROM Parameters:\n"
                << "- Number of breakpoints: " << num_breakpoints << "\n"
                << "- Number of segments: " << num_segments << "\n"
                << "- Index bits required: " << index_bits << "\n"
                << "- Fractional bits: " << frac_bits << "\n\n"
                
                << "Fixed-point Format:\n"
                << "- Slope format: " << (16 - slope_bits) << "." << slope_bits << " (signed fixed-point)\n"
                << "- Intercept format: " << (16 - intercept_bits) << "." << intercept_bits << " (signed fixed-point)\n\n"
                
                << "Resource Estimation:\n"
                << "- Total memory bits: " << total_memory_bits << " bits\n"
                << "- Memory organization: " << num_breakpoints << " breakpoints + " 
                << num_segments << " slopes + " << num_segments << " intercepts\n"
                << "- Memory width: 16 bits per entry\n"
                << "- Estimated BRAMs: " << (total_memory_bits / 18432 + 1) << " (assuming 18Kb BRAMs)\n"
                << "- Pipeline stages: 3 (address decoding, memory access, interpolation)\n"
                << "- Estimated latency: ~5-7 clock cycles per evaluation\n";
    
    metrics_file.close();
    std::cout << "FPGA metrics saved to: " << metrics_filename << "\n";
}

// Save the compressed groups to a file and generate FPGA implementation files
inline void saveCompressedGroupsToFile(const std::vector<IntervalGroup>& groups, const std::string& filename) {
    if (groups.empty()) {
        std::cout << "No groups to save.\n";
        return;
    }

    // Extract the directory path from filename
    std::string directory = "";
    size_t last_slash = filename.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        directory = filename.substr(0, last_slash + 1);  // Include the trailing slash
    }

    // Extract the function name from the directory path
    std::string cleanName = "pwl";  // Default if we can't determine function name
    if (last_slash != std::string::npos) {
        size_t prev_slash = filename.find_last_of("/\\", last_slash - 1);
        if (prev_slash != std::string::npos) {
            // Extract the directory name between slashes (e.g., "tanh" from "results/tanh/file.csv")
            cleanName = filename.substr(prev_slash + 1, last_slash - prev_slash - 1);
        }
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
                    // Serialize each delta as: delta_start:delta_slope:delta_intercept:is_y_reflected:is_x_reflected
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

    // Calculate index bits required for addressing the segments
    // This is defined here to ensure it's in scope for all files generated later
    int index_bits = (num_segments <= 1) ? 1 : static_cast<int>(std::ceil(std::log2(num_segments)));

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

    // Quantize values for fixed-point representation
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

    // Generate main Verilog module file
    std::string verilog_filename = directory + cleanName + "_lut.v";
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
                    << "    reg [15:0] intercepts [0:" << (num_segments - 1) << "];\n"
                    << "    reg [" << (index_bits - 1) << ":0] idx;\n"
                    << "    integer i;\n\n"
                    << "    initial begin\n";
        
        // Initialize breakpoints
        for (size_t i = 0; i < num_breakpoints; ++i) {
            verilog_file << "        breakpoints[" << i << "] = 16'd" << q_breakpoints[i] << ";\n";
        }
        
        // Initialize slopes and intercepts
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
        
        // Use the fractional bits for correct fixed-point calculation
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
        
        // FPGA resource usage estimation
        size_t total_16bit_registers = num_breakpoints + num_segments * 2; // breakpoints + slopes + intercepts
        size_t total_bits = total_16bit_registers * 16 + index_bits;
        std::cout << "Estimated FPGA resource usage:\n"
                << "  16-bit registers: " << total_16bit_registers << "\n"
                << "  Index register bits: " << index_bits << "\n"
                << "  Total storage bits: " << total_bits << "\n";
                
        // Quantization validation
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

    // Generate Verilog configuration header file
    std::string vh_filename = directory + cleanName + "_config.vh";
    std::ofstream vh_file(vh_filename);
    if (vh_file.is_open()) {
        vh_file << "// Auto-generated PWL configuration parameters\n"
                << "`ifndef PWL_CONFIG_VH\n"
                << "`define PWL_CONFIG_VH\n\n"
                << "`define PWL_DATA_WIDTH 16\n"
                << "`define PWL_NUM_BREAKPOINTS " << num_breakpoints << "\n"
                << "`define PWL_NUM_SEGMENTS " << num_segments << "\n"
                << "`define PWL_FRAC_BITS " << frac_bits << "\n"
                << "`define PWL_ADDR_WIDTH " << index_bits << "\n\n"
                << "`endif // PWL_CONFIG_VH\n";
        vh_file.close();
        std::cout << "Configuration header saved to: " << vh_filename << "\n";
    }

    // Generate breakpoints COE file for Xilinx IP core
    std::string bp_coe_filename = directory + cleanName + "_breakpoints.coe";
    std::ofstream bp_coe_file(bp_coe_filename);
    if (bp_coe_file.is_open()) {
        bp_coe_file << "memory_initialization_radix=10;\n"
                    << "memory_initialization_vector=\n";
        for (size_t i = 0; i < q_breakpoints.size(); ++i) {
            bp_coe_file << q_breakpoints[i];
            if (i < q_breakpoints.size() - 1) bp_coe_file << ",\n";
        }
        bp_coe_file << ";\n";
        bp_coe_file.close();
        std::cout << "Breakpoints COE file saved to: " << bp_coe_filename << "\n";
    }

    // Generate slopes COE file for Xilinx IP core
    std::string slopes_coe_filename = directory + cleanName + "_slopes.coe";
    std::ofstream slopes_coe_file(slopes_coe_filename);
    if (slopes_coe_file.is_open()) {
        slopes_coe_file << "memory_initialization_radix=10;\n"
                    << "memory_initialization_vector=\n";
        for (size_t i = 0; i < q_slopes.size(); ++i) {
            slopes_coe_file << q_slopes[i];
            if (i < q_slopes.size() - 1) slopes_coe_file << ",\n";
        }
        slopes_coe_file << ";\n";
        slopes_coe_file.close();
        std::cout << "Slopes COE file saved to: " << slopes_coe_filename << "\n";
    }

    // Generate intercepts COE file for Xilinx IP core
    std::string intercepts_coe_filename = directory + cleanName + "_intercepts.coe";
    std::ofstream intercepts_coe_file(intercepts_coe_filename);
    if (intercepts_coe_file.is_open()) {
        intercepts_coe_file << "memory_initialization_radix=10;\n"
                        << "memory_initialization_vector=\n";
        for (size_t i = 0; i < q_intercepts.size(); ++i) {
            intercepts_coe_file << q_intercepts[i];
            if (i < q_intercepts.size() - 1) intercepts_coe_file << ",\n";
        }
        intercepts_coe_file << ";\n";
        intercepts_coe_file.close();
        std::cout << "Intercepts COE file saved to: " << intercepts_coe_filename << "\n";
    }

    // Generate Verilog memory initialization file
    std::string mem_v_filename = directory + cleanName + "_mem_init.v";
    std::ofstream mem_v_file(mem_v_filename);
    if (mem_v_file.is_open()) {
        mem_v_file << "// Auto-generated PWL memory initialization\n"
                << "`ifndef PWL_MEM_INIT_V\n"
                << "`define PWL_MEM_INIT_V\n\n"
                << "// Initialize memories with hardcoded values\n"
                << "task initialize_pwl_memories;\n"
                << "    input [`PWL_ADDR_WIDTH-1:0] num_breakpoints;\n"
                << "    input [`PWL_ADDR_WIDTH-1:0] num_segments;\n"
                << "    reg [`PWL_DATA_WIDTH-1:0] bp_mem [0:`PWL_NUM_BREAKPOINTS-1];\n"
                << "    reg [`PWL_DATA_WIDTH-1:0] slope_mem [0:`PWL_NUM_SEGMENTS-1];\n"
                << "    reg [`PWL_DATA_WIDTH-1:0] intercept_mem [0:`PWL_NUM_SEGMENTS-1];\n"
                << "    integer i;\n"
                << "begin\n";
                
        // Initialize breakpoints
        for (size_t i = 0; i < q_breakpoints.size(); ++i) {
            mem_v_file << "    bp_mem[" << i << "] = " << q_breakpoints[i] << ";\n";
        }
        
        // Initialize slopes
        for (size_t i = 0; i < q_slopes.size(); ++i) {
            mem_v_file << "    slope_mem[" << i << "] = " << q_slopes[i] << ";\n";
        }
        
        // Initialize intercepts
        for (size_t i = 0; i < q_intercepts.size(); ++i) {
            mem_v_file << "    intercept_mem[" << i << "] = " << q_intercepts[i] << ";\n";
        }
        
        mem_v_file << "end\n"
                << "endtask\n\n"
                << "`endif // PWL_MEM_INIT_V\n";
        mem_v_file.close();
        std::cout << "Memory initialization file saved to: " << mem_v_filename << "\n";
    }

    // Generate Vivado TCL build script
    std::string tcl_filename = directory + cleanName + "_build.tcl";
    std::ofstream tcl_file(tcl_filename);
    if (tcl_file.is_open()) {
        tcl_file << "# Auto-generated Vivado build script for PWL\n"
                << "set project_name \"pwl_recovery\"\n"
                << "set project_dir \"./[set project_name]_proj\"\n"
                << "set device \"xc7a35tcpg236-1\"\n\n"
                << "# Create project\n"
                << "create_project $project_name $project_dir -part $device -force\n\n"
                << "# Create block memory IP cores for parameters\n"
                << "create_ip -name blk_mem_gen -vendor xilinx.com -library ip -module_name breakpoints_rom\n"
                << "set_property -dict [list \\\n"
                << "    CONFIG.Memory_Type {Single_Port_ROM} \\\n"
                << "    CONFIG.Write_Width_A {16} \\\n"
                << "    CONFIG.Read_Width_A {16} \\\n"
                << "    CONFIG.Write_Depth_A {" << num_breakpoints << "} \\\n"
                << "    CONFIG.Read_Depth_A {" << num_breakpoints << "} \\\n"
                << "    CONFIG.Enable_A {Always_Enabled} \\\n"
                << "    CONFIG.Load_Init_File {true} \\\n"
                << "    CONFIG.Coe_File {" << bp_coe_filename << "} \\\n"
                << "    CONFIG.Fill_Remaining_Memory_Locations {true} \\\n"
                << "    CONFIG.Remaining_Memory_Locations {0} \\\n"
                << "    CONFIG.Use_RSTA_Pin {false} \\\n"
                << "    CONFIG.EN_SAFETY_CKT {false} \\\n"
                << "] [get_ips breakpoints_rom]\n\n";
        
        tcl_file << "create_ip -name blk_mem_gen -vendor xilinx.com -library ip -module_name slopes_rom\n"
                << "set_property -dict [list \\\n"
                << "    CONFIG.Memory_Type {Single_Port_ROM} \\\n"
                << "    CONFIG.Write_Width_A {16} \\\n"
                << "    CONFIG.Read_Width_A {16} \\\n"
                << "    CONFIG.Write_Depth_A {" << num_segments << "} \\\n"
                << "    CONFIG.Read_Depth_A {" << num_segments << "} \\\n"
                << "    CONFIG.Enable_A {Always_Enabled} \\\n"
                << "    CONFIG.Load_Init_File {true} \\\n"
                << "    CONFIG.Coe_File {" << slopes_coe_filename << "} \\\n"
                << "    CONFIG.Fill_Remaining_Memory_Locations {true} \\\n"
                << "    CONFIG.Remaining_Memory_Locations {0} \\\n"
                << "    CONFIG.Use_RSTA_Pin {false} \\\n"
                << "    CONFIG.EN_SAFETY_CKT {false} \\\n"
                << "] [get_ips slopes_rom]\n\n";
                
        tcl_file << "create_ip -name blk_mem_gen -vendor xilinx.com -library ip -module_name intercepts_rom\n"
                << "set_property -dict [list \\\n"
                << "    CONFIG.Memory_Type {Single_Port_ROM} \\\n"
                << "    CONFIG.Write_Width_A {16} \\\n"
                << "    CONFIG.Read_Width_A {16} \\\n"
                << "    CONFIG.Write_Depth_A {" << num_segments << "} \\\n"
                << "    CONFIG.Read_Depth_A {" << num_segments << "} \\\n"
                << "    CONFIG.Enable_A {Always_Enabled} \\\n"
                << "    CONFIG.Load_Init_File {true} \\\n"
                << "    CONFIG.Coe_File {" << intercepts_coe_filename << "} \\\n"
                << "    CONFIG.Fill_Remaining_Memory_Locations {true} \\\n"
                << "    CONFIG.Remaining_Memory_Locations {0} \\\n"
                << "    CONFIG.Use_RSTA_Pin {false} \\\n"
                << "    CONFIG.EN_SAFETY_CKT {false} \\\n"
                << "] [get_ips intercepts_rom]\n\n";
                
        tcl_file << "# Generate IP cores\n"
                << "generate_target all [get_ips breakpoints_rom]\n"
                << "generate_target all [get_ips slopes_rom]\n"
                << "generate_target all [get_ips intercepts_rom]\n\n"
                << "# Add Verilog design sources\n"
                << "add_files -norecurse " << vh_filename << "\n"
                << "add_files -norecurse " << verilog_filename << "\n"
                << "add_files -norecurse " << mem_v_filename << "\n\n"
                << "# Run synthesis and implementation\n"
                << "launch_runs synth_1\n"
                << "wait_on_run synth_1\n"
                << "launch_runs impl_1 -to_step write_bitstream\n"
                << "wait_on_run impl_1\n\n";
        
        tcl_file.close();
        std::cout << "Vivado build script saved to: " << tcl_filename << "\n";
    }

    generateFPGAHexFiles(directory, cleanName, q_breakpoints, q_slopes, q_intercepts);

    // Generate simulation test vectors
    // generateSimulationVectors(expression_str, directory, cleanName, x_min, x_max, scale_factor, frac_bits);

    // Output FPGA implementation metrics
    outputFPGAMetrics(directory, cleanName, q_breakpoints.size(), q_slopes.size(), 
                    frac_bits, slope_int_bits, intercept_int_bits);

    std::cout << "FPGA implementation files generated successfully." << std::endl;

}

#endif // INTERVAL_GROUP_COMPRESSOR_HPP

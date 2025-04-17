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
#include <random>
#include "interval_optimizer.hpp"
#include "function_fitter.hpp"

extern void createDirectory(const std::string& dirPath);

struct DeltaEncoding {
    double delta_start;     // 通用：对于常规组是相对起点，对于孤立组是绝对起点
    bool is_x_reflected;
    bool is_y_reflected;
    double delta_intercept; // 通用：对于常规组是增量截距，对于孤立组是绝对截距
    double delta_slope;     // 通用：对于常规组是增量斜率，对于孤立组是绝对斜率
    size_t original_index;
    bool is_padding;
    
    // 孤立组专用：存储完整区间信息，包括精确终点
    Interval original_interval;
    FitParameters original_params;
    
    // 构造函数保持不变
    DeltaEncoding(double start, bool x_ref, bool y_ref, double intercept, double slope, 
                 size_t idx, bool padding = false)
        : delta_start(start), is_x_reflected(x_ref), is_y_reflected(y_ref),
          delta_intercept(intercept), delta_slope(slope), original_index(idx),
          is_padding(padding) {}
};

struct GroupStatistics {
    size_t total_intervals;
    size_t original_intervals;    // Added field for original count before padding
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
    NORMAL_GROUP = 0,
    ORPHAN_GROUP = 1
};

// Define the IntervalGroup structure
struct IntervalGroup {
    static size_t global_group_id;
    size_t id; // Group identifier
    double length; // All intervals in the group have the same length
    Interval base_interval; // Base interval
    FitParameters base_params; // Fitting parameters of the base interval
    std::vector<DeltaEncoding> delta_encodings; // Delta encodings for member intervals
    double start_scale_factor; // Scale factor for start position differences
    double intercept_scale_factor; // Scale factor for intercept differences
    double slope_scale_factor; // Scale factor for slope differences
    double primary_scale_factor{65536.0}; // Primary scale factor for quantization
    int bitwidth_start; // Bitwidth for start position differences
    int bitwidth_intercept; // Bitwidth for intercept differences
    int bitwidth_slope; // Bitwidth for slope differences
    GroupStatistics stats; // Group statistics
    bool has_prefix_gap{false};
    double gap_length{0.0};
    double base_length;
    uint8_t length_type; // 0: Normal, 1: Prefix, 2: Suffix
    GroupStorageType storage_type{NORMAL_GROUP};

    // Power-of-two optimization fields
    bool use_power_of_two{false};     // Whether to use power-of-two lookup
    int power_of_two_value{0};        // The power-of-two value
    int shift_amount{0};              // The corresponding bit shift amount
    size_t lookup_table_offset{0};    // Offset in the global lookup table
    
    // Alias for compatibility
    // int& lookup_accelerator = power_of_two_value;
    int& lookup_accelerator() { return power_of_two_value; }
    const int& lookup_accelerator() const { return power_of_two_value; }
    
    IntervalGroup() : id(global_group_id++), length(0.0),
        base_interval(Interval()), base_params(),
        start_scale_factor(1.0), intercept_scale_factor(1.0),
        slope_scale_factor(1.0), primary_scale_factor(65536.0), bitwidth_start(8),
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

inline int calculateOptimalBitwidth(const std::vector<double>& deltas, 
                                    double max_error,
                                    double &scale_factor) {
    if (deltas.empty()) {
        scale_factor = 1.0;
        return 8;
    }

    double max_abs_delta = 0.0;
    for (const auto& delta : deltas) {
        max_abs_delta = std::max(max_abs_delta, std::abs(delta));
    }

    if (max_abs_delta < max_error || max_abs_delta < 1e-10) {
        scale_factor = 1.0;
        return 8;
    }

    std::cout << "Debug: Max absolute delta: " << max_abs_delta << "\n";
    std::cout << "Debug: Target error: " << max_error << "\n";

    int low = 8, high = 24;
    int optimal = high;
    
    while (low <= high) {
        int mid = (low + high) / 2;

        scale_factor = ((1 << (mid-1)) - 1) / max_abs_delta;

        double quant_error = 0.5 / scale_factor;
        
        std::cout << "Debug: Testing bits: " << mid 
                  << ", scale_factor: " << scale_factor 
                  << ", quant_error: " << quant_error << "\n";
        
        if (quant_error <= max_error) {
            optimal = mid;
            high = mid - 1;
        } else {
            low = mid + 1;
        }
    }

    scale_factor = ((1 << (optimal-1)) - 1) / max_abs_delta;
    
    std::cout << "Debug: Selected optimal bits: " << optimal 
              << ", final scale_factor: " << scale_factor << "\n";
    
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

double simulateHardwareLookup(
    double x_in, 
    const std::vector<IntervalGroup>& groups, 
    double scale_factor = 65536.0,
    double target_error = -1.0,
    double true_value = std::numeric_limits<double>::quiet_NaN());
double evaluateCompressedErrorWithQuantization(
    const std::string& expression_str,
    const std::vector<Interval>& intervals,
    const std::vector<FitParameters>& fit_params,
    const std::vector<IntervalGroup>& groups,
    int precision_bits = 16,
    double* max_error = nullptr,
    double acceptable_error = 1e-4) {  // Add explicit acceptable_error parameter with default
    
    std::cout << "\n===== Compressed Error Evaluation (Function: " << expression_str << ") =====\n";
    std::cout << "Intervals: " << intervals.size() << ", Groups: " << groups.size() 
              << ", Precision bits: " << precision_bits << "\n";
    std::cout << "Target error: " << acceptable_error << "\n";
    
    // Parameter level error is typically 1/100 of function level error
    double parameter_error_target = acceptable_error / 100.0;
    std::cout << "Parameter error target: " << parameter_error_target << "\n";
    
    // Parse expression
    double x = 0.0;
    exprtk::symbol_table<double> symbol_table;
    symbol_table.add_variable("x", x);
    symbol_table.add_constants();
    
    exprtk::expression<double> expression;
    expression.register_symbol_table(symbol_table);
    exprtk::parser<double> parser;
    
    if (!parser.compile(expression_str, expression)) {
        std::cerr << "Error: Failed to parse expression\n";
        return std::numeric_limits<double>::quiet_NaN();
    }
    
    // Calculate quantization factor
    double scale_factor = 1 << precision_bits;
    std::cout << "Using scale factor: " << scale_factor << "\n";
    
    // Statistics
    double total_error = 0.0;
    double local_max_error = 0.0;
    size_t total_points = 0;
    size_t valid_points = 0;
    
    // Error tracking by group
    std::vector<std::tuple<int, double, double, size_t>> group_errors; // group ID, avg error, max error, point count
    std::map<int, std::vector<std::tuple<double, double, double>>> high_error_details; // group ID -> [(x, true value, error)]
    
    // High error points collection
    std::vector<std::tuple<double, double, double, double, int>> high_error_points; // x, true value, estimate, error, group ID
    
    // Determine evaluation range
    double global_min = std::numeric_limits<double>::max();
    double global_max = std::numeric_limits<double>::lowest();
    
    for (const auto& interval : intervals) {
        global_min = std::min(global_min, interval.start);
        global_max = std::max(global_max, interval.end);
    }
    
    std::cout << "Evaluation range: [" << global_min << ", " << global_max << "]\n";
    
    // Group coverage analysis
    std::map<int, std::pair<double, double>> group_ranges; // group ID -> (min x, max x)
    std::map<int, size_t> group_point_counts; // group ID -> sample count
    std::map<int, double> group_total_errors; // group ID -> total error
    std::map<int, double> group_max_errors; // group ID -> max error
    
    // Special handling for orphan groups to ensure correct range recording
    for (const auto& group : groups) {
        if (group.storage_type == ORPHAN_GROUP) {
            double min_x = std::numeric_limits<double>::max();
            double max_x = std::numeric_limits<double>::lowest();
            
            for (const auto& de : group.delta_encodings) {
                if (de.is_padding) continue;
                
                // Use precise boundaries from original_interval for orphan groups
                double interval_start = de.delta_start;  // Already absolute position
                double interval_end = de.original_interval.end;  // Use exact end point
                
                min_x = std::min(min_x, interval_start);
                max_x = std::max(max_x, interval_end);
            }
            
            if (min_x <= max_x) {
                group_ranges[group.id] = {min_x, max_x};
                group_point_counts[group.id] = 0;
                group_total_errors[group.id] = 0.0;
                group_max_errors[group.id] = 0.0;
            }
        }
    }
    
    // Uniform sampling for evaluation
    const size_t total_samples = 1000;
    double step = (global_max - global_min) / total_samples;
    
    std::cout << "Using " << total_samples << " sample points, step size: " << step << "\n";
    
    // Perform sampling
    for (size_t i = 0; i <= total_samples; i++) {
        double sample_x = global_min + i * step;
        if (sample_x > global_max) sample_x = global_max;
        
        // Calculate true function value
        x = sample_x;
        double true_value = expression.value();
        
        // Calculate estimated value using hardware simulation
        double hw_value = simulateHardwareLookup(sample_x, groups, scale_factor);
        
        // Calculate and record error
        total_points++;
        
        // Find corresponding group
        int matching_group_id = -1;
        for (const auto& group : groups) {
            // For normal groups
            if (group.storage_type != ORPHAN_GROUP) {
                double min_start = std::numeric_limits<double>::max();
                double max_end = std::numeric_limits<double>::lowest();
                
                for (const auto& de : group.delta_encodings) {
                    if (de.is_padding) continue;
                    
                    // Use absolute start position
                    double interval_start = de.delta_start;  // Now storing absolute position
                    double interval_end = interval_start + group.length;
                    
                    min_start = std::min(min_start, interval_start);
                    max_end = std::max(max_end, interval_end);
                }
                
                if (sample_x >= min_start && sample_x <= max_end) {
                    matching_group_id = group.id;
                    
                    // Add or update group range
                    if (group_ranges.find(group.id) == group_ranges.end()) {
                        group_ranges[group.id] = {min_start, max_end};
                    } else {
                        group_ranges[group.id].first = std::min(group_ranges[group.id].first, min_start);
                        group_ranges[group.id].second = std::max(group_ranges[group.id].second, max_end);
                    }
                    
                    break;
                }
            } 
            // For orphan groups
            else {
                bool found = false;
                for (const auto& de : group.delta_encodings) {
                    if (de.is_padding) continue;
                    
                    // Use precise boundaries from original_interval for orphan groups
                    double interval_start = de.delta_start;  // Already absolute position
                    double interval_end = de.original_interval.end;  // Use exact end point
                    
                    if (sample_x >= interval_start && sample_x <= interval_end) {
                        matching_group_id = group.id;
                        found = true;
                        break;
                    }
                }
                
                if (found) break;
            }
        }
        
        // Only process valid results
        if (!std::isnan(true_value) && !std::isnan(hw_value) && 
            std::isfinite(true_value) && std::isfinite(hw_value)) {
            valid_points++;
            
            double error = std::abs(true_value - hw_value);
            double rel_error = std::abs(true_value) > 1e-10 ? error / std::abs(true_value) : error;
            
            // Add to overall statistics
            total_error += error;
            
            // Add to group statistics
            if (matching_group_id >= 0) {
                if (group_point_counts.find(matching_group_id) == group_point_counts.end()) {
                    group_point_counts[matching_group_id] = 0;
                    group_total_errors[matching_group_id] = 0.0;
                    group_max_errors[matching_group_id] = 0.0;
                }
                
                group_point_counts[matching_group_id]++;
                group_total_errors[matching_group_id] += error;
                
                if (error > group_max_errors[matching_group_id]) {
                    group_max_errors[matching_group_id] = error;
                }
            }
            
            // Update max error
            if (error > local_max_error) {
                local_max_error = error;
            }
            
            // Collect high error points (>0.1%)
            if (rel_error > 0.001) {
                high_error_points.push_back(std::make_tuple(
                    sample_x, true_value, hw_value, error, matching_group_id));
                
                // Add to group high error details
                if (matching_group_id >= 0) {
                    high_error_details[matching_group_id].push_back(
                        std::make_tuple(sample_x, true_value, error));
                }
            }
        }
    }
    
    // Update max_error pointer
    if (max_error != nullptr) {
        *max_error = local_max_error;
    }
    
    // Calculate average error for each group and fill results
    for (const auto& [group_id, points] : group_point_counts) {
        if (points > 0) {
            double avg_error = group_total_errors[group_id] / points;
            double max_error = group_max_errors[group_id];
            group_errors.push_back(std::make_tuple(group_id, avg_error, max_error, points));
        }
    }
    
    // Calculate overall average error
    double avg_error = valid_points > 0 ? total_error / valid_points : 0.0;
    
    // Output detailed error analysis
    std::cout << "\n===== Detailed Error Analysis =====\n";
    std::cout << "Analysis range: [" << global_min << ", " << global_max << "]\n";
    std::cout << "Sample points: " << valid_points << "\n";
    std::cout << "Overall average error: " << avg_error << "\n";
    std::cout << "Overall maximum error: " << local_max_error << "\n";
    
    // Sort groups by error (high to low)
    std::sort(group_errors.begin(), group_errors.end(), 
          [](const auto& a, const auto& b) { 
              return std::get<1>(a) > std::get<1>(b); 
          });
    
    // Output high error points
    if (!high_error_points.empty()) {
        std::cout << "\nHigh error points (>0.1%):\n";
        std::cout << "X value,True value,Estimated value,Absolute error\n";
        
        // Show up to 10 high error points
        size_t num_to_show = std::min(high_error_points.size(), size_t(10));
        for (size_t i = 0; i < num_to_show; i++) {
            const auto& [x_val, true_val, hw_val, err, group_id] = high_error_points[i];
            std::cout << std::fixed << std::setprecision(6) 
                      << x_val << "," << true_val << "," << hw_val << "," << err << "\n";
        }
        
        if (high_error_points.size() > 10) {
            std::cout << "... " << (high_error_points.size() - 10) << " more high error points not shown\n";
        }
    }
    
    // Output group error statistics
    if (!group_errors.empty()) {
        std::cout << "\nGroup error statistics:\n";
        for (const auto& [group_id, avg_error, max_error, points] : group_errors) {
            std::cout << "Group " << group_id << " errors: avg=" << avg_error 
                      << ", max=" << max_error << " (contains " << points << " points)\n";
            
            // If group error exceeds target, add warning and try to resolve
            if (avg_error > acceptable_error) {
                std::cout << "Warning: Max average error " << avg_error 
                          << " exceeds target " << acceptable_error << ". Adjusting encoding parameters...\n";
                
                // Find this group
                for (const auto& group : groups) {
                    if (group.id == static_cast<size_t>(group_id)) {
                        double max_slope_delta = 0.0;
                        double max_intercept_delta = 0.0;
                        for (const auto& de : group.delta_encodings) {
                            if (de.is_padding) continue;
                            max_slope_delta = std::max(max_slope_delta, std::abs(de.delta_slope));
                            max_intercept_delta = std::max(max_intercept_delta, std::abs(de.delta_intercept));
                        }
                        std::cout << "Group parameter analysis: Max slope delta: " << max_slope_delta << "\n";
                        std::cout << "Target error: " << parameter_error_target << "\n";
                        double max_delta = max_slope_delta;
                        // Choose more appropriate bitwidth for slope
                        int optimal_bits = 16;
                        double optimal_scale = 65536.0;
                        double quant_error = max_delta / optimal_scale;
                        
                        std::cout << "Testing bitwidth: " << optimal_bits 
                                  << ", scale factor: " << optimal_scale 
                                  << ", quantization error: " << quant_error << "\n";
                        
                        // Try different bitwidths
                        for (int bits = 16; bits <= 22; bits++) {
                            double scale = (1 << bits);
                            double error = max_delta / scale;
                            std::cout << "Testing bitwidth: " << bits 
                                      << ", scale factor: " << scale 
                                      << ", quantization error: " << error << "\n";
                            
                            if (error < parameter_error_target) {
                                optimal_bits = bits;
                                optimal_scale = scale;
                                quant_error = error;
                                break;
                            }
                        }
                        
                        std::cout << "Selected optimal bitwidth: " << optimal_bits 
                                  << ", final scale factor: " << optimal_scale << "\n";
                        
                        // Same for intercept
                        std::cout << "Max intercept delta: " << max_intercept_delta << "\n";
                        std::cout << "Target error: " << parameter_error_target << "\n";
                        
                        max_delta = max_intercept_delta;
                        int optimal_intercept_bits = 16;
                        double optimal_intercept_scale = 65536.0;
                        
                        // Try different bitwidths
                        for (int bits = 11; bits <= 16; bits++) {
                            double scale = (1 << bits);
                            double error = max_delta / scale;
                            std::cout << "Testing bitwidth: " << bits 
                                      << ", scale factor: " << scale 
                                      << ", quantization error: " << error << "\n";
                            
                            if (error < parameter_error_target) {
                                optimal_intercept_bits = bits;
                                optimal_intercept_scale = scale;
                                break;
                            }
                        }
                        
                        std::cout << "Selected optimal bitwidth: " << optimal_intercept_bits 
                                  << ", final scale factor: " << optimal_intercept_scale << "\n";
                        
                        break;
                    }
                }
            }
        }
    }
    
    // Output precision analysis
    std::cout << "\nPrecision analysis:\n";
    std::cout << "Current precision: " << precision_bits << " bits (scale_factor = " << scale_factor << ")\n";
    
    // Estimate required precision
    int suggested_bits = 10;
    if (local_max_error > 0) {
        suggested_bits = static_cast<int>(std::ceil(std::log2(1.0 / local_max_error)));
        if (suggested_bits < 0) suggested_bits = 0;
        if (suggested_bits > 30) suggested_bits = 30; // Limit max bitwidth
    }
    
    std::cout << "Estimated minimum required precision: " << suggested_bits << " bits (scale_factor ≈ " 
              << (1 << suggested_bits) << ")\n";
    
    // Analyze currently used scale_factor
    if (!groups.empty()) {
        double avg_scale = 0.0;
        int count = 0;
        for (const auto& group : groups) {
            if (group.primary_scale_factor > 0) {
                avg_scale += group.primary_scale_factor;
                count++;
            }
        }
        
        if (count > 0) {
            avg_scale /= count;
            int current_bits = static_cast<int>(std::log2(avg_scale));
            
            std::cout << "Currently used average primary_scale_factor: " << avg_scale 
                      << " (" << current_bits << " bits)\n";
            
            if (current_bits < suggested_bits) {
                std::cout << "Warning: Current precision may be insufficient! Recommend increasing to at least " 
                          << suggested_bits << " bits\n";
            } else {
                std::cout << "Current precision should be sufficient. Errors may come from interval partitioning or linear approximation itself.\n";
            }
        }
    }
    
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

inline double max_abs(const std::vector<double>& vec) {
    if (vec.empty()) return 0.0;
    auto max_it = std::max_element(vec.begin(), vec.end(),
                                   [](double a, double b) { return std::abs(a) < std::abs(b); });
    return std::abs(*max_it);
}
void groupAndCompressIntervals(const std::string& expression_str,
                             const std::vector<Interval>& intervals,
                             const std::vector<FitParameters>& params,
                             std::vector<IntervalGroup>& groups,
                             double acceptable_error) {
    
    std::cout << "\n===== Interval Grouping and Compression (Function: " << expression_str << ") =====\n";
    
    // Save full domain range for debugging
    double full_domain_min = std::numeric_limits<double>::max();
    double full_domain_max = std::numeric_limits<double>::lowest();
    for (const auto& interval : intervals) {
        full_domain_min = std::min(full_domain_min, interval.start);
        full_domain_max = std::max(full_domain_max, interval.end);
    }
    std::cout << "Initial full domain range: [" << full_domain_min << ", " << full_domain_max << "]\n";
    
    // Scale parameter error target based on acceptable function error
    double param_error_target = acceptable_error * 0.01; // 1% of acceptable error
    std::cout << "Parameter error target: " << param_error_target << " (1/100 of function error target: " 
              << acceptable_error << ")\n";
    
    // Use default length_tolerance
    double length_tolerance = 1e-5;
    std::cout << "Using length tolerance: " << length_tolerance << "\n";
    
    // Clear existing groups
    groups.clear();
    
    // Group intervals by length
    std::map<double, std::vector<size_t>> length_groups;
    
    for (size_t i = 0; i < intervals.size(); i++) {
        double length = intervals[i].end - intervals[i].start;
        
        // Look for closest length group
        bool found_match = false;
        
        for (auto& [group_length, indices] : length_groups) {
            if (std::abs(length - group_length) < length_tolerance) {
                indices.push_back(i);
                found_match = true;
                break;
            }
        }
        
        if (!found_match) {
            length_groups[length] = {i};
        }
    }
    
    // Print initial grouping results
    std::cout << "\nInitial grouping results:\n";
    int group_id = 0;
    for (const auto& [length, indices] : length_groups) {
        std::cout << "Group " << group_id << ": " << indices.size() << " intervals, length: ";
        for (size_t i = 0; i < std::min(indices.size(), size_t(5)); i++) {
            size_t idx = indices[i];
            double actual_length = intervals[idx].end - intervals[idx].start;
            std::cout << actual_length << " ";
        }
        if (indices.size() > 5) {
            std::cout << "... (" << (indices.size() - 5) << " more)";
        }
        std::cout << "\n";
        group_id++;
    }
    
    // Create formal groups, mark small groups as orphans
    std::vector<size_t> orphans;
    int next_group_id = 0;

    for (auto& [length, indices] : length_groups) {
        if (indices.size() < 3) {
            std::cout << "Group " << next_group_id << " has only " << indices.size() 
                    << " intervals, marking as orphan group\n";
            for (size_t idx : indices) {
                orphans.push_back(idx);
            }
            next_group_id++;
            continue;
        }
        
        // Create standard group
        std::cout << "\nProcessing Group " << next_group_id << ":\n";
        
        IntervalGroup group;
        group.id = next_group_id++;
        group.storage_type = NORMAL_GROUP;
        
        // Use first interval as baseline (only for parameter baseline, not for position)
        size_t base_idx = indices[0];
        group.base_interval = intervals[base_idx];
        group.base_params = params[base_idx];
        group.length = intervals[base_idx].end - intervals[base_idx].start;
        
        std::cout << "  Base interval: [" << group.base_interval.start << ", " 
                << group.base_interval.end << "], length=" << group.length << "\n";
        std::cout << "  Base parameters: b=" << group.base_params.b << ", c=" 
                << group.base_params.c << "\n";
        
        // Find max slope and intercept deltas for quantization bitwidth
        double max_delta_slope = 0.0;
        double max_delta_intercept = 0.0;
        double avg_delta_slope = 0.0;
        double avg_delta_intercept = 0.0;
        
        for (size_t i = 0; i < indices.size(); i++) {
            size_t idx = indices[i];
            
            // Use absolute start position
            double absolute_start = intervals[idx].start;
            
            // Calculate parameter deltas (still store deltas for normal groups)
            double delta_slope = params[idx].b - group.base_params.b;
            double delta_intercept = params[idx].c - group.base_params.c;
            
            // Track max delta values
            max_delta_slope = std::max(max_delta_slope, std::abs(delta_slope));
            max_delta_intercept = std::max(max_delta_intercept, std::abs(delta_intercept));
            avg_delta_slope += std::abs(delta_slope);
            avg_delta_intercept += std::abs(delta_intercept);
            
            // Create delta encoding with absolute start position
            DeltaEncoding de(absolute_start, false, false, delta_intercept, delta_slope, idx, false);
            
            // Store original interval information for precise boundaries
            de.original_interval = intervals[idx];
            de.original_params = params[idx];
            
            group.delta_encodings.push_back(de);
            
            // Check and log gaps in coverage (using gap info rather than just detecting)
            if (i > 0) {
                size_t prev_idx = indices[i-1];
                if (intervals[prev_idx].end < intervals[idx].start) {
                    // Use the gap information instead of just detecting it
                    double gap_size = intervals[idx].start - intervals[prev_idx].end;
                    std::cout << "  Gap detected between intervals: " << gap_size 
                              << " units from " << intervals[prev_idx].end 
                              << " to " << intervals[idx].start << "\n";
                }
            }
        }
        
        // Calculate average delta values
        avg_delta_slope /= indices.size();
        avg_delta_intercept /= indices.size();
        
        std::cout << "  Parameter delta statistics:\n";
        std::cout << "    Slope (b): max=" << max_delta_slope << ", avg=" << avg_delta_slope << "\n";
        std::cout << "    Intercept (c): max=" << max_delta_intercept << ", avg=" 
                << avg_delta_intercept << "\n";
        
        // Determine required bitwidth based on error evaluation
        std::cout << "  Max absolute delta - slope: " << max_delta_slope 
                  << ", intercept: " << max_delta_intercept << "\n";
        std::cout << "  Function error target: " << acceptable_error << "\n";
        std::cout << "  Parameter error target: " << param_error_target << "\n";
        
        // Start with a reasonable bitwidth
        int slope_bits_min = 8;
        int slope_bits_max = 21;
        int slope_bits = 14; // Start with a middle value
        
        double best_function_error = std::numeric_limits<double>::max();
        int best_slope_bits = slope_bits;
        
        // Test different bitwidths and measure actual function approximation error
        std::cout << "  Optimizing slope bitwidth based on function approximation error:\n";
        std::cout << "  | Bits | Scale Factor | Param Error  | Function Error | Status |\n";
        std::cout << "  |------|-------------|--------------|----------------|--------|\n";
        
        // Define a function to evaluate approximation error with given bitwidth
        auto evaluateBitwidth = [&](int bits, bool is_slope) -> double {
            double scale = (1 << bits);
            
            // Build temporary group with given bitwidth for testing
            IntervalGroup test_group = group;
            if (is_slope) {
                test_group.bitwidth_slope = bits;
                test_group.slope_scale_factor = scale;
            } else {
                test_group.bitwidth_intercept = bits;
                test_group.intercept_scale_factor = scale;
            }
            
            // Measure max parameter quantization error
            double max_param_error = 0.0;
            double max_func_error = 0.0;
            
            // Create a sparse set of test points across all intervals
            const int test_points_per_interval = 5;
            
            for (const auto& de : test_group.delta_encodings) {
                if (de.is_padding) continue;
                
                const auto& interval = de.original_interval;
                const auto& orig_param = de.original_params;
                
                // Calculate delta values
                double delta_b = orig_param.b - test_group.base_params.b;
                double delta_c = orig_param.c - test_group.base_params.c;
                
                // Quantize based on which parameter we're testing
                double quantized_delta;
                if (is_slope) {
                    int32_t quant = static_cast<int32_t>(delta_b * scale);
                    quantized_delta = static_cast<double>(quant) / scale;
                    max_param_error = std::max(max_param_error, std::abs(delta_b - quantized_delta));
                } else {
                    int32_t quant = static_cast<int32_t>(delta_c * scale);
                    quantized_delta = static_cast<double>(quant) / scale;
                    max_param_error = std::max(max_param_error, std::abs(delta_c - quantized_delta));
                }
                
                // Test function approximation at several points
                for (int i = 0; i <= test_points_per_interval; i++) {
                    double t = i / static_cast<double>(test_points_per_interval);
                    double x = interval.start + t * (interval.end - interval.start);
                    
                    // Original function value
                    double y_original = orig_param.b * x + orig_param.c;
                    
                    // Reconstructed with quantized parameters
                    double b_recon = test_group.base_params.b + (is_slope ? quantized_delta : delta_b);
                    double c_recon = test_group.base_params.c + (is_slope ? delta_c : quantized_delta);
                    double y_approx = b_recon * x + c_recon;
                    
                    max_func_error = std::max(max_func_error, std::abs(y_original - y_approx));
                }
            }
            
            return max_func_error;
        };
        
        // Use binary search to find optimal slope bitwidth
        while (slope_bits_max - slope_bits_min > 1) {
            slope_bits = (slope_bits_min + slope_bits_max) / 2;
            double scale = (1 << slope_bits);
            
            // Raw parameter error (without considering function)
            double param_error = max_delta_slope / scale;
            
            // Function approximation error with this bitwidth
            double func_error = evaluateBitwidth(slope_bits, true);
            
            std::string status = func_error <= acceptable_error ? "PASS" : "FAIL";
            
            std::cout << "  | " << std::setw(4) << slope_bits << " | " 
                      << std::scientific << std::setprecision(2) << scale << " | "
                      << param_error << " | " 
                      << func_error << " | " << status << " |\n";
            
            if (func_error < best_function_error) {
                best_function_error = func_error;
                best_slope_bits = slope_bits;
            }
            
            if (func_error <= acceptable_error) {
                // This bitwidth is sufficient, try with fewer bits
                slope_bits_max = slope_bits;
            } else {
                // This bitwidth is insufficient, try with more bits
                slope_bits_min = slope_bits;
            }
        }
        
        // Use one more bit if we're just below the acceptable error
        if (best_function_error > acceptable_error * 0.9 && best_slope_bits < 20) {
            best_slope_bits++;
        }
        
        // Set optimal slope bitwidth
        slope_bits = best_slope_bits;
        double slope_scale_factor = (1 << slope_bits);
        
        std::cout << "  Selected optimal slope bitwidth: " << slope_bits 
                << ", scale factor: " << slope_scale_factor << "\n";
        
        // Now optimize intercept bitwidth 
        int intercept_bits_min = 8;
        int intercept_bits_max = 21;
        int intercept_bits = 14; // Start with a middle value
        
        best_function_error = std::numeric_limits<double>::max();
        int best_intercept_bits = intercept_bits;
        
        // Use binary search for intercept bitwidth
        std::cout << "\n  Optimizing intercept bitwidth based on function approximation error:\n";
        std::cout << "  | Bits | Scale Factor | Param Error  | Function Error | Status |\n";
        std::cout << "  |------|-------------|--------------|----------------|--------|\n";
        
        while (intercept_bits_max - intercept_bits_min > 1) {
            intercept_bits = (intercept_bits_min + intercept_bits_max) / 2;
            double scale = (1 << intercept_bits);
            
            // Parameter error
            double param_error = max_delta_intercept / scale;
            
            // Function error with this bitwidth
            double func_error = evaluateBitwidth(intercept_bits, false);
            
            std::string status = func_error <= acceptable_error ? "PASS" : "FAIL";
            
            std::cout << "  | " << std::setw(4) << intercept_bits << " | " 
                      << std::scientific << std::setprecision(2) << scale << " | "
                      << param_error << " | " 
                      << func_error << " | " << status << " |\n";
            
            if (func_error < best_function_error) {
                best_function_error = func_error;
                best_intercept_bits = intercept_bits;
            }
            
            if (func_error <= acceptable_error) {
                // This bitwidth is sufficient, try with fewer bits
                intercept_bits_max = intercept_bits;
            } else {
                // This bitwidth is insufficient, try with more bits
                intercept_bits_min = intercept_bits;
            }
        }
        
        // Use one more bit if we're just below the acceptable error
        if (best_function_error > acceptable_error * 0.9 && best_intercept_bits < 20) {
            best_intercept_bits++;
        }
        
        // Set optimal intercept bitwidth
        intercept_bits = best_intercept_bits;
        double intercept_scale_factor = (1 << intercept_bits);
        
        std::cout << "  Selected optimal intercept bitwidth: " << intercept_bits 
                << ", scale factor: " << intercept_scale_factor << "\n";
        
        // Set group quantization parameters
        group.bitwidth_slope = slope_bits;
        group.bitwidth_intercept = intercept_bits;
        group.slope_scale_factor = slope_scale_factor;
        group.intercept_scale_factor = intercept_scale_factor;
        group.primary_scale_factor = std::max(slope_scale_factor, intercept_scale_factor);
        
        std::cout << "  Final selected bitwidths: slope=" << slope_bits << " bits, intercept=" 
                << intercept_bits << " bits\n";
        std::cout << "  Scale factors: slope=" << slope_scale_factor 
                << ", intercept=" << intercept_scale_factor << "\n\n";
        
        // Print parameter quantization analysis
        std::cout << "  Parameter quantization analysis:\n";
        std::cout << "  | Index | Original Slope | Quantized Slope | Slope Error   | "
                << "Original Intercept | Quantized Intercept | Intercept Error |\n";
        std::cout << "  |-------|---------------|----------------|---------------|"
                << "-------------------|-------------------|----------------|\n";
        
        for (size_t i = 0; i < std::min(size_t(5), group.delta_encodings.size()); i++) {
            const auto& de = group.delta_encodings[i];
            size_t idx = de.original_index;
            
            // Original values
            double orig_b = params[idx].b;
            double orig_c = params[idx].c;
            
            // Calculate deltas
            double delta_b = orig_b - group.base_params.b;
            double delta_c = orig_c - group.base_params.c;
            
            // Quantized deltas
            int32_t quant_delta_b = static_cast<int32_t>(delta_b * slope_scale_factor);
            int32_t quant_delta_c = static_cast<int32_t>(delta_c * intercept_scale_factor);
            
            // Dequantized
            double dequant_delta_b = static_cast<double>(quant_delta_b) / slope_scale_factor;
            double dequant_delta_c = static_cast<double>(quant_delta_c) / intercept_scale_factor;
            
            // Reconstructed values
            double recon_b = group.base_params.b + dequant_delta_b;
            double recon_c = group.base_params.c + dequant_delta_c;
            
            // Errors
            double b_error = std::abs(orig_b - recon_b);
            double c_error = std::abs(orig_c - recon_c);
            
            // Output table row
            std::cout << "  | " << std::setw(5) << idx << " | " 
                    << std::scientific << std::setprecision(4) << orig_b << " | " 
                    << recon_b << " | " << b_error << " | " 
                    << orig_c << " | " << recon_c << " | " << c_error << " |\n";
        }
        
        // Hardware optimization: ensure interval count is multiple of 8
        size_t target_size = ((group.delta_encodings.size() + 7) / 8) * 8;
        
        if (target_size > group.delta_encodings.size()) {
            size_t padding_count = target_size - group.delta_encodings.size();
            std::cout << "  Padding group from " << group.delta_encodings.size() 
                    << " to " << target_size << " intervals for hardware optimization\n";
            
            // Add padding
            for (size_t i = 0; i < padding_count; i++) {
                DeltaEncoding padding(0.0, false, false, 0.0, 0.0, 0, true);
                group.delta_encodings.push_back(padding);
            }
        }
        
        groups.push_back(group);
    }

    // Process orphan groups
    if (!orphans.empty()) {
        std::cout << "\nProcessing Orphan Group:\n";
        
        // Create unified orphan group
        IntervalGroup orphan_group;
        orphan_group.id = next_group_id++;
        orphan_group.storage_type = ORPHAN_GROUP;
        
        // Find min/max range for all orphan intervals
        double range_min = std::numeric_limits<double>::max();
        double range_max = std::numeric_limits<double>::lowest();
        
        for (size_t idx : orphans) {
            range_min = std::min(range_min, intervals[idx].start);
            range_max = std::max(range_max, intervals[idx].end);
        }
        
        std::cout << "  Range: [" << range_min << ", " << range_max << "]\n";
        std::cout << "  Contains " << orphans.size() << " intervals\n";
        
        // Find smallest interval for reference and use it
        double min_length = std::numeric_limits<double>::max();
        size_t smallest_interval_idx = 0;
        
        for (size_t i = 0; i < orphans.size(); i++) {
            size_t idx = orphans[i];
            const Interval& interval = intervals[idx];
            double length = interval.end - interval.start;
            
            if (length < min_length) {
                min_length = length;
                smallest_interval_idx = idx;
            }
        }
        
        // Set group length to smallest interval length and use it for reference
        orphan_group.length = min_length;
        std::cout << "  Using smallest interval (idx=" << smallest_interval_idx 
                  << ") with length " << min_length << " as reference\n";
        
        // For orphan group, base_params has no real meaning, set to 0
        orphan_group.base_params.b = 0.0;
        orphan_group.base_params.c = 0.0;
        
        // Find best bitwidth for orphans based on function approximation error
        int orphan_bits = 16; // Start with 16 bits
        double orphan_scale_factor = 1 << orphan_bits;
        
        // Create delta_encoding for each orphan interval
        for (size_t i = 0; i < orphans.size(); i++) {
            size_t idx = orphans[i];
            const Interval& interval = intervals[idx];
            const FitParameters& param = params[idx];
            
            if (i < 5) {
                std::cout << "  Orphan interval " << i << ": [" << interval.start << ", " 
                        << interval.end << "], b=" << param.b << ", c=" << param.c << "\n";
            }
            
            // For orphan group, store absolute parameters and position
            DeltaEncoding de(
                interval.start,  // absolute start position
                false,           // is_x_reflected
                false,           // is_y_reflected
                param.c,         // absolute intercept - note parameter order
                param.b,         // absolute slope
                idx,             // original_index
                false            // is_padding
            );
            
            // Store original interval for precise boundaries
            de.original_interval = interval;
            de.original_params = param;
            
            orphan_group.delta_encodings.push_back(de);
        }
        
        if (orphans.size() > 5) {
            std::cout << "  ... and " << (orphans.size() - 5) << " more orphan intervals\n";
        }
        
        // For orphans, test different bitwidths
        std::cout << "\n  Optimizing orphan group bitwidth based on function approximation:\n";
        
        // Find max parameter values for orphans
        double max_slope = 0.0;
        double max_intercept = 0.0;
        for (const auto& de : orphan_group.delta_encodings) {
            max_slope = std::max(max_slope, std::abs(de.delta_slope));
            max_intercept = std::max(max_intercept, std::abs(de.delta_intercept));
        }
        
        // Test different bitwidths for orphans
        for (int bits = 12; bits <= 20; bits += 2) {
            double scale = 1 << bits;
            double slope_error = max_slope / scale;
            double intercept_error = max_intercept / scale;
            
            // Calculate function approximation error with this bitwidth
            double max_func_error = 0.0;
            
            for (const auto& de : orphan_group.delta_encodings) {
                if (de.is_padding) continue;
                
                const auto& interval = de.original_interval;
                const auto& orig_param = de.original_params;
                
                // Test several points in the interval
                for (int i = 0; i <= 5; i++) {
                    double t = i / 5.0;
                    double x = interval.start + t * (interval.end - interval.start);
                    
                    // Original function value
                    double y_original = orig_param.b * x + orig_param.c;
                    
                    // Quantized parameters
                    int32_t quant_b = static_cast<int32_t>(orig_param.b * scale);
                    int32_t quant_c = static_cast<int32_t>(orig_param.c * scale);
                    double b_quant = static_cast<double>(quant_b) / scale;
                    double c_quant = static_cast<double>(quant_c) / scale;
                    
                    // Approximated value
                    double y_approx = b_quant * x + c_quant;
                    max_func_error = std::max(max_func_error, std::abs(y_original - y_approx));
                }
            }
            
            std::string status = max_func_error <= acceptable_error ? "PASS" : "FAIL";
            
            std::cout << "  Bits: " << bits << ", Scale: " << scale 
                      << ", Param errors (slope: " << slope_error 
                      << ", intercept: " << intercept_error << ")"
                      << ", Function error: " << max_func_error 
                      << " -> " << status << "\n";
            
            if (max_func_error <= acceptable_error) {
                // Found sufficient bitwidth
                orphan_bits = bits;
                orphan_scale_factor = scale;
                break;
            }
        }
        
        // Set orphan group quantization parameters
        orphan_group.slope_scale_factor = orphan_scale_factor;  
        orphan_group.intercept_scale_factor = orphan_scale_factor;
        orphan_group.primary_scale_factor = orphan_scale_factor;
        orphan_group.bitwidth_slope = orphan_bits;
        orphan_group.bitwidth_intercept = orphan_bits;
        
        std::cout << "  Selected bitwidth for orphan group: " << orphan_bits 
                  << " bits (scale factor: " << orphan_scale_factor << ")\n";
        
        // Hardware optimization: ensure interval count is multiple of 8
        size_t target_size = ((orphan_group.delta_encodings.size() + 7) / 8) * 8;
        
        if (target_size > orphan_group.delta_encodings.size()) {
            size_t padding_count = target_size - orphan_group.delta_encodings.size();
            std::cout << "  Padding orphan group from " << orphan_group.delta_encodings.size() 
                    << " to " << target_size << " intervals for hardware optimization\n";
            
            // Add padding
            for (size_t i = 0; i < padding_count; i++) {
                DeltaEncoding padding(0.0, false, false, 0.0, 0.0, 0, true);
                orphan_group.delta_encodings.push_back(padding);
            }
        }
        
        groups.push_back(orphan_group);
    }
}

void generateSimulationVectors(const std::string& expression_str,
                             const std::string& directory,
                             const std::string& cleanName,
                             double start, double end,
                             int hw_scale_factor, int hw_frac_bits,
                             size_t num_vectors = 100) {
    
    // Create directory structure
    std::string test_dir = directory + "/sim/test_vectors";
    createDirectory(directory + "/sim");
    createDirectory(test_dir);
    
    // Create test vector files
    std::string vectors_filename = test_dir + "/" + cleanName + "_vectors.txt";
    std::ofstream vectors_file(vectors_filename);
    
    if (!vectors_file.is_open()) {
        std::cerr << "Failed to open test vectors file: " << vectors_filename << std::endl;
        return;
    }

    // Print hardware implementation parameters
    std::cout << "Generating " << num_vectors << " test vectors:\n";
    std::cout << "- Scale factor: " << hw_scale_factor << " (2^" << hw_frac_bits << ")\n";
    std::cout << "- Input range: [" << start << ", " << end << "]\n";
    
    // Calculate function output range
    double func_min = std::numeric_limits<double>::max();
    double func_max = std::numeric_limits<double>::lowest();
    
    for (double x = start; x <= end; x += (end-start)/100) {
        double y = computeFunctionValue(expression_str, x);
        func_min = std::min(func_min, y);
        func_max = std::max(func_max, y);
    }
    
    std::cout << "- Function output range: [" << func_min << ", " << func_max << "]\n";
    
    // CSV file header
    std::string csv_filename = test_dir + "/" + cleanName + "_vectors.csv";
    std::ofstream csv_file(csv_filename);
    if (csv_file.is_open()) {
        csv_file << "Input,Expected\n";
    }
    
    // Generate uniformly distributed test points
    double step = (end - start) / (num_vectors - 1);
    
    for (size_t i = 0; i < num_vectors; ++i) {
        // Calculate input value
        double x = start + i * step;
        
        // Calculate function value
        double y = computeFunctionValue(expression_str, x);
        
        // Convert to fixed-point representation for display only
        int32_t fixed_x = static_cast<int32_t>(std::round(x * hw_scale_factor));
        int32_t fixed_y = static_cast<int32_t>(std::round(y * hw_scale_factor));
        
        // Write test vector
        vectors_file << std::hex << std::setfill('0') 
                    << std::setw(4) << (fixed_x & 0xFFFF) 
                    << std::setw(4) << (fixed_y & 0xFFFF)
                    << "\n";
        
        // Write CSV - include actual floating point values for verification
        if (csv_file.is_open()) {
            csv_file << std::fixed << std::setprecision(6) << std::dec 
                    << x << "," << y << "\n";
        }
        
        // Print first 10 samples and the last one
        if (i < 10 || i == num_vectors-1) {
            std::cout << "[" << std::dec << i << "] x=" << std::fixed << std::setprecision(6) << x 
                      << " → y=" << y 
                      << " (0x" << std::hex << std::setw(4) << (fixed_x & 0xFFFF) << " → 0x" 
                      << std::setw(4) << (fixed_y & 0xFFFF) << ")"
                      << "\n";
        }
    }
    
    vectors_file.close();
    if (csv_file.is_open()) csv_file.close();
    
    std::cout << "Test vectors saved to: " << vectors_filename << "\n";
    std::cout << "CSV file saved to: " << csv_filename << "\n";
}

double simulateHardwareLookup(
    double x, 
    const std::vector<IntervalGroup>& groups,
    double scale_factor,
    double target_error,
    double true_value) {
    
    // Debug output for first 15 inputs only
    static int debug_counter = 0;
    bool debug_output = (debug_counter < 15);
    
    if (debug_output) {
        std::cout << "\n===== Hardware Simulation (x = " << x << ") =====\n";
        if (!std::isnan(true_value)) {
            std::cout << "True function value: " << true_value << std::endl;
        }
    }
    
    // Search through all groups to find which one contains x
    for (size_t group_idx = 0; group_idx < groups.size(); group_idx++) {
        const auto& group = groups[group_idx];
        
        // Skip empty groups
        if (group.delta_encodings.empty()) continue;
        
        // For normal groups, check if x is within the group's range
        if (group.storage_type == NORMAL_GROUP) {
            double group_min = group.base_interval.start;
            double group_max = group.base_interval.end;
            
            // If x is inside this group's range
            if (x >= group_min && x <= group_max) {
                if (debug_output) {
                    std::cout << "Matched group[" << group_idx << "] (ID=" << group.id << ")\n";
                    std::cout << "  Group range: [" << group_min << ", " << group_max << "]\n";
                }
                
                // Calculate interval index using consistent method
                int interval_idx;
                double relative_position = (x - group_min) / (group_max - group_min);
                interval_idx = static_cast<int>(relative_position * group.delta_encodings.size());
                
                // Safety check - limit to valid range
                if (static_cast<size_t>(interval_idx) >= group.delta_encodings.size()) {
                    interval_idx = static_cast<int>(group.delta_encodings.size() - 1);
                }
                
                if (debug_output) {
                    std::cout << "  Interval index: " << interval_idx << " (of " << group.delta_encodings.size() << ")\n";
                }
                
                // Get the delta parameters for this interval
                const auto& delta = group.delta_encodings[interval_idx];
                
                // Calculate interval boundaries for reflection
                double interval_start = group_min + (delta.delta_start / scale_factor);
                double interval_end;
                double interval_width;
                
                if (static_cast<size_t>(interval_idx) < group.delta_encodings.size() - 1) {
                    // For intermediate intervals
                    const auto& next_delta = group.delta_encodings[interval_idx + 1];
                    double next_start = group_min + (next_delta.delta_start / scale_factor);
                    interval_end = next_start;
                } else {
                    // For the last interval
                    interval_end = group_max;
                }
                interval_width = interval_end - interval_start;
                
                if (debug_output) {
                    std::cout << "  Interval boundaries: [" << interval_start << ", " << interval_end << "]\n";
                }
                
                // Apply X reflection if needed
                double adjusted_x = x;
                if (delta.is_x_reflected) {
                    double midpoint = interval_start + (interval_width / 2.0);
                    adjusted_x = 2.0 * midpoint - x;
                    
                    if (debug_output) {
                        std::cout << "  X reflection applied: midpoint=" << midpoint << ", adjusted_x=" << adjusted_x << "\n";
                    }
                }
                
                // Start with base parameters
                double b = group.base_params.b;
                double c = group.base_params.c;
                
                // Apply delta values directly
                double delta_b = delta.delta_slope / scale_factor;
                double delta_c = delta.delta_intercept / scale_factor;
                
                if (debug_output) {
                    std::cout << "  Delta parameters: start=" << delta.delta_start 
                              << ", slope=" << delta.delta_slope 
                              << ", intercept=" << delta.delta_intercept << std::endl;
                    std::cout << "  Base parameters: b=" << b << ", c=" << c << std::endl;
                    std::cout << "  Delta values: delta_b=" << delta_b << ", delta_c=" << delta_c << std::endl;
                }
                
                // Calculate actual parameters
                b += delta_b;
                c += delta_c;
                
                if (debug_output) {
                    std::cout << "  Recovered parameters: b=" << b << ", c=" << c << std::endl;
                }
                
                // Simulate fixed-point quantization
                int32_t scaled_b = static_cast<int32_t>(std::round(b * scale_factor));
                int32_t scaled_c = static_cast<int32_t>(std::round(c * scale_factor));
                
                // Now scale back down to get quantized values
                double quantized_b = static_cast<double>(scaled_b) / scale_factor;
                double quantized_c = static_cast<double>(scaled_c) / scale_factor;
                
                if (debug_output) {
                    std::cout << "  Quantized parameters: b=" << quantized_b << ", c=" << quantized_c << std::endl;
                }
                
                // Calculate result using adjusted x
                double linear_result = quantized_b * adjusted_x + quantized_c;
                
                // Apply Y reflection if needed
                double final_result = linear_result;
                if (delta.is_y_reflected) {
                    final_result = -linear_result;
                    if (debug_output) {
                        std::cout << "  Y reflection applied: " << linear_result << " → " << final_result << std::endl;
                    }
                }
                
                if (debug_output) {
                    std::cout << "  Final calculation: y = " << quantized_b << " * " << adjusted_x 
                              << " + " << quantized_c << " = " << final_result << std::endl;
                    
                    if (!std::isnan(true_value)) {
                        double error = std::abs(true_value - final_result);
                        std::cout << "  Error: " << error 
                                  << (error > target_error ? " (exceeds target " : " (within target ") 
                                  << target_error << ")" << std::endl;
                    }
                    std::cout << "------------------------------\n";
                    debug_counter++;
                }
                
                return final_result;
            }
        }
        // Handle orphan groups
        else if (group.storage_type == ORPHAN_GROUP) {
            if (debug_output) {
                std::cout << "Checking orphan group[" << group_idx << "] (ID=" << group.id << ")\n";
            }
            
            // Check each orphan interval
            for (size_t i = 0; i < group.delta_encodings.size(); i++) {
                const auto& delta = group.delta_encodings[i];
                if (delta.is_padding) continue;
                
                // Use the stored original interval boundaries for orphans
                double interval_start = delta.original_interval.start;
                double interval_end = delta.original_interval.end;
                
                // Check if x is inside this orphan interval
                if (x >= interval_start && x <= interval_end) {
                    if (debug_output) {
                        std::cout << "  Matched orphan interval [" << interval_start << ", " 
                                  << interval_end << "] (index=" << i << ")\n";
                    }
                    
                    // Get original parameters
                    double b = delta.original_params.b;
                    double c = delta.original_params.c;
                    
                    if (debug_output) {
                        std::cout << "  Original parameters: b=" << b << ", c=" << c << std::endl;
                    }
                    
                    // Simulate fixed-point quantization
                    int32_t scaled_b = static_cast<int32_t>(std::round(b * scale_factor));
                    int32_t scaled_c = static_cast<int32_t>(std::round(c * scale_factor));
                    
                    // Now scale back down to get quantized values
                    double quantized_b = static_cast<double>(scaled_b) / scale_factor;
                    double quantized_c = static_cast<double>(scaled_c) / scale_factor;
                    
                    if (debug_output) {
                        std::cout << "  Quantized parameters: b=" << quantized_b << ", c=" << quantized_c << std::endl;
                    }
                    
                    // Calculate result
                    double result = quantized_b * x + quantized_c;
                    
                    if (debug_output) {
                        std::cout << "  Final calculation: y = " << quantized_b << " * " << x 
                                  << " + " << quantized_c << " = " << result << std::endl;
                        
                        if (!std::isnan(true_value)) {
                            double error = std::abs(true_value - result);
                            std::cout << "  Error: " << error 
                                      << (error > target_error ? " (exceeds target " : " (within target ") 
                                      << target_error << ")" << std::endl;
                        }
                        std::cout << "------------------------------\n";
                        debug_counter++;
                    }
                    
                    return result;
                }
            }
        }
    }
    
    // If no interval was found
    if (debug_output) {
        std::cout << "WARNING: No interval found for x = " << x << std::endl;
        debug_counter++;
    }
    
    return std::numeric_limits<double>::quiet_NaN();
}
/*
double simulateHardwareLookup(
    double x, 
    const std::vector<IntervalGroup>& groups,
    double scale_factor,
    double target_error,
    double true_value) {
    
    // Debug info
    bool debug_output = true;
    // if (debug_output) {
    //     std::cout << "\n===== Hardware Lookup Process (x = " << x << ") =====\n";
    //     if (!std::isnan(true_value)) {
    //         std::cout << "True function value: " << true_value << std::endl;
    //     }
    // }
    
    // Search through all groups to find which one contains x
    for (size_t group_idx = 0; group_idx < groups.size(); group_idx++) {
        const auto& group = groups[group_idx];
        
        // Skip empty groups
        if (group.delta_encodings.empty()) continue;
        
        // For normal groups, check if x is within the group's range
        if (group.storage_type == NORMAL_GROUP) {
            double group_min = std::numeric_limits<double>::max();
            double group_max = std::numeric_limits<double>::lowest();
            
            // Calculate group's range by examining all its intervals
            for (const auto& delta : group.delta_encodings) {
                if (delta.is_padding) continue;
                
                double interval_start = delta.delta_start;
                double interval_end = interval_start + group.length;
                group_min = std::min(group_min, interval_start);
                group_max = std::max(group_max, interval_end);
            }
            
            // If x is inside this group's range
            if (x >= group_min && x <= group_max) {
                // if (debug_output) {
                //     std::cout << "Checking normal group[" << group_idx << "] (ID=" << group.id << ")\n";
                //     std::cout << "  Group range [" << group_min << ", " << group_max << "]\n";
                // }
                
                // Find the specific interval that contains x
                for (const auto& delta : group.delta_encodings) {
                    if (delta.is_padding) continue;
                    
                    double interval_start = delta.delta_start;
                    double interval_end = interval_start + group.length;
                    
                    bool use_exact_match = true;
                    // Check if x is inside this interval
                    if ((use_exact_match && x >= interval_start && x <= interval_end) ||
                        (!use_exact_match && x >= interval_start && x < interval_end)) {
                        // if (debug_output) {
                        //     std::cout << "  Using " << (use_exact_match ? "exact" : "half-open") << " match\n";
                        // }
                        
                        // Start with base parameters
                        double b = group.base_params.b;
                        double c = group.base_params.c;
                        
                        // Apply delta values
                        double delta_b = delta.delta_slope;
                        double delta_c = delta.delta_intercept;
                        
                        // if (debug_output) {
                        //     std::cout << "  Base parameters: b=" << b << ", c=" << c << std::endl;
                        //     std::cout << "  Delta values: delta_b=" << delta_b << ", delta_c=" << delta_c << std::endl;
                        // }
                        
                        // Calculate actual parameters based on deltas
                        b += delta_b;
                        c += delta_c;
                        
                        // if (debug_output) {
                            // std::cout << "  Pre-quantization parameters: b=" << b << ", c=" << c << std::endl;
                        // }
                        
                        // Pre-quantization result (for comparison)
                        double pre_quant_result = b * x + c;
                        // if (debug_output) {
                            // std::cout << "  Pre-quantization result: " << pre_quant_result << std::endl;
                        // }
                        
                        // Simulate fixed-point quantization
                        // Scale the parameters by the scale factor
                        int32_t scaled_b = static_cast<int32_t>(std::round(b * scale_factor));
                        int32_t scaled_c = static_cast<int32_t>(std::round(c * scale_factor));
                        
                        // Now scale back down to get quantized values
                        double quantized_b = static_cast<double>(scaled_b) / scale_factor;
                        double quantized_c = static_cast<double>(scaled_c) / scale_factor;
                        
                        // if (debug_output) {
                        //     std::cout << "  Final quantized parameters: b=" << scaled_b << "/" << scale_factor 
                        //               << ", c=" << scaled_c << "/" << scale_factor << std::endl;
                        //     std::cout << "  Final calculation: y = " << quantized_b << " * " << x << " + " 
                        //               << quantized_c << " = " << (quantized_b * x + quantized_c) << std::endl;
                        // }
                        
                        // Calculate final result using quantized parameters
                        double result = quantized_b * x + quantized_c;
                        
                        // Report error if we have the true value
                        if (debug_output && !std::isnan(true_value)) {
                            double error = std::abs(true_value - result);
                            std::cout << "  ERROR: " << error << (error > target_error ? " (exceeds target " : " (within target ") 
                                      << target_error << ")" << std::endl;
                        }
                        
                        return result;
                    }
                }
            }
        }
        // For orphan groups, check each interval separately
        else if (group.storage_type == ORPHAN_GROUP) {
            // if (debug_output) {
            //     std::cout << "Checking orphan group[" << group_idx << "] (ID=" << group.id << ")\n";
            // }
            
            // Check each orphan interval
            for (const auto& delta : group.delta_encodings) {
                if (delta.is_padding) continue;
                
                // Use the stored original interval boundaries for orphans
                double interval_start = delta.original_interval.start;
                double interval_end = delta.original_interval.end;
                
                // Check if x is inside this orphan interval
                if (x >= interval_start && x <= interval_end) {
                    // if (debug_output) {
                    //     std::cout << "  Matched orphan interval [" << interval_start << ", " 
                    //               << interval_end << "] (index=" << delta.original_index << ")\n";
                    // }
                    
                    // Get original parameters from orphan intervals
                    double b = delta.original_params.b;
                    double c = delta.original_params.c;
                    
                    // if (debug_output) {
                    //     std::cout << "  Original parameters: b=" << b << ", c=" << c << std::endl;
                    // }
                    
                    // Pre-quantization result (for comparison)
                    // double pre_quant_result = b * x + c;
                    // if (debug_output) {
                    //     std::cout << "  Pre-quantization result: " << pre_quant_result << std::endl;
                    // }
                    
                    // Simulate fixed-point quantization
                    // Scale the parameters by the scale factor
                    int32_t scaled_b = static_cast<int32_t>(std::round(b * scale_factor));
                    int32_t scaled_c = static_cast<int32_t>(std::round(c * scale_factor));
                    
                    // Now scale back down to get quantized values
                    double quantized_b = static_cast<double>(scaled_b) / scale_factor;
                    double quantized_c = static_cast<double>(scaled_c) / scale_factor;
                    
                    // if (debug_output) {
                    //     std::cout << "  Quantized parameters: b=" << scaled_b << "/" << scale_factor 
                    //               << ", c=" << scaled_c << "/" << scale_factor << std::endl;
                    //     std::cout << "  Final parameters: b=" << quantized_b << ", c=" << quantized_c << std::endl;
                    //     std::cout << "  Calculation: y = " << quantized_b << " * " << x 
                    //               << " + " << quantized_c << " = " << (quantized_b * x + quantized_c) << std::endl;
                    // }
                    
                    // Calculate final result using quantized parameters
                    double result = quantized_b * x + quantized_c;
                    
                    // Report error if we have the true value
                    if (debug_output && !std::isnan(true_value)) {
                        double error = std::abs(true_value - result);
                        std::cout << "  ERROR: " << error << (error > target_error ? " (exceeds target " : " (within target ") 
                                  << target_error << ")" << std::endl;
                    }
                    
                    return result;
                }
            }
        }
    }
    
    // If we get here, no interval was found that contains x
    if (debug_output) {
        std::cout << "WARNING: No interval found for x = " << x << std::endl;
    }
    
    // Return NaN for values outside of the defined intervals
    return std::numeric_limits<double>::quiet_NaN();
}*/

void analyzeGroupCoverage(const std::vector<IntervalGroup>& groups, double start, double end) {
    std::cout << "\n=== Analyzing Group Coverage (Range " << start << " to " << end << ") ===\n";
    
    if (groups.empty()) {
        std::cout << "Warning: No groups defined!\n";
        return;
    }
    
    // Collect all interval ranges
    std::vector<std::pair<double, double>> all_intervals;
    
    // Display coverage for all groups
    std::cout << "Group coverage ranges:\n";
    for (size_t i = 0; i < groups.size(); i++) {
        const auto& g = groups[i];
        
        if (g.storage_type != ORPHAN_GROUP) {
            // Normal group
            double min_start = std::numeric_limits<double>::max();
            double max_end = std::numeric_limits<double>::lowest();
            size_t valid_intervals = 0;
            
            for (const auto& de : g.delta_encodings) {
                if (de.is_padding) continue;
                valid_intervals++;
                
                // Using absolute positioning for all intervals
                double interval_start = de.delta_start;
                double interval_end = interval_start + g.length;
                
                min_start = std::min(min_start, interval_start);
                max_end = std::max(max_end, interval_end);
                
                all_intervals.push_back({interval_start, interval_end});
            }
            
            std::cout << "Group " << i << ": [" << min_start << ", " << max_end << "]";
            
            if (valid_intervals > 0) {
                std::cout << " containing " << valid_intervals << " encoded intervals";
            }
            std::cout << "\n";
        } else {
            // Orphan group - show each interval
            std::cout << "Group " << i << " (Orphan Group)";
            
            // Count non-padding intervals
            size_t valid_intervals = 0;
            for (const auto& de : g.delta_encodings) {
                if (!de.is_padding) {
                    valid_intervals++;
                    
                    // Use precise boundaries from original_interval
                    double interval_start = de.delta_start;
                    double interval_end = de.original_interval.end;
                    
                    all_intervals.push_back({interval_start, interval_end});
                }
            }
            
            std::cout << " containing " << valid_intervals << " orphan intervals\n";
            
            // Show details for first few intervals
            size_t display_limit = std::min(size_t(5), g.delta_encodings.size());
            size_t shown = 0;
            
            for (size_t j = 0; j < g.delta_encodings.size() && shown < display_limit; j++) {
                const auto& de = g.delta_encodings[j];
                if (de.is_padding) continue;
                
                std::cout << "  Interval " << shown << ": [" 
                          << de.delta_start << ", " 
                          << de.original_interval.end << "]\n";
                shown++;
            }
            
            size_t remaining = valid_intervals - shown;
            if (remaining > 0) {
                std::cout << "  ... and " << remaining << " more intervals\n";
            }
        }
    }
    
    // Use grid-based analysis for coverage
    const int grid_size = 1000;
    std::vector<bool> covered(grid_size, false);
    
    for (const auto& interval : all_intervals) {
        // Calculate grid points covered by interval
        int start_idx = static_cast<int>((interval.first - start) / (end - start) * grid_size);
        int end_idx = static_cast<int>((interval.second - start) / (end - start) * grid_size);
        
        // Ensure indices are within valid range
        start_idx = std::max(0, std::min(grid_size-1, start_idx));
        end_idx = std::max(0, std::min(grid_size-1, end_idx));
        
        // Mark covered regions
        for (int j = start_idx; j <= end_idx; j++) {
            covered[j] = true;
        }
    }
    
    // Calculate coverage percentage
    int covered_count = 0;
    for (bool c : covered) {
        if (c) covered_count++;
    }
    
    double coverage_percent = (double)covered_count / grid_size * 100.0;
    std::cout << "Total coverage: " << coverage_percent << "%\n";
    
    if (coverage_percent < 100.0) {
        // Print uncovered regions
        std::cout << "Uncovered regions:\n";
        double prev_uncovered = -1.0;
        bool in_gap = false;
        
        for (int i = 0; i < grid_size; i++) {
            double x = start + (end - start) * i / grid_size;
            
            if (!covered[i]) {
                if (!in_gap) {
                    prev_uncovered = x;
                    in_gap = true;
                }
            } else if (in_gap) {
                std::cout << "  [" << prev_uncovered << ", " << x << "]\n";
                in_gap = false;
            }
        }
        
        // Handle uncovered region at the end
        if (in_gap) {
            std::cout << "  [" << prev_uncovered << ", " << end << "]\n";
        }
    }
}

// Function to verify hardware implementation
bool verifyHardwareImplementation(const std::string& expression_str,
                                const std::vector<IntervalGroup>& groups,
                                int hw_frac_bits,
                                double target_error,
                                const std::string& func_dir,
                                const std::string& clean_name,
                                double start, double end,
                                bool strict_mode = false) {
    // Calculate hardware scale factor from bit width
    double hw_scale_factor = 1 << hw_frac_bits;
    
    // Calculate actual bits (to verify correct reporting)
    int actual_bits = static_cast<int>(std::log2(hw_scale_factor));
    
    std::cout << "\n===== Hardware Implementation Verification =====\n";
    std::cout << "Function: " << expression_str << "\n";
    std::cout << "Hardware parameters: " << actual_bits << " bits (scale factor = " 
              << hw_scale_factor << ")\n";
    std::cout << "Target error: " << std::scientific << std::setprecision(2) << target_error << "\n";
    std::cout << "Verification mode: " << (strict_mode ? "Strict" : "Average") << "\n";
    
    // Load test vectors from CSV file
    std::string csv_filename = func_dir + "/sim/test_vectors/" + clean_name + "_vectors.csv";
    std::vector<std::pair<double, double>> test_points; // <x, expected_y>
    
    std::ifstream csv_file(csv_filename);
    if (csv_file.is_open()) {
        std::cout << "Loading test vectors from: " << csv_filename << "\n";
        
        std::string line;
        // Skip header line
        std::getline(csv_file, line);
        
        while (std::getline(csv_file, line)) {
            std::stringstream ss(line);
            std::string token;
            std::vector<std::string> tokens;
            
            while (std::getline(ss, token, ',')) {
                tokens.push_back(token);
            }
            
            if (tokens.size() >= 2) {
                double x = std::stod(tokens[0]);
                double expected_y = std::stod(tokens[1]);
                test_points.push_back({x, expected_y});
            }
        }
        csv_file.close();
        std::cout << "Loaded " << test_points.size() << " test vectors\n";
    } else {
        // Fall back to generating test points if CSV doesn't exist
        std::cout << "Test vector CSV not found, generating test points in range [" 
                  << start << ", " << end << "]\n";
        
        size_t num_points = 100;
        double step = (end - start) / (num_points - 1);
        
        for (size_t i = 0; i < num_points; i++) {
            double x = start + i * step;
            double y = computeFunctionValue(expression_str, x);
            test_points.push_back({x, y});
        }
    }
    
    // Verification metrics
    size_t total_points = test_points.size();
    size_t failed_points = 0;
    double max_error = 0.0;
    double total_error = 0.0;
    double worst_x = 0.0;
    
    // Process each test point
    for (const auto& [x, expected_y] : test_points) {
        // Simulate hardware lookup result - pass expected_y for more detailed debug output
        double hw_result = simulateHardwareLookup(x, groups, hw_scale_factor, target_error, expected_y);
        
        // Calculate error
        double error = std::abs(hw_result - expected_y);
        total_error += error;
        
        // Track maximum error
        if (error > max_error) {
            max_error = error;
            worst_x = x;
        }
        
        // Check if error exceeds target
        if (error > target_error) {
            failed_points++;
            
            // Print details for failed points
            if (failed_points <= 10) { // Limit output to first 10 failures
                std::cout << "FAIL: x=" << std::scientific << std::setprecision(2) << x 
                          << ", expected=" << std::scientific << std::setprecision(2) << expected_y 
                          << ", got=" << std::scientific << std::setprecision(2) << hw_result 
                          << ", error=" << std::scientific << std::setprecision(2) << error << "\n";
            } else if (failed_points == 11) {
                std::cout << "Additional failures omitted...\n";
            }
        }
    }
    
    // Calculate average error - ensure we're not dividing by zero
    double avg_error = (total_points > 0) ? (total_error / total_points) : 0.0;
    double pass_rate = (total_points > 0) ? (100.0 * (total_points - failed_points) / total_points) : 0.0;
    
    // Print verification summary
    std::cout << "\n----- Verification Summary -----\n";
    std::cout << "Test points: " << total_points << "\n";
    std::cout << "Pass rate: " << std::fixed << std::setprecision(2) << pass_rate << "% (" 
              << (total_points - failed_points) << "/" << total_points << ")\n";
    std::cout << "Average error: " << std::scientific << std::setprecision(6) << avg_error
              << (avg_error <= target_error ? " (within target)" : " (exceeds target)") << "\n";
    std::cout << "Maximum error: " << std::scientific << std::setprecision(6) << max_error 
              << " at x=" << std::fixed << std::setprecision(6) << worst_x
              << (max_error <= target_error ? " (within target)" : " (exceeds target)") << "\n";
    
    // Determine success based on verification mode
    bool strict_success = failed_points == 0;
    bool avg_success = avg_error <= target_error;
    bool success = strict_mode ? strict_success : avg_success;
    
    std::cout << "Verification result: " << (success ? "PASS" : "FAIL") << "\n";
    if (strict_mode && !strict_success && avg_success) {
        std::cout << "Note: Verification fails in strict mode but average error is within target.\n";
        std::cout << "      Consider using average mode if occasional outliers are acceptable.\n";
    }
    
    return success;
}

// Save the compressed groups to a file and generate FPGA implementation files
inline void saveCompressedGroupsToFile(const std::vector<IntervalGroup>& groups, 
                                     const std::string& filename,
                                     const std::vector<Interval>& intervals = {},
                                     const std::vector<FitParameters>& fit_params = {},
                                     const std::string& expression_str = "",
                                     double start = 0.0, double end = 1.0) {
    if (groups.empty()) {
        std::cout << "No groups to save.\n";
        return;
    }

    std::string directory = "";
    size_t last_slash = filename.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        directory = filename.substr(0, last_slash + 1);
    }

    std::string cleanName = "pwl";
    if (last_slash != std::string::npos) {
        size_t prev_slash = filename.find_last_of("/\\", last_slash - 1);
        if (prev_slash != std::string::npos) {
            cleanName = filename.substr(prev_slash + 1, last_slash - prev_slash - 1);
        }
    }

    std::ofstream file(filename);
    if (file.is_open()) {
        file << "GroupID,Length,BaseStart,BaseEnd,FitOrder,FitA,FitB,FitC,"
            << "StartScaleFactor,SlopeScaleFactor,InterceptScaleFactor,"
            << "BitWidthStart,BitBitWidthIntercept,DeltaEncodings\n";

        size_t group_id = 0;
        for (const auto& group : groups) {
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

            for (size_t i = 0; i < group.delta_encodings.size(); ++i) {
                const auto& delta = group.delta_encodings[i];
                int quant_delta_start = static_cast<int>(std::round(delta.delta_start / group.start_scale_factor));
                int quant_delta_slope = static_cast<int>(std::round(delta.delta_slope / group.slope_scale_factor));
                int quant_delta_intercept = static_cast<int>(std::round(delta.delta_intercept / group.intercept_scale_factor));

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
    } else {
        std::cout << "Failed to open file to save compressed Interval Groups!\n";
        return;
    }

    // No hardware-specific code in this function
}

#endif // INTERVAL_GROUP_COMPRESSOR_HPP

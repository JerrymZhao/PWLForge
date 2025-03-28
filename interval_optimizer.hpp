// interval_optimizer.hpp

#ifndef INTERVAL_OPTIMIZER_HPP
#define INTERVAL_OPTIMIZER_HPP

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

#include <vector>
#include <functional>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <mutex>
#include <future>
#include <unordered_map>
#include <unordered_set>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <atomic>
#include <shared_mutex>
#include "exprtk.hpp"
#include "function_fitter.hpp"

struct IntervalMetrics {
    double entropy;
    double complexity;
    double error_sensitivity;
    double merge_score;

    double max_abs_error;
    double avg_abs_error;

    IntervalMetrics() : entropy(0.0), complexity(0.0), error_sensitivity(0.0), 
                    max_abs_error(0.0), avg_abs_error(0.0) {}
    IntervalMetrics(double e, double c, double es, double ms)
                    : entropy(e), complexity(c), error_sensitivity(es), merge_score(ms) {}
};

struct OptimizationConfig {
    double entropy_threshold;
    double complexity_threshold;
    double error_sensitivity_threshold;
    double merge_score_threshold;
    double target_error;

    OptimizationConfig() : entropy_threshold(0.1), complexity_threshold(0.1), 
                            error_sensitivity_threshold(0.1), merge_score_threshold(0.1), target_error(1e-4) {}
};

struct MergeParams {
    double base_len_tol;
    double base_continuity_tol;
    double curvature_base;
    double curvature_sensitivity = 1.5;
    double slope_base;
    double epsilon_base;

    // MergeParams() : base_len_tol(1e-6), base_continuity_tol(1e-6), 
    //                 curvature_base(1.0), curvature_sensitivity(1.0), 
    //                 slope_base(1.0), epsilon_base(1e-6) {}
};

class FunctionCache {
    private:
        std::unordered_map<double, double> cache;
        mutable std::mutex mutex;
        std::atomic<size_t> hits{0};
        std::atomic<size_t> misses{0};
        
    public:
        double get(const std::string& expression_str, double x) {
            // Try to find the value in the cache
            {
                std::lock_guard<std::mutex> lock(mutex);
                auto it = cache.find(x);
                if (it != cache.end()) {
                    hits++;
                    return it->second;
                }
            }
            
            // Cache miss
            misses++;
            double result = computeFunctionValue(expression_str, x);
            
            // Update cache
            {
                std::unique_lock<std::mutex> lock(mutex);
                cache[x] = result;
            }
            
            return result;
        }
        
        // 预热缓存（批量计算常用点）
        void warmup(const std::string& expression_str, double start, double end, size_t points) {
            if (points < 2) return;
            const double step = (end - start) / (points - 1);
            
            std::vector<std::future<void>> futures;
            const size_t batch_size = 1000;
            const size_t num_batches = (points + batch_size - 1) / batch_size;
            
            for (size_t batch = 0; batch < num_batches; batch++) {
                futures.push_back(std::async(std::launch::async, [this, &expression_str, start, step, batch, batch_size, points]() {
                    size_t begin = batch * batch_size;
                    size_t end = std::min((batch + 1) * batch_size, points);
                    
                    for (size_t i = begin; i < end; i++) {
                        double x = start + i * step;
                        this->get(expression_str, x);
                    }
                }));
            }
            
            for (auto& f : futures) {
                f.wait();
            }
        }
        
        void printStats() const {
            size_t total = hits.load() + misses.load();
            if (total == 0) return;
            
            double hit_rate = 100.0 * hits.load() / total;
            std::cout << "Function cache stats: " 
                      << hits.load() << " hits, " 
                      << misses.load() << " misses, "
                      << hit_rate << "% hit rate\n";
        }
    };
    
// Global function cache
FunctionCache function_cache;

inline void checkCoverage(const std::vector<Interval>& intervals,
                        double domain_start,
                        double domain_end) {
    const double epsilon = 1e-6;
    double prev_end = domain_start;
    size_t gap_count = 0;
    
    for (const auto& iv : intervals) {
        if (iv.start > prev_end + epsilon) {
            gap_count++;
            std::cout << "Gap detected between " << prev_end << " and " << iv.start << std::endl;
        }
        prev_end = iv.end;
    }

    if (prev_end < domain_end - epsilon) {
        gap_count++;
        std::cout << "Gap detected between " << prev_end << " and " << domain_end << std::endl;
    }

    std::cout << "Total gaps detected: " << gap_count << std::endl;
}

// Calculate the Hessian (second derivative)
inline double computeHessian(const std::string& expression_str, double x, double h = 1e-6) {
    double fxph = function_cache.get(expression_str, x + h);
    double fx = function_cache.get(expression_str, x);
    double fxmh = function_cache.get(expression_str, x - h);
    return (fxph - 2 * fx + fxmh) / (h * h);
}

inline IntervalMetrics calculateMetrics(const Interval& interval, const std::string& expression_str, int sample_points = 10) {
    IntervalMetrics metrics;

    double f_start = computeFunctionValue(expression_str, interval.start);
    double f_end = computeFunctionValue(expression_str, interval.end);
    
    // Compute entropy (simplified version for illustration)
    metrics.entropy = std::abs(f_start - f_end) / (f_start + f_end + 1e-6);

    // Compute complexity (simplified version)
    metrics.complexity = std::log(interval.end - interval.start + 1.0);

    // Compute error sensitivity (simplified)
    metrics.error_sensitivity = std::abs(f_start - f_end) / std::max(std::abs(f_start), std::abs(f_end) + 1e-6);
    
    // Calculate a merge score (simplified)
    metrics.merge_score = metrics.entropy + metrics.complexity + metrics.error_sensitivity;

    if (sample_points < 2) sample_points = 2;
    const double step = (interval.end - interval.start) / (sample_points - 1);

    for (int i = 0; i < sample_points; ++i) {
        double x = interval.start + i * step;
        double f_val = computeFunctionValue(expression_str, x);

        double approx = f_start + (x - interval.start) * (f_end - f_start) / (interval.end - interval.start);
        double error = std::abs(f_val - approx);
        metrics.max_abs_error = std::max(metrics.max_abs_error, error);
        metrics.avg_abs_error += error;
    }
    metrics.avg_abs_error /= sample_points;

    return metrics;
}

inline bool quickMergeCheck(const Interval& a, const Interval& b, 
                          double target_error, 
                          const std::string& expression_str) {
    // 区间太小，直接假设可合并
    const double min_len_threshold = 1e-8;
    if ((a.end - a.start) < min_len_threshold || (b.end - b.start) < min_len_threshold) {
        return true;
    }
    
    // 连续性检查
    if (std::abs(a.end - b.start) > 1e-9) {
        return false;
    }
    
    // Hessian差异过大的区间难以合并
    if (std::abs(a.hessian - b.hessian) > 100.0 * target_error) {
        return false;
    }
    
    // 快速线性插值检查 
    double f_a_start = function_cache.get(expression_str, a.start);
    double f_b_end = function_cache.get(expression_str, b.end);
    double f_mid = function_cache.get(expression_str, (a.end + b.start) / 2.0);
    
    // 计算线性插值
    double t = 0.5;  // 中点
    double f_linear = f_a_start * (1-t) + f_b_end * t;
    
    // 检查中点误差
    double error = std::abs(f_mid - f_linear);
    return error < 5.0 * target_error;  // 使用宽松的阈值
}

// Generate initial intervals and calculate the Hessian
inline std::vector<Interval> generateInitialIntervals(double start, 
                                                    double end, 
                                                    size_t num_points, 
                                                    double initial_unit_length, 
                                                    const std::string& expression_str,
                                                    const OptimizationConfig& config) {
    if (start >= end || num_points < 2) {
        throw std::invalid_argument("Invalid interval range or number of points");
    }

    std::cout << "Starting interval generation with " << num_points << " points...\n";
    std::vector<Interval> intervals;
    intervals.reserve(num_points);

    // 预热函数值缓存 - 大大加快后续计算
    std::cout << "Precomputing function values..." << std::endl;
    function_cache.warmup(expression_str, start, end, num_points * 2);
    
    double step = (end - start) / static_cast<double>(num_points - 1);
    double eps = step * 1e-6;  // Relative epsilon based on step size

    std::mutex interval_mutex;
    std::vector<std::future<void>> futures;
    const size_t batch_size = 1000; // 增大批处理大小
    size_t total_points = num_points + 1;

    for (size_t batch_start = 0; batch_start < num_points; batch_start += batch_size) {
        size_t batch_end = std::min(batch_start + batch_size, num_points);
        futures.clear();

        for (size_t i = batch_start; i < batch_end; ++i) {
            double current_start = (i == 0) ? start : start + (i * step) - eps;
            double current_end = (i == total_points - 1) ? end : 
                               std::min(start + ((i + 1) * step) + eps, end);

            futures.emplace_back(std::async(std::launch::async, 
                [&interval_mutex, &intervals, &expression_str, &config, &initial_unit_length]
                (double s, double e) {
                    double mid = (s + e) / 2.0;
                    double hessian = computeHessian(expression_str, mid);

                    IntervalMetrics metrics = calculateMetrics(
                        Interval{s, e, hessian, 0}, 
                        expression_str
                    );

                    if(metrics.error_sensitivity > config.error_sensitivity_threshold ||
                        metrics.entropy > config.entropy_threshold) {
                        // if error sensitivity or entropy is above threshold, split the interval
                        std::lock_guard<std::mutex> lock(interval_mutex);
                        intervals.push_back(Interval{s, e, hessian, 0});
                    } else {
                        std::lock_guard<std::mutex> lock(interval_mutex);
                        intervals.push_back(Interval{s, e, hessian, 0});
                    }
                }, current_start, current_end
            ));
        }

        // Wait for batch completion with timeout
        for (auto& future : futures) {
            future.wait();
        }
    }

    // Ensure final interval reaches the end
    if (!intervals.empty() && intervals.back().end < end) {
        double last_start = intervals.back().end - eps;
        double hessian = computeHessian(expression_str, (last_start + end) / 2.0);
        intervals.push_back(Interval{last_start, end, hessian, 0});
    }

    // Sort intervals by start point
    std::sort(intervals.begin(), intervals.end(), 
        [](const Interval& a, const Interval& b) {
            return a.start < b.start;
        });

    std::cout << "Generated " << intervals.size() << " initial intervals." << std::endl;
    return intervals;
}

// Calcluate the normalized function difference
inline double computeNormalizedFunctionDifference(const std::string& expression_str, double start, double end) {
    double f_start = function_cache.get(expression_str, start);
    double f_end = function_cache.get(expression_str, end);
    double max_value = std::max(std::abs(f_start), std::abs(f_end));
    if (max_value < 1e-12) {
        return 0.0;
    }
    double normalized_diff = std::abs(f_start - f_end) / max_value;
    return normalized_diff;
}

inline bool shouldSplit(const Interval& interval, 
                        double epsilon, 
                        const std::string& expression_str) {
    // Sample points
    double start = interval.start;
    double mid = (interval.start + interval.end) / 2.0;
    double end = interval.end;
    
    // Function values - 使用缓存
    double f_start = function_cache.get(expression_str, start);
    double f_mid = function_cache.get(expression_str, mid);
    double f_end = function_cache.get(expression_str, end);

    // Adaptive thresholds based on interval length
    double curvature_fatcor = 1.0 + std::abs(interval.hessian);
    double length_factor = 1.0 + (interval.end - interval.start);
    double adaptive_epsilon = epsilon / (curvature_fatcor + length_factor);

    // Linearity check
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
        (normalized_error > adaptive_epsilon * 1.2) ||          // Adaptive linearity check
        (slope_diff > slope_threshold * 0.8) ||                 // Adaptive slope check
        (std::abs(interval.hessian) > adaptive_epsilon * 8.0);  // Hessian check

    if (!should_split) {
        double left_hessian = computeHessian(expression_str, interval.start + (mid - interval.start) / 4.0);
        double right_hessian = computeHessian(expression_str, mid + (interval.end - mid) / 4.0);
        should_split |= std::abs(left_hessian - right_hessian) > adaptive_epsilon * 4.0;
    }

    return should_split;
}

// Split the interval function
inline void splitInterval(const Interval& interval, 
                        double epsilon, 
                        double min_unit_length, 
                        const std::string& expression_str, 
                        std::vector<Interval>& result,
                        double target_error,
                        double relax_factor = 1.0) {
    double length = interval.end - interval.start;

    // 优化：使用缓存计算规范化差异
    double normalized_diff = computeNormalizedFunctionDifference(expression_str, interval.start, interval.end);

    // 终止条件检查
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

    double mid = (interval.start + interval.end) / 2.0;
    double left_hessian = computeHessian(expression_str, (interval.start + mid) / 2.0);
    double right_hessian = computeHessian(expression_str, (mid + interval.end) / 2.0);
    
    Interval left = {interval.start, mid, left_hessian, interval.level + 1};
    Interval right = {mid, interval.end, right_hessian, interval.level + 1};

    // 优化：并行拆分子区间
    std::mutex result_mutex;
    std::vector<Interval> left_result, right_result;
    
    auto left_future = std::async(std::launch::async, [&]() {
        splitInterval(left, epsilon, min_unit_length, expression_str, left_result, target_error, relax_factor);
    });
    
    auto right_future = std::async(std::launch::async, [&]() {
        splitInterval(right, epsilon, min_unit_length, expression_str, right_result, target_error, relax_factor);
    });
    
    left_future.wait();
    right_future.wait();
    
    // 合并结果
    result.insert(result.end(), left_result.begin(), left_result.end());
    result.insert(result.end(), right_result.begin(), right_result.end());
}

inline bool findNearestPowerOfTwo(double len, double tol = 1e-8) {
    if (len <= 0) return false;
    const double power = std::log2(len);
    return std::abs(power - std::round(len)) < tol;
}

inline void printDistribution(const std::vector<Interval>& intervals, int precision) {
    std::map<std::string, size_t> dist_map;
    const double round_factor = std::pow(10.0, precision);
    
    for (const auto& iv : intervals) {
        // 计算并四舍五入长度
        const double raw_len = iv.end - iv.start;
        const double rounded_len = std::round(raw_len * round_factor) / round_factor;
        
        // 格式化为科学计数法
        std::ostringstream oss;
        oss << std::scientific << std::setprecision(precision) << rounded_len;
        dist_map[oss.str()]++;
    }

    // 打印分布表
    std::cout << "Length\t\t\tCount\n";
    std::cout << "------------------------------\n";
    for (const auto& entry : dist_map) {
        std::cout << entry.first << "\t\t" << entry.second << "\n";
    }
}

// Helper function to ensure no gaps between intervals
inline void ensureNoGaps(std::vector<Interval>& intervals) {
    if (intervals.empty()) return;
    
    // Sort by start point
    std::sort(intervals.begin(), intervals.end(),
             [](const Interval& a, const Interval& b) {
                 return a.start < b.start;
             });
    
    // Fix any gaps
    const double tolerance = 1e-10;
    for (size_t i = 0; i < intervals.size() - 1; ++i) {
        if (intervals[i+1].start > intervals[i].end + tolerance) {
            intervals[i].end = intervals[i+1].start;
        }
    }
}

// Helper function to adaptively split a high-error interval
inline void adaptiveSplitHighErrorInterval(const Interval& interval,
                                         double target_error,
                                         double min_unit_length,
                                         const std::string& expression_str,
                                         std::vector<Interval>& result) {
    // Following target_error to modify min_unit_length
    // Attention: the min_unit_length is not the final length of the interval
    double adaptive_min_unit_length = std::min(min_unit_length, target_error * 0.01);
    
    // Keep if the interval is already small enough
    if (interval.end - interval.start <= adaptive_min_unit_length) {
        result.push_back(interval);
        return;
    }
    
    // Evaluate the current interval error
    double current_error;
    try {
        FitParameters params = fitSegment(expression_str, interval, target_error);
        current_error = estimateSegmentError(expression_str, interval, params);
        
        if (current_error <= target_error) {
            result.push_back(interval);
            return;
        }
    } catch (const std::exception& e) {
        current_error = std::numeric_limits<double>::max();
    }
    
    // 计算误差超标程度，调整epsilon值
    double error_ratio = current_error / target_error;
    double adaptive_epsilon = std::min(0.001, target_error / (error_ratio * 4)); // 更小的epsilon增强拆分
    
    // 根据区间长度决定至少要拆分成几部分
    int min_split_count = std::min(8, std::max(2, 
        static_cast<int>((interval.end - interval.start) / adaptive_min_unit_length / 2)));
    
    // 根据误差比率决定拆分策略
    if (error_ratio > 10.0) {
        // 误差非常高，强制多路拆分而不仅是二分
        std::cout << "High error ratio (" << error_ratio << "x), using aggressive multi-way splitting" << std::endl;
        
        // 均匀拆分为多个子区间
        for (int i = 0; i < min_split_count; ++i) {
            double sub_start = interval.start + i * (interval.end - interval.start) / min_split_count;
            double sub_end = interval.start + (i + 1) * (interval.end - interval.start) / min_split_count;
            double mid_hessian = computeHessian(expression_str, (sub_start + sub_end) / 2.0);
            
            Interval sub_interval = {sub_start, sub_end, mid_hessian, interval.level + 1};
            
            // 使用splitInterval进一步细化
            std::vector<Interval> sub_result;
            double relax_factor = 0.4;  // 更严格的拆分标准
            
            splitInterval(sub_interval, adaptive_epsilon, adaptive_min_unit_length, expression_str, 
                        sub_result, target_error, relax_factor);
            
            result.insert(result.end(), sub_result.begin(), sub_result.end());
        }
    }
    else {
        // 误差适中，尝试检测特征点
        const size_t num_samples = 31;  // 增加采样点数量以提高精度
        std::vector<double> sample_points;
        std::vector<double> sample_errors;
        
        // 生成采样点
        for (size_t i = 0; i <= num_samples; ++i) {
            double t = static_cast<double>(i) / num_samples;
            double x = interval.start + t * (interval.end - interval.start);
            sample_points.push_back(x);
        }
        
        // 计算误差分布
        try {
            FitParameters params = fitSegment(expression_str, interval, target_error);
            sample_errors.resize(sample_points.size());
            
            // 并行计算样本点误差
            std::vector<std::future<void>> futures;
            for (size_t i = 0; i < sample_points.size(); ++i) {
                futures.push_back(std::async(std::launch::async, [&, i]() {
                    double x = sample_points[i];
                    double actual = function_cache.get(expression_str, x);
                    double approx = evaluateSegment(x, params);
                    sample_errors[i] = std::abs(actual - approx);
                }));
            }
            
            for (auto& future : futures) {
                future.wait();
            }
            
            // 计算误差统计
            double avg_error = 0.0;
            for (double err : sample_errors) {
                avg_error += err;
            }
            avg_error /= sample_errors.size();
            
            // 更智能的特征点检测，降低检测阈值
            std::vector<size_t> peak_indices;
            double peak_threshold = std::max(target_error * 0.5, avg_error * 0.8);
            
            for (size_t i = 1; i < sample_errors.size() - 1; ++i) {
                // 检测局部峰值
                if (sample_errors[i] > peak_threshold &&
                    sample_errors[i] > sample_errors[i-1] &&
                    sample_errors[i] > sample_errors[i+1]) {
                    peak_indices.push_back(i);
                }
            }
            
            // 如果没有找到峰值，选择误差最大的几个点
            if (peak_indices.empty()) {
                std::vector<std::pair<double, size_t>> error_idx_pairs;
                for (size_t i = 1; i < sample_errors.size() - 1; ++i) {
                    error_idx_pairs.push_back({sample_errors[i], i});
                }
                
                // 按误差排序
                std::sort(error_idx_pairs.begin(), error_idx_pairs.end(),
                         [](const auto& a, const auto& b) { return a.first > b.first; });
                
                // 选择前几个点作为分割点
                int max_points = std::min(min_split_count - 1, static_cast<int>(error_idx_pairs.size()));
                for (int i = 0; i < max_points; ++i) {
                    peak_indices.push_back(error_idx_pairs[i].second);
                }
                
                // 确保分割点是有序的
                std::sort(peak_indices.begin(), peak_indices.end());
            }
            
            // 找到有意义的分割点
            if (!peak_indices.empty()) {
                // 在峰值处分割
                std::vector<double> split_points = {interval.start};
                for (size_t idx : peak_indices) {
                    if (idx > 0 && idx < sample_points.size() - 1) {
                        split_points.push_back(sample_points[idx]);
                    }
                }
                split_points.push_back(interval.end);
                
                // 移除过于接近的分割点
                std::vector<double> filtered_points = {split_points[0]};
                for (size_t i = 1; i < split_points.size(); ++i) {
                    if (split_points[i] - filtered_points.back() >= adaptive_min_unit_length) {
                        filtered_points.push_back(split_points[i]);
                    }
                }
                
                // 根据分割点创建和拆分区间
                for (size_t i = 0; i < filtered_points.size() - 1; ++i) {
                    double start = filtered_points[i];
                    double end = filtered_points[i+1];
                    
                    // 使用splitInterval进一步优化
                    double mid_hessian = computeHessian(expression_str, (start + end) / 2.0);
                    Interval sub_interval = {start, end, mid_hessian, interval.level + 1};
                    
                    std::vector<Interval> sub_result;
                    splitInterval(sub_interval, adaptive_epsilon, adaptive_min_unit_length, 
                                expression_str, sub_result, target_error, 0.5); // 更严格的relax_factor
                    
                    result.insert(result.end(), sub_result.begin(), sub_result.end());
                }
                
                // 如果分割后结果仍然只有1个区间，强制多路拆分
                if (result.size() <= 1) {
                    result.clear(); // 清空之前的结果，准备强制拆分
                    std::cout << "Feature-based split ineffective, forcing multi-way split" << std::endl;
                    goto force_split; // 跳转到强制拆分代码
                }
                
                return;
            }
        } catch (const std::exception& e) {
            // 拟合失败，通过标签跳转到强制拆分代码
            std::cout << "Error calculation failed, forcing split: " << e.what() << std::endl;
            goto force_split;
        }
        
        // 没有找到有用的特征点或计算失败，尝试使用splitInterval
        std::vector<Interval> split_result;
        splitInterval(interval, adaptive_epsilon, adaptive_min_unit_length, 
                    expression_str, split_result, target_error, 0.4); // 更严格的relax_factor
        
        // 如果splitInterval未能有效拆分，强制多路拆分
        if (split_result.size() <= 1) {
            std::cout << "Regular split ineffective, forcing multi-way split" << std::endl;
            // 通过标签跳转到强制拆分代码
            goto force_split;
        } else {
            result = std::move(split_result);
            return;
        }
    }
    
    // 强制拆分标签点
force_split:
    result.clear(); // 确保结果为空
    
    // 均匀拆分为多个子区间
    for (int i = 0; i < min_split_count; ++i) {
        double sub_start = interval.start + i * (interval.end - interval.start) / min_split_count;
        double sub_end = interval.start + (i + 1) * (interval.end - interval.start) / min_split_count;
        
        // 确保子区间长度不小于最小单位长度
        if (sub_end - sub_start < adaptive_min_unit_length) continue;
        
        double mid_hessian = computeHessian(expression_str, (sub_start + sub_end) / 2.0);
        result.push_back({sub_start, sub_end, mid_hessian, interval.level + 1});
    }
    
    std::cout << "FORCED " << result.size() << "-way split for interval [" 
              << interval.start << ", " << interval.end << "]" << std::endl;
    
    // 确保至少有一个区间
    if (result.empty()) {
        std::cout << "WARNING: All forced splits failed, keeping original interval" << std::endl;
        result.push_back(interval);
    }
}

inline void identifyAndRefineHighErrorIntervals(std::vector<Interval>& intervals,
                                              double target_error,
                                              const std::string& expression_str,
                                              double min_unit_length) {
    if (intervals.empty()) return;
    
    std::cout << "Analyzing error distribution for " << intervals.size() << " intervals..." << std::endl;
    
    // Compute errors for all intervals
    std::vector<std::pair<size_t, double>> interval_errors;
    interval_errors.reserve(intervals.size());
    
    // Parallelize error computation for performance
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
            } catch (const std::exception& e) {
                std::lock_guard<std::mutex> lock(error_mutex);
                std::cerr << "Error estimating interval " << i << ": " << e.what() << std::endl;
                interval_errors.push_back({i, std::numeric_limits<double>::max()});
            }
        }));
    }
    
    // Wait for all computations to finish
    for (auto& future : futures) {
        future.wait();
    }
    
    // Sort by error for analysis
    std::sort(interval_errors.begin(), interval_errors.end(),
          [](const std::pair<size_t, double>& a, const std::pair<size_t, double>& b) { 
              return a.second < b.second; 
          });
    
    // Calculate error statistics
    const size_t n = interval_errors.size();
    double min_error = interval_errors.front().second;
    double max_error = interval_errors.back().second;
    
    // Calculate quartiles for outlier detection
    double q1 = interval_errors[n / 4].second;
    double median = interval_errors[n / 2].second;
    double q3 = interval_errors[3 * n / 4].second;
    double iqr = q3 - q1;
    
    // Identify potential outliers (high error intervals)
    // Using IQR method but make it configurable
    double outlier_threshold = q3 + 2.0 * iqr;  // More conservative than standard 1.5*IQR
    outlier_threshold = std::max(outlier_threshold, target_error * 5.0);  // Ensure minimum threshold
    
    // Get indices of intervals to refine
    std::vector<size_t> high_error_indices;
    for (auto& pair : interval_errors) {
        if (pair.second > outlier_threshold) {
            high_error_indices.push_back(pair.first);
        }
    }
    
    // Print error distribution and outlier information
    std::cout << "Error distribution: "
              << "min=" << min_error
              << ", Q1=" << q1
              << ", median=" << median
              << ", Q3=" << q3
              << ", max=" << max_error << std::endl;
    
    std::cout << "IQR=" << iqr 
              << ", outlier threshold=" << outlier_threshold 
              << " (" << outlier_threshold / target_error << "x target)" << std::endl;
    
    std::cout << "Found " << high_error_indices.size() 
              << " high-error intervals" << std::endl;
    
    // If no high error intervals are found, we're done
    if (high_error_indices.empty()) {
        std::cout << "No intervals exceed the error threshold." << std::endl;
        return;
    }
    
    // Now refine the high error intervals
    std::vector<Interval> refined_intervals;
    refined_intervals.reserve(intervals.size() + high_error_indices.size());
    
    // Set of indices to be refined (for faster lookup)
    std::unordered_set<size_t> indices_set(high_error_indices.begin(), high_error_indices.end());
    
    for (size_t i = 0; i < intervals.size(); ++i) {
        if (indices_set.find(i) != indices_set.end()) {
            // This is a high-error interval that needs refinement
            std::vector<Interval> split_results;
            
            // Intelligently split the interval based on error profile
            adaptiveSplitHighErrorInterval(intervals[i], target_error, min_unit_length, 
                                         expression_str, split_results);
            
            // Add the refined intervals
            refined_intervals.insert(refined_intervals.end(), split_results.begin(), split_results.end());
            
            std::cout << "Split interval [" << intervals[i].start << ", " << intervals[i].end 
                      << "] into " << split_results.size() << " sub-intervals" << std::endl;
        } else {
            // Keep this interval as is
            refined_intervals.push_back(intervals[i]);
        }
    }
    
    // Replace original intervals with refined ones
    intervals = std::move(refined_intervals);
    
    // Ensure no gaps between intervals
    ensureNoGaps(intervals);
    
    std::cout << "Refinement complete. New interval count: " << intervals.size() << std::endl;
}


// 辅助函数：创建成对合并区间（新增）
inline Interval createMergedPair(const Interval& iv1, const Interval& iv2,
                               const std::string& expression_str, double tol = 1e-9) 
{
    // 连续性检查
    if (std::abs(iv1.end - iv2.start) > tol) {
        std::ostringstream oss;
        oss << "Non-contiguous intervals: [" 
            << iv1.start << "," << iv1.end << "] vs ["
            << iv2.start << "," << iv2.end << "] (gap: "
            << iv2.start - iv1.end << ")";
        throw std::runtime_error(oss.str());
    }

    // 计算合并区间属性
    Interval merged;
    merged.start = iv1.start;
    merged.end = iv2.end;
    merged.level = std::max(iv1.level, iv2.level);
    
    // 计算合并后的Hessian值 - 使用缓存
    const double mid_point = (merged.start + merged.end) / 2.0;
    merged.hessian = computeHessian(expression_str, mid_point);

    return merged;
}

inline bool canMerge(const Interval& a, const Interval& b,
                     double target_error,
                     const std::string& expression_str,
                     double error_factor = 1.0) {
    // 快速预检查 - 尽早拒绝不适合合并的区间
    if (!quickMergeCheck(a, b, target_error, expression_str)) {
        return false;
    }
    
    // 创建合并区间
    Interval merged{
        a.start, 
        b.end, 
        0.0,  // 临时Hessian值
        std::max(a.level, b.level)
    };
    
    // 自适应误差因子
    double interval_length = merged.end - merged.start;
    double domain_length = 1.0; // 假定总长度为1
    double length_factor = std::min(2.0, 1.0 + interval_length/domain_length * 10.0);
    double adaptive_error_factor = error_factor * length_factor;
    
    // 快速线性评估
    const std::vector<double> key_points = {
        merged.start,
        merged.start + (merged.end - merged.start) * 0.25,
        (merged.start + merged.end) * 0.5,
        merged.end - (merged.end - merged.start) * 0.25,
        merged.end
    };
    
    double f_start = function_cache.get(expression_str, merged.start);
    double f_end = function_cache.get(expression_str, merged.end);
    double max_linear_error = 0.0;
    
    // 并行计算误差
    std::vector<double> errors(key_points.size() - 2);
    std::vector<std::future<void>> futures;
    
    for (size_t i = 1; i < key_points.size() - 1; ++i) {
        futures.push_back(std::async(std::launch::async, [&, i]() {
            double x = key_points[i];
            double actual = function_cache.get(expression_str, x);
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
    
    // 精确拟合评估
    try {
        FitParameters params = fitSegment(expression_str, merged, target_error);
        double fit_error = estimateSegmentError(expression_str, merged, params);
        return fit_error <= target_error * adaptive_error_factor;
    } catch (...) {
        return false;
    }
}

// 窗口合并检查函数 - 检查连续的多个区间是否可合并
inline bool canMergeWindow(const std::vector<Interval>& intervals, 
                          size_t start_idx,
                          size_t window_size,
                          double target_error,
                          const std::string& expression_str,
                          double error_factor = 1.0) {
    if (start_idx + window_size > intervals.size()) return false;
    
    // 快速预检查 - 使用端点估计
    const Interval& first = intervals[start_idx];
    const Interval& last = intervals[start_idx + window_size - 1];
    
    // 检查连续性 - 提前拒绝不连续的区间组合
    for (size_t i = start_idx; i < start_idx + window_size - 1; ++i) {
        if (std::abs(intervals[i].end - intervals[i+1].start) > 1e-9) {
            return false;
        }
    }
    
    // 大窗口优化: 先进行粗略评估
    if (window_size > 8) {
        double merged_length = last.end - first.start;
        double max_hessian_diff = 0.0;
        
        // 检测曲率变化是否剧烈
        for (size_t i = start_idx; i < start_idx + window_size - 1; ++i) {
            max_hessian_diff = std::max(max_hessian_diff, 
                                      std::abs(intervals[i].hessian - intervals[i+1].hessian));
        }
        
        // 曲率变化过大时直接拒绝
        if (max_hessian_diff > target_error * error_factor * 10.0) {
            return false;
        }
        
        // 稀疏采样快速线性检查
        std::vector<double> sample_points = {
            first.start,
            first.start + merged_length * 0.25,
            first.start + merged_length * 0.5,
            first.start + merged_length * 0.75,
            last.end
        };
        
        double f_start = function_cache.get(expression_str, first.start);
        double f_end = function_cache.get(expression_str, last.end);
        double max_linear_error = 0.0;
        
        // 并行计算误差
        std::vector<double> errors(3);
        std::vector<std::future<void>> futures;
        
        for (size_t i = 1; i < 4; ++i) {
            futures.push_back(std::async(std::launch::async, [&, i]() {
                double x = sample_points[i];
                double actual = function_cache.get(expression_str, x);
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
        
        // 线性误差远超目标，直接拒绝
        if (max_linear_error > target_error * error_factor * 2.0) {
            return false;
        }
    }
    
    // 创建合并区间
    Interval merged{
        first.start,
        last.end,
        0.0,
        0
    };
    
    // 继承最高level
    for (size_t i = start_idx; i < start_idx + window_size; ++i) {
        merged.level = std::max(merged.level, intervals[i].level);
    }
    
    // 根据窗口大小调整采样点数量
    int sample_points = std::min(20, std::max(5, static_cast<int>(window_size) * 2));
    
    // 精确拟合评估
    try {
        FitParameters params = fitSegment(expression_str, merged, target_error);
        double fit_error = estimateSegmentError(expression_str, merged, params);
        return fit_error <= target_error * error_factor;
    } catch (...) {
        return false;
    }
}

inline void mergeIntervals(std::vector<Interval>& intervals, 
                        double epsilon,
                        double target_error,
                        const std::string& expression_str,
                        const MergeParams& params,
                        double min_unit_length,
                        double merge_relax_factor = 1.0) 
{
    if (intervals.empty()) return;
    const double domain_start = intervals.front().start;
    const double domain_end = intervals.back().end;

    //================= 边界保护常量 =================//
    const double BOUNDARY_SAFETY_MARGIN = std::max(1e-9, target_error * 10);
    constexpr double MIN_LEN_REL = 1e-6;
    constexpr double MIN_LEN_ABS = 1e-12;
    
    // 更激进的合并策略配置
    constexpr int MAX_MERGE_ITERATIONS = 20;  // 增加迭代轮数
    constexpr size_t MAX_WINDOW_SIZE = 128;   // 更大的窗口尺寸
    const double BASE_ERROR_FACTOR = 4.0;     // 大幅增加基础误差放大因子
    const double FINAL_PASS_ERROR_FACTOR = 2.5;
    const double VERIFICATION_ERROR_FACTOR = 3.0; // 最终迭代使用更大的因子

    const double CONTINUITY_THRESHOLD = 1e-12;
    
    // 误差控制相关变量
    double prev_max_error = 0.0;
    double prev_avg_error = 0.0;
    bool early_termination = false;

    const double MAX_ERROR_INCREASE_RATIO = 5.0;    // 允许的最大误差增长比例
    const double MAX_ERROR_THRESHOLD = 6.0;         // 最大误差可接受的目标误差倍数
    const double AVG_ERROR_THRESHOLD = 90.0;        // 平均误差可接受的目标误差倍数
    const double ORDER_MAGNITUDE_THRESHOLD = 0.9;   // 数量级变化阈值

    const double HIGH_ERROR_THRESHOLD = 2.0;  // 降低高误差区间判定阈值，从4.0降至2.0
    const bool INITIAL_REFINEMENT = true;     // 是否在合并前先进行一次细化
    const int REFINEMENT_FREQ = 1;            // 提高细化频率，每2次迭代进行一次，而不是3次

    size_t previous_interval_count = intervals.size();
    int unchanged_count = 0;
    const int MAX_UNCHANGED_COUNT = 3;  // 连续结果相同的最大次数
    std::vector<Interval> best_intervals = intervals;  // 保存最佳结果
    bool early_stable_termination = false;
    
    auto isValidInterval = [&](const Interval& iv) {
        const double abs_len = iv.end - iv.start;
        const double rel_len = abs_len / (domain_end - domain_start);
        return (abs_len > MIN_LEN_ABS) && 
               (rel_len > MIN_LEN_REL) &&
               (iv.start >= domain_start - BOUNDARY_SAFETY_MARGIN) &&
               (iv.end <= domain_end + BOUNDARY_SAFETY_MARGIN);
    };

    // 预热缓存，提高后续计算性能
    std::cout << "Warming up function cache for interval merging..." << std::endl;
    size_t cache_points = std::min(size_t(10000), intervals.size() * 10);
    function_cache.warmup(expression_str, domain_start, domain_end, cache_points);

    // 预处理：移除无效区间并按起点排序
    intervals.erase(std::remove_if(intervals.begin(), intervals.end(),
        [&](const Interval& iv) { return !isValidInterval(iv); }), intervals.end());
    
    std::sort(intervals.begin(), intervals.end(),
        [](const Interval& a, const Interval& b) { return a.start < b.start; });
    
    // 确保第一个区间从domain_start开始
    if (!intervals.empty() && intervals.front().start > domain_start) {
        intervals.front().start = domain_start;
    }
    
    // 确保最后一个区间到domain_end结束
    if (!intervals.empty() && intervals.back().end < domain_end) {
        intervals.back().end = domain_end;
    }

    // 修复初始间隙
    for (size_t i = 0; i < intervals.size() - 1; i++) {
        if (intervals[i+1].start > intervals[i].end + 1e-10) {
            // 发现间隙，将前一个区间延伸到下一个区间的起点
            intervals[i].end = intervals[i+1].start;
        }
    }

    if (INITIAL_REFINEMENT) {
        std::cout << "\n--- Initial high error interval refinement ---" << std::endl;
        
        // 计算初始误差统计
        double initial_max_error = 0.0;
        double initial_sum_error = 0.0;
        int valid_count = 0;
        
        for (const auto& iv : intervals) {
            try {
                FitParameters params = fitSegment(expression_str, iv, target_error);
                double error = estimateSegmentError(expression_str, iv, params);
                initial_max_error = std::max(initial_max_error, error);
                initial_sum_error += error;
                valid_count++;
            } catch (...) {
                // 忽略拟合错误
            }
        }
        
        double initial_avg_error = valid_count > 0 ? initial_sum_error / valid_count : 0.0;
        
        std::cout << "Initial error statistics: max=" << std::scientific 
                  << initial_max_error << ", avg=" << initial_avg_error 
                  << " (target=" << target_error << ")" << std::endl;
        
        if (initial_max_error > target_error * HIGH_ERROR_THRESHOLD) {
            size_t before_refine = intervals.size();
            
            // 执行一次高误差区间细化
            identifyAndRefineHighErrorIntervals(intervals, 
                                              target_error, 
                                              expression_str, 
                                              min_unit_length);
            
            size_t after_refine = intervals.size();
            if (after_refine > before_refine) {
                std::cout << "Initial refinement: " << after_refine - before_refine 
                          << " high error intervals refined" << std::endl;
                
                // 确保区间连续性
                ensureNoGaps(intervals);
                
                // 重新计算Hessian值
                for (auto& iv : intervals) {
                    iv.hessian = computeHessian(expression_str, (iv.start + iv.end) / 2.0);
                }
            }
        }
    }

    bool merged_flag;
    int merge_pass = 0;
    
    do {
        merged_flag = false;
        std::vector<Interval> merged_intervals;
        merged_intervals.reserve(intervals.size());
        
        // 根据当前迭代选择误差因子，每次迭代中根据进度动态调整误差因子
        double progress_ratio = (double)merge_pass / MAX_MERGE_ITERATIONS;
        // 改为更缓慢的线性衰减, 从BASE_ERROR_FACTOR衰减到FINAL_PASS_ERROR_FACTOR
        double current_error_factor = BASE_ERROR_FACTOR - 
                                    (BASE_ERROR_FACTOR - FINAL_PASS_ERROR_FACTOR) * progress_ratio;
        
        std::cout << "Pass " << merge_pass << " using error factor: " << current_error_factor << std::endl;
        
        size_t i = 0;
        while (i < intervals.size()) {
            // 处理最后一个区间或边界区域特殊情况
            if (i == intervals.size() - 1) {
                Interval last_iv = intervals[i];
                last_iv.end = domain_end;  // 强制最后一个区间到域末端
                if (isValidInterval(last_iv)) {
                    merged_intervals.push_back(last_iv);
                }
                i++;
                continue;
            }

            // 区间间隙检查和修复
            if (i < intervals.size() - 1 && 
                intervals[i+1].start > intervals[i].end + CONTINUITY_THRESHOLD) {
                // 发现间隙，延伸当前区间
                Interval extended = intervals[i];
                extended.end = intervals[i+1].start;
                merged_intervals.push_back(extended);
                i++;
                continue;
            }
            
            // 寻找最大可合并窗口
            size_t best_window = 1;
            Interval best_merged = intervals[i];

            // 添加智能窗口选择策略 - 根据区间特性选择合适窗口大小
            size_t max_possible_window = intervals.size() - i;  // 最大可能窗口大小
            double avg_hessian = 0.0;
            for (size_t j = 0; j < std::min(size_t(4), max_possible_window); j++) {
                avg_hessian += std::abs(intervals[i+j].hessian);
            }
            avg_hessian /= std::min(size_t(4), max_possible_window);
            
            // 基于曲率选择初始窗口尺寸
            std::vector<size_t> window_sizes;
            if (avg_hessian < target_error * 10) {
                // 曲率小，尝试更大窗口
                window_sizes = {64, 32, 16, 8, 4, 2};
            } else if (avg_hessian < target_error * 100) {
                // 中等曲率
                window_sizes = {32, 16, 8, 4, 2};
            } else {
                // 高曲率，尝试小窗口
                window_sizes = {8, 4, 2};
            }
            
            // Parallelize window merge checks
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
                
                // 限制并行度
                if (merge_results.size() >= std::thread::hardware_concurrency()) {
                    break;
                }
            }
            
            // 收集结果
            for (auto& future : merge_results) {
                auto result = future.get();
                bool can_merge = result.first;
                auto& window = result.second;
                if (can_merge && window > best_window) {
                    best_window = window;
                }
            }
            
            // 如果发现了可合并窗口，通过二分查找优化窗口大小
            if (best_window > 1) {
                size_t low = best_window + 1;
                size_t high = std::min(MAX_WINDOW_SIZE, intervals.size() - i);
                
                while (low <= high && high - low > 4) {
                    size_t mid = low + (high - low) / 2;
                    
                    if (canMergeWindow(intervals, i, mid, target_error, 
                                     expression_str, current_error_factor)) {
                        // 可以合并更大窗口
                        best_window = mid;
                        low = mid + 1;
                    } else {
                        // 不能合并这个窗口大小
                        high = mid - 1;
                    }
                }
                
                // 应用最佳合并结果
                try {
                    // 使用createMergedPair构建最终合并区间
                    Interval final_merged = intervals[i];
                    for (size_t j = 1; j < best_window; j++) {
                        final_merged = createMergedPair(
                            final_merged, intervals[i+j], expression_str);
                    }
                    // 确保与下一个区间无间隙
                    if (i + best_window < intervals.size()) {
                        if (final_merged.end < intervals[i + best_window].start) {
                            final_merged.end = intervals[i + best_window].start;
                        }
                    }

                    FitParameters verified_params = fitSegment(expression_str, final_merged, target_error);
                    double verified_error = estimateSegmentError(expression_str, final_merged, verified_params);
                    
                    if (verified_error <= target_error * VERIFICATION_ERROR_FACTOR) {
                        merged_intervals.push_back(final_merged);
                        i += best_window;
                        merged_flag = true;
                        
                        // if (best_window >= 8) {
                        //     std::cout << "Merged " << best_window << " intervals, error="
                        //            << verified_error / target_error << "x" << std::endl;
                        // }
                    } 
                    else if (best_window > 4) {
                        // 验证失败时尝试减半窗口
                        size_t half_window = best_window / 2;
                        
                        if (canMergeWindow(intervals, i, half_window, target_error, 
                                         expression_str, current_error_factor)) {
                            try {
                                Interval half_merged = intervals[i];
                                for (size_t j = 1; j < half_window; j++) {
                                    half_merged = createMergedPair(
                                        half_merged, intervals[i+j], expression_str);
                                }
                                
                                // 确保无间隙
                                if (i + half_window < intervals.size()) {
                                    if (half_merged.end < intervals[i + half_window].start) {
                                        half_merged.end = intervals[i + half_window].start;
                                    }
                                }
                                
                                FitParameters half_params = fitSegment(expression_str, half_merged, target_error);
                                double half_error = estimateSegmentError(expression_str, half_merged, half_params);
                                
                                if (half_error <= target_error * VERIFICATION_ERROR_FACTOR) {
                                    merged_intervals.push_back(half_merged);
                                    i += half_window;
                                    merged_flag = true;
                                    // std::cout << "Half merge: " << half_window << " intervals, error="
                                    //         << half_error / target_error << "x" << std::endl;
                                } else {
                                    // 回退到保守合并策略 - 仅合并2-3个区间
                                    size_t safe_window = std::min(size_t(3), max_possible_window);
                                    if (safe_window >= 2 && 
                                        canMergeWindow(intervals, i, safe_window, target_error, 
                                                    expression_str, current_error_factor)) {
                                        Interval safe_merged = intervals[i];
                                        for (size_t j = 1; j < safe_window; j++) {
                                            safe_merged = createMergedPair(
                                                safe_merged, intervals[i+j], expression_str);
                                        }
                                        
                                        // 确保无间隙
                                        if (i + safe_window < intervals.size()) {
                                            if (safe_merged.end < intervals[i + safe_window].start) {
                                                safe_merged.end = intervals[i + safe_window].start;
                                            }
                                        }
                                        
                                        merged_intervals.push_back(safe_merged);
                                        i += safe_window;
                                        merged_flag = true;
                                    } else {
                                        // 保留原区间，但确保无间隙
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
                            } catch (const std::exception&) {
                                merged_intervals.push_back(intervals[i]);
                                i++;
                            }
                        } else {
                            merged_intervals.push_back(intervals[i]);
                            i++;
                        }
                    } else {
                        merged_intervals.push_back(intervals[i]);
                        i++;
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Error creating merged interval: " << e.what() << std::endl;
                    merged_intervals.push_back(intervals[i]);
                    i++;
                }
            } 
            // 尝试合并相邻两个区间
            else if (i + 1 < intervals.size() && 
                    canMerge(intervals[i], intervals[i+1], target_error, 
                           expression_str, current_error_factor * 1.5)) {
                try {
                    Interval merged_iv = createMergedPair(intervals[i], intervals[i+1], expression_str);
                    
                    // 确保无间隙
                    if (i + 2 < intervals.size()) {
                        if (merged_iv.end < intervals[i + 2].start) {
                            merged_iv.end = intervals[i + 2].start;
                        }
                    }
                    
                    merged_intervals.push_back(merged_iv);
                    i += 2;
                    merged_flag = true;
                } catch (const std::exception& e) {
                    std::cerr << "Error merging pair: " << e.what() << std::endl;
                    merged_intervals.push_back(intervals[i]);
                    i++;
                }
            } else {
                // 不可合并，保持原区间但确保无间隙
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
        
        // 保存合并前大小以计算减少率
        size_t before_size = intervals.size();
        
        // 验证合并后的区间
        auto new_end = std::remove_if(merged_intervals.begin(), merged_intervals.end(),
            [&](const Interval& iv) { 
                return !isValidInterval(iv) || iv.start >= iv.end || 
                       std::isnan(iv.start) || std::isnan(iv.end);
            });
        merged_intervals.erase(new_end, merged_intervals.end());

        // 打印当前迭代结果
        size_t after_size = merged_intervals.size();
        double reduction = 100.0 * (1.0 - (double)after_size / before_size);
        
        const int precision = std::max(3, static_cast<int>(-std::log10(target_error)) + 1);
        std::cout << "\nPass " << merge_pass << " length distribution (intervals: " 
                  << after_size << "/" << before_size << ", reduction: " 
                  << std::fixed << std::setprecision(2) << reduction << "%)\n";
        printDistribution(merged_intervals, precision);
        
        // 计算并输出当前pass的误差统计
        double max_error_abs = 0.0;
        double avg_error_abs = 0.0;
        double error_sum = 0.0;
        int valid_intervals = 0;
        std::vector<double> all_errors;
        all_errors.reserve(merged_intervals.size());

        for (const auto& iv : merged_intervals) {
            try {
                FitParameters params = fitSegment(expression_str, iv, target_error);
                double interval_error = estimateSegmentError(expression_str, iv, params);
                
                max_error_abs = std::max(max_error_abs, interval_error);
                error_sum += interval_error;
                all_errors.push_back(interval_error);
                valid_intervals++;
            } catch (const std::exception& e) {
                std::cerr << "Error computing error for interval [" 
                        << iv.start << ", " << iv.end << "]: " << e.what() << std::endl;
            }
        }

        if (valid_intervals > 0) {
            avg_error_abs = error_sum / valid_intervals;
            
            // 计算中位数误差
            std::sort(all_errors.begin(), all_errors.end());
            double median_error = (valid_intervals % 2 == 0) ?
                (all_errors[valid_intervals/2 - 1] + all_errors[valid_intervals/2]) / 2.0 :
                all_errors[valid_intervals/2];
            
            // 计算90%分位数误差
            size_t p90_idx = static_cast<size_t>(valid_intervals * 0.9);
            double p90_error = all_errors[std::min(p90_idx, all_errors.size() - 1)];
            
            // 输出绝对误差值
            std::cout << "Pass " << merge_pass << " absolute errors: "
                    << " avg=" << std::scientific << std::setprecision(6) << avg_error_abs
                    << ", median=" << std::scientific << std::setprecision(6) << median_error
                    << ", p90=" << std::scientific << std::setprecision(6) << p90_error
                    << ", max=" << std::scientific << std::setprecision(6) << max_error_abs
                    << std::endl;
                    
            // 输出相对误差（与目标误差的比值）
            std::cout << "Pass " << merge_pass << " relative errors: "
                    << " avg=" << std::fixed << std::setprecision(6) << avg_error_abs / target_error << "x"
                    << ", median=" << std::fixed << std::setprecision(6) << median_error / target_error << "x"
                    << ", p90=" << std::fixed << std::setprecision(6) << p90_error / target_error << "x"
                    << ", max=" << std::fixed << std::setprecision(6) << max_error_abs / target_error << "x"
                    << " (target=" << std::scientific << std::setprecision(6) << target_error << ")"
                    << std::endl;
            
            // 添加智能终止判断
            if (merge_pass > 0) {  // 第一轮没有前一轮数据，跳过检查
                // 检查误差增长率
                double max_error_increase = max_error_abs / prev_max_error;
                double avg_error_increase = avg_error_abs / prev_avg_error;
                
                // 计算是否接近数量级变化 - 当平均误差与目标误差比值接近10的整数幂时
                bool approaching_order_magnitude = false;
                double error_ratio = avg_error_abs / target_error;
                double log10_ratio = std::log10(error_ratio);
                double next_power = std::ceil(log10_ratio);
                approaching_order_magnitude = (next_power - log10_ratio < ORDER_MAGNITUDE_THRESHOLD);
                
                // 修改提前终止判断逻辑，以平均误差为主要判断依据

                // 检查是否应该提前终止 - 以平均误差为主要判断依据
                if ((avg_error_abs / target_error > AVG_ERROR_THRESHOLD) ||  // 真正使用AVG_ERROR_THRESHOLD作为主要条件
                    (approaching_order_magnitude && avg_error_increase > 1.8 && 
                    avg_error_abs / target_error > AVG_ERROR_THRESHOLD * 0.5) ||  // 平均误差增长过快且接近数量级变化
                    (avg_error_increase > MAX_ERROR_INCREASE_RATIO * 0.8 && 
                    avg_error_abs / target_error > AVG_ERROR_THRESHOLD * 0.3) ||  // 平均误差增长率过高
                    (max_error_abs / target_error > MAX_ERROR_THRESHOLD * 1.5 && 
                    avg_error_abs / target_error > AVG_ERROR_THRESHOLD * 0.2)) {  // 最大误差作为辅助条件，但需要平均误差也达到一定水平
    
                    std::cout << "\n*** EARLY TERMINATION CHECK: Error approaching threshold ***" << std::endl;
                    std::cout << "Current average error ratio: " << std::fixed << avg_error_abs / target_error 
                            << "x target (" << avg_error_abs / target_error / AVG_ERROR_THRESHOLD * 100 << "% of threshold)" << std::endl;
                    std::cout << "Current maximum error ratio: " << std::fixed << max_error_abs / target_error << "x target" << std::endl;
                    std::cout << "Distance to next order: " << next_power - log10_ratio << std::endl;
                    std::cout << "Avg error increase: " << std::fixed << std::setprecision(2) << avg_error_increase 
                            << "x, Max error increase: " << max_error_increase << "x" << std::endl;
    
                    // 如果是第一次出现误差超标，警告但继续；第二次则终止
                    if (early_termination) {
                        std::cout << "Final warning: Stopping merge process to prevent error explosion" << std::endl;
                        break;  // 跳出do-while循环
                    }
                    early_termination = true;
                    std::cout << "First warning: Will continue one more pass" << std::endl;
                }
            }
            
            // 保存当前误差供下一轮使用
            prev_max_error = max_error_abs;
            prev_avg_error = avg_error_abs;
        }

        // 最关键的步骤：检查并修复间隙
        if (!merged_intervals.empty()) {
            // 确保第一个区间从domain_start开始
            merged_intervals.front().start = domain_start;
            
            // 检查并修复中间所有间隙
            for (size_t j = 0; j < merged_intervals.size() - 1; j++) {
                if (merged_intervals[j+1].start > merged_intervals[j].end + CONTINUITY_THRESHOLD) {
                    // 发现间隙，将前一个区间延伸到下一个区间的起点
                    std::cout << "Fixing gap between " << merged_intervals[j].end 
                              << " and " << merged_intervals[j+1].start << std::endl;
                    merged_intervals[j].end = merged_intervals[j+1].start;
                }
            }
            
            // 确保最后一个区间到domain_end结束
            merged_intervals.back().end = domain_end;
        }
        
        // 检查是否有进一步合并
        merged_flag = merged_flag && (merged_intervals.size() < intervals.size());
        intervals.swap(merged_intervals);
        merge_pass++;

        // 检查是否合并结果已经稳定（连续三次相同）
        if (intervals.size() == previous_interval_count) {
            unchanged_count++;
            std::cout << "Result unchanged for " << unchanged_count << " consecutive passes" << std::endl;
            if (unchanged_count >= MAX_UNCHANGED_COUNT) {
                std::cout << "\n*** EARLY STABLE TERMINATION: Result unchanged for " 
                          << unchanged_count << " consecutive passes ***" << std::endl;
                early_stable_termination = true;
                break;  // 结果稳定，提前终止
            }
        } else {
            // 结果有变化，重置计数器
            unchanged_count = 0;
            
            // 如果当前结果比之前更好（区间更少），保存为最佳结果
            if (intervals.size() < best_intervals.size()) {
                std::cout << "New best result: " << intervals.size() << " intervals (previous: " 
                          << best_intervals.size() << ")" << std::endl;
                best_intervals = intervals;
            }
        }
        
        // 更新前一次的区间数量
        previous_interval_count = intervals.size();

        // 处理高误差区间 - 每隔几次迭代执行一次
        if (merge_pass > 0 && merge_pass % REFINEMENT_FREQ == 0 && !early_termination) {
            std::cout << "\n--- Identifying and refining high error intervals... ---" << std::endl;
            
            // 计算当前误差统计
            double current_max_error = 0.0;
            for (const auto& iv : intervals) {
                try {
                    FitParameters params = fitSegment(expression_str, iv, target_error);
                    double error = estimateSegmentError(expression_str, iv, params);
                    current_max_error = std::max(current_max_error, error);
                } catch (...) {
                    // 忽略拟合错误
                }
            }
            
            // 只有在误差超过阈值时才执行优化
            if (current_max_error > target_error * 4.0) {
                std::cout << "Maximum error (" << current_max_error << ") exceeds threshold, "
                        << "refining problematic intervals..." << std::endl;
                
                // 保存现有区间数量以计算增长
                size_t before_refine = intervals.size();
                
                // 调用区间优化函数
                identifyAndRefineHighErrorIntervals(intervals, 
                                                target_error, 
                                                expression_str, 
                                                min_unit_length);
                
                // 报告结果
                size_t after_refine = intervals.size();
                if (after_refine > before_refine) {
                    std::cout << "Refined " << after_refine - before_refine 
                            << " high error intervals" << std::endl;
                    
                    // 确保区间连续性
                    ensureNoGaps(intervals);
                    
                    // 重新计算Hessian值
                    for (auto& iv : intervals) {
                        iv.hessian = computeHessian(expression_str, (iv.start + iv.end) / 2.0);
                    }
                } else {
                    std::cout << "No intervals were refined" << std::endl;
                }
            }
        }
        
        // 避免无谓的最后一次迭代
        if (intervals.size() <= 1) break;
    } while (merged_flag && merge_pass < MAX_MERGE_ITERATIONS && !early_termination && !early_stable_termination);
    
    // 如果提前终止但有更好的结果，使用最佳结果
    if ((early_termination || early_stable_termination) && best_intervals.size() < intervals.size()) {
        std::cout << "Using best result with " << best_intervals.size() 
                  << " intervals instead of current " << intervals.size() << std::endl;
        intervals = best_intervals;
    }

    // 最终合并数据输出
    std::cout << "\nFinal Distribution (precision 1e-" 
              << std::max(3, static_cast<int>(-std::log10(target_error)) + 1) 
              << "):\n";
    printDistribution(intervals, 
        std::max(3, static_cast<int>(-std::log10(target_error)) + 1));
    
    std::cout << "Merged intervals: " << intervals.size() << std::endl;
    
    // 最终一次确保无间隙
    for (size_t i = 0; i < intervals.size() - 1; i++) {
        if (intervals[i+1].start > intervals[i].end + CONTINUITY_THRESHOLD) {
            intervals[i].end = intervals[i+1].start;
        }
    }
    
    // 确保首尾覆盖
    if (!intervals.empty()) {
        intervals.front().start = domain_start;
        intervals.back().end = domain_end;
    }
    
    checkCoverage(intervals, domain_start, domain_end);
    
    // 输出函数缓存统计
    function_cache.printStats();
}

// 辅助函数：创建合并后的区间（带异常处理）
Interval createMergedInterval(double start, double end) {
    if (start >= end) {
        throw std::invalid_argument("Invalid merged interval range");
    }
    return Interval{start, end, /* hessian */0.0, /* level */0};
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

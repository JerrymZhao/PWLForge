#ifndef INTERVAL_OPTIMIZER_HPP
#define INTERVAL_OPTIMIZER_HPP

#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <iomanip>
#include <cmath>
#include "common_utils.hpp"
#include "interval_types.hpp"
#include "interval_split.hpp"
#include "interval_merge.hpp"

// ============================================================================
// Quality Constraint
// ============================================================================

struct QualityConstraint {
    enum class Mode {
        STRICT_PER_INTERVAL,    // Every interval MAE ≤ threshold
        RELAXED_AVERAGE         // Average MAE ≤ threshold
    };

    Mode mode;
    double threshold;
    double max_spike_factor;  // Max spike tolerance

    static QualityConstraint strict(double threshold) {
        return QualityConstraint{Mode::STRICT_PER_INTERVAL, threshold, 2.0};
    }

    static QualityConstraint relaxed(double threshold, double spike_factor = 10.0) {
        return QualityConstraint{Mode::RELAXED_AVERAGE, threshold, spike_factor};
    }
};

struct ErrorMetrics {
    double worst_interval_mae = 0.0;    // max(MAE_i)
    double avg_interval_mae = 0.0;      // average(MAE_i)
    double worst_max_error = 0.0;       // max(max_error_i)
    double global_mae = 0.0;            // Global MAE

    double global_mse = 0.0;            // Mean Squared Error
    double global_rmse = 0.0;           // Root Mean Squared Error
    double worst_interval_mse = 0.0;    // Worst MSE among intervals
    double avg_interval_mse = 0.0;      // Average MSE

    size_t violations = 0;
    double worst_excess = 0.0;

    bool satisfies(const QualityConstraint& constraint) const {
        switch (constraint.mode) {
            case QualityConstraint::Mode::STRICT_PER_INTERVAL:
                return worst_interval_mae <= constraint.threshold;

            case QualityConstraint::Mode::RELAXED_AVERAGE:
                return (avg_interval_mae <= constraint.threshold) &&
                       (worst_max_error <= constraint.threshold * constraint.max_spike_factor);
        }
        return false;
    }
};

// ============================================================================
// Error Metrics Computation
// ============================================================================

inline ErrorMetrics computeErrorMetrics(const std::vector<Interval>& intervals,
                                       const std::string& expression_str,
                                       double base_threshold) {
    ErrorMetrics metrics;
    double sum_interval_mae = 0.0;
    double sum_interval_mse = 0.0;
    double sum_global_abs_error = 0.0;
    double sum_global_squared_error = 0.0;
    size_t total_samples = 0;

    for (const auto& interval : intervals) {
        IntervalMetrics im = calculateMetrics(interval, expression_str, 50);

        metrics.worst_interval_mae = std::max(metrics.worst_interval_mae, im.avg_abs_error);
        sum_interval_mae += im.avg_abs_error;
        metrics.worst_max_error = std::max(metrics.worst_max_error, im.max_abs_error);

        double interval_mse = 0.0;
        double step = (interval.end - interval.start) / 49.0;

        for (int i = 0; i < 50; ++i) {
            double x = interval.start + i * step;
            double true_val = computeFunctionValue(expression_str, x);

            double f_start = computeFunctionValue(expression_str, interval.start);
            double f_end = computeFunctionValue(expression_str, interval.end);
            double approx = f_start + (x - interval.start) *
                           (f_end - f_start) / (interval.end - interval.start);

            double error = true_val - approx;
            double abs_error = std::abs(error);
            double squared_error = error * error;

            sum_global_abs_error += abs_error;
            sum_global_squared_error += squared_error;

            interval_mse += squared_error;
            total_samples++;
        }

        interval_mse /= 50.0;
        sum_interval_mse += interval_mse;
        metrics.worst_interval_mse = std::max(metrics.worst_interval_mse, interval_mse);

        if (im.avg_abs_error > base_threshold) {
            metrics.violations++;
            metrics.worst_excess = std::max(metrics.worst_excess,
                                           im.avg_abs_error - base_threshold);
        }
    }

    size_t num_intervals = std::max(size_t(1), intervals.size());
    metrics.avg_interval_mae = sum_interval_mae / num_intervals;
    metrics.avg_interval_mse = sum_interval_mse / num_intervals;

    size_t num_samples = std::max(size_t(1), total_samples);
    metrics.global_mae = sum_global_abs_error / num_samples;
    metrics.global_mse = sum_global_squared_error / num_samples;
    metrics.global_rmse = std::sqrt(metrics.global_mse);

    return metrics;
}

// ============================================================================
// Initial Interval Generation
// ============================================================================

inline std::vector<Interval> generateInitialIntervals(double start,
                                                      double end,
                                                      size_t num_points,
                                                      double initial_unit_length,
                                                      const std::string& expression_str,
                                                      const OptimizationConfig& config) {
    if (start >= end || num_points < 2) {
        throw std::invalid_argument("Invalid range or points");
    }

    std::vector<Interval> intervals;
    intervals.reserve(num_points);

    double step = (end - start) / static_cast<double>(num_points);

    for (size_t i = 0; i < num_points; ++i) {
        double current_start = start + i * step;
        double current_end = (i == num_points - 1) ? end : start + (i + 1) * step;

        double mid = (current_start + current_end) / 2.0;
        double hessian = computeHessian(expression_str, mid);

        intervals.push_back(Interval{current_start, current_end, hessian, 0});
    }

    return intervals;
}

// ============================================================================
// Phase A: Aggressive initial splitting
// ============================================================================

inline std::vector<Interval> runPhaseA_Strict(
    double start,
    double end,
    const std::string& expression_str,
    const FittingParametersConfig& config) {

    std::cout << "\n[Phase A] Strict (every MAE <= "
              << std::scientific << std::setprecision(2)
              << config.acceptable_error << ")\n";

    QualityConstraint strict = QualityConstraint::strict(config.acceptable_error);

    // Start with fewer intervals
    size_t num_points = static_cast<size_t>((end - start) / config.min_unit_length);
    num_points = std::max(size_t(40), std::min(num_points, size_t(60)));

    double initial_unit_length = (end - start) / num_points;
    OptimizationConfig opt_config;
    opt_config.target_error = config.acceptable_error;

    std::vector<Interval> intervals = generateInitialIntervals(
        start, end, num_points, initial_unit_length, expression_str, opt_config);

    const int MAX_ITER = 50;
    int iter = 0;

    while (iter < MAX_ITER) {
        ErrorMetrics metrics = computeErrorMetrics(intervals, expression_str,
                                                   config.acceptable_error);

        if (metrics.satisfies(strict)) {
            double conservativeness = config.acceptable_error / metrics.avg_interval_mae;
            if (conservativeness > 5.0) {
                std::cout << "  Warning: over-conservative (margin: "
                          << std::fixed << std::setprecision(1)
                          << conservativeness << "x)\n";
            }
            break;
        }

        std::vector<Interval> new_intervals;
        size_t split_count = 0;

        for (const auto& interval : intervals) {
            IntervalMetrics im = calculateMetrics(interval, expression_str, 100);

            // Adaptive split tolerance based on current margin and iteration
            double current_margin = config.acceptable_error / metrics.avg_interval_mae;

            // Two-phase strategy
            // Early iterations: Use relaxed tolerance to reduce intervals
            // Late iterations: Tighten to ensure convergence
            double base_tolerance;

            if (current_margin < 1.2) {
                // Critical: very close to threshold
                base_tolerance = 1.2;
            } else if (current_margin < 1.5) {
                // Tight: close to threshold
                base_tolerance = 1.3;
            } else if (current_margin < 2.0) {
                // Moderate: some room
                base_tolerance = 1.4;
            } else if (current_margin < 3.0) {
                // Comfortable: more room
                base_tolerance = 1.5;
            } else if (current_margin < 5.0) {
                // Relaxed: lots of room
                base_tolerance = 1.4;
            } else if (current_margin < 8.0) {
                // Very relaxed
                base_tolerance = 1.3;
            } else {
                // Extremely relaxed
                base_tolerance = 1.2;
            }

            // Apply phase multiplier
            double split_tolerance;
            if (iter < 15) {
                // Early phase: more aggressive
                split_tolerance = base_tolerance * 1.3;
            } else if (iter < 25) {
                // Mid phase: moderate
                split_tolerance = base_tolerance * 1.15;
            } else {
                // Late phase: strict
                split_tolerance = base_tolerance;
            }

            bool needs_split = im.avg_abs_error > config.acceptable_error * split_tolerance;
            bool can_split = (interval.end - interval.start) > config.min_unit_length * 2;

            if (needs_split && can_split) {
                double length = interval.end - interval.start;

                // 3-way split for extreme cases
                if (im.avg_abs_error > config.acceptable_error * 5.0 &&
                    (interval.end - interval.start) > config.min_unit_length * 3) {

                    double one_third = interval.start + length / 3.0;
                    double two_third = interval.start + 2.0 * length / 3.0;

                    double hess1 = computeHessian(expression_str, (interval.start + one_third) / 2.0);
                    double hess2 = computeHessian(expression_str, (one_third + two_third) / 2.0);
                    double hess3 = computeHessian(expression_str, (two_third + interval.end) / 2.0);

                    new_intervals.push_back(Interval{interval.start, one_third, hess1, interval.level + 1});
                    new_intervals.push_back(Interval{one_third, two_third, hess2, interval.level + 1});
                    new_intervals.push_back(Interval{two_third, interval.end, hess3, interval.level + 1});
                    split_count++;
                } else {
                    double mid = (interval.start + interval.end) / 2.0;
                    double hess1 = computeHessian(expression_str, (interval.start + mid) / 2.0);
                    double hess2 = computeHessian(expression_str, (mid + interval.end) / 2.0);

                    new_intervals.push_back(Interval{interval.start, mid, hess1, interval.level + 1});
                    new_intervals.push_back(Interval{mid, interval.end, hess2, interval.level + 1});
                    split_count++;
                }
            } else {
                new_intervals.push_back(interval);
            }
        }

        if (split_count == 0) break;

        intervals = new_intervals;
        iter++;
    }

    ErrorMetrics final = computeErrorMetrics(intervals, expression_str, config.acceptable_error);
    std::cout << "  Result: " << intervals.size() << " intervals\n";
    std::cout << "  WorstMAE=" << std::scientific << std::setprecision(2) << final.worst_interval_mae
            << " AvgMAE=" << final.avg_interval_mae
            << " GlobalMAE=" << final.global_mae << "\n";
    std::cout << "  WorstMSE=" << final.worst_interval_mse
            << " AvgMSE=" << final.avg_interval_mse
            << " GlobalMSE=" << final.global_mse << "\n";
    std::cout << "  RMSE=" << final.global_rmse
            << " MaxErr=" << final.worst_max_error << "\n";

    return intervals;
}

// ============================================================================
// Phase B: Enhanced merging with higher spikes (SIMPLIFIED OUTPUT)
// ============================================================================

inline std::vector<Interval> runPhaseB_Relaxed(
    double start,
    double end,
    const std::string& expression_str,
    const FittingParametersConfig& config,
    const std::vector<Interval>& phase_a_intervals) {

    std::cout << "\n[Phase B] Relaxed merging (avg MAE <= "
              << std::scientific << std::setprecision(2)
              << config.acceptable_error << ")\n";

    ErrorMetrics phase_a_metrics = computeErrorMetrics(phase_a_intervals, expression_str,
                                                        config.acceptable_error);

    double margin = config.acceptable_error / phase_a_metrics.avg_interval_mae;
    std::cout << "  Phase A: " << phase_a_intervals.size() << " intervals, "
              << "avg MAE=" << std::scientific << std::setprecision(2)
              << phase_a_metrics.avg_interval_mae
              << " (margin: " << std::fixed << std::setprecision(1) << margin << "x)\n";

    // Count extreme intervals
    size_t extreme_count = 0;
    for (const auto& interval : phase_a_intervals) {
        IntervalMetrics im = calculateMetrics(interval, expression_str, 100);
        if (im.avg_abs_error > config.acceptable_error * 5.0) {
            extreme_count++;
        }
    }
    double extreme_ratio = static_cast<double>(extreme_count) / phase_a_intervals.size();

    if (extreme_count > 0) {
        std::cout << "  Extreme intervals: " << extreme_count
                  << " (" << std::fixed << std::setprecision(1)
                  << (extreme_ratio * 100.0) << "%)\n";
    }

    // Select spike factors based on margin
    std::string strategy;
    std::vector<double> spike_factors;
    bool has_extremes = (extreme_ratio > 0.05);

    if (margin > 100.0) {
        strategy = "ultra aggressive";
        spike_factors = has_extremes ?
            std::vector<double>{100.0, 80.0, 60.0, 50.0, 40.0, 30.0, 25.0, 20.0, 15.0, 12.0, 10.0} :
            std::vector<double>{50.0, 40.0, 30.0, 25.0, 20.0, 15.0, 12.0, 10.0, 8.0, 6.0, 5.0, 4.0, 3.0, 2.5, 2.0};
    } else if (margin > 50.0) {
        strategy = "super aggressive";
        spike_factors = has_extremes ?
            std::vector<double>{80.0, 60.0, 50.0, 40.0, 30.0, 25.0, 20.0, 15.0, 12.0, 10.0} :
            std::vector<double>{35.0, 30.0, 25.0, 20.0, 15.0, 12.0, 10.0, 8.0, 6.0, 5.0, 4.0, 3.0, 2.5, 2.0};
    } else if (margin > 20.0) {
        strategy = "very aggressive";
        spike_factors = has_extremes ?
            std::vector<double>{60.0, 50.0, 40.0, 30.0, 25.0, 20.0, 15.0, 12.0, 10.0} :
            std::vector<double>{25.0, 20.0, 15.0, 12.0, 10.0, 8.0, 6.0, 5.0, 4.0, 3.0, 2.5, 2.0};
    } else if (margin > 10.0) {
        strategy = "aggressive";
        spike_factors = has_extremes ?
            std::vector<double>{40.0, 30.0, 25.0, 20.0, 15.0, 12.0, 10.0, 8.0} :
            std::vector<double>{15.0, 12.0, 10.0, 8.0, 6.0, 5.0, 4.0, 3.0, 2.5, 2.0};
    } else if (margin > 5.0) {
        strategy = "moderate";
        spike_factors = has_extremes ?
            std::vector<double>{30.0, 25.0, 20.0, 15.0, 12.0, 10.0, 8.0} :
            std::vector<double>{15.0, 12.0, 10.0, 8.0, 6.0, 5.0, 4.0, 3.0, 2.5, 2.0, 1.5};
    } else if (margin > 3.0) {
        strategy = "conservative";
        spike_factors = has_extremes ?
            std::vector<double>{30.0, 25.0, 20.0, 15.0, 12.0, 10.0, 8.0} :
            std::vector<double>{12.0, 10.0, 8.0, 6.0, 5.0, 4.0, 3.0, 2.5, 2.0, 1.5};
    } else if (margin > 2.0) {
        strategy = "very conservative";
        spike_factors = has_extremes ?
            std::vector<double>{25.0, 20.0, 15.0, 12.0, 10.0, 8.0, 6.0} :
            std::vector<double>{10.0, 8.0, 6.0, 5.0, 4.0, 3.0, 2.5, 2.0, 1.5};
    } else if (margin > 1.5) {
        strategy = "extremely conservative";
        spike_factors = has_extremes ?
            std::vector<double>{15.0, 12.0, 10.0, 8.0, 6.0, 5.0} :
            std::vector<double>{6.0, 5.0, 4.0, 3.0, 2.5, 2.0, 1.5};
    } else if (margin > 1.2) {
        strategy = "minimal";
        spike_factors = has_extremes ?
            std::vector<double>{10.0, 8.0, 6.0, 5.0, 4.0} :
            std::vector<double>{4.0, 3.0, 2.5, 2.0, 1.5, 1.2};
    } else {
        std::cout << "  Phase A already optimal (margin=" << std::fixed << std::setprecision(2)
                  << margin << "x), skipping Phase B\n";
        return phase_a_intervals;
    }

    if (has_extremes) {
        strategy += " + extreme handling";
    }

    std::cout << "  Strategy: " << strategy
              << " (testing " << spike_factors.size() << " spikes)\n";

    // Relaxed validation
    auto relaxed_satisfies = [&](const std::vector<Interval>& intervals, double spike) -> bool {
        ErrorMetrics metrics = computeErrorMetrics(intervals, expression_str, config.acceptable_error);

        if (metrics.avg_interval_mae > config.acceptable_error) {
            return false;
        }

        size_t violation_count = 0;
        for (const auto& interval : intervals) {
            IntervalMetrics im = calculateMetrics(interval, expression_str, 100);
            if (im.max_abs_error > config.acceptable_error * spike) {
                violation_count++;
            }
        }

        double violation_ratio = static_cast<double>(violation_count) / intervals.size();

        // Adaptive violation tolerance
        double max_violation_ratio;
        if (spike >= 10.0) {
            max_violation_ratio = 0.10;
        } else if (spike >= 5.0) {
            max_violation_ratio = 0.07;
        } else {
            max_violation_ratio = 0.05;
        }

        return violation_ratio <= max_violation_ratio;
    };

    // Test spike factors (COMPACT OUTPUT)
    std::vector<Interval> best_intervals = phase_a_intervals;
    double best_spike = 0.0;
    size_t best_count = phase_a_intervals.size();

    std::cout << "  Testing spikes: ";
    size_t test_count = 0;

    for (double spike : spike_factors) {
        std::vector<Interval> merged = mergeIntervalsAdaptive(
            phase_a_intervals, expression_str, config, spike);

        bool satisfies = relaxed_satisfies(merged, spike);

        if (satisfies && merged.size() < best_count) {
            best_intervals = merged;
            best_spike = spike;
            best_count = merged.size();

            // Print only successful improvements
            if (test_count > 0) std::cout << ", ";
            std::cout << std::fixed << std::setprecision(0) << spike
                      << "→" << merged.size();
            test_count++;
        }
    }

    if (test_count == 0) {
        std::cout << "none passed\n";
        std::cout << "  Using Phase A result\n";
        return phase_a_intervals;
    }

    std::cout << "\n";

    // Print final result
    ErrorMetrics final = computeErrorMetrics(best_intervals, expression_str,
                                            config.acceptable_error);

    double reduction = 100.0 * (1.0 - static_cast<double>(best_count) / phase_a_intervals.size());
    double utilization = 100.0 * final.avg_interval_mae / config.acceptable_error;

    std::cout << "  Best: spike=" << std::fixed << std::setprecision(1) << best_spike
              << ", " << phase_a_intervals.size() << "→" << best_count
              << " (" << std::setprecision(1) << reduction << "% reduction)\n";
    std::cout << "  Errors: avgMAE=" << std::scientific << std::setprecision(2)
              << final.avg_interval_mae
              << ", maxMAE=" << final.worst_interval_mae
              << ", maxErr=" << final.worst_max_error << "\n";
    std::cout << "  Quality: " << std::fixed << std::setprecision(1)
              << utilization << "% utilization, "
              << "RMSE=" << std::scientific << std::setprecision(2) << final.global_rmse << "\n";

    return best_intervals;
}

// ============================================================================
// Two-Phase Optimization Pipeline
// ============================================================================

inline std::vector<Interval> optimizeIntervalsTwoPhase(
    double start,
    double end,
    const std::string& expression_str,
    const FittingParametersConfig& config) {

    std::cout << "\n========== Two-Phase Optimization ==========\n";

    std::vector<Interval> phase_a_intervals = runPhaseA_Strict(
        start, end, expression_str, config);

    std::vector<Interval> phase_b_intervals = runPhaseB_Relaxed(
        start, end, expression_str, config, phase_a_intervals);

    std::cout << "\n========== Complete ==========\n";
    ErrorMetrics final = computeErrorMetrics(phase_b_intervals, expression_str,
                                            config.acceptable_error);
    std::cout << "  Intervals: " << phase_b_intervals.size() << "\n";
    std::cout << "  Compression: " << std::fixed << std::setprecision(2)
            << (static_cast<double>(phase_a_intervals.size()) / phase_b_intervals.size()) << "x\n";
    std::cout << "\n  Error Metrics:\n";
    std::cout << "    MAE  (avg): " << std::scientific << std::setprecision(4) << final.avg_interval_mae << "\n";
    std::cout << "    MAE (worst): " << final.worst_interval_mae << "\n";
    std::cout << "    MAE (global): " << final.global_mae << "\n";
    std::cout << "    MSE  (avg): " << final.avg_interval_mse << "\n";
    std::cout << "    MSE (worst): " << final.worst_interval_mse << "\n";
    std::cout << "    MSE (global): " << final.global_mse << "\n";
    std::cout << "    RMSE: " << final.global_rmse << "\n";
    std::cout << "    Max Error: " << final.worst_max_error << "\n";
    std::cout << "==============================\n";

    return phase_b_intervals;
}

// ============================================================================
// Main Pipeline (Legacy Interface)
// ============================================================================

struct OptimizationResult {
    std::vector<Interval> intervals;
    size_t initial_count;
    size_t final_count;
    double compression_ratio;
    double max_error;
    double avg_mae;
};

inline OptimizationResult optimizeIntervals(
    double start,
    double end,
    const std::string& expression_str,
    const FittingParametersConfig& config) {

    OptimizationResult result;

    std::vector<Interval> phase_a = runPhaseA_Strict(start, end, expression_str, config);
    result.initial_count = phase_a.size();

    std::vector<Interval> phase_b = runPhaseB_Relaxed(start, end, expression_str, config, phase_a);

    result.intervals = phase_b;
    result.final_count = phase_b.size();
    result.compression_ratio = static_cast<double>(result.initial_count) / result.final_count;

    ErrorMetrics final_stats = computeErrorMetrics(phase_b, expression_str,
                                                   config.acceptable_error);
    result.max_error = final_stats.worst_max_error;
    result.avg_mae = final_stats.avg_interval_mae;

    return result;
}

// ============================================================================
// File I/O
// ============================================================================

inline void saveIntervalsToFile(const std::vector<Interval>& intervals,
                               const std::string& filename) {
    std::ofstream file(filename);
    if (file.is_open()) {
        file << "Start,End,Level,Hessian\n";
        for (const auto& interval : intervals) {
            file << interval.start << "," << interval.end << ","
                 << interval.level << "," << interval.hessian << "\n";
        }
        file.close();
        std::cout << "Saved: " << filename << "\n";
    }
}

#endif // INTERVAL_OPTIMIZER_HPP
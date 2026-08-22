#ifndef INTERVAL_TYPES_HPP
#define INTERVAL_TYPES_HPP

#include "common_types.hpp"
#include <cstddef>

// ============================================================================
// Stage 1 Configuration Structures
// ============================================================================

struct OptimizationConfig {
    double entropy_threshold;
    double complexity_threshold;
    double error_sensitivity_threshold;
    double merge_score_threshold;
    double target_error;

    OptimizationConfig()
        : entropy_threshold(0.1), complexity_threshold(0.1),
          error_sensitivity_threshold(0.1), merge_score_threshold(0.1),
          target_error(1e-4) {}
};

struct MergeParams {
    double base_len_tol;
    double base_continuity_tol;
    double curvature_base;
    double curvature_sensitivity;
    double slope_base;
    double epsilon_base;

    MergeParams()
        : base_len_tol(1e-6), base_continuity_tol(1e-6),
          curvature_base(1.0), curvature_sensitivity(1.5),
          slope_base(1.0), epsilon_base(1e-6) {}
};

struct FittingParametersConfig {
    double min_unit_length;
    double epsilon_start;
    double epsilon_end;
    size_t epsilon_steps;
    double acceptable_error;
    double compression_aggressiveness;
    double merge_relax_factor;

    FittingParametersConfig()
        : min_unit_length(1e-5),
          epsilon_start(1e-4), epsilon_end(2e-3),
          epsilon_steps(20), acceptable_error(1e-4),
          compression_aggressiveness(0.7), merge_relax_factor(1.0) {}
};

// Note: The following types are imported from common_types.hpp:
// - Interval
// - IntervalMetrics
// - FittingMethod
// - FitParameters
// - CompressedFitParameters

#endif // INTERVAL_TYPES_HPP
#ifndef GROUP_TYPES_HPP
#define GROUP_TYPES_HPP

#include <vector>
#include <string>
#include <cstdint>
#include <map>
#include <iostream>
#include <iomanip>
#include "../common/common_types.hpp"
#include "../fitting/quantization_precision.hpp"

// ============================================================================
// Group Storage Type
// ============================================================================

enum class GroupStorageType {
    POWER_OF_2_GROUP,
    ORPHAN_GROUP,
    SYMMETRIC_PAIR
};

// ============================================================================
// Delta Encoding (Pre-Quantization)
// ============================================================================

struct DeltaEncoding {
    size_t original_index;

    // Original data
    Interval original_interval;
    FitParameters original_params;

    // Delta values (relative to group base)
    double delta_start;
    double delta_a;
    double delta_b;
    double delta_c;

    // Symmetry flags
    bool is_y_reflected = false;
    bool is_x_reflected = false;
    bool is_translated = false;
    bool is_padding = false;

    DeltaEncoding()
        : original_index(0), delta_start(0.0),
          delta_a(0.0), delta_b(0.0), delta_c(0.0) {}
};

// ============================================================================
// Quantized Delta (Post-Quantization)
// ============================================================================

struct QuantizedDelta {
    size_t original_index;

    Interval original_interval;
    FitParameters original_params;

    // Quantized delta values (integer)
    int32_t delta_start_q;
    int32_t delta_a_q;
    int32_t delta_b_q;
    int32_t delta_c_q;

    // Symmetry flags
    bool is_y_reflected = false;
    bool is_x_reflected = false;
    bool is_translated = false;
    bool is_padding = false;

    QuantizedDelta()
        : original_index(0), delta_start_q(0),
          delta_a_q(0), delta_b_q(0), delta_c_q(0) {}
};

// ============================================================================
// Interval Group (Before Quantization)
// ============================================================================

struct IntervalGroup {
    std::string group_id;
    GroupStorageType storage_type;
    size_t count;

    FitParameters base_params;
    std::vector<DeltaEncoding> members;

    double avg_length;
    double length_variance;

    bool has_symmetry = false;
    double symmetry_center = 0.0;
    std::vector<size_t> symmetric_pairs;

    IntervalGroup()
        : storage_type(GroupStorageType::POWER_OF_2_GROUP),
          count(0), avg_length(0.0), length_variance(0.0) {}
};

// ============================================================================
// Quantized Group (After Delta Quantization)
// ============================================================================

struct QuantizedGroup {
    std::string group_id;
    GroupStorageType storage_type;
    size_t count;

    FitParameters base_params;  // Already quantized from Phase 2.5
    std::vector<QuantizedDelta> members;

    // Delta quantization metadata
    uint8_t delta_position_bits;
    uint8_t delta_a_bits;
    uint8_t delta_b_bits;
    uint8_t delta_c_bits;

    // Scale factors for dequantization
    double delta_start_scale;
    double delta_start_offset;
    double delta_a_scale;
    double delta_a_offset;
    double delta_b_scale;
    double delta_b_offset;
    double delta_c_scale;
    double delta_c_offset;

    bool has_symmetry = false;
    double symmetry_center = 0.0;
    std::vector<size_t> symmetric_pairs;

    QuantizedGroup()
        : storage_type(GroupStorageType::POWER_OF_2_GROUP),
          count(0), delta_position_bits(0), delta_a_bits(0),
          delta_b_bits(0), delta_c_bits(0),
          delta_start_scale(1.0), delta_start_offset(0.0),
          delta_a_scale(1.0), delta_a_offset(0.0),
          delta_b_scale(1.0), delta_b_offset(0.0),
          delta_c_scale(1.0), delta_c_offset(0.0) {}
};

// ============================================================================
// Compressed Interval Data
// ============================================================================

struct CompressedIntervalData {
    std::vector<QuantizedGroup> groups;

    size_t total_intervals;
    size_t total_groups;
    double compression_ratio;

    CompressedIntervalData()
        : total_intervals(0), total_groups(0), compression_ratio(1.0) {}
};

// ============================================================================
// Stage 3 Configuration
// ============================================================================

struct Stage3Config {
    // Grouping
    double length_tolerance = 0.05;
    size_t min_group_size = 4;

    // Symmetry
    bool enable_symmetry = true;
    double symmetry_tolerance = 1e-6;
    double symmetry_position_tol = 1e-4;

    // Adaptive bit-width selection
    bool adaptive_bitwidth = false;
    double bitwidth_error_threshold = 1e-10;

    // Delta quantization bit widths
    uint8_t delta_position_bits = 16;
    uint8_t delta_a_bits = 16;
    uint8_t delta_b_bits = 16;
    uint8_t delta_c_bits = 16;

    bool verbose = false;

    void print() const {
        std::cout << "\nStage 3 Configuration:\n";
        std::cout << "  Grouping:\n";
        std::cout << "    Length tolerance: " << length_tolerance << "\n";
        std::cout << "    Min group size:   " << min_group_size << "\n";
        std::cout << "  Delta Quantization:\n";
        std::cout << "    Position bits: " << (int)delta_position_bits << "\n";
        std::cout << "    Delta A bits:  " << (int)delta_a_bits << "\n";
        std::cout << "    Delta B bits:  " << (int)delta_b_bits << "\n";
        std::cout << "    Delta C bits:  " << (int)delta_c_bits << "\n";
        std::cout << "\n";
    }
};

// ============================================================================
// Statistics
// ============================================================================

struct GroupingStats {
    size_t total_intervals = 0;
    size_t total_groups = 0;
    size_t num_normal_groups = 0;
    size_t num_orphan_groups = 0;
    size_t intervals_in_normal_groups = 0;
    size_t intervals_in_orphan_groups = 0;
    double avg_group_size = 0.0;
    double max_group_size = 0.0;
    double min_group_size = 0.0;
    double estimated_compression_ratio = 1.0;
};

struct PrecisionErrorStats {
    double max_error = 0.0;
    double avg_error = 0.0;
    double rmse = 0.0;
    size_t overflow_count = 0;

    void print(const std::string& format_name) const {
        std::cout << "  " << std::left << std::setw(15) << format_name << ": ";
        std::cout << "max=" << std::scientific << std::setprecision(4) << max_error;
        std::cout << "  avg=" << avg_error;
        std::cout << "  rmse=" << rmse;
        if (overflow_count > 0) {
            std::cout << "  overflows=" << overflow_count;
        }
        std::cout << "\n";
    }
};

struct QuantizationStats {
    // Delta bit widths
    double avg_delta_position_bits = 0.0;
    double avg_delta_a_bits = 0.0;
    double avg_delta_b_bits = 0.0;
    double avg_delta_c_bits = 0.0;

    // Delta quantization error
    double max_quantization_error = 0.0;
    double avg_quantization_error = 0.0;

    // Fitting error (vs true function)
    double max_fitting_error = 0.0;
    double avg_fitting_error = 0.0;
    double rmse = 0.0;

    // Storage
    size_t total_bits_uncompressed = 0;
    size_t total_bits_compressed = 0;
    double compression_ratio = 1.0;

    // Multi-precision errors
    std::map<std::string, PrecisionErrorStats> precision_errors;

    void print() const {
        std::cout << "\nStage 3 Quantization Statistics:\n";
        std::cout << "  Delta Bit Widths:\n";
        std::cout << "    Position: " << std::fixed << std::setprecision(1)
                  << avg_delta_position_bits << " bits\n";
        std::cout << "    Delta A:  " << avg_delta_a_bits << " bits\n";
        std::cout << "    Delta B:  " << avg_delta_b_bits << " bits\n";
        std::cout << "    Delta C:  " << avg_delta_c_bits << " bits\n\n";

        std::cout << "  Delta Quantization Error:\n";
        std::cout << std::scientific << std::setprecision(6);
        std::cout << "    Max: " << max_quantization_error << "\n";
        std::cout << "    Avg: " << avg_quantization_error << "\n\n";

        std::cout << "  Final Fitting Error (vs True Function):\n";
        std::cout << "    Max:  " << max_fitting_error << "\n";
        std::cout << "    Avg:  " << avg_fitting_error << "\n";
        std::cout << "    RMSE: " << rmse << "\n\n";

        std::cout << "  Compression:\n";
        std::cout << "    Ratio: " << std::fixed << std::setprecision(2)
                  << compression_ratio << "x\n\n";

        if (!precision_errors.empty()) {
            std::cout << "  Precision Errors:\n";
            for (const auto& [format, stats] : precision_errors) {
                stats.print(format);
            }
            std::cout << "\n";
        }
    }
};

#endif // GROUP_TYPES_HPP

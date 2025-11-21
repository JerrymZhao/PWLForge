#ifndef GROUP_QUANTIZATION_HPP
#define GROUP_QUANTIZATION_HPP

#include <vector>
#include <cmath>
#include <limits>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <cstdint>
#include <functional>
#include "group_types.hpp"
#include "../common/common_types.hpp"

// ============================================================================
// Core delta encoding functions
// ============================================================================

// Evaluate polynomial at local position x
inline double evaluateFit(const FitParameters& params, double x) {
    if (params.method == FittingMethod::Quadratic) {
        return params.a * x * x + params.b * x + params.c;
    } else {
        return params.b * x + params.c;
    }
}

// Compute min/max range for delta values within a group
inline void computeDeltaRange(const std::vector<DeltaEncoding>& deltas,
                              double& min_val, double& max_val,
                              double (DeltaEncoding::*member)) {
    if (deltas.empty()) {
        min_val = 0.0;
        max_val = 0.0;
        return;
    }
    
    min_val = std::numeric_limits<double>::max();
    max_val = std::numeric_limits<double>::lowest();
    
    for (const auto& delta : deltas) {
        double val = delta.*member;
        min_val = std::min(min_val, val);
        max_val = std::max(max_val, val);
    }
}

// ============================================================================
// Delta quantization (lossless compression of delta values)
// ============================================================================

// Quantize floating-point delta to signed integer
inline int32_t quantizeValue(double value, uint8_t bits,
                             double scale, double offset) {
    if (bits == 0 || bits > 32) return 0;
    
    // Normalize: center around offset, scale to integer range
    double normalized = (value - offset) / scale;
    
    // Signed range: [-2^(bits-1), 2^(bits-1) - 1]
    int32_t max_int = (1 << (bits - 1)) - 1;
    int32_t min_int = -(1 << (bits - 1));
    
    int32_t quantized = static_cast<int32_t>(std::round(normalized));
    
    // Clamp to valid range
    return std::max(min_int, std::min(max_int, quantized));
}

// Dequantize signed integer back to floating-point delta
inline double dequantizeValue(int32_t quantized, double scale, double offset) {
    return quantized * scale + offset;
}

// ============================================================================
// Adaptive bit-width selection (optional optimization)
// ============================================================================

// Select optimal bit-width based on delta range and error tolerance
inline uint8_t selectAdaptiveBitWidth(double min_val, double max_val, 
                                     double error_threshold,
                                     uint8_t min_bits = 4,
                                     uint8_t max_bits = 16) {
    double range = max_val - min_val;
    
    // Handle constant deltas
    if (range < 1e-15) {
        return min_bits;
    }
    
    // Test increasing bit-widths until error threshold is met
    for (uint8_t bits = min_bits; bits <= max_bits; bits += 2) {
        int32_t max_levels = (1 << (bits - 1)) - 1;
        double scale = (range / 2.0) / max_levels;
        double max_quant_error = scale / 2.0;
        
        if (max_quant_error <= error_threshold) {
            return bits;
        }
    }
    
    return max_bits; // fallback to maximum bits
}

// ============================================================================
// Group delta quantization (main compression step)
// Note: This quantizes DELTA values, not endpoints/params!
//       Endpoints and params were already quantized in Phase 2.5
// ============================================================================

// Quantize single group's delta values
inline QuantizedGroup quantizeGroup(const IntervalGroup& group,
                                   const Stage3Config& config) {
    QuantizedGroup qgroup;
    qgroup.group_id = group.group_id;
    qgroup.storage_type = group.storage_type;
    qgroup.count = group.count;
    qgroup.has_symmetry = group.has_symmetry;
    qgroup.symmetry_center = group.symmetry_center;
    qgroup.symmetric_pairs = group.symmetric_pairs;
    qgroup.base_params = group.base_params;
    
    if (group.members.empty()) {
        return qgroup;
    }
    
    // Step 1: Compute delta ranges for each component
    // (Deltas are already computed from quantized values in grouping phase)
    double min_start, max_start;
    double min_a, max_a;
    double min_b, max_b;
    double min_c, max_c;
    
    computeDeltaRange(group.members, min_start, max_start, &DeltaEncoding::delta_start);
    computeDeltaRange(group.members, min_a, max_a, &DeltaEncoding::delta_a);
    computeDeltaRange(group.members, min_b, max_b, &DeltaEncoding::delta_b);
    computeDeltaRange(group.members, min_c, max_c, &DeltaEncoding::delta_c);
    
    if (config.verbose) {
        std::cout << "\n=== Group " << group.group_id << " Delta Ranges ===\n";
        std::cout << "  (Deltas computed from quantized endpoints/params)\n";
        std::cout << "  Delta start: [" << std::scientific << std::setprecision(6) 
                  << min_start << ", " << max_start << "] range=" << (max_start - min_start) << "\n";
        std::cout << "  Delta a:     [" << min_a << ", " << max_a << "] range=" << (max_a - min_a) << "\n";
        std::cout << "  Delta b:     [" << min_b << ", " << max_b << "] range=" << (max_b - min_b) << "\n";
        std::cout << "  Delta c:     [" << min_c << ", " << max_c << "] range=" << (max_c - min_c) << "\n";
    }
    
    // Step 2: Select bit-widths (adaptive or fixed)
    if (config.adaptive_bitwidth) {
        // Adaptive: select minimum bits that meet error threshold
        qgroup.delta_position_bits = selectAdaptiveBitWidth(
            min_start, max_start, config.bitwidth_error_threshold);
        qgroup.delta_a_bits = selectAdaptiveBitWidth(
            min_a, max_a, config.bitwidth_error_threshold);
        qgroup.delta_b_bits = selectAdaptiveBitWidth(
            min_b, max_b, config.bitwidth_error_threshold);
        qgroup.delta_c_bits = selectAdaptiveBitWidth(
            min_c, max_c, config.bitwidth_error_threshold);
        
        if (config.verbose) {
            std::cout << "  Adaptive bit-widths: pos=" << (int)qgroup.delta_position_bits
                      << ", a=" << (int)qgroup.delta_a_bits
                      << ", b=" << (int)qgroup.delta_b_bits
                      << ", c=" << (int)qgroup.delta_c_bits << "\n";
        }
    } else {
        // Fixed: use same bit-width from config
        qgroup.delta_position_bits = config.delta_position_bits;
        qgroup.delta_a_bits = config.delta_a_bits;
        qgroup.delta_b_bits = config.delta_b_bits;
        qgroup.delta_c_bits = config.delta_c_bits;
    }
    
    // Step 3: Compute quantization scale/offset (symmetric around midpoint)
    auto compute_scale_offset = [&config](double min_val, double max_val, uint8_t bits,
                                          double& scale, double& offset, const char* name) {
        if (bits == 0) {
            scale = 1.0;
            offset = 0.0;
            return;
        }
        
        double range = max_val - min_val;
        
        if (range < 1e-15) {
            // Constant delta: store value as offset, scale=1
            scale = 1.0;
            offset = min_val;
            if (config.verbose) {
                std::cout << "    " << name << " (" << (int)bits << " bits): constant=" 
                          << offset << "\n";
            }
            return;
        }
        
        // Center offset at midpoint for symmetric quantization
        offset = (min_val + max_val) / 2.0;
        
        // Signed integer range: [-2^(bits-1), 2^(bits-1) - 1]
        int32_t max_levels = (1 << (bits - 1)) - 1;
        
        // Scale maps half-range to max_levels
        double half_range = range / 2.0;
        scale = half_range / max_levels;
        
        // Quantization error bound: ±scale/2
        double max_quant_error = scale / 2.0;
        
        if (config.verbose) {
            std::cout << "    " << name << " (" << (int)bits << " bits):\n";
            std::cout << "      offset=" << std::setprecision(10) << offset 
                      << ", scale=" << scale 
                      << ", max_error=±" << max_quant_error << "\n";
        }
    };
    
    compute_scale_offset(min_start, max_start, qgroup.delta_position_bits,
                        qgroup.delta_start_scale, qgroup.delta_start_offset, "delta_start");
    compute_scale_offset(min_a, max_a, qgroup.delta_a_bits,
                        qgroup.delta_a_scale, qgroup.delta_a_offset, "delta_a");
    compute_scale_offset(min_b, max_b, qgroup.delta_b_bits,
                        qgroup.delta_b_scale, qgroup.delta_b_offset, "delta_b");
    compute_scale_offset(min_c, max_c, qgroup.delta_c_bits,
                        qgroup.delta_c_scale, qgroup.delta_c_offset, "delta_c");
    
    // Step 4: Quantize all delta values in group
    for (const auto& delta : group.members) {
        QuantizedDelta qdelta;
        qdelta.original_index = delta.original_index;
        qdelta.original_interval = delta.original_interval;
        qdelta.original_params = delta.original_params;
        
        // Quantize delta values (incremental encoding compression)
        qdelta.delta_start_q = quantizeValue(delta.delta_start,
                                            qgroup.delta_position_bits,
                                            qgroup.delta_start_scale,
                                            qgroup.delta_start_offset);
        
        qdelta.delta_a_q = quantizeValue(delta.delta_a,
                                        qgroup.delta_a_bits,
                                        qgroup.delta_a_scale,
                                        qgroup.delta_a_offset);
        
        qdelta.delta_b_q = quantizeValue(delta.delta_b,
                                        qgroup.delta_b_bits,
                                        qgroup.delta_b_scale,
                                        qgroup.delta_b_offset);
        
        qdelta.delta_c_q = quantizeValue(delta.delta_c,
                                        qgroup.delta_c_bits,
                                        qgroup.delta_c_scale,
                                        qgroup.delta_c_offset);
        
        qdelta.is_y_reflected = delta.is_y_reflected;
        qdelta.is_x_reflected = delta.is_x_reflected;
        qdelta.is_translated = delta.is_translated;
        qdelta.is_padding = delta.is_padding;
        
        qgroup.members.push_back(qdelta);
    }
    
    // Step 5: Verify quantization quality
    if (config.verbose) {
        double max_recon_error = 0.0;
        for (size_t i = 0; i < qgroup.members.size(); ++i) {
            const auto& member = qgroup.members[i];
            const auto& orig_delta = group.members[i];
            
            double recon_a = dequantizeValue(member.delta_a_q, 
                                            qgroup.delta_a_scale, 
                                            qgroup.delta_a_offset);
            double recon_b = dequantizeValue(member.delta_b_q, 
                                            qgroup.delta_b_scale, 
                                            qgroup.delta_b_offset);
            double recon_c = dequantizeValue(member.delta_c_q, 
                                            qgroup.delta_c_scale, 
                                            qgroup.delta_c_offset);
            
            double error_a = std::abs(recon_a - orig_delta.delta_a);
            double error_b = std::abs(recon_b - orig_delta.delta_b);
            double error_c = std::abs(recon_c - orig_delta.delta_c);
            
            max_recon_error = std::max({max_recon_error, error_a, error_b, error_c});
        }
        std::cout << "  Max delta reconstruction error: " << max_recon_error << "\n";
    }
    
    return qgroup;
}

// Quantize all groups (main entry point)
inline std::vector<QuantizedGroup> quantizeGroups(
    const std::vector<IntervalGroup>& groups,
    const Stage3Config& config) {
    
    std::vector<QuantizedGroup> qgroups;
    qgroups.reserve(groups.size());
    
    for (const auto& group : groups) {
        qgroups.push_back(quantizeGroup(group, config));
    }
    
    return qgroups;
}

// ============================================================================
// Reconstruction (dequantization + delta decoding)
// ============================================================================

// Reconstruct full parameters from quantized delta
// Returns quantized parameters (since deltas are based on quantized base)
inline FitParameters reconstructFitParams(const QuantizedGroup& qgroup,
                                         size_t member_index) {
    if (member_index >= qgroup.members.size()) {
        throw std::out_of_range("Member index out of range");
    }
    
    const auto& member = qgroup.members[member_index];
    
    // Dequantize deltas
    double delta_start = dequantizeValue(member.delta_start_q,
                                        qgroup.delta_start_scale,
                                        qgroup.delta_start_offset);
    
    double delta_a = dequantizeValue(member.delta_a_q,
                                    qgroup.delta_a_scale,
                                    qgroup.delta_a_offset);
    
    double delta_b = dequantizeValue(member.delta_b_q,
                                    qgroup.delta_b_scale,
                                    qgroup.delta_b_offset);
    
    double delta_c = dequantizeValue(member.delta_c_q,
                                    qgroup.delta_c_scale,
                                    qgroup.delta_c_offset);
    
    // Reconstruct: base + delta (both are quantized values)
    FitParameters params;
    params.a = qgroup.base_params.a + delta_a;
    params.b = qgroup.base_params.b + delta_b;
    params.c = qgroup.base_params.c + delta_c;
    params.method = qgroup.base_params.method;
    params.order = qgroup.base_params.order;
    
    // Reconstruct interval endpoints (use quantized values)
    params.range_start = member.original_interval.get_start() + delta_start;
    params.range_end = member.original_interval.get_end();
    
    return params;
}

// ============================================================================
// Error evaluation (delta quantization quality)
// ============================================================================

// Evaluate delta quantization error against original function
inline void evaluateDeltaQuantizationError(
    const std::vector<QuantizedGroup>& qgroups,
    const std::function<double(double)>& original_function,
    double& max_error,
    double& avg_error,
    double& rmse) {
    
    double sum_error = 0.0;
    double sum_squared = 0.0;
    size_t count = 0;
    max_error = 0.0;
    
    for (const auto& qgroup : qgroups) {
        for (size_t i = 0; i < qgroup.members.size(); ++i) {
            const auto& member = qgroup.members[i];
            
            // Reconstruct parameters (quantized base + dequantized delta)
            FitParameters recon_params = reconstructFitParams(qgroup, i);
            
            // Sample interval at multiple points
            double interval_start = member.original_interval.get_start();
            double interval_end = member.original_interval.get_end();
            double interval_length = interval_end - interval_start;
            
            size_t num_samples = 100;
            for (size_t j = 0; j < num_samples; ++j) {
                // Global x coordinate in original domain
                double x_global = interval_start + 
                                 (static_cast<double>(j) / num_samples) * interval_length;
                
                // Local x for polynomial (relative to interval start)
                double x_local = (static_cast<double>(j) / num_samples) * interval_length;
                
                // Ground truth from original function
                double y_true = original_function(x_global);
                
                // Reconstructed value
                double y_reconstructed = evaluateFit(recon_params, x_local);
                
                double error = std::abs(y_reconstructed - y_true);
                
                max_error = std::max(max_error, error);
                sum_error += error;
                sum_squared += error * error;
                count++;
            }
        }
    }
    
    if (count > 0) {
        avg_error = sum_error / count;
        rmse = std::sqrt(sum_squared / count);
    } else {
        avg_error = 0.0;
        rmse = 0.0;
    }
}

// ============================================================================
// Compression statistics
// ============================================================================

// Compute detailed compression statistics
inline QuantizationStats computeQuantizationStatsWithFunction(
    const std::vector<QuantizedGroup>& qgroups,
    const std::function<double(double)>& original_function,
    const Stage3Config& config) {
    
    QuantizationStats stats;
    
    size_t total_intervals = 0;
    size_t compressed_bits = 0;
    
    double sum_quant_error = 0.0;
    double sum_fit_error = 0.0;
    double sum_squared_error = 0.0;
    size_t error_count = 0;
    
    for (const auto& qgroup : qgroups) {
        total_intervals += qgroup.count;
        
        // Base params: 3 * 64 bits (already quantized from Phase 2.5)
        compressed_bits += 3 * 64;
        
        // Delta storage (incremental encoding)
        compressed_bits += qgroup.count * (
            qgroup.delta_position_bits +
            qgroup.delta_a_bits +
            qgroup.delta_b_bits +
            qgroup.delta_c_bits
        );
        
        // Metadata (scales, offsets, bit-widths)
        compressed_bits += 8 * 64;  // 4 scales + 4 offsets
        compressed_bits += 4 * 8;   // 4 bit-width values
        
        // Compute errors
        for (size_t i = 0; i < qgroup.members.size(); ++i) {
            const auto& member = qgroup.members[i];
            
            FitParameters recon_params = reconstructFitParams(qgroup, i);
            
            // Delta quantization error in parameter space
            // Compare reconstructed (quantized base + dequantized delta) vs original quantized
            double param_error = 0.0;
            param_error += std::abs(recon_params.a - member.original_params.get_a());
            param_error += std::abs(recon_params.b - member.original_params.get_b());
            param_error += std::abs(recon_params.c - member.original_params.get_c());
            
            stats.max_quantization_error = std::max(stats.max_quantization_error, param_error);
            sum_quant_error += param_error;
            
            // Fitting error using original function
            double interval_start = member.original_interval.get_start();
            double interval_end = member.original_interval.get_end();
            double interval_length = interval_end - interval_start;
            
            size_t num_samples = 100;
            for (size_t j = 0; j < num_samples; ++j) {
                double x_global = interval_start + 
                                 (static_cast<double>(j) / num_samples) * interval_length;
                double x_local = (static_cast<double>(j) / num_samples) * interval_length;
                
                double fitted = evaluateFit(recon_params, x_local);
                double true_value = original_function(x_global);
                double error = std::abs(fitted - true_value);
                
                stats.max_fitting_error = std::max(stats.max_fitting_error, error);
                sum_fit_error += error;
                sum_squared_error += error * error;
                error_count++;
            }
        }
    }
    
    // Compute averages
    if (total_intervals > 0) {
        stats.avg_quantization_error = sum_quant_error / total_intervals;
    }
    
    if (error_count > 0) {
        stats.avg_fitting_error = sum_fit_error / error_count;
        stats.rmse = std::sqrt(sum_squared_error / error_count);
    }
    
    // Bit-width statistics
    stats.avg_delta_position_bits = config.delta_position_bits;
    stats.avg_delta_a_bits = config.delta_a_bits;
    stats.avg_delta_b_bits = config.delta_b_bits;
    stats.avg_delta_c_bits = config.delta_c_bits;
    
    // Storage statistics
    stats.total_bits_uncompressed = total_intervals * 5 * 64;  // start, end, a, b, c (FP64)
    stats.total_bits_compressed = compressed_bits;
    stats.compression_ratio = static_cast<double>(stats.total_bits_uncompressed) /
                             std::max(stats.total_bits_compressed, size_t(1));
    
    return stats;
}

// Legacy version (basic stats only, no function evaluation)
inline QuantizationStats computeQuantizationStats(
    const std::vector<QuantizedGroup>& qgroups,
    const Stage3Config& config) {
    
    QuantizationStats stats;
    
    size_t total_intervals = 0;
    size_t compressed_bits = 0;
    
    for (const auto& qgroup : qgroups) {
        total_intervals += qgroup.count;
        compressed_bits += 3 * 64 + qgroup.count * (
            qgroup.delta_position_bits +
            qgroup.delta_a_bits +
            qgroup.delta_b_bits +
            qgroup.delta_c_bits
        ) + 8 * 64 + 4 * 8;
    }
    
    stats.avg_delta_position_bits = config.delta_position_bits;
    stats.avg_delta_a_bits = config.delta_a_bits;
    stats.avg_delta_b_bits = config.delta_b_bits;
    stats.avg_delta_c_bits = config.delta_c_bits;
    stats.total_bits_uncompressed = total_intervals * 5 * 64;
    stats.total_bits_compressed = compressed_bits;
    stats.compression_ratio = static_cast<double>(stats.total_bits_uncompressed) /
                             std::max(stats.total_bits_compressed, size_t(1));
    
    return stats;
}

// ============================================================================
// Validation
// ============================================================================

// Validate delta quantization correctness
inline bool validateQuantization(const std::vector<QuantizedGroup>& qgroups,
                                const std::vector<Interval>& original_intervals,
                                double tolerance = 1e-6) {
    size_t total_checked = 0;
    size_t total_intervals = 0;
    
    for (const auto& qgroup : qgroups) {
        total_intervals += qgroup.count;
        
        for (size_t i = 0; i < qgroup.members.size(); ++i) {
            const auto& member = qgroup.members[i];
            
            if (member.original_index >= original_intervals.size()) {
                return false;
            }
            
            FitParameters recon = reconstructFitParams(qgroup, i);
            
            // Check for NaN/Inf
            if (std::isnan(recon.a) || std::isnan(recon.b) || std::isnan(recon.c)) {
                return false;
            }
            
            if (std::isinf(recon.a) || std::isinf(recon.b) || std::isinf(recon.c)) {
                return false;
            }
            
            total_checked++;
        }
    }
    
    return total_checked == total_intervals;
}

#endif // GROUP_QUANTIZATION_HPP
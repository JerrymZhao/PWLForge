#ifndef QUANTIZATION_PRECISION_HPP
#define QUANTIZATION_PRECISION_HPP

#include <cstdint>
#include <string>
#include <cmath>
#include <limits>
#include <algorithm>
#include <stdexcept>

//================================================================================
// Numeric Format Types
//================================================================================

enum class NumericFormat : uint8_t {
    FP64 = 0,    // IEEE 754 double
    FP32 = 1,    // IEEE 754 float
    FP16 = 2,    // IEEE 754 half
    FP8 = 3,     // E4M3 or E5M2
    FIXED = 4    // Fixed-point
};

//================================================================================
// Precision Configuration
//================================================================================

struct PrecisionConfig {
    NumericFormat format;
    uint8_t int_bits;   // For FIXED format
    uint8_t frac_bits;  // For FIXED format
    
    PrecisionConfig(NumericFormat fmt = NumericFormat::FP64,
                   uint8_t i_bits = 0, uint8_t f_bits = 0)
        : format(fmt), int_bits(i_bits), frac_bits(f_bits) {}
    
    std::string name() const {
        switch (format) {
            case NumericFormat::FP64: return "FP64";
            case NumericFormat::FP32: return "FP32";
            case NumericFormat::FP16: return "FP16";
            case NumericFormat::FP8:  return "FP8";
            case NumericFormat::FIXED:
                return "Fixed" + std::to_string(int_bits) + "_" + 
                       std::to_string(frac_bits);
            default: return "Unknown";
        }
    }
    
    size_t bits_per_value() const {
        switch (format) {
            case NumericFormat::FP64: return 64;
            case NumericFormat::FP32: return 32;
            case NumericFormat::FP16: return 16;
            case NumericFormat::FP8:  return 8;
            case NumericFormat::FIXED: return int_bits + frac_bits;
            default: return 0;
        }
    }
    
    // Auto-allocate int/frac bits based on data range
    static PrecisionConfig create_auto_fixed(uint8_t total_bits, 
                                             double min_val, 
                                             double max_val) {
        if (total_bits < 2) {
            throw std::invalid_argument("Total bits must be at least 2");
        }
        
        // Compute required integer bits (including sign bit)
        double abs_max = std::max(std::abs(min_val), std::abs(max_val));
        
        uint8_t int_bits;
        if (abs_max < 1e-10) {
            // Very small values, only need sign bit
            int_bits = 1;
        } else {
            // Required bits = ceil(log2(abs_max)) + 1(sign) + 1(safety margin)
            int_bits = static_cast<uint8_t>(std::ceil(std::log2(abs_max))) + 2;
        }
        
        // Cap integer bits to leave at least 1 bit for fractional part
        int_bits = std::min(int_bits, static_cast<uint8_t>(total_bits - 1));
        
        uint8_t frac_bits = total_bits - int_bits;
        
        return PrecisionConfig(NumericFormat::FIXED, int_bits, frac_bits);
    }
};

//================================================================================
// Quantization Utilities
//================================================================================

class QuantizationUtils {
public:
    // Quantize a single value
    static double quantize(double value, const PrecisionConfig& config) {
        switch (config.format) {
            case NumericFormat::FP64:
                return value;  // No quantization
            
            case NumericFormat::FP32:
                return static_cast<double>(static_cast<float>(value));
            
            case NumericFormat::FP16:
                return quantize_fp16(value);
            
            case NumericFormat::FP8:
                return quantize_fp8(value);
            
            case NumericFormat::FIXED:
                return quantize_fixed(value, config.int_bits, config.frac_bits);
            
            default:
                return value;
        }
    }
    
    // Compute quantization error
    static double quantization_error(double original, double quantized) {
        return std::abs(original - quantized);
    }
    
    // Check if value is representable
    static bool is_representable(double value, const PrecisionConfig& config) {
        double quantized = quantize(value, config);
        return std::isfinite(quantized);
    }

private:
    // FP16 quantization (simplified IEEE 754 half precision)
    static double quantize_fp16(double value) {
        if (value == 0.0) {
            return 0.0;
        }
        
        // IEEE 754 half: 1 sign, 5 exp, 10 mantissa
        constexpr double max_val = 65504.0;
        constexpr double min_normal = 6.103515625e-5;
        
        if (std::abs(value) > max_val) {
            return std::copysign(max_val, value);
        }
        
        if (std::abs(value) < min_normal) {
            // Subnormal range: 2^-14 * (0 to 1023)/1024
            constexpr double min_subnormal = 5.96046447753906e-8;
            
            if (std::abs(value) < min_subnormal) {
                return 0.0;  // Flush to zero
            }
            
            double quantum = min_subnormal;
            return std::round(value / quantum) * quantum;
        }
        
        // Normal range: 11-bit precision (1 implicit + 10 mantissa)
        double scale = std::pow(2.0, std::floor(std::log2(std::abs(value))));
        double quantum = scale / 1024.0;  // 2^10 = 1024
        return std::round(value / quantum) * quantum;
    }
    
    // FP8 quantization (E4M3 format)
    static double quantize_fp8(double value) {
        // E4M3: 1 sign, 4 exp, 3 mantissa
        constexpr double max_val = 448.0;  // 2^8 * (1 + 7/8)
        constexpr double min_normal = 0.015625;  // 2^-6
        
        if (std::abs(value) > max_val) {
            return std::copysign(max_val, value);
        }
        
        if (std::abs(value) < min_normal && value != 0.0) {
            // Subnormal range
            double quantum = min_normal / 8.0;
            return std::round(value / quantum) * quantum;
        }
        
        // Normal range: 4-bit precision
        double scale = std::pow(2.0, std::floor(std::log2(std::abs(value))));
        double quantum = scale / 8.0;  // 2^3 = 8
        return std::round(value / quantum) * quantum;
    }
    
    // Fixed-point quantization
    static double quantize_fixed(double value, uint8_t int_bits, uint8_t frac_bits) {
        double scale = std::pow(2.0, frac_bits);
        double max_val = std::pow(2.0, int_bits) - 1.0 / scale;
        double min_val = -max_val;
        
        // Clamp to representable range
        value = std::clamp(value, min_val, max_val);
        
        // Round to nearest quantum
        return std::round(value * scale) / scale;
    }
};

//================================================================================
// Preset Configurations
//================================================================================

namespace QuantizationPresets {
    inline PrecisionConfig FP64() { return {NumericFormat::FP64}; }
    inline PrecisionConfig FP32() { return {NumericFormat::FP32}; }
    inline PrecisionConfig FP16() { return {NumericFormat::FP16}; }
    inline PrecisionConfig FP8()  { return {NumericFormat::FP8}; }
    
    // Legacy fixed presets (manual bit allocation)
    inline PrecisionConfig Fixed16_16() { return {NumericFormat::FIXED, 16, 16}; }
    inline PrecisionConfig Fixed8_8()   { return {NumericFormat::FIXED, 8, 8}; }
    inline PrecisionConfig Fixed12_12() { return {NumericFormat::FIXED, 12, 12}; }
}

#endif // QUANTIZATION_PRECISION_HPP
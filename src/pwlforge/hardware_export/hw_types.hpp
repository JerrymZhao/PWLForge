#ifndef HW_TYPES_HPP
#define HW_TYPES_HPP

#include <cstdint>
#include <cmath>
#include <algorithm>
#include <cstring>

// Fixed-point constants
constexpr int FIXED_SCALE = 32768;  // 2^15
constexpr int FRAC_BITS = 15;

// Fixed-point quantization
inline int16_t quant_param(double v) {
    return static_cast<int16_t>(std::round(std::clamp(v, -1.0, 0.99997) * FIXED_SCALE));
}

inline int16_t quant_pos(double v) {
    return static_cast<int16_t>(std::round(std::clamp(v, -1.0, 1.0) * FIXED_SCALE));
}

inline uint16_t quant_len(double v) {
    return static_cast<uint16_t>(std::round(std::clamp(v, 0.0, 1.0) * FIXED_SCALE));
}

// Floating-point conversion
inline uint16_t float_to_hex(float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(float));
    return static_cast<uint16_t>((bits >> 16) & 0xFFFF);
}

#endif // HW_TYPES_HPP
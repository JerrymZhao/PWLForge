#ifndef HW_FILE_GEN_FLOAT_HPP
#define HW_FILE_GEN_FLOAT_HPP

#include <fstream>
#include <iomanip>
#include <cmath>
#include "group_types.hpp"
#include "group_quantization.hpp"
#include "hw_types.hpp"
#include "hw_utils.hpp"

//================================================================================
// Floating-point precision enumeration
//================================================================================
enum class FloatPrecision {
    FP16 = 16,
    FP32 = 32,
    FP64 = 64
};

//================================================================================
// Helper: Convert float to FP32 hex (32-bit)
//================================================================================
inline uint32_t float_to_fp32_hex(float value) {
    union {
        float f;
        uint32_t u;
    } converter;
    converter.f = value;
    return converter.u;
}

//================================================================================
// Helper: Convert double to FP64 hex (64-bit)
//================================================================================
inline uint64_t float_to_fp64_hex(double value) {
    union {
        double d;
        uint64_t u;
    } converter;
    converter.d = value;
    return converter.u;
}

//================================================================================
// Helper: Convert float to FP16 hex (16-bit, IEEE 754 half precision)
//================================================================================
inline uint16_t float_to_fp16_hex(float value) {
    union {
        float f;
        uint32_t u;
    } fp32;
    fp32.f = value;

    uint32_t sign = (fp32.u >> 31) & 0x1;
    int32_t exp = ((fp32.u >> 23) & 0xFF) - 127;
    uint32_t mantissa = fp32.u & 0x7FFFFF;

    // Handle special cases
    if (exp > 15) {
        // Overflow -> infinity
        return (sign << 15) | 0x7C00;
    }
    if (exp < -14) {
        // Underflow -> zero or denormal
        if (exp < -24) return (sign << 15);
        // Denormal number
        mantissa = (mantissa | 0x800000) >> (-14 - exp);
        return (sign << 15) | (mantissa >> 13);
    }

    // Normalized number
    uint16_t fp16_exp = exp + 15;
    uint16_t fp16_mantissa = mantissa >> 13;

    return (sign << 15) | (fp16_exp << 10) | fp16_mantissa;
}

//================================================================================
// Template: Generate interval_metadata.mem
// FP16: 3 words/interval (start[1] + end[1] + gid[1])
// FP32: 5 words/interval (start[2] + end[2] + gid[1])
// FP64: 9 words/interval (start[4] + end[4] + gid[1])
//================================================================================
template<FloatPrecision PRECISION>
inline void gen_interval_metadata_float(
    const std::string& dir,
    const std::vector<QuantizedGroup>& groups,
    const std::vector<size_t>& order,
    const std::map<size_t, size_t>& storage_map)
{
    std::ofstream f(dir + "/interval_metadata.mem");

    for (size_t si = 0; si < order.size(); si++) {
        const auto& group = groups[order[si]];

        for (size_t ii = 0; ii < group.members.size(); ii++) {
            const auto& member = group.members[ii];

            if (member.is_padding) {
                if constexpr (PRECISION == FloatPrecision::FP64) {
                    f << "0000\n0000\n0000\n0000\n0000\n0000\n0000\n0000\n0000\n";
                } else if constexpr (PRECISION == FloatPrecision::FP32) {
                    f << "0000\n0000\n0000\n0000\n0000\n";
                } else {
                    f << "0000\n0000\n0000\n";
                }
                continue;
            }

            if constexpr (PRECISION == FloatPrecision::FP64) {
                // FP64: 9 words (4 words per double + 1 word for gid)
                uint64_t start64 = float_to_fp64_hex(member.original_interval.start);
                f << std::hex << std::setfill('0') << std::setw(4) << ((start64 >> 48) & 0xFFFF) << "\n";
                f << std::hex << std::setfill('0') << std::setw(4) << ((start64 >> 32) & 0xFFFF) << "\n";
                f << std::hex << std::setfill('0') << std::setw(4) << ((start64 >> 16) & 0xFFFF) << "\n";
                f << std::hex << std::setfill('0') << std::setw(4) << (start64 & 0xFFFF) << "\n";

                uint64_t end64 = float_to_fp64_hex(member.original_interval.end);
                f << std::hex << std::setfill('0') << std::setw(4) << ((end64 >> 48) & 0xFFFF) << "\n";
                f << std::hex << std::setfill('0') << std::setw(4) << ((end64 >> 32) & 0xFFFF) << "\n";
                f << std::hex << std::setfill('0') << std::setw(4) << ((end64 >> 16) & 0xFFFF) << "\n";
                f << std::hex << std::setfill('0') << std::setw(4) << (end64 & 0xFFFF) << "\n";

                f << std::hex << std::setfill('0') << std::setw(4) << (si & 0xFFFF) << "\n";
            } else if constexpr (PRECISION == FloatPrecision::FP32) {
                // FP32: 5 words (high word first for each 32-bit value)
                uint32_t start32 = float_to_fp32_hex(static_cast<float>(member.original_interval.start));
                f << std::hex << std::setfill('0') << std::setw(4) << ((start32 >> 16) & 0xFFFF) << "\n";
                f << std::hex << std::setfill('0') << std::setw(4) << (start32 & 0xFFFF) << "\n";

                uint32_t end32 = float_to_fp32_hex(static_cast<float>(member.original_interval.end));
                f << std::hex << std::setfill('0') << std::setw(4) << ((end32 >> 16) & 0xFFFF) << "\n";
                f << std::hex << std::setfill('0') << std::setw(4) << (end32 & 0xFFFF) << "\n";

                f << std::hex << std::setfill('0') << std::setw(4) << (si & 0xFFFF) << "\n";
            } else {
                // FP16: 3 words
                uint16_t start = float_to_fp16_hex(static_cast<float>(member.original_interval.start));
                f << std::hex << std::setfill('0') << std::setw(4) << start << "\n";

                uint16_t end = float_to_fp16_hex(static_cast<float>(member.original_interval.end));
                f << std::hex << std::setfill('0') << std::setw(4) << end << "\n";

                f << std::hex << std::setfill('0') << std::setw(4) << (si & 0xFFFF) << "\n";
            }
        }
    }
}

//================================================================================
// Template: Generate group_bounds.mem
// FP16: 2 words/group (min[1] + max[1])
// FP32: 4 words/group (min[2] + max[2])
// FP64: 8 words/group (min[4] + max[4])
//================================================================================
template<FloatPrecision PRECISION>
inline void gen_group_bounds_float(
    const std::string& dir,
    const std::vector<QuantizedGroup>& groups,
    const std::vector<size_t>& order)
{
    std::ofstream f(dir + "/group_bounds.mem");

    for (size_t si = 0; si < order.size(); si++) {
        auto [min_x, max_x] = get_group_bounds(groups[order[si]]);

        if constexpr (PRECISION == FloatPrecision::FP64) {
            uint64_t min64 = float_to_fp64_hex(min_x);
            uint64_t max64 = float_to_fp64_hex(max_x);

            f << std::hex << std::setfill('0') << std::setw(4) << ((min64 >> 48) & 0xFFFF) << "\n";
            f << std::hex << std::setfill('0') << std::setw(4) << ((min64 >> 32) & 0xFFFF) << "\n";
            f << std::hex << std::setfill('0') << std::setw(4) << ((min64 >> 16) & 0xFFFF) << "\n";
            f << std::hex << std::setfill('0') << std::setw(4) << (min64 & 0xFFFF) << "\n";
            f << std::hex << std::setfill('0') << std::setw(4) << ((max64 >> 48) & 0xFFFF) << "\n";
            f << std::hex << std::setfill('0') << std::setw(4) << ((max64 >> 32) & 0xFFFF) << "\n";
            f << std::hex << std::setfill('0') << std::setw(4) << ((max64 >> 16) & 0xFFFF) << "\n";
            f << std::hex << std::setfill('0') << std::setw(4) << (max64 & 0xFFFF) << "\n";
        } else if constexpr (PRECISION == FloatPrecision::FP32) {
            uint32_t min32 = float_to_fp32_hex(static_cast<float>(min_x));
            uint32_t max32 = float_to_fp32_hex(static_cast<float>(max_x));

            f << std::hex << std::setfill('0') << std::setw(4) << ((min32 >> 16) & 0xFFFF) << "\n";
            f << std::hex << std::setfill('0') << std::setw(4) << (min32 & 0xFFFF) << "\n";
            f << std::hex << std::setfill('0') << std::setw(4) << ((max32 >> 16) & 0xFFFF) << "\n";
            f << std::hex << std::setfill('0') << std::setw(4) << (max32 & 0xFFFF) << "\n";
        } else {
            uint16_t qmin = float_to_fp16_hex(static_cast<float>(min_x));
            uint16_t qmax = float_to_fp16_hex(static_cast<float>(max_x));

            f << std::hex << std::setfill('0') << std::setw(4) << qmin << "\n";
            f << std::hex << std::setfill('0') << std::setw(4) << qmax << "\n";
        }
    }
}

//================================================================================
// Generate group_map.mem (same for all precisions)
// Format: 1 word/group (storage_index)
//================================================================================
inline void gen_group_map_float(
    const std::string& dir,
    const std::vector<QuantizedGroup>& groups,
    const std::map<size_t, size_t>& storage_map)
{
    std::ofstream f(dir + "/group_map.mem");

    for (size_t lid = 0; lid < groups.size(); lid++) {
        size_t si = storage_map.at(lid);
        f << std::hex << std::setfill('0') << std::setw(4) << (si & 0xFFFF) << "\n";
    }
}

//================================================================================
// Template: Generate group_info.mem
// FP16: 3 words/group (flags[1] + base_b[1] + base_c[1])
// FP32: 5 words/group (flags[1] + base_b[2] + base_c[2])
// FP64: 9 words/group (flags[1] + base_b[4] + base_c[4])
//================================================================================
template<FloatPrecision PRECISION>
inline void gen_group_info_float(
    const std::string& dir,
    const std::vector<QuantizedGroup>& groups,
    const std::vector<size_t>& order)
{
    std::ofstream f(dir + "/group_info.mem");

    for (size_t si = 0; si < order.size(); si++) {
        const auto& group = groups[order[si]];
        bool is_orphan = (group.storage_type == GroupStorageType::ORPHAN_GROUP);

        // Word 0: flags (bit[0] = orphan)
        uint16_t flags = is_orphan ? 1 : 0;
        f << std::hex << std::setfill('0') << std::setw(4) << flags << "\n";

        if constexpr (PRECISION == FloatPrecision::FP64) {
            // FP64: base_b and base_c are 4 words each
            uint64_t qb64 = float_to_fp64_hex(group.base_params.b);
            f << std::hex << std::setfill('0') << std::setw(4) << ((qb64 >> 48) & 0xFFFF) << "\n";
            f << std::hex << std::setfill('0') << std::setw(4) << ((qb64 >> 32) & 0xFFFF) << "\n";
            f << std::hex << std::setfill('0') << std::setw(4) << ((qb64 >> 16) & 0xFFFF) << "\n";
            f << std::hex << std::setfill('0') << std::setw(4) << (qb64 & 0xFFFF) << "\n";

            uint64_t qc64 = float_to_fp64_hex(group.base_params.c);
            f << std::hex << std::setfill('0') << std::setw(4) << ((qc64 >> 48) & 0xFFFF) << "\n";
            f << std::hex << std::setfill('0') << std::setw(4) << ((qc64 >> 32) & 0xFFFF) << "\n";
            f << std::hex << std::setfill('0') << std::setw(4) << ((qc64 >> 16) & 0xFFFF) << "\n";
            f << std::hex << std::setfill('0') << std::setw(4) << (qc64 & 0xFFFF) << "\n";
        } else if constexpr (PRECISION == FloatPrecision::FP32) {
            // FP32: base_b and base_c are 2 words each
            uint32_t qb32 = float_to_fp32_hex(static_cast<float>(group.base_params.b));
            f << std::hex << std::setfill('0') << std::setw(4) << ((qb32 >> 16) & 0xFFFF) << "\n";
            f << std::hex << std::setfill('0') << std::setw(4) << (qb32 & 0xFFFF) << "\n";

            uint32_t qc32 = float_to_fp32_hex(static_cast<float>(group.base_params.c));
            f << std::hex << std::setfill('0') << std::setw(4) << ((qc32 >> 16) & 0xFFFF) << "\n";
            f << std::hex << std::setfill('0') << std::setw(4) << (qc32 & 0xFFFF) << "\n";
        } else {
            // FP16: base_b and base_c are 1 word each
            uint16_t qb = float_to_fp16_hex(static_cast<float>(group.base_params.b));
            f << std::hex << std::setfill('0') << std::setw(4) << qb << "\n";

            uint16_t qc = float_to_fp16_hex(static_cast<float>(group.base_params.c));
            f << std::hex << std::setfill('0') << std::setw(4) << qc << "\n";
        }
    }
}

//================================================================================
// Template: Generate delta_data.mem
// FP16: 3 words/interval (delta_b[1] + delta_c[1] + flags[1])
// FP32: 5 words/interval (delta_b[2] + delta_c[2] + flags[1])
// FP64: 9 words/interval (delta_b[4] + delta_c[4] + flags[1])
//================================================================================
template<FloatPrecision PRECISION>
inline void gen_delta_data_float(
    const std::string& dir,
    const std::vector<QuantizedGroup>& groups,
    const std::vector<size_t>& order)
{
    std::ofstream f(dir + "/delta_data.mem");

    for (size_t si = 0; si < order.size(); si++) {
        const auto& group = groups[order[si]];

        for (const auto& member : group.members) {
            // Dequantize deltas back to floating-point
            double delta_b_val = dequantizeValue(member.delta_b_q,
                                                group.delta_b_scale,
                                                group.delta_b_offset);
            double delta_c_val = dequantizeValue(member.delta_c_q,
                                                group.delta_c_scale,
                                                group.delta_c_offset);

            if constexpr (PRECISION == FloatPrecision::FP64) {
                uint64_t delta_b64 = float_to_fp64_hex(delta_b_val);
                f << std::hex << std::setfill('0') << std::setw(4) << ((delta_b64 >> 48) & 0xFFFF) << "\n";
                f << std::hex << std::setfill('0') << std::setw(4) << ((delta_b64 >> 32) & 0xFFFF) << "\n";
                f << std::hex << std::setfill('0') << std::setw(4) << ((delta_b64 >> 16) & 0xFFFF) << "\n";
                f << std::hex << std::setfill('0') << std::setw(4) << (delta_b64 & 0xFFFF) << "\n";

                uint64_t delta_c64 = float_to_fp64_hex(delta_c_val);
                f << std::hex << std::setfill('0') << std::setw(4) << ((delta_c64 >> 48) & 0xFFFF) << "\n";
                f << std::hex << std::setfill('0') << std::setw(4) << ((delta_c64 >> 32) & 0xFFFF) << "\n";
                f << std::hex << std::setfill('0') << std::setw(4) << ((delta_c64 >> 16) & 0xFFFF) << "\n";
                f << std::hex << std::setfill('0') << std::setw(4) << (delta_c64 & 0xFFFF) << "\n";

                uint16_t flags = 0;
                if (member.is_x_reflected) flags |= 1;
                if (member.is_y_reflected) flags |= 2;
                f << std::hex << std::setfill('0') << std::setw(4) << flags << "\n";
            } else if constexpr (PRECISION == FloatPrecision::FP32) {
                uint32_t delta_b32 = float_to_fp32_hex(static_cast<float>(delta_b_val));
                f << std::hex << std::setfill('0') << std::setw(4) << ((delta_b32 >> 16) & 0xFFFF) << "\n";
                f << std::hex << std::setfill('0') << std::setw(4) << (delta_b32 & 0xFFFF) << "\n";

                uint32_t delta_c32 = float_to_fp32_hex(static_cast<float>(delta_c_val));
                f << std::hex << std::setfill('0') << std::setw(4) << ((delta_c32 >> 16) & 0xFFFF) << "\n";
                f << std::hex << std::setfill('0') << std::setw(4) << (delta_c32 & 0xFFFF) << "\n";

                uint16_t flags = 0;
                if (member.is_x_reflected) flags |= 1;
                if (member.is_y_reflected) flags |= 2;
                f << std::hex << std::setfill('0') << std::setw(4) << flags << "\n";
            } else {
                uint16_t delta_b = float_to_fp16_hex(static_cast<float>(delta_b_val));
                f << std::hex << std::setfill('0') << std::setw(4) << delta_b << "\n";

                uint16_t delta_c = float_to_fp16_hex(static_cast<float>(delta_c_val));
                f << std::hex << std::setfill('0') << std::setw(4) << delta_c << "\n";

                uint16_t flags = 0;
                if (member.is_x_reflected) flags |= 1;
                if (member.is_y_reflected) flags |= 2;
                f << std::hex << std::setfill('0') << std::setw(4) << flags << "\n";
            }
        }
    }
}

//================================================================================
// Template: Generate config.vh
//================================================================================
template<FloatPrecision PRECISION>
inline void gen_config_float(
    const std::string& dir,
    const std::string& func_name,
    const std::vector<QuantizedGroup>& groups)
{
    std::ofstream f(dir + "/" + func_name + "_config.vh");

    constexpr int data_width = static_cast<int>(PRECISION);

    size_t num_groups = groups.size();
    size_t total_intervals = count_intervals(groups);
    size_t max_intervals = max_intervals_per_group(groups);

    // Calculate required address widths
    size_t group_addr_width = (num_groups <= 1) ? 1 :
                             static_cast<size_t>(std::ceil(std::log2(num_groups)));
    size_t interval_addr_width = (max_intervals <= 1) ? 1 :
                                 static_cast<size_t>(std::ceil(std::log2(max_intervals)));
    size_t delta_addr_width = (total_intervals <= 1) ? 1 :
                              static_cast<size_t>(std::ceil(std::log2(total_intervals)));

    f << "`ifndef " << func_name << "_CONFIG_VH\n";
    f << "`define " << func_name << "_CONFIG_VH\n\n";

    f << "// Data width configuration (FP" << data_width << ")\n";
    f << "`define INPUT_DATA_WIDTH " << data_width << "\n";
    f << "`define OUTPUT_DATA_WIDTH " << data_width << "\n\n";

    f << "// Floating-point format (float" << data_width << ")\n";
    f << "`define FLOAT_FORMAT " << data_width << "\n\n";

    f << "// Group and interval counts\n";
    f << "`define OPT_NUM_GROUPS " << num_groups << "\n";
    f << "`define OPT_TOTAL_INTERVALS " << total_intervals << "\n";
    f << "`define OPT_MAX_INTERVALS_PER_GROUP " << max_intervals << "\n\n";

    f << "// Address widths (bit widths for indexing)\n";
    f << "`define OPT_GROUP_ADDR_WIDTH " << group_addr_width << "\n";
    f << "`define OPT_INTERVAL_ADDR_WIDTH " << interval_addr_width << "\n";
    f << "`define OPT_DELTA_ADDR_WIDTH " << delta_addr_width << "\n\n";

    if constexpr (PRECISION == FloatPrecision::FP64) {
        // FP64: Each value uses 4 words (64 bits = 4 × 16-bit words)
        f << "// Memory sizes (in 16-bit words, FP64 mode)\n";
        f << "`define OPT_INTERVAL_METADATA_SIZE " << (total_intervals * 9)
          << "  // start[4] + end[4] + gid[1]\n";
        f << "`define OPT_GROUP_BOUNDS_SIZE " << (num_groups * 8)
          << "  // min[4] + max[4]\n";
        f << "`define OPT_GROUP_MAP_SIZE " << num_groups << "\n";
        f << "`define OPT_GROUP_INFO_SIZE " << (num_groups * 9)
          << "  // flags[1] + b[4] + c[4]\n";
        f << "`define OPT_DELTA_DATA_SIZE " << (total_intervals * 9)
          << "  // delta_b[4] + delta_c[4] + flags[1]\n\n";
    } else if constexpr (PRECISION == FloatPrecision::FP32) {
        // FP32: Each value uses 2 words (32 bits = 2 × 16-bit words)
        f << "// Memory sizes (in 16-bit words, FP32 mode)\n";
        f << "`define OPT_INTERVAL_METADATA_SIZE " << (total_intervals * 5)
          << "  // start[2] + end[2] + gid[1]\n";
        f << "`define OPT_GROUP_BOUNDS_SIZE " << (num_groups * 4)
          << "  // min[2] + max[2]\n";
        f << "`define OPT_GROUP_MAP_SIZE " << num_groups << "\n";
        f << "`define OPT_GROUP_INFO_SIZE " << (num_groups * 5)
          << "  // flags[1] + b[2] + c[2]\n";
        f << "`define OPT_DELTA_DATA_SIZE " << (total_intervals * 5)
          << "  // delta_b[2] + delta_c[2] + flags[1]\n\n";
    } else {
        // FP16: Each value is 1 word (16 bits)
        f << "// Memory sizes (in 16-bit words, FP16 mode)\n";
        f << "`define OPT_INTERVAL_METADATA_SIZE " << (total_intervals * 3)
          << "  // start[1] + end[1] + gid[1]\n";
        f << "`define OPT_GROUP_BOUNDS_SIZE " << (num_groups * 2)
          << "  // min[1] + max[1]\n";
        f << "`define OPT_GROUP_MAP_SIZE " << num_groups << "\n";
        f << "`define OPT_GROUP_INFO_SIZE " << (num_groups * 3)
          << "  // flags[1] + b[1] + c[1]\n";
        f << "`define OPT_DELTA_DATA_SIZE " << (total_intervals * 3)
          << "  // delta_b[1] + delta_c[1] + flags[1]\n\n";
    }

    f << "`endif\n";
}

//================================================================================
// Explicit instantiation: FP16 version
//================================================================================
inline void generate_hw_files_float16(
    const std::string& dir,
    const std::string& func_name,
    const std::vector<QuantizedGroup>& groups,
    bool orphan_first = false)
{
    ensure_dir(dir);
    auto order = reorder_groups(groups, orphan_first);
    auto storage_map = create_storage_map(order);

    gen_interval_metadata_float<FloatPrecision::FP16>(dir, groups, order, storage_map);
    gen_group_bounds_float<FloatPrecision::FP16>(dir, groups, order);
    gen_group_map_float(dir, groups, storage_map);
    gen_group_info_float<FloatPrecision::FP16>(dir, groups, order);
    gen_delta_data_float<FloatPrecision::FP16>(dir, groups, order);
    gen_config_float<FloatPrecision::FP16>(dir, func_name, groups);

    std::cout << "  Generated FP16 files in: " << dir << "\n";
    std::cout << "    Groups: " << groups.size()
              << ", Intervals: " << count_intervals(groups) << "\n";
}

//================================================================================
// Explicit instantiation: FP32 version
//================================================================================
inline void generate_hw_files_float32(
    const std::string& dir,
    const std::string& func_name,
    const std::vector<QuantizedGroup>& groups,
    bool orphan_first = false)
{
    ensure_dir(dir);
    auto order = reorder_groups(groups, orphan_first);
    auto storage_map = create_storage_map(order);

    gen_interval_metadata_float<FloatPrecision::FP32>(dir, groups, order, storage_map);
    gen_group_bounds_float<FloatPrecision::FP32>(dir, groups, order);
    gen_group_map_float(dir, groups, storage_map);
    gen_group_info_float<FloatPrecision::FP32>(dir, groups, order);
    gen_delta_data_float<FloatPrecision::FP32>(dir, groups, order);
    gen_config_float<FloatPrecision::FP32>(dir, func_name, groups);

    std::cout << "  Generated FP32 files in: " << dir << "\n";
    std::cout << "    Groups: " << groups.size()
              << ", Intervals: " << count_intervals(groups) << "\n";
}

//================================================================================
// Explicit instantiation: FP64 version
//================================================================================
inline void generate_hw_files_float64(
    const std::string& dir,
    const std::string& func_name,
    const std::vector<QuantizedGroup>& groups,
    bool orphan_first = false)
{
    ensure_dir(dir);
    auto order = reorder_groups(groups, orphan_first);
    auto storage_map = create_storage_map(order);

    gen_interval_metadata_float<FloatPrecision::FP64>(dir, groups, order, storage_map);
    gen_group_bounds_float<FloatPrecision::FP64>(dir, groups, order);
    gen_group_map_float(dir, groups, storage_map);
    gen_group_info_float<FloatPrecision::FP64>(dir, groups, order);
    gen_delta_data_float<FloatPrecision::FP64>(dir, groups, order);
    gen_config_float<FloatPrecision::FP64>(dir, func_name, groups);

    std::cout << "  Generated FP64 files in: " << dir << "\n";
    std::cout << "    Groups: " << groups.size()
              << ", Intervals: " << count_intervals(groups) << "\n";
}

#endif // HW_FILE_GEN_FLOAT_HPP
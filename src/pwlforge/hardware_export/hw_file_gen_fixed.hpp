#ifndef HW_FILE_GEN_FIXED_HPP
#define HW_FILE_GEN_FIXED_HPP

#include <fstream>
#include <iomanip>
#include <cmath>
#include "group_types.hpp"
#include "group_quantization.hpp"
#include "hw_types.hpp"
#include "hw_utils.hpp"

//================================================================================
// Fixed-point precision configuration
//================================================================================
struct FixedPointConfig {
    int total_bits;   // 总位宽 (8, 16, 32, 64)
    int frac_bits;    // 小数位数

    FixedPointConfig(int total = 16, int frac = 15)
        : total_bits(total), frac_bits(frac) {}

    int words_needed() const {
        return (total_bits + 15) / 16;  // 向上取整到16位字
    }
};

//================================================================================
// Helper: Split quantized value into 16-bit words
//================================================================================
inline std::vector<uint16_t> split_to_words(int64_t value, int num_words) {
    std::vector<uint16_t> words;
    uint64_t unsigned_val = static_cast<uint64_t>(value);

    for (int i = 0; i < num_words; i++) {
        words.push_back(static_cast<uint16_t>((unsigned_val >> (i * 16)) & 0xFFFF));
    }

    return words;
}

//================================================================================
// Generate interval_metadata.mem (Fixed-point)
// FIXED-16: 3 words/interval (start[1] + end[1] + gid[1])
// FIXED-32: 5 words/interval (start[2] + end[2] + gid[1])
// FIXED-64: 9 words/interval (start[4] + end[4] + gid[1])
//================================================================================
inline void gen_interval_metadata_fixed(
    const std::string& dir,
    const std::vector<QuantizedGroup>& groups,
    const std::vector<size_t>& order,
    const std::map<size_t, size_t>& storage_map,
    const FixedPointConfig& config = FixedPointConfig(16, 15))
{
    std::ofstream f(dir + "/interval_metadata.mem");
    int wpv = config.words_needed();  // words per value

    for (size_t si = 0; si < order.size(); si++) {
        const auto& group = groups[order[si]];

        for (size_t ii = 0; ii < group.members.size(); ii++) {
            const auto& member = group.members[ii];

            if (member.is_padding) {
                // Padding: output zeros
                for (int i = 0; i < wpv * 2 + 1; i++) {
                    f << "0000\n";
                }
                continue;
            }

            // Word 0~(wpv-1): interval start (already quantized)
            int64_t start = static_cast<int64_t>(quant_pos(member.original_interval.start));
            auto start_words = split_to_words(start, wpv);
            for (const auto& word : start_words) {
                f << std::hex << std::setfill('0') << std::setw(4) << word << "\n";
            }

            // Word wpv~(2*wpv-1): interval end (already quantized)
            int64_t end = static_cast<int64_t>(quant_pos(member.original_interval.end));
            auto end_words = split_to_words(end, wpv);
            for (const auto& word : end_words) {
                f << std::hex << std::setfill('0') << std::setw(4) << word << "\n";
            }

            // Word 2*wpv: storage index (group_id)
            f << std::hex << std::setfill('0') << std::setw(4) << (si & 0xFFFF) << "\n";
        }
    }
}

//================================================================================
// Generate group_bounds.mem (Fixed-point)
// FIXED-16: 2 words/group (min[1] + max[1])
// FIXED-32: 4 words/group (min[2] + max[2])
// FIXED-64: 8 words/group (min[4] + max[4])
//================================================================================
inline void gen_group_bounds_fixed(
    const std::string& dir,
    const std::vector<QuantizedGroup>& groups,
    const std::vector<size_t>& order,
    const FixedPointConfig& config = FixedPointConfig(16, 15))
{
    std::ofstream f(dir + "/group_bounds.mem");
    int wpv = config.words_needed();

    for (size_t si = 0; si < order.size(); si++) {
        auto [min_x, max_x] = get_group_bounds(groups[order[si]]);

        // Already quantized
        int64_t qmin = static_cast<int64_t>(quant_pos(min_x));
        int64_t qmax = static_cast<int64_t>(quant_pos(max_x));

        auto min_words = split_to_words(qmin, wpv);
        for (const auto& word : min_words) {
            f << std::hex << std::setfill('0') << std::setw(4) << word << "\n";
        }

        auto max_words = split_to_words(qmax, wpv);
        for (const auto& word : max_words) {
            f << std::hex << std::setfill('0') << std::setw(4) << word << "\n";
        }
    }
}

//================================================================================
// Generate group_map.mem (same for all configurations)
// Format: 1 word/group (storage_index)
//================================================================================
inline void gen_group_map_fixed(
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
// Generate group_info.mem (Fixed-point)
// FIXED-16: 3 words/group (flags[1] + base_b[1] + base_c[1])
// FIXED-32: 5 words/group (flags[1] + base_b[2] + base_c[2])
// FIXED-64: 9 words/group (flags[1] + base_b[4] + base_c[4])
//================================================================================
inline void gen_group_info_fixed(
    const std::string& dir,
    const std::vector<QuantizedGroup>& groups,
    const std::vector<size_t>& order,
    const FixedPointConfig& config = FixedPointConfig(16, 15))
{
    std::ofstream f(dir + "/group_info.mem");
    int wpv = config.words_needed();

    for (size_t si = 0; si < order.size(); si++) {
        const auto& group = groups[order[si]];

        bool is_orphan = (group.storage_type == GroupStorageType::ORPHAN_GROUP);

        // Word 0: flags (bit[0] = orphan)
        uint16_t flags = is_orphan ? 1 : 0;
        f << std::hex << std::setfill('0') << std::setw(4) << flags << "\n";

        // Words 1~wpv: base_b (already quantized)
        int64_t qb = static_cast<int64_t>(quant_param(group.base_params.b));
        auto qb_words = split_to_words(qb, wpv);
        for (const auto& word : qb_words) {
            f << std::hex << std::setfill('0') << std::setw(4) << word << "\n";
        }

        // Words (wpv+1)~(2*wpv): base_c (already quantized)
        int64_t qc = static_cast<int64_t>(quant_param(group.base_params.c));
        auto qc_words = split_to_words(qc, wpv);
        for (const auto& word : qc_words) {
            f << std::hex << std::setfill('0') << std::setw(4) << word << "\n";
        }
    }
}

//================================================================================
// Generate delta_data.mem (Fixed-point)
// FIXED-16: 3 words/interval (delta_b[1] + delta_c[1] + flags[1])
// FIXED-32: 5 words/interval (delta_b[2] + delta_c[2] + flags[1])
// FIXED-64: 9 words/interval (delta_b[4] + delta_c[4] + flags[1])
//================================================================================
inline void gen_delta_data_fixed(
    const std::string& dir,
    const std::vector<QuantizedGroup>& groups,
    const std::vector<size_t>& order,
    const FixedPointConfig& config = FixedPointConfig(16, 15))
{
    std::ofstream f(dir + "/delta_data.mem");
    int wpv = config.words_needed();

    for (size_t si = 0; si < order.size(); si++) {
        const auto& group = groups[order[si]];

        for (const auto& member : group.members) {
            // Delta values are already quantized as delta_b_q and delta_c_q
            // They are stored as 16-bit values
            int64_t delta_b = static_cast<int64_t>(static_cast<int16_t>(member.delta_b_q));
            int64_t delta_c = static_cast<int64_t>(static_cast<int16_t>(member.delta_c_q));

            // Words 0~(wpv-1): delta_b (already quantized)
            auto delta_b_words = split_to_words(delta_b, wpv);
            for (const auto& word : delta_b_words) {
                f << std::hex << std::setfill('0') << std::setw(4) << word << "\n";
            }

            // Words wpv~(2*wpv-1): delta_c (already quantized)
            auto delta_c_words = split_to_words(delta_c, wpv);
            for (const auto& word : delta_c_words) {
                f << std::hex << std::setfill('0') << std::setw(4) << word << "\n";
            }

            // Word 2*wpv: reflection flags
            uint16_t flags = 0;
            if (member.is_x_reflected) flags |= 1;
            if (member.is_y_reflected) flags |= 2;
            f << std::hex << std::setfill('0') << std::setw(4) << flags << "\n";
        }
    }
}

//================================================================================
// Generate config.vh (Fixed-point)
//================================================================================
inline void gen_config_fixed(
    const std::string& dir,
    const std::string& func_name,
    const std::vector<QuantizedGroup>& groups,
    const FixedPointConfig& config = FixedPointConfig(16, 15))
{
    std::ofstream f(dir + "/" + func_name + "_config.vh");

    size_t num_groups = groups.size();
    size_t total_intervals = count_intervals(groups);
    size_t max_intervals = max_intervals_per_group(groups);
    int wpv = config.words_needed();

    // Calculate required address widths
    size_t group_addr_width = (num_groups <= 1) ? 1 :
                             static_cast<size_t>(std::ceil(std::log2(num_groups)));
    size_t interval_addr_width = (max_intervals <= 1) ? 1 :
                                 static_cast<size_t>(std::ceil(std::log2(max_intervals)));
    size_t delta_addr_width = (total_intervals <= 1) ? 1 :
                              static_cast<size_t>(std::ceil(std::log2(total_intervals)));

    f << "`ifndef " << func_name << "_CONFIG_VH\n";
    f << "`define " << func_name << "_CONFIG_VH\n\n";

    f << "// Data width configuration (FIXED-" << config.total_bits << ")\n";
    f << "`define INPUT_DATA_WIDTH " << config.total_bits << "\n";
    f << "`define OUTPUT_DATA_WIDTH " << config.total_bits << "\n\n";

    f << "// Fixed-point format\n";
    f << "`define OPT_FRAC_BITS " << config.frac_bits << "\n";
    f << "`define FIXED_POINT_SCALE " << (1LL << config.frac_bits) << "  // 2^" << config.frac_bits << "\n\n";

    f << "// Group and interval counts\n";
    f << "`define OPT_NUM_GROUPS " << num_groups << "\n";
    f << "`define OPT_TOTAL_INTERVALS " << total_intervals << "\n";
    f << "`define OPT_MAX_INTERVALS_PER_GROUP " << max_intervals << "\n\n";

    f << "// Address widths (bit widths for indexing)\n";
    f << "`define OPT_GROUP_ADDR_WIDTH " << group_addr_width << "\n";
    f << "`define OPT_INTERVAL_ADDR_WIDTH " << interval_addr_width << "\n";
    f << "`define OPT_DELTA_ADDR_WIDTH " << delta_addr_width << "\n\n";

    f << "// Memory sizes (in 16-bit words, FIXED-" << config.total_bits << " mode)\n";
    f << "`define OPT_INTERVAL_METADATA_SIZE " << (total_intervals * (2 * wpv + 1))
      << "  // start[" << wpv << "] + end[" << wpv << "] + gid[1]\n";
    f << "`define OPT_GROUP_BOUNDS_SIZE " << (num_groups * 2 * wpv)
      << "  // min[" << wpv << "] + max[" << wpv << "]\n";
    f << "`define OPT_GROUP_MAP_SIZE " << num_groups << "\n";
    f << "`define OPT_GROUP_INFO_SIZE " << (num_groups * (2 * wpv + 1))
      << "  // flags[1] + b[" << wpv << "] + c[" << wpv << "]\n";
    f << "`define OPT_DELTA_DATA_SIZE " << (total_intervals * (2 * wpv + 1))
      << "  // delta_b[" << wpv << "] + delta_c[" << wpv << "] + flags[1]\n\n";

    f << "`endif\n";
}

//================================================================================
// Generate all fixed-point hardware files
//================================================================================
inline void generate_hw_files_fixed(
    const std::string& dir,
    const std::string& func_name,
    const std::vector<QuantizedGroup>& groups,
    bool orphan_first = false,
    const FixedPointConfig& config = FixedPointConfig(16, 15))
{
    ensure_dir(dir);

    auto order = reorder_groups(groups, orphan_first);
    auto storage_map = create_storage_map(order);

    gen_interval_metadata_fixed(dir, groups, order, storage_map, config);
    gen_group_bounds_fixed(dir, groups, order, config);
    gen_group_map_fixed(dir, groups, storage_map);
    gen_group_info_fixed(dir, groups, order, config);
    gen_delta_data_fixed(dir, groups, order, config);
    gen_config_fixed(dir, func_name, groups, config);

    std::cout << "  Generated FIXED-" << config.total_bits << " (Q"
              << (config.total_bits - config.frac_bits) << "." << config.frac_bits
              << ") files in: " << dir << "\n";
    std::cout << "    Groups: " << groups.size()
              << ", Intervals: " << count_intervals(groups) << "\n";
}

//================================================================================
// Convenience functions for common fixed-point configurations
//================================================================================
inline void generate_hw_files_fixed8(
    const std::string& dir,
    const std::string& func_name,
    const std::vector<QuantizedGroup>& groups,
    bool orphan_first = false)
{
    generate_hw_files_fixed(dir, func_name, groups, orphan_first, FixedPointConfig(8, 7));
}

inline void generate_hw_files_fixed16(
    const std::string& dir,
    const std::string& func_name,
    const std::vector<QuantizedGroup>& groups,
    bool orphan_first = false)
{
    generate_hw_files_fixed(dir, func_name, groups, orphan_first, FixedPointConfig(16, 15));
}

inline void generate_hw_files_fixed32(
    const std::string& dir,
    const std::string& func_name,
    const std::vector<QuantizedGroup>& groups,
    bool orphan_first = false)
{
    generate_hw_files_fixed(dir, func_name, groups, orphan_first, FixedPointConfig(32, 31));
}

inline void generate_hw_files_fixed64(
    const std::string& dir,
    const std::string& func_name,
    const std::vector<QuantizedGroup>& groups,
    bool orphan_first = false)
{
    generate_hw_files_fixed(dir, func_name, groups, orphan_first, FixedPointConfig(64, 63));
}

#endif // HW_FILE_GEN_FIXED_HPP
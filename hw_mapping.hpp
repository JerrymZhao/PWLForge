#ifndef HW_MAPPING_HPP
#define HW_MAPPING_HPP

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <random>
#include <sstream>
#include <map>
#include <tuple>
#include <filesystem>
#include "interval_group_compressor.hpp"

// Helper function to ensure decimal formatting for all indices
std::string ensureDecimalIndex(int index) {
    std::stringstream ss;
    ss << std::dec << index;
    return ss.str();
}

// Analyze FPGA implementation resource usage and performance
void analyzeFPGAImplementation(const std::string& functionName,
                              const std::vector<IntervalGroup>& groups,
                              const std::vector<Interval>& intervals,
                              const std::vector<FitParameters>& fit_params,
                              double start, double end,
                              double scale_factor,
                              double target_error) {
    std::cout << "\n=== FPGA Parameter Analysis for " << functionName << " ===\n";
    
    // Count total intervals and calculate memory requirements
    size_t total_intervals = 0;
    size_t total_groups = groups.size();
    size_t orphan_groups = 0;
    size_t optimized_pow2_groups = 0;
    
    for (const auto& group : groups) {
        total_intervals += group.delta_encodings.size();
        if (group.storage_type == ORPHAN_GROUP) orphan_groups++;
        if (group.use_power_of_two) optimized_pow2_groups++;
    }
    
    // Calculate memory requirements
    size_t group_table_bits = total_groups * 256; // 8 words * 32 bits
    size_t delta_table_bits = total_intervals * 56; // deltas(3*16) + flags(8)
    size_t total_memory_bits = group_table_bits + delta_table_bits;
    
    // Calculate bit widths
    int data_width = std::ceil(std::log2(scale_factor));
    int group_addr_width = std::ceil(std::log2(groups.size()));
    int max_intervals_per_group = 0;
    
    for (const auto& group : groups) {
        max_intervals_per_group = std::max(max_intervals_per_group, 
                                          (int)group.delta_encodings.size());
    }
    int interval_addr_width = std::ceil(std::log2(max_intervals_per_group));
    
    // Display analysis results
    std::cout << "Function Domain: [" << start << ", " << end << "]\n";
    std::cout << "Scale Factor: " << scale_factor << " (" << data_width << " bits)\n";
    std::cout << "Target Error: " << target_error << "\n\n";
    
    std::cout << "LUT Parameters:\n";
    std::cout << "  Groups: " << total_groups 
              << " (Orphans: " << orphan_groups 
              << ", Power-of-two optimized: " << optimized_pow2_groups << ")\n";
    std::cout << "  Total Intervals: " << total_intervals << "\n";
    std::cout << "  Memory Requirements: " << (total_memory_bits / 8) << " bytes\n";
    std::cout << "  Group Address Width: " << group_addr_width << " bits\n";
    std::cout << "  Interval Address Width: " << interval_addr_width << " bits\n";
    std::cout << "  Data Width: " << data_width << " bits\n";
    std::cout << "=== Analysis Complete ===\n\n";
}

// Generate group ROM initialization file
void generateGroupROMFile(const std::string& directory, 
                         const std::string& functionName,
                         const std::vector<IntervalGroup>& groups,
                         double scale_factor) {
    std::string filename = directory + "/" + functionName + "_group_rom.mem";
    std::ofstream file(filename);
    
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filename << "\n";
        return;
    }
    
    // Write file header
    file << "// Group ROM initialization for " << functionName << "\n";
    file << "// Format: 32-bit words, hex format\n";
    file << "// Order: base_start, base_end, base_slope, base_intercept, control_word, start_scale, slope_scale, intercept_scale\n\n";
    
    // Track offset for delta ROM
    size_t delta_offset = 0;
    
    // Write group data
    for (const auto& group : groups) {
        // Base parameters in fixed-point format
        int32_t q_start = static_cast<int32_t>(std::round(group.base_interval.start * scale_factor));
        int32_t q_end = static_cast<int32_t>(std::round(group.base_interval.end * scale_factor));
        int32_t q_b = static_cast<int32_t>(std::round(group.base_params.b * scale_factor));
        int32_t q_c = static_cast<int32_t>(std::round(group.base_params.c * scale_factor));
        
        // Pack control word
        uint32_t control = 0;
        control |= (group.storage_type == ORPHAN_GROUP) ? 1 : 0;           // bit 0: is_orphan
        control |= (group.use_power_of_two ? 1 : 0) << 1;                  // bit 1: use_pow2
        control |= (group.shift_amount & 0x1F) << 2;                       // bits 2-6: shift_amount (5 bits)
        control |= (group.delta_encodings.size() & 0xFFFF) << 7;           // bits 7-22: interval_count (16 bits)
        control |= (delta_offset & 0x1FF) << 23;                           // bits 23-31: delta_offset (9 bits)
        
        // Scale factors in fixed-point
        int32_t q_start_scale = static_cast<int32_t>(std::round(group.start_scale_factor * scale_factor));
        int32_t q_slope_scale = static_cast<int32_t>(std::round(group.slope_scale_factor * scale_factor));
        int32_t q_intercept_scale = static_cast<int32_t>(std::round(group.intercept_scale_factor * scale_factor));
        
        // Write as hex values, one word per line
        file << std::hex << std::setfill('0') << std::setw(8) << (q_start & 0xFFFFFFFF) << "\n";
        file << std::hex << std::setfill('0') << std::setw(8) << (q_end & 0xFFFFFFFF) << "\n";
        file << std::hex << std::setfill('0') << std::setw(8) << (q_b & 0xFFFFFFFF) << "\n";
        file << std::hex << std::setfill('0') << std::setw(8) << (q_c & 0xFFFFFFFF) << "\n";
        file << std::hex << std::setfill('0') << std::setw(8) << control << "\n";
        file << std::hex << std::setfill('0') << std::setw(8) << (q_start_scale & 0xFFFFFFFF) << "\n";
        file << std::hex << std::setfill('0') << std::setw(8) << (q_slope_scale & 0xFFFFFFFF) << "\n";
        file << std::hex << std::setfill('0') << std::setw(8) << (q_intercept_scale & 0xFFFFFFFF) << "\n";
        
        // Update delta offset for next group
        delta_offset += group.delta_encodings.size();
    }
    
    file.close();
    std::cout << "Group ROM initialization file generated: " << filename << "\n";
}

// Generate delta ROM initialization file
void generateDeltaROMFile(const std::string& directory, 
                         const std::string& functionName,
                         const std::vector<IntervalGroup>& groups) {
    std::string filename = directory + "/" + functionName + "_delta_rom.mem";
    std::ofstream file(filename);
    
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filename << "\n";
        return;
    }
    
    // Write file header
    file << "// Delta ROM initialization for " << functionName << "\n";
    file << "// Format: 16/8-bit values packed into 32-bit words, hex format\n";
    file << "// Order: delta_start(16), delta_slope(16), delta_intercept(16), flags(8)\n\n";
    
    // Write delta data
    for (const auto& group : groups) {
        for (const auto& delta : group.delta_encodings) {
            if (group.storage_type == ORPHAN_GROUP) {
                // Orphans use default values
                file << "00000000\n"; // delta_start (unused)
                file << "00000000\n"; // delta_slope (unused)
                file << "00000000\n"; // delta_intercept (unused)
                file << "00000000\n"; // flags (unused)
            } else {
                // Calculate quantized delta values
                int16_t q_delta_start = static_cast<int16_t>(std::round(
                    delta.delta_start / group.start_scale_factor));
                int16_t q_delta_slope = static_cast<int16_t>(std::round(
                    delta.delta_slope / group.slope_scale_factor));
                int16_t q_delta_intercept = static_cast<int16_t>(std::round(
                    delta.delta_intercept / group.intercept_scale_factor));
                
                // Create flags byte (bit 0: y-reflect, bit 1: x-reflect)
                uint8_t flags = (delta.is_y_reflected ? 1 : 0) | (delta.is_x_reflected ? 2 : 0);
                
                // Write as hex values
                file << std::hex << std::setfill('0') << std::setw(8) << (q_delta_start & 0xFFFF) << "\n";
                file << std::hex << std::setfill('0') << std::setw(8) << (q_delta_slope & 0xFFFF) << "\n";
                file << std::hex << std::setfill('0') << std::setw(8) << (q_delta_intercept & 0xFFFF) << "\n";
                file << std::hex << std::setfill('0') << std::setw(8) << (flags & 0xFF) << "\n";
            }
        }
    }
    
    file.close();
    std::cout << "Delta ROM initialization file generated: " << filename << "\n";
}

// Generate Verilog header (.vh) with function parameters and constants
void generateVerilogHeader(const std::string& directory, 
                          const std::string& functionName,
                          const std::vector<IntervalGroup>& groups,
                          double scale_factor,
                          double start, double end) {
    std::string filename = directory + "/" + functionName + "_params.vh";
    std::ofstream file(filename);
    
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filename << "\n";
        return;
    }
    
    // Calculate bit widths based on scale factor and ranges
    int data_width = std::ceil(std::log2(scale_factor));
    int addr_width = std::ceil(std::log2(groups.size()));
    int interval_addr_width = 0;
    
    // Find max intervals in any group for address width calculation
    for (const auto& group : groups) {
        int width = std::ceil(std::log2(group.delta_encodings.size()));
        interval_addr_width = std::max(interval_addr_width, width);
    }
    
    // Write header comment
    file << "// Verilog parameters for " << functionName << " function\n";
    file << "// Auto-generated parameters for PWL implementation\n";
    file << "// Domain: [" << start << ", " << end << "]\n\n";
    
    // Write parameters
    file << "`ifndef " << functionName << "_PARAMS_VH\n";
    file << "`define " << functionName << "_PARAMS_VH\n\n";
    
    // Basic parameters
    file << "// Function parameters\n";
    file << "`define " << functionName << "_DOMAIN_START " << start << "\n";
    file << "`define " << functionName << "_DOMAIN_END " << end << "\n";
    file << "`define " << functionName << "_SCALE_FACTOR " << scale_factor << "\n";
    file << "`define " << functionName << "_DATA_WIDTH " << data_width << "\n";
    
    // Group parameters
    file << "\n// Group parameters\n";
    file << "`define " << functionName << "_GROUP_COUNT " << groups.size() << "\n";
    file << "`define " << functionName << "_GROUP_ADDR_WIDTH " << addr_width << "\n";
    file << "`define " << functionName << "_INTERVAL_ADDR_WIDTH " << interval_addr_width << "\n";
    
    // Special optimization parameters
    int pow2_groups = 0;
    for (const auto& group : groups) {
        if (group.use_power_of_two) pow2_groups++;
    }
    file << "\n// Optimization parameters\n";
    file << "`define " << functionName << "_POW2_OPTIMIZED_GROUPS " << pow2_groups << "\n";
    
    // Close include guard
    file << "\n`endif // " << functionName << "_PARAMS_VH\n";
    
    file.close();
    std::cout << "Verilog header file generated: " << filename << "\n";
}

// Generate optimized Verilog LUTs
void generateOptimizedVerilogLUTs(const std::string& directory,
                                  const std::string& cleanName,
                                  const std::vector<IntervalGroup>& groups,
                                  int scale_factor, int frac_bits,
                                  int alignment_mode = 0) { // 0=compact, 1=16-bit aligned, 2=32-bit aligned

    std::cout << "\nGenerating optimized Verilog LUT data for " << cleanName
              << " using " << (alignment_mode == 0 ? "compact" :
                             (alignment_mode == 1 ? "16-bit aligned" : "32-bit aligned"))
              << " layout with power-of-two optimization...\n";

    // Ensure path doesn't end with slash
    std::string dir = directory;
    if (!dir.empty() && (dir.back() == '/' || dir.back() == '\\')) {
        dir.pop_back();
    }

    // Define output files
    std::string inline_vh_filename = dir + "/" + cleanName + "_inline_lut_data.vh";
    std::string replace_file = dir + "/" + cleanName + "_replace_memory_section.v";
    std::string bitwidth_file = dir + "/" + cleanName + "_optimized_bitwidths.vh";

    // ---------------------------------------------------
    // Analyze parameter ranges for optimal bit width
    // ---------------------------------------------------

    // Parameter analysis structures
    struct BitWidths {
        // Group parameter widths
        int group_start;
        int group_end;
        int base_b;
        int base_c;
        int flags; // Only 1 bit needed for storage type
        int size; // Number of intervals in group
        int offset; // Group offset
        int start_scale;
        int slope_scale;
        int intercept_scale;
        int pow2_flags; // Power-of-two flags (use_pow2 + shift_amount)

        // Delta parameter widths
        int delta_start;
        int delta_slope;
        int delta_intercept;
        int reflection; // 2 bits: x-reflection + y-reflection

        // Storage requirements
        int group_entry_bits; // Total bits per group entry
        int delta_entry_bits; // Total bits per delta entry

        // Memory layout description
        std::vector<std::tuple<std::string, int, int>> group_fields; // field_name, start_bit, width
        std::vector<std::tuple<std::string, int, int>> delta_fields; // field_name, start_bit, width
    };

    BitWidths widths;

    // Collect all parameter values for analysis
    std::vector<double> group_starts, group_ends;
    std::vector<double> base_b_values, base_c_values;
    std::vector<double> start_scales, slope_scales, intercept_scales;
    std::vector<int> quant_delta_starts, quant_delta_slopes, quant_delta_intercepts;
    std::vector<int> group_sizes, group_offsets;
    std::vector<int> shift_amounts;

    size_t total_intervals = 0;
    size_t max_intervals_per_group = 0;

    // First pass - analyze groups and compute power-of-two values
    std::vector<IntervalGroup> optimized_groups = groups;  // Create a copy to modify
    
    for (size_t i = 0; i < optimized_groups.size(); i++) {
        auto& group = optimized_groups[i];
        size_t interval_count = group.delta_encodings.size();
        
        // Calculate power-of-two optimization
        if (interval_count > 1 && group.storage_type != ORPHAN_GROUP) {
            int power_of_two = 1;
            int shift_amount = 0;
            
            // Find largest power of two that is <= interval_count
            while (power_of_two * 2 <= static_cast<int>(interval_count)) {
                power_of_two *= 2;
                shift_amount++;
            }
            
            // Set optimization parameters
            group.use_power_of_two = (power_of_two == static_cast<int>(interval_count));
            group.power_of_two_value = power_of_two;
            group.shift_amount = shift_amount;
            
            shift_amounts.push_back(shift_amount);
        } else {
            // Non-optimizable group
            group.use_power_of_two = false;
            group.power_of_two_value = 0;
            group.shift_amount = 0;
        }
    }

    // Analyze each group's parameters
    for (size_t i = 0; i < optimized_groups.size(); i++) {
        const auto& group = optimized_groups[i];

        group_starts.push_back(group.base_interval.start);
        group_ends.push_back(group.base_interval.end);
        base_b_values.push_back(group.base_params.b);
        base_c_values.push_back(group.base_params.c);

        start_scales.push_back(group.start_scale_factor);
        slope_scales.push_back(group.slope_scale_factor);
        intercept_scales.push_back(group.intercept_scale_factor);

        size_t group_size = group.delta_encodings.size();
        group_sizes.push_back(group_size);
        group_offsets.push_back(total_intervals);

        total_intervals += group_size;
        max_intervals_per_group = std::max(max_intervals_per_group, group_size);

        // Collect quantized delta parameters
        for (const auto& delta : group.delta_encodings) {
            int q_delta_start = static_cast<int>(std::round(delta.delta_start / group.start_scale_factor));
            int q_delta_slope = static_cast<int>(std::round(delta.delta_slope / group.slope_scale_factor));
            int q_delta_intercept = static_cast<int>(std::round(delta.delta_intercept / group.intercept_scale_factor));

            quant_delta_starts.push_back(q_delta_start);
            quant_delta_slopes.push_back(q_delta_slope);
            quant_delta_intercepts.push_back(q_delta_intercept);
        }
    }

    // Helper functions for bit width analysis
    auto find_minmax = [](const std::vector<double>& values) -> std::pair<double, double> {
        if (values.empty()) return {0.0, 0.0};
        auto [min_it, max_it] = std::minmax_element(values.begin(), values.end());
        return {*min_it, *max_it};
    };

    auto find_int_minmax = [](const std::vector<int>& values) -> std::pair<int, int> {
        if (values.empty()) return {0, 0};
        auto [min_it, max_it] = std::minmax_element(values.begin(), values.end());
        return {*min_it, *max_it};
    };

    auto calc_bits_needed = [](double min_val, double max_val, bool is_signed = true) -> int {
        double abs_max = std::max(std::abs(min_val), std::abs(max_val));
        if (abs_max < 1e-10) return is_signed ? 1 : 0;

        int int_bits = std::ceil(std::log2(abs_max + 1));
        return int_bits + (is_signed && (min_val < 0 || max_val < 0) ? 1 : 0);
    };

    // Calculate parameter ranges
    auto [min_start, max_start] = find_minmax(group_starts);
    auto [min_end, max_end] = find_minmax(group_ends);
    auto [min_b, max_b] = find_minmax(base_b_values);
    auto [min_c, max_c] = find_minmax(base_c_values);
    auto [min_start_scale, max_start_scale] = find_minmax(start_scales);
    auto [min_slope_scale, max_slope_scale] = find_minmax(slope_scales);
    auto [min_intercept_scale, max_intercept_scale] = find_minmax(intercept_scales);

    auto [min_delta_start, max_delta_start] = find_int_minmax(quant_delta_starts);
    auto [min_delta_slope, max_delta_slope] = find_int_minmax(quant_delta_slopes);
    auto [min_delta_intercept, max_delta_intercept] = find_int_minmax(quant_delta_intercepts);

    // Calculate max shift amount (usually small)
    int max_shift_amount = shift_amounts.empty() ? 0 : *std::max_element(shift_amounts.begin(), shift_amounts.end());

    // Calculate optimal bit widths based on parameter ranges
    widths.group_start = calc_bits_needed(min_start, max_start) + frac_bits;
    widths.group_end = calc_bits_needed(min_end, max_end) + frac_bits;
    widths.base_b = calc_bits_needed(min_b, max_b) + frac_bits;
    widths.base_c = calc_bits_needed(min_c, max_c) + frac_bits;
    widths.flags = 1; // Only 1 bit for storage type (normal/orphan)
    widths.size = std::ceil(std::log2(max_intervals_per_group + 1));
    widths.offset = std::ceil(std::log2(total_intervals + 1));
    widths.start_scale = calc_bits_needed(min_start_scale, max_start_scale) + frac_bits;
    widths.slope_scale = calc_bits_needed(min_slope_scale, max_slope_scale) + frac_bits;
    widths.intercept_scale = calc_bits_needed(min_intercept_scale, max_intercept_scale) + frac_bits;
    widths.pow2_flags = 1 + std::ceil(std::log2(max_shift_amount + 1)); // use_pow2(1) + shift_amount
    widths.delta_start = calc_bits_needed(min_delta_start, max_delta_start);
    widths.delta_slope = calc_bits_needed(min_delta_slope, max_delta_slope);
    widths.delta_intercept = calc_bits_needed(min_delta_intercept, max_delta_intercept);
    widths.reflection = 2; // 2 bits: x-reflection + y-reflection

    // ---------------------------------------------------
    // Adjust bit widths based on alignment mode
    // ---------------------------------------------------

    switch (alignment_mode) {
        case 0: { // Compact mode - align to 8-bit boundaries
            // Round up to multiples of 8
            widths.group_start = ((widths.group_start + 7) / 8) * 8;
            widths.group_end = ((widths.group_end + 7) / 8) * 8;
            widths.base_b = ((widths.base_b + 7) / 8) * 8;
            widths.base_c = ((widths.base_c + 7) / 8) * 8;

            // Pack small fields together
            int control_bits = widths.flags + widths.size + widths.reflection + widths.pow2_flags;
            control_bits = ((control_bits + 7) / 8) * 8;

            widths.offset = ((widths.offset + 7) / 8) * 8;
            widths.start_scale = ((widths.start_scale + 7) / 8) * 8;
            widths.slope_scale = ((widths.slope_scale + 7) / 8) * 8;
            widths.intercept_scale = ((widths.intercept_scale + 7) / 8) * 8;
            widths.delta_start = ((widths.delta_start + 7) / 8) * 8;
            widths.delta_slope = ((widths.delta_slope + 7) / 8) * 8;
            widths.delta_intercept = ((widths.delta_intercept + 7) / 8) * 8;
            break;
        }

        case 1: { // 16-bit aligned mode
            widths.group_start = 16;
            widths.group_end = 16;
            widths.base_b = 16;
            widths.base_c = 16;
            widths.flags = 1;
            widths.size = 8;
            widths.offset = 16;
            widths.start_scale = 16;
            widths.slope_scale = 16;
            widths.intercept_scale = 16;
            widths.pow2_flags = 6; // 1 bit for use_pow2 + 5 bits for shift_amount
            widths.delta_start = 16;
            widths.delta_slope = 16;
            widths.delta_intercept = 16;
            widths.reflection = 2;
            break;
        }

        case 2: { // 32-bit aligned mode - high precision
            widths.group_start = 32;
            widths.group_end = 32;
            widths.base_b = 32;
            widths.base_c = 32;
            widths.flags = 1;
            widths.size = 8;
            widths.offset = 16;
            widths.pow2_flags = 6; // 1 bit for use_pow2 + 5 bits for shift_amount
            widths.start_scale = 32;
            widths.slope_scale = 32;
            widths.intercept_scale = 32;
            widths.delta_start = 32;
            widths.delta_slope = 32;
            widths.delta_intercept = 32;
            widths.reflection = 2;
            break;
        }
    }

    // ---------------------------------------------------
    // Create memory layout
    // ---------------------------------------------------

    int group_bits = 0; // Current group bit counter
    int delta_bits = 0; // Current delta bit counter

    if (alignment_mode == 0) {
        // Compact mode layout
        // Group info layout - compact mode
        widths.group_fields.push_back({"group_start", group_bits, widths.group_start});
        group_bits += widths.group_start;

        widths.group_fields.push_back({"group_end", group_bits, widths.group_end});
        group_bits += widths.group_end;

        widths.group_fields.push_back({"base_b", group_bits, widths.base_b});
        group_bits += widths.base_b;

        widths.group_fields.push_back({"base_c", group_bits, widths.base_c});
        group_bits += widths.base_c;

        // Pack control fields
        widths.group_fields.push_back({"flags", group_bits, widths.flags});
        group_bits += widths.flags;

        widths.group_fields.push_back({"size", group_bits, widths.size});
        group_bits += widths.size;
        
        // Add power-of-two optimization flags
        widths.group_fields.push_back({"pow2_flags", group_bits, widths.pow2_flags});
        group_bits += widths.pow2_flags;

        // Align to 8-bit boundary
        int padding = (8 - (group_bits % 8)) % 8;
        if (padding > 0) {
            widths.group_fields.push_back({"padding", group_bits, padding});
            group_bits += padding;
        }

        widths.group_fields.push_back({"offset", group_bits, widths.offset});
        group_bits += widths.offset;

        widths.group_fields.push_back({"start_scale", group_bits, widths.start_scale});
        group_bits += widths.start_scale;

        widths.group_fields.push_back({"slope_scale", group_bits, widths.slope_scale});
        group_bits += widths.slope_scale;

        widths.group_fields.push_back({"intercept_scale", group_bits, widths.intercept_scale});
        group_bits += widths.intercept_scale;

        widths.group_entry_bits = group_bits;

        // Delta layout - compact mode
        widths.delta_fields.push_back({"delta_start", delta_bits, widths.delta_start});
        delta_bits += widths.delta_start;

        widths.delta_fields.push_back({"delta_slope", delta_bits, widths.delta_slope});
        delta_bits += widths.delta_slope;

        widths.delta_fields.push_back({"delta_intercept", delta_bits, widths.delta_intercept});
        delta_bits += widths.delta_intercept;

        widths.delta_fields.push_back({"reflection", delta_bits, widths.reflection});
        delta_bits += widths.reflection;

        // Align to 8-bit boundary
        padding = (8 - (delta_bits % 8)) % 8;
        if (padding > 0) {
            widths.delta_fields.push_back({"padding", delta_bits, padding});
            delta_bits += padding;
        }

        widths.delta_entry_bits = delta_bits;
    }
    else if (alignment_mode == 1) {
        // 16-bit aligned layout - optimized for linear fitting with power-of-two acceleration
        // Group info layout - now 10 x 16-bit fields (added power-of-two control word)
        widths.group_entry_bits = 16 * 10;

        // Field 0: group_start (16 bits)
        widths.group_fields.push_back({"group_start", 0, 16});

        // Field 1: group_end (16 bits)
        widths.group_fields.push_back({"group_end", 16, 16});

        // Field 2: base_b (16 bits)
        widths.group_fields.push_back({"base_b", 32, 16});

        // Field 3: base_c (16 bits)
        widths.group_fields.push_back({"base_c", 48, 16});

        // Field 4: packed control (16 bits) - contains flags(1) + size(8) + reserved(7)
        widths.group_fields.push_back({"flags", 64, 1});
        widths.group_fields.push_back({"size", 65, 8});
        widths.group_fields.push_back({"reserved1", 73, 7});

        // Field 5: power-of-two control (16 bits) - contains use_pow2(1) + shift_amount(5) + reserved(10)
        widths.group_fields.push_back({"use_pow2", 80, 1});
        widths.group_fields.push_back({"shift_amount", 81, 5});
        widths.group_fields.push_back({"reserved2", 86, 10});

        // Field 6: offset (16 bits)
        widths.group_fields.push_back({"offset", 96, 16});

        // Field 7: start_scale (16 bits)
        widths.group_fields.push_back({"start_scale", 112, 16});

        // Field 8: slope_scale (16 bits)
        widths.group_fields.push_back({"slope_scale", 128, 16});

        // Field 9: intercept_scale (16 bits)
        widths.group_fields.push_back({"intercept_scale", 144, 16});

        // Delta layout - 4 x 16-bit fields (unchanged)
        widths.delta_entry_bits = 16 * 4;

        // Field 0: delta_start (16 bits)
        widths.delta_fields.push_back({"delta_start", 0, 16});

        // Field 1: delta_slope (16 bits)
        widths.delta_fields.push_back({"delta_slope", 16, 16});

        // Field 2: delta_intercept (16 bits)
        widths.delta_fields.push_back({"delta_intercept", 32, 16});

        // Field 3: packed reflection flags (16 bits) - only 2 bits used, rest reserved
        widths.delta_fields.push_back({"reflection", 48, 2});
        widths.delta_fields.push_back({"reserved", 50, 14});
    }
    else {
        // 32-bit aligned layout - optimized for linear fitting with power-of-two acceleration and dynamic scale packing
        // Group info layout - now variable number of 32-bit fields, depending on scale factor size
        widths.group_entry_bits = 32 * 8; // Maximum size if 3 scale words needed (5 standard + 3 scale)

        // Field 0: group_start (32 bits)
        widths.group_fields.push_back({"group_start", 0, 32});

        // Field 1: group_end (32 bits)
        widths.group_fields.push_back({"group_end", 32, 32});

        // Field 2: base_b (32 bits)
        widths.group_fields.push_back({"base_b", 64, 32});

        // Field 3: base_c (32 bits)
        widths.group_fields.push_back({"base_c", 96, 32});

        // Field 4: expanded control word (32 bits) - now includes power-of-two flags
        // Contains flags(1) + size(8) + use_pow2(1) + shift_amount(5) + offset(16) + reserved(1)
        widths.group_fields.push_back({"flags", 128, 1});
        widths.group_fields.push_back({"size", 129, 8});
        widths.group_fields.push_back({"use_pow2", 137, 1});
        widths.group_fields.push_back({"shift_amount", 138, 5});
        widths.group_fields.push_back({"offset", 143, 16});
        widths.group_fields.push_back({"reserved", 159, 1});

        // Field 5: scale factor packing info (32 bits)
        widths.group_fields.push_back({"scale_packing_info", 160, 32});

        // Fields 6-8: scale values (variable number of 32-bit words)
        widths.group_fields.push_back({"scale_values", 192, 96});

        // Delta layout - 3 x 32-bit fields (unchanged)
        widths.delta_entry_bits = 32 * 3;

        // Field 0: delta_start (32 bits)
        widths.delta_fields.push_back({"delta_start", 0, 32});

        // Field 1: delta_slope (32 bits)
        widths.delta_fields.push_back({"delta_slope", 32, 32});

        // Field 2: delta_intercept + reflection (32 bits)
        widths.delta_fields.push_back({"delta_intercept", 64, 30});
        widths.delta_fields.push_back({"reflection", 94, 2});
    }

    // ---------------------------------------------------
    // Generate optimized bit width configuration file
    // ---------------------------------------------------

    std::ofstream bw_file(bitwidth_file);
    if (bw_file.is_open()) {
        bw_file << "// Auto-generated bit width definitions for " << cleanName << "\n";
        bw_file << "// Generated using " << (alignment_mode == 0 ? "compact" :
                                           (alignment_mode == 1 ? "16-bit aligned" : "32-bit aligned"))
                << " memory layout with power-of-two optimization\n\n";

        bw_file << "`ifndef " << cleanName << "_OPTIMIZED_BITWIDTHS_VH\n";
        bw_file << "`define " << cleanName << "_OPTIMIZED_BITWIDTHS_VH\n\n";

        bw_file << "// Common parameters\n";
        bw_file << "`define OPT_FRAC_BITS " << frac_bits << "\n";
        bw_file << "`define OPT_SCALE_FACTOR " << scale_factor << "\n";
        bw_file << "`define OPT_NUM_GROUPS " << optimized_groups.size() << "\n";
        bw_file << "`define OPT_TOTAL_INTERVALS " << total_intervals << "\n";
        bw_file << "`define OPT_MAX_INTERVALS_PER_GROUP " << max_intervals_per_group << "\n\n";

        bw_file << "// Power-of-two optimization\n";
        bw_file << "`define OPT_USE_POWER_OF_TWO 1\n\n";

        // For 32-bit mode with dynamic scale packing
        if (alignment_mode == 2) {
            bw_file << "// Dynamic scale packing\n";
            bw_file << "`define OPT_DYNAMIC_SCALE_PACKING 1\n";
            bw_file << "`define OPT_MAX_SCALE_WORDS 3\n\n";
        }

        bw_file << "// Group entry parameters\n";
        bw_file << "`define OPT_GROUP_ENTRY_BITS " << widths.group_entry_bits << "\n";
        bw_file << "`define OPT_GROUP_ENTRY_BYTES " << (widths.group_entry_bits + 7) / 8 << "\n";

        // Output field positions and widths
        for (const auto& [name, pos, width] : widths.group_fields) {
            if (name == "padding" || name == "reserved" || name == "reserved1" || name == "reserved2") continue;
            std::string upper_name = name;
            std::transform(upper_name.begin(), upper_name.end(), upper_name.begin(), ::toupper);
            bw_file << "`define OPT_GROUP_" << upper_name << "_POS " << pos << "\n";
            bw_file << "`define OPT_GROUP_" << upper_name << "_WIDTH " << width << "\n";
        }

        bw_file << "\n// Delta entry parameters\n";
        bw_file << "`define OPT_DELTA_ENTRY_BITS " << widths.delta_entry_bits << "\n";
        bw_file << "`define OPT_DELTA_ENTRY_BYTES " << (widths.delta_entry_bits + 7) / 8 << "\n";

        // Output field positions and widths
        for (const auto& [name, pos, width] : widths.delta_fields) {
            if (name == "padding" || name == "reserved") continue;
            std::string upper_name = name;
            std::transform(upper_name.begin(), upper_name.end(), upper_name.begin(), ::toupper);
            bw_file << "`define OPT_DELTA_" << upper_name << "_POS " << pos << "\n";
            bw_file << "`define OPT_DELTA_" << upper_name << "_WIDTH " << width << "\n";
        }

        bw_file << "\n// Memory size requirements\n";
        int total_group_bits = optimized_groups.size() * widths.group_entry_bits;
        int total_delta_bits = total_intervals * widths.delta_entry_bits;
        bw_file << "`define OPT_TOTAL_GROUP_BITS " << total_group_bits << "\n";
        bw_file << "`define OPT_TOTAL_DELTA_BITS " << total_delta_bits << "\n";
        bw_file << "`define OPT_TOTAL_MEMORY_BITS " << (total_group_bits + total_delta_bits) << "\n";
        bw_file << "`define OPT_TOTAL_MEMORY_BYTES " << ((total_group_bits + total_delta_bits + 7) / 8) << "\n";

        bw_file << "\n`endif // " << cleanName << "_OPTIMIZED_BITWIDTHS_VH\n";
        bw_file.close();
        std::cout << "Optimized bit width configuration saved to: " << bitwidth_file << "\n";
    }

    // ---------------------------------------------------
    // Generate inline Verilog LUT data
    // ---------------------------------------------------

    std::ofstream vh_file(inline_vh_filename);
    if (!vh_file.is_open()) {
        std::cerr << "Failed to open file: " << inline_vh_filename << "\n";
        return;
    }

    vh_file << "// Auto-generated inline LUT data for " << cleanName << " function\n";
    vh_file << "// Generated using " << (alignment_mode == 0 ? "compact" :
                                       (alignment_mode == 1 ? "16-bit aligned" : "32-bit aligned"))
            << " bit layout with power-of-two optimization\n\n";

    // Output data initialization code based on alignment mode
    if (alignment_mode == 1) {
        // 16-bit aligned mode
        vh_file << "// Initialization statements for group_info array\n";
        vh_file << "// Each group has 10 16-bit words (optimized for linear fitting with power-of-two)\n";

        // Group data initialization
        size_t group_offset = 0;
        for (size_t i = 0; i < optimized_groups.size(); i++) {
            const auto& group = optimized_groups[i];

            // Quantize basic parameters
            int16_t q_start = static_cast<int16_t>(std::round(group.base_interval.start * scale_factor));
            int16_t q_end = static_cast<int16_t>(std::round(group.base_interval.end * scale_factor));
            int16_t q_b = static_cast<int16_t>(std::round(group.base_params.b * scale_factor));
            int16_t q_c = static_cast<int16_t>(std::round(group.base_params.c * scale_factor));

            // Group size and offset
            int16_t size = static_cast<int16_t>(group.delta_encodings.size());
            int16_t offset = static_cast<int16_t>(group_offset);
            group_offset += size; // Update for next group

            // Flags: only bit 0=storage type(0=normal,1=orphan)
            uint16_t flags = (group.storage_type == ORPHAN_GROUP ? 0x1 : 0);

            // Pack flags and size into one field
            uint16_t packed_flags_size = flags | (size << 1);
            
            // Power-of-two optimization flags
            uint16_t pow2_flags = 0;
            if (group.use_power_of_two) {
                pow2_flags |= 0x1; // Set use_pow2 bit
            }
            pow2_flags |= ((group.shift_amount & 0x1F) << 1); // Set shift_amount (5 bits)

            // Quantize scale factors
            int16_t q_start_scale = static_cast<int16_t>(std::round(group.start_scale_factor * scale_factor));
            int16_t q_slope_scale = static_cast<int16_t>(std::round(group.slope_scale_factor * scale_factor));
            int16_t q_intercept_scale = static_cast<int16_t>(std::round(group.intercept_scale_factor * scale_factor));

            // Write array initialization statements
            size_t base_idx = i * 10; // Each group has 10 16-bit words in this mode
            vh_file << "group_info[" << ensureDecimalIndex(base_idx) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4)
                    << (q_start & 0xFFFF) << "; // Group " << std::dec << i << " start\n";
            vh_file << "group_info[" << ensureDecimalIndex(base_idx+1) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4)
                    << (q_end & 0xFFFF) << "; // Group " << std::dec << i << " end\n";
            vh_file << "group_info[" << ensureDecimalIndex(base_idx+2) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4)
                    << (q_b & 0xFFFF) << "; // Base b\n";
            vh_file << "group_info[" << ensureDecimalIndex(base_idx+3) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4)
                    << (q_c & 0xFFFF) << "; // Base c\n";
            vh_file << "group_info[" << ensureDecimalIndex(base_idx+4) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4)
                    << (packed_flags_size & 0xFFFF) << "; // Packed flags and size\n";
            vh_file << "group_info[" << ensureDecimalIndex(base_idx+5) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4)
                    << (pow2_flags & 0xFFFF) << "; // Power-of-two flags\n";
            vh_file << "group_info[" << ensureDecimalIndex(base_idx+6) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4)
                    << (offset & 0xFFFF) << "; // Offset\n";
            vh_file << "group_info[" << ensureDecimalIndex(base_idx+7) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4)
                    << (q_start_scale & 0xFFFF) << "; // Start scale\n";
            vh_file << "group_info[" << ensureDecimalIndex(base_idx+8) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4)
                    << (q_slope_scale & 0xFFFF) << "; // Slope scale\n";
            vh_file << "group_info[" << ensureDecimalIndex(base_idx+9) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4)
                    << (q_intercept_scale & 0xFFFF) << "; // Intercept scale\n";
            vh_file << "\n";
        }

        // Delta data initialization
        vh_file << "// Delta data initializations\n";
        vh_file << "// Each delta has 4 16-bit words\n";

        size_t delta_idx = 0;
        for (size_t i = 0; i < optimized_groups.size(); i++) {
            const auto& group = optimized_groups[i];

            vh_file << "// Delta data for Group " << i << "\n";
            for (const auto& delta : group.delta_encodings) {
                // Quantize Delta values
                int16_t q_delta_start = static_cast<int16_t>(std::round(delta.delta_start / group.start_scale_factor));
                int16_t q_delta_slope = static_cast<int16_t>(std::round(delta.delta_slope / group.slope_scale_factor));
                int16_t q_delta_intercept = static_cast<int16_t>(std::round(delta.delta_intercept / group.intercept_scale_factor));

                // Reflection flags
                uint16_t reflection_flags =
                    (delta.is_y_reflected ? 0x1 : 0) |
                    (delta.is_x_reflected ? 0x2 : 0);

                // Write array initialization statements
                size_t base_idx = delta_idx * 4; // Each delta has 4 words
                vh_file << "delta_data[" << ensureDecimalIndex(base_idx) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4)
                        << (q_delta_start & 0xFFFF) << "; // Delta start\n";
                vh_file << "delta_data[" << ensureDecimalIndex(base_idx+1) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4)
                        << (q_delta_slope & 0xFFFF) << "; // Delta slope\n";
                vh_file << "delta_data[" << ensureDecimalIndex(base_idx+2) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4)
                        << (q_delta_intercept & 0xFFFF) << "; // Delta intercept\n";
                vh_file << "delta_data[" << ensureDecimalIndex(base_idx+3) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4)
                        << (reflection_flags & 0xFFFF) << "; // Reflection flags\n";

                delta_idx++;
            }
            vh_file << "\n";
        }
    }
    else if (alignment_mode == 0) {
        // Compact mode - more complex packing based on optimized bit widths
        vh_file << "// Compact mode memory initialization requires specialized packing/unpacking logic\n";
        vh_file << "// See accompanying optimized_lut.v file for implementation details\n\n";

        // Output packed data initialization
        // This is more complex and would require bit-packing calculations
    }
    else {
        // 32-bit aligned mode with dynamic scale packing
        vh_file << "// 32-bit aligned memory initialization with linear fitting, power-of-two optimization, and dynamic scale packing\n";
        vh_file << "// Each group has variable number of 32-bit words (minimum 6)\n";

        // Function to calculate bits needed for a value
        auto bits_needed = [](int32_t val) -> int {
            if (val == 0) return 1;
            int32_t abs_val = std::abs(val);
            int bits = 0;
            // Count bits needed (add one for sign bit if negative)
            while (abs_val > 0) {
                abs_val >>= 1;
                bits++;
            }
            return bits + (val < 0 ? 1 : 0);
        };

        // Group data initialization
        size_t group_offset = 0;
        for (size_t i = 0; i < optimized_groups.size(); i++) {
            const auto& group = optimized_groups[i];

            // Quantize basic parameters
            int32_t q_start = static_cast<int32_t>(std::round(group.base_interval.start * scale_factor));
            int32_t q_end = static_cast<int32_t>(std::round(group.base_interval.end * scale_factor));
            int32_t q_b = static_cast<int32_t>(std::round(group.base_params.b * scale_factor));
            int32_t q_c = static_cast<int32_t>(std::round(group.base_params.c * scale_factor));

            // Group size and offset
            int size = static_cast<int>(group.delta_encodings.size());
            int offset = static_cast<int>(group_offset);
            group_offset += size; // Update for next group

            // Flags: only storage type
            uint32_t flags = (group.storage_type == ORPHAN_GROUP ? 0x1 : 0);
            
            // Power-of-two flags
            uint32_t use_pow2 = group.use_power_of_two ? 1 : 0;
            uint32_t shift_amount = group.shift_amount & 0x1F; // 5 bits

            // Pack control fields into one 32-bit word
            uint32_t packed_control = flags | (size << 1) | (use_pow2 << 9) | (shift_amount << 10) | (offset << 15);

            // Quantize scale factors
            int32_t q_start_scale = static_cast<int32_t>(std::round(group.start_scale_factor * scale_factor));
            int32_t q_slope_scale = static_cast<int32_t>(std::round(group.slope_scale_factor * scale_factor));
            int32_t q_intercept_scale = static_cast<int32_t>(std::round(group.intercept_scale_factor * scale_factor));

            // Calculate bits needed for each scale factor
            int start_bits = bits_needed(q_start_scale);
            int slope_bits = bits_needed(q_slope_scale);
            int intercept_bits = bits_needed(q_intercept_scale);

            // Pack scales based on their actual bit requirements
            uint32_t packed_scales = 0;
            uint32_t packed_scales2 = 0;
            uint32_t packed_scales3 = 0;
            uint32_t packing_info = 0; // Will store bit allocation info for unpacking

            if (start_bits + slope_bits + intercept_bits <= 30) {
                // All three scales can fit in one 32-bit word with 2 bits for encoding format
                int pos = 0;
                
                // Store start scale
                packed_scales |= (q_start_scale & ((1 << start_bits) - 1)) << pos;
                pos += start_bits;
                
                // Store slope scale
                packed_scales |= (q_slope_scale & ((1 << slope_bits) - 1)) << pos;
                pos += slope_bits;
                
                // Store intercept scale
                packed_scales |= (q_intercept_scale & ((1 << intercept_bits) - 1)) << pos;
                
                // Store packing format (1-word format)
                packing_info = (0 << 30) | (start_bits << 20) | (slope_bits << 10) | intercept_bits;
            } else if (start_bits + slope_bits <= 30) {
                // Start and slope in first word, intercept in second word
                packed_scales = (q_start_scale & ((1 << start_bits) - 1)) | 
                               ((q_slope_scale & ((1 << slope_bits) - 1)) << start_bits);
                packed_scales2 = q_intercept_scale;
                
                // Store packing format (2-word format, first variant)
                packing_info = (1 << 30) | (start_bits << 20) | (slope_bits << 10);
            } else {
                // Need to use separate words for each scale
                packed_scales = q_start_scale;
                packed_scales2 = q_slope_scale;
                packed_scales3 = q_intercept_scale;
                
                // Store packing format (3-word format)
                packing_info = (2 << 30);
            }

            // Write array initialization statements with dynamic scale packing
            size_t base_idx = i * 8; // Each group has up to 8 32-bit words (5 standard + up to 3 for scales)
            vh_file << "group_data[" << ensureDecimalIndex(base_idx) << "] = 32'h" << std::hex << std::setfill('0') << std::setw(8)
                    << (q_start & 0xFFFFFFFF) << "; // Group " << std::dec << i << " start\n";
            vh_file << "group_data[" << ensureDecimalIndex(base_idx+1) << "] = 32'h" << std::hex << std::setfill('0') << std::setw(8)
                    << (q_end & 0xFFFFFFFF) << "; // Group " << std::dec << i << " end\n";
            vh_file << "group_data[" << ensureDecimalIndex(base_idx+2) << "] = 32'h" << std::hex << std::setfill('0') << std::setw(8)
                    << (q_b & 0xFFFFFFFF) << "; // Base b\n";
            vh_file << "group_data[" << ensureDecimalIndex(base_idx+3) << "] = 32'h" << std::hex << std::setfill('0') << std::setw(8)
                    << (q_c & 0xFFFFFFFF) << "; // Base c\n";
            vh_file << "group_data[" << ensureDecimalIndex(base_idx+4) << "] = 32'h" << std::hex << std::setfill('0') << std::setw(8)
                    << (packed_control & 0xFFFFFFFF) << "; // Packed control fields with power-of-two flags\n";
            vh_file << "group_data[" << ensureDecimalIndex(base_idx+5) << "] = 32'h" << std::hex << std::setfill('0') << std::setw(8)
                    << (packing_info & 0xFFFFFFFF) << "; // Scale packing format info\n";
            vh_file << "group_data[" << ensureDecimalIndex(base_idx+6) << "] = 32'h" << std::hex << std::setfill('0') << std::setw(8)
                    << (packed_scales & 0xFFFFFFFF) << "; // Packed scale factors (1)\n";

            // Write additional scale words if needed
            if ((packing_info >> 30) >= 1) {
                vh_file << "group_data[" << ensureDecimalIndex(base_idx+7) << "] = 32'h" << std::hex << std::setfill('0') << std::setw(8)
                        << (packed_scales2 & 0xFFFFFFFF) << "; // Packed scale factors (2)\n";
            }
            if ((packing_info >> 30) >= 2) {
                vh_file << "group_data[" << ensureDecimalIndex(base_idx+8) << "] = 32'h" << std::hex << std::setfill('0') << std::setw(8)
                        << (packed_scales3 & 0xFFFFFFFF) << "; // Packed scale factors (3)\n";
            }
            vh_file << "\n";
        }

        // Delta data initialization
        vh_file << "// Delta data initializations\n";
        vh_file << "// Each delta has 3 32-bit words\n";

        size_t delta_idx = 0;
        for (size_t i = 0; i < optimized_groups.size(); i++) {
            const auto& group = optimized_groups[i];

            vh_file << "// Delta data for Group " << i << "\n";
            for (const auto& delta : group.delta_encodings) {
                // Quantize Delta values
                int32_t q_delta_start = static_cast<int32_t>(std::round(delta.delta_start / group.start_scale_factor));
                int32_t q_delta_slope = static_cast<int32_t>(std::round(delta.delta_slope / group.slope_scale_factor));
                int32_t q_delta_intercept = static_cast<int32_t>(std::round(delta.delta_intercept / group.intercept_scale_factor));

                // Reflection flags
                uint32_t reflection_flags =
                    (delta.is_y_reflected ? 0x1 : 0) |
                    (delta.is_x_reflected ? 0x2 : 0);

                // Pack intercept and reflection
                uint32_t packed_intercept_reflection = (q_delta_intercept & 0x3FFFFFFF) | (reflection_flags << 30);

                // Write array initialization statements
                size_t base_idx = delta_idx * 3; // Each delta has 3 words
                vh_file << "delta_data[" << ensureDecimalIndex(base_idx) << "] = 32'h" << std::hex << std::setfill('0') << std::setw(8)
                        << (q_delta_start & 0xFFFFFFFF) << "; // Delta start\n";
                vh_file << "delta_data[" << ensureDecimalIndex(base_idx+1) << "] = 32'h" << std::hex << std::setfill('0') << std::setw(8)
                        << (q_delta_slope & 0xFFFFFFFF) << "; // Delta slope\n";
                vh_file << "delta_data[" << ensureDecimalIndex(base_idx+2) << "] = 32'h" << std::hex << std::setfill('0') << std::setw(8)
                        << (packed_intercept_reflection & 0xFFFFFFFF) << "; // Delta intercept + reflection\n";

                delta_idx++;
            }
            vh_file << "\n";
        }
    }

    // Usage guidance
    vh_file << "// Add this to pwl_hlut.v to use optimized inline data with power-of-two optimization:\n";
    vh_file << "/*\n";
    vh_file << "// Replace the original memory initialization with this:\n";
    vh_file << "`include \"" << cleanName << "_optimized_bitwidths.vh\"\n\n";
    vh_file << "// Memory arrays with optimized bit layouts\n";

    if (alignment_mode == 1) {
        vh_file << "(* ram_style = \"distributed\" *) reg [15:0] group_info [0:" << ensureDecimalIndex(optimized_groups.size() * 10 - 1) << "];\n";
        vh_file << "(* ram_style = \"distributed\" *) reg [15:0] delta_data [0:" << ensureDecimalIndex(total_intervals * 4 - 1) << "];\n\n";
    }
    else if (alignment_mode == 0) {
        int group_bytes = (widths.group_entry_bits + 7) / 8;
        int delta_bytes = (widths.delta_entry_bits + 7) / 8;
        vh_file << "(* ram_style = \"distributed\" *) reg [7:0] group_data [0:" << ensureDecimalIndex(optimized_groups.size() * group_bytes - 1) << "];\n";
        vh_file << "(* ram_style = \"distributed\" *) reg [7:0] delta_data [0:" << ensureDecimalIndex(total_intervals * delta_bytes - 1) << "];\n\n";
    }
    else {
        // For 32-bit mode, account for variable number of words per group
        int max_words_per_group = 8; // 5 standard + up to 3 for scale factors
        vh_file << "(* ram_style = \"distributed\" *) reg [31:0] group_data [0:" << ensureDecimalIndex(optimized_groups.size() * max_words_per_group - 1) << "];\n";
        vh_file << "(* ram_style = \"distributed\" *) reg [31:0] delta_data [0:" << ensureDecimalIndex(total_intervals * 3 - 1) << "];\n\n";
    }

    vh_file << "initial begin\n";
    vh_file << " // Initialize all memory with zeros\n";
    if (alignment_mode == 1) {
        vh_file << " for (int i = 0; i < " << ensureDecimalIndex(optimized_groups.size() * 10) << "; i = i + 1) begin\n";
        vh_file << " group_info[i] = 16'h0000;\n";
        vh_file << " end\n";
        vh_file << " for (int i = 0; i < " << ensureDecimalIndex(total_intervals * 4) << "; i = i + 1) begin\n";
        vh_file << " delta_data[i] = 16'h0000;\n";
        vh_file << " end\n\n";
    } else if (alignment_mode == 0) {
        int group_bytes = (widths.group_entry_bits + 7) / 8;
        int delta_bytes = (widths.delta_entry_bits + 7) / 8;
        vh_file << " for (int i = 0; i < " << ensureDecimalIndex(optimized_groups.size() * group_bytes) << "; i = i + 1) begin\n";
        vh_file << " group_data[i] = 8'h00;\n";
        vh_file << " end\n";
        vh_file << " for (int i = 0; i < " << ensureDecimalIndex(total_intervals * delta_bytes) << "; i = i + 1) begin\n";
        vh_file << " delta_data[i] = 8'h00;\n";
        vh_file << " end\n\n";
    } else {
        int max_words_per_group = 8; // 5 standard + up to 3 for scale factors
        vh_file << " for (int i = 0; i < " << ensureDecimalIndex(optimized_groups.size() * max_words_per_group) << "; i = i + 1) begin\n";
        vh_file << " group_data[i] = 32'h00000000;\n";
        vh_file << " end\n";
        vh_file << " for (int i = 0; i < " << ensureDecimalIndex(total_intervals * 3) << "; i = i + 1) begin\n";
        vh_file << " delta_data[i] = 32'h00000000;\n";
        vh_file << " end\n\n";
    }

    vh_file << " // Include the generated initialization statements\n";
    vh_file << " `include \"" << cleanName << "_inline_lut_data.vh\"\n";
    vh_file << "end\n";
    vh_file << "*/\n";

    vh_file.close();
    std::cout << "Inline Verilog LUT data generated: " << inline_vh_filename << "\n";

    // ---------------------------------------------------
    // Generate memory section replacement file
    // ---------------------------------------------------

    std::ofstream rep_file(replace_file);
    if (rep_file.is_open()) {
        rep_file << "// Replace the memory initialization section in pwl_hlut.v with this:\n\n";
        rep_file << "// Include optimized bit width definitions\n";
        rep_file << "`include \"" << cleanName << "_optimized_bitwidths.vh\"\n\n";

        rep_file << "// Memory arrays with optimized layouts\n";

        if (alignment_mode == 1) {
            // 16-bit aligned memory arrays
            rep_file << "(* ram_style = \"distributed\" *) reg [15:0] group_info [0:" << ensureDecimalIndex(optimized_groups.size() * 10 - 1) << "];\n";
            rep_file << "(* ram_style = \"distributed\" *) reg [15:0] delta_data [0:" << ensureDecimalIndex(total_intervals * 4 - 1) << "];\n\n";
        }
        else if (alignment_mode == 0) {
            // Compact memory arrays
            int group_bytes = (widths.group_entry_bits + 7) / 8;
            int delta_bytes = (widths.delta_entry_bits + 7) / 8;
            rep_file << "(* ram_style = \"distributed\" *) reg [7:0] group_data [0:" << ensureDecimalIndex(optimized_groups.size() * group_bytes - 1) << "];\n";
            rep_file << "(* ram_style = \"distributed\" *) reg [7:0] delta_data [0:" << ensureDecimalIndex(total_intervals * delta_bytes - 1) << "];\n\n";
        }
        else {
            // 32-bit aligned memory arrays with dynamic scale packing
            int max_words_per_group = 8; // 5 standard + up to 3 for scale factors
            rep_file << "(* ram_style = \"distributed\" *) reg [31:0] group_data [0:" << ensureDecimalIndex(optimized_groups.size() * max_words_per_group - 1) << "];\n";
            rep_file << "(* ram_style = \"distributed\" *) reg [31:0] delta_data [0:" << ensureDecimalIndex(total_intervals * 3 - 1) << "];\n\n";
        }

        rep_file << "// Memory initialization with optimized inline data\n";
        rep_file << "initial begin\n";
        rep_file << " // Initialize all memory with zeros\n";

        if (alignment_mode == 1) {
            rep_file << " for (int i = 0; i < " << ensureDecimalIndex(optimized_groups.size() * 10) << "; i = i + 1) begin\n";
            rep_file << " group_info[i] = 16'h0000;\n";
            rep_file << " end\n";
            rep_file << " for (int i = 0; i < " << ensureDecimalIndex(total_intervals * 4) << "; i = i + 1) begin\n";
            rep_file << " delta_data[i] = 16'h0000;\n";
            rep_file << " end\n\n";

            // Output actual data initialization for 16-bit aligned mode
            rep_file << " // Group information data\n";
            size_t group_offset = 0;
            for (size_t i = 0; i < optimized_groups.size(); i++) {
                const auto& group = optimized_groups[i];

                // Quantize parameters
                int16_t q_start = static_cast<int16_t>(std::round(group.base_interval.start * scale_factor));
                int16_t q_end = static_cast<int16_t>(std::round(group.base_interval.end * scale_factor));
                int16_t q_b = static_cast<int16_t>(std::round(group.base_params.b * scale_factor));
                int16_t q_c = static_cast<int16_t>(std::round(group.base_params.c * scale_factor));

                int16_t size = static_cast<int16_t>(group.delta_encodings.size());
                int16_t offset = static_cast<int16_t>(group_offset);
                group_offset += size;

                uint16_t flags = (group.storage_type == ORPHAN_GROUP ? 0x1 : 0);
                uint16_t packed_flags_size = flags | (size << 1);
                
                // Power-of-two optimization flags
                uint16_t pow2_flags = 0;
                if (group.use_power_of_two) {
                    pow2_flags |= 0x1; // Set use_pow2 bit
                }
                pow2_flags |= ((group.shift_amount & 0x1F) << 1); // Set shift_amount (5 bits)

                int16_t q_start_scale = static_cast<int16_t>(std::round(group.start_scale_factor * scale_factor));
                int16_t q_slope_scale = static_cast<int16_t>(std::round(group.slope_scale_factor * scale_factor));
                int16_t q_intercept_scale = static_cast<int16_t>(std::round(group.intercept_scale_factor * scale_factor));

                // Write initialization for all fields
                size_t base_idx = i * 10; // 10 fields per group
                rep_file << " group_info[" << ensureDecimalIndex(base_idx) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4) << (q_start & 0xFFFF) << ";\n";
                rep_file << " group_info[" << ensureDecimalIndex(base_idx+1) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4) << (q_end & 0xFFFF) << ";\n";
                rep_file << " group_info[" << ensureDecimalIndex(base_idx+2) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4) << (q_b & 0xFFFF) << ";\n";
                rep_file << " group_info[" << ensureDecimalIndex(base_idx+3) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4) << (q_c & 0xFFFF) << ";\n";
                rep_file << " group_info[" << ensureDecimalIndex(base_idx+4) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4) << (packed_flags_size & 0xFFFF) << ";\n";
                rep_file << " group_info[" << ensureDecimalIndex(base_idx+5) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4) << (pow2_flags & 0xFFFF) << ";\n";
                rep_file << " group_info[" << ensureDecimalIndex(base_idx+6) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4) << (offset & 0xFFFF) << ";\n";
                rep_file << " group_info[" << ensureDecimalIndex(base_idx+7) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4) << (q_start_scale & 0xFFFF) << ";\n";
                rep_file << " group_info[" << ensureDecimalIndex(base_idx+8) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4) << (q_slope_scale & 0xFFFF) << ";\n";
                rep_file << " group_info[" << ensureDecimalIndex(base_idx+9) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4) << (q_intercept_scale & 0xFFFF) << ";\n";
            }

            // Delta parameter data
            rep_file << "\n // Delta parameter data\n";
            size_t delta_idx = 0;
            for (size_t i = 0; i < optimized_groups.size(); i++) {
                const auto& group = optimized_groups[i];

                for (const auto& delta : group.delta_encodings) {
                    // Quantize Delta values
                    int16_t q_delta_start = static_cast<int16_t>(std::round(delta.delta_start / group.start_scale_factor));
                    int16_t q_delta_slope = static_cast<int16_t>(std::round(delta.delta_slope / group.slope_scale_factor));
                    int16_t q_delta_intercept = static_cast<int16_t>(std::round(delta.delta_intercept / group.intercept_scale_factor));

                    uint16_t reflection_flags =
                        (delta.is_y_reflected ? 0x1 : 0) |
                        (delta.is_x_reflected ? 0x2 : 0);

                    // Write all fields
                    size_t base_idx = delta_idx * 4;
                    rep_file << " delta_data[" << ensureDecimalIndex(base_idx) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4) << (q_delta_start & 0xFFFF) << ";\n";
                    rep_file << " delta_data[" << ensureDecimalIndex(base_idx+1) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4) << (q_delta_slope & 0xFFFF) << ";\n";
                    rep_file << " delta_data[" << ensureDecimalIndex(base_idx+2) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4) << (q_delta_intercept & 0xFFFF) << ";\n";
                    rep_file << " delta_data[" << ensureDecimalIndex(base_idx+3) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4) << (reflection_flags & 0xFFFF) << ";\n";

                    delta_idx++;
                }
            }
        }
        else if (alignment_mode == 0) {
            // Compact mode initialization
            rep_file << " // Compact mode initialization - see optimized_lut.v for implementation\n";
        }
        else {
            // 32-bit aligned mode with dynamic scale packing
            int max_words_per_group = 8; // 5 standard + up to 3 for scale factors
            rep_file << " for (int i = 0; i < " << ensureDecimalIndex(optimized_groups.size() * max_words_per_group) << "; i = i + 1) begin\n";
            rep_file << " group_data[i] = 32'h00000000;\n";
            rep_file << " end\n";
            rep_file << " for (int i = 0; i < " << ensureDecimalIndex(total_intervals * 3) << "; i = i + 1) begin\n";
            rep_file << " delta_data[i] = 32'h00000000;\n";
            rep_file << " end\n\n";

            // Function to calculate bits needed for a value
            rep_file << " // Function to calculate bits needed for a value\n";
            rep_file << " function automatic int bits_needed(int value);\n";
            rep_file << "     int abs_val, bits;\n";
            rep_file << "     begin\n";
            rep_file << "         if (value == 0) return 1;\n";
            rep_file << "         abs_val = (value < 0) ? -value : value;\n";
            rep_file << "         bits = 0;\n";
            rep_file << "         while (abs_val > 0) begin\n";
            rep_file << "             abs_val = abs_val >> 1;\n";
            rep_file << "             bits = bits + 1;\n";
            rep_file << "         end\n";
            rep_file << "         return bits + (value < 0 ? 1 : 0);\n";
            rep_file << "     end\n";
            rep_file << " endfunction\n\n";

            // Output initialization with dynamic scale packing
            rep_file << " // Group information data with dynamic scale packing\n";
            size_t group_offset = 0;
            for (size_t i = 0; i < optimized_groups.size(); i++) {
                const auto& group = optimized_groups[i];

                // Quantize parameters
                int32_t q_start = static_cast<int32_t>(std::round(group.base_interval.start * scale_factor));
                int32_t q_end = static_cast<int32_t>(std::round(group.base_interval.end * scale_factor));
                int32_t q_b = static_cast<int32_t>(std::round(group.base_params.b * scale_factor));
                int32_t q_c = static_cast<int32_t>(std::round(group.base_params.c * scale_factor));

                int size = static_cast<int>(group.delta_encodings.size());
                int offset = static_cast<int>(group_offset);
                group_offset += size;

                uint32_t flags = (group.storage_type == ORPHAN_GROUP ? 0x1 : 0);
                
                // Power-of-two flags
                uint32_t use_pow2 = group.use_power_of_two ? 1 : 0;
                uint32_t shift_amount = group.shift_amount & 0x1F; // 5 bits

                // Pack control fields into one 32-bit word
                uint32_t packed_control = flags | (size << 1) | (use_pow2 << 9) | (shift_amount << 10) | (offset << 15);

                // Quantize scale factors
                int32_t q_start_scale = static_cast<int32_t>(std::round(group.start_scale_factor * scale_factor));
                int32_t q_slope_scale = static_cast<int32_t>(std::round(group.slope_scale_factor * scale_factor));
                int32_t q_intercept_scale = static_cast<int32_t>(std::round(group.intercept_scale_factor * scale_factor));

                // Write basic parameters
                size_t base_idx = i * 8; // Up to 8 words per group (5 standard + up to 3 for scales)
                rep_file << " group_data[" << ensureDecimalIndex(base_idx) << "] = 32'h" << std::hex << std::setfill('0') << std::setw(8) << (q_start & 0xFFFFFFFF) << ";\n";
                rep_file << " group_data[" << ensureDecimalIndex(base_idx+1) << "] = 32'h" << std::hex << std::setfill('0') << std::setw(8) << (q_end & 0xFFFFFFFF) << ";\n";
                rep_file << " group_data[" << ensureDecimalIndex(base_idx+2) << "] = 32'h" << std::hex << std::setfill('0') << std::setw(8) << (q_b & 0xFFFFFFFF) << ";\n";
                rep_file << " group_data[" << ensureDecimalIndex(base_idx+3) << "] = 32'h" << std::hex << std::setfill('0') << std::setw(8) << (q_c & 0xFFFFFFFF) << ";\n";
                rep_file << " group_data[" << ensureDecimalIndex(base_idx+4) << "] = 32'h" << std::hex << std::setfill('0') << std::setw(8) << (packed_control & 0xFFFFFFFF) << ";\n";

                // Dynamic packing of scale factors
                rep_file << "\n // Dynamic scale packing for group " << i << "\n";
                rep_file << " begin\n";
                rep_file << "     // Calculate bits needed for each scale factor\n";
                rep_file << "     int start_bits, slope_bits, intercept_bits;\n";
                rep_file << "     int q_start_scale, q_slope_scale, q_intercept_scale;\n";
                rep_file << "     int packed_scales, packed_scales2, packed_scales3;\n";
                rep_file << "     int packing_info;\n\n";
                
                rep_file << "     q_start_scale = " << std::dec << q_start_scale << ";\n";
                rep_file << "     q_slope_scale = " << std::dec << q_slope_scale << ";\n";
                rep_file << "     q_intercept_scale = " << std::dec << q_intercept_scale << ";\n\n";
                
                rep_file << "     start_bits = bits_needed(q_start_scale);\n";
                rep_file << "     slope_bits = bits_needed(q_slope_scale);\n";
                rep_file << "     intercept_bits = bits_needed(q_intercept_scale);\n\n";
                
                rep_file << "     // Choose packing strategy based on required bits\n";
                rep_file << "     if (start_bits + slope_bits + intercept_bits <= 30) begin\n";
                rep_file << "         // All three scales fit in one word with 2 bits for format\n";
                rep_file << "         int pos = 0;\n";
                rep_file << "         packed_scales = 0;\n\n";
                
                rep_file << "         // Store start scale\n";
                rep_file << "         packed_scales |= (q_start_scale & ((1 << start_bits) - 1)) << pos;\n";
                rep_file << "         pos = pos + start_bits;\n\n";
                
                rep_file << "         // Store slope scale\n";
                rep_file << "         packed_scales |= (q_slope_scale & ((1 << slope_bits) - 1)) << pos;\n";
                rep_file << "         pos = pos + slope_bits;\n\n";
                
                rep_file << "         // Store intercept scale\n";
                rep_file << "         packed_scales |= (q_intercept_scale & ((1 << intercept_bits) - 1)) << pos;\n\n";
                
                rep_file << "         // Store packing format (1-word format)\n";
                rep_file << "         packing_info = (0 << 30) | (start_bits << 20) | (slope_bits << 10) | intercept_bits;\n";
                rep_file << "     end else if (start_bits + slope_bits <= 30) begin\n";
                rep_file << "         // Start and slope in first word, intercept in second\n";
                rep_file << "         packed_scales = (q_start_scale & ((1 << start_bits) - 1)) | \n";
                rep_file << "                       ((q_slope_scale & ((1 << slope_bits) - 1)) << start_bits);\n";
                rep_file << "         packed_scales2 = q_intercept_scale;\n\n";
                
                rep_file << "         // Store packing format (2-word format)\n";
                rep_file << "         packing_info = (1 << 30) | (start_bits << 20) | (slope_bits << 10);\n";
                rep_file << "     end else begin\n";
                rep_file << "         // Need separate words for each scale\n";
                rep_file << "         packed_scales = q_start_scale;\n";
                rep_file << "         packed_scales2 = q_slope_scale;\n";
                rep_file << "         packed_scales3 = q_intercept_scale;\n\n";
                
                rep_file << "         // Store packing format (3-word format)\n";
                rep_file << "         packing_info = (2 << 30);\n";
                rep_file << "     end\n\n";
                
                rep_file << "     // Write scale packing information\n";
                rep_file << "     group_data[" << ensureDecimalIndex(base_idx+5) << "] = packing_info;\n";
                rep_file << "     group_data[" << ensureDecimalIndex(base_idx+6) << "] = packed_scales;\n\n";
                
                rep_file << "     // Write additional scale words if needed\n";
                rep_file << "     if ((packing_info >> 30) >= 1) begin\n";
                rep_file << "         group_data[" << ensureDecimalIndex(base_idx+7) << "] = packed_scales2;\n";
                rep_file << "     end\n";
                rep_file << "     if ((packing_info >> 30) >= 2) begin\n";
                rep_file << "         group_data[" << ensureDecimalIndex(base_idx+8) << "] = packed_scales3;\n";
                rep_file << "     end\n";
                rep_file << " end\n";
            }

            // Delta parameter data
            rep_file << "\n // Delta parameter data\n";
            size_t delta_idx = 0;
            for (size_t i = 0; i < optimized_groups.size(); i++) {
                const auto& group = optimized_groups[i];

                for (const auto& delta : group.delta_encodings) {
                    // Quantize Delta values
                    int32_t q_delta_start = static_cast<int32_t>(std::round(delta.delta_start / group.start_scale_factor));
                    int32_t q_delta_slope = static_cast<int32_t>(std::round(delta.delta_slope / group.slope_scale_factor));
                    int32_t q_delta_intercept = static_cast<int32_t>(std::round(delta.delta_intercept / group.intercept_scale_factor));

                    uint32_t reflection_flags =
                        (delta.is_y_reflected ? 0x1 : 0) |
                        (delta.is_x_reflected ? 0x2 : 0);

                    // Pack intercept and reflection
                    uint32_t packed_intercept_reflection = (q_delta_intercept & 0x3FFFFFFF) | (reflection_flags << 30);

                    // Write all fields
                    size_t base_idx = delta_idx * 3;
                    rep_file << " delta_data[" << ensureDecimalIndex(base_idx) << "] = 32'h" << std::hex << std::setfill('0') << std::setw(8) << (q_delta_start & 0xFFFFFFFF) << ";\n";
                    rep_file << " delta_data[" << ensureDecimalIndex(base_idx+1) << "] = 32'h" << std::hex << std::setfill('0') << std::setw(8) << (q_delta_slope & 0xFFFFFFFF) << ";\n";
                    rep_file << " delta_data[" << ensureDecimalIndex(base_idx+2) << "] = 32'h" << std::hex << std::setfill('0') << std::setw(8) << (packed_intercept_reflection & 0xFFFFFFFF) << ";\n";

                    delta_idx++;
                }
            }
        }

        rep_file << "end\n";
        rep_file.close();

        std::cout << "Memory initialization replacement code generated: " << replace_file << "\n";
    }

    // ---------------------------------------------------
    // Update config file
    // ---------------------------------------------------

    std::string config_filename = dir + "/" + cleanName + "_config.vh";
    std::ofstream config_file(config_filename, std::ios::app); // Append mode

    if (config_file.is_open()) {
        config_file << "\n// ADDED: Use optimized LUT data with "
                  << (alignment_mode == 0 ? "compact" :
                     (alignment_mode == 1 ? "16-bit aligned" : "32-bit aligned"))
                  << " memory layout, power-of-two optimization";
        
        if (alignment_mode == 2) {
            config_file << ", and dynamic scale packing";
        }
        config_file << "\n";
        
        config_file << "`define USE_OPTIMIZED_LUT_DATA 1\n";
        config_file << "`define OPT_ALIGNMENT_MODE " << alignment_mode << " // "
                  << (alignment_mode == 0 ? "compact" :
                     (alignment_mode == 1 ? "16-bit aligned" : "32-bit aligned"))
                  << "\n";
        config_file << "`define USE_POWER_OF_TWO_OPTIMIZATION 1\n";
        
        if (alignment_mode == 2) {
            config_file << "`define USE_DYNAMIC_SCALE_PACKING 1\n";
        }
        
        config_file.close();
        std::cout << "Updated config file to use optimized LUT data with power-of-two";
        if (alignment_mode == 2) {
            std::cout << " and dynamic scale packing";
        }
        std::cout << ": " << config_filename << "\n";
    }

    // ---------------------------------------------------
    // Analysis of memory savings
    // ---------------------------------------------------

    // Calculate standard and optimized memory requirements
    int std_group_bits = optimized_groups.size() * 11 * 16; // Standard: 11 16-bit words per group
    int std_delta_bits = total_intervals * 4 * 16; // Standard: 4 16-bit words per interval
    int std_total = std_group_bits + std_delta_bits;

    int opt_group_bits = optimized_groups.size() * widths.group_entry_bits;
    int opt_delta_bits = total_intervals * widths.delta_entry_bits;
    int opt_total = opt_group_bits + opt_delta_bits;

    double savings_percent = 100.0 * (std_total - opt_total) / std_total;

    std::cout << "\nMemory usage comparison:\n";
    std::cout << " Standard implementation: " << std_total << " bits (" << (std_total/8) << " bytes)\n";
    std::cout << " Optimized implementation: " << opt_total << " bits (" << (opt_total/8) << " bytes)\n";
    std::cout << " Memory reduction: " << std::fixed << std::setprecision(2) << savings_percent << "%\n";

    std::cout << "\nPower-of-two optimization details:\n";
    int pow2_groups = 0;
    for (const auto& group : optimized_groups) {
        if (group.use_power_of_two) pow2_groups++;
    }
    std::cout << " Groups using power-of-two optimization: " << pow2_groups << " out of " << optimized_groups.size() << "\n";
    std::cout << " Percentage of power-of-two groups: " << std::fixed << std::setprecision(2) 
              << (100.0 * pow2_groups / optimized_groups.size()) << "%\n";

    if (alignment_mode == 2) {
        std::cout << "\nDynamic scale packing used - scale factors packed based on their actual bit requirements\n";
    }

    std::cout << "\nOptimized LUT data generation with power-of-two optimization complete.\n";
}

// Master function to generate all hardware parameter files
void generateHardwareImplementation(
    const std::string& directory,
    const std::string& functionName,
    const std::vector<IntervalGroup>& groups,
    const std::vector<Interval>& intervals,
    const std::vector<FitParameters>& fit_params,
    const std::string& expression_str,
    double start = 0.0, 
    double end = 1.0,
    double scale_factor = 1024.0,
    double target_error = 0.0001) {
    
    if (groups.empty() || intervals.empty() || fit_params.empty() || expression_str.empty()) {
        std::cout << "Missing required data for hardware parameter generation.\n";
        return;
    }
    
    std::cout << "\n=== Generating Hardware Parameters for " << functionName << " ===\n";
    
    // Create hardware directory if it doesn't exist
    std::string hw_dir = directory + "/hw";
    if (!std::filesystem::exists(hw_dir)) {
        std::filesystem::create_directory(hw_dir);
    }
    
    // Create function-specific directory
    std::string func_dir = hw_dir + "/" + functionName;
    if (!std::filesystem::exists(func_dir)) {
        std::filesystem::create_directory(func_dir);
    }
    
    // Analyze FPGA implementation
    analyzeFPGAImplementation(functionName, groups, intervals, fit_params, 
                             start, end, scale_factor, target_error);
    
    // Generate Verilog parameter files
    generateVerilogHeader(func_dir, functionName, groups, scale_factor, start, end);
    generateGroupROMFile(func_dir, functionName, groups, scale_factor);
    generateDeltaROMFile(func_dir, functionName, groups);
    
    // Determine frac_bits from scale_factor
    int frac_bits = 0;
    double temp_scale = scale_factor;
    while (temp_scale > 1.0) {
        temp_scale /= 2.0;
        frac_bits++;
    }
    
    // Generate optimized LUT data with 16-bit alignment (standard mode)
    generateOptimizedVerilogLUTs(func_dir, functionName, groups, scale_factor, frac_bits, 1);
    
    std::cout << "Hardware parameter files generated in: " << func_dir << "\n";
    std::cout << "=== Hardware Parameter Generation Complete ===\n\n";
}

#endif // HW_MAPPING_HPP
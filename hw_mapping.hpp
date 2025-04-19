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
    std::vector<std::tuple<std::string, int, int>> group_fields;
    std::vector<std::tuple<std::string, int, int>> delta_fields;
    int group_entry_bits; // Total bits per group entry
    int delta_entry_bits; // Total bits per delta entry

    int flags_pos;          // storage type flag position
    int size_pos;           // group size position
    int use_pow2_pos;       // power of 2 optimization flag position
    int shift_amount_pos;   // shift amount position
    int reflection_x_pos;   // X-reflection flag position
    int reflection_y_pos;   // Y-reflection flag position
};

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
    int delta_addr_width = std::ceil(std::log2(total_intervals));
    
    // Display analysis results
    std::cout << "Function Domain: [" << start << ", " << end << "]\n";
    std::cout << "Scale Factor: " << scale_factor << " (" << data_width << " bits)\n";
    std::cout << "Target Error: " << target_error << "\n\n";
    
    std::cout << "LUT Parameters:\n";
    std::cout << "  Groups: " << total_groups 
              << " (Orphans: " << orphan_groups 
              << ", Power-of-two optimized: " << optimized_pow2_groups << ")\n";
    std::cout << "  Total Intervals: " << total_intervals << "\n";
    std::cout << "  Memory Requirements: " << (total_memory_bits) << " bits\n";
    std::cout << "  Group Address Width: " << group_addr_width << " bits\n";
    std::cout << "  Interval Address Width: " << interval_addr_width << " bits\n";
    std::cout << "  Delta Address Width: " << delta_addr_width << " bits\n";
    std::cout << "  Data Width: " << data_width << " bits\n";
    std::cout << "=== Analysis Complete ===\n\n";
}

/**
 * Generates a memory initialization file for group ROM with length format
 * @param directory Output directory
 * @param functionName Name of approximated function
 * @param groups Vector of interval groups
 * @param scale_factor Global scale factor
 */
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
    file << "// Order: base_start, base_length, base_slope, base_intercept, control_word, start_scale, slope_scale, intercept_scale\n\n";
    
    // Add debug log
    std::string debug_filename = directory + "/" + functionName + "_group_debug.log";
    std::ofstream debug_file(debug_filename);
    
    // Track offset for delta ROM
    size_t delta_offset = 0;
    
    // Write group data
    for (const auto& group : groups) {
        debug_file << "===== Group " << group.id << " ===== \n";
        debug_file << "Storage Type: " << (group.storage_type == ORPHAN_GROUP ? "ORPHAN" : "NORMAL") << "\n";
        debug_file << "Base Interval: [" << group.base_interval.start << ", " << group.base_interval.end << "]\n";
        debug_file << "Base Params: b=" << group.base_params.b << ", c=" << group.base_params.c << "\n";
        debug_file << "Scale Factors: start=" << group.start_scale_factor 
                  << ", slope=" << group.slope_scale_factor 
                  << ", intercept=" << group.intercept_scale_factor << "\n";
        
        // Use the same quantization factor for start and length
        double unified_scale_factor = group.slope_scale_factor;
        
        // Quantize base interval start using unified scale factor
        int32_t q_start = static_cast<int32_t>(std::round(group.base_interval.start * unified_scale_factor));
        
        // Calculate and quantize the interval length instead of end point
        double interval_length = group.base_interval.end - group.base_interval.start;
        int32_t q_length = static_cast<int32_t>(std::round(interval_length * unified_scale_factor));
        
        // For debug comparison, also calculate the original end point
        int32_t q_end = static_cast<int32_t>(std::round(group.base_interval.end * unified_scale_factor));
        
        // Apply proper scaling to base_params b and c before casting to integers
        int32_t q_b = static_cast<int32_t>(std::round(group.base_params.b * unified_scale_factor));
        int32_t q_c = static_cast<int32_t>(std::round(group.base_params.c * group.intercept_scale_factor));
        
        // Log quantized values with both length and end for verification
        debug_file << "Quantized: start=" << q_start 
                  << ", length=" << q_length
                  << " (original end=" << q_end << ")"
                  << ", b=" << q_b
                  << ", c=" << q_c << "\n";
        debug_file << "Verification: start + length = " << (q_start + q_length) 
                  << " should be approximately equal to end = " << q_end << "\n\n";
        
        // Pack control word
        uint32_t control = 0;
        control |= (group.storage_type == ORPHAN_GROUP) ? 1 : 0;           // bit 0: is_orphan
        control |= (group.use_power_of_two ? 1 : 0) << 1;                  // bit 1: use_pow2
        control |= (group.shift_amount & 0x1F) << 2;                       // bits 2-6: shift_amount (5 bits)
        control |= (group.delta_encodings.size() & 0xFFFF) << 7;           // bits 7-22: interval_count (16 bits)
        control |= (delta_offset & 0x1FF) << 23;                           // bits 23-31: delta_offset (9 bits)
        
        // Scale factors are used directly, no need for re-quantization
        int32_t q_start_scale = static_cast<int32_t>(group.start_scale_factor);
        int32_t q_slope_scale = static_cast<int32_t>(group.slope_scale_factor);
        int32_t q_intercept_scale = static_cast<int32_t>(group.intercept_scale_factor);
        
        // Write as hex values, one word per line - using length instead of end
        file << std::hex << std::setfill('0') << std::setw(8) << (q_start & 0xFFFFFFFF) << "\n";
        file << std::hex << std::setfill('0') << std::setw(8) << (q_length & 0xFFFFFFFF) << "\n";  // Using length instead of end
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
    debug_file.close();
    
    std::cout << "Group ROM initialization file generated with length format: " << filename << "\n";
    std::cout << "Group quantization debug log: " << debug_filename << "\n";
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
    
    // 写入文件头部
    file << "// Delta ROM initialization for " << functionName << "\n";
    file << "// Format: 16/8-bit values packed into 32-bit words, hex format\n";
    file << "// Order: delta_start(16), delta_slope(16), delta_intercept(16), flags(8)\n\n";
    
    // 添加调试日志
    std::string debug_filename = directory + "/" + functionName + "_delta_debug.log";
    std::ofstream debug_file(debug_filename);
    
    // 写入delta数据
    for (size_t group_idx = 0; group_idx < groups.size(); group_idx++) {
        const auto& group = groups[group_idx];
        
        file << "// Group " << group.id << " with storage_type=" 
             << (group.storage_type == ORPHAN_GROUP ? "ORPHAN" : "NORMAL") 
             << ", interval count=" << group.delta_encodings.size() << "\n";
        
        debug_file << "===== Group " << group.id << " ===== \n";
        debug_file << "Storage Type: " << (group.storage_type == ORPHAN_GROUP ? "ORPHAN" : "NORMAL") << "\n";
        debug_file << "Interval Count: " << group.delta_encodings.size() << "\n";
        debug_file << "Base Params: b=" << group.base_params.b << ", c=" << group.base_params.c << "\n";
        
        // 使用统一的scale factor，即65536
        double unified_scale_factor = group.slope_scale_factor;
        
        debug_file << "Scale Factors: start=" << unified_scale_factor 
                  << ", slope=" << group.slope_scale_factor 
                  << ", intercept=" << group.intercept_scale_factor << "\n\n";
             
        for (size_t i = 0; i < group.delta_encodings.size(); i++) {
            const auto& delta = group.delta_encodings[i];
            
            debug_file << "Interval " << i << ":\n";
            debug_file << "  Original: start=" << delta.delta_start 
                      << ", slope=" << delta.delta_slope 
                      << ", intercept=" << delta.delta_intercept 
                      << ", x_refl=" << delta.is_x_reflected 
                      << ", y_refl=" << delta.is_y_reflected << "\n";
            
            // 对起点进行量化 - 使用统一的scale factor 65536
            int16_t q_delta_start = static_cast<int16_t>(std::round(delta.delta_start * unified_scale_factor));
            
            // 斜率和截距直接使用 - 已经是量化后的整数
            int16_t q_delta_slope = static_cast<int16_t>(delta.delta_slope);
            int16_t q_delta_intercept = static_cast<int16_t>(delta.delta_intercept);
            
            // 创建标志字节
            uint8_t flags = (delta.is_y_reflected ? 1 : 0) | (delta.is_x_reflected ? 2 : 0);
            
            // 计算并打印恢复值（用于调试）
            debug_file << "  Quantized: start=" << q_delta_start 
                      << ", slope=" << q_delta_slope 
                      << ", intercept=" << q_delta_intercept << "\n";
            
            // 根据不同组类型计算恢复值
            if (group.storage_type == ORPHAN_GROUP) {
                // 孤立组 - 恢复值计算方法
                double recovered_start = static_cast<double>(q_delta_start) / unified_scale_factor;
                double recovered_slope = static_cast<double>(q_delta_slope);
                double recovered_intercept = static_cast<double>(q_delta_intercept);
                
                debug_file << "  Recovered (absolute): start=" << recovered_start
                          << ", slope=" << recovered_slope 
                          << ", intercept=" << recovered_intercept << "\n\n";
            } else {
                // 常规组 - 恢复值计算方法
                double recovered_start = static_cast<double>(q_delta_start) / unified_scale_factor;
                double recovered_slope = static_cast<double>(q_delta_slope) * group.slope_scale_factor;
                double recovered_intercept = static_cast<double>(q_delta_intercept) * group.intercept_scale_factor;
                
                debug_file << "  Recovered (relative): start=" << recovered_start
                          << ", slope=" << recovered_slope 
                          << ", intercept=" << recovered_intercept << "\n\n";
            }
            
            // 写入ROM文件（16位对齐的16进制格式）
            file << std::hex << std::setfill('0') << std::setw(8) << (q_delta_start & 0xFFFF) << "\n";
            file << std::hex << std::setfill('0') << std::setw(8) << (q_delta_slope & 0xFFFF) << "\n";
            file << std::hex << std::setfill('0') << std::setw(8) << (q_delta_intercept & 0xFFFF) << "\n";
            file << std::hex << std::setfill('0') << std::setw(8) << (flags & 0xFF) << "\n";
        }
    }
    
    file.close();
    debug_file.close();
    
    std::cout << "Delta ROM initialization file generated: " << filename << "\n";
    std::cout << "Delta quantization debug log: " << debug_filename << "\n";
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

// Adjust bit widths based on alignment mode
void adjustBitWidthsByAlignmentMode(BitWidths& widths, int alignment_mode) {
    switch (alignment_mode) {
        case 0: { // 紧凑模式 - 8位边界对齐
            widths.group_start = ((widths.group_start + 7) / 8) * 8;
            widths.group_end = ((widths.group_end + 7) / 8) * 8;
            widths.base_b = ((widths.base_b + 7) / 8) * 8;
            widths.base_c = ((widths.base_c + 7) / 8) * 8;

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
        case 1: { // 16位对齐模式
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
            widths.pow2_flags = 6; // 1位用于use_pow2 + 5位用于shift_amount
            widths.delta_start = 16;
            widths.delta_slope = 16;
            widths.delta_intercept = 16;
            widths.reflection = 2;
            break;
        }
        case 2: { // 32位对齐模式 - 高精度
            widths.group_start = 32;
            widths.group_end = 32;
            widths.base_b = 32;
            widths.base_c = 32;
            widths.flags = 1;
            widths.size = 8;
            widths.offset = 16;
            widths.pow2_flags = 6; // 1位用于use_pow2 + 5位用于shift_amount
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
}

/**
 * Creates memory layout for interval compression based on alignment mode
 * @param widths Struct to store bit position information
 * @param alignment_mode 0=compact, 1=16-bit aligned, 2=32-bit aligned
 */
void createMemoryLayout(BitWidths& widths, int alignment_mode) {
    // Clear any existing field definitions
    widths.group_fields.clear();
    widths.delta_fields.clear();

    if (alignment_mode == 1) { // 16-bit alignment mode
        // Group parameter layout - fields optimized for 16-bit access
        widths.group_fields.push_back({"START", 0, 16});
        widths.group_fields.push_back({"END", 16, 16});
        widths.group_fields.push_back({"B", 32, 16});
        widths.group_fields.push_back({"C", 48, 16});
        
        // Control bits positions
        widths.flags_pos = 64;
        widths.size_pos = 65;
        widths.group_fields.push_back({"FLAGS_SIZE", 64, 16});
        
        // Power-of-2 optimization flags and parameters
        widths.use_pow2_pos = 80;
        widths.shift_amount_pos = 81;
        widths.group_fields.push_back({"POW2", 80, 16});
        
        // Additional group parameters
        widths.group_fields.push_back({"OFFSET", 96, 16});
        widths.group_fields.push_back({"START_SCALE", 112, 16});
        widths.group_fields.push_back({"SLOPE_SCALE", 128, 16});
        widths.group_fields.push_back({"INTERCEPT_SCALE", 144, 16});
        
        widths.group_entry_bits = 160; // 10 * 16 bits
        
        // Delta layout for interval-specific parameters
        widths.delta_fields.push_back({"START", 0, 16});
        widths.delta_fields.push_back({"SLOPE", 16, 16});
        widths.delta_fields.push_back({"INTERCEPT", 32, 16});
        
        // Reflection flags (x and y) positions
        widths.reflection_x_pos = 48;
        widths.reflection_y_pos = 49;
        // FIXED: Use REFLECTION_WORD to clearly indicate this is the full word containing reflection flags
        widths.delta_fields.push_back({"REFLECTION_WORD", 48, 16});
        
        widths.delta_entry_bits = 64; // 4 * 16 bits
    }
    else if (alignment_mode == 2) { // 32-bit alignment mode
        // Group parameter layout - fields optimized for 32-bit access
        widths.group_fields.push_back({"START", 0, 32});
        widths.group_fields.push_back({"END", 32, 32});
        widths.group_fields.push_back({"B", 64, 32});
        widths.group_fields.push_back({"C", 96, 32});
        
        // Control bits positions
        widths.flags_pos = 128;
        widths.size_pos = 129;
        widths.use_pow2_pos = 137;
        widths.shift_amount_pos = 138;
        widths.group_fields.push_back({"CONTROL", 128, 32});
        
        // Additional format and scale parameters
        widths.group_fields.push_back({"FORMAT", 160, 32});
        widths.group_fields.push_back({"SCALES", 192, 32});
        widths.group_fields.push_back({"SCALES2", 224, 32});
        
        widths.group_entry_bits = 256; // 8 * 32 bits
        
        // Delta layout for 32-bit alignment
        widths.delta_fields.push_back({"START", 0, 32});
        widths.delta_fields.push_back({"SLOPE", 32, 32});
        
        // Reflection flags (x and y) positions
        widths.reflection_x_pos = 64;
        widths.reflection_y_pos = 65;
        // FIXED: Use INTERCEPT_REFL_WORD to clarify this is the full word
        widths.delta_fields.push_back({"INTERCEPT_REFL_WORD", 64, 32});
        
        widths.delta_entry_bits = 96; // 3 * 32 bits
    }
    else if (alignment_mode == 0) { // Compact mode
        // Compact mode - bit-level optimization for minimal memory usage
        // This creates a minimal memory layout with just enough bits for each field
        // Implementation based on required bit widths
        
        // Setup for minimal bit packing - example implementation
        int position = 0;
        
        // Add core group parameters
        widths.group_fields.push_back({"START", position, 16}); position += 16;
        widths.group_fields.push_back({"END", position, 16}); position += 16;
        widths.group_fields.push_back({"B", position, 16}); position += 16;
        widths.group_fields.push_back({"C", position, 16}); position += 16;
        
        // Add control flags with minimum bits
        widths.flags_pos = position; position += 1;
        widths.size_pos = position; position += 7;
        widths.group_fields.push_back({"FLAGS_SIZE", widths.flags_pos, 8});
        
        // Add power-of-2 optimization bits
        widths.use_pow2_pos = position; position += 1;
        widths.shift_amount_pos = position; position += 5;
        widths.group_fields.push_back({"POW2", widths.use_pow2_pos, 6});
        
        // Add remaining fields with minimal widths
        widths.group_fields.push_back({"OFFSET", position, 12}); position += 12;
        widths.group_fields.push_back({"START_SCALE", position, 12}); position += 12;
        widths.group_fields.push_back({"SLOPE_SCALE", position, 12}); position += 12;
        widths.group_fields.push_back({"INTERCEPT_SCALE", position, 12}); position += 12;
        
        // Round to byte boundary for easier memory organization
        widths.group_entry_bits = ((position + 7) / 8) * 8;
        
        // Delta layout with minimal bit sizes
        position = 0;
        widths.delta_fields.push_back({"START", position, 12}); position += 12;
        widths.delta_fields.push_back({"SLOPE", position, 16}); position += 16;
        widths.delta_fields.push_back({"INTERCEPT", position, 16}); position += 16;
        
        // Add reflection flags
        widths.reflection_x_pos = position; position += 1;
        widths.reflection_y_pos = position; position += 1;
        // FIXED: Use field name that reflects its actual size
        widths.delta_fields.push_back({"REFLECTION_FLAGS", widths.reflection_x_pos, 2});
        
        // Round to byte boundary
        widths.delta_entry_bits = ((position + 7) / 8) * 8;
    }
}

// Analyze parameters and compute bit widths
std::tuple<BitWidths, std::vector<IntervalGroup>, size_t, size_t> 
analyzeParametersAndComputeBitWidths(const std::vector<IntervalGroup>& groups, 
                                    int frac_bits, int alignment_mode) {
    BitWidths widths;
    std::vector<IntervalGroup> optimized_groups = groups;
    
    // Collect parameters for analysis
    std::vector<double> group_starts, group_ends;
    std::vector<double> base_b_values, base_c_values;
    std::vector<double> start_scales, slope_scales, intercept_scales;
    std::vector<int> quant_delta_starts, quant_delta_slopes, quant_delta_intercepts;
    std::vector<int> group_sizes, group_offsets;
    std::vector<int> shift_amounts;

    size_t total_intervals = 0;
    size_t max_intervals_per_group = 0;

    // First pass: analyze groups and calculate bit widths
    for (size_t i = 0; i < optimized_groups.size(); i++) {
        auto& group = optimized_groups[i];
        size_t interval_count = group.delta_encodings.size();
        
        if (interval_count > 1 && group.storage_type != ORPHAN_GROUP) {
            int power_of_two = 1;
            int shift_amount = 0;
            
            while (power_of_two * 2 <= static_cast<int>(interval_count)) {
                power_of_two *= 2;
                shift_amount++;
            }

            group.use_power_of_two = (power_of_two == static_cast<int>(interval_count));
            group.power_of_two_value = power_of_two;
            group.shift_amount = shift_amount;
            
            shift_amounts.push_back(shift_amount);
        } else {
            // Non-optimized group
            group.use_power_of_two = false;
            group.power_of_two_value = 0;
            group.shift_amount = 0;
        }
    }

    // Analyze each group and collect parameters
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

        for (const auto& delta : group.delta_encodings) {
            int q_delta_start = static_cast<int>(std::round(delta.delta_start / group.start_scale_factor));
            int q_delta_slope = static_cast<int>(std::round(delta.delta_slope / group.slope_scale_factor));
            int q_delta_intercept = static_cast<int>(std::round(delta.delta_intercept / group.intercept_scale_factor));

            quant_delta_starts.push_back(q_delta_start);
            quant_delta_slopes.push_back(q_delta_slope);
            quant_delta_intercepts.push_back(q_delta_intercept);
        }
    }

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

    // Calculate min/max for group sizes and offsets
    int max_shift_amount = shift_amounts.empty() ? 0 : *std::max_element(shift_amounts.begin(), shift_amounts.end());

    widths.group_start = calc_bits_needed(min_start, max_start) + frac_bits;
    widths.group_end = calc_bits_needed(min_end, max_end) + frac_bits;
    widths.base_b = calc_bits_needed(min_b, max_b) + frac_bits;
    widths.base_c = calc_bits_needed(min_c, max_c) + frac_bits;
    widths.flags = 1; // 1-bit for storage type
    widths.size = std::ceil(std::log2(max_intervals_per_group + 1));
    widths.offset = std::ceil(std::log2(total_intervals + 1));
    widths.start_scale = calc_bits_needed(min_start_scale, max_start_scale) + frac_bits;
    widths.slope_scale = calc_bits_needed(min_slope_scale, max_slope_scale) + frac_bits;
    widths.intercept_scale = calc_bits_needed(min_intercept_scale, max_intercept_scale) + frac_bits;
    widths.pow2_flags = 1 + std::ceil(std::log2(max_shift_amount + 1)); // use_pow2(1) + shift_amount
    widths.delta_start = calc_bits_needed(min_delta_start, max_delta_start);
    widths.delta_slope = calc_bits_needed(min_delta_slope, max_delta_slope);
    widths.delta_intercept = calc_bits_needed(min_delta_intercept, max_delta_intercept);
    widths.reflection = 2; // 2-bit：x-reflection + y-reflection

    widths.flags_pos = 0;        // 这些值会在createMemoryLayout函数中被正确设置
    widths.size_pos = 0;
    widths.use_pow2_pos = 0;
    widths.shift_amount_pos = 0;
    widths.reflection_x_pos = 0;
    widths.reflection_y_pos = 0;

    adjustBitWidthsByAlignmentMode(widths, alignment_mode);
    createMemoryLayout(widths, alignment_mode);

    return std::make_tuple(widths, optimized_groups, total_intervals, max_intervals_per_group);
}

/**
 * Generates hardware configuration file with all necessary macro definitions
 * @param bitwidth_file Output file path
 * @param cleanName Function name for documentation
 * @param optimized_groups Vector of interval groups
 * @param widths Bit position information
 * @param scale_factor Fixed-point scale factor
 * @param frac_bits Fractional bits in fixed-point representation
 * @param alignment_mode Memory alignment mode
 * @param total_intervals Total number of intervals
 * @param max_intervals_per_group Maximum intervals in any group
 * @param group_addr_width Bits needed for group addressing
 * @param interval_addr_width Bits needed for interval addressing
 * @param delta_addr_width Bits needed for delta entry addressing
 */
void generateBitWidthConfigFile(const std::string& bitwidth_file, 
                                const std::string& cleanName,
                                const std::vector<IntervalGroup>& optimized_groups,
                                const BitWidths& widths, int scale_factor, int frac_bits,
                                int alignment_mode, size_t total_intervals,
                                size_t max_intervals_per_group,
                                int group_addr_width, int interval_addr_width, int delta_addr_width,
                                int input_width = 16,    // Add input width parameter 
                                int output_width = 16) { // Add output width parameter

        std::ofstream bw_file(bitwidth_file);
        if (bw_file.is_open()) {
        // File header and basic parameters
        bw_file << "// Auto-generated bit width configuration for " << cleanName << "\n";
        bw_file << "// Alignment mode: " << alignment_mode << "\n\n";

        // Scale factor and bit width parameters
        bw_file << "`define OPT_SCALE_FACTOR " << scale_factor << "\n";
        bw_file << "`define OPT_FRAC_BITS " << frac_bits << "\n\n";

        // Add input/output width parameters
        bw_file << "// Input/Output data width configuration\n";
        bw_file << "`define INPUT_DATA_WIDTH " << input_width << "\n";
        bw_file << "`define OUTPUT_DATA_WIDTH " << output_width << "\n";
        bw_file << "`define INPUT_INT_BITS " << (input_width - frac_bits) << "\n";
        bw_file << "`define OUTPUT_INT_BITS " << (output_width - frac_bits) << "\n\n";

        // Rest of the existing code...
        bw_file << "`define OPT_NUM_GROUPS " << optimized_groups.size() << "\n";
        bw_file << "`define OPT_TOTAL_INTERVALS " << total_intervals << "\n";
        bw_file << "`define OPT_MAX_INTERVALS_PER_GROUP " << max_intervals_per_group << "\n\n";

        bw_file << "`define OPT_GROUP_ADDR_WIDTH " << group_addr_width << "\n";
        bw_file << "`define OPT_INTERVAL_ADDR_WIDTH " << interval_addr_width << "\n";
        bw_file << "`define OPT_DELTA_ADDR_WIDTH " << delta_addr_width << "\n\n";
        
        bw_file << "`define OPT_GROUP_ENTRY_BITS " << widths.group_entry_bits << "\n";
        bw_file << "`define OPT_GROUP_ENTRY_BYTES " << (widths.group_entry_bits + 7) / 8 << "\n\n";
        
        // Group field positions and widths
        bw_file << "// Group field positions and widths\n";
        for (const auto& [name, pos, width] : widths.group_fields) {
            std::string upper_name = name;
            std::transform(upper_name.begin(), upper_name.end(), upper_name.begin(), ::toupper);
            
            bw_file << "`define OPT_GROUP_" << upper_name << "_POS " << pos << "\n";
            bw_file << "`define OPT_GROUP_" << upper_name << "_WIDTH " << width << "\n";
        }
        
        // Group subfield positions
        bw_file << "\n// Group subfield positions\n";
        bw_file << "`define OPT_GROUP_FLAGS_POS " << widths.flags_pos << "\n";
        bw_file << "`define OPT_GROUP_SIZE_POS " << widths.size_pos << "\n";
        bw_file << "`define OPT_GROUP_USE_POW2_POS " << widths.use_pow2_pos << "\n";
        bw_file << "`define OPT_GROUP_SHIFT_AMOUNT_POS " << widths.shift_amount_pos << "\n\n";
        
        bw_file << "`define OPT_DELTA_ENTRY_BITS " << widths.delta_entry_bits << "\n";
        bw_file << "`define OPT_DELTA_ENTRY_BYTES " << (widths.delta_entry_bits + 7) / 8 << "\n\n";
        
        // Delta field positions and widths
        bw_file << "// Delta field positions and widths\n";
        for (const auto& [name, pos, width] : widths.delta_fields) {
            std::string upper_name = name;
            std::transform(upper_name.begin(), upper_name.end(), upper_name.begin(), ::toupper);
            
            // Skip generating any macro with REFLECTION in the name to avoid conflicts 
            // with the special compatibility macros below
            if (upper_name.find("REFLECTION") != std::string::npos || upper_name.find("REFL") != std::string::npos) {
                // Only generate position macros for these fields, not width
                if (upper_name == "REFLECTION_WORD") {
                    bw_file << "`define OPT_DELTA_" << upper_name << "_POS " << pos << "\n";
                } else if (upper_name == "INTERCEPT_REFL_WORD") {
                    bw_file << "`define OPT_DELTA_" << upper_name << "_POS " << pos << "\n";
                } else if (upper_name == "REFLECTION_FLAGS") {
                    bw_file << "`define OPT_DELTA_" << upper_name << "_POS " << pos << "\n";
                }
            } else {
                // Generate both position and width for non-reflection fields
                bw_file << "`define OPT_DELTA_" << upper_name << "_POS " << pos << "\n";
                bw_file << "`define OPT_DELTA_" << upper_name << "_WIDTH " << width << "\n";
            }
        }
        
        // Add special compatibility macros - critical for hardware compatibility
        // These are the macros that are expected by the hardware implementation
        bw_file << "\n// Special compatibility macros\n";
        bw_file << "`define OPT_DELTA_REFLECTION_POS " << widths.reflection_x_pos << "\n";
        bw_file << "`define OPT_DELTA_REFLECTION_WIDTH 2\n\n";
        
        // Reflection flag positions
        bw_file << "// Reflection flag positions\n";
        bw_file << "`define OPT_DELTA_REFLECTION_X_POS " << widths.reflection_x_pos << "\n";
        bw_file << "`define OPT_DELTA_REFLECTION_Y_POS " << widths.reflection_y_pos << "\n\n";
        
        // Memory sizing calculation
        size_t group_memory_bits = optimized_groups.size() * widths.group_entry_bits;
        size_t delta_memory_bits = total_intervals * widths.delta_entry_bits;
        size_t total_memory_bits = group_memory_bits + delta_memory_bits;
        
        bw_file << "// Memory sizing\n";
        bw_file << "`define OPT_GROUP_MEMORY_BITS " << group_memory_bits << "\n";
        bw_file << "`define OPT_DELTA_MEMORY_BITS " << delta_memory_bits << "\n";
        bw_file << "`define OPT_TOTAL_MEMORY_BITS " << total_memory_bits << "\n";
        
        bw_file.close();
    }
}

/**
 * Generates inline Verilog LUT initialization data with interval length format
 * and special handling for orphan groups
 * @param inline_vh_filename Output Verilog include file path
 * @param cleanName Function name for documentation
 * @param optimized_groups Vector of interval groups
 * @param widths Bit position information
 * @param scale_factor Fixed-point scale factor
 * @param alignment_mode Memory alignment mode (0=compact, 1=16-bit, 2=32-bit)
 * @param total_intervals Total number of intervals across all groups
 */
// Helper functions for quantizing different types of values
// Quantize to signed 15-bit fixed-point for parameters (slope/intercept)
int16_t quantize_signed_fixed_point(double value) {
    // Ensure value is in range [-1.0, 0.99997]
    if (value > 0.99997) value = 0.99997;
    if (value < -1.0) value = -1.0;
    
    // Quantize to 15-bit signed fixed-point (2^15 = 32768)
    return static_cast<int16_t>(std::round(value * 32768.0));
}

// Quantize position values (start/end) to signed 15-bit fixed-point
int16_t quantize_position(double value) {
    // Positions are treated as signed values for proper comparison
    // Clamp to reasonable range for 15-bit signed value
    if (value > 1.0) value = 1.0;
    if (value < -1.0) value = -1.0;
    
    return static_cast<int16_t>(std::round(value * 32768.0));
}

// Quantize length values to unsigned with 15-bit precision
uint16_t quantize_length(double value) {
    // Ensure value is positive
    if (value < 0.0) value = 0.0;
    // Clamp to range for 15-bit precision
    if (value > 1.0) value = 1.0;
    
    return static_cast<uint16_t>(std::round(value * 32768.0));
}

void generateInlineVerilogLUT(const std::string& inline_vh_filename, const std::string& cleanName,
                            const std::vector<IntervalGroup>& optimized_groups,
                            const BitWidths& widths, int scale_factor,
                            int alignment_mode, size_t total_intervals) {
    
    // Use 15-bit precision + 1 sign bit
    scale_factor = 32768;
    
    std::ofstream vh_file(inline_vh_filename);
    if (!vh_file.is_open()) {
        std::cerr << "Failed to open file: " << inline_vh_filename << "\n";
        return;
    }

    vh_file << "// Auto-generated inline LUT data for " << cleanName << " function\n";
    vh_file << "// Generated using " << (alignment_mode == 0 ? "compact" :
                                       (alignment_mode == 1 ? "16-bit aligned" : "32-bit aligned"))
            << " bit layout with signed 15-bit fixed-point representation\n\n";
    
    // Add debug log file for verification
    std::string debug_filename = inline_vh_filename + ".debug.log";
    std::ofstream debug_file(debug_filename);
    
    // Helper function to convert a value to the nearest power of 2
    auto to_power_of_two = [](double value) -> uint32_t {
        if (value <= 0) return 1; // Ensure at least 1
        int power = static_cast<int>(std::round(std::log2(value)));
        return static_cast<uint32_t>(1 << power); // Return actual power of 2 value
    };
    
    // Validate compatibility with hardware implementation
    vh_file << "// Compatibility verification - hardware aligns with these positions:\n";
    if (!widths.group_fields.empty()) {
        vh_file << "// GROUP_START_POS = " << std::get<1>(widths.group_fields[0]) << "\n";
    }
    if (widths.group_fields.size() > 1) {
        vh_file << "// GROUP_LENGTH_POS = " << std::get<1>(widths.group_fields[1]) << "\n";
    }
    vh_file << "// DELTA_REFLECTION_X_POS = " << widths.reflection_x_pos << "\n";
    vh_file << "// DELTA_REFLECTION_Y_POS = " << widths.reflection_y_pos << "\n\n";

    // Explanation of number representation and control fields
    vh_file << "// Explanation of control fields and number representation:\n";
    vh_file << "// All values use 15-bit fixed-point representation with scale factor 2^15=32768\n";
    vh_file << "// Parameter values (slope/intercept): signed [-1.0, 0.99997] maps to [-32768, 32767]\n";
    vh_file << "// Position values (start/end): signed for proper comparison\n";
    vh_file << "// Length values: unsigned but using same 15-bit precision\n";
    vh_file << "// FLAGS_SIZE: Bit 0 = group type (0=regular, 1=orphan), Bits [15:1] = actual interval count\n";
    vh_file << "//   - Orphan groups (bit 0=1): store complete parameter values instead of delta values\n";
    vh_file << "//   - Regular groups (bit 0=0): store delta values relative to base parameters\n";
    vh_file << "// GROUP_LENGTH: For regular groups only, stores the quantized length of the group range\n";
    vh_file << "//   - For orphan groups, this field is unused (set to 0)\n";
    vh_file << "// SLOPE_SCALE: Fixed at 2^15=32768 for all values\n";
    vh_file << "//   - Division by scale factor is implemented as right-shift by 15 bits\n";
    vh_file << "//   - This converts floating-point multiplies into fixed-point shift operations\n\n";

    // Reorder groups: orphan groups first, then regular groups
    std::vector<size_t> group_order;
    
    // First add orphan groups
    for (size_t i = 0; i < optimized_groups.size(); i++) {
        if (optimized_groups[i].storage_type == ORPHAN_GROUP) {
            group_order.push_back(i);
        }
    }
    
    // Then add normal groups
    for (size_t i = 0; i < optimized_groups.size(); i++) {
        if (optimized_groups[i].storage_type != ORPHAN_GROUP) {
            group_order.push_back(i);
        }
    }

    // Generate initialization code according to alignment mode
    if (alignment_mode == 1) {
        // 16-bit aligned mode
        vh_file << "// Initialization statements for group_info array\n";
        vh_file << "// Each group has 6 16-bit words with modified layout\n";

        size_t group_offset = 0;
        for (size_t idx = 0; idx < group_order.size(); idx++) {
            size_t i = group_order[idx];
            const auto& group = optimized_groups[i];
            bool is_orphan = (group.storage_type == ORPHAN_GROUP);
            
            debug_file << "===== Group " << i << " ===== \n";
            debug_file << "Storage Type: " << (is_orphan ? "ORPHAN" : "NORMAL") << "\n";
            debug_file << "Base Interval: [" << group.base_interval.start << ", " << group.base_interval.end << "]\n";
            debug_file << "Base Interval Length: " << (group.base_interval.end - group.base_interval.start) << "\n";
            debug_file << "Base Params: b=" << group.base_params.b << ", c=" << group.base_params.c << "\n";
            debug_file << "Scale Factor: " << scale_factor << " (15-bit precision)\n";

            // Actual interval count (excluding padding)
            int16_t actual_size = static_cast<int16_t>(group.delta_encodings.size());
            
            // Quantize base parameters using signed 15-bit fixed-point
            int16_t q_b = quantize_signed_fixed_point(group.base_params.b);
            int16_t q_c = quantize_signed_fixed_point(group.base_params.c);

            int16_t offset = static_cast<int16_t>(group_offset);
            group_offset += actual_size;

            // Calculate group length (only for regular groups)
            uint16_t group_length = 0;
            if (!is_orphan) {
                double length = group.base_interval.end - group.base_interval.start;
                // Length is always positive and treated as unsigned
                group_length = quantize_length(length);
            }

            // First entry: actual interval count and group type flag
            uint16_t flags = (is_orphan ? 0x1 : 0);
            uint16_t packed_flags_size = flags | (actual_size << 1);
            
            // Use 2^15=32768 as fixed scale factor
            uint16_t q_slope_scale = scale_factor;
            
            debug_file << "Actual interval count: " << actual_size << "\n";
            debug_file << "Quantized params (signed 15-bit): b=" << q_b << " (" << static_cast<double>(q_b)/scale_factor << ")"
                      << ", c=" << q_c << " (" << static_cast<double>(q_c)/scale_factor << ")\n";
            debug_file << "Fixed scale factor: " << scale_factor << " (2^15)\n";
            if (!is_orphan) {
                debug_file << "Group length (unsigned): " << group_length << " (" << static_cast<double>(group_length)/scale_factor << ")\n";
            }
            debug_file << "\n";

            // Write group_info initialization statements
            size_t base_idx = idx * 6; // 6 16-bit words per group
            vh_file << "group_info[" << ensureDecimalIndex(base_idx) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4)
                    << (packed_flags_size & 0xFFFF) << "; // Group " << std::dec << i << " FLAGS_SIZE\n";
            vh_file << "group_info[" << ensureDecimalIndex(base_idx+1) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4)
                    << (q_b & 0xFFFF) << "; // BASE_B (signed)\n";
            vh_file << "group_info[" << ensureDecimalIndex(base_idx+2) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4)
                    << (q_c & 0xFFFF) << "; // BASE_C (signed)\n";
            vh_file << "group_info[" << ensureDecimalIndex(base_idx+3) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4)
                    << (offset & 0xFFFF) << "; // OFFSET\n";
            vh_file << "group_info[" << ensureDecimalIndex(base_idx+4) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4)
                    << (q_slope_scale & 0xFFFF) << "; // SCALE_FACTOR (2^15)\n";
            vh_file << "group_info[" << ensureDecimalIndex(base_idx+5) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4)
                    << (group_length & 0xFFFF) << "; // " << (is_orphan ? "UNUSED" : "GROUP_LENGTH (unsigned)") << "\n";
            vh_file << "\n";
        }

        // Delta data initialization, special handling for orphan groups
        vh_file << "// Delta data initializations\n";
        vh_file << "// Each delta has 4 16-bit words with signed parameter values\n";
        vh_file << "// For orphan groups: 4th word stores END instead of REFLECTION flags\n";

        size_t delta_idx = 0;
        // First process all orphan group delta_data
        for (size_t idx = 0; idx < group_order.size(); idx++) {
            size_t i = group_order[idx];
            const auto& group = optimized_groups[i];
            if (group.storage_type != ORPHAN_GROUP) continue; // Only process orphan groups first

            vh_file << "// Delta data for Group " << i << " (ORPHAN)\n";
            for (const auto& delta : group.delta_encodings) {
                debug_file << "Delta for Group " << i << " (ORPHAN):\n";
                debug_file << "  Original: start=" << delta.delta_start 
                          << ", slope=" << delta.delta_slope 
                          << ", intercept=" << delta.delta_intercept << "\n";
                
                // Delta start needs to be quantized as a position (signed)
                int16_t q_delta_start = quantize_position(delta.delta_start);
                
                // Slope and intercept need to be converted to signed 15-bit fixed-point
                // If original delta values were based on 65536, adjust to base 32768
                double adj_delta_slope = delta.delta_slope / 65536.0;
                double adj_delta_intercept = delta.delta_intercept / 65536.0;
                
                int16_t q_delta_slope = quantize_signed_fixed_point(adj_delta_slope);
                int16_t q_delta_intercept = quantize_signed_fixed_point(adj_delta_intercept);
                
                // For orphan groups, calculate endpoint using signed position
                int16_t q_delta_end = quantize_position(delta.original_interval.end);
                
                // Calculate interval length for debugging
                double interval_length = delta.original_interval.end - delta.original_interval.start;
                
                debug_file << "  Orphan interval length: " << interval_length << "\n";
                debug_file << "  Quantized values (15-bit):\n";
                debug_file << "    start=" << q_delta_start << " (" << static_cast<double>(q_delta_start)/scale_factor << ")\n";
                debug_file << "    end=" << q_delta_end << " (" << static_cast<double>(q_delta_end)/scale_factor << ")\n";
                debug_file << "    length=" << (q_delta_end - q_delta_start) << " (" << static_cast<double>(q_delta_end - q_delta_start)/scale_factor << ")\n";
                debug_file << "    slope=" << q_delta_slope << " (" << static_cast<double>(q_delta_slope)/scale_factor << ")\n";
                debug_file << "    intercept=" << q_delta_intercept << " (" << static_cast<double>(q_delta_intercept)/scale_factor << ")\n\n";

                // Write array initialization statements
                size_t base_idx = delta_idx * 4; // 4 words per delta
                vh_file << "delta_data[" << ensureDecimalIndex(base_idx) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4)
                        << (q_delta_start & 0xFFFF) << "; // START (signed position)\n";
                vh_file << "delta_data[" << ensureDecimalIndex(base_idx+1) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4)
                        << (q_delta_slope & 0xFFFF) << "; // SLOPE (signed parameter)\n";
                vh_file << "delta_data[" << ensureDecimalIndex(base_idx+2) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4)
                        << (q_delta_intercept & 0xFFFF) << "; // INTERCEPT (signed parameter)\n";
                vh_file << "delta_data[" << ensureDecimalIndex(base_idx+3) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4)
                        << (q_delta_end & 0xFFFF) << "; // END (signed position)\n";

                delta_idx++;
            }
            vh_file << "\n";
        }
        
        // Then process regular group delta_data
        for (size_t idx = 0; idx < group_order.size(); idx++) {
            size_t i = group_order[idx];
            const auto& group = optimized_groups[i];
            if (group.storage_type == ORPHAN_GROUP) continue; // Skip orphan groups already processed

            vh_file << "// Delta data for Group " << i << "\n";
            for (const auto& delta : group.delta_encodings) {
                debug_file << "Delta for Group " << i << ":\n";
                debug_file << "  Original: start=" << delta.delta_start 
                          << ", slope=" << delta.delta_slope 
                          << ", intercept=" << delta.delta_intercept 
                          << ", x_refl=" << delta.is_x_reflected 
                          << ", y_refl=" << delta.is_y_reflected << "\n";
                
                // Delta start as signed position
                int16_t q_delta_start = quantize_position(delta.delta_start);
                
                // Adjust delta_slope and delta_intercept scale and quantize as signed params
                double adj_delta_slope = delta.delta_slope / 65536.0;
                double adj_delta_intercept = delta.delta_intercept / 65536.0;
                
                int16_t q_delta_slope = quantize_signed_fixed_point(adj_delta_slope);
                int16_t q_delta_intercept = quantize_signed_fixed_point(adj_delta_intercept);
                
                debug_file << "  Quantized values (15-bit):\n";
                debug_file << "    start=" << q_delta_start << " (" << static_cast<double>(q_delta_start)/scale_factor << ")\n";
                debug_file << "    slope=" << q_delta_slope << " (" << static_cast<double>(q_delta_slope)/scale_factor << ")\n";
                debug_file << "    intercept=" << q_delta_intercept << " (" << static_cast<double>(q_delta_intercept)/scale_factor << ")\n\n";

                // Reflection flags
                uint16_t reflection_flags = 0;
                if (delta.is_y_reflected) {
                    reflection_flags |= (1 << (widths.reflection_y_pos % 16));
                }
                if (delta.is_x_reflected) {
                    reflection_flags |= (1 << (widths.reflection_x_pos % 16));
                }

                // Write array initialization statements
                size_t base_idx = delta_idx * 4; // 4 words per delta
                vh_file << "delta_data[" << ensureDecimalIndex(base_idx) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4)
                        << (q_delta_start & 0xFFFF) << "; // START (signed position)\n";
                vh_file << "delta_data[" << ensureDecimalIndex(base_idx+1) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4)
                        << (q_delta_slope & 0xFFFF) << "; // SLOPE (signed parameter)\n";
                vh_file << "delta_data[" << ensureDecimalIndex(base_idx+2) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4)
                        << (q_delta_intercept & 0xFFFF) << "; // INTERCEPT (signed parameter)\n";
                vh_file << "delta_data[" << ensureDecimalIndex(base_idx+3) << "] = 16'h" << std::hex << std::setfill('0') << std::setw(4)
                        << (reflection_flags & 0xFFFF) << "; // REFLECTION flags\n";

                delta_idx++;
            }
            vh_file << "\n";
        }
    }
    else if (alignment_mode == 0) {
        // Compact mode implementation - similar changes would be needed
        vh_file << "// Compact mode memory initialization\n";
        vh_file << "// Using optimized bit packing with 15-bit fixed-point representation\n\n";

        // Implementation for compact mode would go here...
        // Similar changes needed as for 16-bit aligned mode
    }
    else {
        // 32-bit aligned mode implementation - similar changes would be needed 
        vh_file << "// 32-bit aligned memory initialization\n";
        vh_file << "// Using 15-bit fixed-point representation\n\n";

        // Implementation for 32-bit aligned mode would go here...
        // Similar changes needed as for 16-bit aligned mode
    }
    
    // Append usage guidance
    vh_file << "// Implementation guidance for hardware designer\n";
    vh_file << "/*\n";
    vh_file << "// Replace the original memory initialization with this:\n";
    vh_file << "`include \"" << cleanName << "_optimized_bitwidths.vh\"\n\n";
    vh_file << "// Memory arrays with optimized bit layouts\n";
    vh_file << "// Important: All parameter values use signed 15-bit fixed-point representation\n";
    vh_file << "// with SCALE_FACTOR = 32768 (2^15)\n";
    
    if (alignment_mode == 1) {
        vh_file << "(* ram_style = \"distributed\" *) reg [15:0] group_info [0:" 
                << ensureDecimalIndex(group_order.size() * 6 - 1) << "];\n";
        vh_file << "(* ram_style = \"distributed\" *) reg [15:0] delta_data [0:" 
                << ensureDecimalIndex(total_intervals * 4 - 1) << "];\n\n";
    }
    // Other formats would go here
    
    vh_file << "// For proper hardware implementation, update these constants:\n";
    vh_file << "localparam SCALE_FACTOR_BITS = 15;         // log2(SCALE_FACTOR)\n";
    vh_file << "localparam SCALE_FACTOR = 32768;           // 2^15 = 32768\n";
    vh_file << "*/\n";
    
    vh_file.close();
    debug_file.close();
    std::cout << "Inline Verilog LUT data generated with 15-bit signed fixed-point: " << inline_vh_filename << "\n";
    std::cout << "Inline Verilog debug log: " << debug_filename << "\n";
}

// Generate memory replacement file
void generateMemoryReplacementFile(const std::string& replace_file, const std::string& cleanName,
                                  const std::vector<IntervalGroup>& optimized_groups,
                                  const BitWidths& widths, int scale_factor,
                                  int alignment_mode, size_t total_intervals) {
    
    std::ofstream rep_file(replace_file);
    if (rep_file.is_open()) {
        rep_file << "// Replace the memory initialization section in pwl_hlut.v with this:\n\n";
        rep_file << "// Include optimized bit width definitions\n";
        rep_file << "`include \"" << cleanName << "_optimized_bitwidths.vh\"\n\n";

        rep_file << "// Memory arrays with optimized layouts\n";

        if (alignment_mode == 1) {
            // 16位对齐内存数组
            rep_file << "(* ram_style = \"distributed\" *) reg [15:0] group_info [0:" << ensureDecimalIndex(optimized_groups.size() * 10 - 1) << "];\n";
            rep_file << "(* ram_style = \"distributed\" *) reg [15:0] delta_data [0:" << ensureDecimalIndex(total_intervals * 4 - 1) << "];\n\n";
        }
        else if (alignment_mode == 0) {
            // 紧凑内存数组
            int group_bytes = (widths.group_entry_bits + 7) / 8;
            int delta_bytes = (widths.delta_entry_bits + 7) / 8;
            rep_file << "(* ram_style = \"distributed\" *) reg [7:0] group_data [0:" << ensureDecimalIndex(optimized_groups.size() * group_bytes - 1) << "];\n";
            rep_file << "(* ram_style = \"distributed\" *) reg [7:0] delta_data [0:" << ensureDecimalIndex(total_intervals * delta_bytes - 1) << "];\n\n";
        }
        else {
            // 32位对齐内存数组，具有动态比例打包
            int max_words_per_group = 8; // 5个标准 + 最多3个用于比例因子
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

            // 输出16位对齐模式的实际数据初始化
            rep_file << " // Group information data\n";
            size_t group_offset = 0;
            for (size_t i = 0; i < optimized_groups.size(); i++) {
                const auto& group = optimized_groups[i];

                // 量化参数
                int16_t q_start = static_cast<int16_t>(std::round(group.base_interval.start * scale_factor));
                int16_t q_end = static_cast<int16_t>(std::round(group.base_interval.end * scale_factor));
                int16_t q_b = static_cast<int16_t>(std::round(group.base_params.b * scale_factor));
                int16_t q_c = static_cast<int16_t>(std::round(group.base_params.c * scale_factor));

                int16_t size = static_cast<int16_t>(group.delta_encodings.size());
                int16_t offset = static_cast<int16_t>(group_offset);
                group_offset += size;

                uint16_t flags = (group.storage_type == ORPHAN_GROUP ? 0x1 : 0);
                uint16_t packed_flags_size = flags | (size << 1);
                
                // 2的幂优化标志
                uint16_t pow2_flags = 0;
                if (group.use_power_of_two) {
                    pow2_flags |= 0x1; // 设置use_pow2位
                }
                pow2_flags |= ((group.shift_amount & 0x1F) << 1); // 设置shift_amount (5位)

                int16_t q_start_scale = static_cast<int16_t>(std::round(group.start_scale_factor * scale_factor));
                int16_t q_slope_scale = static_cast<int16_t>(std::round(group.slope_scale_factor * scale_factor));
                int16_t q_intercept_scale = static_cast<int16_t>(std::round(group.intercept_scale_factor * scale_factor));

                // 为所有字段写入初始化
                size_t base_idx = i * 10; // 每组10个字段
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

            // Delta参数数据
            rep_file << "\n // Delta parameter data\n";
            size_t delta_idx = 0;
            for (size_t i = 0; i < optimized_groups.size(); i++) {
                const auto& group = optimized_groups[i];

                for (const auto& delta : group.delta_encodings) {
                    // 量化Delta值
                    int16_t q_delta_start = static_cast<int16_t>(std::round(delta.delta_start / group.start_scale_factor));
                    int16_t q_delta_slope = static_cast<int16_t>(std::round(delta.delta_slope / group.slope_scale_factor));
                    int16_t q_delta_intercept = static_cast<int16_t>(std::round(delta.delta_intercept / group.intercept_scale_factor));

                    uint16_t reflection_flags =
                        (delta.is_y_reflected ? 0x1 : 0) |
                        (delta.is_x_reflected ? 0x2 : 0);

                    // 写入所有字段
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
            // 紧凑模式初始化
            rep_file << " // Compact mode initialization - see optimized_lut.v for implementation\n";
        }
        else {
            // 32位对齐模式，具有动态比例打包
            int max_words_per_group = 8; // 5个标准 + 最多3个用于比例因子
            rep_file << " for (int i = 0; i < " << ensureDecimalIndex(optimized_groups.size() * max_words_per_group) << "; i = i + 1) begin\n";
            rep_file << " group_data[i] = 32'h00000000;\n";
            rep_file << " end\n";
            rep_file << " for (int i = 0; i < " << ensureDecimalIndex(total_intervals * 3) << "; i = i + 1) begin\n";
            rep_file << " delta_data[i] = 32'h00000000;\n";
            rep_file << " end\n\n";

            // 计算值所需的位数的函数
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

            // 输出带有动态比例打包的初始化
            rep_file << " // Group information data with dynamic scale packing\n";
            size_t group_offset = 0;
            for (size_t i = 0; i < optimized_groups.size(); i++) {
                const auto& group = optimized_groups[i];

                // 量化参数
                int32_t q_start = static_cast<int32_t>(std::round(group.base_interval.start * scale_factor));
                int32_t q_end = static_cast<int32_t>(std::round(group.base_interval.end * scale_factor));
                int32_t q_b = static_cast<int32_t>(std::round(group.base_params.b * scale_factor));
                int32_t q_c = static_cast<int32_t>(std::round(group.base_params.c * scale_factor));

                int size = static_cast<int>(group.delta_encodings.size());
                int offset = static_cast<int>(group_offset);
                group_offset += size;

                uint32_t flags = (group.storage_type == ORPHAN_GROUP ? 0x1 : 0);
                
                // 2的幂标志
                uint32_t use_pow2 = group.use_power_of_two ? 1 : 0;
                uint32_t shift_amount = group.shift_amount & 0x1F; // 5位

                // 将控制字段打包到一个32位字中
                uint32_t packed_control = flags | (size << 1) | (use_pow2 << 9) | (shift_amount << 10) | (offset << 15);

                // 量化比例因子
                int32_t q_start_scale = static_cast<int32_t>(std::round(group.start_scale_factor * scale_factor));
                int32_t q_slope_scale = static_cast<int32_t>(std::round(group.slope_scale_factor * scale_factor));
                int32_t q_intercept_scale = static_cast<int32_t>(std::round(group.intercept_scale_factor * scale_factor));

                // 写入基本参数
                size_t base_idx = i * 8; // 每组最多8个字（5个标准 + 最多3个用于比例）
                rep_file << " group_data[" << ensureDecimalIndex(base_idx) << "] = 32'h" << std::hex << std::setfill('0') << std::setw(8) << (q_start & 0xFFFFFFFF) << ";\n";
                rep_file << " group_data[" << ensureDecimalIndex(base_idx+1) << "] = 32'h" << std::hex << std::setfill('0') << std::setw(8) << (q_end & 0xFFFFFFFF) << ";\n";
                rep_file << " group_data[" << ensureDecimalIndex(base_idx+2) << "] = 32'h" << std::hex << std::setfill('0') << std::setw(8) << (q_b & 0xFFFFFFFF) << ";\n";
                rep_file << " group_data[" << ensureDecimalIndex(base_idx+3) << "] = 32'h" << std::hex << std::setfill('0') << std::setw(8) << (q_c & 0xFFFFFFFF) << ";\n";
                rep_file << " group_data[" << ensureDecimalIndex(base_idx+4) << "] = 32'h" << std::hex << std::setfill('0') << std::setw(8) << (packed_control & 0xFFFFFFFF) << ";\n";

                // 比例因子的动态打包
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

            // Delta参数数据
            rep_file << "\n // Delta parameter data\n";
            size_t delta_idx = 0;
            for (size_t i = 0; i < optimized_groups.size(); i++) {
                const auto& group = optimized_groups[i];

                for (const auto& delta : group.delta_encodings) {
                    // 量化Delta值
                    int32_t q_delta_start = static_cast<int32_t>(std::round(delta.delta_start / group.start_scale_factor));
                    int32_t q_delta_slope = static_cast<int32_t>(std::round(delta.delta_slope / group.slope_scale_factor));
                    int32_t q_delta_intercept = static_cast<int32_t>(std::round(delta.delta_intercept / group.intercept_scale_factor));

                    uint32_t reflection_flags =
                        (delta.is_y_reflected ? 0x1 : 0) |
                        (delta.is_x_reflected ? 0x2 : 0);

                    // 打包截距和反射
                    uint32_t packed_intercept_reflection = (q_delta_intercept & 0x3FFFFFFF) | (reflection_flags << 30);

                    // 写入所有字段
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
}

void updateConfigFile(const std::string& config_filename, int alignment_mode) {
    std::ofstream config_file(config_filename, std::ios::app);

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
}

// Analyze memory usage and savings
void analyzeMemoryUsage(const std::vector<IntervalGroup>& optimized_groups,
                       const BitWidths& widths, size_t total_intervals,
                       int alignment_mode) {
    int std_group_bits = optimized_groups.size() * 11 * 16; // Regular: Every group 11 * 16-bit words
    int std_delta_bits = total_intervals * 4 * 16; // Regular: 4 16-bit words per interval
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

// Generate optimized Verilog LUTs
void generateOptimizedVerilogLUTs(const std::string& directory,
                                  const std::string& cleanName,
                                  const std::vector<IntervalGroup>& groups,
                                  int scale_factor, int frac_bits,
                                  int alignment_mode = 0,  // 0=compact, 1=16-bit, 2=32-bit
                                  int input_width = 16,    // Add input width parameter
                                  int output_width = 16) { // Add output width parameter

    std::cout << "\nGenerating optimized Verilog LUT data for " << cleanName
              << " using " << (alignment_mode == 0 ? "compact" :
                             (alignment_mode == 1 ? "16-bit aligned" : "32-bit aligned"))
              << " layout with power-of-two optimization...\n";
    std::cout << "Data widths: input=" << input_width << " bits, output=" << output_width << " bits\n";

    // Ensure path doesn't end with slash
    std::string dir = directory;
    if (!dir.empty() && (dir.back() == '/' || dir.back() == '\\')) {
        dir.pop_back();
    }

    // Define output files
    std::string inline_vh_filename = dir + "/" + cleanName + "_inline_lut_data.vh";
    std::string replace_file = dir + "/" + cleanName + "_replace_memory_section.v";
    std::string bitwidth_file = dir + "/" + cleanName + "_optimized_bitwidths.vh";

    auto [widths, optimized_groups, total_intervals, max_intervals_per_group] = 
        analyzeParametersAndComputeBitWidths(groups, frac_bits, alignment_mode);

    int group_addr_width = std::ceil(std::log2(optimized_groups.size()));
    int interval_addr_width = std::ceil(std::log2(max_intervals_per_group));
    int delta_addr_width = std::ceil(std::log2(total_intervals));

    // Pass input/output width to bit width configuration file
    generateBitWidthConfigFile(bitwidth_file, cleanName, optimized_groups, widths, scale_factor, 
                             frac_bits, alignment_mode, total_intervals, max_intervals_per_group,
                             group_addr_width, interval_addr_width, delta_addr_width,
                             input_width, output_width);  // Pass width parameters

    generateInlineVerilogLUT(inline_vh_filename, cleanName, optimized_groups, widths, 
                           scale_factor, alignment_mode, total_intervals);

    generateMemoryReplacementFile(replace_file, cleanName, optimized_groups, widths, 
                                scale_factor, alignment_mode, total_intervals);

    updateConfigFile(dir + "/" + cleanName + "_config.vh", alignment_mode);

    analyzeMemoryUsage(optimized_groups, widths, total_intervals, alignment_mode);
}

// Master function to generate all hardware parameter files
void generateHardwareImplementation(const std::string& directory,
                                    const std::string& functionName,
                                    const std::vector<IntervalGroup>& groups,
                                    const std::vector<Interval>& intervals,
                                    const std::vector<FitParameters>& fit_params,
                                    const std::string& expression_str,
                                    double start = 0.0, 
                                    double end = 1.0,
                                    double scale_factor = 1024.0,
                                    double target_error = 0.0001,
                                    int input_width = 16,
                                    int output_width = 16) {
    if (groups.empty() || intervals.empty() || fit_params.empty() || expression_str.empty()) {
        std::cout << "Missing required data for hardware parameter generation.\n";
        return;
    }
    
    std::cout << "\n=== Generating Hardware Parameters for " << functionName << " ===\n";
    std::cout << "Input width: " << input_width << " bits, Output width: " << output_width << " bits\n";
    
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
    generateOptimizedVerilogLUTs(func_dir, functionName, groups, scale_factor, frac_bits, 1, 
                               input_width, output_width);  // Pass width parameters
    
    std::cout << "Hardware parameter files generated in: " << func_dir << "\n";
    std::cout << "=== Hardware Parameter Generation Complete ===\n\n";
}

#endif // HW_MAPPING_HPP
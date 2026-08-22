#ifndef HW_MAPPING_HPP
#define HW_MAPPING_HPP

#include "hw_types.hpp"
#include "hw_utils.hpp"
#include "hw_file_gen_fixed.hpp"
#include "hw_file_gen_float.hpp"
#include "group_types.hpp"
#include "quantization_eval.hpp"

//================================================================================
// Generate hardware files for a specific QuantizationConfig
//================================================================================
inline void generateHardwareMappingForConfig(
    const std::string& base_dir,
    const std::string& func_name,
    const std::vector<QuantizedGroup>& groups,
    const QuantizationConfig& config,
    bool orphan_first = false)
{
    std::string config_name = config.name();

    std::cout << "  Generating hardware files: " << config_name << "\n";
    std::cout << "    Endpoints: " << config.endpoint_precision.name() << "\n";
    std::cout << "    Parameters: " << config.param_precision.name() << "\n";

    // Determine which generator to use based on precision config
    auto& ep = config.endpoint_precision;
    auto& pp = config.param_precision;

    // Both floating-point
    if (ep.format != NumericFormat::FIXED && pp.format != NumericFormat::FIXED) {
        // Use the higher precision for generation
        if (ep.format == NumericFormat::FP64 || pp.format == NumericFormat::FP64) {
            generate_hw_files_float64(base_dir, func_name, groups, orphan_first);
        } else if (ep.format == NumericFormat::FP32 || pp.format == NumericFormat::FP32) {
            generate_hw_files_float32(base_dir, func_name, groups, orphan_first);
        } else {
            generate_hw_files_float16(base_dir, func_name, groups, orphan_first);
        }
    }
    // At least one is fixed-point
    else {
        // Determine fixed-point configuration
        int total_bits = 16;
        int frac_bits = 15;

        if (ep.format == NumericFormat::FIXED) {
            int ep_total = ep.int_bits + ep.frac_bits;
            total_bits = std::max(total_bits, ep_total);
            frac_bits = static_cast<int>(ep.frac_bits);
        }
        if (pp.format == NumericFormat::FIXED) {
            int pp_total = pp.int_bits + pp.frac_bits;
            total_bits = std::max(total_bits, pp_total);
            frac_bits = std::max(frac_bits, static_cast<int>(pp.frac_bits));
        }

        FixedPointConfig fixed_config(total_bits, frac_bits);
        generate_hw_files_fixed(base_dir, func_name, groups, orphan_first, fixed_config);
    }
}

//================================================================================
// Get all predefined configurations
//================================================================================
inline std::vector<QuantizationConfig> getAllQuantizationConfigs() {
    std::vector<QuantizationConfig> configs;

    // Helper to create precision configs
    auto fp16 = PrecisionConfig(NumericFormat::FP16, 0, 0);
    auto fp32 = PrecisionConfig(NumericFormat::FP32, 0, 0);
    auto fp64 = PrecisionConfig(NumericFormat::FP64, 0, 0);
    auto fixed16 = PrecisionConfig(NumericFormat::FIXED, 1, 15);  // Q1.15
    auto fixed32 = PrecisionConfig(NumericFormat::FIXED, 1, 31);  // Q1.31

    // Floating-point configurations
    configs.push_back(QuantizationConfig(fp16, fp16));   // FP16-FP16
    configs.push_back(QuantizationConfig(fp32, fp32));   // FP32-FP32
    configs.push_back(QuantizationConfig(fp64, fp64));   // FP64-FP64
    configs.push_back(QuantizationConfig(fp32, fp16));   // FP32-FP16
    configs.push_back(QuantizationConfig(fp64, fp32));   // FP64-FP32

    // Fixed-point configurations
    configs.push_back(QuantizationConfig(fixed16, fixed16));  // FIXED16-FIXED16
    configs.push_back(QuantizationConfig(fixed32, fixed32));  // FIXED32-FIXED32

    // Mixed configurations
    configs.push_back(QuantizationConfig(fp32, fixed16));  // FP32-FIXED16
    configs.push_back(QuantizationConfig(fp16, fixed16));  // FP16-FIXED16

    return configs;
}

//================================================================================
// Generate hardware files for ALL predefined configurations
//================================================================================
inline void generateHardwareMappingAllConfigs(
    const std::string& base_dir,
    const std::string& func_name,
    const std::vector<QuantizedGroup>& groups,
    bool orphan_first = false)
{
    std::cout << "\n========================================\n";
    std::cout << "Generating Hardware Files for: " << func_name << "\n";
    std::cout << "Groups: " << groups.size()
              << " | Intervals: " << count_intervals(groups) << "\n";
    std::cout << "Order: " << (orphan_first ? "orphan-first" : "normal-first") << "\n";
    std::cout << "========================================\n";

    auto configs = getAllQuantizationConfigs();

    for (size_t i = 0; i < configs.size(); i++) {
        std::cout << "\n[" << (i + 1) << "/" << configs.size() << "] ";
        generateHardwareMappingForConfig(base_dir, func_name, groups, configs[i], orphan_first);
    }

    // Summary
    std::cout << "\n========================================\n";
    std::cout << "✓ All " << configs.size() << " Hardware Configurations Generated!\n";
    std::cout << "========================================\n";
    std::cout << "Output: " << base_dir << "/\n";
    for (const auto& cfg : configs) {
        std::cout << "  ├── " << cfg.name() << "/\n";
    }
    std::cout << "========================================\n\n";
}

//================================================================================
// Legacy: Generate fixed-point hardware files (default 16-bit)
//================================================================================
inline void generateHardwareMapping(
    const std::string& dir,
    const std::string& func_name,
    const std::vector<QuantizedGroup>& groups,
    bool orphan_first = false)
{
    generate_hw_files_fixed16(dir, func_name, groups, orphan_first);
}

//================================================================================
// Legacy: Generate three common versions
//================================================================================
inline void generateHardwareMappingAll(
    const std::string& base_dir,
    const std::string& func_name,
    const std::vector<QuantizedGroup>& groups,
    bool orphan_first = false)
{
    std::cout << "\n========================================\n";
    std::cout << "Generating Hardware Files for: " << func_name << "\n";
    std::cout << "Groups: " << groups.size()
              << " | Intervals: " << count_intervals(groups) << "\n";
    std::cout << "Order: " << (orphan_first ? "orphan-first" : "normal-first") << "\n";
    std::cout << "========================================\n";

    // Fixed-point version (16-bit)
    {
        std::string fixed_dir = base_dir + "/fixed";
        std::cout << "\n[1/3] Generating Fixed-Point (16-bit)...\n";
        generate_hw_files_fixed16(fixed_dir, func_name, groups, orphan_first);
    }

    // FP32 (Full precision)
    {
        std::string fp32_dir = base_dir + "/float32";
        std::cout << "\n[2/3] Generating FP32 (full precision)...\n";
        generate_hw_files_float32(fp32_dir, func_name, groups, orphan_first);
    }

    // FP16 (Default precision)
    {
        std::string fp16_dir = base_dir + "/float16";
        std::cout << "\n[3/3] Generating FP16 (default precision)...\n";
        generate_hw_files_float16(fp16_dir, func_name, groups, orphan_first);
    }

    // Summary
    std::cout << "\n========================================\n";
    std::cout << "✓ All Hardware Files Generated!\n";
    std::cout << "========================================\n";
    std::cout << "Output: " << base_dir << "/\n";
    std::cout << "  ├── fixed/       Fixed-point (16-bit Q1.15)\n";
    std::cout << "  ├── float32/     FP32 (full precision)\n";
    std::cout << "  └── float16/     FP16 (default)\n";
    std::cout << "========================================\n\n";
}

//================================================================================
// Legacy: Generate both fixed and float (FP16 only)
//================================================================================
inline void generateHardwareMappingBoth(
    const std::string& base_dir,
    const std::string& func_name,
    const std::vector<QuantizedGroup>& groups,
    bool orphan_first = false)
{
    std::cout << "\n=== Generating Hardware Files (Legacy Mode) ===\n";

    // Fixed-point
    std::string fixed_dir = base_dir + "/fixed";
    std::cout << "\n[1/2] Generating Fixed-Point (16-bit)...\n";
    generate_hw_files_fixed16(fixed_dir, func_name, groups, orphan_first);

    // FP16 only (for backward compatibility)
    std::string float_dir = base_dir + "/float";
    std::cout << "\n[2/2] Generating FP16...\n";
    generate_hw_files_float16(float_dir, func_name, groups, orphan_first);

    std::cout << "\n=== Generation Complete ===\n";
    std::cout << "Fixed-point: " << fixed_dir << "/\n";
    std::cout << "Float (FP16): " << float_dir << "/\n";
    std::cout << "Group order: " << (orphan_first ? "orphan-first" : "normal-first") << "\n";
}

#endif // HW_MAPPING_HPP
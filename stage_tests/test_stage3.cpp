#include <iostream>
#include <vector>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <sys/stat.h>
#include "group_types.hpp"
#include "group_encode.hpp"

//================================================================================
// Utility: Create directory if it doesn't exist
//================================================================================
void create_directory(const std::string& path) {
    struct stat info;
    if (stat(path.c_str(), &info) != 0) {
        #ifdef _WIN32
            _mkdir(path.c_str());
        #else
            mkdir(path.c_str(), 0755);
        #endif
    }
}

//================================================================================
// Load compressed data from Stage 2 output
//================================================================================
CompressedIntervalData load_compressed_data(const std::string& output_dir, Stage3Config& config) {
    CompressedIntervalData data;
    
    // Load compression stats to get bit widths
    std::string stats_file = output_dir + "/compression_stats.csv";
    std::ifstream stats(stats_file);
    if (!stats.is_open()) {
        throw std::runtime_error("Cannot open: " + stats_file);
    }
    
    std::string header, line;
    std::getline(stats, header); // Skip header
    
    while (std::getline(stats, line)) {
        std::stringstream ss(line);
        std::string metric, value_str;
        std::getline(ss, metric, ',');
        std::getline(ss, value_str);
        
        if (metric == "total_intervals") data.total_intervals = std::stoi(value_str);
        else if (metric == "total_groups") data.total_groups = std::stoi(value_str);
        else if (metric == "compression_ratio") data.compression_ratio = std::stod(value_str);
        else if (metric == "position_bits") config.delta_position_bits = std::stoi(value_str);
        else if (metric == "delta_a_bits") config.delta_a_bits = std::stoi(value_str);
        else if (metric == "delta_b_bits") config.delta_b_bits = std::stoi(value_str);
        else if (metric == "delta_c_bits") config.delta_c_bits = std::stoi(value_str);
    }
    stats.close();
    
    // Load quantized groups
    std::string groups_file = output_dir + "/quantized_groups.csv";
    std::ifstream groups(groups_file);
    if (!groups.is_open()) {
        throw std::runtime_error("Cannot open: " + groups_file);
    }
    
    std::getline(groups, header); // Skip header: group_id,start_interval,num_intervals,is_symmetric,length_class,base_pos,base_a,base_b,base_c
    
    std::vector<int> start_intervals;
    std::vector<int> num_intervals;
    std::vector<bool> is_symmetric;
    
    while (std::getline(groups, line)) {
        std::stringstream ss(line);
        int group_id, start_iv, num_iv, is_sym, length_class;
        double base_pos, base_a, base_b, base_c;
        char comma;
        
        ss >> group_id >> comma
           >> start_iv >> comma
           >> num_iv >> comma
           >> is_sym >> comma
           >> length_class >> comma
           >> base_pos >> comma
           >> base_a >> comma
           >> base_b >> comma
           >> base_c;
        
        start_intervals.push_back(start_iv);
        num_intervals.push_back(num_iv);
        is_symmetric.push_back(is_sym != 0);
        
        QuantizedGroup g;
        g.group_id = "group_" + std::to_string(group_id);
        g.storage_type = (num_iv == 1) ? GroupStorageType::ORPHAN_GROUP : GroupStorageType::POWER_OF_2_GROUP;
        g.count = num_iv;
        g.base_params.a = base_a;
        g.base_params.b = base_b;
        g.base_params.c = base_c;
        g.has_symmetry = (is_sym != 0);
        g.delta_position_bits = config.delta_position_bits;
        g.delta_a_bits = config.delta_a_bits;
        g.delta_b_bits = config.delta_b_bits;
        g.delta_c_bits = config.delta_c_bits;
        
        data.groups.push_back(g);
    }
    groups.close();
    
    // Load quantized deltas
    std::string deltas_file = output_dir + "/quantized_deltas.csv";
    std::ifstream deltas(deltas_file);
    if (!deltas.is_open()) {
        throw std::runtime_error("Cannot open: " + deltas_file);
    }
    
    std::getline(deltas, header); // Skip header
    
    while (std::getline(deltas, line)) {
        std::stringstream ss(line);
        int group_id, interval_idx;
        int16_t dp, da, db, dc;
        char comma;
        
        ss >> group_id >> comma
           >> interval_idx >> comma
           >> dp >> comma
           >> da >> comma
           >> db >> comma
           >> dc;
        
        if (group_id < (int)data.groups.size()) {
            QuantizedDelta qd;
            qd.original_index = interval_idx;
            qd.delta_start_q = dp;
            qd.delta_a_q = da;
            qd.delta_b_q = db;
            qd.delta_c_q = dc;
            
            data.groups[group_id].members.push_back(qd);
        }
    }
    deltas.close();
    
    return data;
}

//================================================================================
// Generate hardware memory files
//================================================================================
void generate_hardware_files(const CompressedIntervalData& data, 
                             const Stage3Config& config,
                             const std::string& output_dir,
                             const std::string& module_name,
                             bool use_fixed_point) {
    
    create_directory(output_dir);
    
    // 1. Group Metadata (group_id, storage_type, count)
    {
        std::string filename = output_dir + "/group_metadata.mem";
        std::ofstream ofs(filename);
        
        for (const auto& g : data.groups) {
            uint32_t metadata = 0;
            metadata |= (g.count & 0xFF);                                    // [7:0] count
            metadata |= ((uint32_t)g.storage_type & 0x3) << 8;              // [9:8] storage type
            metadata |= (g.has_symmetry ? 1 : 0) << 10;                     // [10] has_symmetry
            
            if (use_fixed_point) {
                ofs << std::hex << std::setw(8) << std::setfill('0') << metadata << "\n";
            } else {
                ofs << std::dec << metadata << "\n";
            }
        }
        ofs.close();
    }
    
    // 2. Base Coefficients (base_a, base_b, base_c per group)
    {
        std::string filename = output_dir + "/base_coefficients.mem";
        std::ofstream ofs(filename);
        
        for (const auto& g : data.groups) {
            if (use_fixed_point) {
                // Convert to Q16.16 fixed-point
                int32_t a_fixed = (int32_t)(g.base_params.a * 65536.0);
                int32_t b_fixed = (int32_t)(g.base_params.b * 65536.0);
                int32_t c_fixed = (int32_t)(g.base_params.c * 65536.0);
                
                ofs << std::hex << std::setw(8) << std::setfill('0') << (uint32_t)a_fixed << "\n";
                ofs << std::hex << std::setw(8) << std::setfill('0') << (uint32_t)b_fixed << "\n";
                ofs << std::hex << std::setw(8) << std::setfill('0') << (uint32_t)c_fixed << "\n";
            } else {
                // IEEE 754 float32
                union { float f; uint32_t i; } a_conv, b_conv, c_conv;
                a_conv.f = (float)g.base_params.a;
                b_conv.f = (float)g.base_params.b;
                c_conv.f = (float)g.base_params.c;
                
                ofs << std::hex << std::setw(8) << std::setfill('0') << a_conv.i << "\n";
                ofs << std::hex << std::setw(8) << std::setfill('0') << b_conv.i << "\n";
                ofs << std::hex << std::setw(8) << std::setfill('0') << c_conv.i << "\n";
            }
        }
        ofs.close();
    }
    
    // 3. Delta Positions
    {
        std::string filename = output_dir + "/delta_positions.mem";
        std::ofstream ofs(filename);
        
        for (const auto& g : data.groups) {
            for (const auto& m : g.members) {
                uint16_t dp = (uint16_t)(m.delta_start_q & 0xFFFF);
                ofs << std::hex << std::setw(4) << std::setfill('0') << dp << "\n";
            }
        }
        ofs.close();
    }
    
    // 4. Delta Coefficients (delta_a, delta_b, delta_c per interval)
    {
        std::string filename = output_dir + "/delta_coefficients.mem";
        std::ofstream ofs(filename);
        
        for (const auto& g : data.groups) {
            for (const auto& m : g.members) {
                uint16_t da = (uint16_t)(m.delta_a_q & 0xFFFF);
                uint16_t db = (uint16_t)(m.delta_b_q & 0xFFFF);
                uint16_t dc = (uint16_t)(m.delta_c_q & 0xFFFF);
                
                ofs << std::hex << std::setw(4) << std::setfill('0') << da << "\n";
                ofs << std::hex << std::setw(4) << std::setfill('0') << db << "\n";
                ofs << std::hex << std::setw(4) << std::setfill('0') << dc << "\n";
            }
        }
        ofs.close();
    }
    
    // 5. Verilog Configuration Header
    {
        std::string filename = output_dir + "/" + module_name + "_config.vh";
        std::ofstream ofs(filename);
        
        ofs << "// Auto-generated hardware configuration for " << module_name << "\n";
        ofs << "// Generated by Stage 4 Hardware Mapping\n\n";
        
        ofs << "`ifndef " << module_name << "_CONFIG_VH\n";
        ofs << "`define " << module_name << "_CONFIG_VH\n\n";
        
        ofs << "// Total statistics\n";
        ofs << "`define TOTAL_INTERVALS " << data.total_intervals << "\n";
        ofs << "`define TOTAL_GROUPS " << data.total_groups << "\n\n";
        
        ofs << "// Bit widths\n";
        ofs << "`define DELTA_POS_BITS " << (int)config.delta_position_bits << "\n";
        ofs << "`define DELTA_A_BITS " << (int)config.delta_a_bits << "\n";
        ofs << "`define DELTA_B_BITS " << (int)config.delta_b_bits << "\n";
        ofs << "`define DELTA_C_BITS " << (int)config.delta_c_bits << "\n\n";
        
        ofs << "// Data format\n";
        if (use_fixed_point) {
            ofs << "`define USE_FIXED_POINT 1\n";
            ofs << "`define DATA_WIDTH 32\n";
            ofs << "`define INTEGER_BITS 16\n";
            ofs << "`define FRACTIONAL_BITS 16\n";
        } else {
            ofs << "`define USE_FLOATING_POINT 1\n";
            ofs << "`define DATA_WIDTH 32  // IEEE 754 float32\n";
        }
        
        ofs << "\n`endif // " << module_name << "_CONFIG_VH\n";
        ofs.close();
    }
}

//================================================================================
// Test case structure
//================================================================================
struct HardwareTest {
    std::string name;
    std::string input_dir;
    std::string output_base_dir;
};

//================================================================================
// Run hardware mapping test
//================================================================================
void run_hardware_test(const HardwareTest& test, bool use_fixed_point) {
    std::string format_str = use_fixed_point ? "Fixed-point" : "Floating-point";
    
    std::cout << "\n========================================\n";
    std::cout << "Testing Hardware Mapping: " << test.name << "\n";
    std::cout << "Format: " << format_str << "\n";
    std::cout << "========================================\n";
    
    try {
        // Load compressed data
        std::cout << "Loading compressed data from: " << test.input_dir << "\n";
        Stage3Config config;
        CompressedIntervalData data = load_compressed_data(test.input_dir, config);
        
        std::cout << "✓ Loaded " << data.total_groups << " groups, "
                  << data.total_intervals << " intervals\n";
        std::cout << "  Bit widths: pos=" << (int)config.delta_position_bits
                  << " dA=" << (int)config.delta_a_bits
                  << " dB=" << (int)config.delta_b_bits
                  << " dC=" << (int)config.delta_c_bits << "\n";
        
        // Create output directory with bit-width info
        std::stringstream output_dir_ss;
        output_dir_ss << test.output_base_dir;
        if (use_fixed_point) {
            output_dir_ss << "_fixed_pos" << (int)config.delta_position_bits
                         << "_dA" << (int)config.delta_a_bits
                         << "_dB" << (int)config.delta_b_bits
                         << "_dC" << (int)config.delta_c_bits;
        } else {
            output_dir_ss << "_float32";
        }
        std::string output_dir = output_dir_ss.str();
        
        // Generate hardware files
        std::cout << "Generating hardware files...\n";
        generate_hardware_files(data, config, output_dir, test.name, use_fixed_point);
        
        std::cout << "✓ Hardware files generated in: " << output_dir << "/\n";
        std::cout << "  Files created:\n";
        std::cout << "    - group_metadata.mem\n";
        std::cout << "    - base_coefficients.mem\n";
        std::cout << "    - delta_positions.mem\n";
        std::cout << "    - delta_coefficients.mem\n";
        std::cout << "    - " << test.name << "_config.vh\n";
        
    } catch (const std::exception& e) {
        std::cout << "✗ Error: " << e.what() << "\n";
    }
}

//================================================================================
// Main
//================================================================================
int main() {
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║          Stage 4: Hardware Mapping Test                   ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n\n";
    
    std::cout << "This test reads compressed groups from Stage 2 and generates\n";
    std::cout << "hardware-ready .mem files for FPGA/ASIC implementation.\n";
    
    // Define test cases - use actual directories from Stage 2
    std::vector<HardwareTest> tests = {
        {"tanh_0_1_1e_4", "results/tanh_0_1_1e_4", "results/tanh_0_1_1e_4_mem"},
        {"tanh_0_1_1e_5", "results/tanh_0_1_1e_5", "results/tanh_0_1_1e_5_mem"},
        {"exp_0_1_1e_4", "results/exp_0_1_1e_4", "results/exp_0_1_1e_4_mem"},
        {"sin_0_pi2_1e_4", "results/sin_0_pi2_1e_4", "results/sin_0_pi2_1e_4_mem"},
        {"gelu_0_1_1e_4", "results/gelu_0_1_1e_4", "results/gelu_0_1_1e_4_mem"},
        {"silu__2_1_1e_4", "results/silu__2_1_1e_4", "results/silu__2_1_1e_4_mem"}
    };
    
    // Run tests for both fixed-point and floating-point
    for (const auto& test : tests) {
        run_hardware_test(test, true);  // Fixed-point
        run_hardware_test(test, false); // Floating-point
    }
    
    std::cout << "\n╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║          All Hardware Mapping Tests Completed              ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n\n";
    
    std::cout << "Generated directories (with bit-width specifications):\n";
    for (const auto& test : tests) {
        std::cout << "   " << test.output_base_dir << "_fixed_pos16_dA16_dB16_dC16/\n";
        std::cout << "   " << test.output_base_dir << "_float32/\n";
    }
    
    std::cout << "\n Next steps:\n";
    std::cout << "   1. Review generated .mem files in results/*_mem*/\n";
    std::cout << "   2. Check Verilog config files (*_config.vh)\n";
    std::cout << "   3. Integrate with hardware RTL design\n";
    std::cout << "   4. Run RTL simulation to verify functionality\n";
    
    return 0;
}
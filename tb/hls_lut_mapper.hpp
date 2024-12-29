// hls_lut_mapper.hpp

#ifndef HLS_LUT_MAPPER_HPP
#define HLS_LUT_MAPPER_HPP

#include <ap_int.h>
#include <ap_fixed.h>
#include <hls_stream.h>
#include <vector>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iomanip>
#include "function_fitter.hpp"          // Includes FitParameters structure
#include "interval_group_compressor.hpp" // Includes CompressedFitParameters structure

// Define FPGA LUT depth and width
#define LUT_DEPTH 1024    // Adjust according to actual requirements
#define LUT_WIDTH 80      // Bit width of each LUT entry (e.g., 80-bit: 16 bits each for start, end, a, b, c)

// Define Fixed-Point data type
typedef ap_fixed<16,6> fixed_point_t; // 16-bit fixed point with 6 integer bits

// Structure to represent a LUT entry
struct LUTEntry {
    fixed_point_t start; // 16 bits
    fixed_point_t end;   // 16 bits
    fixed_point_t a;     // 16 bits
    fixed_point_t b;     // 16 bits
    fixed_point_t c;     // 16 bits
};

// Helper function to convert double to fixed-point hexadecimal representation
inline std::string doubleToFixedHex(double value, int total_bits = 16, int frac_bits = 10) {
    // Simple fixed-point conversion (Q6.10)
    int fixed = static_cast<int>(std::round(value * (1 << frac_bits)));
    // Handle negative values using two's complement
    if (fixed < 0) {
        fixed = (1 << total_bits) + fixed;
    }
    std::stringstream ss;
    ss << std::hex << std::setw(total_bits / 4) << std::setfill('0') << (fixed & ((1 << total_bits) - 1));
    return ss.str();
}

inline void saveLUTToVerilog(
    const std::vector<CompressedFitParameters>& compressed_params_list,
    const std::vector<Interval>& intervals,
    const std::string& filename
) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Unable to open file: " << filename << " for writing." << std::endl;
        return;
    }

    int lut-depth = 0;
    for (const auto& comp_param : compressed_params_list) {
        lut-depth += comp_param.interval_indices.size();
    }
    
    int address_width = static_cast<int>(std::ceil(std::log2(lut_depth)));

    // Write Verilog module definition
    file << "module " << "LUT" << " (\n";
    file << "   input wire [" << address_width - 1 << ":0] address,\n";
    file << "   output reg [79:0] data\n";
    file << ");\n\n";

    file << "    reg [79:0] rom [0:" << lut_depth - 1 << "];\n\n";
    file << "    initial begin\n";
    int current_index = 0;
    for (const auto& comp_param : compressed_params_list) {
        for (auto interval_idx : comp_param.interval_indices) {
            const Interval& interval = intervals[interval_idx];
            double start_val = interval.start;
            double end_val = interval.end;
            double offset = comp_param.offsets[0];
            const FitParameters& params = comp_param.params;
            double a = 0.0, b = 0.0, c = 0.0;
            switch (params.method) {
                case FittingMethod::Linear:
                    a = 0.0;
                    b = params.b;
                    c = params.c + offset;
                    break;
                case FittingMethod::Quadratic:
                    a = params.a;
                    b = params.b;
                    c = params.c + offset;
                    break;
                case FittingMethod::BSpline:
                    if (params.spline.order() > 3) {
                        a = params.spline.controlPoints()(0,0) + offset;
                        b = params.spline.controlPoints()(1,0);
                        c = params.spline.controlPoints()(2,0);
                    } else if (params.spline.order() > 2) {
                        a = params.spline.controlPoints()(0,0) + offset;
                        b = params.spline.controlPoints()(1,0);
                        c = params.spline.controlPoints()(2,0);
                    } else if (params.spline.order() > 1) {
                        a = params.spline.controlPoints()(0,0) + offset;
                        b = params.spline.controlPoints()(1,0);
                        c = 0.0;
                    } else {
                        a = params.spline.controlPoints()(0,0) + offset;
                        b = 0.0;
                        c = 0.0;
                    }
                    break;
                default:
                    break;
            }

            std::string start_hex = doubleToFixedHex(start_val, 16, 10);
            std::string end_hex = doubleToFixedHex(end_val, 16, 10);
            std::string a_hex = doubleToFixedHex(a, 16, 10);
            std::string b_hex = doubleToFixedHex(b, 16, 10);
            std::string c_hex = doubleToFixedHex(c, 16, 10);

            std::stringstream data_stream;
            data_stream << "80'h" << start_hex << end_hex << a_hex << b_hex <<
        }
    }
    file << "    end\n\n";
    file << "    always @(*) begin\n";
    file << "        data = rom[address];\n";
    file << "    end\n";
    file << "endmodule\n";

}

// HLS module: Maps CompressedFitParameters data to FPGA LUT
extern "C" {
    void lut_mapper(
        const CompressedFitParameters compressed_params[], // Input: Array of compressed fit parameters
        const size_t num_compressed_params,                // Input: Number of compressed fit parameters
        const Interval intervals[],                        // Input: Array of all intervals
        LUTEntry lut[LUT_DEPTH]                            // Output: FPGA LUT
    ) {
        // HLS interface directives
        #pragma HLS INTERFACE m_axi port=compressed_params offset=slave bundle=gmem
        #pragma HLS INTERFACE m_axi port=intervals offset=slave bundle=gmem
        #pragma HLS INTERFACE m_axi port=lut offset=slave bundle=gmem
        #pragma HLS INTERFACE s_axilite port=compressed_params bundle=control
        #pragma HLS INTERFACE s_axilite port=num_compressed_params bundle=control
        #pragma HLS INTERFACE s_axilite port=intervals bundle=control
        #pragma HLS INTERFACE s_axilite port=lut bundle=control
        #pragma HLS INTERFACE s_axilite port=return bundle=control

        // Initialize LUT entries to zero
        for (int i = 0; i < LUT_DEPTH; i++) {
            #pragma HLS PIPELINE II=1
            lut[i].start = 0;
            lut[i].end = 0;
            lut[i].a = 0;
            lut[i].b = 0;
            lut[i].c = 0;
        }

        int lut_index = 0;

        // Iterate over all compressed fit parameter groups
        for (size_t g = 0; g < num_compressed_params; g++) {
            const CompressedFitParameters &comp_param = compressed_params[g];
            const FitParameters &params = comp_param.params;

            // Iterate over all intervals in the current group
            for (size_t i = 0; i < comp_param.interval_indices.size(); i++) {
                if (lut_index >= LUT_DEPTH) break;

                size_t interval_idx = comp_param.interval_indices[i];
                double offset = comp_param.offsets[i];

                // Get the corresponding interval
                const Interval &interval = intervals[interval_idx];
                double start_val = interval.start;
                double end_val = interval.end;

                // Calculate fitting function parameters, considering the offset
                double a = 0.0, b = 0.0, c = 0.0;
                switch (params.method) {
                    case FittingMethod::Linear:
                        a = 0.0;
                        b = params.b;
                        c = params.c + offset;
                        break;
                    case FittingMethod::Quadratic:
                        a = params.a;
                        b = params.b;
                        c = params.c + offset;
                        break;
                    case FittingMethod::BSpline:
                        // Dynamically handle the number of control points
                        if (params.spline.order() > 3) { // Ensure there are enough control points
                            a = params.spline.controlPoints()(0,0) + offset;
                            b = params.spline.controlPoints()(1,0);
                            c = params.spline.controlPoints()(2,0);
                        } else if (params.spline.order() > 2) {
                            // If there are exactly three control points
                            a = params.spline.controlPoints()(0,0) + offset;
                            b = params.spline.controlPoints()(1,0);
                            c = params.spline.controlPoints()(2,0);
                        } else if (params.spline.order() > 1) {
                            // If there are only two control points
                            a = params.spline.controlPoints()(0,0) + offset;
                            b = params.spline.controlPoints()(1,0);
                            c = 0.0; // Default value if not enough control points
                        } else {
                            // If there is only one control point
                            a = params.spline.controlPoints()(0,0) + offset;
                            b = 0.0;
                            c = 0.0;
                        }
                        break;
                    default:
                        break;
                }

                lut[lut_index].start = fixed_point_t(start_val);
                lut[lut_index].end = fixed_point_t(end_val);
                lut[lut_index].a = fixed_point_t(a);
                lut[lut_index].b = fixed_point_t(b);
                lut[lut_index].c = fixed_point_t(c);

                lut_index++;
            }

            if (lut_index >= LUT_DEPTH) {
                // LUT is full, stop mapping
                break;
            }
        }

        // Fill remaining LUT entries with zero
        for (; lut_index < LUT_DEPTH; lut_index++) {
            #pragma HLS PIPELINE II=1
            lut[lut_index].start = 0;
            lut[lut_index].end = 0;
            lut[lut_index].a = 0;
            lut[lut_index].b = 0;
            lut[lut_index].c = 0;
        }
    }
}

// Function to save compressed fit parameters to a Verilog `.v` file
inline void saveCompressedParametersToVerilogFile(
    const std::vector<CompressedFitParameters>& compressed_params_list,
    const std::vector<Interval>& intervals,
    const std::string& filename
) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Unable to open file: " << filename << " for writing." << std::endl;
        return;
    }

    // Write Verilog module definition
    file << "module FitParametersROM (\n";
    file << "    input wire [9:0] address, // Supports 1024 addresses\n";
    file << "    output reg [79:0] data     // Each entry is 80-bit: 16-bit start, 16-bit end, 16-bit a, 16-bit b, 16-bit c\n";
    file << ");\n\n";

    file << "    // ROM storage for fit parameters\n";
    file << "    reg [79:0] rom [0:" << LUT_DEPTH - 1 << "];\n\n";

    file << "    initial begin\n";

    size_t index = 0;
    // Iterate over all compressed fit parameter groups
    for (const auto& comp_param : compressed_params_list) {
        const FitParameters& params = comp_param.params;

        // Iterate over all intervals in the current group
        for (size_t i = 0; i < comp_param.interval_indices.size(); i++) {
            if (index >= LUT_DEPTH) break;

            size_t interval_idx = comp_param.interval_indices[i];
            double offset = comp_param.offsets[i];

            // Get the corresponding interval
            const Interval& interval = intervals[interval_idx];
            double start_val = interval.start;
            double end_val = interval.end;

            // Calculate fitting function parameters, considering the offset
            double a = 0.0, b = 0.0, c = 0.0;
            switch (params.method) {
                case FittingMethod::Linear:
                    a = 0.0;
                    b = params.b;
                    c = params.c + offset;
                    break;
                case FittingMethod::Quadratic:
                    a = params.a;
                    b = params.b;
                    c = params.c + offset;
                    break;
                case FittingMethod::BSpline:
                    // Dynamically handle the number of control points
                    if (params.spline.order() > 3) { // Ensure there are enough control points
                        a = params.spline.controlPoints()(0,0) + offset;
                        b = params.spline.controlPoints()(1,0);
                        c = params.spline.controlPoints()(2,0);
                    } else if (params.spline.order() > 2) {
                        // If there are exactly three control points
                        a = params.spline.controlPoints()(0,0) + offset;
                        b = params.spline.controlPoints()(1,0);
                        c = params.spline.controlPoints()(2,0);
                    } else if (params.spline.order() > 1) {
                        // If there are only two control points
                        a = params.spline.controlPoints()(0,0) + offset;
                        b = params.spline.controlPoints()(1,0);
                        c = 0.0; // Default value if not enough control points
                    } else {
                        // If there is only one control point
                        a = params.spline.controlPoints()(0,0) + offset;
                        b = 0.0;
                        c = 0.0;
                    }
                    break;
                default:
                    break;
            }

            // Convert to fixed-point hexadecimal representation
            std::string start_hex = doubleToFixedHex(start_val, 16, 10);
            std::string end_hex = doubleToFixedHex(end_val, 16, 10);
            std::string a_hex = doubleToFixedHex(a, 16, 10);
            std::string b_hex = doubleToFixedHex(b, 16, 10);
            std::string c_hex = doubleToFixedHex(c, 16, 10);

            // Combine into an 80-bit data entry: start (16) | end (16) | a (16) | b (16) | c (16)
            std::stringstream data_stream;
            data_stream << "80'h" 
                       << start_hex << end_hex << a_hex << b_hex << c_hex;

            file << "        rom[" << index << "] = " << data_stream.str() << ";\n";
            index++;
        }
    }

    // Fill remaining LUT entries with zero
    for (; index < LUT_DEPTH; index++) {
        file << "        rom[" << index << "] = 80'h00000000000000000000;\n";
    }

    file << "    end\n\n";

    file << "    always @(*) begin\n";
    file << "        data = rom[address];\n";
    file << "    end\n";

    file << "endmodule\n";

    file.close();
    std::cout << "Compressed parameters have been saved to Verilog file: " << filename << std::endl;
}

#endif // HLS_LUT_MAPPER_HPP

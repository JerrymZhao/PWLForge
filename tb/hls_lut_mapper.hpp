// hls_lut_mapper.hpp

#ifndef HLS_LUT_MAPPER_HPP
#define HLS_LUT_MAPPER_HPP

#include <ap_int.h>
#include "hls_stream.h"
#include <vector>
#include <cmath>
#include "interval_group_compressor.hpp"

// Define FPGA LUT depth and width
#define LUT_DEPTH 1024    // Adjust according to actual requirements
#define LUT_WIDTH 16      // Bit width of each LUT entry (e.g., 16-bit floating point)

// Define IntervalGroup structure (ensure consistency with interval_group_compressor.hpp)
struct IntervalGroup {
    double length; // Length of all intervals in the group
    Interval base_interval; // Base interval
    std::vector<double> delta_starts; // Differences in start points relative to the base interval
    std::vector<double> delta_ends; // Differences in end points relative to the base interval
};

// HLS module: Maps IntervalGroup data to FPGA LUT
// Using AXI4-Stream interface as an example; adjust the interface according to FPGA design requirements
#include <ap_fixed.h>

// Define fixed-point data type based on requirements
typedef ap_fixed<16,6> fixed_point_t; // 16-bit fixed point with 6 integer bits

// Structure to represent a LUT entry
struct LUTEntry {
    fixed_point_t start;
    fixed_point_t end;
};

// HLS function declaration
extern "C" {
    void lut_mapper(
        const IntervalGroup groups[],    // Input: Array of grouped intervals
        const size_t num_groups,        // Input: Number of groups
        LUTEntry lut[LUT_DEPTH]         // Output: FPGA LUT
    );
}

#endif // HLS_LUT_MAPPER_HPP

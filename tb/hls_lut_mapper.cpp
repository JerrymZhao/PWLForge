// hls_lut_mapper.cpp

#include "hls_lut_mapper.hpp"

// Converts a double to a fixed-point number (simplified example)
inline fixed_point_t double_to_fixed(double val) {
    // Simple truncation to fixed-point; in real applications, consider overflow and precision
    return fixed_point_t(val);
}

extern "C" {
    void lut_mapper(
        const IntervalGroup groups[],   // Input: Array of grouped intervals
        const size_t num_groups,        // Input: Number of groups
        LUTEntry lut[LUT_DEPTH]         // Output: FPGA LUT
    ) {
        // Initialize LUT
        for(int i = 0; i < LUT_DEPTH; i++) {
#pragma HLS PIPELINE
            lut[i].start = 0;
            lut[i].end = 0;
        }

        size_t lut_idx = 0;

        // Iterate over each group
        for(size_t g = 0; g < num_groups; g++) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=100
#pragma HLS PIPELINE
            IntervalGroup group = groups[g];

            // Base interval
            if(lut_idx < LUT_DEPTH) {
                lut[lut_idx].start = double_to_fixed(group.base_interval.start);
                lut[lut_idx].end = double_to_fixed(group.base_interval.end);
                lut_idx++;
            }

            // Delta-encoded intervals
            for(size_t i = 0; i < group.delta_starts.size(); i++) {
                if(lut_idx >= LUT_DEPTH) break;

                double new_start = group.base_interval.start + group.delta_starts[i];
                double new_end = group.base_interval.end + group.delta_ends[i];

                lut[lut_idx].start = double_to_fixed(new_start);
                lut[lut_idx].end = double_to_fixed(new_end);
                lut_idx++;
            }
        }

        // Fill remaining LUT entries with zeros if necessary
        for(; lut_idx < LUT_DEPTH; lut_idx++) {
#pragma HLS PIPELINE
            lut[lut_idx].start = 0;
            lut[lut_idx].end = 0;
        }
    }
}

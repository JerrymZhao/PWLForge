//================================================================================
// pwl_hlut.v - Hierarchical LUT implementation for piecewise linear approximation
//================================================================================

// NOTE: This file requires function_config.vh to be included by the build system
`include "/vol/datastore/jmzhao/CompressedLUT/b-spline/testCPP/results/tanh/tanh_config.vh"

// Include optimized bit width definitions
`include "/vol/datastore/jmzhao/CompressedLUT/b-spline/testCPP/results/tanh/tanh_optimized_bitwidths.vh"

module pwl_hlut (
    input wire clk,
    input wire rst_n,
    input wire [15:0] x_in,
    input wire in_valid,
    output reg [15:0] y_out,
    output reg out_valid
);

    // Pipeline stages
    reg [15:0] x_reg1, x_reg2;
    reg valid_pipe1, valid_pipe2;

    // Group selection stage
    reg [`GROUP_ADDR_WIDTH-1:0] group_id;
    reg [15:0] group_start, group_end;
    reg [15:0] base_b, base_c; // base_a removed (linear fit only)
    reg [15:0] start_scale, slope_scale, intercept_scale;
    reg is_orphan; // is_quadratic removed (linear fit only)
    reg [15:0] group_size, group_offset;

    // Interval selection stage
    reg [`INTERVAL_ADDR_WIDTH-1:0] interval_idx;
    reg [15:0] delta_start, delta_slope, delta_intercept;
    reg is_x_reflected, is_y_reflected;
    reg [15:0] adjusted_x;

    // Loop and temporary variables
    integer i;
    reg [15:0] g_start, g_end;
    reg [31:0] idx, next_idx, delta_idx;
    reg [15:0] d_start, next_d_start;
    reg [31:0] scaled_d_start, next_scaled_d_start;
    reg [15:0] adjusted_start, next_start;
    reg [15:0] interval_start, mid_point;
    reg [15:0] flags, refl_flags;

    // DSP multiplication signals
    (* use_dsp = "yes" *) reg [31:0] result;
    (* use_dsp = "yes" *) reg [31:0] scaled_delta_slope, scaled_delta_intercept;
    (* use_dsp = "yes" *) reg [15:0] actual_b, actual_c;
    (* use_dsp = "yes" *) reg [31:0] b_x; // a_x and a_x2 removed (linear fit only)

    // Memory arrays with optimized layouts - using distributed RAM for fast access
    // 9 words per group (versus 11 in original) due to optimization
    (* ram_style = "distributed" *) reg [15:0] group_info [0:`OPT_NUM_GROUPS*9-1];
    // 4 words per delta interval (same as original)
    (* ram_style = "distributed" *) reg [15:0] delta_data [0:`OPT_TOTAL_INTERVALS*4-1];

    // Memory initialization with optimized inline data
    initial begin
        // Initialize all memory with zeros
        for (i = 0; i < `OPT_NUM_GROUPS*9; i = i + 1) begin
            group_info[i] = 16'h0000;
        end
        for (i = 0; i < `OPT_TOTAL_INTERVALS*4; i = i + 1) begin
            delta_data[i] = 16'h0000;
        end

        // Include the generated data initialization
        `include "/vol/datastore/jmzhao/CompressedLUT/b-spline/testCPP/results/tanh/tanh_inline_lut_data.vh"
    end

    // Stage 1: Group Selection - Optimized with parallel comparisons
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            x_reg1 <= 16'h0000;
            valid_pipe1 <= 1'b0;
            group_id <= 0;
        end else begin
            x_reg1 <= x_in;
            valid_pipe1 <= in_valid;
            
            // Find group containing x_in - using parallel comparison for common cases
            if (in_valid) begin
                group_id <= 0; // Default to first group
                
                // Parallel comparison for common cases (first 8 groups)
                if (x_in >= group_info[0] && x_in < group_info[1]) begin
                    group_id <= 0;
                end else if (x_in >= group_info[9] && x_in < group_info[10]) begin
                    group_id <= 1;
                end else if (x_in >= group_info[18] && x_in < group_info[19]) begin
                    group_id <= 2;
                end else if (x_in >= group_info[27] && x_in < group_info[28]) begin
                    group_id <= 3;
                end else if (x_in >= group_info[36] && x_in < group_info[37]) begin
                    group_id <= 4;
                end else if (x_in >= group_info[45] && x_in < group_info[46]) begin
                    group_id <= 5;
                end else if (x_in >= group_info[54] && x_in < group_info[55]) begin
                    group_id <= 6;
                end else if (x_in >= group_info[63] && x_in < group_info[64]) begin
                    group_id <= 7;
                end else begin
                    // Fallback for remaining groups
                    for (i = 8; i < `OPT_NUM_GROUPS; i = i + 1) begin
                        if (x_in >= group_info[i*9] && x_in < group_info[i*9+1]) begin
                            group_id <= i;
                        end
                    end
                end
            end
        end
    end

    // Fetch group parameters - Fast LUT access with optimized layout
    always @(posedge clk) begin
        if (valid_pipe1) begin
            // Fetch from new optimized layout (9 words per group vs 11 in original)
            group_start <= group_info[group_id * 9];       // Field 0: group_start
            group_end <= group_info[group_id * 9 + 1];     // Field 1: group_end
            base_b <= group_info[group_id * 9 + 2];        // Field 2: base_b
            base_c <= group_info[group_id * 9 + 3];        // Field 3: base_c
            
            // Extract flags - simplified to only contain orphan flag
            flags = group_info[group_id * 9 + 4];
            is_orphan <= flags[0];                         // Only storage type flag
            
            // Group size now comes from bits [8:1] of the flags word
            group_size <= flags >> 1;
            
            // Group offset
            group_offset <= group_info[group_id * 9 + 5];  // Field 5: offset
            
            // Scale factors
            start_scale <= group_info[group_id * 9 + 6];   // Field 6: start_scale
            slope_scale <= group_info[group_id * 9 + 7];   // Field 7: slope_scale
            intercept_scale <= group_info[group_id * 9 + 8]; // Field 8: intercept_scale
        end
    end

    // Stage 2: Interval Selection - Optimized direct calculation for uniform intervals
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            x_reg2 <= 16'h0000;
            valid_pipe2 <= 1'b0;
            interval_idx <= 0;
        end else begin
            x_reg2 <= x_reg1;
            valid_pipe2 <= valid_pipe1;
            
            // Find interval within group - optimized calculation
            if (valid_pipe1) begin
                if (group_size > 0 && !is_orphan) begin
                    // Direct calculation for uniform intervals
                    if ((group_size & (group_size - 1)) == 0) begin
                        // If group_size is power of 2, use bit shifting (faster)
                        interval_idx <= ((x_reg1 - group_start) * group_size) >> $clog2(group_end - group_start);
                    end else begin
                        // Regular division for non-power-of-2 sizes
                        interval_idx <= ((x_reg1 - group_start) * group_size) / (group_end - group_start);
                    end
                    
                    // Clamp index to valid range
                    if (interval_idx >= group_size) begin
                        interval_idx <= group_size - 16'h0001;
                    end
                end else begin
                    // Traditional search for non-uniform intervals
                    interval_idx <= 0; // Default to first interval
                    for (i = 0; i < 16; i = i + 1) begin // Practical limit - check only first 16
                        if (i < group_size) begin
                            idx = group_offset + i;
                            d_start = delta_data[idx * 4]; // DELTA_DATA_WORDS_PER_ENTRY = 4
                            scaled_d_start = $signed(d_start) * $signed(start_scale);
                            adjusted_start = group_start + (scaled_d_start >>> `OPT_FRAC_BITS);
                            
                            // Next interval start or group end
                            if (i < group_size - 1) begin
                                next_idx = group_offset + i + 1;
                                next_d_start = delta_data[next_idx * 4];
                                next_scaled_d_start = $signed(next_d_start) * $signed(start_scale);
                                next_start = group_start + (next_scaled_d_start >>> `OPT_FRAC_BITS);
                            end else begin
                                next_start = group_end;
                            end
                            
                            if (x_reg1 >= adjusted_start && x_reg1 < next_start) begin
                                interval_idx <= i;
                            end
                        end
                    end
                end
                
                // Pre-calculate delta_idx to reduce latency
                delta_idx <= group_offset + interval_idx;
            end
        end
    end

    // Fetch interval parameters and begin DSP calculations early
    always @(posedge clk) begin
        if (valid_pipe2) begin
            // Fetch interval parameters - using LUT for fast access
            delta_start <= delta_data[delta_idx * 4];
            delta_slope <= delta_data[delta_idx * 4 + 1];
            delta_intercept <= delta_data[delta_idx * 4 + 2];
            
            // Extract reflection flags
            refl_flags = delta_data[delta_idx * 4 + 3];
            is_y_reflected <= refl_flags[0];
            is_x_reflected <= refl_flags[1];
            
            // Calculate interval start
            scaled_d_start = $signed(delta_start) * $signed(start_scale);
            interval_start = group_start + (scaled_d_start >>> `OPT_FRAC_BITS);
            
            // Apply X reflection if needed
            if (refl_flags[1]) begin // is_x_reflected
                // Calculate next interval start for midpoint
                if (interval_idx < group_size - 1) begin
                    next_idx = delta_idx + 1;
                    next_d_start = delta_data[next_idx * 4];
                    next_scaled_d_start = $signed(next_d_start) * $signed(start_scale);
                    next_start = group_start + (next_scaled_d_start >>> `OPT_FRAC_BITS);
                    
                    // Midpoint for reflection
                    mid_point = interval_start + ((next_start - interval_start) >>> 1);
                end else begin
                    // Use group end for last interval
                    mid_point = interval_start + ((group_end - interval_start) >>> 1);
                end
                adjusted_x <= ((mid_point << 1) - x_reg2);
            end else begin
                adjusted_x <= x_reg2;
            end
            
            // Begin coefficient scaling calculations early - using DSP
            scaled_delta_slope <= $signed(delta_data[delta_idx * 4 + 1]) * $signed(slope_scale);
            scaled_delta_intercept <= $signed(delta_data[delta_idx * 4 + 2]) * $signed(intercept_scale);
        end
    end

    // Stage 3: Computation - Using DSP for multiplications (simplified for linear only)
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            y_out <= 16'h0000;
            out_valid <= 1'b0;
        end else begin
            out_valid <= valid_pipe2;
            
            if (valid_pipe2) begin
                // Calculate actual coefficients
                actual_b = base_b + (scaled_delta_slope >>> `OPT_FRAC_BITS);
                actual_c = base_c + (scaled_delta_intercept >>> `OPT_FRAC_BITS);
                
                // Linear only: b*x + c (quadratic removed since we eliminated base_a)
                b_x = $signed(actual_b) * $signed(adjusted_x);
                
                // Apply Y reflection if needed
                if (is_y_reflected) begin
                    result = -($signed(b_x >>> `OPT_FRAC_BITS) + $signed(actual_c));
                end else begin
                    result = $signed(b_x >>> `OPT_FRAC_BITS) + $signed(actual_c);
                end
                
                // Output the result
                y_out <= result[15:0];
            end
        end
    end
endmodule

`include "/vol/datastore/jmzhao/CompressedLUT/b-spline/testCPP/hardware/include/tanh_optimized_bitwidths.vh"
`define OPT_DATA_WIDTH `INPUT_DATA_WIDTH

module pwl_hlut (
    input                         clk,
    input                         rst_n,
    input  signed [`INPUT_DATA_WIDTH-1:0] x_in,
    input                         valid_in,
    output reg signed [`OUTPUT_DATA_WIDTH-1:0] y_out,
    output reg                        valid_out
);

// Constants - Updated for 15-bit fixed-point
localparam SCALE_FACTOR_BITS = 15;         // log2(SCALE_FACTOR)
localparam SCALE_FACTOR = 32768;           // 2^15 = 32768

// Group structure definitions
localparam NUM_GROUPS = 3;                // Total number of groups
localparam GROUP_WORDS = 6;               // Words per group (including GROUP_LENGTH)
localparam DELTA_WORDS = 4;               // Words per delta

// Field position constants
localparam FLAGS_SIZE_IDX = 0;            // FLAGS_SIZE index in group
localparam BASE_B_IDX = 1;                // BASE_B index in group
localparam BASE_C_IDX = 2;                // BASE_C index in group
localparam OFFSET_IDX = 3;                // OFFSET index in group
localparam SCALE_IDX = 4;                 // SCALE_FACTOR index in group
localparam GROUP_LENGTH_IDX = 5;          // GROUP_LENGTH index in group

// Delta field position constants
localparam DELTA_START_IDX = 0;           // START index in delta
localparam DELTA_SLOPE_IDX = 1;           // SLOPE index in delta
localparam DELTA_INTERCEPT_IDX = 2;       // INTERCEPT index in delta
localparam DELTA_END_FLAGS_IDX = 3;       // END or REFLECTION flags index in delta

// Orphan group flag masks
localparam ORPHAN_FLAG_MASK = 16'h0001;   // Bit 0 = 1 means orphan group
localparam SIZE_MASK = 16'hFFFE;          // Other bits represent size

// Group type enumeration
localparam ORPHAN_GROUP = 1;              // Orphan group
localparam NORMAL_GROUP = 0;              // Regular group

// Memory arrays for lookup table
(* ram_style = "distributed" *) reg [15:0] group_info [0:17]; // 3 groups * 6 words/group - 1
(* ram_style = "distributed" *) reg [15:0] delta_data [0:111]; // Complete delta data array

// Group parameter cache - Mixed signed and unsigned
reg [15:0] group_flags_size [0:NUM_GROUPS-1];
reg signed [15:0] group_base_b [0:NUM_GROUPS-1];     // Signed parameter
reg signed [15:0] group_base_c [0:NUM_GROUPS-1];     // Signed parameter
reg [15:0] group_offset [0:NUM_GROUPS-1];            // Unsigned index
reg [15:0] group_scale [0:NUM_GROUPS-1];             // Unsigned scale factor
reg [15:0] group_length [0:NUM_GROUPS-1];            // Unsigned length but 15-bit quantized
reg [0:0]  group_type [0:NUM_GROUPS-1];              // Type flag
reg [14:0] group_size [0:NUM_GROUPS-1];              // Unsigned size

// Pipeline stage 1 registers
reg signed [`INPUT_DATA_WIDTH-1:0] x_in_r1;
reg x_sign_r1;
reg [`INPUT_DATA_WIDTH-1:0] abs_x_r1;      // Unsigned absolute value
reg valid_r1;

// Pipeline stage 2 registers
reg signed [`INPUT_DATA_WIDTH-1:0] x_in_r2;
reg x_sign_r2;
reg [`INPUT_DATA_WIDTH-1:0] abs_x_r2;      // Unsigned absolute value
reg signed [15:0] slope_r2;                // Signed parameter
reg signed [15:0] intercept_r2;            // Signed parameter
reg valid_r2;
reg is_orphan_r2;
reg y_reflected_r2;

// Pipeline stage 3 registers
reg signed [`INPUT_DATA_WIDTH-1:0] x_in_r3;
reg x_sign_r3;
reg [`INPUT_DATA_WIDTH-1:0] abs_x_r3;      // Unsigned absolute value
reg signed [15:0] slope_r3;                // Signed parameter
reg signed [15:0] intercept_r3;            // Signed parameter
reg valid_r3;
reg is_orphan_r3;
reg y_reflected_r3;

// Additional variables for stage 2 calculation
reg matched;
reg [1:0] matched_group_idx;
reg [7:0] matched_delta_idx;
reg signed [15:0] delta_start, delta_end;  // Signed positions
reg signed [15:0] delta_slope, delta_intercept;  // Signed parameters
reg signed [15:0] delta_end_refl;          // Signed position
reg [15:0] refl_flags;
reg has_orphan_group;
reg [31:0] delta_base_idx;

// Additional variables for stage 3 calculation
reg signed [31:0] mul_result;
reg signed [15:0] scaled_result;
reg signed [15:0] final_result;

// Memory initialization
integer i, j;
initial begin
    // Initialize arrays
    for (i = 0; i < 18; i = i + 1) begin
        group_info[i] = 16'h0000;
    end
    for (i = 0; i < 112; i = i + 1) begin
        delta_data[i] = 16'h0000;
    end
    
    // Include hardcoded lookup table data
    `include "/vol/datastore/jmzhao/CompressedLUT/b-spline/testCPP/hardware/include/tanh_inline_lut_data.vh"
    
    // Extract group parameters to cache
    for (i = 0; i < NUM_GROUPS; i = i + 1) begin
        group_flags_size[i] = group_info[i*GROUP_WORDS + FLAGS_SIZE_IDX];
        group_base_b[i] = group_info[i*GROUP_WORDS + BASE_B_IDX];
        group_base_c[i] = group_info[i*GROUP_WORDS + BASE_C_IDX];
        group_offset[i] = group_info[i*GROUP_WORDS + OFFSET_IDX];
        group_scale[i] = group_info[i*GROUP_WORDS + SCALE_IDX];
        group_length[i] = group_info[i*GROUP_WORDS + GROUP_LENGTH_IDX];
        
        // Parse FLAGS_SIZE
        group_type[i] = (group_flags_size[i] & ORPHAN_FLAG_MASK) ? ORPHAN_GROUP : NORMAL_GROUP;
        group_size[i] = (group_flags_size[i] & SIZE_MASK) >> 1;
    end
end

//---------------------------------------------------------------------
// Stage 1: Input processing and sign extraction
//---------------------------------------------------------------------
always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        x_in_r1 <= 0;
        x_sign_r1 <= 1'b0;
        abs_x_r1 <= 0;
        valid_r1 <= 1'b0;
    end else begin
        valid_r1 <= valid_in;
        x_in_r1 <= x_in;
        
        // Calculate absolute value
        if (x_in[`INPUT_DATA_WIDTH-1]) begin
            x_sign_r1 <= 1'b1;
            abs_x_r1 <= -x_in;
        end else begin
            x_sign_r1 <= 1'b0;
            abs_x_r1 <= x_in;
        end
    end
end

//---------------------------------------------------------------------
// Stage 2: Interval matching and parameter selection
//---------------------------------------------------------------------
integer group_idx, interval_idx;

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        x_in_r2 <= 0;
        x_sign_r2 <= 1'b0;
        abs_x_r2 <= 0;
        slope_r2 <= 0;
        intercept_r2 <= 0;
        valid_r2 <= 1'b0;
        is_orphan_r2 <= 1'b0;
        y_reflected_r2 <= 1'b0;
    end else begin
        valid_r2 <= valid_r1;
        x_in_r2 <= x_in_r1;
        x_sign_r2 <= x_sign_r1;
        abs_x_r2 <= abs_x_r1;
        
        if (valid_r1) begin
            // Initialize search variables
            matched = 1'b0;
            is_orphan_r2 <= 1'b0;
            y_reflected_r2 <= 1'b0;
            
            // Step 1: Check all orphan group intervals
            for (group_idx = 0; group_idx < NUM_GROUPS; group_idx = group_idx + 1) begin
                if (!matched && group_type[group_idx] == ORPHAN_GROUP) begin
                    // Traverse all intervals in this orphan group
                    for (interval_idx = 0; interval_idx < 15; interval_idx = interval_idx + 1) begin
                        if (interval_idx < group_size[group_idx]) begin
                            // Calculate delta index
                            matched_delta_idx = group_offset[group_idx] + interval_idx[7:0];
                            delta_base_idx = {24'b0, matched_delta_idx} * DELTA_WORDS; // Ensure clean multiplication
                            
                            // Get interval start and end
                            delta_start = delta_data[delta_base_idx + DELTA_START_IDX];
                            delta_end_refl = delta_data[delta_base_idx + DELTA_END_FLAGS_IDX];
                            
                            // Check if input is in interval (using signed comparison for positions)
                            if ($signed(abs_x_r1) >= $signed(delta_start) && $signed(abs_x_r1) < $signed(delta_end_refl)) begin
                                matched = 1'b1;
                                matched_group_idx = group_idx[1:0];
                                is_orphan_r2 <= 1'b1;
                                
                                // For orphan groups, use interval parameters directly
                                slope_r2 <= delta_data[delta_base_idx + DELTA_SLOPE_IDX];
                                intercept_r2 <= delta_data[delta_base_idx + DELTA_INTERCEPT_IDX];
                                
                                // No reflection for orphan groups
                                y_reflected_r2 <= 1'b0;
                            end
                        end
                    end
                end
            end
            
            // Step 2: If no match in orphan groups, check regular groups
            if (!matched) begin
                for (group_idx = 0; group_idx < NUM_GROUPS; group_idx = group_idx + 1) begin
                    if (!matched && group_type[group_idx] == NORMAL_GROUP) begin
                        // Traverse all intervals in this regular group
                        for (interval_idx = 0; interval_idx < 15; interval_idx = interval_idx + 1) begin
                            if (interval_idx < group_size[group_idx]) begin
                                // Calculate delta index
                                matched_delta_idx = group_offset[group_idx] + interval_idx[7:0];
                                delta_base_idx = {24'b0, matched_delta_idx} * DELTA_WORDS; // Ensure clean multiplication
                                
                                // Get interval start
                                delta_start = delta_data[delta_base_idx + DELTA_START_IDX];
                                
                                // Calculate interval end
                                if (interval_idx == group_size[group_idx] - 1) begin
                                    // Last interval end = start + group length
                                    delta_end = $signed(delta_start) + group_length[group_idx];
                                end else begin
                                    // Non-last interval end = next interval start
                                    delta_end = delta_data[delta_base_idx + DELTA_WORDS + DELTA_START_IDX];
                                end
                                
                                // Check if input is in interval (using signed comparison for positions)
                                if ($signed(abs_x_r1) >= $signed(delta_start) && $signed(abs_x_r1) < $signed(delta_end)) begin
                                    matched = 1'b1;
                                    matched_group_idx = group_idx[1:0];
                                    
                                    // Get reflection flags
                                    refl_flags = delta_data[delta_base_idx + DELTA_END_FLAGS_IDX];
                                    y_reflected_r2 <= refl_flags[1]; // Y reflection flag - bit 1
                                    
                                    // Get delta parameters
                                    delta_slope = delta_data[delta_base_idx + DELTA_SLOPE_IDX];
                                    delta_intercept = delta_data[delta_base_idx + DELTA_INTERCEPT_IDX];
                                    
                                    // Calculate combined parameters (using signed operations)
                                    slope_r2 <= $signed(group_base_b[group_idx]) + $signed(delta_slope);
                                    intercept_r2 <= $signed(group_base_c[group_idx]) + $signed(delta_intercept);
                                end
                            end
                        end
                    end
                end
            end
            
            // Handle unmatched case (input outside all intervals)
            if (!matched) begin
                // Check if there's an orphan group
                has_orphan_group = 1'b0;
                
                for (group_idx = 0; group_idx < NUM_GROUPS; group_idx = group_idx + 1) begin
                    if (group_type[group_idx] == ORPHAN_GROUP && !has_orphan_group) begin
                        has_orphan_group = 1'b1;
                        is_orphan_r2 <= 1'b1;
                        
                        // Use first interval of first orphan group
                        matched_delta_idx = group_offset[group_idx];
                        delta_base_idx = {24'b0, matched_delta_idx} * DELTA_WORDS; // Ensure clean multiplication
                        
                        delta_start = delta_data[delta_base_idx + DELTA_START_IDX];
                        delta_slope = delta_data[delta_base_idx + DELTA_SLOPE_IDX];
                        delta_intercept = delta_data[delta_base_idx + DELTA_INTERCEPT_IDX];
                        
                        // Use interval parameters directly
                        slope_r2 <= delta_slope;
                        intercept_r2 <= delta_intercept;
                    end
                end
                
                // If no orphan group, use first regular group
                if (!has_orphan_group) begin
                    for (group_idx = 0; group_idx < NUM_GROUPS; group_idx = group_idx + 1) begin
                        if (group_type[group_idx] == NORMAL_GROUP && !matched) begin
                            matched = 1'b1; // Mark as matched to use only first group found
                            
                            // Use first interval of first regular group
                            matched_delta_idx = group_offset[group_idx];
                            delta_base_idx = {24'b0, matched_delta_idx} * DELTA_WORDS; // Ensure clean multiplication
                            
                            delta_start = delta_data[delta_base_idx + DELTA_START_IDX];
                            delta_slope = delta_data[delta_base_idx + DELTA_SLOPE_IDX];
                            delta_intercept = delta_data[delta_base_idx + DELTA_INTERCEPT_IDX];
                            
                            // Calculate regular group parameters
                            slope_r2 <= $signed(group_base_b[group_idx]) + $signed(delta_slope);
                            intercept_r2 <= $signed(group_base_c[group_idx]) + $signed(delta_intercept);
                        end
                    end
                end
            end
        end
    end
end

//---------------------------------------------------------------------
// Stage 3: Fixed-point calculation with 15-bit signed fixed-point
//---------------------------------------------------------------------
always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        x_in_r3 <= 0;
        x_sign_r3 <= 1'b0;
        abs_x_r3 <= 0;
        slope_r3 <= 0;
        intercept_r3 <= 0;
        valid_r3 <= 1'b0;
        is_orphan_r3 <= 1'b0;
        y_reflected_r3 <= 1'b0;
        y_out <= 0;
        valid_out <= 1'b0;
    end else begin
        valid_r3 <= valid_r2;
        x_in_r3 <= x_in_r2;
        x_sign_r3 <= x_sign_r2;
        abs_x_r3 <= abs_x_r2;
        slope_r3 <= slope_r2;
        intercept_r3 <= intercept_r2;
        is_orphan_r3 <= is_orphan_r2;
        y_reflected_r3 <= y_reflected_r2;
        
        valid_out <= valid_r3;
        
        if (valid_r3) begin
            // Main calculation: y = (slope * x) / SCALE_FACTOR + intercept
            // Step 1: Slope multiplied by x (signed slope * unsigned x)
            mul_result = $signed(slope_r3) * abs_x_r3;
            
            // Step 2: Right shift 15 bits (divide by 32768) - ARITHMETIC right shift
            scaled_result = (mul_result >>> SCALE_FACTOR_BITS) + $signed(intercept_r3);
            
            // Step 3: Apply Y reflection (if needed)
            if (y_reflected_r3) begin
                final_result = -$signed(scaled_result); // Signed negation
            end else begin
                final_result = scaled_result;
            end
            
            // Step 4: Apply input sign (for odd functions like tanh)
            if (x_sign_r3) begin
                y_out <= -$signed(final_result[`OUTPUT_DATA_WIDTH-1:0]);
            end else begin
                y_out <= final_result[`OUTPUT_DATA_WIDTH-1:0];
            end
        end
    end
end

endmodule
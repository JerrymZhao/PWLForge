//================================================================================
// pwl_hlut.v - Optimized piecewise linear approximation with FX32 format
//================================================================================

`include "data/pwl_config.vh"

module pwl_hlut (
    input clk,
    input rst_n,
    input signed [`INPUT_DATA_WIDTH-1:0] x_in,
    input valid_in,
    output in_ready,
    output reg signed [`OUTPUT_DATA_WIDTH-1:0] y_out,
    output reg valid_out
);

//---------------------------------------------------------------------
// Configuration parameters
//---------------------------------------------------------------------
localparam INPUT_WIDTH = `INPUT_DATA_WIDTH;
localparam OUTPUT_WIDTH = `OUTPUT_DATA_WIDTH;
localparam SCALE_FACTOR_BITS = `OPT_FRAC_BITS + 1;
localparam NUM_GROUPS = `OPT_NUM_GROUPS;
localparam MAX_INTERVALS_PER_GROUP = `OPT_MAX_INTERVALS_PER_GROUP;
localparam TOTAL_INTERVALS = `OPT_TOTAL_INTERVALS;

//---------------------------------------------------------------------
// Address widths
//---------------------------------------------------------------------
localparam GROUP_ADDR_WIDTH = `OPT_GROUP_ADDR_WIDTH;
localparam INTERVAL_ADDR_WIDTH = `OPT_INTERVAL_ADDR_WIDTH;
localparam DELTA_ADDR_WIDTH = `OPT_DELTA_ADDR_WIDTH;

//---------------------------------------------------------------------
// Optimized data format (FX32 uses 2 words per 32-bit value)
//---------------------------------------------------------------------
localparam GROUP_DATA_WORDS = 5;  // flags[1] + base_b[2] + base_c[2]
localparam DELTA_DATA_WORDS = 5;  // delta_b[2] + delta_c[2] + flags[1]
localparam INTERVAL_META_WORDS = 5; // start[2] + end[2] + group_id[1]
localparam GROUP_BOUNDS_WORDS = 4;  // min[2] + max[2]

// Group info field indices (5 words for FX32)
localparam FLAGS_IDX = 0;
localparam BASE_B_LOW_IDX = 1;
localparam BASE_B_HIGH_IDX = 2;
localparam BASE_C_LOW_IDX = 3;
localparam BASE_C_HIGH_IDX = 4;

// Delta data field indices (5 words for FX32)
localparam DELTA_B_LOW_IDX = 0;
localparam DELTA_B_HIGH_IDX = 1;
localparam DELTA_C_LOW_IDX = 2;
localparam DELTA_C_HIGH_IDX = 3;
localparam DELTA_FLAGS_IDX = 4;

//---------------------------------------------------------------------
// Memory sizing
//---------------------------------------------------------------------
localparam GROUP_MEMORY_SIZE = `OPT_GROUP_INFO_SIZE;
localparam DELTA_MEMORY_SIZE = `OPT_DELTA_DATA_SIZE;
localparam INTERVAL_METADATA_SIZE = `OPT_INTERVAL_METADATA_SIZE;
localparam GROUP_BOUNDS_SIZE = `OPT_GROUP_BOUNDS_SIZE;

//---------------------------------------------------------------------
// Always ready (no backpressure)
//---------------------------------------------------------------------
assign in_ready = 1'b1;

//---------------------------------------------------------------------
// ROM storage
//---------------------------------------------------------------------
(* rom_style = "distributed" *) reg [15:0] group_info [0:GROUP_MEMORY_SIZE-1];
(* rom_style = "distributed" *) reg [15:0] delta_data [0:DELTA_MEMORY_SIZE-1];
(* rom_style = "distributed" *) reg [15:0] interval_metadata [0:INTERVAL_METADATA_SIZE-1];
(* rom_style = "distributed" *) reg [15:0] group_bounds [0:GROUP_BOUNDS_SIZE-1];
(* rom_style = "distributed" *) reg [GROUP_ADDR_WIDTH-1:0] group_storage_map [0:NUM_GROUPS-1];

//---------------------------------------------------------------------
// Pre-computed interval information (32-bit values)
//---------------------------------------------------------------------
wire [31:0] interval_start [0:TOTAL_INTERVALS-1];
wire [31:0] interval_end [0:TOTAL_INTERVALS-1];
wire [GROUP_ADDR_WIDTH-1:0] interval_to_group [0:TOTAL_INTERVALS-1];

//---------------------------------------------------------------------
// Pre-computed group range information (32-bit values)
//---------------------------------------------------------------------
wire [31:0] group_min_x [0:NUM_GROUPS-1];
wire [31:0] group_max_x [0:NUM_GROUPS-1];

//---------------------------------------------------------------------
// Generate blocks for metadata unpacking
//---------------------------------------------------------------------
genvar gen_i;
generate
    for (gen_i = 0; gen_i < TOTAL_INTERVALS; gen_i = gen_i + 1) begin : gen_intervals
        assign interval_start[gen_i] = {interval_metadata[gen_i * INTERVAL_META_WORDS + 1], 
                                        interval_metadata[gen_i * INTERVAL_META_WORDS + 0]};
        assign interval_end[gen_i] = {interval_metadata[gen_i * INTERVAL_META_WORDS + 3], 
                                      interval_metadata[gen_i * INTERVAL_META_WORDS + 2]};
        assign interval_to_group[gen_i] = interval_metadata[gen_i * INTERVAL_META_WORDS + 4][GROUP_ADDR_WIDTH-1:0];
    end
    
    for (gen_i = 0; gen_i < NUM_GROUPS; gen_i = gen_i + 1) begin : gen_groups
        assign group_min_x[gen_i] = {group_bounds[gen_i * GROUP_BOUNDS_WORDS + 1], 
                                     group_bounds[gen_i * GROUP_BOUNDS_WORDS + 0]};
        assign group_max_x[gen_i] = {group_bounds[gen_i * GROUP_BOUNDS_WORDS + 3], 
                                     group_bounds[gen_i * GROUP_BOUNDS_WORDS + 2]};
    end
endgenerate

//---------------------------------------------------------------------
// Initialize memories from files
//---------------------------------------------------------------------
initial begin
    $readmemh("data/group_info.mem", group_info);
    $readmemh("data/delta_data.mem", delta_data);
    $readmemh("data/interval_metadata.mem", interval_metadata);
    $readmemh("data/group_bounds.mem", group_bounds);
    $readmemh("data/group_map.mem", group_storage_map);
end

//---------------------------------------------------------------------
// Stage 1: Sign extraction and absolute value
//---------------------------------------------------------------------
reg x_sign_r1;
reg [INPUT_WIDTH-1:0] abs_x_r1;
reg valid_r1;

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        x_sign_r1 <= 1'b0;
        abs_x_r1 <= 0;
        valid_r1 <= 1'b0;
    end else begin
        valid_r1 <= valid_in;
        if (valid_in) begin
            x_sign_r1 <= x_in[INPUT_WIDTH-1];
            abs_x_r1 <= x_in[INPUT_WIDTH-1] ? -x_in : x_in;
        end
    end
end

//---------------------------------------------------------------------
// Stage 2: Two-level interval search
//---------------------------------------------------------------------

// First level: Find candidate groups
reg [NUM_GROUPS-1:0] group_candidates;
integer gidx;

always @(*) begin
    group_candidates = {NUM_GROUPS{1'b0}};
    for (gidx = 0; gidx < NUM_GROUPS; gidx = gidx + 1) begin
        if ($signed(abs_x_r1) >= $signed(group_min_x[gidx]) && $signed(abs_x_r1) < $signed(group_max_x[gidx])) begin
            group_candidates[gidx] = 1'b1;
        end
    end
end

// Second level: Search within candidate groups
reg [DELTA_ADDR_WIDTH-1:0] found_interval;
reg match_found;
integer idx;
integer grp;

always @(*) begin
    found_interval = {DELTA_ADDR_WIDTH{1'b0}};
    match_found = 1'b0;
    
    for (grp = 0; grp < NUM_GROUPS; grp = grp + 1) begin
        if (group_candidates[grp] && !match_found) begin
            for (idx = 0; idx < TOTAL_INTERVALS; idx = idx + 1) begin
                if (!match_found && interval_to_group[idx] == grp[GROUP_ADDR_WIDTH-1:0] &&
                    $signed(abs_x_r1) >= $signed(interval_start[idx]) && 
                    $signed(abs_x_r1) < $signed(interval_end[idx])) begin
                    found_interval = idx[DELTA_ADDR_WIDTH-1:0];
                    match_found = 1'b1;
                end
            end
        end
    end
end

//---------------------------------------------------------------------
// Pipeline registers Stage 2
//---------------------------------------------------------------------
reg x_sign_r2;
reg [INPUT_WIDTH-1:0] abs_x_r2;
reg valid_r2;
reg [GROUP_ADDR_WIDTH-1:0] group_id_r2;
reg is_orphan_r2;
reg [DELTA_ADDR_WIDTH-1:0] delta_idx_r2;
reg signed [31:0] base_b_r2;
reg signed [31:0] base_c_r2;
reg interval_found_r2;

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        x_sign_r2 <= 1'b0;
        abs_x_r2 <= 0;
        valid_r2 <= 1'b0;
        group_id_r2 <= 0;
        is_orphan_r2 <= 1'b0;
        delta_idx_r2 <= 0;
        base_b_r2 <= 0;
        base_c_r2 <= 0;
        interval_found_r2 <= 1'b0;
    end else begin
        x_sign_r2 <= x_sign_r1;
        abs_x_r2 <= abs_x_r1;
        valid_r2 <= valid_r1;
        interval_found_r2 <= match_found;
        
        if (valid_r1 && match_found) begin
            group_id_r2 <= interval_to_group[found_interval];
            delta_idx_r2 <= found_interval;
            
            // Look up group info with storage index mapping
            begin : group_lookup
                integer grp_base;
                reg [15:0] flags;
                reg [GROUP_ADDR_WIDTH-1:0] storage_idx;
                
                storage_idx = group_storage_map[interval_to_group[found_interval]];
                grp_base = storage_idx * GROUP_DATA_WORDS;
                flags = group_info[grp_base + FLAGS_IDX];
                
                is_orphan_r2 <= flags[0];
                base_b_r2 <= $signed({group_info[grp_base + BASE_B_HIGH_IDX], 
                                     group_info[grp_base + BASE_B_LOW_IDX]});
                base_c_r2 <= $signed({group_info[grp_base + BASE_C_HIGH_IDX], 
                                     group_info[grp_base + BASE_C_LOW_IDX]});
            end
        end else if (valid_r1) begin
            // No match found - use default values
            is_orphan_r2 <= 1'b0;
            base_b_r2 <= 0;
            base_c_r2 <= 0;
        end
    end
end

//---------------------------------------------------------------------
// Stage 3: Parameter lookup and reconstruction
//---------------------------------------------------------------------
reg x_sign_r3;
reg [INPUT_WIDTH-1:0] abs_x_r3;
reg valid_r3;
reg signed [31:0] slope_r3;
reg signed [31:0] intercept_r3;
reg reflection_x_r3;
reg reflection_y_r3;

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        x_sign_r3 <= 1'b0;
        abs_x_r3 <= 0;
        valid_r3 <= 1'b0;
        slope_r3 <= 0;
        intercept_r3 <= 0;
        reflection_x_r3 <= 1'b0;
        reflection_y_r3 <= 1'b0;
    end else begin
        x_sign_r3 <= x_sign_r2;
        abs_x_r3 <= abs_x_r2;
        valid_r3 <= valid_r2;
        
        if (valid_r2 && interval_found_r2) begin
            begin : delta_lookup
                integer delta_base;
                reg [15:0] reflection_word;
                reg signed [31:0] delta_b;
                reg signed [31:0] delta_c;
                
                delta_base = delta_idx_r2 * DELTA_DATA_WORDS;
                reflection_word = delta_data[delta_base + DELTA_FLAGS_IDX];
                delta_b = $signed({delta_data[delta_base + DELTA_B_HIGH_IDX], 
                                  delta_data[delta_base + DELTA_B_LOW_IDX]});
                delta_c = $signed({delta_data[delta_base + DELTA_C_HIGH_IDX], 
                                  delta_data[delta_base + DELTA_C_LOW_IDX]});
                
                if (is_orphan_r2) begin
                    slope_r3 <= delta_b;
                    intercept_r3 <= delta_c;
                    reflection_x_r3 <= 1'b0;
                    reflection_y_r3 <= 1'b0;
                end else begin
                    slope_r3 <= base_b_r2 + delta_b;
                    intercept_r3 <= base_c_r2 + delta_c;
                    reflection_x_r3 <= reflection_word[0];
                    reflection_y_r3 <= reflection_word[1];
                end
            end
        end else begin
            slope_r3 <= 0;
            intercept_r3 <= 0;
            reflection_x_r3 <= 1'b0;
            reflection_y_r3 <= 1'b0;
        end
    end
end

//---------------------------------------------------------------------
// Stage 4: Multiplication (32x32 = 64-bit result)
//---------------------------------------------------------------------
reg x_sign_r4;
reg valid_r4;
reg reflection_y_r4;
reg signed [31:0] intercept_r4;
(* use_dsp = "no" *) reg signed [63:0] mul_result_r4;

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        x_sign_r4 <= 1'b0;
        valid_r4 <= 1'b0;
        reflection_y_r4 <= 1'b0;
        intercept_r4 <= 0;
        mul_result_r4 <= 0;
    end else begin
        x_sign_r4 <= x_sign_r3;
        valid_r4 <= valid_r3;
        reflection_y_r4 <= reflection_y_r3;
        intercept_r4 <= intercept_r3;
        
        if (valid_r3) begin
            if (reflection_x_r3) begin
                mul_result_r4 <= $signed(slope_r3) * $signed({1'b0, 32'hFFFFFFFF - abs_x_r3[31:0]});
            end else begin
                mul_result_r4 <= $signed(slope_r3) * $signed({1'b0, abs_x_r3[31:0]});
            end
        end
    end
end

//---------------------------------------------------------------------
// Stage 5: Final computation
//---------------------------------------------------------------------
reg x_sign_r5;
reg valid_r5;
reg reflection_y_r5;
reg signed [OUTPUT_WIDTH-1:0] result_r5;

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        x_sign_r5 <= 1'b0;
        valid_r5 <= 1'b0;
        reflection_y_r5 <= 1'b0;
        result_r5 <= 0;
    end else begin
        x_sign_r5 <= x_sign_r4;
        valid_r5 <= valid_r4;
        reflection_y_r5 <= reflection_y_r4;
        
        if (valid_r4) begin
            result_r5 <= (mul_result_r4 >>> SCALE_FACTOR_BITS) + intercept_r4;
        end
    end
end

//---------------------------------------------------------------------
// Output Stage: Apply reflections
//---------------------------------------------------------------------
wire signed [OUTPUT_WIDTH-1:0] reflected_result = reflection_y_r5 ? -result_r5 : result_r5;

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        y_out <= 0;
        valid_out <= 1'b0;
    end else begin
        valid_out <= valid_r5;
        
        if (valid_r5) begin
            y_out <= x_sign_r5 ? -reflected_result : reflected_result;
        end
    end
end

endmodule

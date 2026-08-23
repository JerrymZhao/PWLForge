`timescale 1ns / 1ps

// Compile from an example directory containing data/pwl_config.vh and .mem files.
`include "data/pwl_config.vh"
`include "fp_config.vh"

module pwl_hlut_fp16 (
    input wire aclk,
    input wire aresetn,
    
    input wire [15:0] s_axis_x_tdata,
    input wire s_axis_x_tvalid,
    output wire s_axis_x_tready,
    
    output wire [15:0] m_axis_y_tdata,
    output wire m_axis_y_tvalid,
    input wire m_axis_y_tready
);

    localparam DATA_WIDTH = `FP_DATA_WIDTH;
    localparam METADATA_WIDTH = `FP_METADATA_WIDTH;
    localparam MULT_LATENCY = `FP_MULT_LATENCY;
    localparam ADD_LATENCY = `FP_ADD_LATENCY;
    
    localparam NUM_GROUPS = `OPT_NUM_GROUPS;
    localparam TOTAL_INTERVALS = `OPT_TOTAL_INTERVALS;
    localparam GROUP_ADDR_WIDTH = `OPT_GROUP_ADDR_WIDTH;
    localparam DELTA_ADDR_WIDTH = `OPT_DELTA_ADDR_WIDTH;
    
    localparam INTERVAL_METADATA_SIZE = `OPT_INTERVAL_METADATA_SIZE;
    localparam GROUP_BOUNDS_SIZE = `OPT_GROUP_BOUNDS_SIZE;
    localparam GROUP_MAP_SIZE = `OPT_GROUP_MAP_SIZE;
    localparam GROUP_INFO_SIZE = `OPT_GROUP_INFO_SIZE;
    localparam DELTA_DATA_SIZE = `OPT_DELTA_DATA_SIZE;
    
    // FP16: Each entry is 16 bits (one FP16 value per memory location)
    (* rom_style = "distributed" *) reg [15:0] interval_metadata [0:INTERVAL_METADATA_SIZE-1];
    (* rom_style = "distributed" *) reg [15:0] group_bounds [0:GROUP_BOUNDS_SIZE-1];
    (* rom_style = "distributed" *) reg [15:0] group_map [0:GROUP_MAP_SIZE-1];
    (* rom_style = "distributed" *) reg [15:0] group_info [0:GROUP_INFO_SIZE-1];
    (* rom_style = "distributed" *) reg [15:0] delta_data [0:DELTA_DATA_SIZE-1];
    
    initial begin
        $readmemh("data/interval_metadata.mem", interval_metadata);
        $readmemh("data/group_bounds.mem", group_bounds);
        $readmemh("data/group_map.mem", group_map);
        $readmemh("data/group_info.mem", group_info);
        $readmemh("data/delta_data.mem", delta_data);
    end
    
    // FP16: No need to combine - direct 16-bit access
    function get_sign;
        input [15:0] fp;
        begin
            get_sign = fp[15];
        end
    endfunction
    
    function [15:0] negate_fp16;
        input [15:0] fp;
        begin
            negate_fp16 = {~fp[15], fp[14:0]};
        end
    endfunction
    
    function [15:0] abs_fp16;
        input [15:0] fp;
        begin
            abs_fp16 = {1'b0, fp[14:0]};
        end
    endfunction
    
    // FP16: Direct assignment, 3 entries per interval (start, end, group_id)
    wire [15:0] interval_start [0:TOTAL_INTERVALS-1];
    wire [15:0] interval_end [0:TOTAL_INTERVALS-1];
    wire [GROUP_ADDR_WIDTH-1:0] interval_to_group [0:TOTAL_INTERVALS-1];
    
    genvar gi;
    generate
        for (gi = 0; gi < TOTAL_INTERVALS; gi = gi + 1) begin : gen_intervals
            localparam BASE = gi * 3;
            assign interval_start[gi] = interval_metadata[BASE+0];
            assign interval_end[gi] = interval_metadata[BASE+1];
            assign interval_to_group[gi] = interval_metadata[BASE+2][GROUP_ADDR_WIDTH-1:0];
        end
    endgenerate
    
    wire [GROUP_ADDR_WIDTH-1:0] group_storage_map [0:NUM_GROUPS-1];
    generate
        for (gi = 0; gi < NUM_GROUPS; gi = gi + 1) begin : gen_map
            assign group_storage_map[gi] = group_map[gi][GROUP_ADDR_WIDTH-1:0];
        end
    endgenerate
    
    //==========================================================================
    // Stage 0: Sign extraction
    //==========================================================================
    wire x_sign_s0;
    wire [15:0] abs_x_s0;
    wire valid_s0;
    
    assign x_sign_s0 = get_sign(s_axis_x_tdata);
    assign abs_x_s0 = abs_fp16(s_axis_x_tdata);
    assign valid_s0 = s_axis_x_tvalid;
    assign s_axis_x_tready = 1'b1;
    
    //==========================================================================
    // Stage 1-2: Comparators (2 cycle latency)
    //==========================================================================
    wire [TOTAL_INTERVALS-1:0] ge_start, lt_end;
    wire [TOTAL_INTERVALS-1:0] ge_start_valid, lt_end_valid;

    generate
        for (gi = 0; gi < TOTAL_INTERVALS; gi = gi + 1) begin : gen_cmp
            fp_compare_ge_wrapper #(.DATA_WIDTH(16), .METADATA_WIDTH(16)) cmp_start (
                .aclk(aclk), .aresetn(aresetn),
                .s_axis_a_tdata(abs_x_s0), .s_axis_a_tvalid(valid_s0), .s_axis_a_tready(),
                .s_axis_b_tdata(interval_start[gi]), .s_axis_b_tvalid(valid_s0), .s_axis_b_tready(),
                .m_axis_result_tdata(ge_start[gi]), .m_axis_result_tdata_full(),
                .m_axis_result_tvalid(ge_start_valid[gi]), .m_axis_result_tready(1'b1),
                .s_axis_metadata_tdata(16'b0), .s_axis_metadata_tvalid(1'b0), .s_axis_metadata_tready(),
                .m_axis_metadata_tdata(), .m_axis_metadata_tvalid(), .m_axis_metadata_tready(1'b1)
            );
            
            fp_compare_lt_wrapper #(.DATA_WIDTH(16), .METADATA_WIDTH(16)) cmp_end (
                .aclk(aclk), .aresetn(aresetn),
                .s_axis_a_tdata(abs_x_s0), .s_axis_a_tvalid(valid_s0), .s_axis_a_tready(),
                .s_axis_b_tdata(interval_end[gi]), .s_axis_b_tvalid(valid_s0), .s_axis_b_tready(),
                .m_axis_result_tdata(lt_end[gi]), .m_axis_result_tdata_full(),
                .m_axis_result_tvalid(lt_end_valid[gi]), .m_axis_result_tready(1'b1),
                .s_axis_metadata_tdata(16'b0), .s_axis_metadata_tvalid(1'b0), .s_axis_metadata_tready(),
                .m_axis_metadata_tdata(), .m_axis_metadata_tvalid(), .m_axis_metadata_tready(1'b1)
            );
        end
    endgenerate
    
    wire [15:0] abs_x_s2;
    wire x_sign_s2;
    wire valid_s2;
    
    simple_delay_line #(.DATA_WIDTH(16), .DEPTH(2)) delay_abs_x (
        .clk(aclk), .rst_n(aresetn), .data_in(abs_x_s0), .data_out(abs_x_s2));
    
    simple_delay_line #(.DATA_WIDTH(1), .DEPTH(2)) delay_x_sign (
        .clk(aclk), .rst_n(aresetn), .data_in(x_sign_s0), .data_out(x_sign_s2));
    
    valid_delay_line #(.DEPTH(2)) delay_valid (
        .clk(aclk), .rst_n(aresetn), .valid_in(valid_s0), .valid_out(valid_s2));
    
    //==========================================================================
    // Stage 2-3: Interval matching
    //==========================================================================
    reg match_found_comb;
    reg [DELTA_ADDR_WIDTH-1:0] found_interval_comb;
    integer ii;

    always @(*) begin
        match_found_comb = 1'b0;
        found_interval_comb = {DELTA_ADDR_WIDTH{1'b0}};
        
        if (valid_s2 && (&ge_start_valid) && (&lt_end_valid)) begin
            for (ii = 0; ii < TOTAL_INTERVALS; ii = ii + 1) begin
                if (!match_found_comb && ge_start[ii] && lt_end[ii]) begin
                    found_interval_comb = ii[DELTA_ADDR_WIDTH-1:0];
                    match_found_comb = 1'b1;
                end
            end
        end
    end

    reg [DELTA_ADDR_WIDTH-1:0] found_interval_s3;
    reg match_found_s3;
    reg x_sign_s3;
    reg [15:0] abs_x_s3;
    reg valid_s3;

    always @(posedge aclk or negedge aresetn) begin
        if (!aresetn) begin
            found_interval_s3 <= {DELTA_ADDR_WIDTH{1'b0}};
            match_found_s3 <= 1'b0;
            x_sign_s3 <= 1'b0;
            abs_x_s3 <= 16'b0;
            valid_s3 <= 1'b0;
        end else begin
            x_sign_s3 <= x_sign_s2;
            abs_x_s3 <= abs_x_s2;
            valid_s3 <= valid_s2;
            found_interval_s3 <= found_interval_comb;
            match_found_s3 <= match_found_comb;
        end
    end

    //==========================================================================
    // Stage 3-4: Parameter lookup
    //==========================================================================
    
    // FP16: 3 entries per group (flags, b, c)
    // FP16: 3 entries per delta (delta_b, delta_c, delta_flags)
    integer grp_base, delta_base;
    reg [GROUP_ADDR_WIDTH-1:0] storage_idx;
    reg [15:0] flags, delta_flags;
    reg [15:0] base_b_comb, base_c_comb, delta_b_comb, delta_c_comb;
    reg is_orphan_comb, reflection_x_comb, reflection_y_comb;
    
    always @(*) begin
        // Default values for no-match case
        base_b_comb = 16'h0000;      // slope = 0
        base_c_comb = 16'h0000;      // intercept = 0
        delta_b_comb = 16'h0000;
        delta_c_comb = 16'h0000;
        is_orphan_comb = 1'b1;       // treat as orphan
        reflection_x_comb = 1'b0;
        reflection_y_comb = 1'b0;
        
        if (match_found_s3) begin
            storage_idx = group_storage_map[interval_to_group[found_interval_s3]];
            grp_base = storage_idx * 3;
            delta_base = found_interval_s3 * 3;
            
            flags = group_info[grp_base + 0];
            is_orphan_comb = flags[0];
            
            base_b_comb = group_info[grp_base + 1];
            base_c_comb = group_info[grp_base + 2];
            delta_b_comb = delta_data[delta_base + 0];
            delta_c_comb = delta_data[delta_base + 1];
            
            delta_flags = delta_data[delta_base + 2];
            reflection_x_comb = delta_flags[0];
            reflection_y_comb = delta_flags[1];
        end
    end
    
    // Stage 4 registers
    reg x_sign_s4;
    reg [15:0] abs_x_s4;
    reg valid_s4;
    reg [15:0] base_b_s4, base_c_s4, delta_b_s4, delta_c_s4;
    reg is_orphan_s4;
    reg reflection_x_s4, reflection_y_s4;

    always @(posedge aclk or negedge aresetn) begin
        if (!aresetn) begin
            x_sign_s4 <= 1'b0;
            abs_x_s4 <= 16'b0;
            valid_s4 <= 1'b0;
            base_b_s4 <= 16'b0;
            base_c_s4 <= 16'b0;
            delta_b_s4 <= 16'b0;
            delta_c_s4 <= 16'b0;
            is_orphan_s4 <= 1'b0;
            reflection_x_s4 <= 1'b0;
            reflection_y_s4 <= 1'b0;
        end else begin
            x_sign_s4 <= x_sign_s3;
            abs_x_s4 <= abs_x_s3;
            valid_s4 <= valid_s3;
            
            base_b_s4 <= base_b_comb;
            base_c_s4 <= base_c_comb;
            delta_b_s4 <= delta_b_comb;
            delta_c_s4 <= delta_c_comb;
            is_orphan_s4 <= is_orphan_comb;
            reflection_x_s4 <= reflection_x_comb;
            reflection_y_s4 <= reflection_y_comb;
        end
    end

    //==========================================================================
    // Stage 4-5: Reconstruct slope and intercept (adders)
    //==========================================================================
    localparam RECON_META_WIDTH = 20;  // 16-bit abs_x + 4 flags
    wire [RECON_META_WIDTH-1:0] recon_meta_in;
    assign recon_meta_in = {abs_x_s4, x_sign_s4, reflection_x_s4, reflection_y_s4, 1'b0};

    wire [15:0] slope_a_input = is_orphan_s4 ? delta_b_s4 : base_b_s4;
    wire [15:0] slope_b_input = is_orphan_s4 ? 16'h0000 : delta_b_s4;
    wire [15:0] intercept_a_input = is_orphan_s4 ? delta_c_s4 : base_c_s4;
    wire [15:0] intercept_b_input = is_orphan_s4 ? 16'h0000 : delta_c_s4;

    wire [15:0] slope_recon, intercept_recon;
    wire slope_recon_valid, intercept_recon_valid;
    wire [RECON_META_WIDTH-1:0] slope_meta_out, intercept_meta_out;

    fp_add #(.DATA_WIDTH(16), .METADATA_WIDTH(RECON_META_WIDTH)) slope_adder (
        .aclk(aclk), .aresetn(aresetn),
        .s_axis_a_tdata(slope_a_input), .s_axis_a_tvalid(valid_s4), .s_axis_a_tready(),
        .s_axis_b_tdata(slope_b_input), .s_axis_b_tvalid(valid_s4), .s_axis_b_tready(),
        .m_axis_result_tdata(slope_recon), .m_axis_result_tvalid(slope_recon_valid), .m_axis_result_tready(1'b1),
        .s_axis_metadata_tdata(recon_meta_in), .s_axis_metadata_tvalid(valid_s4), .s_axis_metadata_tready(),
        .m_axis_metadata_tdata(slope_meta_out), .m_axis_metadata_tvalid(), .m_axis_metadata_tready(1'b1)
    );

    fp_add #(.DATA_WIDTH(16), .METADATA_WIDTH(RECON_META_WIDTH)) intercept_adder (
        .aclk(aclk), .aresetn(aresetn),
        .s_axis_a_tdata(intercept_a_input), .s_axis_a_tvalid(valid_s4), .s_axis_a_tready(),
        .s_axis_b_tdata(intercept_b_input), .s_axis_b_tvalid(valid_s4), .s_axis_b_tready(),
        .m_axis_result_tdata(intercept_recon), .m_axis_result_tvalid(intercept_recon_valid), .m_axis_result_tready(1'b1),
        .s_axis_metadata_tdata(recon_meta_in), .s_axis_metadata_tvalid(valid_s4), .s_axis_metadata_tready(),
        .m_axis_metadata_tdata(intercept_meta_out), .m_axis_metadata_tvalid(), .m_axis_metadata_tready(1'b1)
    );

    //==========================================================================
    // Stage 5: Register adder outputs
    //==========================================================================
    reg [15:0] slope_s5, intercept_s5, abs_x_s5;
    reg x_sign_s5, reflection_x_s5, reflection_y_s5;
    reg valid_s5;

    always @(posedge aclk or negedge aresetn) begin
        if (!aresetn) begin
            slope_s5 <= 16'b0;
            intercept_s5 <= 16'b0;
            abs_x_s5 <= 16'b0;
            x_sign_s5 <= 1'b0;
            reflection_x_s5 <= 1'b0;
            reflection_y_s5 <= 1'b0;
            valid_s5 <= 1'b0;
        end else begin
            slope_s5 <= slope_recon;
            intercept_s5 <= intercept_recon;
            abs_x_s5 <= slope_meta_out[19:4];
            x_sign_s5 <= slope_meta_out[3];
            reflection_x_s5 <= slope_meta_out[2];
            reflection_y_s5 <= slope_meta_out[1];
            valid_s5 <= slope_recon_valid;
        end
    end
    
    //==========================================================================
    // Stage 6-7: Multiply
    //==========================================================================
    wire [15:0] x_for_mult;
    assign x_for_mult = reflection_x_s5 ? negate_fp16(abs_x_s5) : abs_x_s5;
    
    localparam MULT_META_WIDTH = 18;  // 16-bit intercept + 2 flags
    wire [MULT_META_WIDTH-1:0] mult_meta_in;
    assign mult_meta_in = {intercept_s5, x_sign_s5, reflection_y_s5};
    
    wire [15:0] mult_result;
    wire mult_valid;
    wire [MULT_META_WIDTH-1:0] mult_meta_out;

    fp_mult #(.DATA_WIDTH(16), .METADATA_WIDTH(MULT_META_WIDTH)) multiplier (
        .aclk(aclk), .aresetn(aresetn),
        .s_axis_a_tdata(slope_s5), .s_axis_a_tvalid(valid_s5), .s_axis_a_tready(),
        .s_axis_b_tdata(x_for_mult), .s_axis_b_tvalid(valid_s5), .s_axis_b_tready(),
        .m_axis_result_tdata(mult_result), .m_axis_result_tvalid(mult_valid), .m_axis_result_tready(1'b1),
        .s_axis_metadata_tdata(mult_meta_in), .s_axis_metadata_tvalid(valid_s5), .s_axis_metadata_tready(),
        .m_axis_metadata_tdata(mult_meta_out), .m_axis_metadata_tvalid(), .m_axis_metadata_tready(1'b1)
    );
    
    //==========================================================================
    // Stage 8-9: Final addition
    //==========================================================================
    localparam FINAL_META_WIDTH = 2;
    wire [FINAL_META_WIDTH-1:0] final_meta_in;
    assign final_meta_in = {mult_meta_out[1], mult_meta_out[0]};
    
    wire [15:0] add_result;
    wire add_valid;
    wire [FINAL_META_WIDTH-1:0] final_meta_out;

    fp_add #(.DATA_WIDTH(16), .METADATA_WIDTH(FINAL_META_WIDTH)) final_adder (
        .aclk(aclk), .aresetn(aresetn),
        .s_axis_a_tdata(mult_result), .s_axis_a_tvalid(mult_valid), .s_axis_a_tready(),
        .s_axis_b_tdata(mult_meta_out[17:2]), .s_axis_b_tvalid(mult_valid), .s_axis_b_tready(),
        .m_axis_result_tdata(add_result), .m_axis_result_tvalid(add_valid), .m_axis_result_tready(1'b1),
        .s_axis_metadata_tdata(final_meta_in), .s_axis_metadata_tvalid(mult_valid), .s_axis_metadata_tready(),
        .m_axis_metadata_tdata(final_meta_out), .m_axis_metadata_tvalid(), .m_axis_metadata_tready(1'b1)
    );
    
    //==========================================================================
    // Output reflection
    //==========================================================================
    wire [15:0] reflected_y, final_y;
    
    assign reflected_y = final_meta_out[0] ? negate_fp16(add_result) : add_result;
    assign final_y = final_meta_out[1] ? negate_fp16(reflected_y) : reflected_y;
    
    assign m_axis_y_tdata = final_y;
    assign m_axis_y_tvalid = add_valid;

endmodule

//==============================================================================
// Helper modules - IDENTICAL TO FP32 VERSION
//==============================================================================
module simple_delay_line #(parameter DATA_WIDTH = 16, parameter DEPTH = 10)(
    input wire clk, input wire rst_n,
    input wire [DATA_WIDTH-1:0] data_in,
    output wire [DATA_WIDTH-1:0] data_out
);
    generate
        if (DEPTH == 0) begin : gen_no_delay
            assign data_out = data_in;
        end else if (DEPTH == 1) begin : gen_single_reg
            reg [DATA_WIDTH-1:0] delay_reg;
            always @(posedge clk or negedge rst_n) begin
                if (!rst_n) delay_reg <= {DATA_WIDTH{1'b0}};
                else delay_reg <= data_in;
            end
            assign data_out = delay_reg;
        end else begin : gen_shift_register
            reg [DATA_WIDTH-1:0] shift_reg [0:DEPTH-1];
            integer i;
            always @(posedge clk or negedge rst_n) begin
                if (!rst_n) begin
                    for (i = 0; i < DEPTH; i = i + 1)
                        shift_reg[i] <= {DATA_WIDTH{1'b0}};
                end else begin
                    shift_reg[0] <= data_in;
                    for (i = 1; i < DEPTH; i = i + 1)
                        shift_reg[i] <= shift_reg[i-1];
                end
            end
            assign data_out = shift_reg[DEPTH-1];
        end
    endgenerate
endmodule

module valid_delay_line #(parameter DEPTH = 10)(
    input wire clk, input wire rst_n,
    input wire valid_in, output wire valid_out
);
    generate
        if (DEPTH == 0) begin : gen_no_delay
            assign valid_out = valid_in;
        end else if (DEPTH == 1) begin : gen_single_reg
            reg valid_reg;
            always @(posedge clk or negedge rst_n) begin
                if (!rst_n) valid_reg <= 1'b0;
                else valid_reg <= valid_in;
            end
            assign valid_out = valid_reg;
        end else begin : gen_shift_register
            reg [DEPTH-1:0] valid_shift_reg;
            always @(posedge clk or negedge rst_n) begin
                if (!rst_n) valid_shift_reg <= {DEPTH{1'b0}};
                else valid_shift_reg <= {valid_shift_reg[DEPTH-2:0], valid_in};
            end
            assign valid_out = valid_shift_reg[DEPTH-1];
        end
    endgenerate
endmodule

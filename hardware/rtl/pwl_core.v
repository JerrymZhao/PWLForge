//========================================================================
// pwl_core.v - Piecewise Linear Approximation Core
//========================================================================
`timescale 1ns/1ps
// Make sure the path to tanh_config.vh is correct for your environment
`include "/vol/datastore/jmzhao/CompressedLUT/b-spline/testCPP/results/tanh/tanh_config.vh"

module pwl_core (
    input  wire                        clk,
    input  wire                        rst_n,
    input  wire [15:0]                 x_in,
    input  wire                        x_valid,
    output wire [15:0]                 y_out,
    output wire                        y_valid
);

    // Internal signals for connecting modules
    wire [`PWL_ADDR_WIDTH-1:0] segment_idx;
    wire addr_valid;
    wire [15:0] breakpoint_data, slope_data, intercept_data;
    wire memory_data_valid;

    // Pipelined control signals and input data
    reg  x_valid_r;
    reg  [15:0] x_in_r;
    reg  [15:0] x_in_r2; // Added register stage 2 for pipeline matching
    reg  [15:0] x_in_r3; // Added register stage 3 for pipeline matching

    // Register input for timing and pipeline alignment
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            x_valid_r <= 1'b0;
            x_in_r <= 16'd0;
            x_in_r2 <= 16'd0; // Reset added register stage 2
            x_in_r3 <= 16'd0; // Reset added register stage 3
        end else begin
            x_valid_r <= x_valid;    // Stage 1 valid register
            x_in_r <= x_in;          // Stage 1 data register
            x_in_r2 <= x_in_r;       // Stage 2 data register
            x_in_r3 <= x_in_r2;      // Stage 3 data register (aligns with ROM output)
        end
    end

    // Address decoder module - determines which segment the input falls into
    // Uses the first stage registered input (x_in_r)
    pwl_address_decoder u_addr_decoder (
        .clk        (clk),
        .rst_n      (rst_n),
        .x_in       (x_in_r),
        .x_valid    (x_valid_r),
        .segment_idx(segment_idx),
        .addr_valid (addr_valid)
    );

    // Memory access control - registers the segment index from the decoder
    reg memory_read_en;
    reg [`PWL_ADDR_WIDTH-1:0] segment_idx_r;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            memory_read_en <= 1'b0;
            segment_idx_r <= {`PWL_ADDR_WIDTH{1'b0}};
        end else begin
            memory_read_en <= addr_valid; // Capture valid signal from decoder
            segment_idx_r <= segment_idx; // Register segment index for ROM access
        end
    end

    // Memory modules for PWL parameters (Implicit 1-cycle read latency)
    // Use the registered segment index (segment_idx_r)
    breakpoints_rom u_breakpoints_rom (
        .clk        (clk),
        .addr       (segment_idx_r),
        .data_out   (breakpoint_data)
    );

    slopes_rom u_slopes_rom (
        .clk        (clk),
        .addr       (segment_idx_r),
        .data_out   (slope_data)
    );

    intercepts_rom u_intercepts_rom (
        .clk        (clk),
        .addr       (segment_idx_r),
        .data_out   (intercept_data)
    );

    // Pipeline valid signal for memory read result
    reg memory_read_en_r;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            memory_read_en_r <= 1'b0;
        end else begin
            memory_read_en_r <= memory_read_en; // Register the memory enable signal
        end
    end

    // The data from ROMs is valid one cycle after memory_read_en is high
    assign memory_data_valid = memory_read_en_r;

    // Linear interpolator - performs the PWL calculation
    // Uses the 3-cycle delayed input (x_in_r3) to match ROM data latency
    pwl_interpolator u_interpolator (
        .clk         (clk),
        .rst_n       (rst_n),
        .x_in        (x_in_r3),          // Use the correctly delayed input
        .breakpoint  (breakpoint_data), // Data from ROMs (valid concurrently with x_in_r3)
        .slope       (slope_data),       // Data from ROMs
        .intercept   (intercept_data),   // Data from ROMs
        .valid_in    (memory_data_valid),// Valid signal indicating ROM data is ready
        .y_out       (y_out),
        .valid_out   (y_valid)
    );

endmodule

//================================================================================
// pwl_core.v - Core processing module for piecewise linear approximation
//================================================================================

`include "/vol/datastore/jmzhao/CompressedLUT/b-spline/testCPP/hardware/include/tanh_optimized_bitwidths.vh"

module pwl_core #(
    parameter INPUT_REG_STAGES = 1, // Number of input register stages
    parameter OUTPUT_REG_STAGES = 1 // Number of output register stages
) (
    input wire clk, // Clock
    input wire rst_n, // Active low reset
    input wire [15:0] x_in, // Input value
    input wire in_valid, // Input valid signal
    output wire in_ready, // Input ready signal
    output wire [15:0] y_out, // Output value
    output wire out_valid, // Output valid signal
    input wire out_ready // Output ready signal
);

    // Input buffer stage
    reg [15:0] x_buffered;
    reg in_valid_buffered;

    // Use register arrays for pipelines - fixed size, must be >= to parameters
    reg in_valid_pipe [0:INPUT_REG_STAGES-1];  // Pipeline for input valid
    reg [15:0] y_buffered [0:OUTPUT_REG_STAGES-1];  // Pipeline for output data
    reg out_valid_pipe [0:OUTPUT_REG_STAGES-1];  // Pipeline for output valid

    // Flow control signals
    wire hlut_in_valid;
    wire hlut_out_valid;
    wire [15:0] hlut_y_out;

    // Loop variables
    integer i;

    // Ready signal generation - always ready for this design
    assign in_ready = 1'b1;

    // Input buffer implementation
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            x_buffered <= 16'h0000;
            in_valid_buffered <= 1'b0;
            
            // Initialize all pipeline registers
            for (i = 0; i < INPUT_REG_STAGES; i = i + 1) begin
                in_valid_pipe[i] <= 1'b0;
            end
            for (i = 0; i < OUTPUT_REG_STAGES; i = i + 1) begin
                y_buffered[i] <= 16'h0000;
                out_valid_pipe[i] <= 1'b0;
            end
        end else begin
            // Buffer input
            if (in_ready) begin
                x_buffered <= x_in;
                in_valid_buffered <= in_valid;
            end
            
            // Input valid pipeline
            if (INPUT_REG_STAGES > 0) begin
                in_valid_pipe[0] <= in_valid_buffered;
                for (i = 1; i < INPUT_REG_STAGES; i = i + 1) begin
                    in_valid_pipe[i] <= in_valid_pipe[i-1];
                end
            end
            
            // Output data and valid pipelines
            if (OUTPUT_REG_STAGES > 0) begin
                y_buffered[0] <= hlut_y_out;
                out_valid_pipe[0] <= hlut_out_valid;
                
                for (i = 1; i < OUTPUT_REG_STAGES; i = i + 1) begin
                    y_buffered[i] <= y_buffered[i-1];
                    out_valid_pipe[i] <= out_valid_pipe[i-1];
                end
            end
        end
    end

    // HLUT instantiation
    pwl_hlut hlut_inst (
        .clk(clk),
        .rst_n(rst_n),
        .x_in(x_buffered),
        .in_valid(hlut_in_valid),
        .y_out(hlut_y_out),
        .out_valid(hlut_out_valid)
    );

    // Connect input to HLUT
    assign hlut_in_valid = (INPUT_REG_STAGES > 0) ? in_valid_pipe[INPUT_REG_STAGES-1] : in_valid_buffered;

    // Connect output from HLUT
    assign y_out = (OUTPUT_REG_STAGES > 0) ? y_buffered[OUTPUT_REG_STAGES-1] : hlut_y_out;
    assign out_valid = (OUTPUT_REG_STAGES > 0) ? out_valid_pipe[OUTPUT_REG_STAGES-1] : hlut_out_valid;
endmodule
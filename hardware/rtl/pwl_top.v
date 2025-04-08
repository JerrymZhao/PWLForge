//========================================================================
// pwl_top.v - Top-level PWL Implementation Module
//========================================================================
`timescale 1ns/1ps
`include "/vol/datastore/jmzhao/CompressedLUT/b-spline/testCPP/results/tanh/tanh_config.vh"

module pwl_top (
    input  wire        clk,
    input  wire        rst_n,
    
    // AXI-Stream-like input interface
    input  wire [15:0] s_axis_tdata,
    input  wire        s_axis_tvalid,
    output wire        s_axis_tready,
    
    // AXI-Stream-like output interface
    output wire [15:0] m_axis_tdata,
    output wire        m_axis_tvalid,
    input  wire        m_axis_tready
);

    // Flow control logic
    reg core_busy;
    assign s_axis_tready = !core_busy;
    
    // Input buffer for flow control
    reg [15:0] x_in_buffer;
    reg x_valid_buffer;
    
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            x_in_buffer <= 16'd0;
            x_valid_buffer <= 1'b0;
            core_busy <= 1'b0;
        end else begin
            if (s_axis_tvalid && s_axis_tready) begin
                // Capture new input
                x_in_buffer <= s_axis_tdata;
                x_valid_buffer <= 1'b1;
                core_busy <= 1'b1;
            end else if (m_axis_tvalid && m_axis_tready) begin
                // Calculation complete, ready for next input
                x_valid_buffer <= 1'b0;
                core_busy <= 1'b0;
            end
        end
    end
    
    // Instantiate the PWL core
    pwl_core u_pwl_core (
        .clk      (clk),
        .rst_n    (rst_n),
        .x_in     (x_in_buffer),
        .x_valid  (x_valid_buffer),
        .y_out    (m_axis_tdata),
        .y_valid  (m_axis_tvalid)
    );

endmodule

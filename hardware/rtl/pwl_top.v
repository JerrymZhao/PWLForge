//================================================================================
// pwl_top.v - AXI Stream interface wrapper for PWL function
//================================================================================

module pwl_top (
    input wire clk, // Clock
    input wire rst_n, // Active low reset

    // AXI Stream slave interface
    input wire [15:0] s_axis_tdata,  // Input data
    input wire s_axis_tvalid,        // Input valid
    output wire s_axis_tready,       // Input ready

    // AXI Stream master interface
    output wire [15:0] m_axis_tdata, // Output data
    output wire m_axis_tvalid,       // Output valid
    input wire m_axis_tready         // Output ready
);

    // Instantiate the core processor
    pwl_core #(
        .INPUT_REG_STAGES(1),
        .OUTPUT_REG_STAGES(1)
    ) pwl_core_inst (
        .clk(clk),
        .rst_n(rst_n),
        .x_in(s_axis_tdata),
        .in_valid(s_axis_tvalid),
        .in_ready(s_axis_tready),
        .y_out(m_axis_tdata),
        .out_valid(m_axis_tvalid),
        .out_ready(m_axis_tready)
    );
endmodule
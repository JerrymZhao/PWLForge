`timescale 1ns / 1ps

module pwl_top_fp32 (
    input wire aclk,
    input wire aresetn,
    
    // AXI Stream slave (input)
    input wire [31:0] s_axis_tdata,
    input wire s_axis_tvalid,
    output wire s_axis_tready,
    
    // AXI Stream master (output)
    output wire [31:0] m_axis_tdata,
    output wire m_axis_tvalid,
    input wire m_axis_tready
);

    pwl_hlut_fp32 u_hlut (
        .aclk(aclk),
        .aresetn(aresetn),
        .s_axis_x_tdata(s_axis_tdata),
        .s_axis_x_tvalid(s_axis_tvalid),
        .s_axis_x_tready(s_axis_tready),
        .m_axis_y_tdata(m_axis_tdata),
        .m_axis_y_tvalid(m_axis_tvalid),
        .m_axis_y_tready(m_axis_tready)
    );

endmodule
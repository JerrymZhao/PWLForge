//================================================================================
// pwl_top.v - Top module with AXI Stream interface (FX32)
//================================================================================

module pwl_top (
    input wire clk,
    input wire rst_n,
    
    // AXI Stream slave interface (32-bit)
    input wire [31:0] s_axis_tdata,  
    input wire s_axis_tvalid,        
    output wire s_axis_tready,       
    
    // AXI Stream master interface (32-bit)
    output wire [31:0] m_axis_tdata, 
    output wire m_axis_tvalid,       
    input wire m_axis_tready         
);

//---------------------------------------------------------------------
// Direct instantiation of HLUT module
//---------------------------------------------------------------------
pwl_hlut hlut_inst (
    .clk(clk),
    .rst_n(rst_n),
    .x_in(s_axis_tdata),
    .valid_in(s_axis_tvalid),
    .in_ready(s_axis_tready),
    .y_out(m_axis_tdata),
    .valid_out(m_axis_tvalid)
);

endmodule
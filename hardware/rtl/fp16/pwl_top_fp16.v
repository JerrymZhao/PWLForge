`timescale 1ns / 1ps

//==============================================================================
// PWL Top-Level Module for FP16
//
// Description:
//   Top-level wrapper for the piecewise linear lookup table implementation
//   using FP16 (half-precision floating-point) format.
//
// Features:
//   - AXI-Stream interface compliant
//   - 16-bit floating-point input/output
//   - Supports backpressure via TREADY/TVALID
//
// Latency:
//   - Total pipeline latency: ~20-25 cycles (FP16 operations + comparisons)
//
// Date: 2025-11-16
//==============================================================================

module pwl_top_fp16 (
    input wire aclk,
    input wire aresetn,
    
    // AXI Stream slave (input)
    input wire [15:0] s_axis_tdata,
    input wire s_axis_tvalid,
    output wire s_axis_tready,
    
    // AXI Stream master (output)
    output wire [15:0] m_axis_tdata,
    output wire m_axis_tvalid,
    input wire m_axis_tready
);

    //==========================================================================
    // Instantiate the core HLUT module
    //==========================================================================
    pwl_hlut_fp16 u_hlut (
        .aclk(aclk),
        .aresetn(aresetn),
        
        // Input interface
        .s_axis_x_tdata(s_axis_tdata),
        .s_axis_x_tvalid(s_axis_tvalid),
        .s_axis_x_tready(s_axis_tready),
        
        // Output interface
        .m_axis_y_tdata(m_axis_tdata),
        .m_axis_y_tvalid(m_axis_tvalid),
        .m_axis_y_tready(m_axis_tready)
    );

endmodule
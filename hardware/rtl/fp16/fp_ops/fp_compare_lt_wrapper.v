`timescale 1ns / 1ps

//==============================================================================
// Floating-Point Less-Than Comparison Wrapper (NonBlocking Mode)
//
// Description:
// Parameterized wrapper for Xilinx Floating-Point Comparison IP cores.
// Supports FP16 and FP32 formats with full AXI-Stream interface.
// Configured in NonBlocking mode (no TREADY on inputs/outputs).
// Includes metadata passthrough pipeline synchronized with computation.
//
// Parameters:
// DATA_WIDTH - Bit width of floating-point format (16 or 32, default: 32)
// METADATA_WIDTH - Bit width of metadata to be delayed (default: 32)
//
// Features:
// - Automatic IP selection based on DATA_WIDTH
// - Metadata delayed to match comparison latency (2 cycles)
// - NonBlocking operation (always ready to accept inputs)
//
// Latency: 2 cycles (both FP16 and FP32)
//
// Configuration Requirements (in Vivado IP Customizer):
// - Operation Type: Compare
// - Operation Selection: Less_Than
// - Flow Control: NonBlocking
// - Latency: 2 cycles
//==============================================================================

module fp_compare_lt_wrapper #(
    parameter DATA_WIDTH = 32,      // Default: FP32 (use 16 for FP16)
    parameter METADATA_WIDTH = 32
)(
    input wire aclk,
    input wire aresetn,
    
    // Input operand A (x)
    input wire [DATA_WIDTH-1:0] s_axis_a_tdata,
    input wire s_axis_a_tvalid,
    output wire s_axis_a_tready,
    
    // Input operand B (interval_end)
    input wire [DATA_WIDTH-1:0] s_axis_b_tdata,
    input wire s_axis_b_tvalid,
    output wire s_axis_b_tready,
    
    // Result output (1 bit: a < b)
    output wire m_axis_result_tdata,           // Single bit result
    output wire [7:0] m_axis_result_tdata_full, // Full 8-bit output from IP
    output wire m_axis_result_tvalid,
    input wire m_axis_result_tready,
    
    // Metadata passthrough (delayed to match computation latency)
    input wire [METADATA_WIDTH-1:0] s_axis_metadata_tdata,
    input wire s_axis_metadata_tvalid,
    output wire s_axis_metadata_tready,
    
    output wire [METADATA_WIDTH-1:0] m_axis_metadata_tdata,
    output wire m_axis_metadata_tvalid,
    input wire m_axis_metadata_tready
);

    //==========================================================================
    // Latency Configuration
    //==========================================================================
    localparam LATENCY = 2;  // Both FP16 and FP32 have 2-cycle latency
    
    //==========================================================================
    // Floating-Point Comparison IP Instantiation
    //==========================================================================
    wire [7:0] result_full;
    wire result_valid;
    
    generate
        if (DATA_WIDTH == 16) begin : fp16_compare
            // FP16 Comparison IP
            fp16_compare_lt u_fp_compare (
                .aclk(aclk),
                .aresetn(aresetn),
                .s_axis_a_tdata(s_axis_a_tdata),
                .s_axis_a_tvalid(s_axis_a_tvalid),
                .s_axis_b_tdata(s_axis_b_tdata),
                .s_axis_b_tvalid(s_axis_b_tvalid),
                .m_axis_result_tdata(result_full),
                .m_axis_result_tvalid(result_valid)
            );
        end else if (DATA_WIDTH == 32) begin : fp32_compare
            // FP32 Comparison IP (default)
            fp32_compare_lt u_fp_compare (
                .aclk(aclk),
                .aresetn(aresetn),
                .s_axis_a_tdata(s_axis_a_tdata),
                .s_axis_a_tvalid(s_axis_a_tvalid),
                .s_axis_b_tdata(s_axis_b_tdata),
                .s_axis_b_tvalid(s_axis_b_tvalid),
                .m_axis_result_tdata(result_full),
                .m_axis_result_tvalid(result_valid)
            );
        end else begin : unsupported
            // Generate synthesis error for unsupported widths
            initial begin
                $fatal(1, "Unsupported DATA_WIDTH: %0d. Must be 16 or 32", DATA_WIDTH);
            end
        end
    endgenerate
    
    //==========================================================================
    // Output Assignment
    //==========================================================================
    // NonBlocking IP always accepts input (no backpressure)
    assign s_axis_a_tready = 1'b1;
    assign s_axis_b_tready = 1'b1;
    
    // Extract the comparison result bit (LSB of 8-bit output)
    assign m_axis_result_tdata = result_full[0];
    assign m_axis_result_tdata_full = result_full;
    assign m_axis_result_tvalid = result_valid;
    
    //==========================================================================
    // Metadata Synchronization Pipeline
    //
    // Purpose:
    //   Delay metadata to match the 2-cycle comparison latency.
    //   Metadata is captured when input operands are valid.
    //   Metadata is output when comparison result is valid.
    //
    // Note: NonBlocking mode means we can't stall, so metadata pipeline
    //       is a simple shift register without backpressure handling.
    //==========================================================================
    generate
        if (METADATA_WIDTH > 0) begin : gen_metadata
            reg [METADATA_WIDTH-1:0] metadata_delay [0:LATENCY-1];
            reg [LATENCY-1:0] metadata_valid_delay;
            
            integer i;
            
            always @(posedge aclk or negedge aresetn) begin
                if (!aresetn) begin
                    for (i = 0; i < LATENCY; i = i + 1) begin
                        metadata_delay[i] <= {METADATA_WIDTH{1'b0}};
                    end
                    metadata_valid_delay <= {LATENCY{1'b0}};
                end else begin
                    // Stage 0: Capture input metadata
                    metadata_delay[0] <= s_axis_metadata_tdata;
                    metadata_valid_delay[0] <= s_axis_metadata_tvalid;
                    
                    // Stages 1 to LATENCY-1: Shift pipeline
                    for (i = 1; i < LATENCY; i = i + 1) begin
                        metadata_delay[i] <= metadata_delay[i-1];
                        metadata_valid_delay[i] <= metadata_valid_delay[i-1];
                    end
                end
            end
            
            // Output metadata from last pipeline stage
            assign m_axis_metadata_tdata = metadata_delay[LATENCY-1];
            assign m_axis_metadata_tvalid = metadata_valid_delay[LATENCY-1];
            assign s_axis_metadata_tready = 1'b1;  // Always ready in NonBlocking mode
            
        end else begin : gen_no_metadata
            // No metadata passthrough
            assign m_axis_metadata_tdata = {METADATA_WIDTH{1'b0}};
            assign m_axis_metadata_tvalid = 1'b0;
            assign s_axis_metadata_tready = 1'b1;
        end
    endgenerate

endmodule
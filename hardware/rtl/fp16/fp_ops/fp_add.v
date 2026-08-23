`timescale 1ns / 1ps

//==============================================================================
// Floating-Point Adder Wrapper (Blocking Mode, Add Only)
//
// Description:
// Parameterized wrapper for Xilinx Floating-Point Adder IP cores.
// Supports FP16, FP32, and FP64 formats with full AXI-Stream interface.
// Configured in Blocking mode with complete flow control.
// Operation: ADD only (A + B)
// Includes metadata passthrough pipeline synchronized with computation.
//
// Parameters:
// DATA_WIDTH - Bit width of floating-point format (16, 32, or 64)
// METADATA_WIDTH - Bit width of metadata to be delayed (default: 32)
//
// Features:
// - Metadata is captured when operands A and B are accepted (input handshake)
// - Metadata is delayed to match computation latency
// - Metadata output is synchronized with result output
// - Full back-pressure support on both data and metadata paths
//
// Metadata Synchronization:
// - Input: Metadata captured when (s_axis_a_tvalid & s_axis_a_tready &
// s_axis_b_tvalid & s_axis_b_tready)
// - Output: Metadata valid when (m_axis_result_tvalid)
// - Delay: Automatically matches DATA_WIDTH-dependent latency
//
// Configuration Requirements (in Vivado IP Customizer):
// - Operation Selection: Add/Subtract
// - Add/Subtract options: Add (NOT Both)
// - Flow Control: Blocking
//==============================================================================

module fp_add #(
    parameter DATA_WIDTH = 32,
    parameter METADATA_WIDTH = 32
)(
    input wire aclk,
    input wire aresetn,  // Active-low reset
    
    // Input operand A
    input wire [DATA_WIDTH-1:0] s_axis_a_tdata,
    input wire s_axis_a_tvalid,
    output wire s_axis_a_tready,
    
    // Input operand B
    input wire [DATA_WIDTH-1:0] s_axis_b_tdata,
    input wire s_axis_b_tvalid,
    output wire s_axis_b_tready,
    
    // Output result (A + B)
    output wire [DATA_WIDTH-1:0] m_axis_result_tdata,
    output wire m_axis_result_tvalid,
    input wire m_axis_result_tready,
    
    // Metadata passthrough
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
    
    localparam LATENCY = (DATA_WIDTH == 16) ? 7 : 11;

    //==========================================================================
    // Floating-Point Adder IP Instantiation
    //==========================================================================
    
    generate
        if (DATA_WIDTH == 16) begin : fp16_add
            fp16_add_ip u_add (
                .aclk(aclk),
                .aresetn(aresetn),
                .s_axis_a_tvalid(s_axis_a_tvalid),
                .s_axis_a_tready(s_axis_a_tready),
                .s_axis_a_tdata(s_axis_a_tdata),
                .s_axis_b_tvalid(s_axis_b_tvalid),
                .s_axis_b_tready(s_axis_b_tready),
                .s_axis_b_tdata(s_axis_b_tdata),
                .m_axis_result_tvalid(m_axis_result_tvalid),
                .m_axis_result_tready(m_axis_result_tready),
                .m_axis_result_tdata(m_axis_result_tdata)
            );
        end else if (DATA_WIDTH == 32) begin : fp32_add
            fp32_add_ip u_add (
                .aclk(aclk),
                .aresetn(aresetn),
                .s_axis_a_tvalid(s_axis_a_tvalid),
                .s_axis_a_tready(s_axis_a_tready),
                .s_axis_a_tdata(s_axis_a_tdata),
                .s_axis_b_tvalid(s_axis_b_tvalid),
                .s_axis_b_tready(s_axis_b_tready),
                .s_axis_b_tdata(s_axis_b_tdata),
                .m_axis_result_tvalid(m_axis_result_tvalid),
                .m_axis_result_tready(m_axis_result_tready),
                .m_axis_result_tdata(m_axis_result_tdata)
            );
        end else if (DATA_WIDTH == 64) begin : fp64_add
            fp64_add_ip u_add (
                .aclk(aclk),
                .aresetn(aresetn),
                .s_axis_a_tvalid(s_axis_a_tvalid),
                .s_axis_a_tready(s_axis_a_tready),
                .s_axis_a_tdata(s_axis_a_tdata),
                .s_axis_b_tvalid(s_axis_b_tvalid),
                .s_axis_b_tready(s_axis_b_tready),
                .s_axis_b_tdata(s_axis_b_tdata),
                .m_axis_result_tvalid(m_axis_result_tvalid),
                .m_axis_result_tready(m_axis_result_tready),
                .m_axis_result_tdata(m_axis_result_tdata)
            );
        end else begin : unsupported
            initial begin
                $fatal(1, "Unsupported DATA_WIDTH: %0d. Must be 16, 32, or 64", DATA_WIDTH);
            end
        end
    endgenerate

    //==========================================================================
    // Metadata Synchronization Pipeline
    //==========================================================================
    
    reg [METADATA_WIDTH-1:0] metadata_pipe [0:LATENCY-1];
    reg valid_pipe [0:LATENCY-1];
    wire [LATENCY-1:0] ready_pipe;
    
    integer i;
    
    wire operands_accepted = s_axis_a_tvalid & s_axis_a_tready & 
                            s_axis_b_tvalid & s_axis_b_tready;
    
    wire metadata_input_transfer = s_axis_metadata_tvalid & 
                                   s_axis_metadata_tready & 
                                   operands_accepted;
    
    assign s_axis_metadata_tready = !valid_pipe[0] || ready_pipe[0];
    
    //--------------------------------------------------------------------------
    // Pipeline with Reset
    //--------------------------------------------------------------------------
    
    always @(posedge aclk or negedge aresetn) begin
        if (!aresetn) begin
            for (i = 0; i < LATENCY; i = i + 1) begin
                metadata_pipe[i] <= {METADATA_WIDTH{1'b0}};
                valid_pipe[i] <= 1'b0;
            end
        end else begin
            // Stage 0: Input capture
            if (metadata_input_transfer) begin
                metadata_pipe[0] <= s_axis_metadata_tdata;
                valid_pipe[0] <= 1'b1;
            end else if (ready_pipe[0]) begin
                valid_pipe[0] <= 1'b0;
            end
            
            // Stages 1 to LATENCY-1
            for (i = 1; i < LATENCY; i = i + 1) begin
                if (ready_pipe[i]) begin
                    if (valid_pipe[i-1]) begin
                        metadata_pipe[i] <= metadata_pipe[i-1];
                        valid_pipe[i] <= 1'b1;
                    end else begin
                        valid_pipe[i] <= 1'b0;
                    end
                end
            end
        end
    end
    
    //--------------------------------------------------------------------------
    // Ready Chain
    //--------------------------------------------------------------------------
    
    genvar stage;
    generate
        for (stage = 0; stage < LATENCY; stage = stage + 1) begin : ready_gen
            if (stage == LATENCY - 1) begin
                assign ready_pipe[stage] = !valid_pipe[stage] || m_axis_metadata_tready;
            end else begin
                assign ready_pipe[stage] = !valid_pipe[stage+1] || ready_pipe[stage+1];
            end
        end
    endgenerate
    
    //--------------------------------------------------------------------------
    // Output
    //--------------------------------------------------------------------------
    
    assign m_axis_metadata_tdata = metadata_pipe[LATENCY-1];
    assign m_axis_metadata_tvalid = valid_pipe[LATENCY-1];

endmodule
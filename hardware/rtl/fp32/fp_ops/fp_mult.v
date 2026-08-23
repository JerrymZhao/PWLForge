`timescale 1ns / 1ps

//==============================================================================
// Floating-Point Multiplier Wrapper (Blocking Mode)
// 
// Description:
//   Parameterized wrapper for Xilinx Floating-Point Multiplier IP cores.
//   Supports FP16, FP32, and FP64 formats with full AXI-Stream interface.
//   Configured in Blocking mode with complete flow control.
//   Includes metadata passthrough pipeline synchronized with computation.
//   
// Parameters:
//   DATA_WIDTH - Bit width of floating-point format (16, 32, or 64)
//   METADATA_WIDTH - Bit width of metadata to be delayed (default: 32)
//
// Features:
//   - Metadata is captured when operands A and B are accepted (input handshake)
//   - Metadata is delayed to match computation latency
//   - Metadata output is synchronized with result output
//   - Full back-pressure support on both data and metadata paths
//
// Latency (no back-pressure):
//   FP16: 6 cycles, FP32/FP64: 8 cycles
//
// Metadata Synchronization:
//   - Input: Metadata captured when (s_axis_a_tvalid & s_axis_a_tready & 
//                                      s_axis_b_tvalid & s_axis_b_tready)
//   - Output: Metadata valid when (m_axis_result_tvalid)
//   - Delay: Automatically matches DATA_WIDTH-dependent latency
//   
// Configuration Requirements (in Vivado IP Customizer):
//   - Flow Control: Blocking
//   - RESULT channel has TREADY: Checked
//==============================================================================

module fp_mult #(
    parameter DATA_WIDTH = 32,
    parameter METADATA_WIDTH = 32
)(
    input wire aclk,
    input wire aresetn,  // Active-low reset (AXI-Stream standard)
    
    // Input operand A (AXI-Stream slave)
    input wire [DATA_WIDTH-1:0] s_axis_a_tdata,
    input wire s_axis_a_tvalid,
    output wire s_axis_a_tready,
    
    // Input operand B (AXI-Stream slave)
    input wire [DATA_WIDTH-1:0] s_axis_b_tdata,
    input wire s_axis_b_tvalid,
    output wire s_axis_b_tready,
    
    // Output result (AXI-Stream master)
    // Result = A * B
    output wire [DATA_WIDTH-1:0] m_axis_result_tdata,
    output wire m_axis_result_tvalid,
    input wire m_axis_result_tready,
    
    // Metadata passthrough (delayed to match computation latency)
    // Metadata is captured when input operands are accepted
    // Metadata is output synchronized with computation result
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
    
    // Determine latency based on DATA_WIDTH
    // FP16: 6 cycles, FP32/FP64: 8 cycles
    localparam LATENCY = (DATA_WIDTH == 16) ? 6 : 8;
    
    // Export latency for parent module reference
    function integer get_mult_latency;
        input integer width;
        begin
            get_mult_latency = (width == 16) ? 6 : 8;
        end
    endfunction

    //==========================================================================
    // Floating-Point Multiplier IP Instantiation
    //==========================================================================
    
    generate
        if (DATA_WIDTH == 16) begin : fp16_mult
            fp16_mult_ip u_mult (
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
        end else if (DATA_WIDTH == 32) begin : fp32_mult
            fp32_mult_ip u_mult (
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
        end else if (DATA_WIDTH == 64) begin : fp64_mult
            fp64_mult_ip u_mult (
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
            // Generate synthesis error for unsupported widths
            initial begin
                $fatal(1, "Unsupported DATA_WIDTH: %0d. Must be 16, 32, or 64", DATA_WIDTH);
            end
        end
    endgenerate

    //==========================================================================
    // Metadata Synchronization Pipeline
    //
    // Purpose:
    //   Delay metadata to match the computation latency of the multiplier.
    //   Metadata is captured when input operands are accepted.
    //   Metadata is output when computation result is valid.
    //
    // Synchronization Strategy:
    //   - Input capture: Triggered by successful input handshake
    //     (both A and B operands accepted by multiplier)
    //   - Pipeline depth: Matches LATENCY (6 for FP16, 8 for FP32/FP64)
    //   - Output: Synchronized with m_axis_result_tvalid
    //
    // Back-pressure Handling:
    //   - If downstream is not ready (m_axis_metadata_tready = 0),
    //     metadata pipeline stalls
    //   - If pipeline is full, s_axis_metadata_tready = 0,
    //     preventing new metadata capture
    //
    // Use Case Example (MAC Tree):
    //   Input metadata: {param_0, param_1}
    //   After LATENCY cycles: Output metadata synchronized with product result
    //   Next stage (adder) receives synchronized metadata for further processing
    //==========================================================================
    
    // Pipeline registers
    reg [METADATA_WIDTH-1:0] metadata_pipe [0:LATENCY-1];
    reg valid_pipe [0:LATENCY-1];
    wire [LATENCY-1:0] ready_pipe;
    
    integer i;
    
    //--------------------------------------------------------------------------
    // Input Capture Logic
    //--------------------------------------------------------------------------
    
    // Metadata is captured when:
    // 1. Metadata input is valid
    // 2. Pipeline can accept new data (stage 0 is ready)
    // 3. Operands A and B are being accepted by multiplier (synchronized capture)
    wire operands_accepted = s_axis_a_tvalid & s_axis_a_tready & 
                            s_axis_b_tvalid & s_axis_b_tready;
    
    wire metadata_input_transfer = s_axis_metadata_tvalid & 
                                   s_axis_metadata_tready & 
                                   operands_accepted;
    
    // Metadata input ready when pipeline stage 0 can accept data
    assign s_axis_metadata_tready = !valid_pipe[0] || ready_pipe[0];
    
    //--------------------------------------------------------------------------
    // Pipeline Shift Logic (with Reset)
    //--------------------------------------------------------------------------
    
    always @(posedge aclk or negedge aresetn) begin
        if (!aresetn) begin
            // Reset all pipeline stages
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
            
            // Stages 1 to LATENCY-1: Shift pipeline with back-pressure
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
    // Ready Chain (Back-pressure Propagation)
    //--------------------------------------------------------------------------
    
    // Each stage is ready if:
    // - Next stage is empty (!valid), OR
    // - Next stage is being read (ready)
    genvar stage;
    generate
        for (stage = 0; stage < LATENCY; stage = stage + 1) begin : ready_gen
            if (stage == LATENCY - 1) begin
                // Last stage: ready if output can be consumed
                assign ready_pipe[stage] = !valid_pipe[stage] || m_axis_metadata_tready;
            end else begin
                // Intermediate stages: ready if next stage can accept
                assign ready_pipe[stage] = !valid_pipe[stage+1] || ready_pipe[stage+1];
            end
        end
    endgenerate
    
    //--------------------------------------------------------------------------
    // Output Assignment
    //--------------------------------------------------------------------------
    
    assign m_axis_metadata_tdata = metadata_pipe[LATENCY-1];
    assign m_axis_metadata_tvalid = valid_pipe[LATENCY-1];
    
    //==========================================================================
    // Assertions for Debug (Optional, can be disabled in synthesis)
    //==========================================================================
    
    // synthesis translate_off
    
    // Check metadata and operands are synchronized at input
    always @(posedge aclk) begin
        if (aresetn && metadata_input_transfer && !operands_accepted) begin
            $error("[fp_mult] Metadata captured without operands being accepted!");
        end
    end
    
    // Warn if metadata valid but operands not valid (potential desync)
    always @(posedge aclk) begin
        if (aresetn && s_axis_metadata_tvalid && s_axis_metadata_tready) begin
            if (!s_axis_a_tvalid || !s_axis_b_tvalid) begin
                $warning("[fp_mult] Metadata transfer without valid operands");
            end
        end
    end
    
    // synthesis translate_on

endmodule
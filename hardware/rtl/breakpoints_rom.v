//========================================================================
// breakpoints_rom.v - ROM for Segment Breakpoints with debug
//========================================================================
`timescale 1ns/1ps
`include "/vol/datastore/jmzhao/CompressedLUT/b-spline/testCPP/results/tanh/tanh_config.vh"

module breakpoints_rom (
    input  wire                        clk,
    input  wire [`PWL_ADDR_WIDTH-1:0] addr,
    output reg  [15:0]                 data_out
);

    // Memory array
    reg [15:0] mem [`PWL_NUM_BREAKPOINTS-1:0];
    
    // Memory initialization
    initial begin
        // In simulation, read from hex file
        $readmemh("/vol/datastore/jmzhao/CompressedLUT/b-spline/testCPP/results/tanh/tanh_breakpoints.hex", mem);
        // Display some values to verify loading
//        $display("ROM Init: breakpoints[4]=%h", mem[4]);
    end
    
    // Synchronous read operation with debug
    always @(posedge clk) begin
        if (addr < `PWL_NUM_BREAKPOINTS) begin
            data_out <= mem[addr];
            $display("ROM Debug: breakpoints[%d] read %h", addr, mem[addr]);
        end else begin
            data_out <= 16'd0; // Default for out-of-range addresses
            $display("ROM Error: Address %d out of range (max %d)", addr, `PWL_NUM_BREAKPOINTS-1);
        end
    end

endmodule

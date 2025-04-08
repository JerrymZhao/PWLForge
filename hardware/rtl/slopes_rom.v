//========================================================================
// slopes_rom.v - ROM for Segment Slopes
//========================================================================
`timescale 1ns/1ps
`include "/vol/datastore/jmzhao/CompressedLUT/b-spline/testCPP/results/tanh/tanh_config.vh"

module slopes_rom (
    input  wire                        clk,
    input  wire [`PWL_ADDR_WIDTH-1:0] addr,
    output reg  [15:0]                 data_out
);

    // Memory array
    reg [15:0] mem [`PWL_NUM_SEGMENTS-1:0];
    
    // Memory initialization
    initial begin
        // In simulation, read from hex file
        $readmemh("/vol/datastore/jmzhao/CompressedLUT/b-spline/testCPP/results/tanh/tanh_slopes.hex", mem);
        // In synthesis, COE file will be used by IP core
    end
    
    // Synchronous read operation
    always @(posedge clk) begin
        if (addr < `PWL_NUM_SEGMENTS) begin
            data_out <= mem[addr];
        end else begin
            data_out <= 16'd0; // Default for out-of-range addresses
        end
    end

endmodule

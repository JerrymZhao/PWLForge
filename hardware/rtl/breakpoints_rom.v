//========================================================================
// breakpoints_rom.v - ROM for Segment Breakpoints
//========================================================================
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
        $readmemh("../../../results/tanh/tanh_breakpoints.hex", mem);
        // In synthesis, COE file will be used by IP core
    end
    
    // Synchronous read operation
    always @(posedge clk) begin
        if (addr < `PWL_NUM_BREAKPOINTS) begin
            data_out <= mem[addr];
        end else begin
            data_out <= 16'd0; // Default for out-of-range addresses
        end
    end

endmodule

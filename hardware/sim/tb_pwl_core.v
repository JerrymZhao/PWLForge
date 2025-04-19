//================================================================================
// tb_pwl_core.v - Testbench for Generic PWL Core module
//================================================================================

`timescale 1ns/1ps

module tb_pwl_core;
    // Testbench signals
    reg clk = 0;
    reg rst_n = 0;
    reg [15:0] x_in;
    reg in_valid = 0;
    wire in_ready;
    wire [15:0] y_out;
    wire out_valid;
    reg out_ready = 1;
    
    // Test vectors and configuration
    reg [31:0] test_vectors [0:99];
    integer vec_count = 0;
    integer pass_count = 0;
    integer fail_count = 0;
    
    // Function selection (configurable parameter)
    parameter FUNCTION_TYPE = "GENERIC";
    parameter TEST_VECTOR_PATH = "/vol/datastore/jmzhao/CompressedLUT/b-spline/testCPP/hardware/sim/test_vectors/tanh_vectors.txt";
    
    // DUT instance
    pwl_core #(
        .INPUT_REG_STAGES(1),
        .OUTPUT_REG_STAGES(1),
        .FUNCTION_TYPE(FUNCTION_TYPE)
    ) dut (
        .clk(clk),
        .rst_n(rst_n),
        .x_in(x_in),
        .in_valid(in_valid),
        .in_ready(in_ready),
        .y_out(y_out),
        .out_valid(out_valid),
        .out_ready(out_ready)
    );
    
    // Clock generation
    always #5 clk = ~clk;
    
    // Stimulus
    initial begin
        // Load test vectors - generic path that can be overridden
        $readmemh(TEST_VECTOR_PATH, test_vectors);
        
        // Reset sequence
        rst_n = 0;
        #20 rst_n = 1;
        #10;
        
        // Apply test vectors
        for (vec_count = 0; vec_count < 100; vec_count = vec_count + 1) begin
            @(posedge clk);
            x_in = test_vectors[vec_count][31:16];
            in_valid = 1'b1;
            
            // Wait for the core to accept the input
            while (!in_ready) @(posedge clk);
            
            // Wait for result
            @(posedge clk);
            in_valid = 1'b0;
            
            // Wait for valid output
            while (!out_valid) @(posedge clk);
            
            // Check result
            if (y_out == test_vectors[vec_count][15:0]) begin
                $display("Vector %0d: PASS - x=%h, y=%h (%s)", 
                         vec_count, x_in, y_out, FUNCTION_TYPE);
                pass_count = pass_count + 1;
            end else begin
                $display("Vector %0d: FAIL - x=%h, got y=%h, expected=%h (%s)", 
                         vec_count, x_in, y_out, test_vectors[vec_count][15:0], FUNCTION_TYPE);
                fail_count = fail_count + 1;
            end
            
            #10; // Wait between vectors
        end
        
        // Test summary
        $display("\nPWL Core Test Summary for %s:", FUNCTION_TYPE);
        $display("Total vectors: %0d", vec_count);
        $display("Passed: %0d", pass_count);
        $display("Failed: %0d\n", fail_count);
        
        $finish;
    end
    
    // VCD generation
    initial begin
        $dumpfile("pwl_core_sim.vcd");
        $dumpvars(0, tb_pwl_core);
    end
endmodule
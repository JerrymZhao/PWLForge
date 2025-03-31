//========================================================================
// tb_pwl_core.v - Testbench for PWL Core
//========================================================================
`include "/vol/datastore/jmzhao/CompressedLUT/b-spline/testCPP/results/tanh/tanh_config.vh"
`timescale 1ns / 1ps

module tb_pwl_core();

    // Clock and reset
    reg clk;
    reg rst_n;
    
    // Testbench signals
    reg [15:0] x_in;
    reg x_valid;
    wire [15:0] y_out;
    wire y_valid;
    
    // Test vector file
    reg [31:0] test_vectors [0:999]; // Input and expected output pairs
    integer num_vectors;
    integer errors;
    integer i;
    
    // DUT instantiation
    pwl_core dut (
        .clk     (clk),
        .rst_n   (rst_n),
        .x_in    (x_in),
        .x_valid (x_valid),
        .y_out   (y_out),
        .y_valid (y_valid)
    );
    
    // Clock generation
    initial begin
        clk = 0;
        forever #5 clk = ~clk; // 100MHz clock
    end
    
    // Test procedure
    initial begin
        // Load test vectors
        $readmemh("../sim/test_vectors/tanh_vectors.txt", test_vectors);
        num_vectors = 1000; // Update based on actual file size
        
        // Initialize signals
        rst_n = 0;
        x_in = 0;
        x_valid = 0;
        errors = 0;
        
        // Reset sequence
        #100 rst_n = 1;
        #20;
        
        // Process test vectors
        for (i = 0; i < num_vectors; i = i + 1) begin
            // Apply input
            @(posedge clk);
            x_in = test_vectors[i][31:16]; // Input value
            x_valid = 1;
            
            @(posedge clk);
            x_valid = 0;
            
            // Wait for result
            wait(y_valid);
            @(posedge clk);
            
            // Check result
            if (y_out !== test_vectors[i][15:0]) begin
                $display("Error at vector %d: Expected %h, Got %h", 
                         i, test_vectors[i][15:0], y_out);
                errors = errors + 1;
            end
            
            // Add delay between vectors
            #20;
        end
        
        // Report results
        if (errors == 0) begin
            $display("TEST PASSED: All %d vectors passed", num_vectors);
        end else begin
            $display("TEST FAILED: %d out of %d vectors failed", 
                     errors, num_vectors);
        end
        
        $finish;
    end
    
    // Timeout
    initial begin
        #50000;
        $display("Simulation timeout");
        $finish;
    end
    
    // Waveform dump for debugging
    initial begin
        $dumpfile("pwl_core.vcd");
        $dumpvars(0, tb_pwl_core);
    end

endmodule

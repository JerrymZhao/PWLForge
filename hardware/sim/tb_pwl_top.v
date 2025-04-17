//================================================================================
// tb_pwl_top.v - System-level testbench with AXI Stream interfaces for tanh function
//================================================================================

`timescale 1ns/1ps

module tb_pwl_top;
    // System parameters
    parameter CLK_PERIOD = 10; // 100MHz clock

    // Testbench signals
    reg clk = 0;
    reg rst_n = 0;
    
    // AXI Stream interfaces
    reg [15:0] s_axis_tdata;
    reg s_axis_tvalid;
    wire s_axis_tready;
    wire [15:0] m_axis_tdata;
    wire m_axis_tvalid;
    reg m_axis_tready;
    
    // Test vectors and results
    reg [31:0] test_vectors [0:99];
    integer vec_count = 0;
    integer pass_count = 0;
    integer fail_count = 0;
    integer timeout_count = 0;
    
    // Timeout flag declared at module level
    reg timeout_flag;
    
    // DUT instance
    pwl_top dut (
        .clk(clk),
        .rst_n(rst_n),
        .s_axis_tdata(s_axis_tdata),
        .s_axis_tvalid(s_axis_tvalid),
        .s_axis_tready(s_axis_tready),
        .m_axis_tdata(m_axis_tdata),
        .m_axis_tvalid(m_axis_tvalid),
        .m_axis_tready(m_axis_tready)
    );
    
    // Clock generation
    always #(CLK_PERIOD/2) clk = ~clk;
    
    // Modified timeout task - returns when valid is detected or timeout occurs
    task wait_for_valid_with_timeout;
        input integer max_cycles;
        begin
            timeout_flag = 0;
            repeat (max_cycles) begin
                @(posedge clk);
                if (m_axis_tvalid) return; // Exit task if valid detected
            end
            // If we get here, timeout occurred
            timeout_flag = 1;
        end
    endtask
    
    // Stimulus
    initial begin
        // Initialize
        rst_n = 0;
        s_axis_tdata = 0;
        s_axis_tvalid = 0;
        m_axis_tready = 1; // Always ready to receive output
        
        // Load test vectors - updated path for tanh function
        $readmemh("tanh_vectors.txt", test_vectors);
        
        // Reset sequence
        repeat (5) @(posedge clk);
        rst_n = 1;
        repeat (5) @(posedge clk);
        
        // Apply test vectors
        for (vec_count = 0; vec_count < 100; vec_count = vec_count + 1) begin
            // Apply input
            @(posedge clk);
            s_axis_tdata = test_vectors[vec_count][31:16];
            s_axis_tvalid = 1'b1;
            
            // Wait for ready
            while (!s_axis_tready) @(posedge clk);
            
            // Transaction accepted, deassert valid
            @(posedge clk);
            s_axis_tvalid = 1'b0;
            
            // Wait for output valid with timeout
            timeout_flag = 0;
            wait_for_valid_with_timeout(100);
            
            if (timeout_flag) begin
                $display("Vector %0d: TIMEOUT - No response within 100 cycles", vec_count);
                timeout_count = timeout_count + 1;
            end else begin
                // Check result
                if (m_axis_tdata == test_vectors[vec_count][15:0]) begin
                    $display("Vector %0d: PASS - x=%h, y=%h (tanh)", 
                             vec_count, test_vectors[vec_count][31:16], m_axis_tdata);
                    pass_count = pass_count + 1;
                end else begin
                    $display("Vector %0d: FAIL - x=%h, got y=%h, expected=%h (tanh)", 
                             vec_count, test_vectors[vec_count][31:16], 
                             m_axis_tdata, test_vectors[vec_count][15:0]);
                    fail_count = fail_count + 1;
                end
            end
            
            // Signal consumed
            @(posedge clk);
        end
        
        // Test summary
        $display("\nTanh System Test Summary:");
        $display("Total vectors: %0d", vec_count);
        $display("Passed: %0d", pass_count);
        $display("Failed: %0d", fail_count);
        $display("Timeouts: %0d\n", timeout_count);
        
        $finish;
    end
    
    // VCD generation
    initial begin
        $dumpfile("tanh_pwl_top_sim.vcd");
        $dumpvars(0, tb_pwl_top);
    end
endmodule
//========================================================================
// tb_pwl_top.v - Testbench for PWL Top Module
//========================================================================
`include "/vol/datastore/jmzhao/CompressedLUT/b-spline/testCPP/results/tanh/tanh_config.vh"
`timescale 1ns / 1ps

module tb_pwl_top();

    // Clock and reset
    reg clk;
    reg rst_n;
    
    // AXI-Stream interfaces
    reg [15:0] s_axis_tdata;
    reg s_axis_tvalid;
    wire s_axis_tready;
    
    wire [15:0] m_axis_tdata;
    wire m_axis_tvalid;
    reg m_axis_tready;
    
    // Test vector file
    reg [31:0] test_vectors [0:999]; // Input and expected output pairs
    integer num_vectors;
    integer errors;
    integer i;
    
    // DUT instantiation
    pwl_top dut (
        .clk           (clk),
        .rst_n         (rst_n),
        .s_axis_tdata  (s_axis_tdata),
        .s_axis_tvalid (s_axis_tvalid),
        .s_axis_tready (s_axis_tready),
        .m_axis_tdata  (m_axis_tdata),
        .m_axis_tvalid (m_axis_tvalid),
        .m_axis_tready (m_axis_tready)
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
        s_axis_tdata = 0;
        s_axis_tvalid = 0;
        m_axis_tready = 1; // Always ready to receive output
        errors = 0;
        
        // Reset sequence
        #100 rst_n = 1;
        #20;
        
        // Process test vectors
        for (i = 0; i < num_vectors; i = i + 1) begin
            // Wait for AXI handshake
            wait(s_axis_tready);
            
            // Apply input
            @(posedge clk);
            s_axis_tdata = test_vectors[i][31:16]; // Input value
            s_axis_tvalid = 1;
            
            // Wait for acceptance
            wait(s_axis_tvalid && s_axis_tready);
            @(posedge clk);
            s_axis_tvalid = 0;
            
            // Wait for result
            wait(m_axis_tvalid);
            @(posedge clk);
            
            // Check result
            if (m_axis_tdata !== test_vectors[i][15:0]) begin
                $display("Error at vector %d: Expected %h, Got %h", 
                         i, test_vectors[i][15:0], m_axis_tdata);
                errors = errors + 1;
            end
            
            // Introduce backpressure occasionally
            if (i % 10 == 0) begin
                m_axis_tready = 0;
                #20;
                m_axis_tready = 1;
            end
            
            // Add delay between vectors
            #10;
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
        #100000;
        $display("Simulation timeout");
        $finish;
    end
    
    // Waveform dump for debugging
    initial begin
        $dumpfile("pwl_top.vcd");
        $dumpvars(0, tb_pwl_top);
    end

endmodule

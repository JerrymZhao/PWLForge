//========================================================================
// pwl_core.v - Piecewise Linear Approximation Core
//========================================================================
`include "/vol/datastore/jmzhao/CompressedLUT/b-spline/testCPP/results/tanh/tanh_config.vh"

module pwl_core (
    input  wire                        clk,
    input  wire                        rst_n,
    input  wire [15:0]                 x_in,
    input  wire                        x_valid,
    output wire [15:0]                 y_out,
    output wire                        y_valid
);

    // Internal signals for connecting modules
    wire [`PWL_ADDR_WIDTH-1:0] segment_idx;
    wire addr_valid;
    wire [15:0] breakpoint_data, slope_data, intercept_data;
    wire memory_data_valid;
    
    // Pipelined control signals
    reg  x_valid_r;
    reg  [15:0] x_in_r;
    
    // Register input for timing
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            x_valid_r <= 1'b0;
            x_in_r <= 16'd0;
        end else begin
            x_valid_r <= x_valid;
            x_in_r <= x_in;
        end
    end
    
    // Address decoder module - determines which segment the input falls into
    pwl_address_decoder u_addr_decoder (
        .clk        (clk),
        .rst_n      (rst_n),
        .x_in       (x_in_r),
        .x_valid    (x_valid_r),
        .segment_idx(segment_idx),
        .addr_valid (addr_valid)
    );
    
    // Memory access control
    reg memory_read_en;
    reg [`PWL_ADDR_WIDTH-1:0] segment_idx_r;
    
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            memory_read_en <= 1'b0;
            segment_idx_r <= {`PWL_ADDR_WIDTH{1'b0}};
        end else begin
            memory_read_en <= addr_valid;
            segment_idx_r <= segment_idx;
        end
    end
    
    // Memory modules for PWL parameters
    breakpoints_rom u_breakpoints_rom (
        .clk        (clk),
        .addr       (segment_idx_r),
        .data_out   (breakpoint_data)
    );
    
    slopes_rom u_slopes_rom (
        .clk        (clk),
        .addr       (segment_idx_r),
        .data_out   (slope_data)
    );
    
    intercepts_rom u_intercepts_rom (
        .clk        (clk),
        .addr       (segment_idx_r),
        .data_out   (intercept_data)
    );
    
    // Pipeline valid signal for memory read
    reg memory_read_en_r;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            memory_read_en_r <= 1'b0;
        end else begin
            memory_read_en_r <= memory_read_en;
        end
    end
    
    assign memory_data_valid = memory_read_en_r;
    
    // Linear interpolator - performs the PWL calculation
    pwl_interpolator u_interpolator (
        .clk         (clk),
        .rst_n       (rst_n),
        .x_in        (x_in_r),
        .breakpoint  (breakpoint_data),
        .slope       (slope_data),
        .intercept   (intercept_data),
        .valid_in    (memory_data_valid),
        .y_out       (y_out),
        .valid_out   (y_valid)
    );

endmodule

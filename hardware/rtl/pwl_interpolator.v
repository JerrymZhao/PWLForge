//========================================================================
// pwl_interpolator.v - Linear Interpolation Unit
//========================================================================
`include "/vol/datastore/jmzhao/CompressedLUT/b-spline/testCPP/results/tanh/tanh_config.vh"

module pwl_interpolator (
    input  wire                 clk,
    input  wire                 rst_n,
    input  wire [15:0]          x_in,
    input  wire [15:0]          breakpoint,
    input  wire [15:0]          slope,
    input  wire [15:0]          intercept,
    input  wire                 valid_in,
    output reg  [15:0]          y_out,
    output reg                  valid_out
);

    // Pipeline stage 1 - Compute delta_x = x - breakpoint
    reg [15:0] delta_x, slope_r, intercept_r;
    reg valid_stage1;
    
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            delta_x <= 16'd0;
            slope_r <= 16'd0;
            intercept_r <= 16'd0;
            valid_stage1 <= 1'b0;
        end else begin
            if (valid_in) begin
                // Compute x difference from segment start
                delta_x <= x_in - breakpoint;
                // Pass through parameters
                slope_r <= slope;
                intercept_r <= intercept;
                valid_stage1 <= 1'b1;
            end else begin
                valid_stage1 <= 1'b0;
            end
        end
    end

    // Pipeline stage 2 - Compute slope * delta_x
    reg [31:0] slope_term;
    reg [15:0] intercept_r2;
    reg valid_stage2;
    
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            slope_term <= 32'd0;
            intercept_r2 <= 16'd0;
            valid_stage2 <= 1'b0;
        end else begin
            if (valid_stage1) begin
                // Compute slope * delta_x (fixed-point multiplication)
                slope_term <= delta_x * slope_r;
                intercept_r2 <= intercept_r;
                valid_stage2 <= 1'b1;
            end else begin
                valid_stage2 <= 1'b0;
            end
        end
    end
    
    // Pipeline stage 3 - Apply fractional shift and add intercept
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            y_out <= 16'd0;
            valid_out <= 1'b0;
        end else begin
            if (valid_stage2) begin
                // y = slope * (x - breakpoint) + intercept
                // Adjust for fixed-point representation
                y_out <= (slope_term >> `PWL_FRAC_BITS) + intercept_r2;
                valid_out <= 1'b1;
            end else begin
                valid_out <= 1'b0;
            end
        end
    end

endmodule

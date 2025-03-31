//========================================================================
// pwl_address_decoder.v - Segment Index Lookup Module
//========================================================================
`include "/vol/datastore/jmzhao/CompressedLUT/b-spline/testCPP/results/tanh/tanh_config.vh"

module pwl_address_decoder (
    input  wire                        clk,
    input  wire                        rst_n,
    input  wire [15:0]                 x_in,
    input  wire                        x_valid,
    output reg  [`PWL_ADDR_WIDTH-1:0] segment_idx,
    output reg                         addr_valid
);

    // Breakpoints memory for comparison
    // In actual implementation, this can be replaced with ROM IP
    reg [15:0] breakpoints [`PWL_NUM_BREAKPOINTS-1:0];
    
    // Initialize from included file
    initial begin
        $readmemh("../../results/tanh(x)/tanh_breakpoints.hex", breakpoints);
    end
    
    // Binary search parameters
    reg [`PWL_ADDR_WIDTH-1:0] left, right, mid;
    reg [2:0] state;
    
    // Binary search state machine states
    localparam IDLE = 3'd0;
    localparam INIT = 3'd1;
    localparam SEARCH = 3'd2;
    localparam DONE = 3'd3;
    
    // Binary search for segment index
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            segment_idx <= {`PWL_ADDR_WIDTH{1'b0}};
            addr_valid <= 1'b0;
            state <= IDLE;
            left <= {`PWL_ADDR_WIDTH{1'b0}};
            right <= {`PWL_ADDR_WIDTH{1'b0}};
            mid <= {`PWL_ADDR_WIDTH{1'b0}};
        end else begin
            case (state)
                IDLE: begin
                    addr_valid <= 1'b0;
                    if (x_valid) begin
                        state <= INIT;
                    end
                end
                
                INIT: begin
                    // Initialize binary search boundaries
                    left <= {`PWL_ADDR_WIDTH{1'b0}};
                    right <= `PWL_NUM_BREAKPOINTS - 1;
                    state <= SEARCH;
                end
                
                SEARCH: begin
                    if (left <= right) begin
                        mid <= (left + right) >> 1;
                        
                        // Check if x is in the current segment
                        if (x_in < breakpoints[mid]) begin
                            right <= mid - 1;
                        end else begin
                            left <= mid + 1;
                        end
                    end else begin
                        // Final segment index determination
                        segment_idx <= left;
                        state <= DONE;
                    end
                end
                
                DONE: begin
                    addr_valid <= 1'b1;
                    state <= IDLE;
                end
                
                default: state <= IDLE;
            endcase
        end
    end

endmodule

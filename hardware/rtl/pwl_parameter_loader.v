module pwl_parameter_loader #(
    parameter DATA_WIDTH = 16,
    parameter NUM_BREAKPOINTS = 32,
    parameter NUM_SEGMENTS = NUM_BREAKPOINTS + 1,
    parameter ADDR_WIDTH = $clog2(NUM_SEGMENTS),
    parameter MEM_INIT_FILE = "",   // 参数初始化文件基础名称
    parameter INIT_FROM_FILE = 0    // 是否从文件初始化
)(
    input  wire                    clk,
    input  wire                    rst_n,
    
    // 控制接口
    input  wire                    load_start,
    output reg                     load_done,
    
    // 参数加载接口
    input  wire                    param_valid,
    input  wire [1:0]              param_type,  // 0:断点, 1:斜率, 2:截距
    input  wire [ADDR_WIDTH-1:0]   param_addr,
    input  wire [DATA_WIDTH-1:0]   param_data,
    
    // 参数输出接口
    input  wire [ADDR_WIDTH-1:0]   bp_addr,
    output wire [DATA_WIDTH-1:0]   bp_data,
    input  wire [ADDR_WIDTH-1:0]   slope_addr,
    output wire [DATA_WIDTH-1:0]   slope_data,
    input  wire [ADDR_WIDTH-1:0]   intercept_addr,
    output wire [DATA_WIDTH-1:0]   intercept_data
);
    // 参数存储器 - 使用真双端RAM结构以支持同步读写
    (* ram_style = "block" *) reg [DATA_WIDTH-1:0] breakpoints [0:NUM_BREAKPOINTS-1];
    (* ram_style = "block" *) reg [DATA_WIDTH-1:0] slopes [0:NUM_SEGMENTS-1];
    (* ram_style = "block" *) reg [DATA_WIDTH-1:0] intercepts [0:NUM_SEGMENTS-1];
    
    // 加载状态机
    reg [1:0] state;
    localparam IDLE = 2'b00;
    localparam LOADING = 2'b01;
    localparam DONE = 2'b10;
    
    // 状态控制逻辑
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state <= IDLE;
            load_done <= 0;
        end else begin
            case (state)
                IDLE: begin
                    if (load_start) begin
                        state <= LOADING;
                        load_done <= 0;
                    end
                end
                
                LOADING: begin
                    if (param_valid) begin
                        case (param_type)
                            2'b00: breakpoints[param_addr] <= param_data;
                            2'b01: slopes[param_addr] <= param_data;
                            2'b10: intercepts[param_addr] <= param_data;
                        endcase
                    end else begin
                        state <= DONE;
                    end
                end
                
                DONE: begin
                    load_done <= 1;
                    state <= IDLE;
                end
            endcase
        end
    end
    
    // 注册参数读取输出，提高时序性能
    reg [DATA_WIDTH-1:0] bp_data_reg, slope_data_reg, intercept_data_reg;
    
    always @(posedge clk) begin
        bp_data_reg <= breakpoints[bp_addr];
        slope_data_reg <= slopes[slope_addr];
        intercept_data_reg <= intercepts[intercept_addr];
    end
    
    assign bp_data = bp_data_reg;
    assign slope_data = slope_data_reg;
    assign intercept_data = intercept_data_reg;
    
    // 从文件初始化(可选)
    generate
        if (INIT_FROM_FILE && MEM_INIT_FILE != "") begin: init_from_file
            initial begin
                $readmemh({MEM_INIT_FILE, "_breakpoints.mem"}, breakpoints);
                $readmemh({MEM_INIT_FILE, "_slopes.mem"}, slopes);
                $readmemh({MEM_INIT_FILE, "_intercepts.mem"}, intercepts);
            end
        end else begin: init_default
            integer i;
            initial begin
                for (i = 0; i < NUM_BREAKPOINTS; i = i + 1) begin
                    breakpoints[i] = 0;
                end
                for (i = 0; i < NUM_SEGMENTS; i = i + 1) begin
                    slopes[i] = 0;
                    intercepts[i] = 0;
                end
            end
        end
    endgenerate
endmodule

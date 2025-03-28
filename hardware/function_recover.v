`timescale 1ns/1ps

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

module pwl_core #(
    parameter DATA_WIDTH = 16,
    parameter FRAC_BITS = 14,
    parameter NUM_BREAKPOINTS = 32,
    parameter NUM_SEGMENTS = NUM_BREAKPOINTS + 1,
    parameter ADDR_WIDTH = $clog2(NUM_SEGMENTS),
    parameter USE_PIPELINE = 1,          // 启用流水线
    parameter SEARCH_TYPE = "BINARY",    // "LINEAR" 或 "BINARY"
    parameter USE_DSP = 1                // 使用DSP块进行乘法
)(
    input  wire                  clk,
    input  wire                  rst_n,
    input  wire                  valid_in,
    input  wire [DATA_WIDTH-1:0] x,
    
    // 参数查询接口
    output wire [ADDR_WIDTH-1:0] bp_addr,
    input  wire [DATA_WIDTH-1:0] bp_data,
    output wire [ADDR_WIDTH-1:0] slope_addr,
    input  wire [DATA_WIDTH-1:0] slope_data,
    output wire [ADDR_WIDTH-1:0] intercept_addr,
    input  wire [DATA_WIDTH-1:0] intercept_data,
    
    // 输出接口
    output wire                  valid_out,
    output wire [DATA_WIDTH-1:0] y
);
    // 内部寄存器和信号
    reg [ADDR_WIDTH-1:0] idx;
    reg [DATA_WIDTH-1:0] x_reg;
    
    // 流水线寄存器
    reg valid_pipe [0:2];
    reg [DATA_WIDTH-1:0] x_pipe [0:2];
    reg [ADDR_WIDTH-1:0] idx_pipe [0:1];
    
    // 搜索逻辑 - 实现分段查找
    generate
        if (SEARCH_TYPE == "BINARY") begin: binary_search
            // 优化的二分查找实现
            reg [ADDR_WIDTH-1:0] left, right, mid;
            reg [1:0] search_state;
            
            // 搜索状态机
            always @(posedge clk or negedge rst_n) begin
                if (!rst_n) begin
                    search_state <= 0;
                    left <= 0;
                    right <= NUM_BREAKPOINTS-1;
                    idx <= 0;
                    x_reg <= 0;
                end else if (valid_in) begin
                    // 重置搜索
                    search_state <= 1;
                    left <= 0;
                    right <= NUM_BREAKPOINTS-1;
                    x_reg <= x;
                end else case (search_state)
                    2'd1: begin // 计算中点并比较
                        mid <= (left + right) >> 1;
                        search_state <= 2;
                    end
                    
                    2'd2: begin // 更新搜索范围
                        if (x_reg >= bp_data) begin
                            left <= mid + 1;
                        end else begin
                            right <= mid;
                        end
                        
                        if ((right - left) <= 1) begin
                            search_state <= 3;
                        end else begin
                            search_state <= 1;
                            bp_addr <= mid;
                        end
                    end
                    
                    2'd3: begin // 完成搜索
                        idx <= (x_reg >= bp_data) ? (mid + 1) : mid;
                        search_state <= 0;
                    end
                    
                    default: search_state <= 0;
                endcase
            end
            
            assign bp_addr = (search_state == 1) ? mid : 
                            (search_state == 3) ? mid : 0;
                            
            assign slope_addr = idx_pipe[0];
            assign intercept_addr = idx_pipe[0];
            
        end else begin: linear_search
            // 线性搜索实现
            reg [ADDR_WIDTH-1:0] search_idx;
            reg [1:0] search_state;
            
            always @(posedge clk or negedge rst_n) begin
                if (!rst_n) begin
                    search_state <= 0;
                    search_idx <= 0;
                    idx <= 0;
                    x_reg <= 0;
                end else if (valid_in) begin
                    // 重置搜索
                    search_state <= 1;
                    search_idx <= 0;
                    idx <= 0;
                    x_reg <= x;
                end else case (search_state)
                    2'd1: begin // 线性搜索
                        if (search_idx < NUM_BREAKPOINTS) begin
                            bp_addr <= search_idx;
                            search_state <= 2;
                        end else begin
                            search_state <= 0;
                        end
                    end
                    
                    2'd2: begin // 比较
                        if (x_reg >= bp_data) begin
                            idx <= search_idx + 1;
                            search_idx <= search_idx + 1;
                            search_state <= 1;
                        end else begin
                            search_state <= 0;
                        end
                    end
                    
                    default: search_state <= 0;
                endcase
            end
            
            assign bp_addr = (search_state == 1) ? search_idx : 0;
            assign slope_addr = idx_pipe[0];
            assign intercept_addr = idx_pipe[0];
        end
    endgenerate
    
    // 流水线控制
    generate
        if (USE_PIPELINE) begin: pipeline_control
            always @(posedge clk or negedge rst_n) begin
                if (!rst_n) begin
                    valid_pipe[0] <= 0;
                    valid_pipe[1] <= 0;
                    valid_pipe[2] <= 0;
                end else begin
                    valid_pipe[0] <= (SEARCH_TYPE == "BINARY") ? (search_state == 0 && idx != 0) : 
                                    (search_state == 0 && idx != 0);
                    valid_pipe[1] <= valid_pipe[0];
                    valid_pipe[2] <= valid_pipe[1];
                end
            end
            
            // 索引传递
            always @(posedge clk) begin
                idx_pipe[0] <= idx;
                idx_pipe[1] <= idx_pipe[0];
            end
            
            // 输入值传递
            always @(posedge clk) begin
                x_pipe[0] <= x_reg;
                x_pipe[1] <= x_pipe[0];
                x_pipe[2] <= x_pipe[1];
            end
            
            assign valid_out = valid_pipe[2];
        end else begin: no_pipeline
            // 组合逻辑模式 (简化版，适用于低时钟频率场景)
            assign valid_out = 1'b1;
            assign slope_addr = idx;
            assign intercept_addr = idx;
        end
    endgenerate
    
    // 函数计算
    generate
        if (USE_PIPELINE) begin: pipelined_calc
            // 流水线乘法和加法 - 支持DSP选项
            reg [2*DATA_WIDTH-1:0] product;
            reg [DATA_WIDTH-1:0] result;
            
            // 根据配置选择乘法实现方式
            if (USE_DSP) begin: use_dsp_mult
                (* use_dsp = "yes" *)
                always @(posedge clk) begin
                    product <= slope_data * x_pipe[1];
                    result <= (product[FRAC_BITS+DATA_WIDTH-1:FRAC_BITS]) + intercept_data;
                end
            end else begin: use_logic_mult
                (* use_dsp = "no" *)
                always @(posedge clk) begin
                    product <= slope_data * x_pipe[1];
                    result <= (product[FRAC_BITS+DATA_WIDTH-1:FRAC_BITS]) + intercept_data;
                end
            end
            
            assign y = result;
        end else begin: combinational_calc
            // 组合逻辑乘法和加法 (简化版，仅适用于低时钟频率)
            wire [2*DATA_WIDTH-1:0] product = slope_data * x_reg;
            wire [DATA_WIDTH-1:0] sum = product[FRAC_BITS+DATA_WIDTH-1:FRAC_BITS] + intercept_data;
            
            assign y = sum;
        end
    endgenerate
endmodule

module pwl_system #(
    parameter DATA_WIDTH = 16,
    parameter FRAC_BITS = 14,
    parameter NUM_BREAKPOINTS = 32,
    parameter NUM_SEGMENTS = NUM_BREAKPOINTS + 1,
    parameter ADDR_WIDTH = $clog2(NUM_SEGMENTS),
    parameter USE_PIPELINE = 1,
    parameter SEARCH_TYPE = "BINARY",    // "LINEAR" 或 "BINARY"
    parameter USE_DSP = 1,
    parameter MEM_INIT_FILE = "",        // 参数初始化文件
    parameter INIT_FROM_FILE = 0         // 是否从文件初始化
)(
    input  wire                    clk,
    input  wire                    rst_n,
    
    // 参数加载接口
    input  wire                    load_start,
    input  wire                    param_valid,
    input  wire [1:0]              param_type,
    input  wire [ADDR_WIDTH-1:0]   param_addr,
    input  wire [DATA_WIDTH-1:0]   param_data,
    output wire                    load_done,
    
    // 函数输入/输出接口
    input  wire                    input_valid,
    input  wire [DATA_WIDTH-1:0]   x_in,
    output wire                    output_valid,
    output wire [DATA_WIDTH-1:0]   y_out
);
    // 内部连接信号
    wire [ADDR_WIDTH-1:0] bp_addr, slope_addr, intercept_addr;
    wire [DATA_WIDTH-1:0] bp_data, slope_data, intercept_data;
    
    // 参数加载模块实例
    pwl_parameter_loader #(
        .DATA_WIDTH(DATA_WIDTH),
        .NUM_BREAKPOINTS(NUM_BREAKPOINTS),
        .NUM_SEGMENTS(NUM_SEGMENTS),
        .ADDR_WIDTH(ADDR_WIDTH),
        .MEM_INIT_FILE(MEM_INIT_FILE),
        .INIT_FROM_FILE(INIT_FROM_FILE)
    ) param_loader (
        .clk(clk),
        .rst_n(rst_n),
        .load_start(load_start),
        .load_done(load_done),
        .param_valid(param_valid),
        .param_type(param_type),
        .param_addr(param_addr),
        .param_data(param_data),
        .bp_addr(bp_addr),
        .bp_data(bp_data),
        .slope_addr(slope_addr),
        .slope_data(slope_data),
        .intercept_addr(intercept_addr),
        .intercept_data(intercept_data)
    );
    
    // PWL核心模块实例
    pwl_core #(
        .DATA_WIDTH(DATA_WIDTH),
        .FRAC_BITS(FRAC_BITS),
        .NUM_BREAKPOINTS(NUM_BREAKPOINTS),
        .NUM_SEGMENTS(NUM_SEGMENTS),
        .ADDR_WIDTH(ADDR_WIDTH),
        .USE_PIPELINE(USE_PIPELINE),
        .SEARCH_TYPE(SEARCH_TYPE),
        .USE_DSP(USE_DSP)
    ) pwl_processor (
        .clk(clk),
        .rst_n(rst_n),
        .valid_in(input_valid),
        .x(x_in),
        .bp_addr(bp_addr),
        .bp_data(bp_data),
        .slope_addr(slope_addr),
        .slope_data(slope_data),
        .intercept_addr(intercept_addr),
        .intercept_data(intercept_data),
        .valid_out(output_valid),
        .y(y_out)
    );
endmodule

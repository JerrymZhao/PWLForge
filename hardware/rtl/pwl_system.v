module pwl_system #(
    parameter DATA_WIDTH = 16,
    parameter FRAC_BITS = 14,
    parameter NUM_BREAKPOINTS = 32,
    parameter NUM_SEGMENTS = NUM_BREAKPOINTS + 1,
    parameter ADDR_WIDTH = $clog2(NUM_SEGMENTS),
    parameter USE_PIPELINE = 1,
    parameter SEARCH_TYPE = "BINARY",    // "LINEAR" 或 "BINARY"
    parameter USE_DSP = 1,               // 使用DSP块进行乘法
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

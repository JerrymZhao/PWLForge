module pwl_recovery (
    input  wire [15:0] x,
    output wire [15:0] y
);
    // 实例化通用模块，但保持与原始接口兼容
    // 注：这是组合逻辑版本，无需时钟和复位
    pwl_system #(
        .DATA_WIDTH(16),
        .FRAC_BITS(14),
        .NUM_BREAKPOINTS(21),
        .NUM_SEGMENTS(22),
        .USE_PIPELINE(0),        // 禁用流水线以支持组合逻辑
        .SEARCH_TYPE("LINEAR"),  // 使用线性搜索
        .INIT_FROM_FILE(1),      // 从文件初始化
        .MEM_INIT_FILE("pwl_params")
    ) pwl_compat_inst (
        .clk(1'b0),              // 未使用
        .rst_n(1'b1),            // 未使用
        .load_start(1'b0),       // 未使用
        .param_valid(1'b0),      // 未使用
        .param_type(2'b0),       // 未使用
        .param_addr(0),          // 未使用
        .param_data(0),          // 未使用
        .load_done(),            // 未使用
        .input_valid(1'b1),      // 始终有效
        .x_in(x),
        .output_valid(),         // 未使用
        .y_out(y)
    );
    
    // 注：此实现需要提供三个参数文件:
    // pwl_params_breakpoints.mem
    // pwl_params_slopes.mem
    // pwl_params_intercepts.mem
endmodule

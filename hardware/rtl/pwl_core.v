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
            // 有效信号传递
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

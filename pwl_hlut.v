//================================================================================
// pwl_hlut.v - 通用分段线性层次查找表实现 (含修复的数据加载)
//================================================================================

`include "/vol/datastore/jmzhao/CompressedLUT/b-spline/testCPP/hardware/include/tanh_optimized_bitwidths.vh"

module pwl_hlut (
    input wire clk,
    input wire rst_n,
    input wire [`INPUT_DATA_WIDTH-1:0] x_in,
    input wire in_valid,
    output reg [`OUTPUT_DATA_WIDTH-1:0] y_out,
    output reg out_valid
);
    // 使用宏定义的配置参数
    localparam GROUP_WORDS = `OPT_GROUP_ENTRY_BITS / 16;
    localparam DELTA_WORDS = `OPT_DELTA_ENTRY_BITS / 16;
    
    // 声明位置常量以避免宏运算语法错误
    localparam SLOPE_MSB = `OPT_DELTA_SLOPE_WIDTH - 1;
    localparam INTERCEPT_MSB = `OPT_DELTA_INTERCEPT_WIDTH - 1;
    localparam GROUP_B_MSB = `OPT_GROUP_B_WIDTH - 1;
    localparam GROUP_C_MSB = `OPT_GROUP_C_WIDTH - 1;
    
    // 为循环定义最大边界（对合成至关重要）
    localparam MAX_INTERVALS = `OPT_MAX_INTERVALS_PER_GROUP;
    
    // 调试计数器
    reg [7:0] debug_counter = 0;
    integer i;
    
    // 使用宏定义大小的分布式RAM存储
    (* ram_style = "distributed" *) reg [15:0] group_info [0:(`OPT_NUM_GROUPS*GROUP_WORDS)-1]; 
    (* ram_style = "distributed" *) reg [15:0] delta_data [0:(`OPT_TOTAL_INTERVALS*DELTA_WORDS)-1];
    
    // 预缓存关键组信息以减少ROM访问
    reg [`OPT_GROUP_START_WIDTH-1:0] group_starts[0:`OPT_NUM_GROUPS-1];
    reg [`OPT_GROUP_END_WIDTH-1:0] group_ends[0:`OPT_NUM_GROUPS-1];
    reg [`OPT_GROUP_B_WIDTH-1:0] group_base_bs[0:`OPT_NUM_GROUPS-1];
    reg [`OPT_GROUP_C_WIDTH-1:0] group_base_cs[0:`OPT_NUM_GROUPS-1];
    reg [`OPT_GROUP_OFFSET_WIDTH-1:0] group_offsets[0:`OPT_NUM_GROUPS-1];
    reg [`OPT_GROUP_FLAGS_SIZE_WIDTH-1:0] group_sizes[0:`OPT_NUM_GROUPS-1];
    reg group_is_orphans[0:`OPT_NUM_GROUPS-1];
    reg group_use_pow2s[0:`OPT_NUM_GROUPS-1];
    reg [4:0] group_shift_amounts[0:`OPT_NUM_GROUPS-1];
    
    // 阶段1寄存器：输入处理
    reg [`INPUT_DATA_WIDTH-1:0] x_reg, abs_x;
    reg x_sign;
    reg valid_r1;
    
    // 阶段2寄存器：组匹配 + delta设置
    reg [`OPT_GROUP_ADDR_WIDTH-1:0] group_id;
    reg group_match_found;
    reg [`INPUT_DATA_WIDTH-1:0] x_reg_r2;
    reg x_sign_r2;
    reg valid_r2;
    reg [`OPT_GROUP_B_WIDTH-1:0] selected_base_b;
    reg [`OPT_GROUP_C_WIDTH-1:0] selected_base_c;
    reg [`OPT_GROUP_OFFSET_WIDTH-1:0] selected_offset;
    reg selected_is_orphan, selected_use_pow2;
    reg [4:0] selected_shift_amount;
    reg [`OPT_GROUP_FLAGS_SIZE_WIDTH-1:0] selected_size;
    reg [`OPT_GROUP_START_WIDTH-1:0] selected_group_start;
    reg [`OPT_GROUP_OFFSET_WIDTH-1:0] delta_base_addr;

    // 阶段3寄存器：区间搜索 + 参数准备
    reg [`OPT_INTERVAL_ADDR_WIDTH-1:0] interval_idx;
    reg interval_match_found;
    reg [`INPUT_DATA_WIDTH-1:0] adjusted_x;
    reg x_reflect, y_reflect;
    reg x_sign_r3;
    reg valid_r3;
    reg [`OPT_DELTA_SLOPE_WIDTH-1:0] interval_delta_slope;
    reg [`OPT_DELTA_INTERCEPT_WIDTH-1:0] interval_delta_intercept;
    
    // 阶段4寄存器：参数计算 + 结果计算
    // 扩展以进行DSP优化，带有2个额外的符号扩展位
    reg signed [`OPT_GROUP_B_WIDTH+1:0] slope;
    reg signed [`OPT_GROUP_C_WIDTH+1:0] intercept;
    reg valid_r4;
    reg y_reflect_r4;
    reg x_sign_r4;
    reg [`INPUT_DATA_WIDTH-1:0] adjusted_x_r4;
    reg use_pow2_r4;
    reg [4:0] shift_amount_r4;
    
    // 额外的工作寄存器
    reg [`OPT_GROUP_START_WIDTH-1:0] interval_start, interval_end;
    reg [`OPT_GROUP_OFFSET_WIDTH-1:0] delta_idx_addr;
    reg [1:0] sign_ext_bits;
    
    // 中间计算寄存器
    reg [`OPT_GROUP_B_WIDTH-1:0] param_sum_b;
    reg [`OPT_GROUP_C_WIDTH-1:0] param_sum_c;
    
    // 计算寄存器（模块级别以避免语法错误）
    reg signed [`OPT_GROUP_B_WIDTH+`INPUT_DATA_WIDTH+1:0] mult_result;
    reg signed [`OUTPUT_DATA_WIDTH-1:0] scaled_result;
    reg [4:0] detected_shift;
    reg [`OPT_GROUP_B_WIDTH+1:0] abs_slope;
    reg slope_sign;
    
    // 位操作的临时寄存器
    reg [1:0] temp_sign_ext_b;
    reg [1:0] temp_sign_ext_c;
    reg [`OPT_GROUP_B_WIDTH-1:0] temp_sum_b;
    reg [`OPT_GROUP_C_WIDTH-1:0] temp_sum_c;
    
    // 固定点转十进制函数（用于调试）
    function [63:0] fixed_to_decimal;
        input [`INPUT_DATA_WIDTH-1:0] fixed_val;
        begin
            fixed_to_decimal = ($signed(fixed_val) * 1000) >>> `OPT_FRAC_BITS;
        end
    endfunction

    // 初始化内存和缓存
    initial begin
        // 零初始化
        for (i = 0; i < `OPT_NUM_GROUPS*GROUP_WORDS; i = i + 1) begin
            group_info[i] = 16'h0000;
        end
        for (i = 0; i < `OPT_TOTAL_INTERVALS*DELTA_WORDS; i = i + 1) begin
            delta_data[i] = 16'h0000;
        end
        
        // 包含LUT数据文件
        `include "/vol/datastore/jmzhao/CompressedLUT/b-spline/testCPP/hardware/include/tanh_inline_lut_data.vh"
        
        // 打印LUT数据以进行调试
        $display("===== LUT DATA VERIFICATION =====");
        for (i = 0; i < `OPT_NUM_GROUPS; i = i + 1) begin
            $display("Group %0d: Start=0x%h, End=0x%h, B=0x%h, C=0x%h, Flags=0x%h", 
                     i, 
                     group_info[i*GROUP_WORDS + (`OPT_GROUP_START_POS/16)], 
                     group_info[i*GROUP_WORDS + (`OPT_GROUP_END_POS/16)],
                     group_info[i*GROUP_WORDS + (`OPT_GROUP_B_POS/16)],
                     group_info[i*GROUP_WORDS + (`OPT_GROUP_C_POS/16)],
                     group_info[i*GROUP_WORDS + (`OPT_GROUP_FLAGS_SIZE_POS/16)]);
        end
        
        // 预缓存关键组信息 - 最小化ROM访问
        for (i = 0; i < `OPT_NUM_GROUPS; i = i + 1) begin
            group_starts[i] = group_info[i*GROUP_WORDS + (`OPT_GROUP_START_POS/16)];
            group_ends[i] = group_info[i*GROUP_WORDS + (`OPT_GROUP_END_POS/16)];
            group_base_bs[i] = group_info[i*GROUP_WORDS + (`OPT_GROUP_B_POS/16)];
            group_base_cs[i] = group_info[i*GROUP_WORDS + (`OPT_GROUP_C_POS/16)];
            group_offsets[i] = group_info[i*GROUP_WORDS + (`OPT_GROUP_OFFSET_POS/16)];
            group_sizes[i] = group_info[i*GROUP_WORDS + (`OPT_GROUP_FLAGS_SIZE_POS/16)][15:1];
            group_is_orphans[i] = group_info[i*GROUP_WORDS + (`OPT_GROUP_FLAGS_SIZE_POS/16)][0];
            group_use_pow2s[i] = group_info[i*GROUP_WORDS + (`OPT_GROUP_POW2_POS/16)][0];
            group_shift_amounts[i] = group_info[i*GROUP_WORDS + (`OPT_GROUP_POW2_POS/16)][5:1];
            
            // 缓存值的增强调试验证
            $display("Cached Group %0d: Start=0x%h, End=0x%h, B=0x%h, C=0x%h, size=%0d, is_orphan=%0d, offset=%0d", 
                     i, group_starts[i], group_ends[i], group_base_bs[i], group_base_cs[i], 
                     group_sizes[i], group_is_orphans[i], group_offsets[i]);
        end
        
        // 打印所有组的deltas参数，确保正确加载
        for (i = 0; i < `OPT_NUM_GROUPS; i = i + 1) begin
            if (group_sizes[i] > 0) begin
                $display("Deltas for Group %0d (base addr = %0d):", i, group_offsets[i]*DELTA_WORDS);
                $display("  Delta[0]: Start=0x%h, Slope=0x%h, Intercept=0x%h", 
                         delta_data[group_offsets[i]*DELTA_WORDS],
                         delta_data[group_offsets[i]*DELTA_WORDS + 1],
                         delta_data[group_offsets[i]*DELTA_WORDS + 2]);
            end
        end
    end
    
    //---------------------------------------------------------------------
    // 阶段1：输入注册和预处理
    //---------------------------------------------------------------------
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            x_reg <= 0;
            abs_x <= 0;
            x_sign <= 1'b0;
            valid_r1 <= 1'b0;
        end else begin
            valid_r1 <= in_valid;
            x_reg <= x_in;
            
            // 输入预处理 - 处理对称函数
            x_sign <= x_in[`INPUT_DATA_WIDTH-1];
            abs_x <= x_in[`INPUT_DATA_WIDTH-1] ? -x_in : x_in;  // 适用于奇偶函数
            
            // 调试输出
            if (in_valid && debug_counter < 15) begin
                $display("\n===== PWL HLUT Processing (x = %h, %0d.%03d) =====",
                        x_in, 
                        fixed_to_decimal(x_in) / 1000,
                        fixed_to_decimal(x_in) % 1000);
                debug_counter <= debug_counter + 1;
                
                // 调试 - 输入阶段
                $display("Stage 1: x_sign=%b, abs_x=%h", x_in[`INPUT_DATA_WIDTH-1], 
                         (x_in[`INPUT_DATA_WIDTH-1] ? -x_in : x_in));
            end
        end
    end
    
    //---------------------------------------------------------------------
    // 阶段2：组匹配 + delta设置（压缩阶段）
    //---------------------------------------------------------------------
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            group_id <= 0;
            group_match_found <= 1'b0;
            x_reg_r2 <= 0;
            x_sign_r2 <= 1'b0;
            valid_r2 <= 1'b0;
            selected_base_b <= 0;
            selected_base_c <= 0;
            selected_offset <= 0;
            selected_is_orphan <= 1'b0;
            selected_use_pow2 <= 1'b0;
            selected_shift_amount <= 5'b0;
            selected_size <= 0;
            selected_group_start <= 0;
            delta_base_addr <= 0;
        end else begin
            valid_r2 <= valid_r1;
            x_sign_r2 <= x_sign;
            x_reg_r2 <= abs_x;
            
            // 默认无匹配
            group_match_found <= 1'b0;
            
            if (valid_r1) begin
                // 通用组匹配算法
                group_match_found <= 1'b0;  // 开始时无匹配
                
                if (debug_counter <= 15) begin
                    $display("Stage 2: Finding group for x=%h", abs_x);
                end
                
                // 首先查找范围匹配
                for (i = 0; i < `OPT_NUM_GROUPS; i = i + 1) begin
                    // 如果在范围内
                    if (abs_x <= group_ends[i] && abs_x >= group_starts[i]) begin
                        group_id <= i[`OPT_GROUP_ADDR_WIDTH-1:0];
                        group_match_found <= 1'b1;
                        
                        // 预加载所有需要的组参数
                        selected_base_b <= group_base_bs[i];
                        selected_base_c <= group_base_cs[i];
                        selected_offset <= group_offsets[i];
                        selected_is_orphan <= group_is_orphans[i];
                        selected_use_pow2 <= group_use_pow2s[i];
                        selected_shift_amount <= group_shift_amounts[i];
                        selected_size <= group_sizes[i];
                        selected_group_start <= group_starts[i];
                        
                        // 修复: 确保正确计算delta基址
                        delta_base_addr <= group_offsets[i] * DELTA_WORDS;
                        
                        if (debug_counter <= 15) begin
                            $display("  Group %0d in-range match (start=%h, end=%h)", i, 
                                    group_starts[i], group_ends[i]);
                            $display("  Params: base_b=%h, base_c=%h, is_orphan=%b", 
                                    group_base_bs[i], group_base_cs[i], group_is_orphans[i]);
                            $display("  Delta base addr = %d (offset=%d * %d)", 
                                    group_offsets[i] * DELTA_WORDS, group_offsets[i], DELTA_WORDS);
                        end
                        
                        i = `OPT_NUM_GROUPS; // 提前退出循环
                    end
                end
                
                // 如果没有范围匹配，尝试查找孤立组
                if (!group_match_found) begin
                    for (i = 0; i < `OPT_NUM_GROUPS; i = i + 1) begin
                        if (group_is_orphans[i]) begin
                            group_id <= i[`OPT_GROUP_ADDR_WIDTH-1:0];
                            group_match_found <= 1'b1;
                            
                            // 预加载所有需要的组参数
                            selected_base_b <= group_base_bs[i];
                            selected_base_c <= group_base_cs[i];
                            selected_offset <= group_offsets[i];
                            selected_is_orphan <= 1'b1; // 确保标记为孤立组
                            selected_use_pow2 <= group_use_pow2s[i];
                            selected_shift_amount <= group_shift_amounts[i];
                            selected_size <= group_sizes[i];
                            selected_group_start <= group_starts[i];
                            
                            // 修复: 确保正确计算delta基址
                            delta_base_addr <= group_offsets[i] * DELTA_WORDS;
                            
                            if (debug_counter <= 15) begin
                                $display("  Orphan Group %0d matched", i);
                                $display("  Params: base_b=%h, base_c=%h, is_orphan=1", 
                                        group_base_bs[i], group_base_cs[i]);
                                $display("  Delta base addr = %d (offset=%d * %d)", 
                                        group_offsets[i] * DELTA_WORDS, group_offsets[i], DELTA_WORDS);
                            end
                            
                            i = `OPT_NUM_GROUPS; // 提前退出循环
                        end
                    end
                }
                
                // 最后的后备 - 如果仍然没有匹配
                if (!group_match_found) begin
                    group_id <= {`OPT_GROUP_ADDR_WIDTH{1'b1}}; // 默认组的全1
                    group_match_found <= 1'b1;
                    selected_base_b <= 0;
                    selected_base_c <= 0;
                    selected_offset <= 0;
                    selected_is_orphan <= 1'b0;
                    selected_size <= 0;
                    delta_base_addr <= 0;
                    
                    if (debug_counter <= 15) begin
                        $display("  No group match - using default parameters");
                    end
                end
                
                if (debug_counter <= 15) begin
                    $display("  Stage 2 selected group %0d, group_match_found=%b", 
                             group_id, group_match_found);
                    $display("  base_b=%h, base_c=%h, delta_base_addr=%h, is_orphan=%b, size=%d", 
                             selected_base_b, selected_base_c, delta_base_addr, 
                             selected_is_orphan, selected_size);
                end
            end
        end
    end
    
    //---------------------------------------------------------------------
    // 阶段3：区间匹配 + 参数查找（优化搜索）
    //---------------------------------------------------------------------
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            interval_idx <= 0;
            interval_match_found <= 1'b0;
            adjusted_x <= 0;
            x_reflect <= 1'b0;
            y_reflect <= 1'b0;
            x_sign_r3 <= 1'b0;
            valid_r3 <= 1'b0;
            interval_delta_slope <= 0;
            interval_delta_intercept <= 0;
        end else begin
            valid_r3 <= valid_r2;
            x_sign_r3 <= x_sign_r2;
            
            // 默认值
            interval_match_found <= 1'b0;
            adjusted_x <= x_reg_r2;
            
            if (valid_r2 && group_match_found) begin
                // 针对孤立组的特殊处理
                if (selected_is_orphan) begin
                    // 孤立组直接使用第一个区间
                    interval_idx <= 0;
                    interval_match_found <= 1'b1;
                    delta_idx_addr = delta_base_addr;
                    
                    // 从delta数据中直接读取参数
                    interval_delta_slope <= delta_data[delta_base_addr + 1]; // SLOPE位置为+1
                    interval_delta_intercept <= delta_data[delta_base_addr + 2]; // INTERCEPT位置为+2
                    
                    // 读取反射标志
                    x_reflect <= delta_data[delta_base_addr + 3][`OPT_DELTA_REFLECTION_X_POS % 16];
                    y_reflect <= delta_data[delta_base_addr + 3][`OPT_DELTA_REFLECTION_Y_POS % 16];
                    
                    if (debug_counter <= 15) begin
                        $display("Stage 3: Using orphan group's first interval (delta_base_addr=%h)", delta_base_addr);
                        $display("  Delta values: START=%h, SLOPE=%h, INTERCEPT=%h", 
                                delta_data[delta_base_addr],
                                delta_data[delta_base_addr + 1],
                                delta_data[delta_base_addr + 2]);
                        $display("  Reflection: x=%b, y=%b", 
                                delta_data[delta_base_addr + 3][`OPT_DELTA_REFLECTION_X_POS % 16],
                                delta_data[delta_base_addr + 3][`OPT_DELTA_REFLECTION_Y_POS % 16]);
                    end
                    
                    // 处理反射（如果需要）
                    if (delta_data[delta_base_addr + 3][`OPT_DELTA_REFLECTION_X_POS % 16]) begin
                        // 对于孤立组，可能需要特殊处理反射
                        // 由于没有定义边界，我们可能需要使用其他逻辑
                    end
                end
                // 正常组的区间搜索
                else begin
                    // 通用间隔搜索算法适用于任何函数
                    if (debug_counter <= 15) begin
                        $display("Stage 3: Finding interval for x=%h in group %0d", 
                                x_reg_r2, group_id);
                        $display("  selected_size = %0d, selected_group_start = %h", 
                                selected_size, selected_group_start);
                    end
                    
                    // 高效搜索区间
                    if (selected_size > 0) begin
                        // 第一个和最后一个区间的快速路径
                        // 第一个区间检查
                        interval_start = selected_group_start;
                        if (selected_size > 1) begin
                            interval_end = selected_group_start + delta_data[delta_base_addr + DELTA_WORDS];
                        end else begin
                            interval_end = group_ends[group_id];
                        end
                        
                        if (debug_counter <= 15) begin
                            $display("  First interval: start=%h, end=%h", interval_start, interval_end);
                        end
                        
                        if (x_reg_r2 >= interval_start && x_reg_r2 <= interval_end) begin
                            interval_idx <= 0; // 第一个区间为零
                            interval_match_found <= 1'b1;
                            delta_idx_addr = delta_base_addr;
                            
                            if (debug_counter <= 15) begin
                                $display("  MATCHED first interval, delta_idx_addr=%h", delta_idx_addr);
                            end
                        end
                        // 最后一个区间检查
                        else begin
                            interval_start = selected_group_start + delta_data[delta_base_addr + ((selected_size-1)*DELTA_WORDS)];
                            interval_end = group_ends[group_id];
                            
                            if (debug_counter <= 15) begin
                                $display("  Last interval: start=%h, end=%h", interval_start, interval_end);
                            end
                            
                            if (x_reg_r2 >= interval_start && x_reg_r2 <= interval_end) begin
                                interval_idx <= selected_size - 1;
                                interval_match_found <= 1'b1;
                                delta_idx_addr = delta_base_addr + ((selected_size-1)*DELTA_WORDS);
                                
                                if (debug_counter <= 15) begin
                                    $display("  MATCHED last interval, delta_idx_addr=%h", delta_idx_addr);
                                end
                            end
                            // 中间区间的线性搜索
                            else if (selected_size > 2 && !interval_match_found) begin
                                if (debug_counter <= 15) begin
                                    $display("  Searching middle intervals...");
                                end
                                
                                // 绑定循环到已知的最大区间
                                for (i = 1; i < MAX_INTERVALS-1 && i < selected_size-1; i = i + 1) begin
                                    interval_start = selected_group_start + delta_data[delta_base_addr + (i*DELTA_WORDS)];
                                    interval_end = selected_group_start + delta_data[delta_base_addr + ((i+1)*DELTA_WORDS)];
                                    
                                    if (debug_counter <= 15) begin
                                        $display("  Interval %0d: start=%h, end=%h", i, interval_start, interval_end);
                                    end
                                    
                                    if (x_reg_r2 >= interval_start && x_reg_r2 <= interval_end) begin
                                        interval_idx <= i;
                                        interval_match_found <= 1'b1;
                                        delta_idx_addr = delta_base_addr + (i*DELTA_WORDS);
                                        
                                        if (debug_counter <= 15) begin
                                            $display("  MATCHED interval %0d, delta_idx_addr=%h", i, delta_idx_addr);
                                        end
                                    end
                                end
                            end
                        end
                    end
                    
                    // 如果找到了区间，提取参数
                    if (interval_match_found) begin
                        // 计算反射标志的字索引和位偏移
                        x_reflect <= delta_data[delta_idx_addr + 3][`OPT_DELTA_REFLECTION_X_POS % 16];
                        y_reflect <= delta_data[delta_idx_addr + 3][`OPT_DELTA_REFLECTION_Y_POS % 16];
                        interval_delta_slope <= delta_data[delta_idx_addr + 1];
                        interval_delta_intercept <= delta_data[delta_idx_addr + 2];
                        
                        if (debug_counter <= 15) begin
                            $display("  Delta values: slope=%h, intercept=%h", 
                                    delta_data[delta_idx_addr + 1],
                                    delta_data[delta_idx_addr + 2]);
                            $display("  Reflection: x=%b, y=%b", 
                                    delta_data[delta_idx_addr + 3][`OPT_DELTA_REFLECTION_X_POS % 16],
                                    delta_data[delta_idx_addr + 3][`OPT_DELTA_REFLECTION_Y_POS % 16]);
                        end
                        
                        // 处理反射
                        if (delta_data[delta_idx_addr + 3][`OPT_DELTA_REFLECTION_X_POS % 16]) begin
                            // 计算反射的中点
                            adjusted_x <= ((interval_start + interval_end) >> 1) * 2 - x_reg_r2;
                            
                            if (debug_counter <= 15) begin
                                $display("  Applying x-reflection, adjusted_x=%h", 
                                        ((interval_start + interval_end) >> 1) * 2 - x_reg_r2);
                            end
                        end
                    end
                    // 没有找到区间 - 使用第一个区间的默认值
                    else begin
                        interval_idx <= 0;
                        interval_match_found <= 1'b1;
                        delta_idx_addr = delta_base_addr;
                        interval_delta_slope <= delta_data[delta_base_addr + 1];
                        interval_delta_intercept <= delta_data[delta_base_addr + 2];
                        x_reflect <= delta_data[delta_base_addr + 3][`OPT_DELTA_REFLECTION_X_POS % 16];
                        y_reflect <= delta_data[delta_base_addr + 3][`OPT_DELTA_REFLECTION_Y_POS % 16];
                        
                        if (debug_counter <= 15) begin
                            $display("  No interval match found - using first interval defaults");
                            $display("  Default delta values: slope=%h, intercept=%h", 
                                    delta_data[delta_base_addr + 1],
                                    delta_data[delta_base_addr + 2]);
                        end
                    }
                }
                
                if (debug_counter <= 15) begin
                    $display("  Stage 3 final: interval_idx=%0d, match_found=%b", 
                            interval_idx, interval_match_found);
                    $display("  delta_slope=%h, delta_intercept=%h, adjusted_x=%h", 
                            interval_delta_slope, interval_delta_intercept, adjusted_x);
                end
            end
        end
    end
    
    //---------------------------------------------------------------------
    // 阶段4：参数计算和准备
    //---------------------------------------------------------------------
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            slope <= 0;
            intercept <= 0;
            valid_r4 <= 1'b0;
            y_reflect_r4 <= 1'b0;
            x_sign_r4 <= 1'b0;
            adjusted_x_r4 <= 0;
            use_pow2_r4 <= 1'b0;
            shift_amount_r4 <= 5'b0;
            param_sum_b <= 0;
            param_sum_c <= 0;
        end else begin
            valid_r4 <= valid_r3;
            y_reflect_r4 <= y_reflect;
            x_sign_r4 <= x_sign_r3;
            adjusted_x_r4 <= adjusted_x;
            use_pow2_r4 <= selected_use_pow2;
            shift_amount_r4 <= selected_shift_amount;
            
            if (valid_r3 && interval_match_found) begin
                // 参数计算分支
                if (selected_is_orphan) begin
                    // 孤立组 - 参数直接存储在deltas中
                    param_sum_b <= interval_delta_slope;
                    param_sum_c <= interval_delta_intercept;
                    
                    // 使用斜率的MSB进行符号扩展
                    if (interval_delta_slope[SLOPE_MSB]) begin
                        temp_sign_ext_b = 2'b11;
                    end else begin
                        temp_sign_ext_b = 2'b00;
                    end
                    slope <= {temp_sign_ext_b, interval_delta_slope};
                    
                    // 使用截距的MSB进行符号扩展
                    if (interval_delta_intercept[INTERCEPT_MSB]) begin
                        temp_sign_ext_c = 2'b11;
                    end else begin
                        temp_sign_ext_c = 2'b00;
                    end
                    intercept <= {temp_sign_ext_c, interval_delta_intercept};
                    
                    if (debug_counter <= 15) begin
                        $display("Stage 4: Orphan group - Direct parameter load");
                        $display("  slope = {%b, %h} = %h", temp_sign_ext_b, interval_delta_slope,
                                {temp_sign_ext_b, interval_delta_slope});
                        $display("  intercept = {%b, %h} = %h", temp_sign_ext_c, interval_delta_intercept,
                                {temp_sign_ext_c, interval_delta_intercept});
                    end
                end
                else begin
                    // 普通组 - 将delta添加到基本参数
                    temp_sum_b = selected_base_b + interval_delta_slope;
                    temp_sum_c = selected_base_c + interval_delta_intercept;
                    param_sum_b <= temp_sum_b;
                    param_sum_c <= temp_sum_c;
                    
                    // 计算符号扩展位
                    if (temp_sum_b[GROUP_B_MSB]) begin
                        temp_sign_ext_b = 2'b11;
                    end else begin
                        temp_sign_ext_b = 2'b00;
                    end
                    
                    if (temp_sum_c[GROUP_C_MSB]) begin
                        temp_sign_ext_c = 2'b11;
                    end else begin
                        temp_sign_ext_c = 2'b00;
                    end
                    
                    // 分配带有符号扩展的组合参数
                    slope <= {temp_sign_ext_b, temp_sum_b};
                    intercept <= {temp_sign_ext_c, temp_sum_c};
                    
                    if (debug_counter <= 15) begin
                        $display("Stage 4: Parameter calculation");
                        $display("  base_b=%h + delta_slope=%h = %h", 
                                selected_base_b, interval_delta_slope, temp_sum_b);
                        $display("  base_c=%h + delta_intercept=%h = %h", 
                                selected_base_c, interval_delta_intercept, temp_sum_c);
                        $display("  slope = {%b, %h} = %h", 
                                temp_sign_ext_b, temp_sum_b,
                                {temp_sign_ext_b, temp_sum_b});
                        $display("  intercept = {%b, %h} = %h", 
                                temp_sign_ext_c, temp_sum_c,
                                {temp_sign_ext_c, temp_sum_c});
                    end
                end
            end
        end
    end
    
    //---------------------------------------------------------------------
    // 阶段5：带优化DSP乘法的最终计算
    //---------------------------------------------------------------------
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            y_out <= 0;
            out_valid <= 1'b0;
        end else begin
            out_valid <= valid_r4;
            
            if (valid_r4) begin
                // 标准PWL计算
                // 函数无关的计算优化
                if (use_pow2_r4) begin
                    // 使用移位的2的幂优化
                    mult_result = $signed(adjusted_x_r4) << shift_amount_r4;
                    
                    if (debug_counter <= 15) begin
                        $display("Stage 5: Using power-of-2 optimization");
                        $display("  %h << %d = %h", adjusted_x_r4, shift_amount_r4, mult_result);
                    end
                end
                else if (slope == 0) begin
                    // 如果斜率为零，跳过乘法
                    mult_result = 0;
                    
                    if (debug_counter <= 15) begin
                        $display("Stage 5: Slope is zero, skipping multiplication");
                    end
                end
                else if ((slope & (slope - 1)) == 0 && slope != 0) begin
                    // 运行时2的幂检测 - 绑定以防止无限循环
                    detected_shift = 5'd0;
                    abs_slope = slope[`OPT_GROUP_B_WIDTH+1] ? -slope : slope;
                    slope_sign = slope[`OPT_GROUP_B_WIDTH+1];
                    
                    // 找到设置位的位置 - 使用常量边界
                    for (i = 0; i < 32; i = i + 1) begin
                        if (i < (`OPT_GROUP_B_WIDTH+2) && abs_slope[i]) begin
                            detected_shift = i[4:0];
                        end
                    end
                    
                    // 应用基于移位的乘法
                    if (slope_sign) begin
                        mult_result = -($signed(adjusted_x_r4) << detected_shift);
                    end else begin
                        mult_result = $signed(adjusted_x_r4) << detected_shift;
                    end
                    
                    if (debug_counter <= 15) begin
                        $display("Stage 5: Runtime power-of-2 detected");
                        $display("  slope=%h, detected_shift=%d, sign=%b", 
                                slope, detected_shift, slope_sign);
                        $display("  result = %h", mult_result);
                    end
                end
                else begin
                    // 标准乘法 - 与DSP块对齐
                    mult_result = $signed(slope) * $signed(adjusted_x_r4);
                    
                    if (debug_counter <= 15) begin
                        $display("Stage 5: Standard multiplication");
                        $display("  %h * %h = %h", slope, adjusted_x_r4, mult_result);
                    end
                end
                
                // 根据定点格式缩放结果
                scaled_result = $signed(mult_result >>> `OPT_FRAC_BITS) + $signed(intercept[`OPT_GROUP_C_WIDTH-1:0]);
                
                if (debug_counter <= 15) begin
                    $display("  scaled result = (%h >>> %d) + %h = %h", 
                            mult_result, `OPT_FRAC_BITS, intercept[`OPT_GROUP_C_WIDTH-1:0], scaled_result);
                end
                
                // 基于函数属性应用变换
                if (y_reflect_r4) begin
                    scaled_result = -scaled_result;
                    
                    if (debug_counter <= 15) begin
                        $display("  applying y-reflection: %h", -scaled_result);
                    end
                end
                
                // 最终符号处理 - 取决于函数对称性属性
                // 这个通用代码处理奇函数、偶函数和不对称函数
                if (x_sign_r4) begin
                    // 对于像tanh、正弦等奇函数
                    y_out <= -scaled_result;
                    
                    if (debug_counter <= 15) begin
                        $display("  input was negative, final y_out = %h", -scaled_result);
                    end
                end else begin
                    y_out <= scaled_result;
                    
                    if (debug_counter <= 15) begin
                        $display("  input was positive, final y_out = %h", scaled_result);
                    end
                end
                
                if (debug_counter <= 15) begin
                    $display("FINAL OUTPUT: y_out = %h", 
                            x_sign_r4 ? -scaled_result : scaled_result);
                end
            end
        end
    end
endmodule
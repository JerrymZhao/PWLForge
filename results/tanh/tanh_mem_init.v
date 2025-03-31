// Auto-generated PWL memory initialization
`ifndef PWL_MEM_INIT_V
`define PWL_MEM_INIT_V

// Initialize memories with hardcoded values
task initialize_pwl_memories;
    input [`PWL_ADDR_WIDTH-1:0] num_breakpoints;
    input [`PWL_ADDR_WIDTH-1:0] num_segments;
    reg [`PWL_DATA_WIDTH-1:0] bp_mem [0:`PWL_NUM_BREAKPOINTS-1];
    reg [`PWL_DATA_WIDTH-1:0] slope_mem [0:`PWL_NUM_SEGMENTS-1];
    reg [`PWL_DATA_WIDTH-1:0] intercept_mem [0:`PWL_NUM_SEGMENTS-1];
    integer i;
begin
    bp_mem[0] = 0;
    bp_mem[1] = 0;
    bp_mem[2] = 0;
    bp_mem[3] = 0;
    bp_mem[4] = 2000;
    bp_mem[5] = 3001;
    bp_mem[6] = 4002;
    bp_mem[7] = 5002;
    bp_mem[8] = 6003;
    bp_mem[9] = 7003;
    bp_mem[10] = 8002;
    bp_mem[11] = 8500;
    bp_mem[12] = 8997;
    bp_mem[13] = 9492;
    bp_mem[14] = 9989;
    bp_mem[15] = 10484;
    bp_mem[16] = 10981;
    bp_mem[17] = 11479;
    bp_mem[18] = 11973;
    bp_mem[19] = 12471;
    bp_mem[20] = 12965;
    bp_mem[21] = 13463;
    bp_mem[22] = 13960;
    slope_mem[0] = 0;
    slope_mem[1] = 0;
    slope_mem[2] = 0;
    slope_mem[3] = 0;
    slope_mem[4] = 16078;
    slope_mem[5] = 15753;
    slope_mem[6] = 15327;
    slope_mem[7] = 14809;
    slope_mem[8] = 14211;
    slope_mem[9] = 13636;
    slope_mem[10] = 12932;
    slope_mem[11] = 12567;
    slope_mem[12] = 12198;
    slope_mem[13] = 11820;
    slope_mem[14] = 11442;
    slope_mem[15] = 11060;
    slope_mem[16] = 10488;
    slope_mem[17] = 10108;
    slope_mem[18] = 9730;
    slope_mem[19] = 9356;
    slope_mem[20] = 8990;
    slope_mem[21] = 8625;
    slope_mem[22] = 8270;
    slope_mem[23] = 7920;
    intercept_mem[0] = 0;
    intercept_mem[1] = 0;
    intercept_mem[2] = 0;
    intercept_mem[3] = 0;
    intercept_mem[4] = 28;
    intercept_mem[5] = 81;
    intercept_mem[6] = 180;
    intercept_mem[7] = 332;
    intercept_mem[8] = 542;
    intercept_mem[9] = 777;
    intercept_mem[10] = 1107;
    intercept_mem[11] = 1291;
    intercept_mem[12] = 1493;
    intercept_mem[13] = 1706;
    intercept_mem[14] = 1936;
    intercept_mem[15] = 2177;
    intercept_mem[16] = 2560;
    intercept_mem[17] = 2829;
    intercept_mem[18] = 3109;
    intercept_mem[19] = 3396;
    intercept_mem[20] = 3690;
    intercept_mem[21] = 3989;
    intercept_mem[22] = 4298;
    intercept_mem[23] = 4609;
end
endtask

`endif // PWL_MEM_INIT_V

`ifndef hswish_CONFIG_VH
`define hswish_CONFIG_VH

// Data width configuration (FIXED-16)
`define INPUT_DATA_WIDTH 16
`define OUTPUT_DATA_WIDTH 16

// Fixed-point format
`define OPT_FRAC_BITS 13
`define FIXED_POINT_SCALE 8192  // 2^13

// Group and interval counts
`define OPT_NUM_GROUPS 2
`define OPT_TOTAL_INTERVALS 120
`define OPT_MAX_INTERVALS_PER_GROUP 96

// Address widths (bit widths for indexing)
`define OPT_GROUP_ADDR_WIDTH 1
`define OPT_INTERVAL_ADDR_WIDTH 7
`define OPT_DELTA_ADDR_WIDTH 7

// Memory sizes (in 16-bit words, FIXED-16 mode)
`define OPT_INTERVAL_METADATA_SIZE 360  // start[1] + end[1] + gid[1]
`define OPT_GROUP_BOUNDS_SIZE 4  // min[1] + max[1]
`define OPT_GROUP_MAP_SIZE 2
`define OPT_GROUP_INFO_SIZE 6  // flags[1] + b[1] + c[1]
`define OPT_DELTA_DATA_SIZE 360  // delta_b[1] + delta_c[1] + flags[1]

`endif

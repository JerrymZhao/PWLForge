`ifndef tanh_CONFIG_VH
`define tanh_CONFIG_VH

// Data width configuration (FIXED-16)
`define INPUT_DATA_WIDTH 16
`define OUTPUT_DATA_WIDTH 16

// Fixed-point format
`define OPT_FRAC_BITS 14
`define FIXED_POINT_SCALE 16384  // 2^14

// Group and interval counts
`define OPT_NUM_GROUPS 3
`define OPT_TOTAL_INTERVALS 26
`define OPT_MAX_INTERVALS_PER_GROUP 19

// Address widths (bit widths for indexing)
`define OPT_GROUP_ADDR_WIDTH 2
`define OPT_INTERVAL_ADDR_WIDTH 5
`define OPT_DELTA_ADDR_WIDTH 5

// Memory sizes (in 16-bit words, FIXED-16 mode)
`define OPT_INTERVAL_METADATA_SIZE 78  // start[1] + end[1] + gid[1]
`define OPT_GROUP_BOUNDS_SIZE 6  // min[1] + max[1]
`define OPT_GROUP_MAP_SIZE 3
`define OPT_GROUP_INFO_SIZE 9  // flags[1] + b[1] + c[1]
`define OPT_DELTA_DATA_SIZE 78  // delta_b[1] + delta_c[1] + flags[1]

`endif

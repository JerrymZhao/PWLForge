`ifndef swish_CONFIG_VH
`define swish_CONFIG_VH

// Data width configuration (FP16)
`define INPUT_DATA_WIDTH 16
`define OUTPUT_DATA_WIDTH 16

// Floating-point format (float16)
`define FLOAT_FORMAT 16

// Group and interval counts
`define OPT_NUM_GROUPS 4
`define OPT_TOTAL_INTERVALS 20
`define OPT_MAX_INTERVALS_PER_GROUP 7

// Address widths (bit widths for indexing)
`define OPT_GROUP_ADDR_WIDTH 2
`define OPT_INTERVAL_ADDR_WIDTH 3
`define OPT_DELTA_ADDR_WIDTH 5

// Memory sizes (in 16-bit words, FP16 mode)
`define OPT_INTERVAL_METADATA_SIZE 60  // start[1] + end[1] + gid[1]
`define OPT_GROUP_BOUNDS_SIZE 8  // min[1] + max[1]
`define OPT_GROUP_MAP_SIZE 4
`define OPT_GROUP_INFO_SIZE 12  // flags[1] + b[1] + c[1]
`define OPT_DELTA_DATA_SIZE 60  // delta_b[1] + delta_c[1] + flags[1]

`endif

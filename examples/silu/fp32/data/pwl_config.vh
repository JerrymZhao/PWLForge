`ifndef silu_CONFIG_VH
`define silu_CONFIG_VH

// Data width configuration (FP32)
`define INPUT_DATA_WIDTH 32
`define OUTPUT_DATA_WIDTH 32

// Floating-point format (float32)
`define FLOAT_FORMAT 32

// Group and interval counts
`define OPT_NUM_GROUPS 4
`define OPT_TOTAL_INTERVALS 23
`define OPT_MAX_INTERVALS_PER_GROUP 10

// Address widths (bit widths for indexing)
`define OPT_GROUP_ADDR_WIDTH 2
`define OPT_INTERVAL_ADDR_WIDTH 4
`define OPT_DELTA_ADDR_WIDTH 5

// Memory sizes (in 16-bit words, FP32 mode)
`define OPT_INTERVAL_METADATA_SIZE 115  // start[2] + end[2] + gid[1]
`define OPT_GROUP_BOUNDS_SIZE 16  // min[2] + max[2]
`define OPT_GROUP_MAP_SIZE 4
`define OPT_GROUP_INFO_SIZE 20  // flags[1] + b[2] + c[2]
`define OPT_DELTA_DATA_SIZE 115  // delta_b[2] + delta_c[2] + flags[1]

`endif

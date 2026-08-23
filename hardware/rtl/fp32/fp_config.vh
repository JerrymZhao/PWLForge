// Floating-point configuration header
`ifndef FP_CONFIG_VH
`define FP_CONFIG_VH

// Select format by uncommenting one:
// `define FP_FORMAT_FP64
`define FP_FORMAT_FP32
// `define FP_FORMAT_FP16
// `define FP_FORMAT_FP8

// Auto-configure based on selected format
`ifdef FP_FORMAT_FP64
    `define FP_DATA_WIDTH 64
    `define FP_MULT_LATENCY 12
    `define FP_ADD_LATENCY 14
`elsif FP_FORMAT_FP32
    `define FP_DATA_WIDTH 32
    `define FP_MULT_LATENCY 8
    `define FP_ADD_LATENCY 11
`elsif FP_FORMAT_FP16
    `define FP_DATA_WIDTH 16
    `define FP_MULT_LATENCY 6
    `define FP_ADD_LATENCY 7
`elsif FP_FORMAT_FP8
    `define FP_DATA_WIDTH 8
    `define FP_MULT_LATENCY 4
    `define FP_ADD_LATENCY 5
`else
    // Default to FP32
    `define FP_DATA_WIDTH 32
    `define FP_MULT_LATENCY 7
    `define FP_ADD_LATENCY 12
`endif

`define FP_METADATA_WIDTH 32
`define FP_COMPARE_LATENCY 2 

`endif // FP_CONFIG_VH

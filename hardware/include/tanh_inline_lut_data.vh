// Auto-generated inline LUT data for tanh function
// Generated using 16-bit aligned bit layout with signed 15-bit fixed-point representation

// Compatibility verification - hardware aligns with these positions:
// GROUP_START_POS = 0
// GROUP_LENGTH_POS = 16
// DELTA_REFLECTION_X_POS = 48
// DELTA_REFLECTION_Y_POS = 49

// Explanation of control fields and number representation:
// All values use 15-bit fixed-point representation with scale factor 2^15=32768
// Parameter values (slope/intercept): signed [-1.0, 0.99997] maps to [-32768, 32767]
// Position values (start/end): signed for proper comparison
// Length values: unsigned but using same 15-bit precision
// FLAGS_SIZE: Bit 0 = group type (0=regular, 1=orphan), Bits [15:1] = actual interval count
//   - Orphan groups (bit 0=1): store complete parameter values instead of delta values
//   - Regular groups (bit 0=0): store delta values relative to base parameters
// GROUP_LENGTH: For regular groups only, stores the quantized length of the group range
//   - For orphan groups, this field is unused (set to 0)
// SLOPE_SCALE: Fixed at 2^15=32768 for all values
//   - Division by scale factor is implemented as right-shift by 15 bits
//   - This converts floating-point multiplies into fixed-point shift operations

// Initialization statements for group_info array
// Each group has 6 16-bit words with modified layout
group_info[0] = 16'h0009; // Group 2 FLAGS_SIZE
group_info[1] = 16'h0000; // BASE_B (signed)
group_info[2] = 16'h0000; // BASE_C (signed)
group_info[3] = 16'h0000; // OFFSET
group_info[4] = 16'h8000; // SCALE_FACTOR (2^15)
group_info[5] = 16'h0000; // UNUSED

group_info[6] = 16'h0010; // Group 0 FLAGS_SIZE
group_info[7] = 16'h7d9b; // BASE_B (signed)
group_info[8] = 16'h0037; // BASE_C (signed)
group_info[9] = 16'h0004; // OFFSET
group_info[10] = 16'h8000; // SCALE_FACTOR (2^15)
group_info[11] = 16'h07d1; // GROUP_LENGTH (unsigned)

group_info[12] = 16'h0020; // Group 1 FLAGS_SIZE
group_info[13] = 16'h6507; // BASE_B (signed)
group_info[14] = 16'h08a3; // BASE_C (signed)
group_info[15] = 16'h000c; // OFFSET
group_info[16] = 16'h8000; // SCALE_FACTOR (2^15)
group_info[17] = 16'h03e0; // GROUP_LENGTH (unsigned)

// Delta data initializations
// Each delta has 4 16-bit words with signed parameter values
// For orphan groups: 4th word stores END instead of REFLECTION flags
// Delta data for Group 2 (ORPHAN)
delta_data[0] = 16'h0000; // START (signed position)
delta_data[1] = 16'h7fd8; // SLOPE (signed parameter)
delta_data[2] = 16'h0000; // INTERCEPT (signed parameter)
delta_data[3] = 16'h0fa2; // END (signed position)
delta_data[4] = 16'h78bf; // START (signed position)
delta_data[5] = 16'h3724; // SLOPE (signed parameter)
delta_data[6] = 16'h2a59; // INTERCEPT (signed parameter)
delta_data[7] = 16'h7f90; // END (signed position)
delta_data[8] = 16'h7f80; // START (signed position)
delta_data[9] = 16'h35eb; // SLOPE (signed parameter)
delta_data[10] = 16'h2b91; // INTERCEPT (signed parameter)
delta_data[11] = 16'h8000; // END (signed position)
delta_data[12] = 16'h7f90; // START (signed position)
delta_data[13] = 16'h35e6; // SLOPE (signed parameter)
delta_data[14] = 16'h2b97; // INTERCEPT (signed parameter)
delta_data[15] = 16'h8000; // END (signed position)

// Delta data for Group 0
delta_data[16] = 16'h0fa2; // START (signed position)
delta_data[17] = 16'h0000; // SLOPE (signed parameter)
delta_data[18] = 16'h0000; // INTERCEPT (signed parameter)
delta_data[19] = 16'h0000; // REFLECTION flags
delta_data[20] = 16'h1773; // START (signed position)
delta_data[21] = 16'hfd79; // SLOPE (signed parameter)
delta_data[22] = 16'h006e; // INTERCEPT (signed parameter)
delta_data[23] = 16'h0000; // REFLECTION flags
delta_data[24] = 16'h1f44; // START (signed position)
delta_data[25] = 16'hfa23; // SLOPE (signed parameter)
delta_data[26] = 16'h0132; // INTERCEPT (signed parameter)
delta_data[27] = 16'h0000; // REFLECTION flags
delta_data[28] = 16'h2715; // START (signed position)
delta_data[29] = 16'hf616; // SLOPE (signed parameter)
delta_data[30] = 16'h0260; // INTERCEPT (signed parameter)
delta_data[31] = 16'h0000; // REFLECTION flags
delta_data[32] = 16'h2ee6; // START (signed position)
delta_data[33] = 16'hf16b; // SLOPE (signed parameter)
delta_data[34] = 16'h0404; // INTERCEPT (signed parameter)
delta_data[35] = 16'h0000; // REFLECTION flags
delta_data[36] = 16'h36b7; // START (signed position)
delta_data[37] = 16'hecec; // SLOPE (signed parameter)
delta_data[38] = 16'h05dc; // INTERCEPT (signed parameter)
delta_data[39] = 16'h0000; // REFLECTION flags
delta_data[40] = 16'h70ee; // START (signed position)
delta_data[41] = 16'hc043; // SLOPE (signed parameter)
delta_data[42] = 16'h23cb; // INTERCEPT (signed parameter)
delta_data[43] = 16'h0000; // REFLECTION flags
delta_data[44] = 16'h0000; // START (signed position)
delta_data[45] = 16'h0000; // SLOPE (signed parameter)
delta_data[46] = 16'h0000; // INTERCEPT (signed parameter)
delta_data[47] = 16'h0000; // REFLECTION flags

// Delta data for Group 1
delta_data[48] = 16'h3e88; // START (signed position)
delta_data[49] = 16'h0000; // SLOPE (signed parameter)
delta_data[50] = 16'h0000; // INTERCEPT (signed parameter)
delta_data[51] = 16'h0000; // REFLECTION flags
delta_data[52] = 16'h4268; // START (signed position)
delta_data[53] = 16'hfd28; // SLOPE (signed parameter)
delta_data[54] = 16'h0174; // INTERCEPT (signed parameter)
delta_data[55] = 16'h0000; // REFLECTION flags
delta_data[56] = 16'h4649; // START (signed position)
delta_data[57] = 16'hfa43; // SLOPE (signed parameter)
delta_data[58] = 16'h0305; // INTERCEPT (signed parameter)
delta_data[59] = 16'h0000; // REFLECTION flags
delta_data[60] = 16'h4a29; // START (signed position)
delta_data[61] = 16'hf754; // SLOPE (signed parameter)
delta_data[62] = 16'h04b3; // INTERCEPT (signed parameter)
delta_data[63] = 16'h0000; // REFLECTION flags
delta_data[64] = 16'h4e0a; // START (signed position)
delta_data[65] = 16'hf45d; // SLOPE (signed parameter)
delta_data[66] = 16'h067c; // INTERCEPT (signed parameter)
delta_data[67] = 16'h0000; // REFLECTION flags
delta_data[68] = 16'h51ea; // START (signed position)
delta_data[69] = 16'hf162; // SLOPE (signed parameter)
delta_data[70] = 16'h085f; // INTERCEPT (signed parameter)
delta_data[71] = 16'h0000; // REFLECTION flags
delta_data[72] = 16'h55cb; // START (signed position)
delta_data[73] = 16'hece9; // SLOPE (signed parameter)
delta_data[74] = 16'h0b5e; // INTERCEPT (signed parameter)
delta_data[75] = 16'h0000; // REFLECTION flags
delta_data[76] = 16'h59ab; // START (signed position)
delta_data[77] = 16'he9f1; // SLOPE (signed parameter)
delta_data[78] = 16'h0d79; // INTERCEPT (signed parameter)
delta_data[79] = 16'h0000; // REFLECTION flags
delta_data[80] = 16'h5d8c; // START (signed position)
delta_data[81] = 16'he6fe; // SLOPE (signed parameter)
delta_data[82] = 16'h0fa6; // INTERCEPT (signed parameter)
delta_data[83] = 16'h0000; // REFLECTION flags
delta_data[84] = 16'h616c; // START (signed position)
delta_data[85] = 16'he413; // SLOPE (signed parameter)
delta_data[86] = 16'h11e4; // INTERCEPT (signed parameter)
delta_data[87] = 16'h0000; // REFLECTION flags
delta_data[88] = 16'h654d; // START (signed position)
delta_data[89] = 16'he132; // SLOPE (signed parameter)
delta_data[90] = 16'h1431; // INTERCEPT (signed parameter)
delta_data[91] = 16'h0000; // REFLECTION flags
delta_data[92] = 16'h692d; // START (signed position)
delta_data[93] = 16'hde5c; // SLOPE (signed parameter)
delta_data[94] = 16'h168b; // INTERCEPT (signed parameter)
delta_data[95] = 16'h0000; // REFLECTION flags
delta_data[96] = 16'h6d0e; // START (signed position)
delta_data[97] = 16'hdb94; // SLOPE (signed parameter)
delta_data[98] = 16'h18ef; // INTERCEPT (signed parameter)
delta_data[99] = 16'h0000; // REFLECTION flags
delta_data[100] = 16'h0000; // START (signed position)
delta_data[101] = 16'h0000; // SLOPE (signed parameter)
delta_data[102] = 16'h0000; // INTERCEPT (signed parameter)
delta_data[103] = 16'h0000; // REFLECTION flags
delta_data[104] = 16'h0000; // START (signed position)
delta_data[105] = 16'h0000; // SLOPE (signed parameter)
delta_data[106] = 16'h0000; // INTERCEPT (signed parameter)
delta_data[107] = 16'h0000; // REFLECTION flags
delta_data[108] = 16'h0000; // START (signed position)
delta_data[109] = 16'h0000; // SLOPE (signed parameter)
delta_data[110] = 16'h0000; // INTERCEPT (signed parameter)
delta_data[111] = 16'h0000; // REFLECTION flags

// Implementation guidance for hardware designer
/*
// Replace the original memory initialization with this:
`include "tanh_optimized_bitwidths.vh"

// Memory arrays with optimized bit layouts
// Important: All parameter values use signed 15-bit fixed-point representation
// with SCALE_FACTOR = 32768 (2^15)
(* ram_style = "distributed" *) reg [15:0] group_info [0:17];
(* ram_style = "distributed" *) reg [15:0] delta_data [0:111];

// For proper hardware implementation, update these constants:
localparam SCALE_FACTOR_BITS = 15;         // log2(SCALE_FACTOR)
localparam SCALE_FACTOR = 32768;           // 2^15 = 32768
*/

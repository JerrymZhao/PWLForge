// Replace the memory initialization section in pwl_hlut.v with this:

// Include optimized bit width definitions
`include "tanh_optimized_bitwidths.vh"

// Memory arrays with optimized layouts
(* ram_style = "distributed" *) reg [15:0] group_info [0:29];
(* ram_style = "distributed" *) reg [15:0] delta_data [0:127];

// Memory initialization with optimized inline data
initial begin
 // Initialize all memory with zeros
 for (int i = 0; i < 30; i = i + 1) begin
 group_info[i] = 16'h0000;
 end
 for (int i = 0; i < 128; i = i + 1) begin
 delta_data[i] = 16'h0000;
 end

 // Group information data
 group_info[0] = 16'h01f4;
 group_info[1] = 16'h0213;
 group_info[2] = 16'h0328;
 group_info[3] = 16'h0045;
 group_info[4] = 16'h0020;
 group_info[5] = 16'h0009;
 group_info[6] = 16'h0000;
 group_info[7] = 16'h0400;
 group_info[8] = 16'h0000;
 group_info[9] = 16'h0000;
 group_info[10] = 16'h007d;
 group_info[11] = 16'h00bc;
 group_info[12] = 16'h03ed;
 group_info[13] = 16'h0002;
 group_info[14] = 16'h0010;
 group_info[15] = 16'h0007;
 group_info[16] = 16'h0010;
 group_info[17] = 16'h0400;
 group_info[18] = 16'h0000;
 group_info[19] = 16'h0000;
 group_info[20] = 16'h0000;
 group_info[21] = 16'h0000;
 group_info[22] = 16'h0000;
 group_info[23] = 16'h0000;
 group_info[24] = 16'h0011;
 group_info[25] = 16'h0000;
 group_info[26] = 16'h0018;
 group_info[27] = 16'h0400;
 group_info[28] = 16'h0000;
 group_info[29] = 16'h0000;

 // Delta parameter data
 delta_data[0] = 16'h0000;
 delta_data[1] = 16'h0000;
 delta_data[2] = 16'h0000;
 delta_data[3] = 16'h0000;
 delta_data[4] = 16'h0001;
 delta_data[5] = 16'h0000;
 delta_data[6] = 16'h0000;
 delta_data[7] = 16'h0000;
 delta_data[8] = 16'h0001;
 delta_data[9] = 16'h0000;
 delta_data[10] = 16'h0000;
 delta_data[11] = 16'h0000;
 delta_data[12] = 16'h0001;
 delta_data[13] = 16'h0000;
 delta_data[14] = 16'h0000;
 delta_data[15] = 16'h0000;
 delta_data[16] = 16'h0001;
 delta_data[17] = 16'h0000;
 delta_data[18] = 16'h0000;
 delta_data[19] = 16'h0000;
 delta_data[20] = 16'h0001;
 delta_data[21] = 16'h0000;
 delta_data[22] = 16'h0000;
 delta_data[23] = 16'h0000;
 delta_data[24] = 16'h0001;
 delta_data[25] = 16'h0000;
 delta_data[26] = 16'h0000;
 delta_data[27] = 16'h0000;
 delta_data[28] = 16'h0001;
 delta_data[29] = 16'h0000;
 delta_data[30] = 16'h0000;
 delta_data[31] = 16'h0000;
 delta_data[32] = 16'h0001;
 delta_data[33] = 16'h0000;
 delta_data[34] = 16'h0000;
 delta_data[35] = 16'h0000;
 delta_data[36] = 16'h0001;
 delta_data[37] = 16'h0000;
 delta_data[38] = 16'h0000;
 delta_data[39] = 16'h0000;
 delta_data[40] = 16'h0001;
 delta_data[41] = 16'h0000;
 delta_data[42] = 16'h0000;
 delta_data[43] = 16'h0000;
 delta_data[44] = 16'h0001;
 delta_data[45] = 16'h0000;
 delta_data[46] = 16'h0000;
 delta_data[47] = 16'h0000;
 delta_data[48] = 16'h0001;
 delta_data[49] = 16'h0000;
 delta_data[50] = 16'h0000;
 delta_data[51] = 16'h0000;
 delta_data[52] = 16'h0000;
 delta_data[53] = 16'h0000;
 delta_data[54] = 16'h0000;
 delta_data[55] = 16'h0000;
 delta_data[56] = 16'h0000;
 delta_data[57] = 16'h0000;
 delta_data[58] = 16'h0000;
 delta_data[59] = 16'h0000;
 delta_data[60] = 16'h0000;
 delta_data[61] = 16'h0000;
 delta_data[62] = 16'h0000;
 delta_data[63] = 16'h0000;
 delta_data[64] = 16'h0000;
 delta_data[65] = 16'h0000;
 delta_data[66] = 16'h0000;
 delta_data[67] = 16'h0000;
 delta_data[68] = 16'h0000;
 delta_data[69] = 16'h0000;
 delta_data[70] = 16'h0000;
 delta_data[71] = 16'h0000;
 delta_data[72] = 16'h0000;
 delta_data[73] = 16'h0000;
 delta_data[74] = 16'h0000;
 delta_data[75] = 16'h0000;
 delta_data[76] = 16'h0000;
 delta_data[77] = 16'h0000;
 delta_data[78] = 16'h0000;
 delta_data[79] = 16'h0000;
 delta_data[80] = 16'h0000;
 delta_data[81] = 16'h0000;
 delta_data[82] = 16'h0000;
 delta_data[83] = 16'h0000;
 delta_data[84] = 16'h0000;
 delta_data[85] = 16'h0000;
 delta_data[86] = 16'h0000;
 delta_data[87] = 16'h0000;
 delta_data[88] = 16'h0001;
 delta_data[89] = 16'h0000;
 delta_data[90] = 16'h0000;
 delta_data[91] = 16'h0000;
 delta_data[92] = 16'h0000;
 delta_data[93] = 16'h0000;
 delta_data[94] = 16'h0000;
 delta_data[95] = 16'h0000;
 delta_data[96] = 16'h0001;
 delta_data[97] = 16'h0000;
 delta_data[98] = 16'h0000;
 delta_data[99] = 16'h0000;
 delta_data[100] = 16'h0001;
 delta_data[101] = 16'h0000;
 delta_data[102] = 16'h0000;
 delta_data[103] = 16'h0000;
 delta_data[104] = 16'h0001;
 delta_data[105] = 16'h0000;
 delta_data[106] = 16'h0000;
 delta_data[107] = 16'h0000;
 delta_data[108] = 16'h0000;
 delta_data[109] = 16'h0000;
 delta_data[110] = 16'h0000;
 delta_data[111] = 16'h0000;
 delta_data[112] = 16'h0000;
 delta_data[113] = 16'h0000;
 delta_data[114] = 16'h0000;
 delta_data[115] = 16'h0000;
 delta_data[116] = 16'h0000;
 delta_data[117] = 16'h0000;
 delta_data[118] = 16'h0000;
 delta_data[119] = 16'h0000;
 delta_data[120] = 16'h0000;
 delta_data[121] = 16'h0000;
 delta_data[122] = 16'h0000;
 delta_data[123] = 16'h0000;
 delta_data[124] = 16'h0000;
 delta_data[125] = 16'h0000;
 delta_data[126] = 16'h0000;
 delta_data[127] = 16'h0000;
end

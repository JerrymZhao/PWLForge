module pwl_recovery (
    input wire [15:0] x,
    output reg [15:0] y
);
    localparam NUM_BREAKPOINTS = 21;
    localparam NUM_SEGMENTS = 22;
    localparam FRAC_BITS = 14;
    reg [15:0] breakpoints [0:20];
    reg [15:0] slopes [0:21];
    reg [15:0] intercepts [0:21];
    reg [4:0] idx;
    integer i;

    initial begin
        breakpoints[0] = 16'd0;
        breakpoints[1] = 16'd0;
        breakpoints[2] = 16'd2000;
        breakpoints[3] = 16'd3001;
        breakpoints[4] = 16'd4002;
        breakpoints[5] = 16'd5002;
        breakpoints[6] = 16'd6003;
        breakpoints[7] = 16'd7003;
        breakpoints[8] = 16'd8002;
        breakpoints[9] = 16'd8500;
        breakpoints[10] = 16'd8997;
        breakpoints[11] = 16'd9492;
        breakpoints[12] = 16'd9989;
        breakpoints[13] = 16'd10484;
        breakpoints[14] = 16'd10981;
        breakpoints[15] = 16'd11479;
        breakpoints[16] = 16'd11973;
        breakpoints[17] = 16'd12471;
        breakpoints[18] = 16'd12965;
        breakpoints[19] = 16'd13463;
        breakpoints[20] = 16'd13960;
        slopes[0] = 16'd0;
        slopes[1] = 16'd0;
        slopes[2] = 16'd16078;
        slopes[3] = 16'd15753;
        slopes[4] = 16'd15327;
        slopes[5] = 16'd14809;
        slopes[6] = 16'd14211;
        slopes[7] = 16'd13636;
        slopes[8] = 16'd12932;
        slopes[9] = 16'd12567;
        slopes[10] = 16'd12198;
        slopes[11] = 16'd11820;
        slopes[12] = 16'd11442;
        slopes[13] = 16'd11060;
        slopes[14] = 16'd10488;
        slopes[15] = 16'd10108;
        slopes[16] = 16'd9730;
        slopes[17] = 16'd9356;
        slopes[18] = 16'd8990;
        slopes[19] = 16'd8625;
        slopes[20] = 16'd8270;
        slopes[21] = 16'd7920;
        intercepts[0] = 16'd0;
        intercepts[1] = 16'd0;
        intercepts[2] = 16'd28;
        intercepts[3] = 16'd81;
        intercepts[4] = 16'd180;
        intercepts[5] = 16'd332;
        intercepts[6] = 16'd542;
        intercepts[7] = 16'd777;
        intercepts[8] = 16'd1107;
        intercepts[9] = 16'd1291;
        intercepts[10] = 16'd1493;
        intercepts[11] = 16'd1706;
        intercepts[12] = 16'd1936;
        intercepts[13] = 16'd2177;
        intercepts[14] = 16'd2560;
        intercepts[15] = 16'd2829;
        intercepts[16] = 16'd3109;
        intercepts[17] = 16'd3396;
        intercepts[18] = 16'd3690;
        intercepts[19] = 16'd3989;
        intercepts[20] = 16'd4298;
        intercepts[21] = 16'd4609;
    end

    always @(*) begin
        idx = 0;
        for (i = 0; i < NUM_BREAKPOINTS; i = i + 1) begin
            if (x >= breakpoints[i]) idx = i + 1;
            else break;
        end
        y = (slopes[idx] * x + intercepts[idx]) >> FRAC_BITS;
    end
endmodule

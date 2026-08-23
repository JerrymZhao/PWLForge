# 200 MHz timing constraint for fixed-point PWL AXI-stream wrapper.
create_clock -period 5.000 -name clk -waveform {0.000 2.500} [get_ports clk]

set_input_delay  -clock clk -max 1.000 [get_ports {s_axis_tdata[*] s_axis_tvalid m_axis_tready}]
set_input_delay  -clock clk -min 0.250 [get_ports {s_axis_tdata[*] s_axis_tvalid m_axis_tready}]
set_output_delay -clock clk -max 1.000 [get_ports {m_axis_tdata[*] m_axis_tvalid s_axis_tready}]
set_output_delay -clock clk -min 0.250 [get_ports {m_axis_tdata[*] m_axis_tvalid s_axis_tready}]

set_false_path -from [get_ports rst_n]

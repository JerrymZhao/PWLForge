#========================================================================
# pwl_constraints.xdc - Timing and physical constraints
#========================================================================

# Clock definition
create_clock -period 10.000 -name clk -waveform {0.000 5.000} [get_ports clk]

# Clock domain crossing constraints for asynchronous reset
set_false_path -from [get_ports rst_n]

# I/O timing
set_input_delay -clock clk -max 1.000 [get_ports {s_axis_tdata[*]}]
set_input_delay -clock clk -max 1.000 [get_ports s_axis_tvalid]
set_output_delay -clock clk -max 1.000 [get_ports {m_axis_tdata[*]}]
set_output_delay -clock clk -max 1.000 [get_ports m_axis_tvalid]

# I/O standard
set_property IOSTANDARD LVCMOS33 [get_ports clk]
set_property IOSTANDARD LVCMOS33 [get_ports rst_n]
set_property IOSTANDARD LVCMOS33 [get_ports {s_axis_tdata[*]}]
set_property IOSTANDARD LVCMOS33 [get_ports s_axis_tvalid]
set_property IOSTANDARD LVCMOS33 [get_ports s_axis_tready]
set_property IOSTANDARD LVCMOS33 [get_ports {m_axis_tdata[*]}]
set_property IOSTANDARD LVCMOS33 [get_ports m_axis_tvalid]
set_property IOSTANDARD LVCMOS33 [get_ports m_axis_tready]

# Configuration options
# set_property CFGBVS VCCO [current_design]
# set_property CONFIG_VOLTAGE 3.3 [current_design]

# Timing performance
#set_operating_conditions -grade commercial

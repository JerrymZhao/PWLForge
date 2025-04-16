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
set_property CFGBVS VCCO [current_design]
set_property CONFIG_VOLTAGE 3.3 [current_design]

# ADDED: Memory and DSP optimization constraints
# Force LUT-based distributed RAM implementation
set_property RAM_STYLE DISTRIBUTED [get_cells -hierarchical *group_info_reg*]
set_property RAM_STYLE DISTRIBUTED [get_cells -hierarchical *delta_data_reg*]

# Force DSP usage for multiplication operations
set_property USE_DSP48 YES [get_cells -hierarchical *a_x*]
set_property USE_DSP48 YES [get_cells -hierarchical *b_x*]
set_property USE_DSP48 YES [get_cells -hierarchical *scaled_delta*]
set_property USE_DSP48 YES [get_cells -hierarchical *a_x2*]

# Pipeline optimization
set_property SHREG_EXTRACT NO [get_cells -hierarchical *valid_pipe*]
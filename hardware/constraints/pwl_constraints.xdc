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
set_property CONFIG_VOLTAGE 3.3 [current_design]

# ADDED: Memory and DSP optimization constraints
# Force LUT-based distributed RAM implementation - adjusted patterns to match RTL
set_property RAM_STYLE DISTRIBUTED [get_cells -quiet -hierarchical *group_info*]
set_property RAM_STYLE DISTRIBUTED [get_cells -quiet -hierarchical *delta_data*]

# Force DSP usage for multiplication operations - adjusted patterns to match RTL
# Find all multiplications
set_property USE_DSP48 YES [get_cells -quiet -hierarchical *scaled_delta_slope*]
set_property USE_DSP48 YES [get_cells -quiet -hierarchical *scaled_delta_intercept*]
set_property USE_DSP48 YES [get_cells -quiet -hierarchical *b_x*]
set_property USE_DSP48 YES [get_cells -quiet -hierarchical *interval_calc*]

# Pipeline optimization - use -quiet flag to prevent errors if cells don't exist
set_property SHREG_EXTRACT NO [get_cells -quiet -hierarchical *valid_reg*]
set_property SHREG_EXTRACT NO [get_cells -quiet -hierarchical *valid_pipe*]
set_property SHREG_EXTRACT NO [get_cells -quiet -hierarchical *out_valid*]
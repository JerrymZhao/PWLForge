# 200 MHz core-only timing constraint.
# This measures internal register-to-register timing and excludes top-level IO pad paths.
create_clock -period 5.000 -name clk -waveform {0.000 2.500} [get_ports clk]

set data_inputs [remove_from_collection [all_inputs] [get_ports {clk rst_n}]]
set data_outputs [all_outputs]

set_false_path -from $data_inputs
set_false_path -to $data_outputs
set_false_path -from [get_ports rst_n]

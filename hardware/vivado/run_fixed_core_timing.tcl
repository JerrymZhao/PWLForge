# Synthesize one shared fixed-point template/example pair and report core timing.
#
# Example:
#   VIVADO_EXAMPLE=examples/tanh/fixed16 \
#   VIVADO_TEMPLATE=hardware/rtl/fixed16 \
#   VIVADO_XDC=hardware/constraints/core_clk_200mhz.xdc \
#   vivado -mode batch -source hardware/vivado/run_fixed_core_timing.tcl
#
# Optional: VIVADO_PART (default xczu9eg-ffvb1156-2-e) and
# VIVADO_REPORTS_DIR (default <example>/vivado_core_timing_report).

set repo_dir [file normalize [file join [file dirname [info script]] ".." ".."]]

if {![info exists ::env(VIVADO_EXAMPLE)]} {
    error "Set VIVADO_EXAMPLE, e.g. examples/tanh/fixed16"
}
set example_dir [file normalize [file join $repo_dir $::env(VIVADO_EXAMPLE)]]

if {![info exists ::env(VIVADO_TEMPLATE)]} {
    set template_dir [file join $repo_dir hardware rtl fixed16]
} else {
    set template_dir [file normalize [file join $repo_dir $::env(VIVADO_TEMPLATE)]]
}

if {![info exists ::env(VIVADO_XDC)]} {
    set xdc [file join $repo_dir hardware constraints core_clk_200mhz.xdc]
} else {
    set xdc [file normalize [file join $repo_dir $::env(VIVADO_XDC)]]
}

if {[info exists ::env(VIVADO_PART)]} {
    set part $::env(VIVADO_PART)
} else {
    set part xczu9eg-ffvb1156-2-e
}

if {[info exists ::env(VIVADO_REPORTS_DIR)]} {
    set reports_dir [file normalize $::env(VIVADO_REPORTS_DIR)]
} else {
    set reports_dir [file join $example_dir vivado_core_timing_report]
}

if {![file isdirectory $example_dir] || ![file isdirectory $template_dir] || ![file exists $xdc]} {
    error "Example, template, or XDC path is missing"
}
file mkdir $reports_dir

# The RTL reads data/*.mem at elaboration; make the example the working dir.
cd $example_dir
create_project -in_memory -part $part
set_property target_language Verilog [current_project]
read_verilog [file join data pwl_config.vh]
read_mem [glob -nocomplain [file join data *.mem]]
read_verilog [glob -nocomplain [file join $template_dir *.v]]
read_xdc $xdc

synth_design -top pwl_top -part $part
opt_design
place_design
phys_opt_design
route_design

report_timing_summary -file [file join $reports_dir timing_summary.rpt] -delay_type min_max -report_unconstrained -check_timing_verbose
report_timing -from [all_registers] -to [all_registers] -max_paths 20 -file [file join $reports_dir reg2reg_timing.rpt]
report_utilization -file [file join $reports_dir utilization.rpt]
report_route_status -file [file join $reports_dir route_status.rpt]
puts "Reports written to $reports_dir"
close_project

# Core-only timing sweep.
# Run from timing_sweep_package:
#   vivado -mode batch -source scripts/run_fixed_core_timing_sweep.tcl
#
# This excludes top-level IO pad paths and reports internal register-to-register timing.

set script_dir [file normalize [file dirname [info script]]]
set pkg_dir [file normalize [file join $script_dir ".."]]
set designs_dir [file join $pkg_dir "designs"]
set constraints_dir [file join $pkg_dir "constraints"]
set reports_dir [file join $pkg_dir "reports_core"]
file mkdir $reports_dir

if {[info exists ::env(VIVADO_PART)]} {
    set part $::env(VIVADO_PART)
} else {
    set part "xczu9eg-ffvb1156-2-e"
}

if {[info exists ::env(VIVADO_JOBS)]} {
    set jobs $::env(VIVADO_JOBS)
} else {
    set jobs 8
}

if {[info exists ::env(SWEEP_DESIGNS)]} {
    set designs $::env(SWEEP_DESIGNS)
} else {
    set designs {
        exp_fx gelu_fx hswish_fx silu_fx sqrt_fx tanh_fx
        exp_fx32 gelu_fx32 hswish_fx32 silu_fx32 sqrt_fx32 tanh_fx32
    }
}

if {[info exists ::env(SWEEP_CLOCKS)]} {
    set clocks $::env(SWEEP_CLOCKS)
} else {
    set clocks {200 300 400 500}
}

if {[info exists ::env(SWEEP_REPORTS_DIR)]} {
    set reports_dir [file join $pkg_dir $::env(SWEEP_REPORTS_DIR)]
    file mkdir $reports_dir
}

if {[info exists ::env(VIVADO_IMPL_STRATEGY)]} {
    set impl_strategy $::env(VIVADO_IMPL_STRATEGY)
} else {
    set impl_strategy "default"
}

set summary_path [file join $reports_dir "core_timing_summary.csv"]
set summary [open $summary_path "w"]
puts $summary "design,clock_mhz,period_ns,setup_wns_ns,hold_wns_ns,flow_status,timing_status"

proc metric_or_na {cmd} {
    if {[catch {uplevel 1 $cmd} value]} {
        return "NA"
    }
    if {$value eq ""} {
        return "NA"
    }
    return $value
}

foreach design $designs {
    set design_dir [file join $designs_dir $design]
    set rtl_dir [file join $design_dir "rtl"]
    set data_dir [file join $design_dir "data"]

    if {![file isdirectory $rtl_dir] || ![file isdirectory $data_dir]} {
        puts "WARNING: skip $design; missing rtl/ or data/"
        continue
    }

    foreach mhz $clocks {
        set run_name "${design}_${mhz}mhz_core"
        set run_dir [file join $reports_dir $run_name]
        file mkdir $run_dir
        set xdc [file join $constraints_dir "core_clk_${mhz}mhz.xdc"]
        set flow_status "PASS"

        puts "\n=== core-only $design @ ${mhz} MHz ==="
        cd $design_dir
        create_project -in_memory -part $part
        set_property target_language Verilog [current_project]
        set_property default_lib xil_defaultlib [current_project]

        set_param general.maxThreads $jobs
        set_param project.singleFileAddWarning.threshold 0

        read_verilog [glob -nocomplain [file join $data_dir "*.vh"]]
        read_mem [glob -nocomplain [file join $data_dir "*.mem"]]
        read_verilog [glob -nocomplain [file join $rtl_dir "*.v"]]
        read_xdc $xdc

        if {[catch {
            if {$impl_strategy eq "aggressive"} {
                synth_design -top pwl_top -part $part -retiming
                opt_design -directive Explore
                place_design -directive ExtraNetDelay_high
                phys_opt_design -directive AggressiveExplore
                route_design -directive AggressiveExplore
                phys_opt_design -directive AggressiveExplore
                route_design -directive AggressiveExplore
            } else {
                synth_design -top pwl_top -part $part
                opt_design
                place_design
                phys_opt_design
                route_design
            }
        } err]} {
            puts "ERROR: $run_name failed: $err"
            set flow_status "FAIL"
        }

        report_timing_summary -file [file join $run_dir "timing_summary.rpt"] -delay_type min_max -report_unconstrained -check_timing_verbose
        report_timing -from [all_registers] -to [all_registers] -max_paths 20 -file [file join $run_dir "reg2reg_timing.rpt"]
        report_utilization -file [file join $run_dir "utilization.rpt"]
        report_route_status -file [file join $run_dir "route_status.rpt"]

        set period [expr {1000.0 / double($mhz)}]
        set wns [metric_or_na {get_property SLACK [get_timing_paths -delay_type max -max_paths 1]}]
        set whs [metric_or_na {get_property SLACK [get_timing_paths -delay_type min -max_paths 1]}]
        if {$wns eq "NA"} {
            set timing_status "UNKNOWN"
        } elseif {$wns >= 0.0} {
            set timing_status "TIMING_PASS"
        } else {
            set timing_status "TIMING_FAIL"
        }

        puts $summary "$design,$mhz,[format %.3f $period],$wns,$whs,$flow_status,$timing_status"
        flush $summary
        close_project
        cd $pkg_dir
    }
}

close $summary
puts "\nCore-only timing sweep summary: $summary_path"

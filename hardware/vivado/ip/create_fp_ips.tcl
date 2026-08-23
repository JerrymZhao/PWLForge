# Create the AMD/Xilinx Floating-Point IP required by the FP16/FP32 templates.
#
# Usage (Vivado Tcl shell or batch mode):
#   vivado -mode batch -source hardware/vivado/ip/create_fp_ips.tcl
#
# The generated IP is intentionally local and ignored by Git.  Review the
# latency constants in hardware/rtl/fp{16,32}/fp_config.vh if you change an IP
# configuration.

set script_dir [file normalize [file dirname [info script]]]
set output_dir [file join $script_dir generated]
file mkdir $output_dir

if {![info exists ::env(VIVADO_PART)]} {
    set part xczu9eg-ffvb1156-2-e
} else {
    set part $::env(VIVADO_PART)
}

create_project -force pwlforge_fp_ip $output_dir -part $part

proc create_binary_ip {name operation exponent fraction latency flow add_sub_value} {
    create_ip -name floating_point -vendor xilinx.com -library ip -module_name $name
    set_property -dict [list \
        CONFIG.Operation_Type $operation \
        CONFIG.C_A_Exponent_Width $exponent \
        CONFIG.C_A_Fraction_Width $fraction \
        CONFIG.C_Latency $latency \
        CONFIG.Flow_Control $flow \
        CONFIG.Add_Sub_Value $add_sub_value] [get_ips $name]
    generate_target all [get_ips $name]
}

proc create_compare_ip {name operation exponent fraction} {
    create_ip -name floating_point -vendor xilinx.com -library ip -module_name $name
    set_property -dict [list \
        CONFIG.Operation_Type Compare \
        CONFIG.C_Compare_Operation $operation \
        CONFIG.C_A_Exponent_Width $exponent \
        CONFIG.C_A_Fraction_Width $fraction \
        CONFIG.C_Latency 2 \
        CONFIG.Flow_Control NonBlocking] [get_ips $name]
    generate_target all [get_ips $name]
}

# The wrapper modules instantiate these exact names.
foreach spec {
    {fp16_add_ip Add_Subtract 5 11 7 Blocking Add}
    {fp16_mult_ip Multiply 5 11 6 Blocking Both}
    {fp32_add_ip Add_Subtract 8 24 11 Blocking Add}
    {fp32_mult_ip Multiply 8 24 8 Blocking Both}
} {
    lassign $spec name operation exponent fraction latency flow add_sub_value
    create_binary_ip $name $operation $exponent $fraction $latency $flow $add_sub_value
}

foreach spec {
    {fp16_compare_ge Greater_Than_Or_Equal 5 11}
    {fp16_compare_lt Less_Than 5 11}
    {fp32_compare_ge Greater_Than_Or_Equal 8 24}
    {fp32_compare_lt Less_Than 8 24}
} {
    lassign $spec name operation exponent fraction
    create_compare_ip $name $operation $exponent $fraction
}

save_project_as -force pwlforge_fp_ip $output_dir
close_project
puts "Generated floating-point IP in $output_dir"

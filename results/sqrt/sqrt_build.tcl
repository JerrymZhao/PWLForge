# Auto-generated Vivado build script for PWL
set project_name "pwl_recovery"
set project_dir "./[set project_name]_proj"
set device "xc7a35tcpg236-1"

# Create project
create_project $project_name $project_dir -part $device -force

# Create block memory IP cores for parameters
create_ip -name blk_mem_gen -vendor xilinx.com -library ip -module_name breakpoints_rom
set_property -dict [list \
    CONFIG.Memory_Type {Single_Port_ROM} \
    CONFIG.Write_Width_A {16} \
    CONFIG.Read_Width_A {16} \
    CONFIG.Write_Depth_A {47} \
    CONFIG.Read_Depth_A {47} \
    CONFIG.Enable_A {Always_Enabled} \
    CONFIG.Load_Init_File {true} \
    CONFIG.Coe_File {results/sqrt/sqrt_breakpoints.coe} \
    CONFIG.Fill_Remaining_Memory_Locations {true} \
    CONFIG.Remaining_Memory_Locations {0} \
    CONFIG.Use_RSTA_Pin {false} \
    CONFIG.EN_SAFETY_CKT {false} \
] [get_ips breakpoints_rom]

create_ip -name blk_mem_gen -vendor xilinx.com -library ip -module_name slopes_rom
set_property -dict [list \
    CONFIG.Memory_Type {Single_Port_ROM} \
    CONFIG.Write_Width_A {16} \
    CONFIG.Read_Width_A {16} \
    CONFIG.Write_Depth_A {48} \
    CONFIG.Read_Depth_A {48} \
    CONFIG.Enable_A {Always_Enabled} \
    CONFIG.Load_Init_File {true} \
    CONFIG.Coe_File {results/sqrt/sqrt_slopes.coe} \
    CONFIG.Fill_Remaining_Memory_Locations {true} \
    CONFIG.Remaining_Memory_Locations {0} \
    CONFIG.Use_RSTA_Pin {false} \
    CONFIG.EN_SAFETY_CKT {false} \
] [get_ips slopes_rom]

create_ip -name blk_mem_gen -vendor xilinx.com -library ip -module_name intercepts_rom
set_property -dict [list \
    CONFIG.Memory_Type {Single_Port_ROM} \
    CONFIG.Write_Width_A {16} \
    CONFIG.Read_Width_A {16} \
    CONFIG.Write_Depth_A {48} \
    CONFIG.Read_Depth_A {48} \
    CONFIG.Enable_A {Always_Enabled} \
    CONFIG.Load_Init_File {true} \
    CONFIG.Coe_File {results/sqrt/sqrt_intercepts.coe} \
    CONFIG.Fill_Remaining_Memory_Locations {true} \
    CONFIG.Remaining_Memory_Locations {0} \
    CONFIG.Use_RSTA_Pin {false} \
    CONFIG.EN_SAFETY_CKT {false} \
] [get_ips intercepts_rom]

# Generate IP cores
generate_target all [get_ips breakpoints_rom]
generate_target all [get_ips slopes_rom]
generate_target all [get_ips intercepts_rom]

# Add Verilog design sources
add_files -norecurse results/sqrt/sqrt_config.vh
add_files -norecurse results/sqrt/sqrt_lut.v
add_files -norecurse results/sqrt/sqrt_mem_init.v

# Run synthesis and implementation
launch_runs synth_1
wait_on_run synth_1
launch_runs impl_1 -to_step write_bitstream
wait_on_run impl_1


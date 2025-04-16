#================================================================================
# setup_simulation.tcl - Configuration script for simulation
#================================================================================

# This script helps set up the simulation environment for different functions

# Usage: 
# 1. Set the FUNCTION_NAME variable to the name of the function you want to test
# 2. Run the simulation with this script

# Define the function name
set FUNCTION_NAME "tanh"  # Change this to the function you want to test

# Setup paths
set RESULTS_DIR "../../results/$FUNCTION_NAME"
set CONFIG_FILE "$RESULTS_DIR/${FUNCTION_NAME}_config.vh"
set VECTORS_FILE "$RESULTS_DIR/sim/test_vectors/${FUNCTION_NAME}_vectors.txt"

# Check if files exist
if {![file exists $CONFIG_FILE]} {
    puts "ERROR: Config file $CONFIG_FILE not found!"
    exit 1
}

if {![file exists $VECTORS_FILE]} {
    puts "ERROR: Test vectors file $VECTORS_FILE not found!"
    exit 1
}

# Create a symbolic link to the config file
file delete -force function_config.vh
file link -symbolic function_config.vh $CONFIG_FILE

# Update test vector path in testbenches
set tb_files [list "tb_pwl_core.v" "tb_pwl_top.v"]
foreach tb $tb_files {
    if {[file exists $tb]} {
        set content [read [open $tb r]]
        regsub -all {\"../../results/function_name/sim/test_vectors/function_name_vectors.txt\"} $content "\"$VECTORS_FILE\"" new_content
        set fp [open $tb w]
        puts $fp $new_content
        close $fp
        puts "Updated test vector path in $tb"
    } else {
        puts "Warning: $tb not found"
    }
}

puts "Simulation setup complete for function: $FUNCTION_NAME"
puts "Configuration file: $CONFIG_FILE"
puts "Test vectors: $VECTORS_FILE"
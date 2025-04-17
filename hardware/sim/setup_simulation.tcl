#================================================================================
# setup_simulation.tcl - Configuration script for simulation
#================================================================================

# This script helps set up the simulation environment for different functions

# Usage: 
# 1. Set the FUNCTION_NAME variable to the name of the function you want to test
# 2. Run the simulation with this script

# Define the function name
set FUNCTION_NAME "tanh"  # Change this to the function you want to test

# Setup paths based on current project structure
set PROJ_ROOT [file normalize [file dirname [info script]]/..] 
set RTL_DIR "$PROJ_ROOT/rtl"
set INCLUDE_DIR "$PROJ_ROOT/include"
set SIM_DIR "$PROJ_ROOT/sim"
set TEST_VECTORS_DIR "$SIM_DIR/test_vectors"

# Create directories if they don't exist
file mkdir $INCLUDE_DIR
file mkdir $TEST_VECTORS_DIR

# Define file paths
set OPTIMIZED_BITWIDTHS_FILE "$INCLUDE_DIR/${FUNCTION_NAME}_optimized_bitwidths.vh"
set VECTORS_FILE "$TEST_VECTORS_DIR/${FUNCTION_NAME}_vectors.txt"

# Check if the optimized bitwidths file exists
if {![file exists $OPTIMIZED_BITWIDTHS_FILE]} {
    puts "ERROR: Optimized bitwidths file $OPTIMIZED_BITWIDTHS_FILE not found!"
    puts "Creating an empty file as placeholder. Please update with correct content."
    set fid [open $OPTIMIZED_BITWIDTHS_FILE w]
    puts $fid "// Placeholder for ${FUNCTION_NAME}_optimized_bitwidths.vh"
    puts $fid "// Replace with actual optimized bitwidth definitions"
    close $fid
}

# Check if test vectors file exists
if {![file exists $VECTORS_FILE]} {
    puts "ERROR: Test vectors file $VECTORS_FILE not found!"
    puts "Creating the file with sample data. Please update with correct vectors."
    
    # Create test vectors directory if it doesn't exist
    file mkdir [file dirname $VECTORS_FILE]
    
    # Copy the vectors you provided to the file
    set fid [open $VECTORS_FILE w]
    puts $fid "00000000"
    puts $fid "02960296"
    puts $fid "052c052c"
    # Add more vectors here...
    puts $fid "fd6ac1e0"
    puts $fid "0000c2f8"
    close $fid
}

# Update test vector path in testbenches
set tb_files [list \
    "$SIM_DIR/tb_pwl_core.v" \
    "$SIM_DIR/tb_pwl_top.v" \
]

foreach tb $tb_files {
    if {[file exists $tb]} {
        set content [read [open $tb r]]
        
        # Update the path to test vectors using a more precise pattern
        regsub -all {\$readmemh\([^,]+} $content "\$readmemh\(\"$VECTORS_FILE\"" new_content

        # Write the updated content back to the file
        set fp [open $tb w]
        puts $fp $new_content
        close $fp
        puts "Updated test vector path in $tb"
    } else {
        puts "Warning: Testbench file $tb not found"
    }
}

# Create or update the RTL files to include the proper header files
set hlut_file "$RTL_DIR/pwl_hlut.v"
if {[file exists $hlut_file]} {
    set content [read [open $hlut_file r]]
    
    # Check if include statement already exists
    if {![regexp {\`include\s+\".*optimized_bitwidths\.vh\"} $content]} {
        # Add include statement at the beginning of the file
        set new_content "// Auto-updated include statement\n\`include \"${FUNCTION_NAME}_optimized_bitwidths.vh\"\n\n$content"
        set fp [open $hlut_file w]
        puts $fp $new_content
        close $fp
        puts "Added include statement for optimized bitwidths to $hlut_file"
    } else {
        puts "Include statement already exists in $hlut_file"
    }
} else {
    puts "Warning: pwl_hlut.v file not found at $hlut_file"
}

puts "\nSimulation setup complete for function: $FUNCTION_NAME"
puts "Optimized bitwidths file: $OPTIMIZED_BITWIDTHS_FILE"
puts "Test vectors: $VECTORS_FILE"
puts "\nNext steps:"
puts "1. Ensure pwl_hlut.v has the correct memory initialization code"
puts "2. Run your simulation tool with the updated files"
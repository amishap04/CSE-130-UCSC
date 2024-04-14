#!/bin/bash

# Path to the input and output files
input_file="test_files/mix.txt"
output_file="output.txt"

# Create and populate the test file
echo "123abc#785#ayi9075#mabl4" > "$input_file"

# Run split on the mixed content file with '#' as the delimiter
./split '#' "$input_file" > "$output_file"

# Basic verification: Check if the output has the expected number of lines
expected_lines=4
actual_lines=$(wc -l < "$output_file")

# Clean-up operation
cleanup() {
    rm -f "$output_file"
}

# Verify the output
if [ "$actual_lines" -eq "$expected_lines" ]; then
    echo "SUCCESS: Mixed content file processed correctly."
    cleanup
    exit 0
else
    echo "FAILED: Mixed content file test."
    cleanup
    exit 1
fi


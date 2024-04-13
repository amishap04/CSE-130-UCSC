#!/bin/bash

# Create a file containing only symbols
echo "!!$%%^&*(%$||" > test_files/symbols.txt

# Run split on the file with '|' as the delimiter
./split '*' test_files/symbols.txt > output.txt

# Check if the output file correctly splits the content
if [ "$(wc -l < output.txt)" -eq 2 ]; then
    echo "SUCCESS: File with just symbols ran properly"
    rm -f output.txt
    exit 0
else
    echo "FAILED: File with just symbols ran properly"
    rm -f output.txt
    exit 1
fi


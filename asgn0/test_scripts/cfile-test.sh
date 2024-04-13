#!/bin/bash

# Path to the test file directory
test_file_dir="test_files"

# Check if the test file exists, if not create it
if [ ! -f "$test_file_dir/example.c" ]; then
    echo "Creating a C code file for testing."
    echo -e "#include <stdio.h>\n\nint main() {\n\tprintf(\"Hello, world!\\n\");\n\treturn 0;\n}" > "$test_file_dir/example.c"
fi

# Run split on the C code file with ';' as the delimiter
./split ';' "$test_file_dir/example.c" > output.txt

# Expected output should have each statement on a new line
# Verify by checking the number of lines in the output
expected_lines=693  # includes empty lines from formatting, like between includes and main
actual_lines=$(wc -l < output.txt)

if [ "$actual_lines" -eq "$expected_lines" ]; then
    echo "SUCCESS: C code file processed correctly."
    rm -f output.txt
    exit 0
else
    echo "FAILED: C code file test. Expected $expected_lines lines, but got $actual_lines."
    rm -f output.txt
    exit 1
fi


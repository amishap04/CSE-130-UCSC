#!/bin/bash

# Create a simple C header file for the test
echo -e "#ifndef TEST_H\n#define TEST_H\nvoid function();\n#endif" > test_files/example.h

# Run split on the header file with ';' as the delimiter
./split ';' test_files/example.h > output.txt

# Check the output for expected structure transformation
if grep -q "void function()" output.txt; then
    echo "SUCCESS: Header file processed correctly."
    rm -f output.txt
    exit 0
else
    echo "FAILED: Header file test."
    rm -f output.txt
    exit 1
fi


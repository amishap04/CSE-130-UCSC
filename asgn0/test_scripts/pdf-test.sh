#!/bin/bash

# Run split on the PDF file with 'e' as the delimiter (common byte in PDFs)
./split a test_files/resume.pdf > output.txt

# Check if the output file is not empty - rudimentary check
if [ -s output.txt ]; then
    echo "SUCCESS: PDF file was ran properly"
    rm -f output.txt
    exit 0
else
    echo "FAILED: PDF file did not run properly"
    rm -f output.txt
    exit 1
fi


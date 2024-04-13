echo "bbbbb" > test_files/all_delimiters.txt

./split b test_files/all_delimiters.txt > output.txt

if [ "$(wc -l < output.txt)" -eq 6 ]; then
    echo "SUCCESS: All delimiter characters handled correctly."
    rm -f output.txt
    exit 0
else
    echo "FAILED: All delimiter characters not handled as expected."
    echo "Output was:"
    cat output.txt
    rm -f output.txt
    exit 1
fi

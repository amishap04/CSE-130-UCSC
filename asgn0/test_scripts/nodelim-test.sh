echo "no delimiter here I wonder why the delimiter is not here or maybe it is how would you know zebra" > test_files/no_delimiter.txt

./split x test_files/no_delimiter.txt > output.txt

if diff -q test_files/no_delimiter.txt output.txt; then
    echo "SUCCESS: no delimiter found in file"
    rm -f output.txt
    exit 0
else
    echo "FAILED: File had the delimiter"
    rm -f output.txt
    exit 1
fi


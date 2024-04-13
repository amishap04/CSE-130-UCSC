# Test if split uses a sane buffer size to read.
strace -o results -e trace=read ./split a test_files/dogs.jpg > /dev/null
calls=$(grep "read" results | wc -l)
rm -f results

# Assume that 512B is the smallest sane buffer size.
if [[ $calls -gt 1796 ]]; then
    exit 1
fi

exit 0

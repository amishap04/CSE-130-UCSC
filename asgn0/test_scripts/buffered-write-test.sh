# Test if split uses a sane buffer size to write.
strace -o results -e trace=write ./split a test_files/dogs.jpg > /dev/null
calls=$(grep "write" results | wc -l)
rm -f results

# Assume that 512B is the smallest sane buffer size.
if [[ $calls -gt 1795 ]]; then
    exit 1
fi

exit 0

# Test if split on two individual files gives the same result as split
# on two files at the same time

diff \
    <(cat <(./split a test_files/dogs.jpg) <(./split a test_files/frankenstein.txt)) \
    <(./split a test_files/dogs.jpg test_files/frankenstein.txt) > /dev/null

# Test valgrind on valid file.
valgrind --error-exitcode=1 --leak-check=full ./split a test_files/dogs.jpg > /dev/null
if [[ $? -ne 0 ]]; then
    exit 1
fi

exit 0

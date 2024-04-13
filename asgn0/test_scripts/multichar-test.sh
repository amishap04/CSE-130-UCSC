./split ab test_files/dogs.jpg

if [[ $? -ne 0 ]]; then
    exit 0
fi
exit 1

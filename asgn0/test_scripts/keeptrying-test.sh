texts="test_files/dogs.jpg missing test_files/wonderful.txt"
diff <(./split a $texts) <(./rsplit a $texts) > /dev/null

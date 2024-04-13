# Tests for split on a binary input file (image of Huck and Harley)
text="test_files/dogs.jpg"
diff <(./split a $text) <(./rsplit a $text) > /dev/null

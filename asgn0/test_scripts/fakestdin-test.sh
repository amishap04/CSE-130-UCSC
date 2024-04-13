# Tests if student handles stdin if and only if the file is called "-".
text="test_files/-rickrolled.txt"
./split a $text > /dev/null

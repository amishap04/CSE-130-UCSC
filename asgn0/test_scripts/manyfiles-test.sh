# tests to see if we can overload the system with files

# Manually set hard and soft limits.
files=1024
ulimit -Hn $files -Sn $files

text="test_files/dogs.jpg"
args=""

for i in $(seq 0 $(($files+100))); do
    args+=" $text"
done

./split a $args > /dev/null

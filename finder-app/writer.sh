if [ $# -ne 2 ]; then
    echo "Error: use $0 <writefile> <writestr>"
    exit 1
fi

mkdir -p "$(dirname "$1")"

if [ ! -f "$1" ]; then
    touch "$1"
    if [ $? -eq 1 ]; then
        echo "Error: Could not create file $1"
        exit 1
    fi
fi
echo "$2" > "$1"
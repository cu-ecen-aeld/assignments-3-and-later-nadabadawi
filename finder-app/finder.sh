#!/bin/sh

if [ $# -ne 2 ]; then
    echo "Error: use $0 <filesdir> <searchstr>"
    exit 1
fi

if [ ! -d "$1" ]; then
    echo "Error: $1 is not a directory"
    exit 1
fi

filesdir="$1"
searchstr="$2"

x=$(grep -rl "$searchstr" "$filesdir" | wc -l)
y=$(grep -r "$searchstr" "$filesdir" | wc -l)

echo "The number of files are $x and the number of matching lines are $y"

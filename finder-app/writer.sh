#!/bin/sh

if [ -z $1 ] || [ -z $2 ]
then
    echo "Parameter 1 or 2 is empty"
    exit 1
fi


# Make full path, create file
mkdir -p "$(dirname "$1")" && touch "$1"
# Write contents
echo $2 > $1




#!/bin/sh

if [ -z $1 ] || [ -z $2 ]
then
    echo "Parameter 1 or 2 is empty"
    exit 1
fi
if [ ! -d $1 ]
then
    echo "Parameter 1 not a directory"
    exit 1
fi

NFILES=$(find $1 -type f | wc -l)
NLINES=$(grep -r $2 $1 | wc -l)



echo "The number of files are $NFILES and the number of matching lines are $NLINES" 




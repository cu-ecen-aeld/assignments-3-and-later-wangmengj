#!/bin/bash

if [ -z "$1" ] 
then
# the first argument is a path to a directory on the filesystem, referred to below as filesdir; 
  echo "Argument: finder-app filesdir searchstr"	
  exit 1

elif [ -z "$2" ]
then
# the second argument is a text string which will be searched within these files, referred to below as searchstr
  echo "Argument: finder-app filesdir searchstr"	
  exit 1

elif [ ! -d "$1" ]
then
  echo "Argument: filesdir needs to be a directory"	
  exit 1

fi

fNum=$(find "$1"  -type f | wc -l)
lNum=$(grep -r -c "$2" "$1"  | grep -v ":0" | awk ' BEGIN {FS="\:";s=0 } {s+=$2} END {print s}')

echo "The number of files are $fNum and the number of matching lines are $lNum"

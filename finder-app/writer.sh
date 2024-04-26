#!/bin/bash

if [ -z "$1" -o -z "$2" ] 
then
# the first argument is a full path to a file (including filename) on the filesystem, referred to below as writefile;
#the second argument is a text string which will be written within this file, referred to below as writestr
  echo "Argument: writer.sh writefile writestr"	
  exit 1

fi

# create its direcotries that is required.
if [ ! -f "$1" ]
then
mkdir -p "$(dirname "$1")" 
fi

touch "$1" && echo "$2" > "$1" && exit 0

echo "failed. "


#!/bin/bash

# Usage: ./mount.sh <binary file> <directory>
# Example: ./mount.sh ./a.out bin
# It will copy a.out to mount/bin in the disk image
if [ $# -ne 2 ]; then
    echo "Usage: ./mount.sh <binary file> <binary file name>"
    echo "Example: ./mount.sh ./mount.sh ./a.out bin"
    exit 1
fi


# copy a binary file to a specific directory under ~/.cache/gem5/x86-ubuntu-18.04-img-cpy

# if mount does not exist, create one
if [ ! -d "./mount" ]; then
    mkdir ./mount
fi

sudo mount -o loop,offset=1048576 ~/.cache/gem5/x86-ubuntu-18.04-img ./mount
sudo cp $1 ./mount/$2
sudo umount ./mount
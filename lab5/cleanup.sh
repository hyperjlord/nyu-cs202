#!/bin/bash
fusermount -u mnt      # unmount the driver
test/makeimage.bash    # make a clean testfs.img
rm -rf mnt             # remove the mounting directory and its residents
mkdir mnt              # recreate the mounting directory
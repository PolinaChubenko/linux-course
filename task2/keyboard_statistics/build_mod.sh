#!/bin/bash

sudo rmmod keyboard_statistics
sudo rm /dev/keyboard_statistics

sudo mknod /dev/keyboard_statistics c 700 0
sudo chmod a+rw /dev/keyboard_statistics
make
sudo insmod keyboard_statistics.ko


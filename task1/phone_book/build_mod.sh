#!/bin/bash

sudo rmmod phone_book
sudo rm /dev/phone_book

sudo mknod /dev/phone_book c 700 0
sudo chmod a+rw /dev/phone_book
make
sudo insmod phone_book.ko


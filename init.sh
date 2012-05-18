#!/bin/bash

mkfifo -m 644 gstbcsink_fifo0 
mkfifo -m 644 gstbcinit_fifo0
mkfifo -m 644 gstbcack_fifo0

mkfifo -m 644 gstbcsink_fifo1 
mkfifo -m 644 gstbcinit_fifo1
mkfifo -m 644 gstbcack_fifo1

mkfifo -m 644 gstbcsink_fifo2 
mkfifo -m 644 gstbcinit_fifo2
mkfifo -m 644 gstbcack_fifo2

mkfifo -m 644 gstbcsink_fifo3 
mkfifo -m 644 gstbcinit_fifo3
mkfifo -m 644 gstbcack_fifo3

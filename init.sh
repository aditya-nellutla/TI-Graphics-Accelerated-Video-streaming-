#!/bin/sh

PATH_TO_CMEM=""
insmod /opt/gfxlibraries/gfx_rel_es8.x/bufferclass_ti.ko
insmod $PATH_TO_CMEM/cmemk.ko "phys_start=0x8A000001 phys_end=0x94000001 pools=1x41943040,1x41943040,1x41943040,1x41943040 allowOverlap=1"

echo "Creating the required named pipes..."
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

mkfifo -m 644 gstcrtl_fifo

echo "Starting the Qt in background..."
./vsGUI -qws &


echo "Initialization complete, please start renderer and gst pipelines..."



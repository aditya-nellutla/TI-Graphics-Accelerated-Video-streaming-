#!/bin/sh

PATH_TO_CMEM=""
insmod /opt/gfxlibraries/gfx_rel_es8.x/bufferclass_ti.ko
insmod $PATH_TO_CMEM/cmemk.ko "phys_start=0x8A000001 phys_end=0x94000001 pools=1x41943040,1x41943040,1x41943040,1x41943040 allowOverlap=1"

echo "Creating the required named pipes..."

if [ ! -e "/opt/gstbc/gstbcsink_fifo0" ]; then
	mkfifo -m 644 /opt/gstbc/gstbcsink_fifo0
fi

if [ ! -e "/opt/gstbc/gstbcinit_fifo0" ]; then
	mkfifo -m 644 /opt/gstbc/gstbcinit_fifo0
fi

if [ ! -e "/opt/gstbc/gstbcack_fifo0" ]; then
	mkfifo -m 644 /opt/gstbc/gstbcack_fifo0
fi

if [ ! -e "/opt/gstbc/gstbcsink_fifo1" ]; then
	mkfifo -m 644 /opt/gstbc/gstbcsink_fifo1
fi

if [ ! -e "/opt/gstbc/gstbcinit_fifo1" ]; then
	mkfifo -m 644 /opt/gstbc/gstbcinit_fifo1
fi

if [ ! -e "/opt/gstbc/gstbcack_fifo1" ]; then
	mkfifo -m 644 /opt/gstbc/gstbcack_fifo1
fi

if [ ! -e "/opt/gstbc/gstbcsink_fifo2" ]; then
	mkfifo -m 644 /opt/gstbc/gstbcsink_fifo2
fi

if [ ! -e "/opt/gstbc/gstbcinit_fifo2" ]; then
	mkfifo -m 644 /opt/gstbc/gstbcinit_fifo2
fi

if [ ! -e "/opt/gstbc/gstbcack_fifo2" ]; then
	mkfifo -m 644 /opt/gstbc/gstbcack_fifo2
fi

if [ ! -e "/opt/gstbc/gstbcsink_fifo3" ]; then
	mkfifo -m 644 /opt/gstbc/gstbcsink_fifo3
fi

if [ ! -e "/opt/gstbc/gstbcinit_fifo3" ]; then
	mkfifo -m 644 /opt/gstbc/gstbcinit_fifo3
fi

if [ ! -e "/opt/gstbc/gstbcack_fifo3" ]; then
	mkfifo -m 644 /opt/gstbc/gstbcack_fifo3
fi

if [ ! -e "/opt/gstbc/gstcrtl_fifo" ]; then
	mkfifo -m 644 /opt/gstbc/gstcrtl_fifo
fi

echo "Starting the Qt in background..."
./vsGUI -qws &


echo "Initialization complete, please start renderer and gst pipelines..."



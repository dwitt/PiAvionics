#!/bin/bash
VERSION="1.0.0"
# usage: fi.sh IMU|AIR|GPS "!M"   -- send a fault-injection command to a module and print its heartbeat
P=$(ls /dev/serial/by-id/*$1* | head -1); stty -F $P 115200 raw -echo hupcl
python3 - "$P" "$2" <<PY
import os,sys,time
fd=os.open(sys.argv[1],os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK); os.write(fd,sys.argv[2].encode()); time.sleep(0.3); os.close(fd)
PY
sleep 2
case $1 in IMU) id=701;; AIR) id=702;; GPS) id=703;; esac
timeout 2 candump can0,$id:7FF | head -1 | awk '{print "heartbeat state "$8" mask "$9}'

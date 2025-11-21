#!/bin/sh

pactl load-module module-null-sink sink_name=RXcable rate=48000 sink properties="device.description=RXcable"
pactl load-module module-null-sink sink_name=TXcable rate=48000 sink properties="device.description=TXcable"

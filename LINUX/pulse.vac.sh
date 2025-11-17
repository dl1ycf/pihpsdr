#!/bin/sh

pacmd load-module module-null-sink sink_name=RXcable rate=48000 sink_properties="device.description=RXcable"
pacmd load-module module-null-sink sink_name=TXcable rate=48000 sink_properties="device.description=TXcable"

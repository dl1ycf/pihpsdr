#!/bin/sh

sudo modprobe snd-aloop index=5,6 id=RXcable,TXcable enable=1,1 pcm substreams=2,2

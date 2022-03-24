#!/bin/sh

tag=$(date +'%Y%m%dH%H%M')



cp SmartEVSE-3/.pio/build/release/firmware.bin releases/firmware_V3a_${tag}.bin
cp SmartEVSE-3/.pio/build/release/spiffs.bin releases/spiffs.bin

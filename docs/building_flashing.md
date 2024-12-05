# Building the firmware
You can get the latest release off of https://github.com/dingo35/SmartEVSE-3.5/releases, but if you want to build it yourself:
* Install platformio-core https://docs.platformio.org/en/latest/core/installation/methods/index.html
* Clone this github project, cd to the smartevse directory where platformio.ini is located
* Compile firmware.bin: `platformio run` (or `pio run`) <br>

Following these instructions on Linux you would create firmware.bin in directory /path/to/SmartEVSE-3.5/SmartEVSE-3/.pio/build/release as follows:
```
sudo apt install platformio
git clone https://github.com/dingo35/SmartEVSE-3.5.git
cd SmartEVSE-3.5/SmartEVSE
pio run
```

To enable the telnet server that allows you to online view the debug logs, add the compile flag like this:
```
PLATFORMIO_BUILD_FLAGS='-DDBG=1' pio run
```

Other compile flags:
* DDBG=0 : no logging (default)
* DDBG=2 : log via USB-C connector
* DMIN_CURRENT=5 ; decrease minimum allowed current from 6A to 5A ----> THIS IS NOT FOLLOWING THE PROTOCOLS SO AT YOUR OWN RISK !!!

For versions older than v3.6.0, build the spiffs filesystem:
* Compile spiffs.bin: `pio run -t buildfs`

If you get all kinds of mongoose compile errors (mg_....), that means that your python environment is not installed correctly.
Usually a link from python python3 solves the problem:
```
whereis python3
cd /usr/bin
sudo ln -s /usr/bin/python3 /usr/bin/python
```

If you execute:
```
python packfs.py
```
this should generate a fresh src/packed_fs.c file.


# Flashing the firmware
1. Almost always, even when your webserver seems not to be working, the http://ipaddress/update link will be working;
   this is the simplest way to flash your firmware; with the "Choose file" option you can flash any firmware[.debug].bin you downloaded or built.
   When flashing firmware older then v3.6.0, you must also flash spiffs.bin this way.
2. Alternatively, you can connect your SmartEVSE with a USB-C cable to your computer:
   * Linux users: the device will present itself usually as /dev/ttyUSB0
   * Windows users will have to install USB drivers https://www.silabs.com/de...o-uart-bridge-vcp-drivers

   You can use the following flashing software:

    1. If you have installed platformio:
       ```
       pio run -t upload
       ```

       THIS IS THE PREFERRED WAY, because it also flashes your bootloader and the partitions.bin; so whatever you messed up, this will fix it!

       For versions older than v3.6.0, upload the spiffs filesystem:

       ```
       pio run -t uploadfs
       ```
    2. esptool:
       ```
       sudo apt install esptool
       esptool --port /dev/ttyUSB0 write_flash 0x10000 firmware.bin
       esptool --port /dev/ttyUSB0 write_flash 0x1c0000 firmware.bin 
       ```
    3. Flash it with a 3rd party tool:
       A nice 3rd party tool can be found here: https://github.com/marcelstoer/nodemcu-pyflasher
       Follow the instructions in the screenshot posted here: https://github.com/dingo35/SmartEVSE-3.5/issues/79
       Remember to flash to both partitions, `0x10000` and `0x1c0000` !!!

# I think I bricked my SmartEVSE
Luckily, there are no known instances of people who bricked their SmartEVSE.

Get your preferred firmware.bin from the asset zip you can download from https://github.com/dingo35/SmartEVSE-3.5/releases, and follow the
instructions in [Flashing the firmware](#flashing-the-firmware).

If all else fails, follow the [Building the Firmware](#building-the-firmware) instructions, and flash following the "pio run -t upload" path; always works!!!

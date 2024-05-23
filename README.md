SmartEVSE v3
=========

Smart Electric Vehicle Charge Controller

![Image of SmartEVSE](/pictures/SmartEVSEv3.png)

# What is it?

It's an open source EVSE (Electric Vehicle Supply Equipment). It supports 1-3 phase charging, fixed charging cable or charging socket. Locking actuator support (5 different types). And it can directly drive a mains contactor for supplying power to the EV. It features a display from which all module parameters can be configured.<br>
Up to 8 modules can be connected together to charge up to eight EV's from one mains connection without overloading it.<br>
The mains connection can be monitored by the (optional) sensorbox or a modbus kWh meter. This allows smart charging.
Communication between the SmartEVSE(s) / Sensorbox or kWh meters is done over RS485(modbus).


# Features

- Fits into a standard DIN rail enclosure.
- Measures the current consumption of other appliances, and automatically lowers or increases the charging current to the EV. (sensorbox required)
- The load balancing feature let's you connect up to 8 SmartEVSE's to one mains supply.
- Two switched 230VAC outputs, for contactors.
- Powered RS485 communication bus for sensorbox / Modbus kWh Meters.
- Can be used with fixed cable, or socket and charging cable.
- Automatically selects current capacity of the connected cable (13/16/32A)
- Locking actuator support, locks the charging cable in the socket.
- RFID reader support, restrict the use of the charging station to max 20 RFID cards.
- An optional modbus kWh meter will measure power and charged energy, and display this on the LCD.
- Built-in temperature sensor.
- RGB led output for status information while charging.
- All module parameters can be configured using the display and buttons.
- WiFi status page.
- Firmware upgradable through USB-C port or through the built in webserver.
- REST API for communication with external software (e.g. HomeAssistant)
- MQTT API
- Rudimentary support for home batteries
- Supporting delayed charging

# Connecting the SmartESVE to WiFi

In order to connect the SmartEVSE to your local WiFi network, a temporarily hotspot is created by the SmartESVE to which you can connect using a phone/tablet.
Here you can then scan for your local WiFi, and enter your Wifi network password. Then the SmartEVSE will use this information to connect to your local Wifi network.

The steps to connect the SmartEVSE to Wifi are as follows:
- in the SmartEVSE menu, go to the option WIFI, then select SetupWiFi.
- after 10 seconds, a hotspot/access point SmartESVE-xxxx is started. (xxxx is the serial nr of your SmartEVSE)
- Using a phone or tablet, connect to this access point.
- You will be asked to enter a password. This password is visible on the top right corner of the SmartEVSE's display. (PW:xxxxxxxx)
- Once connected you will be able to select your local WiFi network, and enter the password for this network.
- click SAVE, the SmartEVSE will try to connect to your local WiFi network.
- Enter the menu of your SmartEVSE again. The SmartEVSE should now display the IP address on the top row of the display.
- use this IP address in a webbrowser to connect to the webserver of the controller. You can also use http://smartevse-xxxx.local  (replace xxxx with the serial nr of your controller)

# Updating Firmware

Connect the SmartEVSE controller to your WiFi network (using the menu of the SmartEVSE), and then browse to http://IPaddress/update where IPaddress is the IP which is shown on the display.
You can also use http://smartevse-xxxx.local/update where xxxx is the serial nr of your controller.<br>
Here you can select the firmware.bin and press update to update the firmware.<br>
It's also possible to update the spiffs partition from this page. (for v3.0.1 this is not needed)<br>
After updating the firmware, you can access the status page again using the normal url: http://smartevse-xxxx.local  (replace xxxx with the serial nr of your controller)<br>

# Documentation

[Hardware installation](docs/installation.md)<br>
[Configuration](docs/configuration.md)<br>
[Operation](docs/operation.md)<br>

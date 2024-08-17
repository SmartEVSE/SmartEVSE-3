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

For connecting your device to your WiFi, follow the detailed instructions
on the [Configuration page](docs/configuration.md#all-menu-options-on-the-lcd-screen) page, WIFI section.

# Updating Firmware

Connect the SmartEVSE controller to your WiFi network (using the menu of the SmartEVSE), and then browse to http://IPaddress/update where IPaddress is the IP which is shown on the display.
You can also use http://smartevse-xxxx.local/update where xxxx is the serial nr of your controller.<br>
Here you can select the firmware.bin and press update to update the firmware.<br>
After updating the firmware, you can access the status page again using the normal url: http://smartevse-xxxx.local  (replace xxxx with the serial nr of your controller)<br>

# Documentation

[Hardware installation](docs/installation.md)<br>
[Configuration](docs/configuration.md)<br>
[Operation](docs/operation.md)<br>

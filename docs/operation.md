# Improved starting/stopping through the LCD screen
* When pressing o button longer then 2 seconds you will enter the Menu screen
* When pressing < button longer then 2 seconds the access will be denied, i.e. the mode will be set to "Off" and charging will stop
* When pressing > button longer then 2 seconds the access will be granted, i.e. the previously set mode will be activated and charging will start

# Webserver
After configuration of your Wifi parameters, your SmartEVSE will present itself on your LAN via a webserver. This webserver can be accessed through:
* http://ip-address/
* http://smartevse-xxxx.local/ where xxxx is the serial number of your SmartEVSE. It can be found on a sticker on the bottom of your SmartEVSE. It might be necessary that mDNS is configured on your LAN.
* http://smartevse-yyyy.lan/ where yyyy is a derivative of the MAC addresss, that was shown to you when you configured your Wifi parameters.
* OTA update of your firmware:
    - surf to http://your-smartevse/update
    - select the firmware.bin from this archive, OR if you want the debug version (via telnet over your wifi),
 rename firmware.debug.bin to firmware.bin and select that. YOU CANNOT FLASH A FILE WITH ANOTHER NAME!
    - if you get FAIL, check your wifi connection and try again;
    - after OK, select spiffs.bin from this archive;
    - if you get FAIL, check your wifi connection and try again;
    - after OK, wait 10-30 seconds and your new firmware including the webserver should be online!
* Added wifi-debugging: if you flashed the debug version, telnet http://your-smartevse/ will bring you to a debugger that shows you whats going on!


# REST API
//TODO    

# MQTT API
Your SmartEVSE can now export the most important data to your MQTT-server. Just fill in the configuration data on the webserver and the data will automatically be announced to your MQTT server.
//TODO

# Error Messages
If an error occurs, the SmartEVSE will stop charging, and display one of the following messages:
* ERROR NO SERIAL COM	  CHECK WIRING<br>No signal from the Sensorbox or other SmartEVSE (when load balancing is used) has been received for 11 seconds. Please check the wiring to the Sensorbox or other SmartEVSE.
* ERROR NO CURRENT<br>There is not enough current available to start charging, or charging was interrupted because there was not enough current available to keep charging. The SmartEVSE will try again in 60 seconds.
* ERROR	HIGH TEMP<br>The temperature inside the module has reached 65º Celsius. Charging is stopped.
Once the temperature has dropped below 55ºC charging is started again.
* RESIDUAL FAULT CURRENT DETECTED<br>An optional DC Residual Current Monitor has detected a fault current, the Contactor is switched off.
The error condition can be reset by pressing any button on the SmartEVSE.



# Changes with regards to the original firmware
* Endpoint to send L1/2/3 data, this removed the need for a SensorBox
  * Note: Set MainsMeter to the new 'API' option in the config menu when sending L1/2/3
* Endpoint to send EvMeter L1/2/3 data (and energy/power)
  * Note: Set EvMeter to the new 'API' option in the config menu when sending L1/2/3
* Callable API endpoints for easy integration (see [API Overview](#API-Overview) and [Home Assistant Integration](#Integration-with-Home-Assistant))
  * Change charging mode
  * Override charge current
  * Pass in current measurements (p1, battery, ...) - this eliminates having to use additionalhard
  * Switch between single- and three phase power (requires extra 2P relais on the 2nd output)

# Simple Timer

There is a simple timer implemented on the webserver, for Delayed Charging.
* Upon refreshing your webpage, the StartTime field (next to the Mode buttons) will be filled with the current system time.
* If you press any of the Mode buttons, your charging session will start immediately;
* If you choose to enter a StartTime that is in the future, a StopTime field will open up;
  If you leave this to the default value it is considered to be empty; now if you press Normal, Solar or Smart mode
    - the StartTime will be registered,
    - the mode will switch to OFF,
    - a charging session will be started at StartTime, at either Normal or Smart mode;
    - the SmartEVSE will stay on indefinitely.
* If you enter a StopTime, a checkbox named "Daily" will open up; if you check this, the startime/stoptime combination will be used on a daily basis,
  starting on the date you entered at the StartTime.
* To clear StartTime, StopTime and Repeat, refresh your webpage and choose either Normal, Solar or Smart mode.
* Know bugs:
    - if your NTP time is not synchronized yet (e.g. after a reboot), results will be unpredictable. WAIT until time is settled.
    - if your StopTime is AFTER your StartTime+24Hours, untested territories are entered. Please enter values that make sense.

# EU Capacity Rate Limiting
An EU directive gives electricity providers the possibility to charge end consumers by a "capacity rate", so consumers will be stimulated to flatten their usage curve.
Currently the only known country that has this active is Belgium.
For more details see https://github.com/serkri/SmartEVSE-3/issues/215

* In the Menu screen an item "SumMains" is now available, default set at 600A
* This setting will only be of use in Smart or Solar mode
* Apart from all other limits (Mains, MaxCirCuit), the charge current will be limited so that the sum of all phases of the Mains currents will not be exceeding the SumMains setting
* If you don't understand this setting, or don't live in Belgium, leave this setting at its default value

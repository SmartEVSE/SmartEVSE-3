# REST API

The REST API can be accessed through any http tool, here as an example CURL will be used.

# GET: /settings

curl -X GET http://ipaddress/settings

will give output like:
```
{"version":"21:02:46 @Jan  3 2024","mode":"OFF","mode_id":0,"car_connected":false,"wifi":{"status":"WL_CONNECTED","ssid":"wifi_nomap_EXT","rssi":-82,"bssid":"28:87:BA:D6:B9:DE"},"evse":{"temp":16,"temp_max":60,"connected":false,"access":false,"mode":1,"loadbl":0,"pwm":1024,"solar_stop_timer":0,"state":"Ready to Charge","state_id":0,"error":"None","error_id":0,"rfid":"Not Installed"},"settings":{"charge_current":0,"override_current":0,"current_min":6,"current_max":16,"current_main":25,"current_max_circuit":16,"current_max_sum_mains":600,"solar_max_import":9,"solar_start_current":29,"solar_stop_time":10,"enable_C2":"Always On","modem":"Not present","mains_meter":"InvEastrn","starttime":0,"stoptime":0,"repeat":0},"mqtt":{"host":"10.0.0.28","port":1883,"topic_prefix":"SmartEVSE-51446","username":"homeassistant","password_set":true,"status":"Connected"},"home_battery":{"current":0,"last_update":0},"ev_meter":{"description":"Eastron3P","address":11,"import_active_power":0,"total_kwh":5670.1,"charged_kwh":0,"currents":{"TOTAL":1,"L1":0,"L2":0,"L3":1},"import_active_energy":5670.1,"export_active_energy":0},"mains_meter":{"import_active_energy":8614.8,"export_active_energy":5289.3},"phase_currents":{"TOTAL":75,"L1":57,"L2":6,"L3":12,"last_data_update":1704535684,"charging_L1":false,"charging_L2":false,"charging_L3":false,"original_data":{"TOTAL":75,"L1":57,"L2":6,"L3":12}},"backlight":{"timer":0,"status":"OFF"}}
```

This output is often used to add to your bug report, so the developers can see your configuration.

NOTE:
In the http world, GET parameters are passed like this:
curl -X GET http://ipaddress/endpoint?param1=value1&param2=value2
and POST parameters are passed like this:
curl -X POST http://ipaddress/endpoint -d 'param1=value1' -d 'param2=value2' -d ''

Now in the ESP world, we all have picked up the habit of using the GET way of passing parameters also for POST commands. SmartEVSE development not excluded....
From version v3.6.0 on, instead of using the Arduino Core webserver libraries, we are now using the Mongoose webserver, which is broadly used. This webserver however sticks to the "normal" http standards.

This means that if you POST a request to SmartEVSE > 3.6.0, the webserver will be waiting for the -d data until it times out (or you ctrl-C your curl command). -d ''
You can prevent this by adding
'''
-d ''
'''

to your curl POST command. -d ''

# POST: /settings
* backlight

&emsp;&emsp;Turns backlight on (1) or off (0) for the duration of the backlighttimer.

```
    curl -X POST http://ipaddress/settings?backlight=1 -d ''
```

* mode

&emsp;&emsp;Only following values are permitted:
<br>&emsp;&emsp;0: OFF
<br>&emsp;&emsp;1: NORMAL
<br>&emsp;&emsp;2: SOLAR
<br>&emsp;&emsp;3: SMART

* stop_timer

&emsp;&emsp;Set the stop timer to be used when there isn't sufficient solar power. Value must be >=0 and <= 60.
<br>&emsp;&emsp;Using 0 will disable the stop timer.

* disable_override_current

&emsp;&emsp;If this parameter is passed the override current will be reset (value doesn't matter)

* override_current

&emsp;&emsp;Works only when using NORMAL or SMART mode
<br>&emsp;&emsp;Desired current multiplied by 10
<br>&emsp;&emsp;If set to 0, override_current is disabled

<br>&emsp;&emsp;Examples:
<br>&emsp;&emsp;If the desired current is 8.3A the value to be sent is 83
```
    curl -X POST http://ipaddress/settings?override_current=83 -d ''
```

* enable_C2

&emsp;&emsp;Enables switching between 1 phase mode and 3 phase mode by controlling a 2nd contactor (C2 port)
<br>&emsp;&emsp;
<br>&emsp;&emsp;Note 1: The 2nd contactor will only be turned ON when state chages to C (Charging)
<br>&emsp;&emsp;Note 2: This is just changing the config setting, the contactor will not be controlled immediately but only when there is a
<br>&emsp;&emsp;state change.
<br>&emsp;&emsp;
<br>&emsp;&emsp;If car is charging and you want to change from 1F to 3F or vice versa:
```
  - Change mode to OFF
  - Enable or disable C2 contactor
  - Change to desired value: 0 "Not present", 1 "Always Off", 2 "Solar Off", 3 "Always On", 4 "Auto"
  - Examples:
  - If the desired C2 mode is "Solar Off", the string to be sent is 2
```
* starttime

&emsp;&emsp;Enables delayed charging; always has to be combined with sending the mode in which you want to start charging.
<br>&emsp;&emsp;
<br>&emsp;&emsp;Note 1: The time string has to be in the format "2023-04-14T23:31".
<br>&emsp;&emsp;Note 2: The time must be in the future, in local time.
<br>&emsp;&emsp;Note 3: Only valid when combined with Normal or Smart mode. Solar mode will itself decide when to start...
<br>&emsp;&emsp;
<br>&emsp;&emsp;Examples:
<br>&emsp;&emsp;If you want the car to start charging at 23:31 on April 14th 2023, in Smart mode, the strings to be sent are:

```
    curl -X POST 'http://ipaddress/settings?starttime="2023-04-14T23:31"&mode=3' -d ''
```

* solar_start_current

&emsp;&emsp;The Start Current at which the car starts charging when in Solar Mode.
<br>&emsp;&emsp;
<br>&emsp;&emsp;Examples:
<br>&emsp;&emsp;If you want the car to start charging when the sum of all 3 phases of the MainsMeter is exporting 6A or more to the grid,
<br>&emsp;&emsp;the value to be sent is 6

* current_min

&emsp;&emsp;The Minimum Charging Current in Ampères, per phase.
<br>&emsp;&emsp;Usually you should leave this setting at its default value (6A) since this is standarized. 
<br>&emsp;&emsp;Note: This setting is useful for EV's that don't obey standards, like the Renault Zoe, whose MinCurrents not only differ
<br>&emsp;&emsp;from the standard, but also change when charging at 1 phase and charging at 2 phases.
<br>&emsp;&emsp;The values even differ per build year.
<br>&emsp;&emsp;Examples:
<br>&emsp;&emsp;If you want the car to start charging at minimally 6A, the value to be sent is 6

* solar_max_import

&emsp;&emsp;The maximum current (sum of all phases) of the MainsMeter that can be imported before the solar timer is fired off,
<br>&emsp;&emsp;after expiration the car will stop charging.

<br>&emsp;&emsp;Examples:
<br>&emsp;&emsp;If you want the car to stop charging when the sum of all 3 phases of the MainsMeter is importing 0A or more to the grid,
<br>&emsp;&emsp;the value to be sent is 0

* current_max_sum_mains

&emsp;&emsp;The Maximum allowed Mains Current summed over all phases: 10-600A
<br>&emsp;&emsp;This is used for the EU Capacity rate limiting, currently only in Belgium.
<br>&emsp;&emsp;Usually you should leave this setting at its default value (600A)
<br>&emsp;&emsp;since your electricity provider probably does not supports this.

# POST: /currents

* battery_current

&emsp;&emsp;Actual home battery current multiplied by 10
<br>&emsp;&emsp;A positive number means the home battery is charging
<br>&emsp;&emsp;A negative number means the home battery is discharging

* L1, L2, L3

&emsp;&emsp;Note: Only works when MainsMeter == API
<br>&emsp;&emsp;L1, L2 and L3 must be send all together otherwise the data won't be registered.
<br>&emsp;&emsp;Ampere must be multiplied by 10
```
    curl -X POST "http://ipaddress/currents?L1=100&L2=50&L3=30" -d ''
```
&emsp;&emsp;P.S.: If you want to send your currents through HomeAsistant, look at the scripts in the (integration)[integration] directory.

# POST: /modem

* pwm

&emsp;&emsp;The duty cycle (PWM) multiplied by 10
<br>&emsp;&emsp;Examples:
<br>&emsp;&emsp;If the desired dutycycle is 5% the value to be sent is 50
<br>&emsp;&emsp;Note: EXPERIMENTAL FEATURE ONLY FOR EXPERTS
<br>&emsp;&emsp;DO NOT USE THIS IF YOU ARE NOT AN EVSE EXPERT. DANGEROUS!


# POST: /ev_meter

* L1, L2, L3

&emsp;&emsp;Note: Only works when EVMeter == API
<br>&emsp;&emsp;L1, L2 and L3 must be send all together otherwise the data won't be registered.
<br>&emsp;&emsp;Ampere must be multiplied by 10
```
    curl -X POST "http://ipaddress/ev_meter?L1=100&L2=50&L3=30" -d ''
```

* import_active_energy, export_active_energy and import_active_power

&emsp;&emsp;Note: Only works when EvMeter == API
<br>&emsp;&emsp;import_active_energy, export_active_energy and import_active_power must be send all together otherwise
<br>&emsp;&emsp;the data won't be registered.
<br>&emsp;&emsp;Data should be in Wh (kWh * 1000), for import_active_power data should be in w(att)

# POST: /reboot

&emsp;&emsp;Note: no parameters, reboots your device.

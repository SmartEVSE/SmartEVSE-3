Forked from: https://github.com/SmartEVSE/SmartEVSE-3

SmartEVSE v3 + API
=========

This is a modification of the SmartEVSE-3 software. <br/>
Goals for this modification:
* Integrate with a home battery
* Callable API endpoints for easy integration (e.g. Home Assistant)

Home Battery Integration
---------
In a normal EVSE setup a sensorbox is used to read the P1 information to deduce if there is sufficient solar energy available. This however can give unwanted results when also using a home battery as this will result in one battery charging the other one. <br/>

For this purpose the settings endpoint allows you to pass through the battery current information:
* A positive current means the battery is charging
* A negative current means the battery is discharging
<br>
The EVSE will use this current to neutralize the impact of a home battery on the P1 information.<br>

Example:
-
* Home battery is charging at 2300W -> 10A
* P1 has an export value of 230W -> -1A
* EVSE will neutralize the battery and P1 will be "exporting" -11A

Note:
The sender has several options when sending the home battery current:
* Send the current AS-IS -> EVSE current will be maximized
* Only send when battery is discharging -> AS-IS operation but EVSE will not discharge the home battery
* Reserve an amount of current for the home battery (e.g. 10A) -> Prioritize the home battery up to a specific limit

Overview endpoints:
---------
![Image of SmartEVSE](/pictures/api-1.png)

View Current Settings
---------
![Image of SmartEVSE](/pictures/api-2.png)

Change Settings
---------
![Image of SmartEVSE](/pictures/api-3.png)



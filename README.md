Forked from: https://github.com/SmartEVSE/SmartEVSE-3

# Note:
<span style="color:red">
This fork is exploring the capabilities in modifying the Smart-EVSEv3 firmware.<br/>
Feel free to use this repository to build it yourself or to use the latest on from the *releases* folder <b>but this is on your own risk</b>.
</span>
<br />
<br />

# Changes in regards with the original firmware:
* Disabled WebSockets
* Reduced max backlight brightness
* Callable API endpoints for easy integration (e.g. Home Assistant)
* Home battery integration

# Home Battery Integration
In a normal EVSE setup a sensorbox is used to read the P1 information to deduce if there is sufficient solar energy available. This however can give unwanted results when also using a home battery as this will result in one battery charging the other one. <br/>

For this purpose the settings endpoint allows you to pass through the battery current information:
* A positive current means the battery is charging
* A negative current means the battery is discharging

The EVSE will use the battery current to neutralize the impact of a home battery on the P1 information.<br>
**Regular updates from the consumer are required to keep this working as values cannot be older than 60 seconds.**

### Example:
* Home battery is charging at 2300W -> 10A
* P1 has an export value of 230W -> -1A
* EVSE will neutralize the battery and P1 will be "exporting" -11A

The sender has several options when sending the home battery current:
* Send the current AS-IS -> EVSE current will be maximized
* Only send when battery is discharging -> AS-IS operation but EVSE will not discharge the home battery
* Reserve an amount of current for the home battery (e.g. 10A) -> Prioritize the home battery up to a specific limit


# API Overview:
![Image of SmartEVSE](/pictures/api-1.png)

## Example Response

```
{
  "mode": "SOLAR",
  "mode_id": 2,
  "access": true,
  "temp": 37,
  "evse": {
    "connected": "true",
    "mode": 2,
    "state": "Connected to EV",
    "state_id": 1,
    "error": "None",
    "error_id": 0
  },
  "settings": {
    "charge_current": 16,
    "current_min": 6,
    "current_max": 16,
    "current_main": 40,
    "solar_max_import": 0,
    "solar_start_current": 6,
    "solar_stop_time": 5
  },
  "home_battery": {
    "current": 0,
    "last_update": 1647948979
  },
  "phase_currents": {
    "L1": -58,
    "L2": -90,
    "L3": -74,
    "original_data": {
      "L1": -58,
      "L2": -90,
      "L3": -74
    }
  }
}
```
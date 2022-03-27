Forked from: https://github.com/SmartEVSE/SmartEVSE-3

# Note
<span style="color:red">
This fork is exploring the capabilities in modifying the Smart-EVSEv3 firmware.<br/>
Feel free to use this repository to build it yourself or to use the latest on from the *releases* folder <b>but this is on your own risk</b>.
</span>
<br />
<br />

# Changes in regards with the original firmware
* New Status page using the Rest API
* Disabled WebSockets
* Reduced max backlight brightness
* Home battery integration
* Callable API endpoints for easy integration (e.g. Home Assistant) - (See [API Overview](#API-Overview))
  * Change charging mode
  * Override charge current
  * Pass in current measurements (p1, battery, ...) - this eliminates having to use additionalhard
  * Switch between single- and three phase power (requires extra 2P relais on the 2nd output)

# New Status Page
![Status Page](/pictures/status-page.png)


# Home Battery Integration
In a normal EVSE setup a sensorbox is used to read the P1 information to deduce if there is sufficient solar energy available. This however can give unwanted results when also using a home battery as this will result in one battery charging the other one. <br/>

For this purpose the settings endpoint allows you to pass through the battery current information:
* A positive current means the battery is charging
* A negative current means the battery is discharging

The EVSE will use the battery current to neutralize the impact of a home battery on the P1 information.<br>
**Regular updates from the consumer are required to keep this working as values cannot be older than 60 seconds.**

### Example
* Home battery is charging at 2300W -> 10A
* P1 has an export value of 230W -> -1A
* EVSE will neutralize the battery and P1 will be "exporting" -11A

The sender has several options when sending the home battery current:
* Send the current AS-IS -> EVSE current will be maximized
* Only send when battery is discharging -> AS-IS operation but EVSE will not discharge the home battery
* Reserve an amount of current for the home battery (e.g. 10A) -> Prioritize the home battery up to a specific limit

# API Overview
View API <a href="https://swagger-ui.serkri.be/" target="_blank">https://swagger-ui.serkri.be/</a>


Have an idea for the API? Edit it here <a href="https://swagger-editor.serkri.be/" target="_blank">https://swagger-editor.serkri.be/</a> and copy/paste it in a new issue with your request (https://github.com/serkri/SmartEVSE-3/issues)

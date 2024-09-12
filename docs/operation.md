# Improved Start/Stop Functionality via LCD Screen

* Pressing the "O" button for longer than 2 seconds will open the Menu screen.
* Pressing the "<" button for less than 2 seconds will toggle between Smart and Solar modes.
* Pressing the "<" button for longer than 2 seconds will deny access, setting the mode to "Off" and stopping charging.
* Pressing the ">" button for longer than 2 seconds will grant access, activating the previously set mode and resuming charging.

Simultaneously pressing both "<" and ">" buttons will refresh the LCD screen.

---

# Webserver Features

Once your Wi-Fi parameters are configured, your SmartEVSE will be accessible on your local network through a built-in webserver. Access the webserver via:

* `http://<ip-address>/`
* `http://smartevse-xxxx.local/` where `xxxx` is the serial number of your SmartEVSE (found on a sticker at the bottom). Ensure mDNS is configured on your LAN.
* `http://smartevse-xxxx.lan/` where `xxxx` is the serial number of your SmartEVSE. mDNS may need to be configured for this.

### Firmware Updates (OTA)

* Navigate to `http://<your-smartevse>/update` or press the "UPDATE" button on the webserver.
* Upload the `firmware.bin` file from this archive, or use `firmware.debug.bin` renamed to `firmware.bin` for a debug version (accessible via Telnet). Ensure the file name is correct; otherwise, flashing will fail.
* In case of failure (FAIL), check your Wi-Fi connection and retry.
* After a successful update (OK), wait 10-30 seconds for the firmware, including the webserver, to go online.

### Wi-Fi Debugging

* If the debug version is flashed, you can access the debugger via Telnet at `http://<your-smartevse>/` to monitor system activity.

### RFID List Uploads (OTA)

* Upload RFID lists via the "update" button or the `/update` endpoint by submitting a file named `rfid.txt`.
* Each line should contain one RFID (NFC) tag UID in hex format (size bytes):
    ```
    112233445566
    0A3B123FFFA0
    ```
* All existing RFID tags are deleted upon upload.
* If Power Share (Master/Slave configuration) is enabled, upload the list to each SmartEVSE device individually to maintain separate lists for each.

---

# Power Share Mode Switching

* When switching the mode on the Master device, the Slaves will automatically switch modes accordingly.
* If you change the mode on a Slave and the Master is not configured with a Smart/Solar toggle switch, the Master and all other Slaves will follow the mode change.
* If a Smart/Solar toggle switch is present, ensure that the Master and all Slaves are set to the same mode. We recommend replacing the toggle switch with a pushbutton switch for ease of use.

---

# Error Messages

If an error occurs, SmartEVSE will stop charging and display one of the following error messages:

* **ERROR: NO SERIAL COM** – No signal has been received from the Sensorbox or another SmartEVSE (used for load balancing) for 11 seconds. Please check the wiring.
* **ERROR: NO CURRENT** – Insufficient current is available to start or maintain charging. The system will retry in 60 seconds.
* **ERROR: HIGH TEMP** – The internal temperature has reached 65°C, stopping charging. Charging will resume once the temperature drops below 55°C.
* **RESIDUAL FAULT CURRENT DETECTED** – A DC Residual Current Monitor has detected a fault, and the Contactor has been switched off. Press any button to reset the error.

---

# Firmware Enhancements

* New endpoints for sending L1/2/3 data, removing the need for a SensorBox.
    * **Note**: Set MainsMeter to the 'API' option in the config menu when sending L1/2/3 data.
* New endpoints for sending EvMeter L1/2/3 data (including energy/power).
    * **Note**: Set EvMeter to the 'API' option in the config menu when sending L1/2/3 data.
* Callable API endpoints for integration with third-party systems (e.g., REST API and Home Assistant). These allow you to:
    * Change the charging mode.
    * Override the charge current.
    * Pass current measurements (e.g., p1, battery) without additional hardware.
    * Switch between single- and three-phase power (requires an extra 2P relay on the C2 connector).

---

# Simple Timer for Delayed Charging

A simple timer for delayed charging is available via the webserver.

* Upon refreshing the webpage, the "StartTime" field (next to the mode buttons) will display the current system time.
* If you press any mode button, charging will start immediately.
* If you set a future "StartTime," a "StopTime" field will appear. If you leave "StopTime" at the default, it will be ignored. Pressing Normal, Solar, or Smart mode will:
    - Register the StartTime.
    - Switch the mode to "Off."
    - Start the charging session at the designated StartTime, either in Normal or Smart mode.
    - Continue the charging session indefinitely.
* Entering a "StopTime" will enable a "Daily" checkbox, allowing the StartTime/StopTime combination to repeat daily starting from the selected date.
* To clear StartTime, StopTime, and Repeat, refresh the webpage and select Normal, Solar, or Smart mode.

### Known Bugs

* If the NTP time is not yet synchronized (e.g., after a reboot), results may be unpredictable. Wait until the system time settles.
* If the StopTime is set more than 24 hours after the StartTime, results are untested. Ensure that values make sense.

---

# EU Capacity Rate Limiting

A European Union directive allows electricity providers to charge consumers based on a "capacity rate," encouraging users to balance their energy consumption more evenly and reduce peak usage.

For more information, visit [this link](https://github.com/serkri/SmartEVSE-3/issues/215).

* A new menu option, "SumMains," has been added with a default setting of 600A.
* This setting applies in Smart or Solar mode only.
* In addition to other limits (Mains, MaxCircuit), the charging current will be restricted to ensure that the total current across all phases does not exceed the SumMains setting.
* If you are unsure how to configure this, it is recommended to leave the setting at its default value.

![image](https://user-images.githubusercontent.com/36994651/162763664-8f976840-70aa-4684-bcc6-c815cc23e701.png)

# Note
<span style="color:red">
This fork is exploring the capabilities in modifying the Smart-EVSEv3 firmware.<br/>
Feel free to use this repository to build it yourself or to use the latest on from the *releases* folder <b>but this is on your own risk</b>.
Make sure to have read the original README.md (e.g. on howto connect to wifi).
</span>
<br />
<br />

# Changes in regards with the original firmware
* New Status page using the Rest API
* Disabled WebSockets
* Reduced max backlight brightness
* Home battery integration
* Endpoint to send L1/2/3 data, this removed the need for a SensorBox
  * Note: Set MainsMeter to the new 'API' option in the config menu when sending L1/2/3
* Callable API endpoints for easy integration (e.g. Home Assistant) - (See [API Overview](#API-Overview))
  * Change charging mode
  * Override charge current
  * Pass in current measurements (p1, battery, ...) - this eliminates having to use additionalhard
  * Switch between single- and three phase power (requires extra 2P relais on the 2nd output)
* Added "Inverted Eastron" kWh, so that polarity is reversed when power is supplied to meter from below (like in most Dutch power panels)
* Added current-limiting functionality if a subpanel is used, example:

                             mains
                               |
                        [main breaker 25A]
                               |
                        [kWh meter "Mains"]
                               |
            -----------------------------------
            |            |                    |
                [group breaker 16A]   [subpanel breaker 16A]
                                              |
                                       [kWh meter "EV"]
                                              |
                                        ----------------
                                        |              |
                            [washer breaker 16A]  [smartevse breaker 16A]

   In this example you configure Mains to 25A, MaxCircuit to 16A; the charger will limit itself so that neither the 25A mains nor the 16A from the subpanel will be
   exceeded...
   Note that for this functionality you will need to be in Smart or Solar mode; it is no longer necessary to enable Load Balancing for this function.

* Added wifi-debugging: if compiled in, you can debug SmartEVSE device by telnetting to it over your wifi connection
* Small code optimisations, fixed some small bugs
* Added EXPERIMENTAL use of Contactor 2 (C2);
    - one can add a second contactor (C2) that switches off 2 of the 3 phases of a three-phase Mains installation; this can be usefull if one wants to charge of off
      Solar; EV's have a minimal charge current of 6A, so switching off 2 phases allows you to charge with a current of 6-18A, while 3 phases have a minimum current
      of 3x6A=18A. This way you can still charge solar-only on smaller solar installations.
    - one should wire C2 according to this schema:

            N    L1   L2   L3
            |    |    |    |
          --------------------
          | 4-p contactor C1 |
          --------------------
            |    |    |    |
            |    | ------------------
            |    | |2-p contactor C2|
            |    | ------------------
            |    |    |    |
          --------------------
          |    EV-cable      |
          --------------------

      This way the (dangerous) situation is avoided that some Phases are switched ON, and Neutral is switched OFF.
      Note that it is important that you actually DO NOT switch the L1 pin of the CCS plug with the C2 contactor; some cars (e.g. Tesla Model 3) will go into error;
      they expect the charging phase to be on the L1 pin when single-phase charging...

    - by default C2 is switched OFF ("Not present"); if you want to keep on charging on 3 phases after installing C2, you should change the setting Contact2 in the
      Setup Menu.


    - For this a new parameter Contact2 is introduced, with values
        - NOT_PRESENT(default),
        - ALWAYS_OFF (= 1 phase charging),
        - ALWAYS_ON (=3 phase charging),
        - SOLAR_OFF (always on except in Solar Mode where it is always off),
        - AUTO (starts charging at 3phase, but when in Solar mode and not enough current available for 3 phases, switches off C2 so it will continue on 1 phase)
          Note: this option will not work when LoadBalancing is enabled; CONTACT2 will be set to ALWAYS_ON when Loadbalancing is enabled.
          Disclaimer: this option is EXPERIMENTAL; in Normal and Smart mode there are not much problems to be expected, but in Solar mode strange Start/Stop
          scenario's might occur. As always, YOU ARE EXPERIMENTING AT YOUR OWN RISK!

    - There is a bug in the original firmware, and in the serkri firmware up until this version, that makes charging in Solar mode on a 3phase instalation,
      with a 3phase car toggle into an infinite start/stop/start.... sequence when not enough sun is available (e.g. when you only have a 1x16A solar feed).
      In order to fix this bug the behaviour of SmartEVSE is adapted:
    - When EVMeter and/of MainsMeter enabled, AND when in Smart or Solar mode, SmartEVSE now starts charging at MinCurrent (usually 6A); in the first 7 seconds it
      detects on which phases it is charging through EVMeter or MainsMeter (might be a Sensorbox)
    - if it is in Solar mode, and Contact2 is on "AUTO", and has not enough current to stay within ImportCurrent limits, it will switch
      C2 off to 1 phase charging. It will then move up the charging current to whatever is suitable.
      If current goes up again, it will NOT switch back to 3 phase charging since this is known to give problems with certain EVs.
    - if it is in Smart mode, it will now correctly limit its currents to the phases it is actually charging
    - This means the bug mentioned above is solved ONLY for people having Sensorbox and/or kWh-meter enabled.

    - Charging in Normal mode has not changed, and charging in Smart and Solar mode has not changed if you have no Sensorbox and/or kWh-meter enabled.


# New Status Page
![image](https://user-images.githubusercontent.com/36994651/160653707-121dd618-ee0d-4cb3-bc39-82fde1a1a653.png)


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

# Building the firmware

* Install platformio-core https://docs.platformio.org/en/latest/core/installation/methods/index.html
* Clone this github project, cd to the smartevse directory where platformio.ini is located
* Compile firmware.bin: platformio run
* Compile spiffs.bin: platformio run -t buildfs

If you are not using the webserver /update endpoint:
* Upload via USB configured in platformio.ini: platformio run --target upload

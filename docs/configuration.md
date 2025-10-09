
# How to configure
* First configure all settings that are shown to you (see below); don't configure your MAINSMET
* Now you are ready to test/operate your SmartEVSE in its simplest mode, called Normal Mode.
* If your EV charges at MAX current, everything works as expected, and you don't have a MAINSMET, you are done!
* If you have a MAINSMET, configure it now; browse through the settings again, since now other options have opened up
* If you are feeding your SmartEVSE with MAINS or EV data through the REST API or the MQTT API, make sure you have set up these feeds; as soon as you select "API" for the Meters, the data is expected within 11 seconds! You can use the test scripts in the test directory to feed your MQTT server with test data.
* If you configured MULTIple SmartEVSE's, follow the instructions below
* Put your SmartEVSE in Solar Mode, and some specific settings for Solar Mode will open up
* Now your SmartEVSE is ready for use!

# All menu options on the LCD screen:
## MODE
By default, you are in normal EVSE mode. You can also choose smart mode or solar mode, but these modes require configuring a [MAINS MET](#mains-met) to function.

- **Normal**: The EV will charge with the current set at [MAX](#max).
- **Smart**: The EV will charge with a dynamic charge current, depending on [MAINSMET](#mains-met) data, [MAINS](#mains), [MAX](#max) and [MIN](#min) settings.
- **Solar**: The EV will charge using solar power.

## CONFIG
Configure SmartEVSE with Type 2 Socket or fixed cable.

- **Socket**: The SmartEVSE is connected to a socket, so it will need to sense the cable used for its maximum capacity.
- **Fixed**: The SmartEVSE is connected to a fixed cable, and MAX will determine your maximum charge current.

## LOCK
Only appears when [CONFIG](#config) is set to **Socket**.

- **Disabled**: No lock is used.
- **Solenoid**: Dostar, DUOSIDA DSIEC-ELB / ELM, or Ratio lock.
- **Motor**: Signal wire reversed, DUOSIDA DSIEC-EL or Phoenix Contact.

## PWR SHARE
Power Share (formerly LOAD BAL). 2 Upto 8 SmartEVSE’s can be connected via Modbus, and the available power will be shared.

- **Disabled**: Power sharing is not used (single SmartEVSE).
- **Master**: Set the first SmartEVSE to Master. Only one Master should be set.
- **Node1-7**: Set the other SmartEVSE’s to Node 1-7.

## MAINS MET
Only appears if [MODE](#mode) is **Smart** or **Solar**. Set the type of MAINS kWh Meter.

- **Disabled**: No MAINS meter connected (only Normal mode possible).
- **Sensorbox**: The Sensorbox sends measurement data to the SmartEVSE.
- **API**: MAINS meter data is fed through the [REST API](REST_API.md) or [MQTT API](#mqtt-api).
- **Phoenix C** / **Finder** / **...** / **Custom**: A Modbus kWh meter is used.
- **HmWzrd P1**: HomeWizard P1 meter (wifi based connection to the smart meter's P1 port).

**Note**:  
- Eastron1P is for single-phase Eastron meters.  
- Eastron3P is for Eastron three-phase meters.  
- InvEastron is for Eastron three-phase meters fed from below (inverted).

If MAINS MET is not **Disabled** and not **API**, these settings appear:

- **MAINSADR**: Set the Modbus address for the kWh meter.
- **GRID**: 3 or 4 wire (only appears when Sensorbox with CT’s is used).
  - **4Wire**: Star connection with 3 phase wires and neutral.
  - **3Wire**: Delta connection with 3 phase wires without neutral.

## EV METER
Set Type of EV kWh Meter (measures power and charged energy)

- **Disabled**: No EV meter connected.
- **API**: EV meter data is fed through the REST API or MQTT API.
- **Phoenix C** / **Finder** / **...** / **Custom**: A Modbus kWh meter is used.

**Note**:  
- Eastron1P is for single-phase Eastron meters.  
- Eastron3P is for Eastron three-phase meters.  
- InvEastron is for Eastron’s three-phase meter fed from below (inverted).

If EV METER is not **Disabled** and not **API**, this setting appears:

- **EV ADR**: Set the Modbus address for the EV Meter.

## MAINS
Only appears when a [MAINS MET](#mains-met) is configured. Set max mains current (10-200A) per phase.

## MIN
Only appears when a [MAINS MET](#mains-met) is configured. Set the min charge current for the EV (6-16A) per phase.

## MAX
Set the MAX charge current for the EV: (10-80A) per phase. If [CONFIG](#config) is set to **Fixed**, configure MAX to be lower than or equal to the maximum current that your fixed cable can carry.

## CIRCUIT
Only appears when an [EV METER](#ev-meter) is configured, in **Smart** or **Solar** mode. Set the max current the EVSE circuit can handle (power sharing): 10-200A.  

## START
Only shown when [MODE](#mode) is set to **Solar** and [PWR SHARE](#pwr-share) is set to **Disabled** or **Master**. Set the current at which the EV should start solar charging: -0 to -48A (sum of all phases).

## STOP  
Only shown when [MODE](#mode) is set to **Solar** and [PWR SHARE](#pwr-share) is set to **Disabled** or **Master**. Stop charging when there is not enough solar power available. 1-60 minutes or
  - **Disabled**: Never stop charging.  
 
## IMPORT
Only shown when [MODE](#mode) is set to **Solar** and [PWR SHARE](#pwr-share) is set to **Disabled** or **Master**. Allow additional grid power when solar charging: 0-20A (sum of all phases). Use this when there is not enough solar power but you want to use as much solar power as possible.

**Important Note**: START and IMPORT are summed over all phases, and MIN is per phase!

## SWITCH
Set the Function of an External Switch (Pin SW or Connector P2).

- **Disabled**: A push button can be used to stop charging.
- **Access B**: A momentary push button is used to enable/disable access to the charging station.
- **Access S**: A toggle switch is used to enable/disable access to the charging station.
- **Sma-Sol B**: A momentary push button is used to switch between Smart and Solar modes.
- **Sma-Sol S**: A toggle switch is used to switch between Smart and Solar modes.
- **Grid Relay**: A relay from your energy provider is connected; when the relay is open, power usage is limited to 4.2kW (Energy Industry Act, par 14a).
- **Custom B**: A momentary push button can be used for external integrations.
- **Custom S**: A toggle switch can be used for external integrations.

## RCMON
Residual Current Monitor (RCM14-03) plugged into connector P1.

- **Disabled**: The RCD option is not used.
- **Enabled**: When a fault current is detected, the contactor will be opened.

## RFID
Use an RFID Card Reader to Enable/Disable Access to the EVSE. A maximum of 100 RFID cards can be stored. Only a push button can be used simultaneously with the RFID reader.

- **Disabled**: RFID reader turned off.
- **EnableAll**: Accept all learned cards for enabling/disabling the SmartEVSE.
- **EnableOne**: Only allow a single (learned) card for enabling/disabling the SmartEVSE.
  - In this mode, the lock (if used) will lock the cable in the charging socket, and the same card is used to unlock it.
- **Learn**: Learn a new card and store it in the SmartEVSE. Present a card in front of the reader, and "Card Stored" will be shown.
- **Delete**: Erase a previously learned card. Hold the card in front of the reader, and "Card Deleted" will be shown.
- **DeleteAll**: Erase all cards from the SmartEVSE.
- **Rmt/OCPP**: Authorize remotely over OCPP and bypass the SmartEVSE's local RFID storage.

## WIFI
Enable Wifi connection to your network.

- **Disabled**: Wifi connection is disabled.
- **SetupWifi** 
  - v3.9.0 or newer:
  The SmartEVSE presents itself as a Wifi Acces Point with SSID "SmartEVSE-config"; the password is displayed at the top line of your LCD display. Connect with your phone to that access point, go to [http://192.168.4.1/](http://192.168.4.1/) and configure your Wifi SSID and password.
  - v3.6.4 until v3.8.x:
    - Connect your smartphone to the wifi network you want your SmartEVSE connected to.
Note: If you have a multi AP setup, with the same SSID, you need to be connected to the desired AP, as the configuration is based on the BSSID, so it will choos the specific AP your phone is connected to.
    - Download and run the ESPTouch app from your favorite app store [Android](https://play.google.com/store/apps/details?id=com.fyent.esptouch.android&hl=en_US:) (please ignore the strange Author name) or [Apple](https://apps.apple.com/us/app/espressif-esptouch/id1071176700) or  [Github](https://github.com/EspressifApp/EsptouchForAndroid) (for source code).
    - Choose EspTouch V2.
    - Fill in the key (password) of the wifi network.
    - Fill in **1** in device count for provisioning.
    - On the SmartEVSE LCD screen, select **Wifi**, select **SetupWifi**
    - Press the middle button to start the configuration procedure.
    - Once pressed, the bottom line shows you a 16 character key, first 8 are 01234567. note that from this point on, you have 120s TO FINISH this procedure!
    - Fill in that key in the ESPTouch app, in the AES Key field
    - Leave Custom Data empty
    - Press **Confirm**, within 30 seconds the app will confirm a MAC address and an IP address.
    - You are connected now. If you want special stuff (static IP address, special DNS address), configure them on your AP/router.

  - v3.6.4 and until v3.8.x: BACKUP PROCEDURE: if you don't get it to work with the ESPTouch app, there is a backup procedure:
    - connect your SmartEVSE with a USB cable to your PC
    - install the USB driver (Windows) or not (Linux) for ESP32 chipset
    - connect your favorite serial terminal to the appropriate port,
    - use the following settings:
      - 115200 bps
      - 8 bits
      - no parity
      - 1 stopbit
    - on the SmartEVSE LCD screen, select "Wifi", select "SetupWifi"
    - press the middle button to start the configuration procedure
    - on your terminal window you should see a request to enter your WiFi access point SSID and password. 
    - the controller should now connect to WiFi.
  - v3.6.3 or older:
  The SmartEVSE presents itself as a Wifi Acces Point with SSID "smartevse-xxxx". Connect with your phone to that access point, go to [http://192.168.4.1/](http://192.168.4.1/) and configure your Wifi SSID and key (password).
- **Enabled**: Connect to your network via Wifi.

## AUTOUPDAT
Only appears when [WIFI](#wifi) is **Enabled**. Automatic update of the SmartEVSE firmware.

- **Disabled**: No automatic update.
- **Enabled**: Checks daily for a new stable firmware version and installs it when no EV is connected.  
  **Note**: This will not work if your version is not in the format `vx.y.z` (e.g., v3.6.1). Locally compiled versions or RCx versions will not auto-update.

## MAX TEMP
Maximum allowed temperature for your SmartEVSE: 40-75°C (default 65°C).  
Charging will stop once the internal temperature reaches this threshold and resume once it drops to 55°C.

## CAPACITY
Only appears when a [MAINSMET](#mains-met) is configured. Maximum allowed mains current summed over all phases: 10-600A. Used for the EU Capacity rate limiting.

## CAP STOP
Only appears when [CAPACITY](#capacity) is configured. Timer in minutes. If CAPACITY is exceeded, charging will not immediately stop but will wait until the timer expires.  
- If set to **Disabled**, charging stops immediately when CAPACITY is exceeded.

## LCD PIN
Pin code so that you can use the buttons on the LCD menu on the web-interface.
Left button increases the digit by one, Right button goes to next digit, Middle button ends entry.

## CONTACT2
Use a second contactor (C2) to switch phases L2 and L3. 

- **Not present**: The second contactor is not present, and SmartEVSE assumes 3-phase charging.
- **Always Off**: C2 is always off, single-phase charging. WE RECOMMEND THIS SETTING IF YOU ARE SINGLE PHASE CHARGING IN SOLAR MODE, EVEN IF YOU DONT HAVE A second contactor INSTALLED!
- **Always On**: C2 is always on, three-phase charging (default).
- **Solar Off**: C2 is always on except in Solar Mode, where it is always off.
- **Auto**: (only available when POWER SHARE Disabled or Master): SmartEVSE starts charging at 3-phase, but in Solar Mode, it will switch off C2 when there is not enough current for 3 phases, continuing on 1 phase; if there is enough current it will switch on C2, continuing on 3 phases. In Smart mode we will charge 3P, since we assume you are switching to Smart mode because not enough sun is available for Solar mode.

**Important**: Wire your C2 contactor according to the schematics in the [Hardware installation](installation.md).

# SINGLE PHASE CHARGING

SmartEVSE calculates with currents per phase; a problem arises in Solar mode, because there Isum (the sum of the currents of all phases) has to be guarded; to calculate with it per phase you have to know the number of phases you are charging.
We try to detect the number of phases you are charging with, with the help of the settings of the second contator C2 and the EVMeter, if present.
This detection can fail easily; not always an EVMeter is present, and even if there is, an EV could determine to start charging at one phase and later on add more phases (Teslas are known to do this); an EV could even decide during the charging process to stop charging on certain phases.
We could introduce a setting "1phase/3phase" charging, but this setting would be EV dependent if you are on a 3 phase grid; so you would have to change the setting every time another EV connects to your SmartEVSE.

Currently the most reliable way to get the correct behaviour at Solar mode is:
- if you are on a 3 phase grid and you are 3 phase charging, you have no problem
- if you are on a 3 phase grid and you are 1 phase charging in Solar mode, set CONTACT2 to "Always Off", even if you don't have a second contactor installed; it will tell the algorithm to calculate with single phase
- if you are on a 1 phase grid, set CONTACT2 to "Always Off" since you will always be charging single phase

If you are at Smart mode you just set CONTACT2 to the appropriate setting as documented above.

# OCPP (you want your company to pay for your electricity charges, or you want to exploit your SmartEVSE as a public charger)
To charge a company or a user for your electricity cost, you need a Backend Provider (BP). The BP will monitor your charger usage and will bill the appropriate user and/or company, and will pay you your part.
Your SmartEVSE can be connected to any BP by the OCPP protocol.
See the OCPP section in the SmartEVSE dashboard for setting up identifiers and configuring the OCPP interface.
Connect to the OCPP server using the credentials set up in the SmartEVSE dashboard. To use
the RFID reader with OCPP, set the mode Rmt/OCPP in the RFID menu. Note that the other
RFID modes overrule the OCPP access control. OCPP SmartCharging requires the SmartEVSE
internal load balancing needs to be turned off.
For user experiences with back-end providers, see [OCPP Backends](ocpp.md)

# REST API

For the specification of the REST API, see [REST API](REST_API.md)

# MQTT API
Your SmartEVSE can now export the most important data to your MQTT-server. Just fill in the configuration data on the webserver and the data will automatically be announced to your MQTT server. Note that because the configuration data is transported to the SmartEVSE via the URL, special characters are not allowed.

You can easily show all the MQTT topics published:
```
mosquitto_sub -v -h ip-of-mosquitto-server -u username -P password  -t '#'
```

You can feed the SmartEVSE data by publishing to a topic:
```
mosquitto_pub  -h ip-of-mosquitto-server -u username -P password -t 'SmartEVSE-xxxxx/Set/CurrentOverride' -m 150
```
...where xxxxx your SmartEVSE's serial number, will set your Override Current to 15.0A.

Valid topics you can publish to are:
```
/Set/Mode
/Set/CurrentOverride
/Set/CurrentMaxSumMains
/Set/CPPWMOverride
/Set/MainsMeter
/Set/EVMeter
/Set/HomeBatteryCurrent
/Set/RequiredEVCCID
/Set/ColorOff
/Set/ColorNormal
/Set/ColorSmart
/Set/ColorSolar
/Set/CableLock
/Set/EnableC2  0 "Not present", 1 "Always Off", 2 "Solar Off", 3 "Always On", 4 "Auto" ; do not change during charging to prevent unexpected errors of your EV!
               You can send either the number or the string, SmartEVSE will accept both!
```
Your mains kWh meter data can be fed with:
```
mosquitto_pub  -h ip-of-mosquitto-server -u username -P password -t 'SmartEVSE-xxxxx/Set/MainsMeter' -m L1:L2:L3
```
...where L1 - L3 are the currents in deci-Ampères. So 100 means 10.0A importing, -5 means 0.5A exporting.
...These should be fed at least ervery 10 seconds.

Your EV kWh meter data can be fed with:
```
mosquitto_pub  -h ip-of-mosquitto-server -u username -P password -t 'SmartEVSE-xxxxx/Set/EVMeter' -m L1:L2:L3:P:E
```
...where L1 - L3 are the currents in deci-Ampères. So 100 means 10.0A.
...where P is the Power in W,
...where E is the Energy in Wh.

You can find test scripts in the [test directory](https://github.com/SmartEVSE/SmartEVSE-3/tree/master/SmartEVSE-3/test) that feed EV and MainsMeter data to your MQTT server.

# Multiple SmartEVSE controllers on one mains supply (Power Share)
Up to eight SmartEVSE modules can share one mains supply.
  - Hardware connections
    - Connect the A, B and GND connections from the Master to the Node(s).
    - So A connects to A, B goes to B etc.
    - If you are using Smart/Solar mode, you should connect the A, B , +12V and GND wires from the sensorbox to the same screw terminals of the SmartEVSE! Make sure that the +12V wire from the sensorbox is connected to only -one– SmartEVSE.

  - Software configuration
    - Set one SmartEVSE PWR SHARE setting to MASTER, and the others to NODE 1-7. Make sure there is only one Master, and the Node numbers are unique.
    - On the Master configure the following:
      - MODE	  Set this to Smart if a Sensorbox (or configured kWh meter) is used to measure the current draw on the mains supply.
      It will then dynamically vary the charge current for all connected EV’s.  If you are using a dedicated mains supply for the EV’s you can leave this set to Normal.
      - MAINS Set to the maximum current of the MAINS connection (per phase).
      If the sensorbox or other MainsMeter device measures a higher current than this value on one of the phases, it will immediately reduce the current to the EVSE’s
      - CIRCUIT Set this to the maximum current of the EVSE circuit (per phase).
      This will be split between the connected and charging EV’s.
      - MAX 		 Set the maximum charging current for the EV connected to -this- SmartEVSE (per phase).
      - MIN		 Set to the lowest allowable charging current for all connected EV’s.
    - On the Nodes configure the following:
      - MAX 		 Set the maximum charging current for the EV connected to -this- SmartEVSE (per phase).

# Home Battery Integration
In a normal EVSE setup, a sensorbox is used to read the P1 information to deduce if there is sufficient solar energy available. This however can give unwanted results when also using a home battery as this will result in one battery charging the other one.

For this purpose the settings endpoint allows you to pass through the battery current information:
* A positive current means the battery is charging
* A negative current means the battery is discharging

The EVSE will use the battery current to neutralize the impact of a home battery on the P1 information.

**Regular updates from the consumer are required to keep this working as values cannot be older than 11 seconds.**

**The battery currents are ONLY taken into account in Solar mode!!

### Example
* Home battery is charging at 2300W -> 10A
* P1 has an export value of 230W -> -1A
* EVSE will neutralize the battery and P1 will be "exporting" -11A

The sender has several options when sending the home battery current:
* Send the current AS-IS -> EVSE current will be maximized
* Only send when battery is discharging -> AS-IS operation but EVSE will not discharge the home battery
* Reserve an amount of current for the home battery (e.g. 10A) -> Prioritize the home battery up to a specific limit

# Integration with Home Assistant
There are three options to integrate your SmartEVSE with Home Assistant:

## By MQTT (preferred)

If you already use MQTT in your Home Assistant setup, this is the easiest and fastest way to integrate your SmartEVSE into HA. As soon as you have MQTT configured correctly in the SmartEVSE, the device will automatically be discovered by Home Assistant!

> [!TIP]
> Just add the MQTT details in the SmartEVSE and you're good! There is no further integration needed to set up, you will find the SmartEVSE listed on the [MQTT integration page](https://my.home-assistant.io/redirect/integration/?domain=mqtt). Not even a HA restart needed!

## Through the HA-integration - DEPRECATED

If you cannot (or do not want to) use MQTT to integrate your SmartEVSE with Home Assistant, please have a look at [the SmartEVSE `custom_component` for Home Assistant](https://github.com/dingo35/ha-SmartEVSEv3). This `custom_component` uses the REST API to share data from the SmartEVSE to Home Assistant, and enables you to set SmartEVSE settings from Home Assistant. You will need SmartEVSE firmware version 1.5.2 or higher to use this integration.

> [!WARNING]
>  Because of how this `custom_component` and the REST API works, data updates will arrive considerably slower in HA when compared to the MQTT integration. When possible, consider using MQTT.

## By manually configuring your configuration.yaml

It's a lot of work, but you can have everything exactly your way. See examples in the integrations directory of our GitHub repository.



# EU Capacity Rate Limiting

In line with a EU directive, electricity providers can implement a "capacity rate" for consumers, encouraging more balanced energy consumption. This approach aims to smooth out usage patterns and reduce peak demand.

For further details, please refer to [serkri#215](https://github.com/serkri/SmartEVSE-3/issues/215).



* The menu item "Capacity" can be set from 10-600A. (sum of all phases)
* This setting applies only in Smart or Solar mode.
* Beyond existing limits (Mains, MaxCircuit), the charging current will be controlled to ensure that the total of all Mains phase currents does not exceed the Capacity setting.
* If you are unfamiliar with this setting or do not fall under the applicable regulations, it is advisable to keep the setting at its default setting. (disabled)



# How to configure
* First configure all settings that are shown to you (see below); don't configure your MAINSMET
* Now you are ready to test/operate your SmartEVSE in its simplest mode, called Normal Mode.
* If your EV charges at MAX current, and everything works as expected, and you don't have a MAINSMET, you are done!
* If you have a MAINSMET, configure it now; browse through the settings again, since now other options have opened up
* If you are feeding your SmartEVSE with MAINS or EV data through the REST API or the MQTT API, make sure you have set up these feeds; as soon as you select "API" for the Meters, the data is expected within 11 seconds! You can use the test scripts in the test directory to feed your MQTT server with test data.
* If you configured MULTIple SmartEVSE's, follow instructions below
* Put your SmartEVSE in Solar Mode, and some specific settings for Solar Mode will open up
* Now your SmartEVSE is ready for use!

# All menu options on the LCD screen:
```
MODE:
        Per default you are in Normal EVSE mode; you can also choose Smart Mode or Solar Mode,
        but you will have to configure a MAINSMETer to actually use these modes. 
  <Normal>	The EV will charge with the current set at MAX
  <Smart>	The EV will charge with a dynamic charge current, depending on MAINSMET
                data, and MAINS, MAX, MIN settings
  <Solar>       The EV will charge on solar power

CONFIG  Configure EVSE with Type 2 Socket or fixed cable:
  <Socket>      Your SmartEVSE is connected to a socket, so it will need to sense the
                cable used for its maximum capacity
  <Fixed>       Your SmartEVSE is connected to a fixed cable, so MAX will determine your
                maximum charge current

LOCK    (only appears when CONFIG is set to <Socket>)
        Enable or disable the locking actuator (config = socket)
  <Disabled>    No lock is used
  <Solenoid>	Dostar, DUOSIDA DSIEC-ELB or Ratio lock
  <Motor>	Signal wire reversed, DUOSIDA DSIEC-EL or Phoenix Contact

PWR SHARE  ; formerly known as LOADBALANCING.
        2 to 8 EVSE’s can be connected via modbus, and their load will be balanced
  <Disabled>	Single SmartEVSE
  <Master>	Set the first SmartEVSE to Master. Make sure there is only one Master.
  <Node1-7>	And the other SmartEVSE's to Node 1-7.

PRIORITY (only appears when PWR SHARE set to <Master>)
        Determines the priority when multiple EVSEs are asking for power and there is
        not enough current to provide them all with MinCurrent.
  <NodeNr>      Priority is given to already charging EV's, with highest priority to Master,
                then to Node1, then to Node2 etc.
  <FirstConn>   Priority is given to already charging EV's, with highest priority to the EV
                that was first connected to its EVSE.
                The thought is: "First come, first serve"
  <LastConn>    Priority is given to already charging EV's, with highest priority to the EV
                that was last connected to its EVSE.
                The thought is: the EV that is connected the longest, has already the most
                charge; a charge from 5% -> 15% is more valuable to that EV
                than a charge from 80% -> 90% .

MAINSMET Set type of MAINS meter (only appears in Smart or Solar mode):
  <Disabled>    No MAINS meter connected; only Normal mode possible
  <Sensorbox>   the Sensorbox will send measurement data to the SmartEVSE
  <API>         The MAINS meter data will be fed through the REST API or the MQTT API.
  <Phoenix C> / <Finder> / <...> / <Custom> a Modbus kWh meter is used

  Note that Eastron1P is for single phase Eastron meters, Eastron3P for Eastron three phase
  meters, and InvEastron is for Eastron three phase meter that is fed from below (inverted).
  If MAINSMET is not <Disabled> and not <API>, these settings appear:
  MAINSADR  Set the Modbus address for the kWh meter
  GRID      (only appears when Sensorbox with CT’s is used)
            3 or 4 wire
  CAL	    Calibrate CT1. CT2 and CT3 will use the same cal value.
	    6.0-99.9A	A minimum of 6A is required in order to change this value.
            Hold both ▼and ▲ buttons to reset to default settings.

EV METER Set type of EV kWh meter (measures power and charged energy)
  <Disabled>  No EV meter connected.
  <API>         The EV meter data will be fed through the REST API or the MQTT API.
  <Phoenix C> / <Finder> / <...> / <Custom> a Modbus kWh meter is used

  Note that Eastron1P is for single phase Eastron meters, Eastron3P for Eastron three phase
  meters, and InvEastron is for Eastron three phase meter that is fed from below (inverted).
  If EV METER is not <Disabled> and not <API>, this setting appears:
  EV ADR   Set the Modbus address for the EV Meter

MAINS	(only appears when a MAINSMET is configured):
        Set Max Mains current: 10-200A (per phase)

MIN     (only appears when a MAINSMET is configured):
        Set MIN charge current for the EV: 6-16A (per phase)

MAX	Set MAX charge current for the EV: 10-80A (per phase)
        If CONFIG is set to <Fixed>, configure MAX lower or equal to the maximum current
        that your fixed cable can carry.

CIRCUIT
        If PWR SHARE set to <Disabled>:
        Only appears when an EV METER is configured, in Smart or Solar mode.
        Set the max current the EVSE circuit can handle (load balancing): 10-200A
        Not obeyed in Normal mode.
        (see also subpanel wiring)

        If PWR SHARE set to <Master>:
        Set the max current the EVSE circuit can handle (load balancing): 10-200A
        Obeyed in all modes!

SWITCH  Set the function of an external switch connected to pin SW
  <Disabled>    A push button on io pin SW can be used to STOP charging
  <Access B>    A momentary push Button is used to enable/disable access to the charging station
  <Access S>    A toggle switch is used to enable/disable access to the charging station
  <Sma-Sol B>   A momentary push Button is used to switch between Smart and Solar modes
  <Sma-Sol S>   A toggle switch is used to switch between Smart and Solar modes
  <Grid Relay>  A relay, provided by your energy provider, is connected; when the relay is open, power usage
                is limited to 4.2kW, as per par 14a of the Energy Industry Act.

RCMON   RCM14-03 Residual Current Monitor is plugged into connector P1
  <Disabled>    The RCD option is not used
  <Enabled>     When a fault current is detected, the contactor will be opened

RFID    use a RFID card reader to enable/disable access to the EVSE
        A maximum of 20 RFID cards can be stored.
  <Disabled>  RFID reader turned off
  <EnableAll> Accept all learned cards for enabling/disabling the SmartEVSE
  <EnableOne> Only allow a single (learned) card to be used for enabling/disabling the
              SmartEVSE. In this mode the lock (if used) will lock the cable in the charging
              socket, and the same card is used to unlock it again
  <Learn>     Learn a new card and store it into the SmartEVSE. Make sure you stay in the
              menu when learning cards. Present a card in front of the reader. "Card Stored"
              will be shown on the LCD
  <Delete>    Erase a previous learned card. Hold the card in front of the reader. "Card
              Deleted" will be shown on the LCD once the card has been deleted
  <DeleteAll> Erase all cards from the SmartEVSE. The cards will be erased once you exit
              the menu of the SmartEVSE
  <Rmt/OCPP>  Authorize remotely over OCPP and bypass the SmartEVSE local RFID storage. For
              offline storage, use OCPP local lists. SmartEVSE sends RFID readings to the
              OCPP server in this mode only

WIFI          Enable wifi connection to your LAN
  <Disabled>  No wifi connection
  <SetupWifi> v3.6.3 or older: The SmartEVSE presents itself as a Wifi Acces Point "smartevse-xxxx";
              connect with your phone to that access point, goto http://192.168.4.1/
              and configure your Wifi password
              v.3.6.4 and newer: On your smartphone:
              -connect your smartphone to the wifi network you want your SmartEVSE connected to
              -download and run the ESPTouch app from your favourite appstore
.              [Android](https://play.google.com/store/apps/details?id=com.fyent.esptouch.android&hl=en_US:)
.              (please ignore the strange Author name) or
.              [Apple](https://apps.apple.com/us/app/espressif-esptouch/id1071176700) or
.              [Github](https://github.com/EspressifApp/EsptouchForAndroid) (for source code).
              -choose EspTouch V2,
              -fill in the password of the wifi network,
              -fill in "1" in device count for provisioning,
              -on the SmartEVSE LCD screen, select "Wifi", select "SetupWifi",
              -press the middle button to start the configuration procedure,
              -once pressed, the bottom line shows you a 16 character key, first 8 zeros,
              -note that from this point on, you have 120s TO FINISH this procedure!
              -fill in that key in the ESPTouch app, in the AES Key field
              -leave Custom Data empty
              -press "Confirm", within 30 seconds the app will confirm a MAC address and an IP address
              You are connected now. If you want special stuff (static IP address, special DNS address),
              configure them on your AP/router.

              v3.6.4 and newer BACKUP PROCEDURE: if you don't get it to work with the ESPTouch app, there is
              a backup procedure:
              -connect your SmartEVSE with a USB cable to your PC
              -install the USB driver (Windows) or not (linux) for ESP32 chipset
              -connect your favorite serial terminal to the appropriate port
              -on the SmartEVSE LCD screen, select "Wifi", select "SetupWifi",
              -press the middle button to start the configuration procedure,
              -wait for 120s ; then press enter on your serial terminal; you will be prompted for your WiFi
               network name and password.
  <Enabled>   Connect to your LAN via Wifi.

OCPP          Enable OCPP
              See the OCPP section in the SmartEVSE dashboard for setting up identifiers and configuring
              the OCPP interface.
  <Disabled>  No OCPP functionality including OCPP access control and load managment
  <Enabled>   Connect to the OCPP server using the credentials set up in the SmartEVSE dashboard. To use
              the RFID reader with OCPP, set the mode Rmt/OCPP in the RFID menu. Note that the other
              RFID modes overrule the OCPP access control. OCPP SmartCharging requires the SmartEVSE
              internal load balancing means to be turned off.

AUTOUPDAT     (only appears when WIFI is Enabled):
              Automatic update of your firmware
  <Disabled>  No automatic update
  <Enabled>   Checks every 18 hours if there is a new stable firmware version available;
              If so, downloads and flashes it, and reboots as soon as no EV is connected.
              DOES NOT WORK if your current version is not one of the format vx.y.z, e.g. v3.6.1
              So locally compiled versions, or RCx versions, will NOT Autoupdate!

MAX TEMP      Maximum allowed temperature for your SmartEVSE; 40-75C, default 65.
              You can increase this if your SmartEVSE is in direct sunlight.

CAPACITY      (only appears when a MAINSMET is configured):
              Maximum allowed Mains Current summed over all phases: 10-600A
              This is used for the EU Capacity rate limiting, currently only in Belgium
CAP STOP      (only appears when a SUMMAINS is configured):
              Timer in minutes; if set, if SUMMAINS is exceeded, we do not immediately stop
              charging but wait until the timer expires.

The following options are only shown when Mode set to <Solar> and
PWR SHARE set to <Disabled> or <Master>:
START         set the current on which the EV should start Solar charging:
              -0  -48A (sum of all phases)
STOP          Stop charging when there is not enough solar power available:
              Disabled - 60 minutes (Disabled = never stop charging)
IMPORT        Allow additional grid power when solar charging: 0-20A (summed over all phases)
              NOTE: A setting of IMPORT lower thant START + MIN makes NO SENSE and will
              result in a non-charging SmartEVSE when in Solar mode.
              You even need to set IMPORT at least a few Amps higher then START + MIN to get
              a desired charging behaviour if you are charging at 1 phase.
              You even need to set IMPORT at least a few Amps higher then START + 3 * MIN to get
              a desired charging behaviour if you are charging at 3 phases.
              NOTE2: Note that START and IMPORT are summed over all phases, and MIN is per phase!
CONTACT2      One can add a second contactor (C2) that switches off 2 of the 3 phases of a
              3 phase Mains installation; this can be useful if one wants to charge of off
              Solar; EV's have a minimal charge current of 6A, so switching off 2 phases
              allows you to charge with a current of 6-18A, while 3 phases have a
              minimum current of 3x6A=18A.
              This way you can still charge solar-only on smaller solar installations.
<br>
              IMPORTANT NOTE: You WILL have to wire your C2 contactor according to the schematics
              in [Hardware installation](docs/installation.md). If you invent your own wiring
              your installation will be UNSAFE!

  <Not present> No second contactor C2 is present (default);
                In this case SmartEVSE will assume 3 phase charging, which is "worst case"
  <Always Off>  C2 is always off, so you are single phase charging
                You can use this setting if you want SmartEVSE to assume 1 phase charging in its calculations
  <Always On>   C2 is always on, so you are three phase charging (if your Mains are three phase and your EV
                supports it)
  <Solar Off>   C2 is always on except in Solar Mode where it is always off
  <Auto>        SmartEVSE starts charging at 3phase, but when in Solar mode and not enough
                current available for 3 phases, switches off C2 so it will continue on 1 phase


```
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
```
Your MainsMeter can be fed with:
```
mosquitto_pub  -h ip-of-mosquitto-server -u username -P password -t 'SmartEVSE-xxxxx/Set/MainsMeter' -m L1:L2:L3
```
...where L1 - L3 are the currents in deci-Ampères. So 100 means 10.0A.

You can find test scripts in the test directory that feed EV and MainsMeter data to your MQTT server.

# Multiple SmartEVSE controllers on one mains supply (Power Share)
Up to eight SmartEVSE modules can share one mains supply.
  - Hardware connections
    - Connect the A, B and GND connections from the Master to the Node(s).
    - So A connects to A, B goes to B etc.
    - If you are using Smart/Solar mode, you should connect the A, B , +12V and GND wires from the sensorbox to the same screw terminals of the SmartEVSE! Make sure that the +12V  wire from the sensorbox is connected to only  -one– SmartEVSE.

  - Software configuration
    - Set one SmartEVSE PWR SHARE setting to MASTER, the others to NODE 1-7. Make sure there is only one Master, and the Node numbers are unique.
    - On the Master configure the following:
      - MODE	  Set this to Smart if a Sensorbox (or configured kWh meter) is used to measure the current draw on the mains supply.
      It will then dynamically vary the charge current for all connected EV’s.  If you are using a dedicated mains supply for the EV’s you can leave this set to Normal.
      - MAINS Set to the maximum current of the MAINS connection (per phase).
      If the sensorbox or other MainsMeter device measures a higher current then this value on one of the phases, it will immediately reduce the current to the EVSE’s
      - CIRCUIT Set this to the maximum current of the EVSE circuit (per phase).
      This will be split between the connected and charging EV’s.
      - MAX 		 Set the maximum charging current for the EV connected to -this- SmartEVSE (per phase).
      - MIN		 Set to the lowest allowable charging current for all connected EV’s.
    - On the Nodes configure the following:
      - MAX 		 Set the maximum charging current for the EV connected to -this- SmartEVSE (per phase).

# Home Battery Integration
In a normal EVSE setup a sensorbox is used to read the P1 information to deduce if there is sufficient solar energy available. This however can give unwanted results when also using a home battery as this will result in one battery charging the other one. <br/>

For this purpose the settings endpoint allows you to pass through the battery current information:
* A positive current means the battery is charging
* A negative current means the battery is discharging

The EVSE will use the battery current to neutralize the impact of a home battery on the P1 information.<br>
**Regular updates from the consumer are required to keep this working as values cannot be older than 11 seconds.**

### Example
* Home battery is charging at 2300W -> 10A
* P1 has an export value of 230W -> -1A
* EVSE will neutralize the battery and P1 will be "exporting" -11A

The sender has several options when sending the home battery current:
* Send the current AS-IS -> EVSE current will be maximized
* Only send when battery is discharging -> AS-IS operation but EVSE will not discharge the home battery
* Reserve an amount of current for the home battery (e.g. 10A) -> Prioritize the home battery up to a specific limit

# Integration with Home Assistant
There are three options to integrate SmartEVSE with Home Assistant:
* through the HA-integration - the easy way<br />

    If you want to integrate your SmartEVSE with Home Asisstant, please have a look at [the SmartEVSE `custom_component` for Home Assistant](https://github.com/dingo35/ha-SmartEVSEv3). This `custom_component` uses the API to share data from the SmartEVSE to Home Assistant, and enables you to set SmartEVSE settings from Home Assistant. You will need firmware version 1.5.2 or higher to use this integration.

* by manually configuring your configuration.yaml<br />

    Its a lot of work, but you can have everything exactly your way. See examples in the integrations directory of our github repository.

* by MQTT<br />

    If you don't like the integration, e.g. because it only updates its data every 60 seconds, you might like to interface through MQTT; updates are done as soon as values change.... you can even mix it up by using both the integration AND the MQTT interface at the same time!

# EU Capacity Rate Limiting
An EU directive gives electricity providers the possibility to charge end consumers by a "capacity rate", so consumers will be stimulated to flatten their usage curve.
Currently the only known country that has this active is Belgium.
For more details see https://github.com/serkri/SmartEVSE-3/issues/215

* In the Menu screen an item "SumMains" is now available, default set at 600A
* This setting will only be of use in Smart or Solar mode
* Apart from all other limits (Mains, MaxCirCuit), the charge current will be limited so that the sum of all phases of the Mains currents will not be exceeding the SumMains setting
* If you don't understand this setting, or don't live in Belgium, leave this setting at its default value

# Building the firmware
You can get the latest release off of https://github.com/dingo35/SmartEVSE-3.5/releases, but if you want to build it yourself:
* Install platformio-core https://docs.platformio.org/en/latest/core/installation/methods/index.html
* Clone this github project, cd to the smartevse directory where platformio.ini is located
* Compile firmware.bin: platformio run
* If your want to give special compiler options, e.g. lower the minimum current to 5A:  PLATFORMIO_BUILD_FLAGS='-DDBG=1 -DMIN_CURRENT=5' pio run
For versions older than v3.6.0:
* Compile spiffs.bin: platformio run -t buildfs

If you are not using the webserver /update endpoint:
* Windows users: install USB drivers https://www.silabs.com/de...o-uart-bridge-vcp-drivers
* Upload via USB configured in platformio.ini: platformio run --target upload

# I think I bricked my SmartEVSE
Luckily for you, there are no known instances of people who actually bricked their SmartEVSE.
But if all else fails, connect your SmartEVSE via USB-C to your laptop and follow the instruction https://github.com/dingo35/SmartEVSE-3.5/issues/79

Another tool can be found here: https://github.com/marcelstoer/nodemcu-pyflasher

Remember to flash to both partitions, 0x10000 and 0x1c0000  !!!

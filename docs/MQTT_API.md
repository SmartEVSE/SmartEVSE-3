# MQTT API

Below is a list of MQTT topics the Smart EVSE v 3.6 can use.

Each MQTT topic starts with the topic prefix, which can be set in the MQTT config section of html UI, accessible via http://SmartEVSE-xxxx/
The default topic prefix is SmartEVSE-xxxx, where xxxx is your EVSE serial number.
Below we use "<prefix>" for this.

# EVSE MQTT settable
*  <prefix> "/Set/Mode" = [String: one of "Off"|"Normal"|"Smart"|"Solar"]

*  <prefix> "/Set/CurrentOverride" = [Integer: deciAmpere, <MinCurrent>...<MaxCurrent>]

*  <prefix> "/Set/CurrentMaxSumMains" = [Integer: deciAmpere, 10...600]

*  <prefix> "/Set/CPPWMOverride" = [Integer: <pwmvalue>, -1...1024]

*  <prefix> "/Set/MainsMeter" = [String: "l1:L2:l3",
	where L1..L3 is integer value in deciAmpere, -2000...+2000]

*  <prefix> "/Set/EVMeter" = [String: "l1:L2:l3:W:WH",
	where L1..L3 is integer value in deciAmpere, -2000...+2000,
	where W is integer value in Watt (power), use -1 when unknown,
	where WH is integer value in WhatHour (Energy), use -1 when unknown.]

*  <prefix> "/Set/HomeBatteryCurrent" = [Integer: Ampere]

*  <prefix> "/Set/RequiredEVCCID" = [String]
*  <prefix> "/Set/ColorOff" = [String: "R:G:B",
	where R,G,B is integer value 0. .255]
*  <prefix> "/Set/ColorNormal" = [String: "R:G:B",
	where R,G,B is integer value 0. .255]
*  <prefix> "/Set/ColorSmart" = [String: "R:G:B",
	where R,G,B is integer value 0. .255]
*  <prefix> "/Set/ColorSolar" = [String: "R:G:B",
	where R,G,B is integer value 0. .255]

# EVSE MQTT publish

* <prefix> "/connected" = "online"

* "/homeassistant/" <prefix> "/config" = JSON string announcement configuration

* <prefix> "/MainsCurrentL1" = [Integer: Ampere]

* <prefix> "/MainsCurrentL1" = [Integer: Ampere]

* <prefix> "/MainsCurrentL1" = [Integer: Ampere]

* <prefix> "/EVCurrentL1" = [Integer: Ampere]

* <prefix> "/EVCurrentL1" = [Integer: Ampere]

* <prefix> "/EVCurrentL1" = [Integer: Ampere]

* <prefix> "/ESPUptime" = [Integer: Seconds]

* <prefix> "/ESPTemp" = [Integer: DegreesC]

* <prefix> "/Mode" = [String: one of  "Off"|"Normal"|"Smart"|"Solar"|"N/A"]

* <prefix> "/MaxCurrent" = [Integer: deciAmpere]

* <prefix> "/ChargeCurrent" = [Integer: Ampere]

* <prefix> "/ChargeCurrentOverride" = [Integer: Ampere]

* <prefix> "/Access" = [String: one of "Deny"|"Allow"]

* <prefix> "/RFID" = [String: one of  "Not Installed"|"Ready to read card"|"Present"|"Card Stored"|"Card Deleted"|"Card already stored"|"Card not in storage"|"Card Storage full"|"Invalid"|"NOSTATUS"]

* <prefix> "/RFIDLastRead" = [String: "FFFFFFFFFF", 6-byte hex string]

* <prefix> "/State" = [String: one of "Ready to Charge"|"Connected to EV"|"Charging"|"D"|"Request State B"|"State B OK"|"Request State C"|"State C OK"|"Activate"|"Charging Stopped"|"Stop Charging"|"Modem Setup"|"Modem Request"|"Modem Done"|"Modem Denied"|"NOSTATE"]

* <prefix> "/Error" = [String: one of  "None"|"No Power Available"|"Communication Error"|"Temperature High"|"EV Meter Comm Error"|"RCM Tripped"|"Waiting for Solar"|"Test IO"|"Flash Error" ]

* <prefix> "/EVPlugState" =  [String:  "Connected"|"Disconnected"]

* <prefix> "/WiFiSSID"  =  [String: <WiFi.SSID>]

* <prefix> "/WiFiBSSID" = [String: <WiFi.BSSIDstr>]

* <prefix> "/WiFiRSSI" = [String: <WiFi.RSSI>]

* <prefix> "/EVChargePower" = [Integer: <PowerMeasured>]

* <prefix> "/EVEnergyCharged" = [Integer: <EnergyCharged>]

* <prefix> "/EVTotalEnergyCharged" = [Integer: <EnergyEV>]

* <prefix> "/HomeBatteryCurrent" = [Integer: <homeBatteryCurrent>]


# TODO

* Bring MQTT settable topics to same level as the REST API settings

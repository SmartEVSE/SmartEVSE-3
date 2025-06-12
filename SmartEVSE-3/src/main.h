/*
;    Project: Smart EVSE
;
;
; Permission is hereby granted, free of charge, to any person obtaining a copy
; of this software and associated documentation files (the "Software"), to deal
; in the Software without restriction, including without limitation the rights
; to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
; copies of the Software, and to permit persons to whom the Software is
; furnished to do so, subject to the following conditions:
;
; The above copyright notice and this permission notice shall be included in
; all copies or substantial portions of the Software.
;
; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
; IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
; FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
; AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
; LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
; OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
; THE SOFTWARE.
 */

#ifndef __EVSE_MAIN
#define __EVSE_MAIN


#ifndef DBG
//the wifi-debugger is available by telnetting to your SmartEVSE device
#define DBG 0  //comment or set to 0 for production release, 0 = no debug 1 = debug over telnet, 2 = debug over usb serial
#endif

#ifndef FAKE_RFID
//set FAKE_RFID to 1 to emulate an rfid reader with rfid of card = 123456
//showing the rfid card is simulated by executing http://smartevse-xxx.lan/debug?showrfid=1
//don't forget to first store the card before it can activate charging
#define FAKE_RFID 0
#endif

#ifndef AUTOMATED_TESTING
//set AUTOMATED_TESTING to 1 to make hardware-related paramaters like MaxCurrent and MaxCircuit updatable via REST API
//e.g. by executing curl -X POST http://smartevse-xxx.lan/automated_testing?maxcurrent=100
#define AUTOMATED_TESTING 0
#endif

#ifndef FAKE_SUNNY_DAY
//set this to 1 to emulate a sunny day where your solar charger is injecting current in the grid:
#define FAKE_SUNNY_DAY 0
//disclaimer: might not work for CT1 calibration/uncalibration stuff, since I can't test that
//the number of Amperes you want to have fake injected into Lx
#endif

#if FAKE_SUNNY_DAY
#define INJECT_CURRENT_L1 10
#define INJECT_CURRENT_L2 0
#define INJECT_CURRENT_L3 0
#endif

#ifndef ENABLE_OCPP
#define ENABLE_OCPP 0
#endif

#include "debug.h"
#include "stdint.h"
#include "main_c.h"

#if ENABLE_OCPP //TODO perhaps move to esp32.h
#include <MicroOcpp/Model/ConnectorBase/Notification.h>
#endif

#ifndef MODEM //TODO perhaps move to esp32.h
//the wifi-debugger is available by telnetting to your SmartEVSE device
#define MODEM 0  //0 = no modem 1 = modem
#endif

#define MAX_MAINS 25                                                            // max Current the Mains connection can supply
#define MAX_SUMMAINS 0                                                          // only used for capacity rate limiting, max current over the sum of all phases
#define MAX_SUMMAINSTIME 0
#define GRID_RELAY_MAX_SUMMAINS 18                                              // only used for rate limiting by grid switched relay,
                                                                                // max current over the sum of all phases
                                                                                // 6A * 3 phases * 230V = 4140W, law says 4.2kW ...
#define MAX_CURRENT 13                                                          // max charging Current for the EV
#ifndef MIN_CURRENT
#define MIN_CURRENT 6                                                           // minimum Current the EV will accept
#endif
#define MODE 0                                                                  // Normal EVSE mode
#define LOCK 0                                                                  // No Cable lock
#define MAX_CIRCUIT 16                                                          // Max current of the EVSE circuit breaker
#define CONFIG 0                                                                // Configuration: 0= TYPE 2 socket, 1= Fixed Cable
#define LOADBL 0                                                                // Load Balancing disabled
#define SWITCH 0                                                                // 0= Charge on plugin, 1= (Push)Button on IO2 is used to Start/Stop charging.
#define RC_MON 0                                                                // Residual Current Monitoring on IO3. Disabled=0, RCM14=1
#define CHARGEDELAY 60                                                          // Seconds to wait after overcurrent, before trying again
#define BACKLIGHT 120                                                           // Seconds delay for the LCD backlight to turn off.
#define RFIDLOCKTIME 60                                                         // Seconds delay for the EVSE to lock again (RFIDreader = EnableOne)
#define START_CURRENT 4                                                         // Start charging when surplus current on sum of all phases exceeds 4A (Solar)
#define STOP_TIME 10                                                            // Stop charging after 10 minutes at MIN charge current (Solar)
#define IMPORT_CURRENT 0                                                        // Allow the use of grid power when solar charging (Amps)
#define MAINS_METER 0                                                           // Mains Meter, 0=Disabled, 1= Sensorbox, 2=Phoenix, 3= Finder, 4= Eastron, 5=Custom
#define GRID 0                                                                  // Grid, 0= 4-Wire CW, 1= 4-Wire CCW, 2= 3-Wire CW, 3= 3-Wire CCW
#define MAINS_METER_ADDRESS 10
#define EV_METER 0
#define EV_METER_ADDRESS 12
#define MIN_METER_ADDRESS 10
#define MAX_METER_ADDRESS 247
#define EMCUSTOM_ENDIANESS 0
#define EMCUSTOM_DATATYPE 0
#define EMCUSTOM_FUNCTION 4
#define EMCUSTOM_UREGISTER 0
#define EMCUSTOM_UDIVISOR 8
#define EMCUSTOM_IREGISTER 0
#define EMCUSTOM_IDIVISOR 8
#define EMCUSTOM_PREGISTER 0
#define EMCUSTOM_PDIVISOR 8
#define EMCUSTOM_EREGISTER 0
#define EMCUSTOM_EDIVISOR 8
#define RFID_READER 0
#define WIFI_MODE 0
#define CARD_OFFSET 0
#define ENABLE_C2 ALWAYS_ON
#define MAX_TEMPERATURE 65
#define DELAYEDSTARTTIME 0                                                             // The default StartTime for delayed charged, 0 = not delaying
#define DELAYEDSTOPTIME 0                                                       // The default StopTime for delayed charged, 0 = not stopping
#define SOLARSTARTTIME 40                                                       // Seconds to keep chargecurrent at 6A
#define OCPP_MODE 0
#define AUTOUPDATE 0                                                            // default for Automatic Firmware Update: 0 = disabled, 1 = enabled
#define SB2_WIFI_MODE 0
#define LCD_LOCK 0                                                              // 0 = LCD buttons operational, 1 = LCD buttons disabled
#define CABLE_LOCK 0                                                            // 0 = Cable Lock disabled, 1 = Cable Lock enabled
#define INITIALIZED 0

// Mode settings
#define MODE_NORMAL 0
#define MODE_SMART 1
#define MODE_SOLAR 2

#define MODBUS_BAUDRATE 9600
#define MODBUS_TIMEOUT 4
#define ACK_TIMEOUT 1000                                                        // 1000ms timeout
#define NR_EVSES 8
#define BROADCAST_ADR 0x09
#define COMM_TIMEOUT 11                                                         // Timeout for MainsMeter
#define COMM_EVTIMEOUT 8*NR_EVSES                                               // Timeout for EV Energy Meters

#define PILOT_12V   12                                                          // State A - vehicle disconnected
#define PILOT_9V    9                                                           // State B - vehicle connected
#define PILOT_6V    6                                                           // State C - EV charge
#define PILOT_3V    3
#define PILOT_DIODE 1
#define PILOT_NOK   0

#define _RSTB_0 digitalWrite(PIN_LCD_RST, LOW);
#define _RSTB_1 digitalWrite(PIN_LCD_RST, HIGH);
#define _A0_0 digitalWrite(PIN_LCD_A0_B2, LOW);
#define _A0_1 digitalWrite(PIN_LCD_A0_B2, HIGH);

#define STATE_A_LED_BRIGHTNESS 40
#define STATE_B_LED_BRIGHTNESS 255
#define ERROR_LED_BRIGHTNESS 255
#define WAITING_LED_BRIGHTNESS 255
#define LCD_BRIGHTNESS 255


//TODO replace the macros by function calls
void setPilot(bool On);
#define PILOT_CONNECTED setPilot(true);
#define PILOT_DISCONNECTED setPilot(false);

//TODO this can be integrated by choosing same definitions
#ifdef SMARTEVSE_VERSION //ESP32

#define BACKLIGHT_ON digitalWrite(PIN_LCD_LED, HIGH);
#define BACKLIGHT_OFF digitalWrite(PIN_LCD_LED, LOW);

#define ACTUATOR_LOCK { _LOG_A("Locking Actuator.\n"); digitalWrite(PIN_ACTB, HIGH); digitalWrite(PIN_ACTA, LOW); }
#define ACTUATOR_UNLOCK { _LOG_A("Unlocking Actuator.\n"); digitalWrite(PIN_ACTB, LOW); digitalWrite(PIN_ACTA, HIGH); }
#define ACTUATOR_OFF { digitalWrite(PIN_ACTB, HIGH); digitalWrite(PIN_ACTA, HIGH); }

#define RCMFAULT digitalRead(PIN_RCM_FAULT) //TODO ok for v4?
#if SMARTEVSE_VERSION >=40
#define SEND_TO_CH32(X) Serial1.printf("@%s:%u\n", #X, X); _LOG_V("[->] %s:%u\n", #X, X);
#define SEND_TO_ESP32(X) //dummy
#else //v3
#define SEND_TO_CH32(X) //dummy
#define SEND_TO_ESP32(X) //dummy
#define CONTACTOR1_ON _LOG_A("@MSG: Switching Contactor1 ON.\n"); digitalWrite(PIN_SSR, HIGH);
#define CONTACTOR1_OFF _LOG_A("@MSG: Switching Contactor1 OFF.\n"); digitalWrite(PIN_SSR, LOW);

#define CONTACTOR2_ON _LOG_A("@MSG: Switching Contactor2 ON.\n"); digitalWrite(PIN_SSR2, HIGH);
#define CONTACTOR2_OFF _LOG_A("@MSG: Switching Contactor2 OFF.\n"); digitalWrite(PIN_SSR2, LOW);
#endif
#else //CH32
#define SEND_TO_CH32(X) //dummy
#define SEND_TO_ESP32(X) printf("@%s:%u\n", #X, X);

#define CONTACTOR1_ON printf("@MSG: Switching Contactor1 ON.\n"); funDigitalWrite(SSR1, FUN_HIGH);
#define CONTACTOR1_OFF printf("@MSG: Switching Contactor1 OFF.\n"); funDigitalWrite(SSR1, FUN_LOW);

#define CONTACTOR2_ON printf("@MSG: Switching Contactor2 ON.\n"); funDigitalWrite(SSR2, FUN_HIGH);
#define CONTACTOR2_OFF printf("@MSG: Switching Contactor2 OFF.\n"); funDigitalWrite(SSR2, FUN_LOW);

#define ACTUATOR_LOCK { funDigitalWrite(ACTB, FUN_HIGH); funDigitalWrite(ACTA, FUN_LOW); }
#define ACTUATOR_UNLOCK { funDigitalWrite(ACTB, FUN_LOW); funDigitalWrite(ACTA, FUN_HIGH); }
#define ACTUATOR_OFF { funDigitalWrite(ACTB, FUN_HIGH); funDigitalWrite(ACTA, FUN_HIGH); }
#endif

#define MODBUS_INVALID 0
#define MODBUS_OK 1
#define MODBUS_REQUEST 2
#define MODBUS_RESPONSE 3
#define MODBUS_EXCEPTION 4

#define MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS 0x02
#define MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE 0x03

#define MODBUS_EVSE_STATUS_START 0x0000
#define MODBUS_EVSE_STATUS_COUNT 12
#define MODBUS_EVSE_CONFIG_START 0x0100
#define MODBUS_EVSE_CONFIG_COUNT 10
#define MODBUS_SYS_CONFIG_START  0x0200
#define MODBUS_SYS_CONFIG_COUNT  26

#define MODBUS_MAX_REGISTER_READ MODBUS_SYS_CONFIG_COUNT
#define MODBUS_BUFFER_SIZE MODBUS_MAX_REGISTER_READ * 2 + 10

// EVSE status
#define STATUS_STATE 64                                                         // 0x0000: State
#define STATUS_ERROR 65                                                         // 0x0001: Error
#define STATUS_CURRENT 66                                                       // 0x0002: Charging current (A * 10)
#define STATUS_MODE 67                                                          // 0x0003: EVSE Mode
#define STATUS_SOLAR_TIMER 68                                                   // 0x0004: Solar Timer
#define STATUS_ACCESS 69                                                        // 0x0005: Access bit
#define STATUS_CONFIG_CHANGED 70                                                // 0x0006: Configuration changed
#define STATUS_MAX 71                                                           // 0x0007: Maximum charging current (RO)
#define STATUS_PHASE_COUNT 72                                                   // 0x0008: Number of used phases (RO) (ToDo)
#define STATUS_REAL_CURRENT 73                                                  // 0x0009: Real charging current (RO) (ToDo)
#define STATUS_TEMP 74                                                          // 0x000A: Temperature (RO)
#define STATUS_SERIAL 75                                                        // 0x000B: Serial number (RO)

// Node specific configuration
#define MENU_ENTER 1
#define MENU_CONFIG 2                                                           // 0x0100: Configuration
#define MENU_LOCK 3                                                             // 0x0101: Cable lock
#define MENU_MIN 4                                                              // 0x0102: MIN Charge Current the EV will accept
#define MENU_MAX 5                                                              // 0x0103: MAX Charge Current for this EVSE
#define MENU_LOADBL 6                                                           // 0x0104: Load Balance
#define MENU_SWITCH 7                                                           // 0x0105: External Start/Stop button
#define MENU_RCMON 8                                                            // 0x0106: Residual Current Monitor
#define MENU_RFIDREADER 9                                                       // 0x0107: Use RFID reader
#define MENU_EVMETER 10                                                         // 0x0108: Type of EV electric meter
#define MENU_EVMETERADDRESS 11                                                  // 0x0109: Address of EV electric meter

// System configuration (same on all SmartEVSE in a LoadBalancing setup)
#define MENU_MODE 12                                                            // 0x0200: EVSE mode
#define MENU_CIRCUIT 13                                                         // 0x0201: EVSE Circuit max Current
#define MENU_GRID 14                                                            // 0x0202: Grid type to which the Sensorbox is connected
#define MENU_SB2_WIFI 15                                                        // 0x0203: WiFi mode of the Sensorbox 2
#define MENU_MAINS 16                                                           // 0x0204: Max Mains Current
#define MENU_START 17                                                           // 0x0205: Surplus energy start Current
#define MENU_STOP 18                                                            // 0x0206: Stop solar charging at 6A after this time
#define MENU_IMPORT 19                                                          // 0x0207: Allow grid power when solar charging
#define MENU_MAINSMETER 20                                                      // 0x0208: Type of Mains electric meter
#define MENU_MAINSMETERADDRESS 21                                               // 0x0209: Address of Mains electric meter
#define MENU_EMCUSTOM_ENDIANESS 22                                              // 0x020D: Byte order of custom electric meter
#define MENU_EMCUSTOM_DATATYPE 23                                               // 0x020E: Data type of custom electric meter
#define MENU_EMCUSTOM_FUNCTION 24                                               // 0x020F: Modbus Function (3/4) of custom electric meter
#define MENU_EMCUSTOM_UREGISTER 25                                              // 0x0210: Register for Voltage (V) of custom electric meter
#define MENU_EMCUSTOM_UDIVISOR 26                                               // 0x0211: Divisor for Voltage (V) of custom electric meter (10^x)
#define MENU_EMCUSTOM_IREGISTER 27                                              // 0x0212: Register for Current (A) of custom electric meter
#define MENU_EMCUSTOM_IDIVISOR 28                                               // 0x0213: Divisor for Current (A) of custom electric meter (10^x)
#define MENU_EMCUSTOM_PREGISTER 29                                              // 0x0214: Register for Power (W) of custom electric meter
#define MENU_EMCUSTOM_PDIVISOR 30                                               // 0x0215: Divisor for Power (W) of custom electric meter (10^x)
#define MENU_EMCUSTOM_EREGISTER 31                                              // 0x0216: Register for Energy (kWh) of custom electric meter
#define MENU_EMCUSTOM_EDIVISOR 32                                               // 0x0217: Divisor for Energy (kWh) of custom electric meter (10^x)
#define MENU_EMCUSTOM_READMAX 33                                                // 0x0218: Maximum register read (ToDo)
#define MENU_WIFI 34                                                            // 0x0219: WiFi mode
#define MENU_AUTOUPDATE 35
#define MENU_C2 36
#define MENU_MAX_TEMP 37
#define MENU_SUMMAINS 38
#define MENU_SUMMAINSTIME 39
#define MENU_LCDPIN 40
#define MENU_OFF 41                                                             // so access bit is reset and charging stops when pressing < button 2 seconds
#define MENU_ON 42                                                              // so access bit is set and charging starts when pressing > button 2 seconds
#define MENU_EXIT 43

#define MENU_STATE 50

class Button {
  public:
    void CheckSwitch(bool force = false);
    bool Pressed;                                                               // when io = low key is pressed
    uint32_t TimeOfPress;                                                       // we need the time when the button or switch was pressed to detect longpress
    void HandleSwitch(void);

    // constructor
    Button(void);
};

extern Button ExtSwitch;
extern uint8_t SB2_WIFImode;
extern uint8_t LCDNav;
extern uint8_t SubMenu;
extern uint8_t GridActive;                                                      // When the CT's are used on Sensorbox2, it enables the GRID menu option.
extern uint8_t Grid;
extern void Timer10ms(void * parameter);
extern void Timer100ms(void * parameter);
extern void Timer1S(void * parameter);
extern void BlinkLed(void * parameter);
extern void getButtonState();
extern void PowerPanicESP();

extern uint8_t LCDlock;
enum Switch_Phase_t { NO_SWITCH, GOING_TO_SWITCH_1P, GOING_TO_SWITCH_3P, AFTER_SWITCH };
enum AccessStatus_t { OFF, ON, PAUSE };
extern void CalcBalancedCurrent(char mod);
extern void write_settings(void);
extern void CalcIsum(void);
extern void setChargeDelay(uint8_t delay);

struct Sensorbox {
    uint8_t SoftwareVer;        // Sensorbox 2 software version
    uint8_t WiFiConnected;      // 0:not connected / 1:connected to WiFi
    uint8_t WiFiAPSTA;          // 0:no portal /  1: portal active
    uint8_t WIFImode;           // 0:Wifi Off / 1:WiFi On / 2: Portal Start
    uint8_t IP[4];
    uint8_t APpassword[9];      // 8 characters + null termination
};

uint8_t setItemValue(uint8_t nav, uint16_t val);
uint16_t getItemValue(uint8_t nav);

enum EnableC2_t { NOT_PRESENT, ALWAYS_OFF, SOLAR_OFF, ALWAYS_ON, AUTO };

struct Node_t {
    uint8_t Online;
    uint8_t ConfigChanged;
    uint8_t EVMeter;
    uint8_t EVAddress;
    uint8_t MinCurrent;     // 0.1A
    uint8_t Phases;
    uint32_t Timer;         // 1s
    uint32_t IntTimer;      // 1s
    uint16_t SolarTimer;    // 1s
    uint8_t Mode;
};

extern bool BuzzerPresent;
#endif

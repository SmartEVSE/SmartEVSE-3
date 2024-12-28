/*
;    Project: Smart EVSE v3
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

#ifndef SMARTEVSE_VERSION
#define SMARTEVSE_VERSION 3
#endif

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

#include <Arduino.h>
#include "debug.h"
#include "stdint.h"
#include "common.h"

#if ENABLE_OCPP
#include <MicroOcpp/Model/ConnectorBase/Notification.h>
#endif

#ifndef MODEM
//the wifi-debugger is available by telnetting to your SmartEVSE device
#define MODEM 0  //0 = no modem 1 = modem
#endif

// Pin definitions left side ESP32
#define PIN_TEMP 36
#define PIN_CP_IN 39
#define PIN_PP_IN 34
#define PIN_LOCK_IN 35
#define PIN_SSR 32
#define PIN_LCD_SDO_B3 33                                                       // = SPI_MOSI
#define PIN_LCD_A0_B2 25
#define PIN_LCD_CLK 26                                                          // = SPI_SCK
#define PIN_SSR2 27
#define PIN_LCD_LED 14
#define PIN_LEDB 12
#define PIN_RCM_FAULT 13 //TODO ok for v4?

// Pin definitions right side ESP32
#define PIN_RS485_RX 23
#define PIN_RS485_DIR 22
//#define PIN_RXD 
//#define PIN_TXD
#define PIN_RS485_TX 21
#define PIN_CP_OUT 19
#define PIN_ACTB 18
#define PIN_LCD_RST 5
#define PIN_ACTA 17
#define PIN_SW_IN 16
#define PIN_LEDG 4
#define PIN_IO0_B1 0
#define PIN_LEDR 2
#define PIN_CPOFF 15

#if SMARTEVSE_VERSION == 3
#define SPI_MOSI 33                                                             // SPI connections to LCD
#define SPI_MISO -1
#define SPI_SCK 26
#define SPI_SS -1
#endif //SMARTEVSE_VERSION

#define CP_CHANNEL 0
#define RED_CHANNEL 2                                                           // PWM channel 2 (0 and 1 are used by CP signal)
#define GREEN_CHANNEL 3
#define BLUE_CHANNEL 4
#define LCD_CHANNEL 5                                                           // LED Backlight LCD

#define PWM_5 50                                                                // 5% of PWM
#define PWM_95 950                                                              // 95% of PWM
#define PWM_100 1000                                                            // 100% of PWM

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
#define PV_METER 0
#define PV_METER_ADDRESS 11
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
#define ACCESS_BIT 1
#define WIFI_MODE 0
#define CARD_OFFSET 0
#define ENABLE_C2 ALWAYS_ON
#define MAX_TEMPERATURE 65
#define DELAYEDSTARTTIME 0                                                             // The default StartTime for delayed charged, 0 = not delaying
#define DELAYEDSTOPTIME 0                                                       // The default StopTime for delayed charged, 0 = not stopping
#define OCPP_MODE 0
#define AUTOUPDATE 0                                                            // default for Automatic Firmware Update: 0 = disabled, 1 = enabled
#define SB2_WIFI_MODE 0
#define LCD_LOCK 0                                                              // 0 = LCD buttons operational, 1 = LCD buttons disabled

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

#define STATE_A 0                                                               // A Vehicle not connected
#define STATE_B 1                                                               // B Vehicle connected / not ready to accept energy
#define STATE_C 2                                                               // C Vehicle connected / ready to accept energy / ventilation not required
#define STATE_D 3                                                               // D Vehicle connected / ready to accept energy / ventilation required (not implemented)
#define STATE_COMM_B 4                                                          // E State change request A->B (set by node)
#define STATE_COMM_B_OK 5                                                       // F State change A->B OK (set by master)
#define STATE_COMM_C 6                                                          // G State change request B->C (set by node)
#define STATE_COMM_C_OK 7                                                       // H State change B->C OK (set by master)
#define STATE_ACTSTART 8                                                        // I Activation mode in progress
#define STATE_B1 9                                                              // J Vehicle connected / EVSE not ready to deliver energy: no PWM signal
#define STATE_C1 10                                                             // K Vehicle charging / EVSE not ready to deliver energy: no PWM signal (temp state when stopping charge from EVSE)
//#if SMARTEVSE_VERSION == 3 TODO
#define STATE_MODEM_REQUEST 11                                                          // L Vehicle connected / requesting ISO15118 communication, 0% duty
#define STATE_MODEM_WAIT 12                                                          // M Vehicle connected / requesting ISO15118 communication, 5% duty
#define STATE_MODEM_DONE 13                                                // Modem communication succesful, SoCs extracted. Here, re-plug vehicle
#define STATE_MODEM_DENIED 14                                                // Modem access denied based on EVCCID, re-plug vehicle and try again
//#else
//#define STATE_E 11                  // disconnected pilot / powered down
//#define STATE_F 12                  // -12V Fault condition
//#endif

#define NOSTATE 255

#define PILOT_12V 1                                                             // State A - vehicle disconnected
#define PILOT_9V 2                                                              // State B - vehicle connected
#define PILOT_6V 3                                                              // State C - EV charge
#define PILOT_3V 4
#define PILOT_DIODE 5
#define PILOT_NOK 0


#define NO_ERROR 0
#define LESS_6A 1
#define CT_NOCOMM 2
#define TEMP_HIGH 4
#define EV_NOCOMM 8
#define RCM_TRIPPED 16                                                          // RCM tripped. >6mA DC residual current detected.
#define NO_SUN 32
#define Test_IO 64
#define BL_FLASH 128

#define STATE_A_LED_BRIGHTNESS 40
#define STATE_B_LED_BRIGHTNESS 255
#define ERROR_LED_BRIGHTNESS 255
#define WAITING_LED_BRIGHTNESS 255
#define LCD_BRIGHTNESS 255


#define CP_ON digitalWrite(PIN_CPOFF, LOW);
#define CP_OFF digitalWrite(PIN_CPOFF, HIGH);

#define PILOT_CONNECTED digitalWrite(PIN_CPOFF, LOW);
#define PILOT_DISCONNECTED digitalWrite(PIN_CPOFF, HIGH);

#define CONTACTOR1_ON _LOG_A("Switching Contactor1 ON.\n"); digitalWrite(PIN_SSR, HIGH);
#define CONTACTOR1_OFF _LOG_A("Switching Contactor1 OFF.\n"); digitalWrite(PIN_SSR, LOW);

#define CONTACTOR2_ON _LOG_A("Switching Contactor2 ON.\n"); digitalWrite(PIN_SSR2, HIGH);
#define CONTACTOR2_OFF _LOG_A("Switching Contactor2 OFF.\n"); digitalWrite(PIN_SSR2, LOW);

#define BACKLIGHT_ON digitalWrite(PIN_LCD_LED, HIGH);
#define BACKLIGHT_OFF digitalWrite(PIN_LCD_LED, LOW);

#define ACTUATOR_LOCK { _LOG_A("Locking Actuator.\n"); digitalWrite(PIN_ACTB, HIGH); digitalWrite(PIN_ACTA, LOW); }
#define ACTUATOR_UNLOCK { _LOG_A("Unlocking Actuator.\n"); digitalWrite(PIN_ACTB, LOW); digitalWrite(PIN_ACTA, HIGH); }
#define ACTUATOR_OFF { digitalWrite(PIN_ACTB, HIGH); digitalWrite(PIN_ACTA, HIGH); }

#define RCMFAULT digitalRead(PIN_RCM_FAULT) //TODO ok for v4?

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
#define MENU_OFF 40                                                             // so access bit is reset and charging stops when pressing < button 2 seconds
#define MENU_ON 41                                                              // so access bit is set and charging starts when pressing > button 2 seconds
#define MENU_EXIT 42

#define MENU_STATE 50

#define _RSTB_0 digitalWrite(PIN_LCD_RST, LOW);
#define _RSTB_1 digitalWrite(PIN_LCD_RST, HIGH);
#define _A0_0 digitalWrite(PIN_LCD_A0_B2, LOW);
#define _A0_1 digitalWrite(PIN_LCD_A0_B2, HIGH);

extern portMUX_TYPE rtc_spinlock;   //TODO: Will be placed in the appropriate position after the rtc module is finished.

#define RTC_ENTER_CRITICAL()    portENTER_CRITICAL(&rtc_spinlock)
#define RTC_EXIT_CRITICAL()     portEXIT_CRITICAL(&rtc_spinlock)


extern char SmartConfigKey[];
extern struct tm timeinfo;


extern uint8_t Mode;                                                            // EVSE mode
extern uint8_t LoadBl;                                                          // Load Balance Setting (Disable, Master or Node)
extern uint8_t Grid;
extern uint8_t SB2_WIFImode;
#if FAKE_RFID
extern uint8_t Show_RFID;
#endif

extern uint8_t State;
extern uint8_t ErrorFlags;
extern uint8_t NextState;

extern int16_t Isum;
extern uint16_t Balanced[NR_EVSES];                                             // Amps value per EVSE

extern uint8_t LCDTimer;
extern uint16_t BacklightTimer;                                                 // remaining seconds the LCD backlight is active
extern uint8_t ButtonState;                                                     // Holds latest push Buttons state (LSB 2:0)
extern uint8_t OldButtonState;                                                  // Holds previous push Buttons state (LSB 2:0)
extern uint8_t LCDNav;
extern uint8_t SubMenu;
extern uint32_t ScrollTimer;
extern uint8_t ChargeDelay;                                                     // Delays charging in seconds.
extern uint8_t TestState;
extern uint8_t Access_bit;
extern uint16_t CardOffset;

extern uint8_t GridActive;                                                      // When the CT's are used on Sensorbox2, it enables the GRID menu option.
extern uint16_t SolarStopTimer;
extern int32_t EnergyCapacity;
extern uint8_t RFIDstatus;
extern uint8_t OcppMode;
extern bool LocalTimeSet;
extern uint32_t serialnr;

const static char StrEnableC2[][12] = { "Not present", "Always Off", "Solar Off", "Always On", "Auto" };
extern Single_Phase_t Switching_To_Single_Phase;
extern uint8_t Nr_Of_Phases_Charging;

const struct {
    char LCD[10];
    char Desc[52];
    uint16_t Min;
    uint16_t Max;
    uint16_t Default;
} MenuStr[MENU_EXIT + 1] = {
    {"", "Not in menu", 0, 0, 0},
    {"", "Hold 2 sec", 0, 0, 0},

    // Node specific configuration
    /* LCD,       Desc,                                                 Min, Max, Default */
    {"CONFIG",  "Fixed Cable or Type 2 Socket",                       0, 1, CONFIG},
    {"LOCK",    "Cable locking actuator type",                        0, 2, LOCK},
    {"MIN",     "MIN Charge Current the EV will accept (per phase)",  MIN_CURRENT, 16, MIN_CURRENT},
    {"MAX",     "MAX Charge Current for this EVSE (per phase)",       6, 80, MAX_CURRENT},
    {"PWR SHARE", "Share Power between multiple SmartEVSEs (2-8)",    0, NR_EVSES, LOADBL},
    {"SWITCH",  "Switch function control on pin SW",                  0, 7, SWITCH},
    {"RCMON",   "Residual Current Monitor on pin RCM",                0, 1, RC_MON},
    {"RFID",    "RFID reader, learn/remove cards",                    0, 5 + (ENABLE_OCPP ? 1 : 0), RFID_READER},
    {"EV METER","Type of EV electric meter",                          0, EM_CUSTOM, EV_METER},
    {"EV ADDR", "Address of EV electric meter",                       MIN_METER_ADDRESS, MAX_METER_ADDRESS, EV_METER_ADDRESS},

    // System configuration
    /* LCD,       Desc,                                                 Min, Max, Default */
    {"MODE",    "Normal, Smart or Solar EVSE mode",                   0, 2, MODE},
    {"CIRCUIT", "EVSE Circuit max Current",                           10, 160, MAX_CIRCUIT},
    {"GRID",    "Grid type to which the Sensorbox is connected",      0, 1, GRID},
    {"SB2 WIFI","Connect Sensorbox-2 to WiFi",                        0, 2, SB2_WIFI_MODE},
    {"MAINS",   "Max MAINS Current (per phase)",                      10, 200, MAX_MAINS},
    {"START",   "Surplus energy start Current (sum of phases)",       0, 48, START_CURRENT},
    {"STOP",    "Stop solar charging at 6A after this time",          0, 60, STOP_TIME},
    {"IMPORT",  "Allow grid power when solar charging (sum of phase)",0, 48, IMPORT_CURRENT},
    {"MAINS MET","Type of mains electric meter",                       0, EM_CUSTOM, MAINS_METER},
    {"MAINS ADR","Address of mains electric meter",                    MIN_METER_ADDRESS, MAX_METER_ADDRESS, MAINS_METER_ADDRESS},
    {"BYTE ORD","Byte order of custom electric meter",                0, 3, EMCUSTOM_ENDIANESS},
    {"DATA TYPE","Data type of custom electric meter",                 0, MB_DATATYPE_MAX - 1, EMCUSTOM_DATATYPE},
    {"FUNCTION","Modbus Function of custom electric meter",           3, 4, EMCUSTOM_FUNCTION},
    {"VOL REGI","Register for Voltage (V) of custom electric meter",  0, 65530, EMCUSTOM_UREGISTER},
    {"VOL DIVI","Divisor for Voltage (V) of custom electric meter",   0, 7, EMCUSTOM_UDIVISOR},
    {"CUR REGI","Register for Current (A) of custom electric meter",  0, 65530, EMCUSTOM_IREGISTER},
    {"CUR DIVI","Divisor for Current (A) of custom electric meter",   0, 7, EMCUSTOM_IDIVISOR},
    {"POW REGI","Register for Power (W) of custom electric meter",    0, 65534, EMCUSTOM_PREGISTER},
    {"POW DIVI","Divisor for Power (W) of custom electric meter",     0, 7, EMCUSTOM_PDIVISOR},
    {"ENE REGI","Register for Energy (kWh) of custom electric meter", 0, 65534, EMCUSTOM_EREGISTER},
    {"ENE DIVI","Divisor for Energy (kWh) of custom electric meter",  0, 7, EMCUSTOM_EDIVISOR},
    {"READ MAX","Max register read at once of custom electric meter", 3, 255, 3},
    {"WIFI",    "Connect SmartEVSE to WiFi",                          0, 2, WIFI_MODE},
    {"AUTOUPDAT","Automatic Firmware Update",                         0, 1, AUTOUPDATE},
    {"CONTACT 2","Contactor2 (C2) behaviour",                          0, sizeof(StrEnableC2) / sizeof(StrEnableC2[0])-1, ENABLE_C2},
    {"MAX TEMP","Maximum temperature for the EVSE module",            40, 75, MAX_TEMPERATURE},
    {"CAPACITY","Capacity Rate limit on sum of MAINS Current (A)",    0, 600, MAX_SUMMAINS},
    {"CAP STOP","Stop Capacity Rate limit charging after X minutes",    0, 60, MAX_SUMMAINSTIME},
    {"", "Hold 2 sec to stop charging", 0, 0, 0},
    {"", "Hold 2 sec to start charging", 0, 0, 0},

    {"EXIT", "EXIT", 0, 0, 0}
};


struct DelayedTimeStruct {
    uint32_t epoch2;        // in case of Delayed Charging the StartTime in epoch2; if zero we are NOT Delayed Charging
                            // epoch2 is the number of seconds since 1/1/2023 00:00 UTC, which equals epoch 1672531200
                            // we avoid using epoch so we don't need expensive 64bits arithmetics with difftime
                            // and we can store dates until 7/2/2159
    int32_t diff;           // StartTime minus current time in seconds
};

#define EPOCH2_OFFSET 1672531200

extern struct DelayedTimeStruct DelayedStartTime;

void read_settings();
void write_settings(void);
void setSolarStopTimer(uint16_t Timer);
void setState(uint8_t NewState);
void setAccess(bool Access);
void SetCPDuty(uint32_t DutyCycle);
uint8_t setItemValue(uint8_t nav, uint16_t val);
uint16_t getItemValue(uint8_t nav);
void ConfigureModbusMode(uint8_t newmode);

void setMode(uint8_t NewMode) ;

#if ENABLE_OCPP
void ocppUpdateRfidReading(const unsigned char *uuid, size_t uuidLen);
bool ocppIsConnectorPlugged();

bool ocppHasTxNotification();
MicroOcpp::TxNotification ocppGetTxNotification();

bool ocppLockingTxDefined();
#endif //ENABLE_OCPP

#if SMARTEVSE_VERSION == 4
// Pin definitions
#define PIN_QCA700X_INT 9           // SPI connections to QCA7000X
#define PIN_QCA700X_CS 11           // on ESP-S3 with OCTAL flash/PSRAM, GPIO pins 33-37 can not be used!
#define SPI_MOSI 13
#define SPI_MISO 12
#define SPI_SCK 10
#define PIN_QCA700X_RESETN 45

#define USART_TX 43                 // comm bus to mainboard
#define USART_RX 44

#define BUTTON1 0                   // Navigation buttons
//#define BUTTON2 1                   // renamed from prototype!
#define BUTTON3 2

#define RTC_SDA 6                   // RTC interface
#define RTC_SCL 7
#define RTC_INT 16

// New top board
#define WCH_NRST 8                  // microcontroller program interface
#define WCH_SWDIO 17                // unconnected!!! pin on 16pin connector is used for LCD power
#define WCH_SWCLK 18

// Old prototype top board
//#define WCH_NRST 18                  // microcontroller program interface
//#define WCH_SWDIO 8
//#define WCH_SWCLK 17

#define LCD_SDA 38                  // LCD interface
#define LCD_SCK 39
#define LCD_A0_B2 40
#define LCD_LED 41
#define LCD_RST 42
#define LCD_CS 1

#define LCD_CHANNEL 5               // PWM channel

#define LCD_RST_0 digitalWrite(LCD_RST, LOW);
#define LCD_RST_1 digitalWrite(LCD_RST, HIGH);
#define LCD_A0_0 digitalWrite(LCD_A0_B2, LOW);
#define LCD_A0_1 digitalWrite(LCD_A0_B2, HIGH);


// RTC power sources
#define BATTERY 0x0C                // Trickle charger (TCE) disabled, Level Switching Mode (LSM) enabled.
#define SUPERCAP 0x24               // Trickle charger (TCE) enabled, Direct Switching Mode (DSM) enabled.




// ESP-WCH Communication States
#define COMM_OFF 0
#define COMM_VER_REQ 1              // Version Reqest           ESP -> WCH
#define COMM_VER_RSP 2              // Version Response         ESP <- WCH
#define COMM_CONFIG_SET 3           // Configuration Set        ESP -> WCH
#define COMM_CONFIG_CNF 4           // Configuration confirm.   ESP <- WCH
#define COMM_STATUS_REQ 5           // Status Request
#define COMM_STATUS_RSP 6           // Status Response

/*====================================================================*
 *   SPI registers QCA700X
 *--------------------------------------------------------------------*/

#define QCA7K_SPI_READ (1 << 15)                // MSB(15) of each command (16 bits) is the read(1) or write(0) bit.
#define QCA7K_SPI_WRITE (0 << 15)
#define QCA7K_SPI_INTERNAL (1 << 14)            // MSB(14) sets the Internal Registers(1) or Data Buffer(0)
#define QCA7K_SPI_EXTERNAL (0 << 14)

#define	SPI_REG_BFR_SIZE        0x0100
#define SPI_REG_WRBUF_SPC_AVA   0x0200
#define SPI_REG_RDBUF_BYTE_AVA  0x0300
#define SPI_REG_SPI_CONFIG      0x0400
#define SPI_REG_INTR_CAUSE      0x0C00
#define SPI_REG_INTR_ENABLE     0x0D00
#define SPI_REG_RDBUF_WATERMARK 0x1200
#define SPI_REG_WRBUF_WATERMARK 0x1300
#define SPI_REG_SIGNATURE       0x1A00
#define SPI_REG_ACTION_CTRL     0x1B00

#define QCASPI_GOOD_SIGNATURE   0xAA55
#define QCA7K_BUFFER_SIZE       3163

#define SPI_INT_WRBUF_BELOW_WM (1 << 10)
#define SPI_INT_CPU_ON         (1 << 6)
#define SPI_INT_ADDR_ERR       (1 << 3)
#define SPI_INT_WRBUF_ERR      (1 << 2)
#define SPI_INT_RDBUF_ERR      (1 << 1)
#define SPI_INT_PKT_AVLBL      (1 << 0)

/*====================================================================*
 *   Modem States
 *--------------------------------------------------------------------*/

#define MODEM_POWERUP 0
#define MODEM_WRITESPACE 1
#define MODEM_CM_SET_KEY_REQ 2
#define MODEM_CM_SET_KEY_CNF 3
#define MODEM_CONFIGURED 10
#define SLAC_PARAM_REQ 20
#define SLAC_PARAM_CNF 30
#define MNBC_SOUND 40
#define ATTEN_CHAR_IND 50
#define ATTEN_CHAR_RSP 60
#define SLAC_MATCH_REQ 70

#define MODEM_LINK_STATUS 80
#define MODEM_WAIT_LINK 90
#define MODEM_GET_SW_REQ 100
#define MODEM_WAIT_SW 110
#define MODEM_LINK_READY 120


/*====================================================================*
 *   SLAC commands
 *--------------------------------------------------------------------*/

#define CM_SET_KEY 0x6008
#define CM_GET_KEY 0x600C
#define CM_SC_JOIN 0x6010
#define CM_CHAN_EST 0x6014
#define CM_TM_UPDATE 0x6018
#define CM_AMP_MAP 0x601C
#define CM_BRG_INFO 0x6020
#define CM_CONN_NEW 0x6024
#define CM_CONN_REL 0x6028
#define CM_CONN_MOD 0x602C
#define CM_CONN_INFO 0x6030
#define CM_STA_CAP 0x6034
#define CM_NW_INFO 0x6038
#define CM_GET_BEACON 0x603C
#define CM_HFID 0x6040
#define CM_MME_ERROR 0x6044
#define CM_NW_STATS 0x6048
#define CM_SLAC_PARAM 0x6064
#define CM_START_ATTEN_CHAR 0x6068
#define CM_ATTEN_CHAR 0x606C
#define CM_PKCS_CERT 0x6070
#define CM_MNBC_SOUND 0x6074
#define CM_VALIDATE 0x6078
#define CM_SLAC_MATCH 0x607C
#define CM_SLAC_USER_DATA 0x6080
#define CM_ATTEN_PROFILE 0x6084
#define CM_GET_SW 0xA000
#define CM_LINK_STATUS 0xA0B8

#define MMTYPE_REQ 0x0000   // request
#define MMTYPE_CNF 0x0001   // confirmation = +1
#define MMTYPE_IND 0x0002
#define MMTYPE_RSP 0x0003

// Frametypes

#define FRAME_IPV6 0x86DD
#define FRAME_HOMEPLUG 0x88E1

/* V2GTP */
#define V2GTP_HEADER_SIZE 8 /* header has 8 bytes */

extern struct tm timeinfo;


struct rtcTime {
    uint8_t Status;
    uint8_t Hour;
    uint8_t Minute;
    uint8_t Second;
    uint8_t Date;
    uint8_t Month;
    uint16_t Year;
} ;

#endif //SMARTEVSE_VERSION

#endif

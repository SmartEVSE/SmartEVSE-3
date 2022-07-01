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


//for wifi-debugging, don't forget to set the debug levels LOG_EVSE_LOG and LOG_MODBUS_LOG before compiling
//the wifi-debugger is available by telnetting to your SmartEVSE device
//the on-screen instructions for verbose/warning/info/... do not apply, 
//the debug messages that are compiled in are always shown for backwards compatibility reasons
//uncomment for production release, comment this to debug via wifi:
#define DEBUG_DISABLED 1

#ifndef VERSION
#ifdef DEBUG_DISABLED
#define VERSION "v3serkri-0.00"
#else
#define VERSION "v3serkri-0.00-debug"
#endif
#endif


#define LOG_DEBUG 3                                                             // Debug messages including measurement data
#define LOG_INFO 2                                                              // Information messages without measurement data
#define LOG_WARN 1                                                              // Warning or error messages
#define LOG_OFF 0

#define LOG_EVSE LOG_INFO                                                       // Default: LOG_INFO
#define LOG_MODBUS LOG_WARN                                                     // Default: LOG_WARN


#ifdef DEBUG_DISABLED
#define _Serialprintf Serial.printf //for standard use of the serial line
#define _Serialprintln Serial.println //for standard use of the serial line
#define _Serialprint Serial.print //for standard use of the serial line
#else
#define _Serialprintf rdebugA //for debugging over the serial line
#define _Serialprintln rdebugA //for debugging over the serial line
#define _Serialprint rdebugA //for debugging over the serial line
#include "RemoteDebug.h"  //https://github.com/JoaoLopesF/RemoteDebug
extern RemoteDebug Debug;
#endif


#define TRANSFORMER_COMP 100   


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
#define PIN_RCM_FAULT 13

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

#define SPI_MOSI 33                                                             // SPI connections to LCD
#define SPI_MISO -1
#define SPI_SCK 26
#define SPI_SS -1

#define CP_CHANNEL 0
#define RED_CHANNEL 2                                                           // PWM channel 2 (0 and 1 are used by CP signal)
#define GREEN_CHANNEL 3
#define BLUE_CHANNEL 4
#define LCD_CHANNEL 5                                                           // LED Backlight LCD

#define PWM_5 50                                                                // 5% of PWM
#define PWM_95 950                                                              // 95% of PWM
#define PWM_100 1000                                                            // 100% of PWM

#define ICAL 1024                                                               // Irms Calibration value (for Current transformers)
#define MAX_MAINS 25                                                            // max Current the Mains connection can supply
#define MAX_CURRENT 13                                                          // max charging Current for the EV
#define MIN_CURRENT 6                                                           // minimum Current the EV will accept
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
#define START_CURRENT 4                                                         // Start charging when surplus current on one phase exceeds 4A (Solar)
#define STOP_TIME 10                                                            // Stop charging after 10 minutes at MIN charge current (Solar)
#define IMPORT_CURRENT 0                                                        // Allow the use of grid power when solar charging (Amps)
#define MAINS_METER 1                                                           // Mains Meter, 1= Sensorbox, 2=Phoenix, 3= Finder, 4= Eastron, 5=Custom
#define GRID 0                                                                  // Grid, 0= 4-Wire CW, 1= 4-Wire CCW, 2= 3-Wire CW, 3= 3-Wire CCW
#define MAINS_METER_ADDRESS 10
#define MAINS_METER_MEASURE 0
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
#define WIFI_MODE 0
#define AP_PASSWORD "00000000"
#define USE_3PHASES 0


// Mode settings
#define MODE_NORMAL 0
#define MODE_SMART 1
#define MODE_SOLAR 2

#define MODBUS_BAUDRATE 9600
#define MODBUS_TIMEOUT 4
#define ACK_TIMEOUT 1000                                                        // 1000ms timeout
#define NR_EVSES 8
#define BROADCAST_ADR 0x09

#define STATE_A 0                                                               // A Vehicle not connected
#define STATE_B 1                                                               // B Vehicle connected / not ready to accept energy
#define STATE_C 2                                                               // C Vehicle connected / ready to accept energy / ventilation not required
#define STATE_D 3                                                               // D Vehicle connected / ready to accept energy / ventilation required (not implemented)
#define STATE_COMM_B 4                                                          // E State change request A->B (set by node)
#define STATE_COMM_B_OK 5                                                       // F State change A->B OK (set by master)
#define STATE_COMM_C 6                                                          // G State change request B->C (set by node)
#define STATE_COMM_C_OK 7                                                       // H State change B->C OK (set by master)
#define STATE_ACTSTART 8                                                        // I Activation mode in progress
#define STATE_B1 9                                                              // J Vehicle connected / no PWM signal
#define STATE_C1 10                                                             // K Vehicle charging / no PWM signal (temp state when stopping charge from EVSE)

#define NOSTATE 255

#define PILOT_12V 1
#define PILOT_9V 2
#define PILOT_6V 3
#define PILOT_DIODE 4
#define PILOT_NOK 0


#define NO_ERROR 0
#define LESS_6A 1
#define CT_NOCOMM 2
#define TEMP_HIGH 4
#define UNUSED 8                                                                // Unused
#define RCM_TRIPPED 16                                                          // RCM tripped. >6mA DC residual current detected.
#define NO_SUN 32
#define Test_IO 64
#define BL_FLASH 128

#define STATE_A_LED_BRIGHTNESS 40
#define STATE_B_LED_BRIGHTNESS 255
#define ERROR_LED_BRIGHTNESS 255
#define WAITING_LED_BRIGHTNESS 255
#define LCD_BRIGHTNESS 255


#define CONTACTOR1_ON digitalWrite(PIN_SSR, HIGH);
#define CONTACTOR1_OFF digitalWrite(PIN_SSR, LOW);

#define CONTACTOR2_ON digitalWrite(PIN_SSR2, HIGH);
#define CONTACTOR2_OFF digitalWrite(PIN_SSR2, LOW);

#define BACKLIGHT_ON digitalWrite(PIN_LCD_LED, HIGH);
#define BACKLIGHT_OFF digitalWrite(PIN_LCD_LED, LOW);

#define ACTUATOR_LOCK { digitalWrite(PIN_ACTB, HIGH); digitalWrite(PIN_ACTA, LOW); }
#define ACTUATOR_UNLOCK { digitalWrite(PIN_ACTB, LOW); digitalWrite(PIN_ACTA, HIGH); }
#define ACTUATOR_OFF { digitalWrite(PIN_ACTB, HIGH); digitalWrite(PIN_ACTA, HIGH); }

#define ONEWIRE_LOW { digitalWrite(PIN_SW_IN, LOW); pinMode(PIN_SW_IN, OUTPUT); }   // SW set to 0, set to output (driven low)
#define ONEWIRE_HIGH { digitalWrite(PIN_SW_IN, HIGH); pinMode(PIN_SW_IN, OUTPUT); } // SW set to 1, set to output (driven low)
#define ONEWIRE_FLOATHIGH pinMode(PIN_SW_IN, INPUT);                                // SW input (floating high)

#define RCMFAULT digitalRead(PIN_RCM_FAULT)


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
#define MENU_CAL 15                                                             // 0x0203: CT calibration value
#define MENU_MAINS 16                                                           // 0x0204: Max Mains Current
#define MENU_START 17                                                           // 0x0205: Surplus energy start Current
#define MENU_STOP 18                                                            // 0x0206: Stop solar charging at 6A after this time
#define MENU_IMPORT 19                                                          // 0x0207: Allow grid power when solar charging
#define MENU_MAINSMETER 20                                                      // 0x0208: Type of Mains electric meter
#define MENU_MAINSMETERADDRESS 21                                               // 0x0209: Address of Mains electric meter
#define MENU_MAINSMETERMEASURE 22                                               // 0x020A: What does Mains electric meter measure
#define MENU_PVMETER 23                                                         // 0x020B: Type of PV electric meter
#define MENU_PVMETERADDRESS 24                                                  // 0x020C: Address of PV electric meter
#define MENU_EMCUSTOM_ENDIANESS 25                                              // 0x020D: Byte order of custom electric meter
#define MENU_EMCUSTOM_DATATYPE 26                                               // 0x020E: Data type of custom electric meter
#define MENU_EMCUSTOM_FUNCTION 27                                               // 0x020F: Modbus Function (3/4) of custom electric meter
#define MENU_EMCUSTOM_UREGISTER 28                                              // 0x0210: Register for Voltage (V) of custom electric meter
#define MENU_EMCUSTOM_UDIVISOR 29                                               // 0x0211: Divisor for Voltage (V) of custom electric meter (10^x)
#define MENU_EMCUSTOM_IREGISTER 30                                              // 0x0212: Register for Current (A) of custom electric meter
#define MENU_EMCUSTOM_IDIVISOR 31                                               // 0x0213: Divisor for Current (A) of custom electric meter (10^x)
#define MENU_EMCUSTOM_PREGISTER 32                                              // 0x0214: Register for Power (W) of custom electric meter
#define MENU_EMCUSTOM_PDIVISOR 33                                               // 0x0215: Divisor for Power (W) of custom electric meter (10^x)
#define MENU_EMCUSTOM_EREGISTER 34                                              // 0x0216: Register for Energy (kWh) of custom electric meter
#define MENU_EMCUSTOM_EDIVISOR 35                                               // 0x0217: Divisor for Energy (kWh) of custom electric meter (10^x)
#define MENU_EMCUSTOM_READMAX 36                                                // 0x0218: Maximum register read (ToDo)
#define MENU_WIFI 37                                                            // 0x0219: WiFi mode
#define MENU_3F 38
#define MENU_EXIT 39

#define MENU_STATE 50

#if LOG_EVSE >= LOG_DEBUG
#define LOG_DEBUG_EVSE
#endif
#if LOG_EVSE >= LOG_INFO
#define LOG_INFO_EVSE
#endif
#if LOG_EVSE >= LOG_WARN
#define LOG_WARN_EVSE
#endif
#if LOG_MODBUS >= LOG_DEBUG
#define LOG_DEBUG_MODBUS
#endif
#if LOG_MODBUS >= LOG_INFO
#define LOG_INFO_MODBUS
#endif
#if LOG_MODBUS >= LOG_WARN
#define LOG_WARN_MODBUS
#endif

#define _RSTB_0 digitalWrite(PIN_LCD_RST, LOW);
#define _RSTB_1 digitalWrite(PIN_LCD_RST, HIGH);
#define _A0_0 digitalWrite(PIN_LCD_A0_B2, LOW);
#define _A0_1 digitalWrite(PIN_LCD_A0_B2, HIGH);

#define EM_SENSORBOX 1                                                          // Mains meter types
#define EM_PHOENIX_CONTACT 2
#define EM_FINDER 3
#define EM_EASTRON 4
#define EM_EASTRON_INV 5
#define EM_ABB 6
#define EM_SOLAREDGE 7
#define EM_WAGO 8
#define EM_API 9
#define EM_CUSTOM 10

#define ENDIANESS_LBF_LWF 0
#define ENDIANESS_LBF_HWF 1
#define ENDIANESS_HBF_LWF 2
#define ENDIANESS_HBF_HWF 3


typedef enum mb_datatype {
    MB_DATATYPE_INT32 = 0,
    MB_DATATYPE_FLOAT32 = 1,
    MB_DATATYPE_INT16 = 2,
    MB_DATATYPE_MAX,
} MBDataType;


extern portMUX_TYPE rtc_spinlock;   //TODO: Will be placed in the appropriate position after the rtc module is finished.

#define RTC_ENTER_CRITICAL()    portENTER_CRITICAL(&rtc_spinlock)
#define RTC_EXIT_CRITICAL()     portEXIT_CRITICAL(&rtc_spinlock)


extern IPAddress localIp;
extern String APhostname;
extern String APpassword;
extern struct tm timeinfo;

extern uint8_t GLCDbuf[512];                                                    // GLCD buffer (half of the display)

extern uint16_t MaxMains;                                                       // Max Mains Amps (hard limit, limited by the MAINS connection)
extern uint16_t MaxCurrent;                                                     // Max Charge current
extern uint16_t MinCurrent;                                                     // Minimal current the EV is happy with
extern uint16_t ICal;                                                           // CT calibration value
extern uint8_t Mode;                                                            // EVSE mode
extern uint8_t Lock;                                                            // Cable lock enable/disable
extern uint16_t MaxCircuit;                                                     // Max current of the EVSE circuit
extern uint8_t Config;                                                          // Configuration (Fixed Cable or Type 2 Socket)
extern uint8_t LoadBl;                                                          // Load Balance Setting (Disable, Master or Node)
extern uint8_t Switch;                                                          // Allow access to EVSE with button on SW
extern uint8_t RCmon;                                                           // Residual Current monitor
extern uint8_t Grid;
extern uint16_t StartCurrent;
extern uint16_t StopTime;
extern uint16_t ImportCurrent;
extern uint8_t MainsMeter;                                                      // Type of Mains electric meter (0: Disabled / Constants EM_*)
extern uint8_t MainsMeterAddress;
extern uint8_t MainsMeterMeasure;                                               // What does Mains electric meter measure (0: Mains (Home+EVSE+PV) / 1: Home+EVSE / 2: Home)
extern uint8_t PVMeter;                                                         // Type of PV electric meter (0: Disabled / Constants EM_*)
extern uint8_t PVMeterAddress;
extern uint8_t EVMeter;                                                         // Type of EV electric meter (0: Disabled / Constants EM_*)
extern uint8_t EVMeterAddress;
extern uint8_t RFIDReader;
extern uint8_t WIFImode;

extern int32_t Irms[3];                                                         // Momentary current per Phase (Amps *10) (23 = 2.3A)
extern int32_t Irms_EV[3];                                                         // Momentary current per Phase (Amps *10) (23 = 2.3A)

extern uint8_t State;
extern uint8_t ErrorFlags;
extern uint8_t NextState;

extern uint16_t MaxCapacity;                                                    // Cable limit (Amps)(limited by the wire in the charge cable, set automatically, or manually if Config=Fixed Cable)
extern int16_t Imeasured;                                                       // Max of all CT inputs (Amps * 10) (23 = 2.3A)
extern int16_t Isum;
extern uint16_t Balanced[NR_EVSES];                                             // Amps value per EVSE

extern uint8_t menu;
extern uint32_t ChargeTimer;                                                    // seconds counter
extern uint8_t LCDTimer;
extern uint16_t BacklightTimer;                                                 // remaining seconds the LCD backlight is active
extern int8_t TempEVSE;                                                         // Temperature EVSE in deg C (-40 - +125)
extern uint8_t ButtonState;                                                     // Holds latest push Buttons state (LSB 2:0)
extern uint8_t OldButtonState;                                                  // Holds previous push Buttons state (LSB 2:0)
extern uint8_t LCDNav;
extern uint8_t LCDupdate;
extern uint8_t SubMenu;
extern uint32_t ScrollTimer;
extern uint8_t LCDpos;
extern uint8_t ChargeDelay;                                                     // Delays charging in seconds.
extern uint8_t TestState;
extern uint8_t Access_bit;
extern uint8_t GridActive;                                                      // When the CT's are used on Sensorbox2, it enables the GRID menu option.
extern uint8_t CalActive;                                                       // When the CT's are used on Sensorbox(1.5 or 2), it enables the CAL menu option.
extern uint16_t Iuncal;
extern uint16_t SolarStopTimer;
extern int32_t EnergyCharged;
extern int32_t PowerMeasured;
extern uint8_t RFIDstatus;
extern bool LocalTimeSet;

extern uint8_t MenuItems[MENU_EXIT];

const struct {
    char Key[8];
    char LCD[9];
    char Desc[52];
    uint16_t Min;
    uint16_t Max;
    uint16_t Default;
} MenuStr[MENU_EXIT + 1] = {
    {"", "", "Not in menu", 0, 0, 0},
    {"", "", "Hold 2 sec", 0, 0, 0},

    // Node specific configuration
    /* Key,    LCD,       Desc,                                                 Min, Max, Default */
    {"CONFIG", "CONFIG",  "Fixed Cable or Type 2 Socket",                       0, 1, CONFIG},
    {"LOCK",   "LOCK",    "Cable locking actuator type",                        0, 2, LOCK},
    {"MIN",    "MIN",     "MIN Charge Current the EV will accept (per phase)",  6, 16, MIN_CURRENT},
    {"MAX",    "MAX",     "MAX Charge Current for this EVSE (per phase)",       6, 80, MAX_CURRENT},
    {"LOADBL", "LOAD BAL","Load Balancing mode for 2-8 SmartEVSEs",             0, NR_EVSES, LOADBL},
    {"SW",     "SWITCH",  "Switch function control on pin SW",                  0, 4, SWITCH},
    {"RCMON",  "RCMON",   "Residual Current Monitor on pin RCM",                0, 1, RC_MON},
    {"RFID",   "RFID",    "RFID reader, learn/remove cards",                    0, 5, RFID_READER},
    {"EVEM",   "EV METER","Type of EV electric meter",                          0, EM_CUSTOM, EV_METER},
    {"EVAD",   "EV ADDR", "Address of EV electric meter",                       MIN_METER_ADDRESS, MAX_METER_ADDRESS, EV_METER_ADDRESS},

    // System configuration
    /* Key,    LCD,       Desc,                                                 Min, Max, Default */
    {"MODE",   "MODE",    "Normal, Smart or Solar EVSE mode",                   0, 2, MODE},
    {"CIRCUIT","CIRCUIT", "EVSE Circuit max Current",                           10, 160, MAX_CIRCUIT},
    {"GRID",   "GRID",    "Grid type to which the Sensorbox is connected",      0, 1, GRID},
    {"CAL",    "CAL",     "Calibrate CT1 (CT2+3 will also change)",             (unsigned int) (ICAL * 0.3), (unsigned int) (ICAL * 2.0), ICAL}, // valid range is 0.3 - 2.0 times measured value
    {"MAINS",  "MAINS",   "Max MAINS Current (per phase)",                      10, 200, MAX_MAINS},
    {"START",  "START",   "Surplus energy start Current (sum of phases)",       0, 48, START_CURRENT},
    {"STOP",   "STOP",    "Stop solar charging at 6A after this time",          0, 60, STOP_TIME},
    {"IMPORT", "IMPORT",  "Allow grid power when solar charging (sum of phase)",0, 20, IMPORT_CURRENT},
    {"MAINEM", "MAINSMET","Type of mains electric meter",                       1, EM_CUSTOM, MAINS_METER},
    {"MAINAD", "MAINSADR","Address of mains electric meter",                    MIN_METER_ADDRESS, MAX_METER_ADDRESS, MAINS_METER_ADDRESS},
    {"MAINM",  "MAINSMES","Mains electric meter scope (What does it measure?)", 0, 1, MAINS_METER_MEASURE},
    {"PVEM",   "PV METER","Type of PV electric meter",                          0, EM_CUSTOM, PV_METER},
    {"PVAD",   "PV ADDR", "Address of PV electric meter",                       MIN_METER_ADDRESS, MAX_METER_ADDRESS, PV_METER_ADDRESS},
    {"EMBO",   "BYTE ORD","Byte order of custom electric meter",                0, 3, EMCUSTOM_ENDIANESS},
    {"EMDATA", "DATATYPE","Data type of custom electric meter",                 0, MB_DATATYPE_MAX - 1, EMCUSTOM_DATATYPE},
    {"EMFUNC", "FUNCTION","Modbus Function of custom electric meter",           3, 4, EMCUSTOM_FUNCTION},
    {"EMUREG", "VOL REGI","Register for Voltage (V) of custom electric meter",  0, 65530, EMCUSTOM_UREGISTER},
    {"EMUDIV", "VOL DIVI","Divisor for Voltage (V) of custom electric meter",   0, 7, EMCUSTOM_UDIVISOR},
    {"EMIREG", "CUR REGI","Register for Current (A) of custom electric meter",  0, 65530, EMCUSTOM_IREGISTER},
    {"EMIDIV", "CUR DIVI","Divisor for Current (A) of custom electric meter",   0, 7, EMCUSTOM_IDIVISOR},
    {"EMPREG", "POW REGI","Register for Power (W) of custom electric meter",    0, 65534, EMCUSTOM_PREGISTER},
    {"EMPDIV", "POW DIVI","Divisor for Power (W) of custom electric meter",     0, 7, EMCUSTOM_PDIVISOR},
    {"EMEREG", "ENE REGI","Register for Energy (kWh) of custom electric meter", 0, 65534, EMCUSTOM_EREGISTER},
    {"EMEDIV", "ENE DIVI","Divisor for Energy (kWh) of custom electric meter",  0, 7, EMCUSTOM_EDIVISOR},
    {"EMREAD", "READ MAX","Max register read at once of custom electric meter", 3, 255, 3},
    {"WIFI",   "WIFI",    "Connect to WiFi access point",                       0, 2, WIFI_MODE},
    {"EV3P",   "3 PHASE",  "Can EV use 3 phases",                               0, 1, USE_3PHASES},

    {"EXIT", "EXIT", "EXIT", 0, 0, 0}
};


struct NodeStatus {
    bool Online;
    uint8_t ConfigChanged;
    uint8_t EVMeter;
    uint8_t EVAddress;
    uint8_t MinCurrent;     // 0.1A
    uint8_t Phases;
    uint16_t Timer;         // 1s
};

struct EMstruct {
    uint8_t Desc[10];
    uint8_t Endianness;     // 0: low byte first, low word first, 1: low byte first, high word first, 2: high byte first, low word first, 3: high byte first, high word first
    uint8_t Function;       // 3: holding registers, 4: input registers
    MBDataType DataType;    // How data is represented on this Modbus meter
    uint16_t URegister;     // Single phase voltage (V)
    uint8_t UDivisor;       // 10^x
    uint16_t IRegister;     // Single phase current (A)
    uint8_t IDivisor;       // 10^x
    uint16_t PRegister;     // Total power (W) -- only used for EV/PV meter momentary power
    uint8_t PDivisor;       // 10^x
    uint16_t ERegister;     // Total energy (kWh)
    uint8_t EDivisor;       // 10^x
};

extern struct EMstruct EMConfig[EM_CUSTOM + 1];

void CheckAPpassword(void);
void read_settings(bool write);
void write_settings(void);
void setSolarStopTimer(uint16_t Timer);
void setState(uint8_t NewState, boolean forceState);
void setState(uint8_t NewState);
void setAccess(bool Access);
uint8_t getMenuItems(void);
uint8_t setItemValue(uint8_t nav, uint16_t val);
uint16_t getItemValue(uint8_t nav);
const char * getMenuItemOption(uint8_t nav);
void ConfigureModbusMode(uint8_t newmode);


#endif

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

#ifndef __EVSE_ESP32

#define __EVSE_ESP32


#include <Arduino.h>
#include "main.h"
#include "glcd.h"
#include "meter.h"

// Pin definitions left side ESP32
#define PIN_TEMP 36
#define PIN_CP_IN 39
#define PIN_PP_IN 34
#define PIN_LOCK_IN 35
#define PIN_SSR 32
#define PIN_LCD_SDO_B3 33                                                       // = SPI_MOSI
#define PIN_LCD_CLK 26                                                          // = SPI_SCK
#define PIN_SSR2 27
#define PIN_LCD_LED 14
#define PIN_LEDB 12
#define PIN_RCM_FAULT_V30 13 //TODO ok for v4?

// Pin definitions right side ESP32
#define PIN_RS485_RX_V30 23
#define PIN_RS485_DIR 22
//#define PIN_RXD 
//#define PIN_TXD
#define PIN_RS485_TX 21
#define PIN_CP_OUT 19
#define PIN_ACTB_V30 18
#define PIN_ACTA_V30 17
#define PIN_SW_IN_V30 16
#define PIN_LEDG 4
#define PIN_IO0_B1 0
#define PIN_LEDR 2
#define PIN_CPOFF 15

#if SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40
#define PIN_LCD_A0_B2 25
#define PIN_LCD_RST 5
#define SPI_MOSI 33                                                             // SPI connections to LCD
#define SPI_MISO -1
#define SPI_SCK 26
#define SPI_SS -1

#define CP_CHANNEL 0
#define RED_CHANNEL 2                                                           // PWM channel 2 (0 and 1 are used by CP signal)
#define GREEN_CHANNEL 3
#define BLUE_CHANNEL 4
#define LCD_CHANNEL 5                                                           // LED Backlight LCD
#else
#define PIN_LCD_A0_B2 40
#define PIN_LCD_RST 42
#include "funconfig.h"
#endif //SMARTEVSE_VERSION

//extra pin definitions for v3.1
#define PIN_RCM_FAULT_V31 38
#define PIN_ACTA_V31 10
#define PIN_ACTB_V31 9
#define PIN_SW_IN_V31 23
#define PIN_RS485_RX_V31 37
#define PIN_EXT_V31 13
#define PIN_BUZZER_V31 18

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
#if FAKE_RFID
extern uint8_t Show_RFID;
#endif

extern uint8_t State;

extern int16_t Isum;
extern uint16_t Balanced[NR_EVSES];                                             // Amps value per EVSE

extern uint8_t LCDTimer;
extern uint16_t BacklightTimer;                                                 // remaining seconds the LCD backlight is active
extern uint8_t ButtonState;                                                     // Holds latest push Buttons state (LSB 2:0)
extern uint8_t OldButtonState;                                                  // Holds previous push Buttons state (LSB 2:0)
extern uint8_t ChargeDelay;                                                     // Delays charging in seconds.
extern uint8_t TestState;
extern AccessStatus_t AccessStatus;
extern uint16_t CardOffset;

extern uint16_t SolarStopTimer;
extern uint8_t RFIDstatus;
extern uint8_t OcppMode;
extern bool LocalTimeSet;
extern uint32_t serialnr;

extern const char StrEnableC2[5][12];
//extern Single_Phase_t Switching_To_Single_Phase;
extern uint8_t Nr_Of_Phases_Charging;

extern uint16_t EMConfigSize;

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
    {"EV METER","Type of EV electric meter",                          0, (uint16_t) (EMConfigSize / sizeof(EMConfig[0])-1), EV_METER},
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
    {"MAINS MET","Type of mains electric meter",                       0, (uint16_t) (EMConfigSize / sizeof(EMConfig[0])-1), MAINS_METER},
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
    {"LCD PIN", "Pin code to operate LCD from web interface",         0, 65534, 0},
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
void setAccess(AccessStatus_t Access);
void setOverrideCurrent(uint16_t Current);
void SetCPDuty(uint32_t DutyCycle);
uint8_t setItemValue(uint8_t nav, uint16_t val);
uint16_t getItemValue(uint8_t nav);
void ConfigureModbusMode(uint8_t newmode);
void setMode(uint8_t NewMode) ;
void BuzzConfirmation(void);
void BuzzError(void);

#if ENABLE_OCPP && defined(SMARTEVSE_VERSION) //run OCPP only on ESP32
void ocppUpdateRfidReading(const unsigned char *uuid, size_t uuidLen);
bool ocppIsConnectorPlugged();

bool ocppHasTxNotification();
MicroOcpp::TxNotification ocppGetTxNotification();

bool ocppLockingTxDefined();
#endif //ENABLE_OCPP

#if SMARTEVSE_VERSION >= 40
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
#define LCD_LED 41
#define LCD_CS 1

#define LCD_CHANNEL 5               // PWM channel

// ESP-WCH Communication States
#define COMM_OFF 0
#define COMM_VER_REQ 1              // Version Reqest           ESP -> WCH
#define COMM_VER_RSP 2              // Version Response         ESP <- WCH
#define COMM_CONFIG_SET 3           // Configuration Set        ESP -> WCH
#define COMM_CONFIG_CNF 4           // Configuration confirm.   ESP <- WCH
#define COMM_STATUS_REQ 5           // Status Request
#define COMM_STATUS_RSP 6           // Status Response

#endif //SMARTEVSE_VERSION

#endif

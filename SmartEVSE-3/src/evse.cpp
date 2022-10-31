#include <Arduino.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Preferences.h>

#include <FS.h>
#include <SPIFFS.h>

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsync_WiFiManager.h>
#include <ESPmDNS.h>
#include <Update.h>

#include <Logging.h>
#include <ModbusServerRTU.h>        // Slave/node
#include <ModbusClientRTU.h>        // Master

#include <time.h>

#include <nvs_flash.h>              // nvs initialisation code (can be removed?)

#include <soc/sens_reg.h>
#include <soc/sens_struct.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>

#include <driver/uart.h>
#include <soc/rtc_io_struct.h>

#include "evse.h"
#include "glcd.h"
#include "utils.h"
#include "OneWire.h"
#include "modbus.h"

#ifndef DEBUG_DISABLED
RemoteDebug Debug;
#endif

const char* NTP_SERVER = "europe.pool.ntp.org";        // only one server is supported

// Specification of the Time Zone string:
// http://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html
// list of time zones: https://remotemonitoringsystems.ca/time-zone-abbreviations.php
// more: https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
//
const char* TZ_INFO    = "CET-1CEST-2,M3.5.0/2,M10.5.0/3";      // Europe/Amsterdam
//const char* TZ_INFO    = "GMT+0IST-1,M3.5.0/1,M10.5.0/2";     // Europe/Dublin
//const char* TZ_INFO    = "EET-2EEST-3,M3.5.0/3,M10.5.0/4";    // Europe/Helsinki
//const char* TZ_INFO    = "WET-0WEST-1,M3.5.0/1,M10.5.0/2";    // Europe/Lisbon
//const char* TZ_INFO    = "GMT+0BST-1,M3.5.0/1,M10.5.0/2";     // Europe/London
//const char* TZ_INFO    = "PST8PDT,M3.2.0,M11.1.0";            // USA, Los Angeles

struct tm timeinfo;

AsyncWebServer webServer(80);
//AsyncWebSocket ws("/ws");           // data to/from webpage
DNSServer dnsServer;
String APhostname = "SmartEVSE-" + String( MacId() & 0xffff, 10);           // SmartEVSE access point Name = SmartEVSE-xxxxx

ESPAsync_WiFiManager ESPAsync_wifiManager(&webServer, &dnsServer, APhostname.c_str());

// SSID and PW for your Router
String Router_SSID;
String Router_Pass;

// Create a ModbusRTU server and client instance on Serial1 
ModbusServerRTU MBserver(Serial1, 2000, PIN_RS485_DIR);     // TCP timeout set to 2000 ms
ModbusClientRTU MBclient(Serial1, PIN_RS485_DIR);  

hw_timer_t * timerA = NULL;
Preferences preferences;

static esp_adc_cal_characteristics_t * adc_chars_CP;
static esp_adc_cal_characteristics_t * adc_chars_PP;
static esp_adc_cal_characteristics_t * adc_chars_Temperature;

struct ModBus MB;          // Used by SmartEVSE fuctions

const char StrStateName[11][10] = {"A", "B", "C", "D", "COMM_B", "COMM_B_OK", "COMM_C", "COMM_C_OK", "Activate", "B1", "C1"};
const char StrStateNameWeb[11][17] = {"Ready to Charge", "Connected to EV", "Charging", "D", "Request State B", "State B OK", "Request State C", "State C OK", "Activate", "Charging Stopped", "Stop Charging" };
const char StrErrorNameWeb[9][20] = {"None", "No Power Available", "Communication Error", "Temperature High", "Unused", "RCM Tripped", "Waiting for Solar", "Test IO", "Flash Error"};

// Global data


// The following data will be updated by eeprom/storage data at powerup:
uint16_t MaxMains = MAX_MAINS;                                              // Max Mains Amps (hard limit, limited by the MAINS connection) (A)
uint16_t MaxCurrent = MAX_CURRENT;                                          // Max Charge current (A)
uint16_t MinCurrent = MIN_CURRENT;                                          // Minimal current the EV is happy with (A)
uint16_t ICal = ICAL;                                                       // CT calibration value
uint8_t Mode = MODE;                                                        // EVSE mode (0:Normal / 1:Smart / 2:Solar)
uint8_t Lock = LOCK;                                                        // Cable lock (0:Disable / 1:Solenoid / 2:Motor)
uint16_t MaxCircuit = MAX_CIRCUIT;                                          // Max current of the EVSE circuit (A)
uint8_t Config = CONFIG;                                                    // Configuration (0:Socket / 1:Fixed Cable)
uint8_t LoadBl = LOADBL;                                                    // Load Balance Setting (0:Disable / 1:Master / 2-8:Node)
uint8_t Switch = SWITCH;                                                    // External Switch (0:Disable / 1:Access B / 2:Access S / 3:Smart-Solar B / 4:Smart-Solar S)
                                                                            // B=momentary push button, S=toggle switch
uint8_t RCmon = RC_MON;                                                     // Residual Current Monitor (0:Disable / 1:Enable)
uint16_t StartCurrent = START_CURRENT;
uint16_t StopTime = STOP_TIME;
uint16_t ImportCurrent = IMPORT_CURRENT;
uint8_t MainsMeter = MAINS_METER;                                           // Type of Mains electric meter (0: Disabled / Constants EM_*)
uint8_t MainsMeterAddress = MAINS_METER_ADDRESS;
uint8_t MainsMeterMeasure = MAINS_METER_MEASURE;                            // What does Mains electric meter measure (0: Mains (Home+EVSE+PV) / 1: Home+EVSE / 2: Home)
uint8_t PVMeter = PV_METER;                                                 // Type of PV electric meter (0: Disabled / Constants EM_*)
uint8_t PVMeterAddress = PV_METER_ADDRESS;
uint8_t Grid = GRID;                                                        // Type of Grid connected to Sensorbox (0:4Wire / 1:3Wire )
uint8_t EVMeter = EV_METER;                                                 // Type of EV electric meter (0: Disabled / Constants EM_*)
uint8_t EVMeterAddress = EV_METER_ADDRESS;
uint8_t RFIDReader = RFID_READER;                                           // RFID Reader (0:Disabled / 1:Enabled / 2:Enable One / 3:Learn / 4:Delete / 5:Delete All)
#ifdef FAKE_RFID
uint8_t Show_RFID = 0;
#endif
uint8_t WIFImode = WIFI_MODE;                                               // WiFi Mode (0:Disabled / 1:Enabled / 2:Start Portal)
String APpassword = "00000000";

boolean enable3f = USE_3PHASES;
uint16_t maxTemp = MAX_TEMPERATURE;

int32_t Irms[3]={0, 0, 0};                                                  // Momentary current per Phase (23 = 2.3A) (resolution 100mA)
int32_t Irms_EV[3]={0, 0, 0};                                               // Momentary current per Phase (23 = 2.3A) (resolution 100mA)
                                                                            // Max 3 phases supported
uint8_t State = STATE_A;
uint8_t ErrorFlags = NO_ERROR;
uint8_t NextState;
uint8_t pilot;

uint16_t MaxCapacity;                                                       // Cable limit (A) (limited by the wire in the charge cable, set automatically, or manually if Config=Fixed Cable)
uint16_t ChargeCurrent;                                                     // Calculated Charge Current (Amps *10)
uint16_t OverrideCurrent = 0;                                               // Temporary assigned current (Amps *10) (modbus)
int16_t Imeasured = 0;                                                      // Max of all Phases (Amps *10) of mains power
int16_t Imeasured_EV = 0;                                                   // Max of all Phases (Amps *10) of EV power
int16_t Isum = 0;                                                           // Sum of all measured Phases (Amps *10) (can be negative)

// Load Balance variables
int16_t IsetBalanced = 0;                                                   // Max calculated current (Amps *10) available for all EVSE's
uint16_t Balanced[NR_EVSES] = {0, 0, 0, 0, 0, 0, 0, 0};                     // Amps value per EVSE
uint16_t BalancedMax[NR_EVSES] = {0, 0, 0, 0, 0, 0, 0, 0};                  // Max Amps value per EVSE
uint8_t BalancedState[NR_EVSES] = {0, 0, 0, 0, 0, 0, 0, 0};                 // State of all EVSE's 0=not active (state A), 1=charge request (State B), 2= Charging (State C)
uint16_t BalancedError[NR_EVSES] = {0, 0, 0, 0, 0, 0, 0, 0};                // Error state of EVSE
struct NodeStatus Node[NR_EVSES] = {                                            // 0: Master / 1: Node 1 ...
   /*         Config   EV     EV       Min                    *
    * Online, Changed, Meter, Address, Current, Phases, Timer */
    {   true,       0,     0,       0,       0,      0,     0 },
    {  false,       1,     0,       0,       0,      0,     0 },
    {  false,       1,     0,       0,       0,      0,     0 },
    {  false,       1,     0,       0,       0,      0,     0 },
    {  false,       1,     0,       0,       0,      0,     0 },
    {  false,       1,     0,       0,       0,      0,     0 },
    {  false,       1,     0,       0,       0,      0,     0 },
    {  false,       1,     0,       0,       0,      0,     0 }
};

uint8_t menu = 0;
uint8_t lock1 = 0, lock2 = 1;
uint8_t UnlockCable = 0, LockCable = 0;
uint8_t timeout = 5;                                                        // communication timeout (sec)
uint16_t BacklightTimer = 0;                                                // Backlight timer (sec)
uint8_t BacklightSet = 0;
uint8_t LCDTimer = 0;
uint8_t AccessTimer = 0;
int8_t TempEVSE = 0;                                                        // Temperature EVSE in deg C (-50 to +125)
uint8_t ButtonState = 0x0f;                                                 // Holds latest push Buttons state (LSB 3:0)
uint8_t OldButtonState = 0x0f;                                              // Holds previous push Buttons state (LSB 3:0)
uint8_t LCDNav = 0;
uint8_t SubMenu = 0;
uint32_t ScrollTimer = 0;
uint8_t LCDpos = 0;
uint8_t LCDupdate = 0;                                                      // flag to update the LCD every 1000ms
uint8_t ChargeDelay = 0;                                                    // Delays charging at least 60 seconds in case of not enough current available.
uint8_t C1Timer = 0;
uint8_t NoCurrent = 0;                                                      // counts overcurrent situations.
uint8_t TestState = 0;
uint8_t ModbusRequest = 0;                                                  // Flag to request Modbus information
uint8_t MenuItems[MENU_EXIT];
uint8_t Access_bit = 0;
uint8_t ConfigChanged = 0;
uint32_t serialnr = 0;
uint8_t GridActive = 0;                                                     // When the CT's are used on Sensorbox2, it enables the GRID menu option.
uint8_t CalActive = 0;                                                      // When the CT's are used on Sensorbox(1.5 or 2), it enables the CAL menu option.
uint16_t Iuncal = 0;                                                        // Uncalibrated CT1 measurement (resolution 10mA)

uint16_t SolarStopTimer = 0;
int32_t EnergyCharged = 0;                                                  // kWh meter value energy charged. (Wh) (will reset if state changes from A->B)
int32_t EnergyMeterStart = 0;                                               // kWh meter value is stored once EV is connected to EVSE (Wh)
int32_t PowerMeasured = 0;                                                  // Measured Charge power in Watt by kWh meter
uint8_t RFIDstatus = 0;
uint8_t ExternalMaster = 0;
int32_t EnergyEV = 0;   
int32_t Mains_export_active_energy = 0;                                     // Mainsmeter exported active energy, only for API purposes so you can guard the
                                                                            // enery usage of your house
int32_t Mains_import_active_energy = 0;                                     // Mainsmeter imported active energy, only for API purposes so you can guard the
                                                                            // enery usage of your house
int32_t CM[3]={0, 0, 0};
int32_t PV[3]={0, 0, 0};
uint8_t ResetKwh = 2;                                                       // if set, reset EV kwh meter at state transition B->C
                                                                            // cleared when charging, reset to 1 when disconnected (state A)
uint8_t ActivationMode = 0, ActivationTimer = 0;
volatile uint16_t adcsample = 0;
volatile uint16_t ADCsamples[25];                                           // declared volatile, as they are used in a ISR
volatile uint8_t sampleidx = 0;
volatile int adcchannel = ADC1_CHANNEL_3;
char str[20];
bool LocalTimeSet = false;

int phasesLastUpdate = 0;
int32_t IrmsOriginal[3]={0, 0, 0};   
int homeBatteryCurrent = 0;
int homeBatteryLastUpdate = 0; // Time in milliseconds

struct EMstruct EMConfig[EM_CUSTOM + 1] = {
    /* DESC,      ENDIANNESS,      FCT, DATATYPE,            U_REG,DIV, I_REG,DIV, P_REG,DIV, E_REG_IMP,DIV, E_REG_EXP, DIV */
    {"Disabled",  ENDIANESS_LBF_LWF, 0, MB_DATATYPE_INT32,        0, 0,      0, 0,      0, 0,      0, 0,0     , 0}, // First entry!
    {"Sensorbox", ENDIANESS_HBF_HWF, 4, MB_DATATYPE_FLOAT32, 0xFFFF, 0,      0, 0, 0xFFFF, 0, 0xFFFF, 0,0     , 0}, // Sensorbox (Own routine for request/receive)
    {"Phoenix C", ENDIANESS_HBF_LWF, 4, MB_DATATYPE_INT32,      0x0, 1,    0xC, 3,   0x28, 1,   0x3E, 1,0     , 0}, // PHOENIX CONTACT EEM-350-D-MCB (0,1V / mA / 0,1W / 0,1kWh) max read count 11
    {"Finder",    ENDIANESS_HBF_HWF, 4, MB_DATATYPE_FLOAT32, 0x1000, 0, 0x100E, 0, 0x1026, 0, 0x1106, 3,0x110E, 3}, // Finder 7E.78.8.400.0212 (V / A / W / Wh) max read count 127
    {"Eastron",   ENDIANESS_HBF_HWF, 4, MB_DATATYPE_FLOAT32,    0x0, 0,    0x6, 0,   0x34, 0,  0x48 , 0,0x4A  , 0}, // Eastron SDM630 (V / A / W / kWh) max read count 80
    {"InvEastrn", ENDIANESS_HBF_HWF, 4, MB_DATATYPE_FLOAT32,    0x0, 0,    0x6, 0,   0x34, 0,  0x48 , 0,0x4A  , 0}, // Since Eastron SDM series are bidirectional, sometimes they are connected upsidedown, so positive current becomes negative etc.; Eastron SDM630 (V / A / W / kWh) max read count 80
    {"ABB",       ENDIANESS_HBF_HWF, 3, MB_DATATYPE_INT32,   0x5B00, 1, 0x5B0C, 2, 0x5B14, 2, 0x5000, 2,0x5004, 2}, // ABB B23 212-100 (0.1V / 0.01A / 0.01W / 0.01kWh) RS485 wiring reversed / max read count 125
    {"SolarEdge", ENDIANESS_HBF_HWF, 3, MB_DATATYPE_INT16,    40196, 0,  40191, 0,  40083, 0,  40234, 3, 40226, 3}, // SolarEdge SunSpec (0.01V (16bit) / 0.1A (16bit) / 1W  (16bit) / 1 Wh (32bit))
    {"WAGO",      ENDIANESS_HBF_HWF, 3, MB_DATATYPE_FLOAT32, 0x5002, 0, 0x500C, 0, 0x5012, 3, 0x600C, 0,0x6018, 0}, // WAGO 879-30x0 (V / A / kW / kWh)//TODO maar WAGO heeft ook totaal
    {"API",       ENDIANESS_HBF_HWF, 3, MB_DATATYPE_FLOAT32, 0x5002, 0, 0x500C, 0, 0x5012, 3, 0x6000, 0,0x6018, 0}, // WAGO 879-30x0 (V / A / kW / kWh)
    {"Custom",    ENDIANESS_LBF_LWF, 4, MB_DATATYPE_INT32,        0, 0,      0, 0,      0, 0,      0, 0,     0, 0}  // Last entry!
};


// Some low level stuff here to setup the ADC, and perform the conversion.
//
//
uint16_t IRAM_ATTR local_adc1_read(int channel) {
    uint16_t adc_value;

    SENS.sar_read_ctrl.sar1_dig_force = 0;                      // switch SARADC into RTC channel 
    SENS.sar_meas_wait2.force_xpd_sar = SENS_FORCE_XPD_SAR_PU;  // adc_power_on
    RTCIO.hall_sens.xpd_hall = false;                           // disable other peripherals
    
    //adc_ll_amp_disable()  // Close ADC AMP module if don't use it for power save.
    SENS.sar_meas_wait2.force_xpd_amp = SENS_FORCE_XPD_AMP_PD;  // channel is set in the convert function
    // disable FSM, it's only used by the LNA.
    SENS.sar_meas_ctrl.amp_rst_fb_fsm = 0; 
    SENS.sar_meas_ctrl.amp_short_ref_fsm = 0;
    SENS.sar_meas_ctrl.amp_short_ref_gnd_fsm = 0;
    SENS.sar_meas_wait1.sar_amp_wait1 = 1;
    SENS.sar_meas_wait1.sar_amp_wait2 = 1;
    SENS.sar_meas_wait2.sar_amp_wait3 = 1; 

    // adc_hal_set_controller(ADC_NUM_1, ADC_CTRL_RTC);         //Set controller
    // see esp-idf/components/hal/esp32/include/hal/adc_ll.h
    SENS.sar_read_ctrl.sar1_dig_force       = 0;                // 1: Select digital control;       0: Select RTC control.
    SENS.sar_meas_start1.meas1_start_force  = 1;                // 1: SW control RTC ADC start;     0: ULP control RTC ADC start.
    SENS.sar_meas_start1.sar1_en_pad_force  = 1;                // 1: SW control RTC ADC bit map;   0: ULP control RTC ADC bit map;
    SENS.sar_touch_ctrl1.xpd_hall_force     = 1;                // 1: SW control HALL power;        0: ULP FSM control HALL power.
    SENS.sar_touch_ctrl1.hall_phase_force   = 1;                // 1: SW control HALL phase;        0: ULP FSM control HALL phase.

    // adc_hal_convert(ADC_NUM_1, channel, &adc_value);
    // see esp-idf/components/hal/esp32/include/hal/adc_ll.h
    SENS.sar_meas_start1.sar1_en_pad = (1 << channel);          // select ADC channel to sample on
    while (SENS.sar_slave_addr1.meas_status != 0);              // wait for conversion to be idle (blocking)
    SENS.sar_meas_start1.meas1_start_sar = 0;         
    SENS.sar_meas_start1.meas1_start_sar = 1;                   // start ADC conversion
    while (SENS.sar_meas_start1.meas1_done_sar == 0);           // wait (blocking) for conversion to finish
    adc_value = SENS.sar_meas_start1.meas1_data_sar;            // read ADC value from register

    return adc_value;
}



// CP pin low to high transition ISR
//
//
void IRAM_ATTR onCPpulse() {

  // reset timer, these functions are in IRAM !
  timerWrite(timerA, 0);                                        
  timerAlarmEnable(timerA);
}



// Alarm interrupt handler
// in STATE A this is called every 1ms (autoreload)
// in STATE B/C there is a PWM signal, and the Alarm is set to 5% after the low-> high transition of the PWM signal
void IRAM_ATTR onTimerA() {

  RTC_ENTER_CRITICAL();
  adcsample = local_adc1_read(adcchannel);
  RTC_EXIT_CRITICAL();

  ADCsamples[sampleidx++] = adcsample;
  if (sampleidx == 25) sampleidx = 0;
}


// --------------------------- END of ISR's -----------------------------------------------------

// Blink the RGB LED and LCD Backlight.
//
// NOTE: need to add multiple colour schemes 
//
// Task is called every 10ms
void BlinkLed(void * parameter) {
    uint8_t LcdPwm = 0;
    uint8_t RedPwm = 0, GreenPwm = 0, BluePwm = 0;
    uint8_t LedCount = 0;                                                   // Raw Counter before being converted to PWM value
    unsigned int LedPwm = 0;                                                // PWM value 0-255

    while(1) 
    {
        // Backlight LCD
        if (BacklightTimer > 1 && BacklightSet != 1) {                      // Enable LCD backlight at max brightness
                                                                            // start only when fully off(0) or when we are dimming the backlight(2)
            LcdPwm = LCD_BRIGHTNESS;
            ledcWrite(LCD_CHANNEL, LcdPwm);
            BacklightSet = 1;                                               // 1: we have set the backlight to max brightness
        } 
        
        if (BacklightTimer == 1 && LcdPwm >= 3) {                           // Last second of Backlight
            LcdPwm -= 3;
            ledcWrite(LCD_CHANNEL, ease8InOutQuad(LcdPwm));                 // fade out
            BacklightSet = 2;                                               // 2: we are dimming the backlight
        }
                                                                            // Note: could be simplified by removing following code if LCD_BRIGHTNESS is multiple of 3                                                               
        if (BacklightTimer == 0 && BacklightSet) {                          // End of LCD backlight
            ledcWrite(LCD_CHANNEL, 0);                                      // switch off LED PWM
            BacklightSet = 0;                                               // 0: backlight fully off
        }

        // RGB LED
        if (ErrorFlags || ChargeDelay) {

            if (ErrorFlags & (RCM_TRIPPED | CT_NOCOMM) ) {
                LedCount += 20;                                                 // Very rapid flashing, RCD tripped or no Serial Communication.
                if (LedCount > 128) LedPwm = ERROR_LED_BRIGHTNESS;              // Red LED 50% of time on, full brightness
                else LedPwm = 0;
                RedPwm = LedPwm;
                GreenPwm = 0;
                BluePwm = 0;
            } else {                                                            // Waiting for Solar power or not enough current to start charging
                LedCount += 2;                                                  // Slow blinking.
                if (LedCount > 230) LedPwm = WAITING_LED_BRIGHTNESS;            // LED 10% of time on, full brightness
                else LedPwm = 0;

                if (Mode == MODE_SOLAR) {                                       // Orange
                    RedPwm = LedPwm;
                    GreenPwm = LedPwm * 2 / 3;
                } else {                                                        // Green
                    RedPwm = 0;
                    GreenPwm = LedPwm;
                }    
                BluePwm = 0;
            }

        } else if (Access_bit == 0) {                                            // No Access, LEDs off
            RedPwm = 0;
            GreenPwm = 0;
            BluePwm = 0;
            LedPwm = 0;                  
        } else {                                                                // State A, B or C
    
            if (State == STATE_A) {
                LedPwm = STATE_A_LED_BRIGHTNESS;                                // STATE A, LED on (dimmed)
            
            } else if (State == STATE_B || State == STATE_B1) {
                LedPwm = STATE_B_LED_BRIGHTNESS;                                // STATE B, LED on (full brightness)
                LedCount = 128;                                                 // When switching to STATE C, start at full brightness

            } else if (State == STATE_C) {                                      
                if (Mode == MODE_SOLAR) LedCount ++;                            // Slower fading (Solar mode)
                else LedCount += 2;                                             // Faster fading (Smart mode)
                LedPwm = ease8InOutQuad(triwave8(LedCount));                    // pre calculate new LedPwm value
            }

            if (Mode == MODE_SOLAR) {                                           // Orange/Yellow for Solar mode
                RedPwm = LedPwm;
                GreenPwm = LedPwm * 2 / 3;
            } else {
                RedPwm = 0;                                                     // Green for Normal/Smart mode
                GreenPwm = LedPwm;
            }
            BluePwm = 0;            

        }
        ledcWrite(RED_CHANNEL, RedPwm);
        ledcWrite(GREEN_CHANNEL, GreenPwm);
        ledcWrite(BLUE_CHANNEL, BluePwm);

        // Pause the task for 10ms
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }  // while(1) loop 
}


// Set Charge Current 
// Current in Amps * 10 (160 = 16A)
void SetCurrent(uint16_t current) {
    uint32_t DutyCycle;

    if ((current >= 60) && (current <= 510)) DutyCycle = current / 0.6;
                                                                            // calculate DutyCycle from current
    else if ((current > 510) && (current <= 800)) DutyCycle = (current / 2.5) + 640;
    else DutyCycle = 100;                                                   // invalid, use 6A

    DutyCycle = DutyCycle * 1024 / 1000;                                    // conversion to 1024 = 100%
    ledcWrite(CP_CHANNEL, DutyCycle);                                       // update PWM signal
}


// Sample the Temperature sensor.
//
signed char TemperatureSensor() {
    uint32_t sample, voltage;
    signed char Temperature;

    RTC_ENTER_CRITICAL();
    // Sample Temperature Sensor
    sample = local_adc1_read(ADC1_CHANNEL_0);
    RTC_EXIT_CRITICAL();

    // voltage range is from 0-2200mV 
    voltage = esp_adc_cal_raw_to_voltage(sample, adc_chars_Temperature);

    // The MCP9700A temperature sensor outputs 500mV at 0C, and has a 10mV/C change in output voltage.
    // so 750mV is 25C, 400mV = -10C
    Temperature = (signed int)(voltage - 500)/10;
    //_Serialprintf("\nTemp: %i C (%u mV) ", Temperature , voltage);
    
    return Temperature;
}


// Sample the Proximity Pin, and determine the maximum current the cable can handle.
//
void ProximityPin() {
    uint32_t sample, voltage;

    RTC_ENTER_CRITICAL();
    // Sample Proximity Pilot (PP)
    sample = local_adc1_read(ADC1_CHANNEL_6);
    RTC_EXIT_CRITICAL();

    voltage = esp_adc_cal_raw_to_voltage(sample, adc_chars_PP);

    if (!Config) {                                                          // Configuration (0:Socket / 1:Fixed Cable)
        //socket
        _Serialprintf("PP pin: %u (%u mV)\n", sample, voltage);
    } else {
        //fixed cable
        _Serialprintf("PP pin: %u (%u mV) (warning: fixed cable configured so PP probably disconnected, making this reading void)\n", sample, voltage);
    }

    MaxCapacity = 13;                                                       // No resistor, Max cable current = 13A
    if ((voltage > 1200) && (voltage < 1400)) MaxCapacity = 16;             // Max cable current = 16A	680R -> should be around 1.3V
    if ((voltage > 500) && (voltage < 700)) MaxCapacity = 32;               // Max cable current = 32A	220R -> should be around 0.6V
    if ((voltage > 200) && (voltage < 400)) MaxCapacity = 63;               // Max cable current = 63A	100R -> should be around 0.3V

    if (Config) MaxCapacity = MaxCurrent;                                   // Override with MaxCurrent when Fixed Cable is used.
}


// Determine the state of the Pilot signal
//
uint8_t Pilot() {

    uint32_t sample, Min = 3300, Max = 0;
    uint32_t voltage;
    uint8_t n;

    // make sure we wait 100ms after each state change before calculating Average
    //if ( (StateTimer + 100) > millis() ) return PILOT_WAIT;

    // calculate Min/Max of last 25 CP measurements
    for (n=0 ; n<25 ;n++) {
        sample = ADCsamples[n];
        voltage = esp_adc_cal_raw_to_voltage( sample, adc_chars_CP);        // convert adc reading to voltage
        if (voltage < Min) Min = voltage;                                   // store lowest value
        if (voltage > Max) Max = voltage;                                   // store highest value
    }    
    //_Serialprintf("min:%u max:%u\n",Min ,Max);

    // test Min/Max against fixed levels
    if (Min > 3000 ) return PILOT_12V;                                      // Pilot at 12V (min 11.0V)
    if ((Min > 2700) && (Max < 2930)) return PILOT_9V;                      // Pilot at 9V
    if ((Min > 2400) && (Max < 2600)) return PILOT_6V;                      // Pilot at 6V
    if ((Min > 100) && (Max < 300)) return PILOT_DIODE;                     // Diode Check OK
    return PILOT_NOK;                                                       // Pilot NOT ok
}


/**
 * Get name of a state
 *
 * @param uint8_t State
 * @return uint8_t[] Name
 */
const char * getStateName(uint8_t StateCode) {
    if(StateCode < 11) return StrStateName[StateCode];
    else return "NOSTATE";
}


const char * getStateNameWeb(uint8_t StateCode) {
    if(StateCode < 11) return StrStateNameWeb[StateCode];
    else return "NOSTATE";    
}




uint8_t getErrorId(uint8_t ErrorCode) {
    uint8_t count = 0;
    //find the error bit that is set
    while (ErrorCode) {
        count++;
        ErrorCode = ErrorCode >> 1;
    }    
    return count;
}


const char * getErrorNameWeb(uint8_t ErrorCode) {
    uint8_t count = 0;
    const static char StrErrorNameWeb[9][20] = {"None", "No Power Available", "Communication Error", "Temperature High", "Unused", "RCM Tripped", "Waiting for Solar", "Test IO", "Flash Error"};
    count = getErrorId(ErrorCode);
    if(count < 9) return StrErrorNameWeb[count];
    else return "Multiple Errors";
}

/**
 * Set EVSE mode
 * 
 * @param uint8_t Mode
 */
void setMode(uint8_t NewMode) {
    if (LoadBl == 1) ModbusWriteSingleRequest(BROADCAST_ADR, 0x0003, NewMode);
    Mode = NewMode;
}

/**
 * Set the solar stop timer
 * 
 * @param unsigned int Timer (seconds)
 */
void setSolarStopTimer(uint16_t Timer) {
    if (LoadBl == 1 && SolarStopTimer != Timer) {
        ModbusWriteSingleRequest(BROADCAST_ADR, 0x0004, Timer);
    }
    SolarStopTimer = Timer;
}

void setState(uint8_t NewState) {
    setState(NewState, false);
}

void setState(uint8_t NewState, bool forceState) {

    if (State != NewState || forceState) {
        
        char Str[50];
        snprintf(Str, sizeof(Str), "#%02d:%02d:%02d STATE %s -> %s\n",timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, getStateName(State), getStateName(NewState) );

#ifdef LOG_DEBUG_EVSE
        // Log State change to webpage
        // ws.textAll(Str);    
#endif                
        _Serialprintf("%s",Str+1);
    }

    switch (NewState) {
        case STATE_B1:
            if (!ChargeDelay) ChargeDelay = 3;                                  // When entering State B1, wait at least 3 seconds before switching to another state.
            // fall through
        case STATE_A:                                                           // State A1
            CONTACTOR1_OFF;  
            CONTACTOR2_OFF;  
            ledcWrite(CP_CHANNEL, 1024);                                        // PWM off,  channel 0, duty cycle 100%
            timerAlarmWrite(timerA, PWM_100, true);                             // Alarm every 1ms, auto reload 
            if (NewState == STATE_A) {
                ErrorFlags &= ~NO_SUN;
                ErrorFlags &= ~LESS_6A;
                ChargeDelay = 0;
                // Reset Node
                Node[0].Timer = 0;
                Node[0].Phases = 0;
                Node[0].MinCurrent = 0;                                         // Clear ChargeDelay when disconnected.
            }
            break;
        case STATE_B:
            CONTACTOR1_OFF;
            CONTACTOR2_OFF;
            timerAlarmWrite(timerA, PWM_95, false);                             // Enable Timer alarm, set to diode test (95%)
            SetCurrent(ChargeCurrent);                                          // Enable PWM
            break;      
        case STATE_C:                                                           // State C2
            ActivationMode = 255;                                               // Disable ActivationMode
            CONTACTOR1_ON;      
            if(Mode == MODE_NORMAL && enable3f)                                 // Contactor1 ON
                CONTACTOR2_ON;                                                  // Contactor2 ON
            LCDTimer = 0;
            break;
        case STATE_C1:
            ledcWrite(CP_CHANNEL, 1024);                                        // PWM off,  channel 0, duty cycle 100%
            timerAlarmWrite(timerA, PWM_100, true);                             // Alarm every 1ms, auto reload 
                                                                                // EV should detect and stop charging within 3 seconds
            C1Timer = 6;                                                        // Wait maximum 6 seconds, before forcing the contactor off.
            ChargeDelay = 15;
            break;
        default:
            break;
    }
    
    BalancedState[0] = NewState;
    State = NewState;

    // BacklightTimer = BACKLIGHT;                                                 // Backlight ON
}

void setAccess(bool Access) {
    Access_bit = Access;
    if (Access == 0) {
        if (State == STATE_C) setState(STATE_C1);                               // Determine where to switch to.
        else if (State == STATE_B) setState(STATE_B1);
    }
}

/**
 * Returns the known battery charge rate if the data is not too old.
 * Returns 0 if data is too old.
 * A positive number means charging, a negative number means discharging --> this means the inverse must be used for calculations
 * 
 * Example:
 * homeBatteryCharge == 1000 --> Battery is charging using Solar
 * P1 = -500 --> Solar injection to the net but nut sufficient for charging
 * 
 * If the P1 value is added with the inverse battery charge it will inform the EVSE logic there is enough Solar --> -500 + -1000 = -1500
 * 
 * Note: The user who is posting battery charge data should take this into account, meaning: if he wants a minimum home battery (dis)charge rate he should substract this from the value he is sending.
 */
// 
int getBatteryCurrent(void) {
    int currentTime = time(NULL) - 60; // The data should not be older than 1 minute
    
    if (Mode == MODE_SOLAR && homeBatteryLastUpdate > (currentTime)) {
        return homeBatteryCurrent;
    } else {
        homeBatteryCurrent = 0;
        homeBatteryLastUpdate = 0;
        return 0;
    }
}


// Is there at least 6A(configurable MinCurrent) available for a new EVSE?
// Look whether there would be place for one more EVSE if we could lower them all down to MinCurrent
// returns 1 if there is 6A available
// returns 0 if there is no current available
//
char IsCurrentAvailable(void) {
    uint8_t n, ActiveEVSE = 0;
    int Baseload, Baseload_EV, TotalCurrent = 0;


    for (n = 0; n < NR_EVSES; n++) if (BalancedState[n] == STATE_C)             // must be in STATE_C
    {
        ActiveEVSE++;                                                           // Count nr of active (charging) EVSE's
        TotalCurrent += Balanced[n];                                            // Calculate total of all set charge currents
    }
    if (ActiveEVSE == 0) {                                                      // No active (charging) EVSE's
        if (Imeasured > ((MaxMains - MinCurrent) * 10)) {                       // There should be at least 6A available
            return 0;                                                           // Not enough current available!, return with error
        }
        if (Imeasured_EV > ((MaxCircuit - MinCurrent) * 10)) {                  // There should be at least 6A available
            return 0;                                                           // Not enough current available!, return with error
        }
    } else {                                                                    // at least one active EVSE
        ActiveEVSE++;                                                           // Do calculations with one more EVSE
        Baseload = Imeasured - TotalCurrent;                                    // Calculate Baseload (load without any active EVSE)
        Baseload_EV = Imeasured_EV - TotalCurrent;                              // Load on the EV subpanel excluding any active EVSE
        if (Baseload < 0) Baseload = 0;                                         // only relevant for Smart/Solar mode

        if (ActiveEVSE > NR_EVSES) ActiveEVSE = NR_EVSES;
        // When load balancing is active, and we are the Master, the Circuit option limits the max total current
        if (LoadBl == 1) {
            if ((ActiveEVSE * (MinCurrent * 10)) > (MaxCircuit * 10) - Baseload_EV) {
                return 0;                                                       // Not enough current available!, return with error
            }
        }

        // Check if the lowest charge current(6A) x ActiveEV's + baseload would be higher then the MaxMains.
        if ((ActiveEVSE * (MinCurrent * 10) + Baseload) > (MaxMains * 10)) {
            return 0;                                                           // Not enough current available!, return with error
        }
        if ((ActiveEVSE * (MinCurrent * 10) + Baseload) > (MaxCircuit * 10) - Baseload_EV) {
            return 0;                                                           // Not enough current available!, return with error
        }

    }

    // Allow solar Charging if surplus current is above 'StartCurrent' (sum of all phases)
    // Charging will start after the timeout (chargedelay) period has ended
     // Only when StartCurrent configured or Node MinCurrent detected or Node inactive
    if (Mode == MODE_SOLAR) {                                                   // no active EVSE yet?
        if (ActiveEVSE == 0 && Isum >= ((signed int)StartCurrent *-10)) return 0;
        else if ((ActiveEVSE * MinCurrent * 10) > TotalCurrent) return 0;       // check if we can split the available current between all active EVSE's
    }

    return 1;
}

void ResetBalancedStates(void) {
    uint8_t n;

    for (n = 1; n < NR_EVSES; n++) {
        BalancedState[n] = STATE_A;                                             // Yes, disable old active Node states
        Balanced[n] = 0;                                                        // reset ChargeCurrent to 0
    }
}

// Calculates Balanced PWM current for each EVSE
// mod =0 normal
// mod =1 we have a new EVSE requesting to start charging.
//
void CalcBalancedCurrent(char mod) {
    int Average, MaxBalanced, Idifference, Idifference2, Baseload_EV;
    int BalancedLeft = 0;
    signed int IsumImport;
    int ActiveMax = 0, TotalCurrent = 0, Baseload;
    char CurrentSet[NR_EVSES] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t n;
    int16_t IsetBalanced2 = 0;                                                  // Max calculated current (Amps *10) available for all EVSE's

    if (!LoadBl) ResetBalancedStates();                                         // Load balancing disabled?, Reset States
                                                                                // Do not modify MaxCurrent as it is a config setting. (fix 2.05)
    if (BalancedState[0] == STATE_C && MaxCurrent > MaxCapacity && !Config) ChargeCurrent = MaxCapacity * 10;
    else ChargeCurrent = MaxCurrent * 10;                                       // Instead use new variable ChargeCurrent.

    // Override current temporary if set (from Modbus)
    if (OverrideCurrent) ChargeCurrent = OverrideCurrent;

    if (LoadBl < 2) BalancedMax[0] = ChargeCurrent;                             // Load Balancing Disabled or Master:
                                                                                // update BalancedMax[0] if the MAX current was adjusted using buttons or CLI

    for (n = 0; n < NR_EVSES; n++) if (BalancedState[n] == STATE_C) {
            BalancedLeft++;                                                     // Count nr of Active (Charging) EVSE's
            ActiveMax += BalancedMax[n];                                        // Calculate total Max Amps for all active EVSEs
            TotalCurrent += Balanced[n];                                        // Calculate total of all set charge currents
        }

    if (!mod && Mode != MODE_SOLAR) {                                           // Normal and Smart mode
        Idifference = (MaxMains * 10) - Imeasured;                              // Difference between MaxMains and Measured current (can be negative)
        Idifference2 = (MaxCircuit * 10) - Imeasured_EV;                        // Difference between MaxCircuit and Measured EV current (can be negative)

        if (Idifference2 < Idifference) {
            Idifference = Idifference2;
        }
        if (Idifference > 0) IsetBalanced += (Idifference / 4);                 // increase with 1/4th of difference (slowly increase current)
        else IsetBalanced += (Idifference * 100 / TRANSFORMER_COMP);            // last PWM setting + difference (immediately decrease current)
        if (IsetBalanced < 0) IsetBalanced = 0;
        if (IsetBalanced > 800) IsetBalanced = 800;                             // hard limit 80A (added 11-11-2017)
    }



    if (Mode == MODE_SOLAR)                                                     // Solar version
    {
        IsumImport = Isum - (10 * ImportCurrent);                               // Allow Import of power from the grid when solar charging

        if (IsumImport < 0)                                                     
        {
            // negative, we have surplus (solar) power available
            if (IsumImport < -10) IsetBalanced = IsetBalanced + 5;              // more then 1A available, increase Balanced charge current with 0.5A
            else IsetBalanced = IsetBalanced + 1;                               // less then 1A available, increase with 0.1A
        } else {
            // positive, we use more power then is generated
            if (IsumImport > 20) IsetBalanced = IsetBalanced - (IsumImport / 2);// we use atleast 2A more then available, decrease Balanced charge current.
            else if (IsumImport > 10) IsetBalanced = IsetBalanced - 5;          // we use 1A more then available, decrease with 0.5A     
            else if (IsumImport > 3) IsetBalanced = IsetBalanced - 1;           // we still use > 0.3A more then available, decrease with 0.1A
                                                                                // if we use <= 0.3A we do nothing
        }
        
                                                                                // If IsetBalanced is below MinCurrent or negative, make sure it's set to MinCurrent.
        if ( (IsetBalanced < (BalancedLeft * MinCurrent * 10)) || (IsetBalanced < 0) ) {
            IsetBalanced = BalancedLeft * MinCurrent * 10;
                                                                                // ----------- Check to see if we have to continue charging on solar power alone ----------
            if (BalancedLeft && StopTime && (IsumImport > 10)) {
                if (SolarStopTimer == 0) setSolarStopTimer(StopTime * 60);      // Convert minutes into seconds
            } else {
                setSolarStopTimer(0);
            }
        } else {
            setSolarStopTimer(0);
        }
    }
                                                                                // When Load balancing = Master,  Limit total current of all EVSEs to MaxCircuit
    Baseload_EV = Imeasured_EV - TotalCurrent;                                        // Calculate Baseload (load without any active EVSE)
    if (Baseload_EV < 0) Baseload_EV = 0;
    if (LoadBl == 1 && (IsetBalanced > (MaxCircuit * 10) - Baseload_EV) ) IsetBalanced = MaxCircuit * 10 - Baseload_EV;


    Baseload = Imeasured - TotalCurrent;                                        // Calculate Baseload (load without any active EVSE)
    if (Baseload < 0) Baseload = 0;

    if (Mode == MODE_NORMAL)                                                    // Normal Mode
    {
        if (LoadBl == 1) IsetBalanced = MaxCircuit * 10 - Baseload_EV;          // Load Balancing = Master? MaxCircuit is max current for all active EVSE's
        else IsetBalanced = ChargeCurrent;                                      // No Load Balancing in Normal Mode. Set current to ChargeCurrent (fix: v2.05)
    }

    if (BalancedLeft)                                                           // Only if we have active EVSE's
    {
        // New EVSE charging, and no Solar mode
        if (mod && Mode != MODE_SOLAR) {                                        // Set max combined charge current to MaxMains - Baseload //TODO so now we ignore MaxCircuit? And all the other calculations we did before on IsetBalanced?
            IsetBalanced = (MaxMains * 10) - Baseload;
            IsetBalanced2 = (MaxCircuit * 10 ) - Baseload_EV;
            if (IsetBalanced2 < IsetBalanced) {
                IsetBalanced=IsetBalanced2;
            }
        }

        if (IsetBalanced < 0 || IsetBalanced < (BalancedLeft * MinCurrent * 10)
          || ( Mode == MODE_SOLAR && Isum > 10 && (Imeasured > (MaxMains * 10) || Imeasured_EV > (MaxCircuit * 10))))
                                                                                //TODO why Isum > 10 ? If Imeasured > MaxMains then Isum is always bigger than 1A, unless MaxMains is set at 0....
        {
            IsetBalanced = BalancedLeft * MinCurrent * 10;                      // set minimal "MinCurrent" charge per active EVSE
            NoCurrent++;                                                        // Flag NoCurrent left
#ifdef LOG_INFO_EVSE
            _Serialprintf("No Current!!\n");
#endif
        } else NoCurrent = 0;

        if (IsetBalanced > ActiveMax) IsetBalanced = ActiveMax;                 // limit to total maximum Amps (of all active EVSE's)

        MaxBalanced = IsetBalanced;                                             // convert to Amps

        // Calculate average current per EVSE
        n = 0;
        do {
            Average = MaxBalanced / BalancedLeft;                               // Average current for all active EVSE's

        // Check for EVSE's that have a lower MAX current
            if ((BalancedState[n] == STATE_C) && (!CurrentSet[n]) && (Average >= BalancedMax[n])) // Active EVSE, and current not yet calculated?
            {
                Balanced[n] = BalancedMax[n];                                   // Set current to Maximum allowed for this EVSE
                CurrentSet[n] = 1;                                              // mark this EVSE as set.
                BalancedLeft--;                                                 // decrease counter of active EVSE's
                MaxBalanced -= Balanced[n];                                     // Update total current to new (lower) value
                n = 0;                                                          // check all EVSE's again
            } else n++;
        } while (n < NR_EVSES && BalancedLeft);

        // All EVSE's which had a Max current lower then the average are set.
        // Now calculate the current for the EVSE's which had a higher Max current
        n = 0;
        if (BalancedLeft)                                                       // Any Active EVSE's left?
        {
            do {                                                                // Check for EVSE's that are not set yet
                if ((BalancedState[n] == STATE_C) && (!CurrentSet[n]))          // Active EVSE, and current not yet calculated?
                {
                    Balanced[n] = MaxBalanced / BalancedLeft;                   // Set current to Average
                    CurrentSet[n] = 1;                                          // mark this EVSE as set.
                    BalancedLeft--;                                             // decrease counter of active EVSE's
                    MaxBalanced -= Balanced[n];                                 // Update total current to new (lower) value
                }                                                               //TODO since the average has risen the other EVSE's should be checked for exceeding their MAX's too!
            } while (++n < NR_EVSES && BalancedLeft);
        }


    } // BalancedLeft

#ifdef LOG_DEBUG_EVSE
    char Str[128];
    char *cur = Str, * const end = Str + sizeof Str;
    if (LoadBl == 1) {
        for (n = 0; n < NR_EVSES; n++) {
            if (cur < end) cur += snprintf(cur, end-cur, "EVSE%u:%s(%u.%1uA),", n, getStateName(BalancedState[n]), Balanced[n]/10, Balanced[n]%10);
            else strcpy(end-sizeof("**truncated**"), "**truncated**");
        }
    _Serialprintf("Balance: %s\n", Str);
    }
#endif
}

/**
 * Broadcast momentary currents to all Node EVSE's
 */
void BroadcastCurrent(void) {
    ModbusWriteMultipleRequest(BROADCAST_ADR, 0x0020, Balanced, NR_EVSES);
}

/**
 * Master requests Node configuration over modbus
 * Master -> Node
 * 
 * @param uint8_t NodeNr (1-7)
 */
void requestNodeConfig(uint8_t NodeNr) {
    ModbusReadInputRequest(NodeNr + 1u, 4, 0x0108, 2);
}

/**
 * Master receives Node configuration over modbus
 * Node -> Master
 * 
 * @param uint8_t NodeNr (1-7)
 */
void receiveNodeConfig(uint8_t *buf, uint8_t NodeNr) {
    Node[NodeNr].EVMeter = buf[1];
    Node[NodeNr].EVAddress = buf[3];

    Node[NodeNr].ConfigChanged = 0;                                             // Reset flag on master
    ModbusWriteSingleRequest(NodeNr + 1u, 0x0006, 0);                           // Reset flag on node
}

/**
 * Master requests Node status over modbus
 * Master -> Node
 *
 * @param uint8_t NodeNr (1-7)
 */
void requestNodeStatus(uint8_t NodeNr) {
    Node[NodeNr].Online = false;
    ModbusReadInputRequest(NodeNr + 1u, 4, 0x0000, 8);
}

/**
 * Master receives Node status over modbus
 * Node -> Master
 *
 * @param uint8_t NodeAdr (1-7)
 */
void receiveNodeStatus(uint8_t *buf, uint8_t NodeNr) {
    if (LoadBl == 1) Node[NodeNr].Online = true;
    else Node[NodeNr].Online = false;
//    memcpy(buf, (uint8_t*)&Node[NodeNr], sizeof(struct NodeState));
    BalancedState[NodeNr] = buf[1];                                             // Node State
    BalancedError[NodeNr] = buf[3];                                             // Node Error status
    Node[NodeNr].ConfigChanged = buf[13] | Node[NodeNr].ConfigChanged;
    BalancedMax[NodeNr] = buf[15] * 10;                                         // Node Max ChargeCurrent (0.1A)
    //_Serialprintf("ReceivedNode[%u]Status State:%u Error:%u, BalancedMax:%u\n", NodeNr, BalancedState[NodeNr], BalancedError[NodeNr], BalancedMax[NodeNr] );
}

/**
 * Master checks node status requests, and responds with new state
 * Master -> Node
 *
 * @param uint8_t NodeAdr (1-7)
 */
void processAllNodeStates(uint8_t NodeNr) {
    uint16_t values[2];
    uint8_t current, write = 0;

    values[0] = BalancedState[NodeNr];

    current = IsCurrentAvailable();
    if (current) {                                                              // Yes enough current
        if (BalancedError[NodeNr] & (LESS_6A|NO_SUN)) {
            BalancedError[NodeNr] &= ~(LESS_6A | NO_SUN);                       // Clear Error flags
            write = 1;
        }
    }

    // Check EVSE for request to charge states
    switch (BalancedState[NodeNr]) {
        case STATE_A:
            // Reset Node
            Node[NodeNr].Timer = 0;
            Node[NodeNr].Phases = 0;
            Node[NodeNr].MinCurrent = 0;
            break;

        case STATE_COMM_B:                                                      // Request to charge A->B
#ifdef LOG_INFO_EVSE
            _Serialprintf("Node %u State A->B request ", NodeNr);
#endif
            if (current) {                                                      // check if we have enough current
                                                                                // Yes enough current..
                BalancedState[NodeNr] = STATE_B;                                // Mark Node EVSE as active (State B)
                Balanced[NodeNr] = MinCurrent * 10;                             // Initially set current to lowest setting
                values[0] = STATE_COMM_B_OK;
                write = 1;
#ifdef LOG_INFO_EVSE
                _Serialprintf("- OK!\n");
#endif
            } else {                                                            // We do not have enough current to start charging
                Balanced[NodeNr] = 0;                                           // Make sure the Node does not start charging by setting current to 0
                if ((BalancedError[NodeNr] & (LESS_6A|NO_SUN)) == 0) {          // Error flags cleared?
                    if (Mode == MODE_SOLAR) BalancedError[NodeNr] |= NO_SUN;    // Solar mode: No Solar Power available
                    else BalancedError[NodeNr] |= LESS_6A;                      // Normal or Smart Mode: Not enough current available
                    write = 1;
                }
#ifdef LOG_INFO_EVSE
                _Serialprintf("- Not enough current!\n");
#endif
            }
            break;

        case STATE_COMM_C:                                                      // request to charge B->C
#ifdef LOG_INFO_EVSE
            _Serialprintf("Node %u State B->C request\n", NodeNr);
#endif
            Balanced[NodeNr] = 0;                                               // For correct baseload calculation set current to zero
            if (current) {                                                      // check if we have enough current
                                                                                // Yes
                BalancedState[NodeNr] = STATE_C;                                // Mark Node EVSE as Charging (State C)
                CalcBalancedCurrent(1);                                         // Calculate charge current for all connected EVSE's
                values[0] = STATE_COMM_C_OK;
                write = 1;
#ifdef LOG_INFO_EVSE
                _Serialprintf("- OK!\n");
#endif
            } else {                                                            // We do not have enough current to start charging
                if ((BalancedError[NodeNr] & (LESS_6A|NO_SUN)) == 0) {          // Error flags cleared?
                    if (Mode == MODE_SOLAR) BalancedError[NodeNr] |= NO_SUN;    // Solar mode: No Solar Power available
                    else BalancedError[NodeNr] |= LESS_6A;                      // Normal or Smart Mode: Not enough current available
                    write = 1;
                }
#ifdef LOG_INFO_EVSE
                _Serialprintf("- Not enough current!\n");
#endif
            }
            break;

        default:
            break;

    }
    values[1] = BalancedError[NodeNr];

    if (write) {
#ifdef LOG_DEBUG_EVSE
        _Serialprintf("NodeAdr %u, BalancedError:%u\n",NodeNr, BalancedError[NodeNr]);
#endif
        ModbusWriteMultipleRequest(NodeNr+1 , 0x0000, values, 2);                 // Write State and Error to Node
    }

}


/**
 * Check minimum and maximum of a value and set the variable
 *
 * @param uint8_t MENU_xxx
 * @param uint16_t value
 * @return uint8_t success
 */
uint8_t setItemValue(uint8_t nav, uint16_t val) {
    if (nav < MENU_EXIT) {
        if (val < MenuStr[nav].Min || val > MenuStr[nav].Max) return 0;
    }

    switch (nav) {
        case MENU_MAX_TEMP:
            maxTemp = val;
            break;
        case MENU_3F:
            enable3f = val == 1;
            break;
        case MENU_CONFIG:
            Config = val;
            break;
        case STATUS_MODE:
            // Do not change Charge Mode when set to Normal or Load Balancing is disabled
            if (Mode == 0 || LoadBl == 0) break;
        case MENU_MODE:
            Mode = val;
            break;
        case MENU_START:
            StartCurrent = val;
            break;
        case MENU_STOP:
            StopTime = val;
            break;
        case MENU_IMPORT:
            ImportCurrent = val;
            break;
        case MENU_LOADBL:
            ConfigureModbusMode(val);
            LoadBl = val;
            break;
        case MENU_MAINS:
            MaxMains = val;
            break;
        case MENU_MIN:
            MinCurrent = val;
            break;
        case MENU_MAX:
            MaxCurrent = val;
            break;
        case MENU_CIRCUIT:
            MaxCircuit = val;
            break;
        case MENU_LOCK:
            Lock = val;
            break;
        case MENU_SWITCH:
            Switch = val;
            break;
        case MENU_RCMON:
            RCmon = val;
            break;
        case MENU_CAL:
            ICal = val;
            break;
        case MENU_GRID:
            Grid = val;
            break;
        case MENU_MAINSMETER:
            MainsMeter = val;
            break;
        case MENU_MAINSMETERADDRESS:
            MainsMeterAddress = val;
            break;
        case MENU_MAINSMETERMEASURE:
            MainsMeterMeasure = val;
            break;
        case MENU_PVMETER:
            PVMeter = val;
            break;
        case MENU_PVMETERADDRESS:
            PVMeterAddress = val;
            break;
        case MENU_EVMETER:
            EVMeter = val;
            break;
        case MENU_EVMETERADDRESS:
            EVMeterAddress = val;
            break;
        case MENU_EMCUSTOM_ENDIANESS:
            EMConfig[EM_CUSTOM].Endianness = val;
            break;
        case MENU_EMCUSTOM_DATATYPE:
            EMConfig[EM_CUSTOM].DataType = (mb_datatype)val;
            break;
        case MENU_EMCUSTOM_FUNCTION:
            EMConfig[EM_CUSTOM].Function = val;
            break;
        case MENU_EMCUSTOM_UREGISTER:
            EMConfig[EM_CUSTOM].URegister = val;
            break;
        case MENU_EMCUSTOM_UDIVISOR:
            EMConfig[EM_CUSTOM].UDivisor = val;
            break;
        case MENU_EMCUSTOM_IREGISTER:
            EMConfig[EM_CUSTOM].IRegister = val;
            break;
        case MENU_EMCUSTOM_IDIVISOR:
            EMConfig[EM_CUSTOM].IDivisor = val;
            break;
        case MENU_EMCUSTOM_PREGISTER:
            EMConfig[EM_CUSTOM].PRegister = val;
            break;
        case MENU_EMCUSTOM_PDIVISOR:
            EMConfig[EM_CUSTOM].PDivisor = val;
            break;
        case MENU_EMCUSTOM_EREGISTER:
            EMConfig[EM_CUSTOM].ERegister = val;
            break;
        case MENU_EMCUSTOM_EDIVISOR:
            EMConfig[EM_CUSTOM].EDivisor = val;
            break;
        case MENU_RFIDREADER:
            RFIDReader = val;
            break;
        case MENU_WIFI:
            WIFImode = val;
            break;    

        // Status writeable
        case STATUS_STATE:
            setState(val);
            break;
        case STATUS_ERROR:
            ErrorFlags = val;
            if (ErrorFlags) {                                                   // Is there an actual Error? Maybe the error got cleared?
                if (State == STATE_C) setState(STATE_C1);                       // tell EV to stop charging
                else setState(STATE_B1);                                        // when we are not charging switch to State B1
                ChargeDelay = CHARGEDELAY;
#ifdef LOG_DEBUG_MODBUS
                _Serialprintf("Broadcast Error message received!\n");
            } else {
                _Serialprintf("Broadcast Errors Cleared received!\n");
#endif
            }
            break;
        case STATUS_CURRENT:
            OverrideCurrent = val;
            timeout = 10;                                                       // reset timeout when register is written
            break;
        case STATUS_SOLAR_TIMER:
            setSolarStopTimer(val);
            break;
        case STATUS_ACCESS:
            if (val == 0 || val == 1) {
                setAccess(val);
            }
            break;
        case STATUS_CONFIG_CHANGED:
            ConfigChanged = val;
            break;

        default:
            return 0;
    }

    return 1;
}

/**
 * Get the variable
 *
 * @param uint8_t MENU_xxx
 * @return uint16_t value
 */
uint16_t getItemValue(uint8_t nav) {
    switch (nav) {
        case MENU_MAX_TEMP:
            return maxTemp;
        case MENU_3F:
            return enable3f;
        case MENU_CONFIG:
            return Config;
        case MENU_MODE:
        case STATUS_MODE:
            return Mode;
        case MENU_START:
            return StartCurrent;
        case MENU_STOP:
            return StopTime;
        case MENU_IMPORT:
            return ImportCurrent;
        case MENU_LOADBL:
            return LoadBl;
        case MENU_MAINS:
            return MaxMains;
        case MENU_MIN:
            return MinCurrent;
        case MENU_MAX:
            return MaxCurrent;
        case MENU_CIRCUIT:
            return MaxCircuit;
        case MENU_LOCK:
            return Lock;
        case MENU_SWITCH:
            return Switch;
        case MENU_RCMON:
            return RCmon;
        case MENU_CAL:
            return ICal;
        case MENU_GRID:
            return Grid;
        case MENU_MAINSMETER:
            return MainsMeter;
        case MENU_MAINSMETERADDRESS:
            return MainsMeterAddress;
        case MENU_MAINSMETERMEASURE:
            return MainsMeterMeasure;
        case MENU_PVMETER:
            return PVMeter;
        case MENU_PVMETERADDRESS:
            return PVMeterAddress;
        case MENU_EVMETER:
            return EVMeter;
        case MENU_EVMETERADDRESS:
            return EVMeterAddress;
        case MENU_EMCUSTOM_ENDIANESS:
            return EMConfig[EM_CUSTOM].Endianness;
        case MENU_EMCUSTOM_DATATYPE:
            return EMConfig[EM_CUSTOM].DataType;
        case MENU_EMCUSTOM_FUNCTION:
            return EMConfig[EM_CUSTOM].Function;
        case MENU_EMCUSTOM_UREGISTER:
            return EMConfig[EM_CUSTOM].URegister;
        case MENU_EMCUSTOM_UDIVISOR:
            return EMConfig[EM_CUSTOM].UDivisor;
        case MENU_EMCUSTOM_IREGISTER:
            return EMConfig[EM_CUSTOM].IRegister;
        case MENU_EMCUSTOM_IDIVISOR:
            return EMConfig[EM_CUSTOM].IDivisor;
        case MENU_EMCUSTOM_PREGISTER:
            return EMConfig[EM_CUSTOM].PRegister;
        case MENU_EMCUSTOM_PDIVISOR:
            return EMConfig[EM_CUSTOM].PDivisor;
        case MENU_EMCUSTOM_EREGISTER:
            return EMConfig[EM_CUSTOM].ERegister;
        case MENU_EMCUSTOM_EDIVISOR:
            return EMConfig[EM_CUSTOM].EDivisor;
        case MENU_RFIDREADER:
            return RFIDReader;
        case MENU_WIFI:
            return WIFImode;    

        // Status writeable
        case STATUS_STATE:
            return State;
        case STATUS_ERROR:
            return ErrorFlags;
        case STATUS_CURRENT:
            return Balanced[0];
        case STATUS_SOLAR_TIMER:
            return SolarStopTimer;
        case STATUS_ACCESS:
            return Access_bit;
        case STATUS_CONFIG_CHANGED:
            return ConfigChanged;

        // Status readonly
        case STATUS_MAX:
            return MaxCapacity;
        case STATUS_TEMP:
            return (signed int)TempEVSE + 273;
        case STATUS_SERIAL:
            return serialnr;

        default:
            return 0;
    }
}


/**
 * Update current data after received current measurement
 */
void UpdateCurrentData(void) {
    uint8_t x;

    // reset Imeasured value (grid power used)
    Imeasured = 0;
    Imeasured_EV = 0;
    for (x=0; x<3; x++) {
        // Imeasured holds highest Irms of all channels
        if (Irms[x] > Imeasured) Imeasured = Irms[x];
        if (Irms_EV[x] > Imeasured_EV) Imeasured_EV = Irms_EV[x];
    }


    // Load Balancing mode: Smart/Master or Disabled
    if (Mode && LoadBl < 2) {
        // Calculate dynamic charge current for connected EVSE's
        CalcBalancedCurrent(0);

        // No current left, or Overload (2x Maxmains)?
        if (NoCurrent > 2 || (Imeasured > (MaxMains * 20))) {
            // STOP charging for all EVSE's
            // Display error message
            ErrorFlags |= LESS_6A; //NOCURRENT;
            // Set all EVSE's to State A
            ResetBalancedStates();

            // Broadcast Error code over RS485
            ModbusWriteSingleRequest(BROADCAST_ADR, 0x0001, LESS_6A);
            NoCurrent = 0;
        } else if (LoadBl) BroadcastCurrent();                                  // Master sends current to all connected EVSE's

        if ((State == STATE_B) || (State == STATE_C)) {
            // Set current for Master EVSE in Smart Mode
            SetCurrent(Balanced[0]);
        }

#ifdef LOG_DEBUG_EVSE
        char Str[140];
        snprintf(Str, sizeof(Str) , "#STATE: %s Error: %u StartCurrent: -%i ChargeDelay: %u SolarStopTimer: %u NoCurrent: %u Imeasured: %.1f A IsetBalanced: %.1f A\n", getStateName(State), ErrorFlags, StartCurrent,
                                                                        ChargeDelay, SolarStopTimer,  NoCurrent,
                                                                        (float)Imeasured/10,
                                                                        (float)IsetBalanced/10);
        _Serialprintf("%s",Str+1);

        // Log to webpage
//        ws.textAll(Str);    

        _Serialprintf("L1: %.1f A L2: %.1f A L3: %.1f A Isum: %.1f A\n", (float)Irms[0]/10, (float)Irms[1]/10, (float)Irms[2]/10, (float)Isum/10);
#endif

    } else Imeasured = 0; // In case Sensorbox is connected in Normal mode. Clear measurement.
}


// CheckSwitch (SW input)
//
void CheckSwitch(void)
{
    static uint8_t RB2count = 0, RB2last = 1, RB2low = 0;
    static unsigned long RB2Timer = 0;                                                 // 1500ms

    // External switch changed state?
    if ( (digitalRead(PIN_SW_IN) != RB2last) || RB2low) {
        // make sure that noise on the input does not switch
        if (RB2count++ > 5 || RB2low) {
            RB2last = digitalRead(PIN_SW_IN);
            if (RB2last == 0) {
                // Switch input pulled low
                switch (Switch) {
                    case 1: // Access Button
                        setAccess(!Access_bit);                             // Toggle Access bit on/off
#ifdef LOG_DEBUG_EVSE
                        _Serialprintf("Access: %d\n", Access_bit);
#endif
                        break;
                    case 2: // Access Switch
                        setAccess(true);
                        break;
                    case 3: // Smart-Solar Button or hold button for 1,5 second to STOP charging
                        if (RB2low == 0) {
                            RB2low = 1;
                            RB2Timer = millis();
                        }
                        if (RB2low && millis() > RB2Timer + 1500) {
                            if (State == STATE_C) {
                                setState(STATE_C1);
                                if (!TestState) ChargeDelay = 15;           // Keep in State B for 15 seconds, so the Charge cable can be removed.
                            RB2low = 2;
                            }
                        }
                        break;
                    case 4: // Smart-Solar Switch
                        if (Mode == MODE_SOLAR) {
                            setMode(MODE_SMART);
                            setSolarStopTimer(0);                           // Also make sure the SolarTimer is disabled.
                        }
                        break;
                    default:
                        if (State == STATE_C) {                             // Menu option Access is set to Disabled
                            setState(STATE_C1);
                            if (!TestState) ChargeDelay = 15;               // Keep in State B for 15 seconds, so the Charge cable can be removed.
                        }
                        break;
                }

                // Reset RCM error when button is pressed
                // RCM was tripped, but RCM level is back to normal
                if (RCmon == 1 && (ErrorFlags & RCM_TRIPPED) && digitalRead(PIN_RCM_FAULT) == LOW) {
                    // Clear RCM error
                    ErrorFlags &= ~RCM_TRIPPED;
                }
                // Also light up the LCD backlight
                // BacklightTimer = BACKLIGHT;                                 // Backlight ON

            } else {
                // Switch input released
                switch (Switch) {
                    case 2: // Access Switch
                        setAccess(false);
                        break;
                    case 3: // Smart-Solar Button
                        if (RB2low != 2) {
                            if (Mode == MODE_SMART) {
                                setMode(MODE_SOLAR);
                            } else if (Mode == MODE_SOLAR) {
                                setMode(MODE_SMART);
                            }
                            ErrorFlags &= ~(NO_SUN | LESS_6A);                   // Clear All errors
                            ChargeDelay = 0;                                // Clear any Chargedelay
                            setSolarStopTimer(0);                           // Also make sure the SolarTimer is disabled.
                            LCDTimer = 0;
                        }
                        RB2low = 0;
                        break;
                    case 4: // Smart-Solar Switch
                        if (Mode == MODE_SMART) setMode(MODE_SOLAR);
                        break;
                    default:
                        break;
                }
            }

            RB2count = 0;
        }
    } else RB2count = 0;

    // Residual current monitor active, and DC current > 6mA ?
    if (RCmon == 1 && digitalRead(PIN_RCM_FAULT) == HIGH) {                   
        delay(1);
        // check again, to prevent voltage spikes from tripping the RCM detection
        if (digitalRead(PIN_RCM_FAULT) == HIGH) {                           
            if (State) setState(STATE_B1);
            ErrorFlags = RCM_TRIPPED;
            LCDTimer = 0;                                                   // display the correct error message on the LCD
        }
    }


    // One RFID card can Lock/Unlock the charging socket (like a public charging station)
    if (RFIDReader == 2) {
        if (Access_bit == 0) UnlockCable = 1; 
        else UnlockCable = 0;
    // The charging socket is unlocked when charging stops.
    } else {
        if (State != STATE_C) UnlockCable = 1;
        else UnlockCable = 0;
    } 
    // If the cable is connected to the EV, the cable will be locked.
   if (State == STATE_B || State == STATE_C) LockCable = 1;
   else LockCable = 0;
    

}



// Task that handles EVSE State Changes
// Reads buttons, and updates the LCD.
//
// called every 10ms
void EVSEStates(void * parameter) {

  //uint8_t n;
    uint8_t leftbutton = 5;
    uint8_t DiodeCheck = 0; 
   

    // infinite loop
    while(1) { 
    
        
        // Sample the three < o > buttons.
        // As the buttons are shared with the SPI lines going to the LCD,
        // we have to make sure that this does not interfere by write actions to the LCD.
        // Therefore updating the LCD is also done in this task.

        pinMatrixOutDetach(PIN_LCD_SDO_B3, false, false);       // disconnect MOSI pin
        pinMode(PIN_LCD_SDO_B3, INPUT);
        pinMode(PIN_LCD_A0_B2, INPUT);
        // sample buttons                                       < o >
        if (digitalRead(PIN_LCD_SDO_B3)) ButtonState = 4;       // > (right)
        else ButtonState = 0;
        if (digitalRead(PIN_LCD_A0_B2)) ButtonState |= 2;       // o (middle)
        if (digitalRead(PIN_IO0_B1)) ButtonState |= 1;          // < (left)

        pinMode(PIN_LCD_SDO_B3, OUTPUT);
        pinMatrixOutAttach(PIN_LCD_SDO_B3, VSPID_IN_IDX, false, false); // re-attach MOSI pin
        pinMode(PIN_LCD_A0_B2, OUTPUT);


        // When one or more button(s) are pressed, we call GLCDMenu
        if ((ButtonState != 0x07) || (ButtonState != OldButtonState)) GLCDMenu(ButtonState);

        // Update/Show Helpmenu
        if (LCDNav > MENU_ENTER && LCDNav < MENU_EXIT && (ScrollTimer + 5000 < millis() ) && (!SubMenu)) GLCDHelp();

        // Left button pressed, Loadbalancing is Master or Disabled, switch is set to "Sma-Sol B" and Mode is Smart or Solar?
        if (!LCDNav && ButtonState == 0x6 && Mode && !leftbutton && (LoadBl < 2) && Switch == 3) {
            setMode(~Mode & 0x3);                                           // Change from Solar to Smart mode and vice versa.
            ErrorFlags &= ~(NO_SUN | LESS_6A);                              // Clear All errors
            ChargeDelay = 0;                                                // Clear any Chargedelay
            setSolarStopTimer(0);                                           // Also make sure the SolarTimer is disabled.
            LCDTimer = 0;
            leftbutton = 5;
        } else if (leftbutton && ButtonState == 0x7) leftbutton--;

        // Check the external switch and RCM sensor
        CheckSwitch();

        // sample the Pilot line
        pilot = Pilot();

        // ############### EVSE State A #################

        if (State == STATE_A || State == STATE_COMM_B || State == STATE_B1)
        {

            if (pilot == PILOT_12V) {                                           // Check if we are disconnected, or forced to State A, but still connected to the EV

                // If the RFID reader is set to EnableOne mode, and the Charging cable is disconnected
                // We start a timer to re-lock the EVSE (and unlock the cable) after 60 seconds.
                if (RFIDReader == 2 && AccessTimer == 0 && Access_bit == 1) AccessTimer = RFIDLOCKTIME;

                if (State != STATE_A) setState(STATE_A);                        // reset state, incase we were stuck in STATE_COMM_B
                ChargeDelay = 0;                                                // Clear ChargeDelay when disconnected.

                if (!ResetKwh) ResetKwh = 1;                                    // when set, reset EV kWh meter on state B->C change.
            } else if ( pilot == PILOT_9V && ErrorFlags == NO_ERROR 
                && ChargeDelay == 0 && Access_bit
                && State != STATE_COMM_B) {                                     // switch to State B ?
                                                                                // Allow to switch to state C directly if STATE_A_TO_C is set to PILOT_6V (see EVSE.h)
                DiodeCheck = 0;

                ProximityPin();                                                 // Sample Proximity Pin

#ifdef LOG_DEBUG_EVSE
                _Serialprintf("Cable limit: %uA  Max: %uA\n", MaxCapacity, MaxCurrent);
#endif
                if (MaxCurrent > MaxCapacity) ChargeCurrent = MaxCapacity * 10; // Do not modify Max Cable Capacity or MaxCurrent (fix 2.05)
                else ChargeCurrent = MaxCurrent * 10;                           // Instead use new variable ChargeCurrent

                // Load Balancing : Node
                if (LoadBl > 1) {                                               // Send command to Master, followed by Max Charge Current
                    setState(STATE_COMM_B);                                     // Node wants to switch to State B

                // Load Balancing: Master or Disabled
                } else if (IsCurrentAvailable()) {                             
                    BalancedMax[0] = MaxCapacity * 10;
                    Balanced[0] = ChargeCurrent;                                // Set pilot duty cycle to ChargeCurrent (v2.15)
                    setState(STATE_B);                                          // switch to State B
                    ActivationMode = 30;                                        // Activation mode is triggered if state C is not entered in 30 seconds.
                    AccessTimer = 0;
                } else if (Mode == MODE_SOLAR) {                                // Not enough power:
                    ErrorFlags |= NO_SUN;                                       // Not enough solar power
                } else ErrorFlags |= LESS_6A;                                   // Not enough power available
            }
        }

        if (State == STATE_COMM_B_OK) {
            setState(STATE_B);
            ActivationMode = 30;                                                // Activation mode is triggered if state C is not entered in 30 seconds.
            AccessTimer = 0;
        }

        // ############### EVSE State B #################
        
        if (State == STATE_B || State == STATE_COMM_C) {
        
            if (pilot == PILOT_12V) {                                           // Disconnected?
                setState(STATE_A);                                              // switch to STATE_A

            } else if (pilot == PILOT_6V) {

                if ((DiodeCheck == 1) && (ErrorFlags == NO_ERROR) && (ChargeDelay == 0)) {
                    if (EVMeter && ResetKwh) {
                        EnergyMeterStart = EnergyEV;                            // store kwh measurement at start of charging.
                        ResetKwh = 0;                                           // clear flag, will be set when disconnected from EVSE (State A)
                    }

                    // Load Balancing : Node
                    if (LoadBl > 1) {                                           
                        if (State != STATE_COMM_C) setState(STATE_COMM_C);      // Send command to Master, followed by Charge Current

                    // Load Balancing: Master or Disabled
                    } else { 
                        BalancedMax[0] = ChargeCurrent;
                        if (IsCurrentAvailable()) {

                            Balanced[0] = 0;                                    // For correct baseload calculation set current to zero
                            CalcBalancedCurrent(1);                             // Calculate charge current for all connected EVSE's

                            DiodeCheck = 0;                                     // (local variable)
                            setState(STATE_C);                                  // switch to STATE_C
                            if (!LCDNav) GLCD();                                // Don't update the LCD if we are navigating the menu
                                                                                // immediately update LCD (20ms)
                        }
                        else if (Mode == MODE_SOLAR) {                          // Not enough power:
                            ErrorFlags |= NO_SUN;                               // Not enough solar power
                        } else ErrorFlags |= LESS_6A;                           // Not enough power available
                    }
                }

            // PILOT_9V
            } else {

                if (ActivationMode == 0) {
                    setState(STATE_ACTSTART);
                    ActivationTimer = 3;

                    ledcWrite(CP_CHANNEL, 0);                                   // PWM off,  channel 0, duty cycle 0%
                                                                                // Control pilot static -12V
                }
            }
            if (pilot == PILOT_DIODE) {
                DiodeCheck = 1;                                                 // Diode found, OK
                _Serialprintf("Diode OK\n");
                timerAlarmWrite(timerA, PWM_5, false);                          // Enable Timer alarm, set to start of CP signal (5%)
            }    

        }

        // ############### EVSE State C1 #################

        if (State == STATE_C1)
        {
            if (pilot == PILOT_12V) 
            {                                                                   // Disconnected or connected to EV without PWM
                setState(STATE_A);                                              // switch to STATE_A
                GLCD_init();                                                    // Re-init LCD
            }
            else if (pilot == PILOT_9V)
            {
                setState(STATE_B1);                                             // switch to State B1
                GLCD_init();                                                    // Re-init LCD
            }
        }


        if (State == STATE_ACTSTART && ActivationTimer == 0) {
            setState(STATE_B);                                                  // Switch back to State B
            ActivationMode = 255;                                               // Disable ActivationMode
        }

        if (State == STATE_COMM_C_OK) {
            DiodeCheck = 0;
            setState(STATE_C);                                                  // switch to STATE_C
                                                                                // Don't update the LCD if we are navigating the menu
            if (!LCDNav) GLCD();                                                // immediately update LCD
        }

        // ############### EVSE State C #################

        if (State == STATE_C) {
        
            if (pilot == PILOT_12V) {                                           // Disconnected ?
                setState(STATE_A);                                              // switch back to STATE_A
                GLCD_init();                                                    // Re-init LCD
    
            } else if (pilot == PILOT_9V) {
                setState(STATE_B);                                              // switch back to STATE_B
                DiodeCheck = 0;
                GLCD_init();                                                    // Re-init LCD (200ms delay)
                                                                                // Mark EVSE as inactive (still State B)
            }  
    
        } // end of State C code

        // update LCD (every 1000ms) when not in the setup menu
        if (LCDupdate) {
            // This is also the ideal place for debug messages that should not be printed every 10ms
            //_Serialprintf("States task free ram: %u\n", uxTaskGetStackHighWaterMark( NULL ));
            GLCD();
            LCDupdate = 0;
        }    

        if ((ErrorFlags & CT_NOCOMM) && timeout == 10) ErrorFlags &= ~CT_NOCOMM;          // Clear communication error, if present
        
        // Pause the task for 10ms
        vTaskDelay(10 / portTICK_PERIOD_MS);
    } // while(1) loop
}

/**
 * Send Energy measurement request over modbus
 *
 * @param uint8_t Meter
 * @param uint8_t Address
 * @param bool    Export (if exported energy is requested)
 */
void requestEnergyMeasurement(uint8_t Meter, uint8_t Address, bool Export) {
   switch (Meter) {
        case EM_SOLAREDGE:
            // Note:
            // - SolarEdge uses 16-bit values, except for this measurement it uses 32bit int format
            // - EM_SOLAREDGE should not be used for EV Energy Measurements
            if (Export) ModbusReadInputRequest(Address, EMConfig[Meter].Function, EMConfig[Meter].ERegister_Exp, 2);
            else        ModbusReadInputRequest(Address, EMConfig[Meter].Function, EMConfig[Meter].ERegister, 2);
            break;
        case EM_FINDER:
        case EM_ABB:
        case EM_EASTRON:
        case EM_WAGO:
            if (Export) requestMeasurement(Meter, Address, EMConfig[Meter].ERegister_Exp, 1);
            else        requestMeasurement(Meter, Address, EMConfig[Meter].ERegister, 1);
            break;
        case EM_EASTRON_INV:
            if (Export) requestMeasurement(Meter, Address, EMConfig[Meter].ERegister, 1);
            else        requestMeasurement(Meter, Address, EMConfig[Meter].ERegister_Exp, 1);
            break;
        default:
            if (!Export) //refuse to do a request on exported energy if the meter doesnt support it
                requestMeasurement(Meter, Address, EMConfig[Meter].ERegister, 1);
            break;
    }
}


// Task that handles the Cable Lock and modbus
// 
// called every 100ms
//
void Timer100ms(void * parameter) {

unsigned int locktimer = 0, unlocktimer = 0, energytimer = 0;
uint8_t PollEVNode = NR_EVSES;


    while(1)  // infinite loop
    {
        // Check if the cable lock is used
        if (Lock) {                                                 // Cable lock enabled?

            // UnlockCable takes precedence over LockCable
            if (UnlockCable) {
                if (unlocktimer < 6) {                              // 600ms pulse
                    ACTUATOR_UNLOCK;
                } else ACTUATOR_OFF;
                if (unlocktimer++ > 7) {
                    if (digitalRead(PIN_LOCK_IN) == lock1 )         // still locked...
                    {
                        if (unlocktimer > 50) unlocktimer = 0;      // try to unlock again in 5 seconds
                    } else unlocktimer = 7;
                }
                locktimer = 0;
            // Lock Cable    
            } else if (LockCable) { 
                if (locktimer < 6) {                                // 600ms pulse
                    ACTUATOR_LOCK;
                } else ACTUATOR_OFF;
                if (locktimer++ > 7) {
                    if (digitalRead(PIN_LOCK_IN) == lock2 )         // still unlocked...
                    {
                        if (locktimer > 50) locktimer = 0;          // try to lock again in 5 seconds
                    } else locktimer = 7;
                }
                unlocktimer = 0;
            }
        }

       

        // Every 2 seconds, request measurements from modbus meters
        if (ModbusRequest) {
            switch (ModbusRequest++) {                                          // State
                case 1:                                                         // PV kwh meter
                    if (PVMeter) {
                        requestCurrentMeasurement(PVMeter, PVMeterAddress);
                        break;
                    }
                    ModbusRequest++;
                case 2:                                                         // Sensorbox or kWh meter that measures -all- currents
#ifdef LOG_INFO_MODBUS
                    _Serialprintf("ModbusRequest %u: Request MainsMeter Measurement\n", ModbusRequest);
#endif
                    requestCurrentMeasurement(MainsMeter, MainsMeterAddress);
                    break;
                case 3:
                    // Find next online SmartEVSE
                    do {
                        PollEVNode++;
                        if (PollEVNode >= NR_EVSES) PollEVNode = 0;
                    } while(Node[PollEVNode].Online == false);

                    // Request Configuration if changed
                    if (Node[PollEVNode].ConfigChanged) {
#ifdef LOG_INFO_MODBUS
                        _Serialprintf("ModbusRequest %u: Request Configuration Node %u\n", ModbusRequest, PollEVNode);
#endif
                        requestNodeConfig(PollEVNode);
                        break;
                    }
                    ModbusRequest++;
                case 4:                                                         // EV kWh meter, Energy measurement (total charged kWh)
                    // Request Energy if EV meter is configured
                    if (Node[PollEVNode].EVMeter) {
#ifdef LOG_INFO_MODBUS
                        _Serialprintf("ModbusRequest %u: Request Energy Node %u\n", ModbusRequest, PollEVNode);
#endif
                        requestEnergyMeasurement(Node[PollEVNode].EVMeter, Node[PollEVNode].EVAddress, 0);
                        break;
                    }
                    ModbusRequest++;
                case 5:                                                         // EV kWh meter, Power measurement (momentary power in Watt)
                    // Request Power if EV meter is configured
                    if (Node[PollEVNode].EVMeter) {
                        requestMeasurement(Node[PollEVNode].EVMeter, Node[PollEVNode].EVAddress,EMConfig[Node[PollEVNode].EVMeter].PRegister, 1);
                        break;
                    }
                    ModbusRequest++;
                case 6:                                                         // Node 1
                case 7:
                case 8:
                case 9:
                case 10:
                case 11:
                case 12:
                    if (LoadBl == 1) {
                        requestNodeStatus(ModbusRequest - 6u);                   // Master, Request Node 1-8 status
                        break;
                    }
                    ModbusRequest = 12;
                case 13:
                case 14:
                case 15:
                case 16:
                case 17:
                case 18:
                case 19:
                    if (LoadBl == 1) {
                        processAllNodeStates(ModbusRequest - 13u);
                        break;
                    }
                    ModbusRequest = 21;
                case 20:                                                         // EV kWh meter, Current measurement
                    // Request Current if EV meter is configured
                    if (EVMeter) {
#ifdef LOG_INFO_MODBUS
                        _Serialprintf("ModbusRequest %u: Request EVMeter Current Measurement\n", ModbusRequest);
#endif
                        requestCurrentMeasurement(EVMeter, EVMeterAddress);
                        break;
                    }
                    ModbusRequest++;
                case 21:
                    // Request active energy if Mainsmeter is configured
                    if (MainsMeter ) {
                        energytimer++; //this ticks approx every second?!?
                        if (energytimer == 30) {
#ifdef LOG_INFO_MODBUS
                            _Serialprintf("ModbusRequest %u: Request MainsMeter Import Active Energy Measurement\n", ModbusRequest);
#endif
                            requestEnergyMeasurement(MainsMeter, MainsMeterAddress, 0);
                            break;
                        }
                        if (energytimer >= 60) {
#ifdef LOG_INFO_MODBUS
                            _Serialprintf("ModbusRequest %u: Request MainsMeter Export Active Energy Measurement\n", ModbusRequest);
#endif
                            requestEnergyMeasurement(MainsMeter, MainsMeterAddress, 1);
                            energytimer = 0;
                            break;
                        }
                    }
                    ModbusRequest++;
                default:
                    if (Mode) {                                                 // Smart/Solar mode
                        if ((ErrorFlags & CT_NOCOMM) == 0) UpdateCurrentData();      // No communication error with Sensorbox /Kwh meter?
                                                                                // then update the data and send broadcast to all connected EVSE's
                    } else {                                                    // Normal Mode
                        CalcBalancedCurrent(0);                                 // Calculate charge current for connected EVSE's
                        if (LoadBl == 1) BroadcastCurrent();                    // Send to all EVSE's (only in Master mode)
                        if ((State == STATE_B) || (State == STATE_C)) SetCurrent(Balanced[0]); // set PWM output for Master
                    }
                    ModbusRequest = 0;
                    //_Serialprintf("Task free ram: %u\n", uxTaskGetStackHighWaterMark( NULL ));
                    break;
            } //switch
        }


        // Pause the task for 100ms
        vTaskDelay(100 / portTICK_PERIOD_MS);

    } //while(1) loop

}


// task 1000msTimer
void Timer1S(void * parameter) {

    uint8_t Broadcast = 1;
    //uint8_t Timer5sec = 0;
    uint8_t x;


    while(1) { // infinite loop

        if (homeBatteryLastUpdate != 0 && homeBatteryLastUpdate < (time(NULL) - 60)) {
            homeBatteryCurrent = 0;
            homeBatteryLastUpdate = 0;
        }

        if (BacklightTimer) BacklightTimer--;                               // Decrease backlight counter every second.

        // wait for Activation mode to start
        if (ActivationMode && ActivationMode != 255) {
            ActivationMode--;                                               // Decrease ActivationMode every second.
        }

        // activation Mode is active
        if (ActivationTimer) ActivationTimer--;                             // Decrease ActivationTimer every second.
        
        if (State == STATE_C1) {
            if (C1Timer) C1Timer--;                                         // if the EV does not stop charging in 6 seconds, we will open the contactor.
            else {
                _Serialprintf("State C1 timeout!\n");
                setState(STATE_B1);                                         // switch back to STATE_B1
                GLCD_init();                                                // Re-init LCD (200ms delay)
            }
        }

        // once a second, measure temperature
        // range -40 .. +125C
        TempEVSE = TemperatureSensor();                                                             


        // Check if there is a RFID card in front of the reader
        CheckRFID();

                 
        // When Solar Charging, once the current drops to MINcurrent a timer is started.
        // Charging is stopped when the timer reaches the time set in 'StopTime' (in minutes)
        // Except when Stoptime =0, then charging will continue.

        if (SolarStopTimer) {
            SolarStopTimer--;
            if (SolarStopTimer == 0) {

                if (State == STATE_C) setState(STATE_C1);                   // tell EV to stop charging
                ErrorFlags |= NO_SUN;                                       // Set error: NO_SUN

                ResetBalancedStates();                                      // reset all states
            }
        }

        if (ChargeDelay) ChargeDelay--;                                     // Decrease Charge Delay counter

        if (AccessTimer && State == STATE_A) {
            if (--AccessTimer == 0) {
                setAccess(false);                                           // re-lock EVSE
            }
        } else AccessTimer = 0;                                             // Not in state A, then disable timer

        if ((TempEVSE < (maxTemp - 10)) && (ErrorFlags & TEMP_HIGH)) {                  // Temperature below limit?
            ErrorFlags &= ~TEMP_HIGH; // clear Error
        }

        if ( (ErrorFlags & (LESS_6A|NO_SUN) ) && (LoadBl < 2) && (IsCurrentAvailable())) {
            ErrorFlags &= ~LESS_6A;                                         // Clear Errors if there is enough current available, and Load Balancing is disabled or we are Master
            ErrorFlags &= ~NO_SUN;
#ifdef LOG_DEBUG_EVSE
            _Serialprintf("No sun/current Errors Cleared.\n");
#endif
            ModbusWriteSingleRequest(BROADCAST_ADR, 0x0001, ErrorFlags);    // Broadcast
        }

        if (ExternalMaster) {
            ExternalMaster--;
        }

        // Charge timer
        for (x = 0; x < NR_EVSES; x++) {
            if (BalancedState[x] == STATE_C) Node[x].Timer++;
        }

        if ( (timeout == 0) && !(ErrorFlags & CT_NOCOMM)) { // timeout if current measurement takes > 10 secs
            ErrorFlags |= CT_NOCOMM;
            if (State == STATE_C) setState(STATE_C1);                       // tell EV to stop charging
            else setState(STATE_B1);                                        // when we are not charging switch to State B1
#ifdef LOG_WARN_EVSE
            _Serialprintf("Error, communication error!\n");
#endif
            // Try to broadcast communication error to Nodes if we are Master
            if (LoadBl < 2) ModbusWriteSingleRequest(BROADCAST_ADR, 0x0001, ErrorFlags);         
            ResetBalancedStates();
        } else if (timeout) timeout--;

        if (TempEVSE > maxTemp && !(ErrorFlags & TEMP_HIGH))                         // Temperature too High?
        {
            ErrorFlags |= TEMP_HIGH;
            setState(STATE_A);                                              // ERROR, switch back to STATE_A
#ifdef LOG_WARN_EVSE
            _Serialprintf("Error, temperature %i C !\n", TempEVSE);
#endif
            ResetBalancedStates();
        }

        if (ErrorFlags & (NO_SUN | LESS_6A)) {
#ifdef LOG_INFO_EVSE
            if (Mode == MODE_SOLAR) {
                if (ChargeDelay == 0) _Serialprintf("Waiting for Solar power...\n");
            } else {
                if (ChargeDelay == 0) _Serialprintf("Not enough current available!\n");
            }
#endif
            if (State == STATE_C) setState(STATE_C1);                       // If we are charging, tell EV to stop charging
            else if (State != STATE_C1) setState(STATE_B1);                 // If we are not in State C1, switch to State B1
            ChargeDelay = CHARGEDELAY;                                      // Set Chargedelay
        }

        // set flag to update the LCD once every second
        LCDupdate = 1;

        // Every two seconds request measurement data from sensorbox/kwh meters.
        // and send broadcast to Node controllers.
        if (LoadBl < 2 && !ExternalMaster && !Broadcast--) {                // Load Balancing mode: Master or Disabled
            if (Mode) {                                                     // Smart or Solar mode
                ModbusRequest = 1;                                          // Start with state 1
            } else {                                                        // Normal mode
                Imeasured = 0;                                              // No measurements, so we set it to zero
                ModbusRequest = 6;                                          // Start with state 5 (poll Nodes)
                timeout = 10;                                               // reset timeout counter (not checked for Master)
            }
            Broadcast = 1;                                                  // repeat every two seconds
        }

          

        // this will run every 5 seconds
        // if (Timer5sec++ >= 5) {
        //     // Connected to WiFi?
        //     if (WiFi.status() == WL_CONNECTED) {
        //         ws.printfAll("T:%d",TempEVSE);                              // Send internal temperature to clients 
        //         ws.printfAll("S:%s",getStateNameWeb(State));
        //         ws.printfAll("E:%s",getErrorNameWeb(ErrorFlags));
        //         ws.printfAll("C:%2.1f",(float)Balanced[0]/10);
        //         ws.printfAll("I:%3.1f,%3.1f,%3.1f",(float)Irms[0]/10,(float)Irms[1]/10,(float)Irms[2]/10);
        //         ws.printfAll("R:%u", esp_reset_reason() );

        //         ws.cleanupClients();                                        // Cleanup old websocket clients
        //     } 
        //     Timer5sec = 0;
        // }

        //_Serialprintf("Task 1s free ram: %u\n", uxTaskGetStackHighWaterMark( NULL ));


        // Pause the task for 1 Sec
        vTaskDelay(1000 / portTICK_PERIOD_MS);

    } // while(1)
}


/**
 * Read energy measurement from modbus
 *
 * @param pointer to buf
 * @param uint8_t Meter
 * @return signed int Energy (Wh)
 */
signed int receiveEnergyMeasurement(uint8_t *buf, uint8_t Meter) {
    switch (Meter) {
        case EM_SOLAREDGE:
            // Note:
            // - SolarEdge uses 16-bit values, except for this measurement it uses 32bit int format
            // - EM_SOLAREDGE should not be used for EV Energy Measurements
            return receiveMeasurement(buf, 0, EMConfig[Meter].Endianness, MB_DATATYPE_INT32, EMConfig[Meter].EDivisor - 3);
        default:
            return receiveMeasurement(buf, 0, EMConfig[Meter].Endianness, EMConfig[Meter].DataType, EMConfig[Meter].EDivisor - 3);
    }
}

/**
 * Read Power measurement from modbus
 *
 * @param pointer to buf
 * @param uint8_t Meter
 * @return signed int Power (W)
  */
signed int receivePowerMeasurement(uint8_t *buf, uint8_t Meter) {
    switch (Meter) {
        case EM_SOLAREDGE:
        {
            // Note:
            // - SolarEdge uses 16-bit values, with a extra 16-bit scaling factor
            // - EM_SOLAREDGE should not be used for EV power measurements, only PV power measurements are supported
            int scalingFactor = -(int)receiveMeasurement(
                        buf,
                        1,
                        EMConfig[Meter].Endianness,
                        EMConfig[Meter].DataType,
                        0
            );
            return receiveMeasurement(buf, 0, EMConfig[Meter].Endianness, EMConfig[Meter].DataType, scalingFactor);
        }
        default:
            return receiveMeasurement(buf, 0, EMConfig[Meter].Endianness, EMConfig[Meter].DataType, EMConfig[Meter].PDivisor);
    }
}


// Modbus functions

// Monitor EV Meter responses, and update Enery and Power and Current measurements
// Does not send any data back.
//
ModbusMessage MBEVMeterResponse(ModbusMessage request) {
    
    uint8_t x;
    int32_t EV[3]={0, 0, 0};

    ModbusDecode( (uint8_t*)request.data(), request.size());

    if (MB.Type == MODBUS_RESPONSE) {
       // _Serialprint("EVMeter Response\n");
        // Packet from EV electric meter
        if (MB.Register == EMConfig[EVMeter].ERegister) {
            // Energy measurement
            EnergyEV = receiveEnergyMeasurement(MB.Data, EVMeter);
            if (ResetKwh == 2) EnergyMeterStart = EnergyEV;                 // At powerup, set EnergyEV to kwh meter value
            EnergyCharged = EnergyEV - EnergyMeterStart;                    // Calculate Energy

        } else if (MB.Register == EMConfig[EVMeter].PRegister) {
            // Power measurement
            PowerMeasured = receivePowerMeasurement(MB.Data, EVMeter);
        } else if (MB.Register == EMConfig[EVMeter].IRegister) {
            // Current measurement
            x = receiveCurrentMeasurement(MB.Data, EVMeter, EV );
            if (x && LoadBl <2) timeout = 10;                   // only reset timeout when data is ok, and Master/Disabled
            for (x = 0; x < 3; x++) {
                // CurrentMeter and PV values are MILLI AMPERE
                Irms_EV[x] = (signed int)(EV[x] / 100);            // Convert to AMPERE * 10
            }
        }
    }
    // As this is a response to an earlier request, do not send response.
    
    return NIL_RESPONSE;              
}

// Monitor PV Meter responses, and update PV current measurements
// Does not send any data back.
//
ModbusMessage MBPVMeterResponse(ModbusMessage request) {

    ModbusDecode( (uint8_t*)request.data(), request.size());

    if (MB.Type == MODBUS_RESPONSE) {
//        _Serialprint("PVMeter Response\n");
        if (PVMeter && MB.Address == PVMeterAddress && MB.Register == EMConfig[PVMeter].IRegister) {
            // packet from PV electric meter
            receiveCurrentMeasurement(MB.Data, PVMeter, PV );
        }    
    }
    // As this is a response to an earlier request, do not send response.
    return NIL_RESPONSE;  
}

//
// Monitor Mains Meter responses, and update Irms values
// Does not send any data back.
ModbusMessage MBMainsMeterResponse(ModbusMessage request) {
    uint8_t x;
    ModbusMessage response;     // response message to be sent back

    ModbusDecode( (uint8_t*)request.data(), request.size());

    // process only Responses, as otherwise MB.Data is unitialized, and it will throw an exception
    if (MB.Type == MODBUS_RESPONSE) {
        if (MB.Register == EMConfig[MainsMeter].IRegister) {

        //_Serialprint("Mains Meter Response\n");
            x = receiveCurrentMeasurement(MB.Data, MainsMeter, CM);
            if (x && LoadBl <2) timeout = 10;                   // only reset timeout when data is ok, and Master/Disabled

            // Calculate Isum (for nodes and master)

            phasesLastUpdate=time(NULL);
            Isum = 0;
            int batteryPerPhase = getBatteryCurrent() / 3; // Divide the battery current per phase to spread evenly

            for (x = 0; x < 3; x++) {
                // Calculate difference of Mains and PV electric meter
                if (PVMeter) CM[x] = CM[x] - PV[x];             // CurrentMeter and PV values are MILLI AMPERE
                Irms[x] = (signed int)(CM[x] / 100);            // Convert to AMPERE * 10
                IrmsOriginal[x] = Irms[x];
                Irms[x] -= batteryPerPhase;
                Isum = Isum + Irms[x];
            }
        }
        else if (MB.Register == EMConfig[MainsMeter].ERegister) {
            //import active energy
            if (MainsMeter == EM_EASTRON_INV)
                Mains_export_active_energy = receiveEnergyMeasurement(MB.Data, MainsMeter);
            else
                Mains_import_active_energy = receiveEnergyMeasurement(MB.Data, MainsMeter);
        }
        else if (MB.Register == EMConfig[MainsMeter].ERegister_Exp) {
            //export active energy
            if (MainsMeter == EM_EASTRON_INV)
                Mains_import_active_energy = receiveEnergyMeasurement(MB.Data, MainsMeter);
            else
                Mains_export_active_energy = receiveEnergyMeasurement(MB.Data, MainsMeter);
        }
    }

    // As this is a response to an earlier request, do not send response.
    return NIL_RESPONSE;              
}


// Request handler for modbus messages addressed to -this- Node/Slave EVSE.
// Sends response back to Master
//
ModbusMessage MBNodeRequest(ModbusMessage request) {
    ModbusMessage response;     // response message to be sent back
    uint8_t ItemID;
    uint8_t i, OK = 0;
    uint16_t value, values[MODBUS_MAX_REGISTER_READ];
    
    // Check if the call is for our current ServerID, or maybe for an old ServerID?
    if (LoadBl != request.getServerID()) return NIL_RESPONSE;
    

    ModbusDecode( (uint8_t*)request.data(), request.size());
    ItemID = mapModbusRegister2ItemID();

    switch (MB.Function) {
        case 0x03: // (Read holding register)
        case 0x04: // (Read input register)
            //     ReadItemValueResponse();
            if (ItemID) {
                response.add(MB.Address, MB.Function, (uint8_t)(MB.RegisterCount * 2));

                for (i = 0; i < MB.RegisterCount; i++) {
                    values[i] = getItemValue(ItemID + i);
                    response.add(values[i]);
                }
                //ModbusReadInputResponse(MB.Address, MB.Function, values, MB.RegisterCount);
            } else {
                response.setError(MB.Address, MB.Function, ILLEGAL_DATA_ADDRESS);
            }
            break;
        case 0x06: // (Write single register)
            //WriteItemValueResponse();
            if (ItemID) {
                OK = setItemValue(ItemID, MB.Value);
            }

            if (OK && ItemID < STATUS_STATE) write_settings();

            if (MB.Address != BROADCAST_ADR || LoadBl == 0) {
                if (!ItemID) {
                    response.setError(MB.Address, MB.Function, ILLEGAL_DATA_ADDRESS);
                } else if (!OK) {
                    response.setError(MB.Address, MB.Function, ILLEGAL_DATA_VALUE);
                } else {
                    return ECHO_RESPONSE;
                }
            }
            break;
        case 0x10: // (Write multiple register))
            //      WriteMultipleItemValueResponse();
            if (ItemID) {
                for (i = 0; i < MB.RegisterCount; i++) {
                    value = (MB.Data[i * 2] <<8) | MB.Data[(i * 2) + 1];
                    OK += setItemValue(ItemID + i, value);
                }
            }

            if (OK && ItemID < STATUS_STATE) write_settings();

            if (MB.Address != BROADCAST_ADR || LoadBl == 0) {
                if (!ItemID) {
                    response.setError(MB.Address, MB.Function, ILLEGAL_DATA_ADDRESS);
                } else if (!OK) {
                    response.setError(MB.Address, MB.Function, ILLEGAL_DATA_VALUE);
                } else  {
                    response.add(MB.Address, MB.Function, (uint16_t)MB.Register, (uint16_t)OK);
                }
            }
            break;
        default:
            break;
    }

  return response;
}

// The Node/Server receives a broadcast message from the Master
// Does not send any data back.
ModbusMessage MBbroadcast(ModbusMessage request) {
    uint8_t ItemID, i, OK = 0;
    uint16_t value;

    ModbusDecode( (uint8_t*)request.data(), request.size());
    ItemID = mapModbusRegister2ItemID();

    if (MB.Type == MODBUS_REQUEST) {

        // Broadcast or addressed to this device
        switch (MB.Function) {
            // FC 03 and 04 are not possible with broadcast messages.
            case 0x06: // (Write single register)
                //WriteItemValueResponse();
                if (ItemID) {
                    OK = setItemValue(ItemID, MB.Value);
                }

                if (OK && ItemID < STATUS_STATE) write_settings();
#ifdef LOG_DEBUG_MODBUS
                _Serialprintf("Broadcast FC06 Item:%u val:%u\n",ItemID, MB.Value);
#endif
                break;
            case 0x10: // (Write multiple register))
                // 0x0020: Balance currents
                if (MB.Register == 0x0020 && LoadBl > 1) {      // Message for Node(s)
                    Balanced[0] = (MB.Data[(LoadBl - 1) * 2] <<8) | MB.Data[(LoadBl - 1) * 2 + 1];
                    if (Balanced[0] == 0 && State == STATE_C) setState(STATE_C1);               // tell EV to stop charging if charge current is zero
                    else if ((State == STATE_B) || (State == STATE_C)) SetCurrent(Balanced[0]); // Set charge current, and PWM output
#ifdef LOG_DEBUG_MODBUS
                    _Serialprintf("Broadcast received, Node %u.%1u A\n", Balanced[0]/10, Balanced[0]%10);
#endif
                    timeout = 10;                                   // reset 10 second timeout
                } else {
                    //WriteMultipleItemValueResponse();
                    if (ItemID) {
                        for (i = 0; i < MB.RegisterCount; i++) {
                            value = (MB.Data[i * 2] <<8) | MB.Data[(i * 2) + 1];
                            OK += setItemValue(ItemID + i, value);
                        }
                    }

                    if (OK && ItemID < STATUS_STATE) write_settings();
#ifdef LOG_DEBUG_MODBUS
                    _Serialprintf("Other Broadcast received\n");
#endif                    
                }    
                break;
            default:
                break;
        }
    }

    // As it is a broadcast message, do not send response.
    return NIL_RESPONSE;              
}

// Data handler for Master
// Responses from Slaves/Nodes are handled here
void MBhandleData(ModbusMessage msg, uint32_t token) 
{
   uint8_t Address = msg.getServerID();

    if (Address == MainsMeterAddress) {
        //_Serialprint("MainsMeter data\n");
        MBMainsMeterResponse(msg);
    } else if (Address == EVMeterAddress) {
        //_Serialprint("EV Meter data\n");
        MBEVMeterResponse(msg);
    } else if (Address == PVMeterAddress) {
        //_Serialprint("PV Meter data\n");
        MBPVMeterResponse(msg);
    // Only responses to FC 03/04 are handled here. FC 06/10 response is only a acknowledge.
    } else {
        ModbusDecode( (uint8_t*)msg.data(), msg.size());

        if (MB.Address > 1 && MB.Address <= NR_EVSES && (MB.Function == 03 || MB.Function == 04)) {
        
            // Packet from Node EVSE
            if (MB.Register == 0x0000) {
                // Node status
            //    _Serialprint("Node Status received\n");
                receiveNodeStatus(MB.Data, MB.Address - 1u);
            }  else if (MB.Register == 0x0108) {
                // Node EV meter settings
            //    _Serialprint("Node EV Meter settings received\n");
                receiveNodeConfig(MB.Data, MB.Address - 1u);
            }
        }
    }

}


void MBhandleError(Error error, uint32_t token) 
{
  // ModbusError wraps the error code and provides a readable error message for it
  ModbusError me(error);
  //_Serialprintf("Error response: %02X - %s\n", error, (const char *)me);
}


  
void ConfigureModbusMode(uint8_t newmode) {

    if(MainsMeter == EM_API) return;

    _Serialprintf("changing LoadBL from %u to %u\n",LoadBl, newmode);
    
    if ((LoadBl < 2 && newmode > 1) || (LoadBl > 1 && newmode < 2) || (newmode == 255) ) {
        
        if (newmode != 255 ) LoadBl = newmode;

        // Setup Modbus workers for Node
        if (LoadBl > 1 ) {
            
            _Serialprint("Setup MBserver/Node workers, end Master/Client\n");
            // Stop Master background task (if active)
            if (newmode != 255 ) MBclient.end();    
            _Serialprintf("task free ram: %u\n", uxTaskGetStackHighWaterMark( NULL ));

            // Register worker. at serverID 'LoadBl', all function codes
            MBserver.registerWorker(LoadBl, ANY_FUNCTION_CODE, &MBNodeRequest);      
            // Also add handler for all broadcast messages from Master.
            MBserver.registerWorker(BROADCAST_ADR, ANY_FUNCTION_CODE, &MBbroadcast);

            if (MainsMeter != EM_API) MBserver.registerWorker(MainsMeterAddress, ANY_FUNCTION_CODE, &MBMainsMeterResponse);
            if (EVMeter) MBserver.registerWorker(EVMeterAddress, ANY_FUNCTION_CODE, &MBEVMeterResponse);
            if (PVMeter) MBserver.registerWorker(PVMeterAddress, ANY_FUNCTION_CODE, &MBPVMeterResponse);

            // Start ModbusRTU Node background task
            MBserver.start();

        } else if (LoadBl < 2 ) {
            // Setup Modbus workers as Master 
            // Stop Node background task (if active)
            _Serialprint("Setup Modbus as Master/Client, stop Server/Node handler\n");

            if (newmode != 255) MBserver.stop();
            _Serialprintf("task free ram: %u\n", uxTaskGetStackHighWaterMark( NULL ));

            MBclient.setTimeout(100);       // timeout 100ms
            MBclient.onDataHandler(&MBhandleData);
            MBclient.onErrorHandler(&MBhandleError);

            // Start ModbusRTU Master backgroud task
            MBclient.begin();
        } 
    } else if (newmode > 1) {
        // Register worker. at serverID 'LoadBl', all function codes
        _Serialprintf("Registering new LoadBl worker at id %u\n", newmode);
        LoadBl = newmode;
        MBserver.registerWorker(newmode, ANY_FUNCTION_CODE, &MBNodeRequest);   
    }
    
}


// Generate random password for AP if not initialized
void CheckAPpassword(void) {
    uint8_t i, c;
    // Set random password if not yet initialized.
    if (strcmp(APpassword.c_str(), "00000000") == 0) {
        for (i=0; i<8 ;i++) {
            c = random(16) + '0';
            if (c > '9') c += 'a'-'9'-1;
            APpassword[i] = c;
        }
    }
    _Serialprintf("APpassword: %s",APpassword.c_str());
}

/**
 * Validate setting ranges and dependencies
 */
void validate_settings(void) {
    uint8_t i;
    uint16_t value;

    // If value is out of range, reset it to default value
    for (i = MENU_ENTER + 1;i < MENU_EXIT; i++){
        value = getItemValue(i);
    //    _Serialprintf("value %s set to %i\n",MenuStr[i].Key, value );
        if (value > MenuStr[i].Max || value < MenuStr[i].Min) {
            value = MenuStr[i].Default;
    //        _Serialprintf("set default value for %s to %i\n",MenuStr[i].Key, value );
            setItemValue(i, value);
        }
    }

    // RFID reader set to Enable One card, the EVSE is disabled by default
    if (RFIDReader == 2) Access_bit = 0;
    // Enable access if no access switch used
    else if (Switch != 1 && Switch != 2) Access_bit = 1;
    // Sensorbox v2 has always address 0x0A
    if (MainsMeter == EM_SENSORBOX) MainsMeterAddress = 0x0A;
    // Disable PV reception if not configured
    if (MainsMeterMeasure == 0) PVMeter = 0;
    // set Lock variables for Solenoid or Motor
    if (Lock == 1) { lock1 = LOW; lock2 = HIGH; }
    else if (Lock == 2) { lock1 = HIGH; lock2 = LOW; }
    // Erase all RFID cards from ram + eeprom if set to EraseAll
    if (RFIDReader == 5) {
       DeleteAllRFID();
    }

    // Update master node config
    Node[0].EVMeter = EVMeter;
    Node[0].EVAddress = EVMeterAddress;

    // Check if AP password is unitialized. 
    // Create random AP password.
    CheckAPpassword(); 

          
    // Default to modbus input registers
    if (EMConfig[EM_CUSTOM].Function != 3) EMConfig[EM_CUSTOM].Function = 4;

    // Backward compatibility < 2.20
    if (EMConfig[EM_CUSTOM].IRegister == 8 || EMConfig[EM_CUSTOM].URegister == 8 || EMConfig[EM_CUSTOM].PRegister == 8 || EMConfig[EM_CUSTOM].ERegister == 8) {
        EMConfig[EM_CUSTOM].DataType = MB_DATATYPE_FLOAT32;
        EMConfig[EM_CUSTOM].IRegister = 0;
        EMConfig[EM_CUSTOM].URegister = 0;
        EMConfig[EM_CUSTOM].PRegister = 0;
        EMConfig[EM_CUSTOM].ERegister = 0;
    }
}

void read_settings(bool write) {
    
    if (preferences.begin("settings", false) == true) {

        Config = preferences.getUChar("Config", CONFIG); 
        Lock = preferences.getUChar("Lock", LOCK); 
        Mode = preferences.getUChar("Mode", MODE); 
        LoadBl = preferences.getUChar("LoadBl", LOADBL); 
        MaxMains = preferences.getUShort("MaxMains", MAX_MAINS); 
        MaxCurrent = preferences.getUShort("MaxCurrent", MAX_CURRENT); 
        MinCurrent = preferences.getUShort("MinCurrent", MIN_CURRENT); 
        MaxCircuit = preferences.getUShort("MaxCircuit", MAX_CIRCUIT); 
        ICal = preferences.getUShort("ICal", ICAL); 
        Switch = preferences.getUChar("Switch", SWITCH); 
        RCmon = preferences.getUChar("RCmon", RC_MON); 
        StartCurrent = preferences.getUShort("StartCurrent", START_CURRENT); 
        StopTime = preferences.getUShort("StopTime", STOP_TIME); 
        ImportCurrent = preferences.getUShort("ImportCurrent",IMPORT_CURRENT);
        Grid = preferences.getUChar("Grid",GRID);
        RFIDReader = preferences.getUChar("RFIDReader",RFID_READER);

        MainsMeter = preferences.getUChar("MainsMeter", MAINS_METER);
        MainsMeterAddress = preferences.getUChar("MainsMAddress",MAINS_METER_ADDRESS);
        MainsMeterMeasure = preferences.getUChar("MainsMMeasure",MAINS_METER_MEASURE);
        PVMeter = preferences.getUChar("PVMeter",PV_METER);
        PVMeterAddress = preferences.getUChar("PVMAddress",PV_METER_ADDRESS);
        EVMeter = preferences.getUChar("EVMeter",EV_METER);
        EVMeterAddress = preferences.getUChar("EVMeterAddress",EV_METER_ADDRESS);
        EMConfig[EM_CUSTOM].Endianness = preferences.getUChar("EMEndianness",EMCUSTOM_ENDIANESS);
        EMConfig[EM_CUSTOM].IRegister = preferences.getUShort("EMIRegister",EMCUSTOM_IREGISTER);
        EMConfig[EM_CUSTOM].IDivisor = preferences.getUChar("EMIDivisor",EMCUSTOM_IDIVISOR);
        EMConfig[EM_CUSTOM].URegister = preferences.getUShort("EMURegister",EMCUSTOM_UREGISTER);
        EMConfig[EM_CUSTOM].UDivisor = preferences.getUChar("EMUDivisor",EMCUSTOM_UDIVISOR);
        EMConfig[EM_CUSTOM].PRegister = preferences.getUShort("EMPRegister",EMCUSTOM_PREGISTER);
        EMConfig[EM_CUSTOM].PDivisor = preferences.getUChar("EMPDivisor",EMCUSTOM_PDIVISOR);
        EMConfig[EM_CUSTOM].ERegister = preferences.getUShort("EMERegister",EMCUSTOM_EREGISTER);
        EMConfig[EM_CUSTOM].EDivisor = preferences.getUChar("EMEDivisor",EMCUSTOM_EDIVISOR);
        EMConfig[EM_CUSTOM].DataType = (mb_datatype)preferences.getUChar("EMDataType",EMCUSTOM_DATATYPE);
        EMConfig[EM_CUSTOM].Function = preferences.getUChar("EMFunction",EMCUSTOM_FUNCTION);
        WIFImode = preferences.getUChar("WIFImode",WIFI_MODE);
        APpassword = preferences.getString("APpassword",AP_PASSWORD);

        enable3f = preferences.getUChar("enable3f", false); 
        maxTemp = preferences.getUShort("maxTemp", MAX_TEMPERATURE);

        preferences.end();                                  

        if (write) write_settings();

    } else _Serialprint("Can not open preferences!\n");
}

void write_settings(void) {

    validate_settings();

 if (preferences.begin("settings", false) ) {

    preferences.putUChar("Config", Config); 
    preferences.putUChar("Lock", Lock); 
    preferences.putUChar("Mode", Mode); 
    preferences.putUChar("LoadBl", LoadBl); 
    preferences.putUShort("MaxMains", MaxMains); 
    preferences.putUShort("MaxCurrent", MaxCurrent); 
    preferences.putUShort("MinCurrent", MinCurrent); 
    preferences.putUShort("MaxCircuit", MaxCircuit); 
    preferences.putUShort("ICal", ICal); 
    preferences.putUChar("Switch", Switch); 
    preferences.putUChar("RCmon", RCmon); 
    preferences.putUShort("StartCurrent", StartCurrent); 
    preferences.putUShort("StopTime", StopTime); 
    preferences.putUShort("ImportCurrent", ImportCurrent);
    preferences.putUChar("Grid", Grid);
    preferences.putUChar("RFIDReader", RFIDReader);

    preferences.putUChar("MainsMeter", MainsMeter);
    preferences.putUChar("MainsMAddress", MainsMeterAddress);
    preferences.putUChar("MainsMMeasure", MainsMeterMeasure);
    preferences.putUChar("PVMeter", PVMeter);
    preferences.putUChar("PVMAddress", PVMeterAddress);
    preferences.putUChar("EVMeter", EVMeter);
    preferences.putUChar("EVMeterAddress", EVMeterAddress);
    preferences.putUChar("EMEndianness", EMConfig[EM_CUSTOM].Endianness);
    preferences.putUShort("EMIRegister", EMConfig[EM_CUSTOM].IRegister);
    preferences.putUChar("EMIDivisor", EMConfig[EM_CUSTOM].IDivisor);
    preferences.putUShort("EMURegister", EMConfig[EM_CUSTOM].URegister);
    preferences.putUChar("EMUDivisor", EMConfig[EM_CUSTOM].UDivisor);
    preferences.putUShort("EMPRegister", EMConfig[EM_CUSTOM].PRegister);
    preferences.putUChar("EMPDivisor", EMConfig[EM_CUSTOM].PDivisor);
    preferences.putUShort("EMERegister", EMConfig[EM_CUSTOM].ERegister);
    preferences.putUChar("EMEDivisor", EMConfig[EM_CUSTOM].EDivisor);
    preferences.putUChar("EMDataType", EMConfig[EM_CUSTOM].DataType);
    preferences.putUChar("EMFunction", EMConfig[EM_CUSTOM].Function);
    preferences.putUChar("WIFImode", WIFImode);
    preferences.putString("APpassword", APpassword);

    preferences.putBool("enable3f", enable3f);
    preferences.putUShort("maxTemp", maxTemp);

    preferences.end();

#ifdef LOG_INFO_EVSE
    _Serialprint("\nsettings saved\n");
#endif

 } else _Serialprint("Can not open preferences!\n");


    if (LoadBl == 1) {                                                          // Master mode
        uint16_t i, values[MODBUS_SYS_CONFIG_COUNT];
        for (i = 0; i < MODBUS_SYS_CONFIG_COUNT; i++) {
            values[i] = getItemValue(MENU_MODE + i);
        }
        // Broadcast settings to other controllers
        ModbusWriteMultipleRequest(BROADCAST_ADR, MODBUS_SYS_CONFIG_START, values, MODBUS_SYS_CONFIG_COUNT);
    }

    ConfigChanged = 1;
}


/*
void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
    _Serialprint("WiFi lost connection.\n");
    // try to reconnect when not connected to AP
    if (WiFi.getMode() != WIFI_AP_STA) {                        
        _Serialprint("Trying to Reconnect\n");
        WiFi.begin();
    }
}
*/

/*
void WiFiStationGotIp(WiFiEvent_t event, WiFiEventInfo_t info) {
    _Serialprintf("Connected to AP: %s\nLocal IP: %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}
*/



/*
//
// WebSockets event handler
//
void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){

  if (type == WS_EVT_CONNECT) {
    _Serialprintf("ws[%s][%u] connect\n", server->url(), client->id());
    //client->printf("Hello Client %u\n", client->id());
    //client->ping();                                                               // this will crash the ESP on IOS 15.3.1 / Safari
    //client->text("Hello from ESP32 Server");

  } else if (type == WS_EVT_DISCONNECT) {
    _Serialprintf("ws[%s][%u] disconnect\n", server->url(), client->id());
  } else if(type == WS_EVT_PONG){
//   _Serialprintf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len)?(char*)data:"");
  } else if (type == WS_EVT_DATA){

    _Serialprintln("Data received: ");
    for (int i=0; i < len; i++) {
      _Serialprintf("%c",(char) data[i]);
    }

    _Serialprintf("\nFree: %d\n",ESP.getFreeHeap() );
  }
}

*/
//
// Replaces %variables% in html file with local variables
//
String processor(const String& var){
  
    if (var == "APhostname") return APhostname;
    if (var == "TempEVSE") return String(TempEVSE);
    if (var == "StateEVSE") return StrStateNameWeb[State];
    if (var == "ErrorEVSE") return getErrorNameWeb(ErrorFlags);
    if (var == "ChargeCurrent") return String((float)Balanced[0]/10, 1U);
    if (var == "ResetReason") return String(esp_reset_reason() );
    if (var == "IrmsL1") return String((float)Irms[0]/10, 1U);
    if (var == "IrmsL2") return String((float)Irms[1]/10, 1U);
    if (var == "IrmsL3") return String((float)Irms[2]/10, 1U);

    return String("");
}


//
// 404 (page not found) handler
// 
void onRequest(AsyncWebServerRequest *request){
    //Handle Unknown Request
    request->send(404);
}



void StopwebServer(void) {
    // ws.closeAll();
    webServer.end();
}


void StartwebServer(void) {

    webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        _Serialprint("page / (root) requested and sent\n");
        request->send(SPIFFS, "/index.html", String(), false, processor);
    });
    // handles compressed .js file from SPIFFS
    // webServer.on("/required.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    //     AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/required.js", "text/javascript");
    //     response->addHeader("Content-Encoding", "gzip");
    //     request->send(response);
    // });
    // // handles compressed .css file from SPIFFS
    // webServer.on("/required.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    //     AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/required.css", "text/css");
    //     response->addHeader("Content-Encoding", "gzip");
    //     request->send(response);
    // });

    webServer.on("/erasesettings", HTTP_DELETE, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Erasing settings, rebooting");
        if ( preferences.begin("settings", false) ) {         // our own settings
          preferences.clear();
          preferences.end();
        }
        if (preferences.begin("nvs.net80211", false) ) {      // WiFi settings used by ESP
          preferences.clear();
          preferences.end();       
        }
        ESP.restart();
    });

    webServer.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", "spiffs.bin updates the SPIFFS partition<br>firmware.bin updates the main firmware<br><form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>");
    });

    webServer.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
       bool shouldReboot = !Update.hasError();
        AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", shouldReboot?"OK":"FAIL");
        response->addHeader("Connection", "close");
        request->send(response);
        delay(500);
        if (shouldReboot) ESP.restart();
    },[](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
        if(!index) {
            _Serialprintf("\nUpdate Start: %s\n", filename.c_str());
                if (filename == "spiffs.bin" ) {
                    _Serialprint("\nSPIFFS partition write\n");
                    // Partition size is 0x90000
                    if(!Update.begin(0x90000, U_SPIFFS)) {
                        Update.printError(Serial);
                    }    
                } else if(!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000), U_FLASH) {
                    Update.printError(Serial);
                }    
        }
        if(!Update.hasError()) {
            if(Update.write(data, len) != len) {
                Update.printError(Serial);
            } else _Serialprintf("bytes written %u\r", index+len);
        }
        if(final) {
            if(Update.end(true)) {
                _Serialprint("\nUpdate Success\n");
            } else {
                Update.printError(Serial);
            }
        }
    });

    webServer.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
        String mode = "N/A";
        int modeId = -1;
        if(Access_bit == 0)  {
            mode = "OFF";
            modeId=0;
        } else {
            switch(Mode) {
                case MODE_NORMAL: mode = "NORMAL"; modeId=1; break;
                case MODE_SOLAR: mode = "SOLAR"; modeId=2; break;
                case MODE_SMART: mode = "SMART"; modeId=3; break;
            }
        }
        String backlight = "N/A";
        switch(BacklightSet) {
            case 0: backlight = "OFF"; break;
            case 1: backlight = "ON"; break;
            case 2: backlight = "DIMMED"; break;
        }
        String evstate = StrStateNameWeb[State];
        String error = getErrorNameWeb(ErrorFlags);
        int errorId = getErrorId(ErrorFlags);

        // if(error == "Waiting for Solar") {
        if(errorId == 6) {
            evstate += " - " + error;
            error = "None";
            errorId = 0;
        }

        boolean evConnected = pilot != PILOT_12V;                    //when access bit = 1, p.ex. in OFF mode, the STATEs are no longer updated

        DynamicJsonDocument doc(1024); // https://arduinojson.org/v6/assistant/
        doc["version"] = String(VERSION);
        doc["mode"] = mode;
        doc["mode_id"] = modeId;
        doc["car_connected"] = evConnected;

        if(WiFi.isConnected()) {
            switch(WiFi.status()) {
                case WL_NO_SHIELD:          doc["wifi"]["status"] = "WL_NO_SHIELD"; break;
                case WL_IDLE_STATUS:        doc["wifi"]["status"] = "WL_IDLE_STATUS"; break;
                case WL_NO_SSID_AVAIL:      doc["wifi"]["status"] = "WL_NO_SSID_AVAIL"; break;
                case WL_SCAN_COMPLETED:     doc["wifi"]["status"] = "WL_SCAN_COMPLETED"; break;
                case WL_CONNECTED:          doc["wifi"]["status"] = "WL_CONNECTED"; break;
                case WL_CONNECT_FAILED:     doc["wifi"]["status"] = "WL_CONNECT_FAILED"; break;
                case WL_CONNECTION_LOST:    doc["wifi"]["status"] = "WL_CONNECTION_LOST"; break;
                case WL_DISCONNECTED:       doc["wifi"]["status"] = "WL_DISCONNECTED"; break;
                default:                    doc["wifi"]["status"] = "UNKNOWN"; break;
            }

            doc["wifi"]["ssid"] = WiFi.SSID();    
            doc["wifi"]["rssi"] = WiFi.RSSI();    
            doc["wifi"]["bssid"] = WiFi.BSSIDstr();  
            doc["wifi"]["auto_connect"] = WiFi.getAutoConnect();  
            doc["wifi"]["auto_reconnect"] = WiFi.getAutoReconnect();   
        }
        
        doc["evse"]["temp"] = TempEVSE;
        doc["evse"]["temp_max"] = maxTemp;
        doc["evse"]["connected"] = evConnected;
        doc["evse"]["access"] = Access_bit == 1;
        doc["evse"]["mode"] = Mode;
        doc["evse"]["solar_stop_timer"] = SolarStopTimer;
        doc["evse"]["state"] = evstate;
        doc["evse"]["state_id"] = State;
        doc["evse"]["error"] = error;
        doc["evse"]["error_id"] = errorId;

        if (RFIDReader) {
            switch(RFIDstatus) {
                case 0: doc["evse"]["rfid"] = "Ready to read card"; break;
                case 1: doc["evse"]["rfid"] = "Present"; break;
                case 2: doc["evse"]["rfid"] = "Card Stored"; break;
                case 3: doc["evse"]["rfid"] = "Card Deleted"; break;
                case 4: doc["evse"]["rfid"] = "Card already stored"; break;
                case 5: doc["evse"]["rfid"] = "Card not in storage"; break;
                case 6: doc["evse"]["rfid"] = "Card Storage full"; break;
                case 7: doc["evse"]["rfid"] = "Invalid"; break;
            }
         } else {
             doc["evse"]["rfid"] = "Not Installed";
         }

        doc["settings"]["charge_current"] = Balanced[0];
        doc["settings"]["override_current"] = OverrideCurrent;
        doc["settings"]["current_min"] = MinCurrent;
        doc["settings"]["current_max"] = MaxCurrent;
        doc["settings"]["current_main"] = MaxMains;
        doc["settings"]["solar_max_import"] = ImportCurrent;
        doc["settings"]["solar_start_current"] = StartCurrent;
        doc["settings"]["solar_stop_time"] = StopTime;
        doc["settings"]["3phases_enabled"] = enable3f;
        doc["settings"]["mains_meter"] = EMConfig[MainsMeter].Desc;
        
        doc["home_battery"]["current"] = homeBatteryCurrent;
        doc["home_battery"]["last_update"] = homeBatteryLastUpdate;

        doc["ev_meter"]["description"] = EMConfig[EVMeter].Desc;
        doc["ev_meter"]["address"] = EVMeterAddress;
        doc["ev_meter"]["import_active_energy"] = round(PowerMeasured / 100)/10; //in kW, precision 1 decimal
        doc["ev_meter"]["total_kwh"] = round(EnergyEV / 100)/10; //in kWh, precision 1 decimal
        doc["ev_meter"]["charged_kwh"] = round(EnergyCharged / 100)/10; //in kWh, precision 1 decimal

        doc["mains_meter"]["import_active_energy"] = round(Mains_import_active_energy / 100)/10; //in kWh, precision 1 decimal
        doc["mains_meter"]["export_active_energy"] = round(Mains_export_active_energy / 100)/10; //in kWh, precision 1 decimal

        doc["phase_currents"]["TOTAL"] = Irms[0] + Irms[1] + Irms[2];
        doc["phase_currents"]["L1"] = Irms[0];
        doc["phase_currents"]["L2"] = Irms[1];
        doc["phase_currents"]["L3"] = Irms[2];
        doc["phase_currents"]["last_data_update"] = phasesLastUpdate;
        doc["phase_currents"]["original_data"]["TOTAL"] = IrmsOriginal[0] + IrmsOriginal[1] + IrmsOriginal[2];
        doc["phase_currents"]["original_data"]["L1"] = IrmsOriginal[0];
        doc["phase_currents"]["original_data"]["L2"] = IrmsOriginal[1];
        doc["phase_currents"]["original_data"]["L3"] = IrmsOriginal[2];
        
        doc["backlight"]["timer"] = BacklightTimer;
        doc["backlight"]["status"] = backlight;

        String json;
        serializeJson(doc, json);

        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
        response->addHeader("Access-Control-Allow-Origin","*"); 
        request->send(response);
    });


    webServer.on("/settings", HTTP_POST, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(512); // https://arduinojson.org/v6/assistant/

        if(request->hasParam("backlight")) {
            String current = request->getParam("backlight")->value();
            int backlight = current.toInt();
            ledcWrite(LCD_CHANNEL, backlight);
            doc["Backlight"] = backlight;
        }

        if(request->hasParam("disable_override_current")) {
            OverrideCurrent = 0;
            doc["disable_override_current"] = "OK";
        }

        if(request->hasParam("mode")) {
            String mode = request->getParam("mode")->value();
            switch(mode.toInt()) {
                case 0: // OFF
                    setAccess(0);
                    break;
                case 1:
                    setAccess(1);
                    setMode(MODE_NORMAL);
                    break;
                case 2:
                    OverrideCurrent = 0;
                    setAccess(1);
                    setMode(MODE_SOLAR);
                    break;
                case 3:
                    OverrideCurrent = 0;
                    setAccess(1);
                    setMode(MODE_SMART);
                    break;
                default:
                    mode = "Value not allowed!";
            }
            doc["mode"] = mode;
        }

        if(request->hasParam("enable_3phases")) {
            String enabled = request->getParam("enable_3phases")->value();
            if(enabled.equalsIgnoreCase("true")) {
                enable3f = true;
                doc["enable_3phases"] = true;
            } else {
                enable3f = false;
                doc["enable_3phases"] = false;
            }
            write_settings();
        }

        if(request->hasParam("stop_timer")) {
            String stop_timer = request->getParam("stop_timer")->value();

            if(stop_timer.toInt() >= 0 && stop_timer.toInt() <= 60) {
                StopTime = stop_timer.toInt();
                doc["stop_timer"] = true;
                write_settings();
            } else {
                doc["stop_timer"] = false;
            }

        }

        if(Mode == MODE_NORMAL) {
            if(request->hasParam("override_current")) {
                String current = request->getParam("override_current")->value();
                if(current.toInt() >= ( MinCurrent * 10 ) && current.toInt() <= ( MaxCurrent * 10 )) {
                    OverrideCurrent = current.toInt();
                    doc["override_current"] = OverrideCurrent;
                } else {
                    doc["override_current"] = "Value not allowed!";
                }
            }

            // if(request->hasParam("force_contactors")) {
            //     String force_contactors = request->getParam("force_contactors")->value();
            //     if(force_contactors.equalsIgnoreCase("true")) {
            //         setState(State, true);
            //         doc["force_contactors"] = "OK";
            //     }
            // }
        }

        String json;
        serializeJson(doc, json);

        request->send(200, "application/json", json);
    },[](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    });

    webServer.on("/currents", HTTP_POST, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(200);

        if(request->hasParam("battery_current")) {
            String value = request->getParam("battery_current")->value();
            homeBatteryCurrent = value.toInt();
            homeBatteryLastUpdate = time(NULL);
            doc["battery_current"] = homeBatteryCurrent;
        }

        if(MainsMeter == EM_API) {
            if(request->hasParam("L1") && request->hasParam("L2") && request->hasParam("L3")) {
                phasesLastUpdate = time(NULL);

                Irms[0] = request->getParam("L1")->value().toInt();
                Irms[1] = request->getParam("L2")->value().toInt();
                Irms[2] = request->getParam("L3")->value().toInt();

                int batteryPerPhase = getBatteryCurrent() / 3;
                Isum = 0; 
                for (int x = 0; x < 3; x++) {  
                    IrmsOriginal[x] = Irms[x];
                    doc["original"]["L" + x] = Irms[x];
                    Irms[x] -= batteryPerPhase;           
                    doc["L" + x] = Irms[x];
                    Isum = Isum + Irms[x];
                }
                doc["TOTAL"] = Isum;

                timeout = 10;

                UpdateCurrentData();
            }
        }

        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);

    },[](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    });

    webServer.on("/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(200);

        ESP.restart();
        doc["reboot"] = true;

        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);

    },[](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    });

#ifdef FAKE_RFID
    //this can be activated by: http://smartevse-xxx.lan/debug?showrfid=1
    webServer.on("/debug", HTTP_GET, [](AsyncWebServerRequest *request) {
        if(request->hasParam("showrfid")) {
            Show_RFID = strtol(request->getParam("showrfid")->value().c_str(),NULL,0);
        }
        _Serialprintf("DEBUG: Show_RFID=%u.\n",Show_RFID);
        request->send(200, "text/html", "Finished request");
    });
#endif

    // attach filesystem root at URL /
    webServer.serveStatic("/", SPIFFS, "/");

    // setup 404 handler 'onRequest'
    webServer.onNotFound(onRequest);

    // setup websockets handler 'onWsEvent'
    // ws.onEvent(onWsEvent);
    // webServer.addHandler(&ws);
    
    // Setup async webserver
    webServer.begin();
    _Serialprint("HTTP server started\n");

}

void onWifiEvent(WiFiEvent_t event) {
    switch (event) {
        case WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP:
            _Serialprintf("Connected to AP: %s\nLocal IP: %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
            break;
        case WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED:
            _Serialprint("Connected or reconnected to WiFi\n");
            break;
        case WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            if (WIFImode == 1) {
                _Serialprint("WiFi Disconnected. Reconnecting...\n");
                //WiFi.setAutoReconnect(true);  //I know this is very counter-intuitive, you would expect this line in WiFiSetup but this is according to docs
                                                //look at: https://github.com/alanswx/ESPAsyncWiFiManager/issues/92
                                                //but somehow it doesnt work reliably, depending on how the disconnect happened...
                WiFi.reconnect();               //this works better!
            }
            break;
        default: break;
  }
}

// Setup Wifi 
void WiFiSetup(void) {

    //ESPAsync_wifiManager.resetSettings();   //reset saved settings

    ESPAsync_wifiManager.setDebugOutput(true);
    ESPAsync_wifiManager.setMinimumSignalQuality(-1);
    // Set config portal channel, default = 1. Use 0 => random channel from 1-13
    ESPAsync_wifiManager.setConfigPortalChannel(0);
    ESPAsync_wifiManager.setAPStaticIPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));

    ESPAsync_wifiManager.setConfigPortalTimeout(120);   // Portal will be available 2 minutes to connect to, then close. (if connected within this time, it will remain active)

    // Start the mDNS responder so that the SmartEVSE can be accessed using a local hostame: http://SmartEVSE-xxxxxx.local
    if (!MDNS.begin(APhostname.c_str())) {                
        _Serialprint("Error setting up MDNS responder!\n");
    } else {
        _Serialprintf("mDNS responder started. http://%s.local\n",APhostname.c_str());
    }

    //WiFi.setAutoReconnect(true);
    //WiFi.persistent(true);
    WiFi.onEvent(onWifiEvent);

    // On disconnect Event, call function
    //WiFi.onEvent(WiFiStationDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    // On IP, call function
    //
    //WiFi.onEvent (WiFiAPstop, SYSTEM_EVENT_AP_STOP);

    // Init and get the time
    // See https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv for Timezone codes for your region
    configTzTime(TZ_INFO, NTP_SERVER);
    
    //if(!getLocalTime(&timeinfo)) _Serialprint("Failed to obtain time\n");
    
    // test code, sets time to 31-OCT, 02:59:50 , 10 seconds before DST will be switched off
    //timeval epoch = {1635641990, 0};                    
    //settimeofday((const timeval*)&epoch, 0);            

#ifndef DEBUG_DISABLED
    // Initialize the server (telnet or web socket) of RemoteDebug
    Debug.begin(APhostname);
#endif
}


void SetupNetworkTask(void * parameter) {

  WiFiSetup();
  StartwebServer();

  while(1) {

    // retrieve time from NTP server
    LocalTimeSet = getLocalTime(&timeinfo, 1000U);
    
    // Cleanup old websocket clients
    // ws.cleanupClients();

    if (WIFImode == 2 && LCDTimer > 10 && WiFi.getMode() != WIFI_AP_STA) {
        _Serialprint("Start Portal...\n");
        StopwebServer();
        ESPAsync_wifiManager.startConfigPortal(APhostname.c_str(), APpassword.c_str());         // blocking until connected or timeout.
        WIFImode = 1;
        write_settings();
        LCDNav = 0;
        StartwebServer();       //restart webserver
    }

    if (WIFImode == 1 && WiFi.getMode() == WIFI_OFF) {
        _Serialprint("Starting WiFi..\n");
        WiFi.mode(WIFI_STA);
        WiFi.begin();
    }    

    if (WIFImode == 0 && WiFi.getMode() != WIFI_OFF) {
        _Serialprint("Stopping WiFi..\n");
        WiFi.disconnect(true);
    }    

#ifndef DEBUG_DISABLED
    // Remote debug over WiFi
    Debug.handle();
#endif

    vTaskDelay(1000 / portTICK_PERIOD_MS);
  } // while(1)

}


void setup() {

    pinMode(PIN_CP_OUT, OUTPUT);            // CP output
    pinMode(PIN_SW_IN, INPUT);              // SW Switch input
    pinMode(PIN_SSR, OUTPUT);               // SSR1 output
    pinMode(PIN_SSR2, OUTPUT);              // SSR2 output
    pinMode(PIN_RCM_FAULT, INPUT_PULLUP);   

    pinMode(PIN_LCD_LED, OUTPUT);           // LCD backlight
    pinMode(PIN_LCD_RST, OUTPUT);           // LCD reset
    pinMode(PIN_IO0_B1, INPUT);             // < button
    pinMode(PIN_LCD_A0_B2, OUTPUT);         // o Select button + A0 LCD
    pinMode(PIN_LCD_SDO_B3, OUTPUT);        // > button + SDA/MOSI pin

    pinMode(PIN_LOCK_IN, INPUT);            // Locking Solenoid input
    pinMode(PIN_LEDR, OUTPUT);              // Red LED output
    pinMode(PIN_LEDG, OUTPUT);              // Green LED output
    pinMode(PIN_LEDB, OUTPUT);              // Blue LED output
    pinMode(PIN_ACTA, OUTPUT);              // Actuator Driver output R
    pinMode(PIN_ACTB, OUTPUT);              // Actuator Driver output W
    pinMode(PIN_CPOFF, OUTPUT);             // Disable CP output (active high)
    pinMode(PIN_RS485_RX, INPUT);
    pinMode(PIN_RS485_TX, OUTPUT);
    pinMode(PIN_RS485_DIR, OUTPUT);

    digitalWrite(PIN_LEDR, LOW);
    digitalWrite(PIN_LEDG, LOW);
    digitalWrite(PIN_LEDB, LOW);
    digitalWrite(PIN_ACTA, LOW);
    digitalWrite(PIN_ACTB, LOW);        
    digitalWrite(PIN_CPOFF, LOW);           // CP signal ACTIVE
    digitalWrite(PIN_SSR, LOW);             // SSR1 OFF
    digitalWrite(PIN_SSR2, LOW);            // SSR2 OFF
    digitalWrite(PIN_LCD_LED, HIGH);        // LCD Backlight ON

 
    // Uart 0 debug/program port
    Serial.begin(115200);
    while (!Serial);
    _Serialprint("\nSmartEVSE v3 powerup\n");

    // configure SPI connection to LCD
    // only the SPI_SCK and SPI_MOSI pins are used
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, SPI_SS);
    // the ST7567's max SPI Clock frequency is 20Mhz at 3.3V/25C
    // We choose 10Mhz here, to reserve some room for error.
    // SPI mode is MODE3 (Idle = HIGH, clock in on rising edge)
    SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE3));
    

    // The CP (control pilot) output is a fixed 1khz square-wave (+6..9v / -12v).
    // It's pulse width varies between 10% and 96% indicating 6A-80A charging current.
    // to detect state changes we should measure the CP signal while it's at ~5% (so 50uS after the positive pulse started)
    // we use an i/o interrupt at the CP pin output, and a one shot timer interrupt to start the ADC conversion.
    // would be nice if there was an easier way...

    // setup timer, and one shot timer interrupt to 50us
    timerA = timerBegin(0, 80, true);
    timerAttachInterrupt(timerA, &onTimerA, false);
    // we start in STATE A, with a static +12V CP signal
    // set alarm to trigger every 1mS, and let it reload every 1ms
    timerAlarmWrite(timerA, PWM_100, true);
    // when PWM is active, we sample the CP pin after 5% 
    timerAlarmEnable(timerA);


    // Setup ADC on CP, PP and Temperature pin
    adc1_config_width(ADC_WIDTH_BIT_10);                                    // 10 bits ADC resolution is enough
    adc1_config_channel_atten(ADC1_CHANNEL_3, ADC_ATTEN_DB_11);             // setup the CP pin input attenuation to 11db
    adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_6);              // setup the PP pin input attenuation to 6db
    adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_6);              // setup the Temperature input attenuation to 6db

    //Characterize the ADC at particular attentuation for each channel
    adc_chars_CP = (esp_adc_cal_characteristics_t *) calloc(1, sizeof(esp_adc_cal_characteristics_t));
    adc_chars_PP = (esp_adc_cal_characteristics_t *) calloc(1, sizeof(esp_adc_cal_characteristics_t));
    adc_chars_Temperature = (esp_adc_cal_characteristics_t *) calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_10, 1100, adc_chars_CP);
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_6, ADC_WIDTH_BIT_10, 1100, adc_chars_PP);
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_6, ADC_WIDTH_BIT_10, 1100, adc_chars_Temperature);
          
    
    // Setup PWM on channel 0, 1000Hz, 10 bits resolution
    ledcSetup(CP_CHANNEL, 1000, 10);            // channel 0  => Group: 0, Channel: 0, Timer: 0
    // setup the RGB led PWM channels
    // as PWM channel 1 is used by the same timer as the CP timer (channel 0), we start with channel 2
    ledcSetup(RED_CHANNEL, 5000, 8);            // R channel 2, 5kHz, 8 bit
    ledcSetup(GREEN_CHANNEL, 5000, 8);          // G channel 3, 5kHz, 8 bit
    ledcSetup(BLUE_CHANNEL, 5000, 8);           // B channel 4, 5kHz, 8 bit
    ledcSetup(LCD_CHANNEL, 5000, 8);            // LCD channel 5, 5kHz, 8 bit

    // attach the channels to the GPIO to be controlled
    ledcAttachPin(PIN_CP_OUT, CP_CHANNEL);      
    //pinMode(PIN_CP_OUT, OUTPUT);                // Re-init the pin to output, required in order for attachInterrupt to work (2.0.2)
                                                // not required/working on master branch..
                                                // see https://github.com/espressif/arduino-esp32/issues/6140
    ledcAttachPin(PIN_LEDR, RED_CHANNEL);
    ledcAttachPin(PIN_LEDG, GREEN_CHANNEL);
    ledcAttachPin(PIN_LEDB, BLUE_CHANNEL);
    ledcAttachPin(PIN_LCD_LED, LCD_CHANNEL);

    ledcWrite(CP_CHANNEL, 1024);                // channel 0, duty cycle 100%
    ledcWrite(RED_CHANNEL, 255);
    ledcWrite(GREEN_CHANNEL, 0);
    ledcWrite(BLUE_CHANNEL, 255);
    ledcWrite(LCD_CHANNEL, 0);

    // Setup PIN interrupt on rising edge
    // the timer interrupt will be reset in the ISR.
    attachInterrupt(PIN_CP_OUT, onCPpulse, RISING);   
   
    // Uart 1 is used for Modbus @ 9600 8N1
    Serial1.begin(MODBUS_BAUDRATE, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);

   
    //Check type of calibration value used to characterize ADC
    _Serialprint("Checking eFuse Vref settings: ");
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        _Serialprint("OK\n");
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        _Serialprint("Two Point\n");
    } else {
        _Serialprint("not programmed!!!\n");
    }
    
    // Initialize SPIFFS
    if (!SPIFFS.begin(true)) {
        LOG_E("SPIFFS failed! Already tried formatting. HALT\n");
        while (true) {
          delay(1);
        }
    }
    _Serialprintf("Total SPIFFS bytes: %u, Bytes used: %u\n",SPIFFS.totalBytes(),SPIFFS.usedBytes());


   // Read all settings from non volatile memory
    read_settings(true);                                                        // initialize with default data when starting for the first time
    ReadRFIDlist();                                                             // Read all stored RFID's from storage

    // We might need some sort of authentication in the future.
    // SmartEVSE v3 have programmed ECDSA-256 keys stored in nvs
    // Unused for now.
    if (preferences.begin("KeyStorage", true) == true) {                        // readonly
        uint16_t hwversion = preferences.getUShort("hwversion");                // 0x0101 (01 = SmartEVSE,  01 = hwver 01)
        serialnr = preferences.getUInt("serialnr");      
        String ec_private = preferences.getString("ec_private");
        String ec_public = preferences.getString("ec_public");
        preferences.end(); 

        // overwrite APhostname if serialnr is programmed
        APhostname = "SmartEVSE-" + String( serialnr & 0xffff, 10);           // SmartEVSE access point Name = SmartEVSE-xxxxx
        _Serialprintf("hwversion %04x serialnr:%u \n",hwversion, serialnr);
        //_Serialprint(ec_public);

    } else _Serialprint("No KeyStorage found in nvs!\n");


    // Create Task EVSEStates, that handles changes in the CP signal
    xTaskCreate(
        EVSEStates,     // Function that should be called
        "EVSEStates",   // Name of the task (for debugging)
        4096,           // Stack size (bytes)                              // printf needs atleast 1kb
        NULL,           // Parameter to pass
        1,              // Task priority
        NULL            // Task handle
    );

    // Create Task BlinkLed (10ms)
    xTaskCreate(
        BlinkLed,       // Function that should be called
        "BlinkLed",     // Name of the task (for debugging)
        1024,           // Stack size (bytes)                              // printf needs atleast 1kb
        NULL,           // Parameter to pass
        1,              // Task priority
        NULL            // Task handle
    );

    // Create Task 100ms Timer
    xTaskCreate(
        Timer100ms,     // Function that should be called
        "Timer100ms",   // Name of the task (for debugging)
        3072,           // Stack size (bytes)                              
        NULL,           // Parameter to pass
        1,              // Task priority
        NULL            // Task handle
    );

    // Create Task Second Timer (1000ms)
    xTaskCreate(
        Timer1S,        // Function that should be called
        "Timer1S",      // Name of the task (for debugging)
        4096,           // Stack size (bytes)                              
        NULL,           // Parameter to pass
        1,              // Task priority
        NULL            // Task handle
    );

    // Setup WiFi, webserver and firmware OTA
    // Please be aware that after doing a OTA update, its possible that the active partition is set to OTA1.
    // Uploading a new firmware through USB will however update OTA0, and you will not notice any changes...

    // Create Task that setups the network, and the webserver 
    xTaskCreate(
        SetupNetworkTask,     // Function that should be called
        "SetupNetworkTask",   // Name of the task (for debugging)
        10000,                // Stack size (bytes)                              // printf needs atleast 1kb
        NULL,                 // Parameter to pass
        1,                    // Task priority
        NULL                  // Task handleCTReceive
    );
    
    // Set eModbus LogLevel to 1, to suppress possible E5 errors
    MBUlogLvl = LOG_LEVEL_CRITICAL;
    ConfigureModbusMode(255);
  
    BacklightTimer = BACKLIGHT;
    GLCD_init();
          
}

void loop() {

    //time_t current_time;
    delay(1000);
    //current_time = time(NULL);
    /*
    LocalTimeSet = getLocalTime(&timeinfo, 1000U);
    
    //_Serialprintf("\ntime: %02d:%02d:%02d dst:%u epoch:%ld",timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, timeinfo.tm_isdst, (long)current_time);
  

    //printf("RSSI: %d\r",WiFi.RSSI() );
    */
}

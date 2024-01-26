#include <Arduino.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Preferences.h>

#include <FS.h>
#include <SPIFFS.h>

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#define USE_ESP_WIFIMANAGER_NTP true
#define USING_AFRICA        false
#define USING_AMERICA       false
#define USING_ANTARCTICA    false
#define USING_ASIA          false
#define USING_ATLANTIC      false
#define USING_AUSTRALIA     false
#define USING_EUROPE        true
#define USING_INDIAN        false
#define USING_PACIFIC       false
#define USING_ETC_GMT       false
#include <ESPAsync_WiFiManager.h>

#include <ESPmDNS.h>
#include <Update.h>

#include <Logging.h>
#include <ModbusServerRTU.h>        // Slave/node
#include <ModbusClientRTU.h>        // Master
#include <time.h>

#include <soc/sens_reg.h>
#include <soc/sens_struct.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>

#include <soc/rtc_io_struct.h>

#include "evse.h"
#include "glcd.h"
#include "utils.h"
#include "OneWire.h"
#include "modbus.h"

#if MQTT
#include <MQTT.h>
WiFiClient client;
MQTTClient MQTTclient;
#endif

#ifndef DEBUG_DISABLED
RemoteDebug Debug;
#endif

#define SNTP_GET_SERVERS_FROM_DHCP 1
#include <esp_sntp.h>

struct tm timeinfo;

AsyncWebServer webServer(80);
//AsyncWebSocket ws("/ws");           // data to/from webpage
AsyncDNSServer dnsServer;
String APhostname = "SmartEVSE-" + String( MacId() & 0xffff, 10);           // SmartEVSE access point Name = SmartEVSE-xxxxx

#if MQTT
// MQTT connection info
String MQTTuser;
String MQTTpassword;
String MQTTprefix = APhostname;
String MQTTHost = "";
uint16_t MQTTPort;

TaskHandle_t MqttTaskHandle = NULL;
uint8_t lastMqttUpdate = 0;
#endif

ESPAsync_WiFiManager ESPAsync_wifiManager(&webServer, &dnsServer, APhostname.c_str());

// SSID and PW for your Router
String Router_SSID;
String Router_Pass;

// Create a ModbusRTU server, client and bridge instance on Serial1
ModbusServerRTU MBserver(2000, PIN_RS485_DIR);     // TCP timeout set to 2000 ms
ModbusClientRTU MBclient(PIN_RS485_DIR);

hw_timer_t * timerA = NULL;
Preferences preferences;

static esp_adc_cal_characteristics_t * adc_chars_CP;
static esp_adc_cal_characteristics_t * adc_chars_PP;
static esp_adc_cal_characteristics_t * adc_chars_Temperature;

struct ModBus MB;          // Used by SmartEVSE fuctions

const char StrStateName[15][13] = {"A", "B", "C", "D", "COMM_B", "COMM_B_OK", "COMM_C", "COMM_C_OK", "Activate", "B1", "C1", "MODEM1", "MODEM2", "MODEM_OK", "MODEM_DENIED"};
const char StrStateNameWeb[15][17] = {"Ready to Charge", "Connected to EV", "Charging", "D", "Request State B", "State B OK", "Request State C", "State C OK", "Activate", "Charging Stopped", "Stop Charging", "Modem Setup", "Modem Request", "Modem Done", "Modem Denied"};
const char StrErrorNameWeb[9][20] = {"None", "No Power Available", "Communication Error", "Temperature High", "Unused", "RCM Tripped", "Waiting for Solar", "Test IO", "Flash Error"};
const char StrMode[3][8] = {"Normal", "Smart", "Solar"};
const char StrAccessBit[2][6] = {"Deny", "Allow"};
const char StrRFIDStatusWeb[8][20] = {"Ready to read card","Present", "Card Stored", "Card Deleted", "Card already stored", "Card not in storage", "Card Storage full", "Invalid" };

// Global data


// The following data will be updated by eeprom/storage data at powerup:
uint16_t MaxMains = MAX_MAINS;                                              // Max Mains Amps (hard limit, limited by the MAINS connection) (A)
uint16_t MaxSumMains = MAX_SUMMAINS;                                        // Max Mains Amps summed over all 3 phases, limit used by EU capacity rate
                                                                            // see https://github.com/serkri/SmartEVSE-3/issues/215
uint16_t MaxCurrent = MAX_CURRENT;                                          // Max Charge current (A)
uint16_t MinCurrent = MIN_CURRENT;                                          // Minimal current the EV is happy with (A)
uint16_t ICal = ICAL;                                                       // CT calibration value
uint8_t Mode = MODE;                                                        // EVSE mode (0:Normal / 1:Smart / 2:Solar)
uint32_t CurrentPWM = 0;                                                    // Current PWM duty cycle value (0 - 1024)
int8_t InitialSoC = -1;                                                     // State of charge of car
int8_t FullSoC = -1;                                                        // SoC car considers itself fully charged
int8_t ComputedSoC = -1;                                                    // Estimated SoC, based on charged kWh
int8_t RemainingSoC = -1;                                                   // Remaining SoC, based on ComputedSoC
int32_t TimeUntilFull = -1;                                                 // Remaining time until car reaches FullSoC, in seconds
int32_t EnergyCapacity = -1;                                                // Car's total battery capacity
int32_t EnergyRequest = -1;                                                 // Requested amount of energy by car
char EVCCID[32];                                                            // Car's EVCCID (EV Communication Controller Identifer)
char RequiredEVCCID[32];                                                    // Required EVCCID before allowing charging

bool CPDutyOverride = false;
uint8_t Lock = LOCK;                                                        // Cable lock (0:Disable / 1:Solenoid / 2:Motor)
uint16_t MaxCircuit = MAX_CIRCUIT;                                          // Max current of the EVSE circuit (A)
uint8_t Config = CONFIG;                                                    // Configuration (0:Socket / 1:Fixed Cable)
uint8_t LoadBl = LOADBL;                                                    // Load Balance Setting (0:Disable / 1:Master / 2-8:Node)
uint8_t Switch = SWITCH;                                                    // External Switch (0:Disable / 1:Access B / 2:Access S / 3:Smart-Solar B / 4:Smart-Solar S)
                                                                            // B=momentary push <B>utton, S=toggle <S>witch
uint8_t RCmon = RC_MON;                                                     // Residual Current Monitor (0:Disable / 1:Enable)
uint16_t StartCurrent = START_CURRENT;
uint16_t StopTime = STOP_TIME;
uint16_t ImportCurrent = IMPORT_CURRENT;
struct DelayedTimeStruct DelayedStartTime;
struct DelayedTimeStruct DelayedStopTime;
uint8_t DelayedRepeat;                                                      // 0 = no repeat, 1 = daily repeat
uint8_t MainsMeter = MAINS_METER;                                           // Type of Mains electric meter (0: Disabled / Constants EM_*)
uint8_t MainsMeterAddress = MAINS_METER_ADDRESS;
uint8_t Grid = GRID;                                                        // Type of Grid connected to Sensorbox (0:4Wire / 1:3Wire )
uint8_t EVMeter = EV_METER;                                                 // Type of EV electric meter (0: Disabled / Constants EM_*)
uint8_t EVMeterAddress = EV_METER_ADDRESS;
uint8_t RFIDReader = RFID_READER;                                           // RFID Reader (0:Disabled / 1:Enabled / 2:Enable One / 3:Learn / 4:Delete / 5:Delete All)
#if FAKE_RFID
uint8_t Show_RFID = 0;
#endif
uint8_t WIFImode = WIFI_MODE;                                               // WiFi Mode (0:Disabled / 1:Enabled / 2:Start Portal)
String APpassword = "00000000";
uint8_t Initialized = INITIALIZED;                                          // When first powered on, the settings need to be initialized.
String TZname = "";

EnableC2_t EnableC2 = ENABLE_C2;                                            // Contactor C2
Modem_t Modem = NOTPRESENT;                                                 // Is an ISO15118 modem installed (experimental)
uint16_t maxTemp = MAX_TEMPERATURE;

int32_t Irms[3]={0, 0, 0};                                                  // Momentary current per Phase (23 = 2.3A) (resolution 100mA)
int32_t Irms_EV[3]={0, 0, 0};                                               // Momentary current per Phase (23 = 2.3A) (resolution 100mA)
int32_t Old_Irms[3]={0, 0, 0};                                              // Saved momentary current per Phase (23 = 2.3A) (resolution 100mA)
                                                                            // Max 3 phases supported
uint8_t Nr_Of_Phases_Charging = 0;                                          // 0 = Undetected, 1,2,3 = nr of phases that was detected at the start of this charging session
Single_Phase_t Switching_To_Single_Phase = FALSE;

uint8_t State = STATE_A;
uint8_t ErrorFlags = NO_ERROR;
uint8_t NextState;
uint8_t pilot;
uint8_t prev_pilot;

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

struct {
    uint8_t Online;
    uint8_t ConfigChanged;
    uint8_t EVMeter;
    uint8_t EVAddress;
    uint8_t MinCurrent;     // 0.1A
    uint8_t Phases;
    uint32_t Timer;         // 1s
} Node[NR_EVSES] = {                                                        // 0: Master / 1: Node 1 ...
   /*         Config   EV     EV       Min      Used    Interval*           // Interval Time   : last Charge time, reset when not charging
    * Online, Changed, Meter, Address, Current, Phases, Timer */            // Min Current     : minimal measured current per phase the EV consumes when starting to charge @ 6A (can be lower then 6A)

    {      1,       0,     0,       0,       0,      0,     0 },            // Used Phases     : detected nr of phases when starting to charge (works with configured EVmeter meter, and might work with sensorbox)

    {      0,       1,     0,       0,       0,      0,     0 },
    {      0,       1,     0,       0,       0,      0,     0 },
    {      0,       1,     0,       0,       0,      0,     0 },
    {      0,       1,     0,       0,       0,      0,     0 },
    {      0,       1,     0,       0,       0,      0,     0 },
    {      0,       1,     0,       0,       0,      0,     0 },
    {      0,       1,     0,       0,       0,      0,     0 }
};

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
uint8_t LCDupdate = 0;                                                      // flag to update the LCD every 1000ms
uint8_t ChargeDelay = 0;                                                    // Delays charging at least 60 seconds in case of not enough current available.
uint8_t C1Timer = 0;
uint8_t ModemStage = 0;                                                     // 0: Modem states will be executed when Modem is enabled 1: Modem stages will be skipped, as SoC is already extracted
int8_t DisconnectTimeCounter = -1;                                          // Count for how long we're disconnected, so we can more reliably throw disconnect event. -1 means counter is disabled
uint8_t ToModemWaitStateTimer = 0;                                          // Timer used from STATE_MODEM_REQUEST to STATE_MODEM_WAIT
uint8_t ToModemDoneStateTimer = 0;                                          // Timer used from STATE_MODEM_WAIT to STATE_MODEM_DONE
uint8_t LeaveModemDoneStateTimer = 0;                                       // Timer used from STATE_MODEM_DONE to other, usually STATE_B
uint8_t LeaveModemDeniedStateTimer = 0;                                     // Timer used from STATE_MODEM_DENIED to STATE_B to re-try authentication
uint8_t NoCurrent = 0;                                                      // counts overcurrent situations.
uint8_t TestState = 0;
uint8_t ModbusRequest = 0;                                                  // Flag to request Modbus information
uint8_t MenuItems[MENU_EXIT];
uint8_t Access_bit = 0;                                                     // 0:No Access 1:Access to SmartEVSE
uint8_t CardOffset = CARD_OFFSET;                                           // RFID card used in Enable One mode

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
bool PilotDisconnected = false;
uint8_t PilotDisconnectTime = 0;                                            // Time the Control Pilot line should be disconnected (Sec)

int32_t EnergyEV = 0;                                                       // Wh -> EV_import_active_energy - EV_export_active_energy
int32_t Mains_export_active_energy = 0;                                     // Mainsmeter exported active energy, only for API purposes so you can guard the
                                                                            // enery usage of your house
int32_t Mains_import_active_energy = 0;                                     // Mainsmeter imported active energy, only for API purposes so you can guard the
                                                                            // enery usage of your house
int32_t EV_export_active_energy = 0;
int32_t EV_import_active_energy = 0;
int32_t CM[3]={0, 0, 0};
int32_t PV[3]={0, 0, 0};
uint8_t ResetKwh = 2;                                                       // if set, reset EV kwh meter at state transition B->C
                                                                            // cleared when charging, reset to 1 when disconnected (state A)
uint8_t ActivationMode = 0, ActivationTimer = 0;
volatile uint16_t adcsample = 0;
volatile uint16_t ADCsamples[25];                                           // declared volatile, as they are used in a ISR
volatile uint8_t sampleidx = 0;
char str[20];
bool LocalTimeSet = false;

int phasesLastUpdate = 0;
int phasesLastUpdate_processed = 0;
int32_t IrmsOriginal[3]={0, 0, 0};   
int homeBatteryCurrent = 0;
int homeBatteryLastUpdate = 0; // Time in milliseconds

struct EMstruct EMConfig[EM_CUSTOM + 1] = {
    /* DESC,      ENDIANNESS,      FCT, DATATYPE,            U_REG,DIV, I_REG,DIV, P_REG,DIV, E_REG_IMP,DIV, E_REG_EXP, DIV */
    {"Disabled",  ENDIANESS_LBF_LWF, 0, MB_DATATYPE_INT32,        0, 0,      0, 0,      0, 0,      0, 0,0     , 0}, // First entry!
    {"Sensorbox", ENDIANESS_HBF_HWF, 4, MB_DATATYPE_FLOAT32, 0xFFFF, 0,      0, 0, 0xFFFF, 0, 0xFFFF, 0,0     , 0}, // Sensorbox (Own routine for request/receive)
    {"Phoenix C", ENDIANESS_HBF_LWF, 4, MB_DATATYPE_INT32,      0x0, 1,    0xC, 3,   0x28, 1,   0x3E, 1,0     , 0}, // PHOENIX CONTACT EEM-350-D-MCB (0,1V / mA / 0,1W / 0,1kWh) max read count 11
    {"Finder 7E", ENDIANESS_HBF_HWF, 4, MB_DATATYPE_FLOAT32, 0x1000, 0, 0x100E, 0, 0x1026, 0, 0x1106, 3,0x110E, 3}, // Finder 7E.78.8.400.0212 (V / A / W / Wh) max read count 127
    {"Eastron3P", ENDIANESS_HBF_HWF, 4, MB_DATATYPE_FLOAT32,    0x0, 0,    0x6, 0,   0x34, 0,  0x48 , 0,0x4A  , 0}, // Eastron SDM630 (V / A / W / kWh) max read count 80
    {"InvEastrn", ENDIANESS_HBF_HWF, 4, MB_DATATYPE_FLOAT32,    0x0, 0,    0x6, 0,   0x34, 0,  0x48 , 0,0x4A  , 0}, // Since Eastron SDM series are bidirectional, sometimes they are connected upsidedown, so positive current becomes negative etc.; Eastron SDM630 (V / A / W / kWh) max read count 80
    {"ABB",       ENDIANESS_HBF_HWF, 3, MB_DATATYPE_INT32,   0x5B00, 1, 0x5B0C, 2, 0x5B14, 2, 0x5000, 2,0x5004, 2}, // ABB B23 212-100 (0.1V / 0.01A / 0.01W / 0.01kWh) RS485 wiring reversed / max read count 125
    {"SolarEdge", ENDIANESS_HBF_HWF, 3, MB_DATATYPE_INT16,    40196, 0,  40191, 0,  40083, 0,  40234, 3, 40226, 3}, // SolarEdge SunSpec (0.01V (16bit) / 0.1A (16bit) / 1W  (16bit) / 1 Wh (32bit))
    {"WAGO",      ENDIANESS_HBF_HWF, 3, MB_DATATYPE_FLOAT32, 0x5002, 0, 0x500C, 0, 0x5012, -3, 0x600C, 0,0x6018, 0}, // WAGO 879-30x0 (V / A / kW / kWh)//TODO maar WAGO heeft ook totaal
    {"API",       ENDIANESS_HBF_HWF, 3, MB_DATATYPE_FLOAT32, 0x5002, 0, 0x500C, 0, 0x5012, 3, 0x6000, 0,0x6018, 0}, // WAGO 879-30x0 (V / A / kW / kWh)
    {"Eastron1P", ENDIANESS_HBF_HWF, 4, MB_DATATYPE_FLOAT32,    0x0, 0,    0x6, 0,   0x0C, 0,  0x48 , 0,0x4A  , 0}, // Eastron SDM630 (V / A / W / kWh) max read count 80
    {"Finder 7M", ENDIANESS_HBF_HWF, 4, MB_DATATYPE_FLOAT32,   2500, 0,   2516, 0,   2536, 0,   2638, 3,     0, 0}, // Finder 7M.38.8.400.0212 (V / A / W / Wh) / Backlight 10173
    {"Unused 1",  ENDIANESS_LBF_LWF, 4, MB_DATATYPE_INT32,        0, 0,      0, 0,      0, 0,      0, 0,     0, 0}, // unused slot for future new meters
    {"Unused 2",  ENDIANESS_LBF_LWF, 4, MB_DATATYPE_INT32,        0, 0,      0, 0,      0, 0,      0, 0,     0, 0}, // unused slot for future new meters
    {"Unused 3",  ENDIANESS_LBF_LWF, 4, MB_DATATYPE_INT32,        0, 0,      0, 0,      0, 0,      0, 0,     0, 0}, // unused slot for future new meters
    {"Unused 4",  ENDIANESS_LBF_LWF, 4, MB_DATATYPE_INT32,        0, 0,      0, 0,      0, 0,      0, 0,     0, 0}, // unused slot for future new meters
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



// Timer interrupt handler
// in STATE A this is called every 1ms (autoreload)
// in STATE B/C there is a PWM signal, and the Alarm is set to 5% after the low-> high transition of the PWM signal
void IRAM_ATTR onTimerA() {

  RTC_ENTER_CRITICAL();
  adcsample = local_adc1_read(ADC1_CHANNEL_3);

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

        } else if (Access_bit == 0 || State == STATE_MODEM_DENIED) {                                            // No Access, LEDs off
            RedPwm = 0;
            GreenPwm = 0;
            BluePwm = 0;
            LedPwm = 0;                  
        } else {                                                                // State A, B or C
    
            if (State == STATE_A) {
                LedPwm = STATE_A_LED_BRIGHTNESS;                                // STATE A, LED on (dimmed)
            
            } else if (State == STATE_B || State == STATE_B1 || State == STATE_MODEM_REQUEST || State == STATE_MODEM_WAIT) {
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
    SetCPDuty(DutyCycle);
}

// Write duty cycle to pin
// Value in range 0 (0% duty) to 1024 (100% duty)
void SetCPDuty(uint32_t DutyCycle){
    ledcWrite(CP_CHANNEL, DutyCycle);                                       // update PWM signal
    CurrentPWM = DutyCycle;
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
    //_LOG_A("\nTemp: %i C (%u mV) ", Temperature , voltage);
    
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
        _LOG_A("PP pin: %u (%u mV)\n", sample, voltage);
    } else {
        //fixed cable
        _LOG_A("PP pin: %u (%u mV) (warning: fixed cable configured so PP probably disconnected, making this reading void)\n", sample, voltage);
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

    // calculate Min/Max of last 25 CP measurements
    for (n=0 ; n<25 ;n++) {
        sample = ADCsamples[n];
        voltage = esp_adc_cal_raw_to_voltage( sample, adc_chars_CP);        // convert adc reading to voltage
        if (voltage < Min) Min = voltage;                                   // store lowest value
        if (voltage > Max) Max = voltage;                                   // store highest value
    }    
    //_LOG_A("min:%u max:%u\n",Min ,Max);

    // test Min/Max against fixed levels
    if (Min >= 3055 ) return PILOT_12V;                                     // Pilot at 12V (min 11.0V)
    if ((Min >= 2735) && (Max < 3055)) return PILOT_9V;                     // Pilot at 9V
    if ((Min >= 2400) && (Max < 2735)) return PILOT_6V;                     // Pilot at 6V
    if ((Min >= 2000) && (Max < 2400)) return PILOT_3V;                     // Pilot at 3V
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
    if(StateCode < 15) return StrStateName[StateCode];
    else return "NOSTATE";
}


const char * getStateNameWeb(uint8_t StateCode) {
    if(StateCode < 15) return StrStateNameWeb[StateCode];
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
    // If mainsmeter disabled we can only run in Normal Mode
    if (!MainsMeter && NewMode != MODE_NORMAL)
        return;

    // Take care of extra conditionals/checks for custom features
    setAccess(!DelayedStartTime.epoch2); //if DelayedStartTime not zero then we are Delayed Charging
    if (NewMode == MODE_SOLAR) {
        // Reset OverrideCurrent if mode is SOLAR
        OverrideCurrent = 0;
    }

    // when switching modes, we just keep charging at the phases we were charging at;
    // it's only the regulation algorithm that is changing...
    // EXCEPT when EnableC2 == Solar Off, because we would expect C2 to be off when in Solar Mode and EnableC2 == Solar Off
    // and also the other way around, multiple phases might be wanted when changing from Solar to Normal or Smart
    bool switchOnLater = false;
    if (EnableC2 == SOLAR_OFF) {
        if ((Mode != MODE_SOLAR && NewMode == MODE_SOLAR) || (Mode == MODE_SOLAR && NewMode != MODE_SOLAR)) {
            //we are switching from non-solar to solar
            //since we EnableC2 == SOLAR_OFF C2 is turned On now, and should be turned off
            setAccess(0);                                                       //switch to OFF
            switchOnLater = true;
        }
    }

#if MQTT
    // Update MQTT faster
    lastMqttUpdate = 10;
#endif

    if (LoadBl == 1) ModbusWriteSingleRequest(BROADCAST_ADR, 0x0003, NewMode);
    if (NewMode == MODE_SMART) {
        ErrorFlags &= ~(NO_SUN | LESS_6A);                                      // Clear All errors
        setSolarStopTimer(0);                                                   // Also make sure the SolarTimer is disabled.
    }
    ChargeDelay = 0;                                                            // Clear any Chargedelay
    BacklightTimer = BACKLIGHT;                                                 // Backlight ON
    Mode = NewMode;
    if (switchOnLater)
        setAccess(1);

    //make mode and start/stoptimes persistent on reboot
    if (preferences.begin("settings", false) ) {                        //false = write mode
        preferences.putUChar("Mode", Mode);
        preferences.putULong("DelayedStartTim", DelayedStartTime.epoch2); //epoch2 only needs 4 bytes
        preferences.putULong("DelayedStopTime", DelayedStopTime.epoch2);   //epoch2 only needs 4 bytes
        preferences.end();
    }
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

/**
 * Checks all parameters to determine whether
 * we are going to force single phase charging
 * Returns true if we are going to do single phase charging
 * Returns false if we are going to do (traditional) 3 phase charing
 * This is only relevant on a 3f mains and 3f car installation!
 * 1f car will always charge 1f undetermined by CONTACTOR2
 */
uint8_t Force_Single_Phase_Charging() {                                         // abbreviated to FSPC
    if (LoadBl != 0)                                                            // No FSPC allowed when loadbalancing
        return 0;       //3f
    switch (EnableC2) {
        case NOT_PRESENT:                                                       //no use trying to switch a contactor on that is not present
        case ALWAYS_OFF:
            return 1;
        case SOLAR_OFF:
            return (Mode == MODE_SOLAR);
        case AUTO:
        case ALWAYS_ON:
            return 0;   //3f charging
    }
    //in case we don't know, stick to 3f charging
    return 0;
}

void setState(uint8_t NewState) {
    if (State != NewState) {
        char Str[50];
        snprintf(Str, sizeof(Str), "#%02d:%02d:%02d STATE %s -> %s\n",timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, getStateName(State), getStateName(NewState) );

        _LOG_A("%s",Str+1);
    }

    switch (NewState) {
        case STATE_B1:
            if (!ChargeDelay) ChargeDelay = 3;                                  // When entering State B1, wait at least 3 seconds before switching to another state.
            if (State != STATE_B1 && State != STATE_B && !PilotDisconnected) {
                PILOT_DISCONNECTED;
                PilotDisconnected = true;
                PilotDisconnectTime = 5;                                       // Set PilotDisconnectTime to 5 seconds

                Serial.print("Pilot Disconnected\n");
            }
            // fall through
        case STATE_A:                                                           // State A1
            CONTACTOR1_OFF;  
            CONTACTOR2_OFF;  
            SetCPDuty(1024);                                                    // PWM off,  channel 0, duty cycle 100%
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

#if MODEM
            if (DisconnectTimeCounter == -1){
                DisconnectTimeCounter = 0;                                      // Start counting disconnect time. If longer than 60 seconds, throw DisconnectEvent
            }
            break;
        case STATE_MODEM_REQUEST: // After overriding PWM, and resetting the safe state is 10% PWM. To make sure communication recovers after going to normal, we do this. Ugly and temporary
            ToModemWaitStateTimer = 5;
            DisconnectTimeCounter = -1;                                         // Disable Disconnect timer. Car is connected
            SetCPDuty(1024);
            CONTACTOR1_OFF;
            CONTACTOR2_OFF;
            break;
        case STATE_MODEM_WAIT: 
            SetCPDuty(50);
            ToModemDoneStateTimer = 60;
            break;
        case STATE_MODEM_DONE:  // This state is reached via STATE_MODEM_WAIT after 60s (timeout condition, nothing received) or after REST request (success, shortcut to immediate charging).
            CP_OFF;
            DisconnectTimeCounter = -1;                                         // Disable Disconnect timer. Car is connected
            LeaveModemDoneStateTimer = 5;                                       // Disconnect CP for 5 seconds, restart charging cycle but this time without the modem steps.
#endif
            break;
        case STATE_B:
            if (Modem)
                CP_ON;
            CONTACTOR1_OFF;
            CONTACTOR2_OFF;
            if (Modem)
                DisconnectTimeCounter = -1;                                         // Disable Disconnect timer. Car is connected
            timerAlarmWrite(timerA, PWM_95, false);                             // Enable Timer alarm, set to diode test (95%)
            SetCurrent(ChargeCurrent);                                          // Enable PWM
            break;      
        case STATE_C:                                                           // State C2
            ActivationMode = 255;                                               // Disable ActivationMode

            if (Switching_To_Single_Phase == GOING_TO_SWITCH) {
                    _LOG_A("Switching CONTACTOR C2 OFF.\n");
                    CONTACTOR2_OFF;
                    setSolarStopTimer(0); //TODO still needed? now we switched contactor2 off, review if we need to stop solar charging
                    //Nr_Of_Phases_Charging = 1; this will be detected automatically
                    Switching_To_Single_Phase = AFTER_SWITCH;                   // we finished the switching process,
                                                                                // BUT we don't know which is the single phase
            }

            CONTACTOR1_ON;
            if (!Force_Single_Phase_Charging() && Switching_To_Single_Phase != AFTER_SWITCH) {                               // in AUTO mode we start with 3phases
                _LOG_I("Switching CONTACTOR C2 ON.\n");
                CONTACTOR2_ON;                                                  // Contactor2 ON
            }
            LCDTimer = 0;
            break;
        case STATE_C1:
            SetCPDuty(1024);                                                    // PWM off,  channel 0, duty cycle 100%
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

#if MQTT
    // Update MQTT faster
    lastMqttUpdate = 10;
#endif

    // BacklightTimer = BACKLIGHT;                                                 // Backlight ON
}

void setAccess(bool Access) {
    Access_bit = Access;
    if (Access == 0) {
        if (State == STATE_C) setState(STATE_C1);                               // Determine where to switch to.
        else if (State == STATE_B || State == STATE_MODEM_REQUEST || State == STATE_MODEM_WAIT || State == STATE_MODEM_DONE || State == STATE_MODEM_DENIED) setState(STATE_B1);
    }

    //make mode and start/stoptimes persistent on reboot
    if (preferences.begin("settings", false) ) {                        //false = write mode
        preferences.putUChar("Access", Access_bit);
        preferences.putUChar("CardOffset", CardOffset);
        preferences.end();
    }

#if MQTT
    // Update MQTT faster
    lastMqttUpdate = 10;
#endif
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
// only runs on the Master or when loadbalancing Disabled
char IsCurrentAvailable(void) {
    uint8_t n, ActiveEVSE = 0;
    int Baseload, Baseload_EV, TotalCurrent = 0;

    for (n = 0; n < NR_EVSES; n++) if (BalancedState[n] == STATE_C)             // must be in STATE_C
    {
        ActiveEVSE++;                                                           // Count nr of active (charging) EVSE's
        TotalCurrent += Balanced[n];                                            // Calculate total of all set charge currents
    }

    ActiveEVSE++;                                                           // Do calculations with one more EVSE
    if (ActiveEVSE > NR_EVSES) ActiveEVSE = NR_EVSES;
    Baseload = Imeasured - TotalCurrent;                                    // Calculate Baseload (load without any active EVSE)
    Baseload_EV = Imeasured_EV - TotalCurrent;                              // Load on the EV subpanel excluding any active EVSE
    if (Baseload < 0) Baseload = 0;                                         // only relevant for Smart/Solar mode

    // Check if the lowest charge current(6A) x ActiveEV's + baseload would be higher then the MaxMains.
    if ((ActiveEVSE * (MinCurrent * 10) + Baseload) > (MaxMains * 10)) {
        return 0;                                                           // Not enough current available!, return with error
    }
    if ((ActiveEVSE * (MinCurrent * 10) + Baseload_EV) > (MaxCircuit * 10)) {
        return 0;                                                           // Not enough current available!, return with error
    }
    //assume the current should be available on all 3 phases
    bool must_be_single_phase_charging = (EnableC2 == ALWAYS_OFF || (Mode == MODE_SOLAR && EnableC2 == SOLAR_OFF) ||
            (Mode == MODE_SOLAR && EnableC2 == AUTO && Switching_To_Single_Phase == AFTER_SWITCH));
    int Phases = must_be_single_phase_charging ? 1 : 3;
    if ((Phases * ActiveEVSE * (MinCurrent * 10) + Isum) > (MaxSumMains * 10)) {
        return 0;                                                           // Not enough current available!, return with error
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

// Set global var Nr_Of_Phases_Charging
// 0 = undetected, 1 - 3 nr of phases we are charging
void Set_Nr_of_Phases_Charging(void) {
    uint32_t Max_Charging_Prob = 0;
    uint32_t Charging_Prob=0;                                        // Per phase, the probability that Charging is done at this phase
    Nr_Of_Phases_Charging = 0;
#define THRESHOLD 40
#define BOTTOM_THRESHOLD 25
    _LOG_D("Detected Charging Phases: ChargeCurrent=%u, Balanced[0]=%u, IsetBalanced=%u.\n", ChargeCurrent, Balanced[0],IsetBalanced);
    for (int i=0; i<3; i++) {
        if (EVMeter) {
            Charging_Prob = 10 * (abs(Irms_EV[i] - IsetBalanced)) / IsetBalanced;    //100% means this phase is charging, 0% mwans not charging
                                                                                        //TODO does this work for the slaves too?
            _LOG_D("Trying to detect Charging Phases END Irms_EV[%i]=%.1f A.\n", i, (float)Irms_EV[i]/10);
        }
        Max_Charging_Prob = max(Charging_Prob, Max_Charging_Prob);

        //normalize percentages so they are in the range [0-100]
        if (Charging_Prob >= 200)
            Charging_Prob = 0;
        if (Charging_Prob > 100)
            Charging_Prob = 200 - Charging_Prob;
        _LOG_I("Detected Charging Phases: Charging_Prob[%i]=%i.\n", i, Charging_Prob);

        if (Charging_Prob == Max_Charging_Prob) {
            _LOG_D("Suspect I am charging at phase: L%i.\n", i+1);
            Nr_Of_Phases_Charging++;
        }
        else {
            if ( Charging_Prob <= BOTTOM_THRESHOLD ) {
                _LOG_D("Suspect I am NOT charging at phase: L%i.\n", i+1);
            }
            else {
                if ( Max_Charging_Prob - Charging_Prob <= THRESHOLD ) {
                    _LOG_D("Serious candidate for charging at phase: L%i.\n", i+1);
                    Nr_Of_Phases_Charging++;
                }
            }
        }
    }

    // sanity checks
    // TODO test, this might work with slaves too!
    if (LoadBl != 0) {
        _LOG_A("ERROR: detecting phases while LoadBl=%i, this should never happen!\n", LoadBl);
        Nr_Of_Phases_Charging = 0; //undetected
    }

    if (EnableC2 != AUTO && EnableC2 != NOT_PRESENT) {                         // no further sanity checks possible when AUTO or NOT_PRESENT
        if (Nr_Of_Phases_Charging != 1 && (EnableC2 == ALWAYS_OFF || (EnableC2 == SOLAR_OFF && Mode == MODE_SOLAR))) {
            _LOG_A("Error in detecting phases: EnableC2=%s and Nr_Of_Phases_Charging=%i.\n", StrEnableC2[EnableC2], Nr_Of_Phases_Charging);
            Nr_Of_Phases_Charging = 1;
            _LOG_A("Setting Nr_Of_Phases_Charging to 1.\n");
        }
        if (!Force_Single_Phase_Charging() && Nr_Of_Phases_Charging != 3) {//TODO 2phase charging very rare?
            _LOG_A("Possible error in detecting phases: EnableC2=%s and Nr_Of_Phases_Charging=%i.\n", StrEnableC2[EnableC2], Nr_Of_Phases_Charging);
        }
    }

    _LOG_A("Charging at %i phases.\n", Nr_Of_Phases_Charging);
}

// Calculates Balanced PWM current for each EVSE
// mod =0 normal
// mod =1 we have a new EVSE requesting to start charging.
// only runs on the Master or when loadbalancing Disabled
void CalcBalancedCurrent(char mod) {
    int Average, MaxBalanced, Idifference, Baseload_EV;
    int BalancedLeft = 0;
    signed int IsumImport;
    int ActiveMax = 0, TotalCurrent = 0, Baseload;
    char CurrentSet[NR_EVSES] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t n;

    if (BalancedState[0] == STATE_C && MaxCurrent > MaxCapacity && !Config)
        ChargeCurrent = MaxCapacity * 10;
    else
        ChargeCurrent = MaxCurrent * 10;                                        // Instead use new variable ChargeCurrent.

    // Override current temporary if set
    if (OverrideCurrent)
        ChargeCurrent = OverrideCurrent;

    BalancedMax[0] = ChargeCurrent;
                                                                                // update BalancedMax[0] if the MAX current was adjusted using buttons or CLI
    for (n = 0; n < NR_EVSES; n++) if (BalancedState[n] == STATE_C) {
            BalancedLeft++;                                                     // Count nr of Active (Charging) EVSE's
            ActiveMax += BalancedMax[n];                                        // Calculate total Max Amps for all active EVSEs
            TotalCurrent += Balanced[n];                                        // Calculate total of all set charge currents
    }
    _LOG_V("Checkpoint 1 Isetbalanced=%.1f A Imeasured=%.1f A MaxCircuit=%i Imeasured_EV=%.1f A, Battery Current = %.1f A, mode=%i.\n", (float)IsetBalanced/10, (float)Imeasured/10, MaxCircuit, (float)Imeasured_EV/10, (float)homeBatteryCurrent/10, Mode);

    // When Load balancing = Master,  Limit total current of all EVSEs to MaxCircuit
    // Also, when not in Normal Mode, if MaxCircuit is set, it will limit the total current (subpanel configuration)
    Baseload_EV = Imeasured_EV - TotalCurrent;                                  // Calculate Baseload (load without any active EVSE)
    if (Baseload_EV < 0)
        Baseload_EV = 0;
    Baseload = Imeasured - TotalCurrent;                                        // Calculate Baseload (load without any active EVSE)
    if (Baseload < 0)
        Baseload = 0;

    if (Mode == MODE_NORMAL)                                                    // Normal Mode
    {
        if (LoadBl == 1)                                                        // Load Balancing = Master? MaxCircuit is max current for all active EVSE's;
            IsetBalanced = MaxCircuit * 10 - Baseload_EV;                       // subpanel option not valid in Normal Mode;
                                                                                // limiting is per phase so no Nr_Of_Phases_Charging here!
        else
            IsetBalanced = ChargeCurrent;                                       // No Load Balancing in Normal Mode. Set current to ChargeCurrent (fix: v2.05)
        if (BalancedLeft && mod) {                                              // Only if we have active EVSE's and New EVSE charging
            // Set max combined charge current to MaxMains - Baseload, or MaxCircuit - Baseload_EV if that is less
            IsetBalanced = min((MaxMains * 10) - Baseload, (MaxCircuit * 10 ) - Baseload_EV); //TODO: why are we checking MaxMains and MaxCircuit while we are in Normal mode?
                                                                                              //TODO: capacity rate limiting here?
        }
    } //end MODE_NORMAL
    else { // start MODE_SOLAR || MODE_SMART
        // adapt IsetBalanced in Smart Mode, and ensure the MaxMains/MaxCircuit settings for Solar

        uint8_t Temp_Phases;
        Temp_Phases = (Nr_Of_Phases_Charging ? Nr_Of_Phases_Charging : 3);      // in case nr of phases not detected, assume 3
        Idifference = min((MaxMains * 10) - Imeasured, min((MaxCircuit * 10) - Imeasured_EV, ((MaxSumMains * 10) - Isum)/Temp_Phases));
        if (!mod) {                                                             // no new EVSE's charging
                                                                                // For Smart mode, no new EVSE asking for current
                                                                                // But for Solar mode we _also_ have to guard MaxCircuit and Maxmains!
            if (Idifference > 0) {
                if (Mode == MODE_SMART) {
                    _LOG_V("phaseLastUpdate=%i,processed=%i.\n", phasesLastUpdate ,phasesLastUpdate_processed);
                    if (phasesLastUpdate > phasesLastUpdate_processed) {        // only increase current if phases are updated; even in subpanel mode, if EVMeter says
                                                                                // there is current available, still wait until last phases update to increase
                        IsetBalanced += (Idifference / 4);                      // increase with 1/4th of difference (slowly increase current)
                        if ( LocalTimeSet ) phasesLastUpdate_processed = time(NULL); //only load phasesLastUpdate_processed with valid time
                    }
                }
            }                                                                   // in Solar mode we compute increase of current later on!
            else
                IsetBalanced += Idifference;                                    // last PWM setting + difference (immediately decrease current)
            if (IsetBalanced < 0) IsetBalanced = 0;
            if (IsetBalanced > 800) IsetBalanced = 800;                         // hard limit 80A (added 11-11-2017)
        }
        _LOG_V("Checkpoint 2 Isetbalanced=%.1f A, Idifference=%.1f, mod=%i.\n", (float)IsetBalanced/10, (float)Idifference/10, mod);

        if (Mode == MODE_SOLAR)                                                 // Solar version
        {
            IsumImport = Isum - (10 * ImportCurrent);                           // Allow Import of power from the grid when solar charging
            if (Idifference > 0) {                                              // so we had some room for power as far as MaxCircuit and MaxMains are concerned
                if (IsumImport < 0) {
                    // negative, we have surplus (solar) power available
                    if (IsumImport < -10 && Idifference > 10)
                        IsetBalanced = IsetBalanced + 5;                        // more then 1A available, increase Balanced charge current with 0.5A
                    else
                        IsetBalanced = IsetBalanced + 1;                        // less then 1A available, increase with 0.1A
                } else {
                    // positive, we use more power then is generated
                    if (IsumImport > 20)
                        IsetBalanced = IsetBalanced - (IsumImport / 2);         // we use atleast 2A more then available, decrease Balanced charge current.
                    else if (IsumImport > 10)
                        IsetBalanced = IsetBalanced - 5;                        // we use 1A more then available, decrease with 0.5A
                    else if (IsumImport > 3)
                        IsetBalanced = IsetBalanced - 1;                        // we still use > 0.3A more then available, decrease with 0.1A
                                                                                // if we use <= 0.3A we do nothing
                }
            }                                                                   // we already corrected Isetbalance in case of NOT enough power MaxCircuit/MaxMains
            _LOG_V("Checkpoint 3 Isetbalanced=%.1f A, IsumImport=%.1f, Isum=%.1f, ImportCurrent=%i.\n", (float)IsetBalanced/10, (float)IsumImport/10, (float)Isum/10, ImportCurrent);

            // If IsetBalanced is below MinCurrent or negative, make sure it's set to MinCurrent.
            if ( (IsetBalanced < (BalancedLeft * MinCurrent * 10)) || (IsetBalanced < 0) ) {
                IsetBalanced = BalancedLeft * MinCurrent * 10;
                // ----------- Check to see if we have to continue charging on solar power alone ----------
                if (BalancedLeft && StopTime && (IsumImport > 10)) {
                    //TODO maybe enable solar switching for loadbl = 1
                    if (EnableC2 == AUTO && LoadBl == 0)
                        Set_Nr_of_Phases_Charging();
                    if (Nr_Of_Phases_Charging > 1 && EnableC2 == AUTO && LoadBl == 0) { // when loadbalancing is enabled we don't do forced single phase charging
                        _LOG_A("Switching to single phase.\n");                 // because we wouldnt know which currents to make available to the nodes...
                                                                                // since we don't know how many phases the nodes are using...
                        //switching contactor2 off works ok for Skoda Enyaq but Hyundai Ioniq 5 goes into error, so we have to switch more elegantly
                        if (State == STATE_C) setState(STATE_C1);               // tell EV to stop charging
                        Switching_To_Single_Phase = GOING_TO_SWITCH;
                    }
                    else {
                        if (SolarStopTimer == 0) setSolarStopTimer(StopTime * 60); // Convert minutes into seconds
                    }
                } else {
                    setSolarStopTimer(0);
                }
            } else {
                setSolarStopTimer(0);
            }
        } //end MODE_SOLAR
        else { // MODE_SMART
        // New EVSE charging, and only if we have active EVSE's
            if (mod && BalancedLeft) {                                          // Set max combined charge current to MaxMains - Baseload
                IsetBalanced = min((MaxMains * 10) - Baseload, min((MaxCircuit * 10 ) - Baseload_EV, ((MaxSumMains * 10) - Isum)/3)); //assume the current should be available on all 3 phases
            }
        } //end MODE_SMART
    } // end MODE_SOLAR || MODE_SMART

    // guard MaxCircuit in all modes; slave doesnt run CalcBalancedCurrent
    if (IsetBalanced > (MaxCircuit * 10) - Baseload_EV)
        IsetBalanced = MaxCircuit * 10 - Baseload_EV; //limiting is per phase so no Nr_Of_Phases_Charging here!

    _LOG_V("Checkpoint 4 Isetbalanced=%.1f A.\n", (float)IsetBalanced/10);

    if (BalancedLeft) {                                                         // Only if we have active EVSE's
        if (IsetBalanced < 0 || IsetBalanced < (BalancedLeft * MinCurrent * 10)) {
            IsetBalanced = BalancedLeft * MinCurrent * 10;                      // retain old software behaviour: set minimal "MinCurrent" charge per active EVSE
            NoCurrent++;                                                        // Flag NoCurrent left
            _LOG_I("No Current!!\n");
        } else
            NoCurrent = 0;

        if (IsetBalanced > ActiveMax) IsetBalanced = ActiveMax;                 // limit to total maximum Amps (of all active EVSE's)
                                                                                // TODO not sure if Nr_Of_Phases_Charging should be involved here
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
        if (BalancedLeft) {                                                     // Any Active EVSE's left?
            do {                                                                // Check for EVSE's that are not set yet
                if ((BalancedState[n] == STATE_C) && (!CurrentSet[n])) {        // Active EVSE, and current not yet calculated?
                    Balanced[n] = MaxBalanced / BalancedLeft;                   // Set current to Average
                    CurrentSet[n] = 1;                                          // mark this EVSE as set.
                    BalancedLeft--;                                             // decrease counter of active EVSE's
                    MaxBalanced -= Balanced[n];                                 // Update total current to new (lower) value
                }                                                               //TODO since the average has risen the other EVSE's should be checked for exceeding their MAX's too!
            } while (++n < NR_EVSES && BalancedLeft);
        }


    } // BalancedLeft
    _LOG_V("Checkpoint 5 Isetbalanced=%.1f A.\n", (float)IsetBalanced/10);

    char Str[128];
    char *cur = Str, * const end = Str + sizeof Str;
    if (LoadBl == 1) {
        for (n = 0; n < NR_EVSES; n++) {
            if (cur < end) cur += snprintf(cur, end-cur, "EVSE%u:%s(%u.%1uA),", n, getStateName(BalancedState[n]), Balanced[n]/10, Balanced[n]%10);
            else strcpy(end-sizeof("**truncated**"), "**truncated**");
        }
    _LOG_D("Balance: %s\n", Str);
    }
} //CalcBalancedCurrent

/**
 * Load Balancing 	Modbus Address  LoadBl
    Disabled     	0x01            0x00
    Master       	0x01            0x01
    Node 1 	        0x02            0x02
    Node 2 	        0x03            0x03
    Node 3 	        0x04            0x04
    Node 4 	        0x05            0x05
    Node 5 	        0x06            0x06
    Node 6 	        0x07            0x07
    Node 7 	        0x08            0x08
    Broadcast to all SmartEVSE with address 0x09.
**/

/**
 * In order to keep each node happy, and not timeout with a comm-error you will have to send the chargecurrent for each node in a broadcast message to all nodes
 * (address 09):

    09 10 00 20 00 08 10 00 A0 00 00 00 3C 00 00 00 00 00 00 00 00 00 00 99 24
    Node 0 00 A0 = 160 = 16.0A
    Node 1 00 00 = 0 = 0.0A
    Node 2 00 3C = 60 = 6.0A
    etc.

 *  Each time this message is received on each node, the timeout timer is reset to 10 seconds.
 *  The master will usually send this message every two seconds.
**/

/**
 * Broadcast momentary currents to all Node EVSE's
 */
void BroadcastCurrent(void) {
    ModbusWriteMultipleRequest(BROADCAST_ADR, 0x0020, Balanced, NR_EVSES);
}

/**
 * EVSE Register 0x02*: System configuration (same on all SmartEVSE in a LoadBalancing setup)
 * TODO not sure if this is used anywhere in the code?
Regis 	Access 	Description 	                                        Unit 	Values
0x0200 	R/W 	EVSE mode 		                                        0:Normal / 1:Smart / 2:Solar
0x0201 	R/W 	EVSE Circuit max Current 	                        A 	10 - 160
0x0202 	R/W 	Grid type to which the Sensorbox is connected 		        0:4Wire / 1:3Wire
0x0203 	R/W 	CT calibration value 	                                0.01 	Multiplier
0x0204 	R/W 	Max Mains Current 	                                A 	10 - 200
0x0205 	R/W 	Surplus energy start Current 	                        A 	1 - 16
0x0206 	R/W 	Stop solar charging at 6A after this time 	        min 	0:Disable / 1 - 60
0x0207 	R/W 	Allow grid power when solar charging 	                A 	0 - 6
0x0208 	R/W 	Type of Mains electric meter 		                *
0x0209 	R/W 	Address of Mains electric meter 		                10 - 247
//0x020A 	R/W 	What does Mains electric meter measure 		                0:Mains (Home+EVSE+PV) / 1:Home+EVSE
0x020B 	R/W 	Type of PV electric meter 		                *
0x020C 	R/W 	Address of PV electric meter 		                        10 - 247
0x020D 	R/W 	Byte order of custom electric meter 		                0:LBF & LWF / 1:LBF & HWF / 2:HBF & LWF / 3:HBF & HWF
0x020E 	R/W 	Data type of custom electric meter 		                0:Integer / 1:Double
0x020F 	R/W 	Modbus Function (3/4) of custom electric meter
0x0210 	R/W 	Register for Voltage (V) of custom electric meter 		0 - 65530
0x0211 	R/W 	Divisor for Voltage (V) of custom electric meter 	10x 	0 - 7
0x0212 	R/W 	Register for Current (A) of custom electric meter 		0 - 65530
0x0213 	R/W 	Divisor for Current (A) of custom electric meter 	10x 	0 - 7
0x0214 	R/W 	Register for Power (W) of custom electric meter 		0 - 65534
0x0215 	R/W 	Divisor for Power (W) of custom electric meter 	        10x 	0 - 7 /
0x0216 	R/W 	Register for Energy (kWh) of custom electric meter 		0 - 65534
0x0217 	R/W 	Divisor for Energy (kWh) of custom electric meter 	10x 	0 - 7
0x0218 	R/W 	Maximum register read (Not implemented)
0x0219 	R/W 	WiFi mode
0x021A 	R/W 	Limit max current draw on MAINS (sum of phases) 	A 	9:Disable / 10 - 200
**/

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
 * EVSE Node Config layout
 *
Reg 	Access 	Description 	                        Unit 	Values
0x0100 	R/W 	Configuration 		                        0:Socket / 1:Fixed Cable
0x0101 	R/W 	Cable lock 		                        0:Disable / 1:Solenoid / 2:Motor
0x0102 	R/W 	MIN Charge Current the EV will accept 	A 	6 - 16
0x0103 	R/W 	MAX Charge Current for this EVSE 	A 	6 - 80
0x0104 	R/W 	Load Balance 		                        0:Disabled / 1:Master / 2-8:Node
0x0105 	R/W 	External Switch on pin SW 		        0:Disabled / 1:Access Push-Button / 2:Access Switch / 3:Smart-Solar Push-Button / 4:Smart-Solar Switch
0x0106 	R/W 	Residual Current Monitor on pin RCM 		0:Disabled / 1:Enabled
0x0107 	R/W 	Use RFID reader 		                0:Disabled / 1:Enabled
0x0108 	R/W 	Type of EV electric meter 		        *
0x0109 	R/W 	Address of EV electric meter 		        10 - 247
**/

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
    if(Node[NodeNr].Online) {
        if(Node[NodeNr].Online-- == 1) {
            // Reset Node state when node is offline
            BalancedState[NodeNr] = STATE_A;
            Balanced[NodeNr] = 0;
        }
    }

    ModbusReadInputRequest(NodeNr + 1u, 4, 0x0000, 8);
}

/** To have full control over the nodes, you will have to read each node's status registers, and see if it requests to charge.
 * for example for node 2:

    Received packet (21 bytes) 03 04 10 00 01 00 00 00 3c 00 01 00 00 00 01 00 01 00 20 4d 8c
    00 01 = state B
    00 00 = no errors
    00 3c = charge current 6.0 A
    00 01 = Smart mode
    etc.

    Here the state changes to STATE_COMM_C (00 06)
    Received packet (21 bytes) 03 04 10 00 06 00 00 00 3c 00 01 00 00 00 01 00 01 00 20 0a 8e
    So the ESVE request to charge.

    You can respond to this request by changing the state of the node to State_C
    03 10 00 00 00 02 04 00 07 00 00 49 D6
    Here it will write 00 07 (STATE_COMM_C_OK) to register 0x0000, and reset the error register 0x0001

    The node will respond to this by switching to STATE_C (Charging).
**/

/**
 * EVSE Node status layout
 *
Regist 	Access  Description 	        Unit 	Values
0x0000 	R/W 	State 		                0:A / 1:B / 2:C / 3:D / 4:Node request B / 5:Master confirm B / 6:Node request C /
                                                7:Master confirm C / 8:Activation mode / 9:B1 / 10:C1
0x0001 	R/W 	Error 	                Bit 	1:LESS_6A / 2:NO_COMM / 4:TEMP_HIGH / 8:Unused / 16:RCD / 32:NO_SUN
0x0002 	R/W 	Charging current        0.1 A 	0:no current available / 6-80
0x0003 	R/W 	EVSE mode (without saving)      0:Normal / 1:Smart / 2:Solar
0x0004 	R/W 	Solar Timer 	        s
0x0005 	R/W 	Access bit 		        0:No Access / 1:Access
0x0006 	R/W 	Configuration changed (Not implemented)
0x0007 	R 	Maximum charging current A
0x0008 	R/W 	Number of used phases (Not implemented) 0:Undetected / 1 - 3
0x0009 	R 	Real charging current (Not implemented) 0.1 A
0x000A 	R 	Temperature 	        K
0x000B 	R 	Serial number
0x0020 - 0x0027
        W 	Broadcast charge current. SmartEVSE uses only one value depending on the "Load Balancing" configuration
                                        0.1 A 	0:no current available
**/

/**
 * Master receives Node status over modbus
 * Node -> Master
 *
 * @param uint8_t NodeAdr (1-7)
 */
void receiveNodeStatus(uint8_t *buf, uint8_t NodeNr) {
    Node[NodeNr].Online = 5;
//    memcpy(buf, (uint8_t*)&Node[NodeNr], sizeof(struct NodeState));
    BalancedState[NodeNr] = buf[1];                                             // Node State
    BalancedError[NodeNr] = buf[3];                                             // Node Error status
    // Update Mode when changed on Node and not Smart/Solar Switch on the Master
    // Also make sure we are not in the menu.
    if (buf[7] != Mode && Switch != 4 && !LCDNav) setMode(buf[7]); //TODO get rid of Switch != 4 to prevent master and slave in different modes?
    Node[NodeNr].ConfigChanged = buf[13] | Node[NodeNr].ConfigChanged;
    BalancedMax[NodeNr] = buf[15] * 10;                                         // Node Max ChargeCurrent (0.1A)
    _LOG_D("ReceivedNode[%u]Status State:%u Error:%u, BalancedMax:%u, Mode:%u, ConfigChanged:%u.\n", NodeNr, BalancedState[NodeNr], BalancedError[NodeNr], BalancedMax[NodeNr], buf[7], Node[NodeNr].ConfigChanged);
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
            _LOG_I("Node %u State A->B request ", NodeNr);
            if (current) {                                                      // check if we have enough current
                                                                                // Yes enough current..
                BalancedState[NodeNr] = STATE_B;                                // Mark Node EVSE as active (State B)
                Balanced[NodeNr] = MinCurrent * 10;                             // Initially set current to lowest setting
                values[0] = STATE_COMM_B_OK;
                write = 1;
                _LOG_I("- OK!\n");
            } else {                                                            // We do not have enough current to start charging
                Balanced[NodeNr] = 0;                                           // Make sure the Node does not start charging by setting current to 0
                if ((BalancedError[NodeNr] & (LESS_6A|NO_SUN)) == 0) {          // Error flags cleared?
                    if (Mode == MODE_SOLAR) BalancedError[NodeNr] |= NO_SUN;    // Solar mode: No Solar Power available
                    else BalancedError[NodeNr] |= LESS_6A;                      // Normal or Smart Mode: Not enough current available
                    write = 1;
                }
                _LOG_I("- Not enough current!\n");
            }
            break;

        case STATE_COMM_C:                                                      // request to charge B->C
            _LOG_I("Node %u State B->C request\n", NodeNr);
            Balanced[NodeNr] = 0;                                               // For correct baseload calculation set current to zero
            if (current) {                                                      // check if we have enough current
                                                                                // Yes
                BalancedState[NodeNr] = STATE_C;                                // Mark Node EVSE as Charging (State C)
                CalcBalancedCurrent(1);                                         // Calculate charge current for all connected EVSE's
                values[0] = STATE_COMM_C_OK;
                write = 1;
                _LOG_I("- OK!\n");
            } else {                                                            // We do not have enough current to start charging
                if ((BalancedError[NodeNr] & (LESS_6A|NO_SUN)) == 0) {          // Error flags cleared?
                    if (Mode == MODE_SOLAR) BalancedError[NodeNr] |= NO_SUN;    // Solar mode: No Solar Power available
                    else BalancedError[NodeNr] |= LESS_6A;                      // Normal or Smart Mode: Not enough current available
                    write = 1;
                }
                _LOG_I("- Not enough current!\n");
            }
            break;

        default:
            break;

    }
    values[1] = BalancedError[NodeNr];

    if (write) {
        _LOG_D("NodeAdr %u, BalancedError:%u\n",NodeNr, BalancedError[NodeNr]);
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
        case MENU_MODEM:
            Modem = (Modem_t) val;
            break;
        case MENU_MAX_TEMP:
            maxTemp = val;
            break;
        case MENU_C2:
            EnableC2 = (EnableC2_t) val;
            break;
        case MENU_CONFIG:
            Config = val;
            break;
        case STATUS_MODE:
            // fall through
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
        case MENU_SUMMAINS:
            MaxSumMains = val;
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
                _LOG_V("Broadcast Error message received!\n");
            } else {
                _LOG_V("Broadcast Errors Cleared received!\n");
            }
            break;
        case STATUS_CURRENT:
            OverrideCurrent = val;
            timeout = COMM_TIMEOUT;                                             // reset timeout when register is written
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
        case MENU_MODEM:
            return Modem;
        case MENU_MAX_TEMP:
            return maxTemp;
        case MENU_C2:
            return EnableC2;
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
        case MENU_SUMMAINS:
            return MaxSumMains;
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
            return (signed int)TempEVSE;
        case STATUS_SERIAL:
            return serialnr;

        default:
            return 0;
    }
}


void printStatus(void)
{
        char Str[140];
        snprintf(Str, sizeof(Str) , "#STATE: %s Error: %u StartCurrent: -%i ChargeDelay: %u SolarStopTimer: %u NoCurrent: %u Imeasured: %.1f A IsetBalanced: %.1f A\n", getStateName(State), ErrorFlags, StartCurrent,
                                                                        ChargeDelay, SolarStopTimer,  NoCurrent,
                                                                        (float)Imeasured/10,
                                                                        (float)IsetBalanced/10);
        _LOG_I("%s",Str+1);
        _LOG_I("L1: %.1f A L2: %.1f A L3: %.1f A Isum: %.1f A\n", (float)Irms[0]/10, (float)Irms[1]/10, (float)Irms[2]/10, (float)Isum/10);
}

// Recompute State of Charge, in case we have a known initial state of charge
// This function is called by kWh logic and after an EV state update through API, Serial or MQTT
void RecomputeSoC(void) {
    if (InitialSoC > 0 && FullSoC > 0 && EnergyCapacity > 0) {
        if (InitialSoC == FullSoC) {
            // We're already at full SoC
            ComputedSoC = FullSoC;
            RemainingSoC = 0;
            TimeUntilFull = -1;
        } else {
            int EnergyRemaining = -1;
            int TargetEnergyCapacity = (FullSoC / 100.f) * EnergyCapacity;

            if (EnergyRequest > 0) {
                // Attempt to use EnergyRequest to determine SoC with greater accuracy
                EnergyRemaining = EnergyCharged > 0 ? (EnergyRequest - EnergyCharged) : EnergyRequest;
            } else {
                // We use a rough estimation based on FullSoC and EnergyCapacity
                EnergyRemaining = TargetEnergyCapacity - (EnergyCharged + (InitialSoC / 100.f) * EnergyCapacity);
            }

            RemainingSoC = ((FullSoC * EnergyRemaining) / TargetEnergyCapacity);
            ComputedSoC = RemainingSoC > 1 ? (FullSoC - RemainingSoC) : FullSoC;

            // Only attempt to compute the SoC and TimeUntilFull if we have a EnergyRemaining and PowerMeasured
            if (EnergyRemaining > -1) {
                int TimeToGo = -1;
                // Do a very simple estimation in seconds until car would reach FullSoC according to current charging power
                if (PowerMeasured > 0) {
                    // Use real-time PowerMeasured data if available
                    TimeToGo = (3600 * EnergyRemaining) / PowerMeasured;
                } else if (Nr_Of_Phases_Charging > 0) {
                    // Else, fall back on the theoretical maximum of the cable + nr of phases
                    TimeToGo = (3600 * EnergyRemaining) / (MaxCapacity * (Nr_Of_Phases_Charging * 230));
                }

                // Wait until we have a somewhat sensible estimation while still respecting granny chargers
                if (TimeToGo < 100000) {
                    TimeUntilFull = TimeToGo;
                }
            }

            // We can't possibly charge to over 100% SoC
            if (ComputedSoC > FullSoC) {
                ComputedSoC = FullSoC;
                RemainingSoC = 0;
                TimeUntilFull = -1;
            }

            _LOG_I("SoC: EnergyRemaining %i RemaningSoC %i EnergyRequest %i EnergyCharged %i EnergyCapacity %i ComputedSoC %i FullSoC %i TimeUntilFull %i TargetEnergyCapacity %i\n", EnergyRemaining, RemainingSoC, EnergyRequest, EnergyCharged, EnergyCapacity, ComputedSoC, FullSoC, TimeUntilFull, TargetEnergyCapacity);
        }
    } else {
        if (TimeUntilFull != -1) TimeUntilFull = -1;
    }
    // There's also the possibility an external API/app is used for SoC info. In such case, we allow setting ComputedSoC directly.
}

// EV disconnected from charger. Triggered after 60 seconds of disconnect
// This is done so we can "re-plug" the car in the Modem process without triggering disconnect events
void DisconnectEvent(void){
    _LOG_A("EV disconnected for a while. Resetting SoC states");
    ModemStage = 0; // Enable Modem states again
    InitialSoC = -1;
    FullSoC = -1;
    RemainingSoC = -1;
    ComputedSoC = -1;
    EnergyCapacity = -1;
    EnergyRequest = -1;
    TimeUntilFull = -1;
    strncpy(EVCCID, "", sizeof(EVCCID));
}

void CalcIsum(void) {
    phasesLastUpdate = time(NULL);
    int batteryPerPhase = getBatteryCurrent() / 3;
    Isum = 0;
#if FAKE_SUNNY_DAY
    int32_t temp[3]={0, 0, 0};
    temp[0] = INJECT_CURRENT_L1 * 10;                   //Irms is in units of 100mA
    temp[1] = INJECT_CURRENT_L2 * 10;
    temp[2] = INJECT_CURRENT_L3 * 10;
#endif

    for (int x = 0; x < 3; x++) {
#if FAKE_SUNNY_DAY
        Irms[x] = Irms[x] - temp[x];
#endif
        IrmsOriginal[x] = Irms[x];
        Irms[x] -= batteryPerPhase;
        Isum = Isum + Irms[x];
    }
}

/**
 * Update current data after received current measurement
 */
void UpdateCurrentData(void) {
    uint8_t x;

    // reset Imeasured value (grid power used)
    Imeasured = (MaxMains) * -20;                                               // init to 0 is problematic with negative Irms values, so init to -2x Maxmains
    Imeasured_EV = (MaxCircuit) * -20;
    for (x=0; x<3; x++) {
    // Imeasured holds highest Irms of all channels
        if (Irms[x] > Imeasured) Imeasured = Irms[x];
        if (Irms_EV[x] > Imeasured_EV) Imeasured_EV = Irms_EV[x];
    }
    //sanity check
    if (Imeasured == (MaxMains) * -20) {                                        // if it equals the initialized value, something went wrong!
        _LOG_A("UpdateCurrentData: Imeasured=%.1f, this looks wrong, correcting it to 0 for safety!", (float)Imeasured/10);
        Imeasured = 0;
    }
    if (Imeasured_EV == (MaxCircuit) * -20) {                                        // if it equals the initialized value, something went wrong!
        _LOG_A("UpdateCurrentData: Imeasured_EV=%.1f, this looks wrong, correcting it to 0 for safety!", (float)Imeasured_EV/10);
        Imeasured_EV = 0;
    }

    // Load Balancing mode: Smart/Master or Disabled
    // not needed for subpanel mode
    if (Mode && LoadBl < 2) {
        // Calculate dynamic charge current for connected EVSE's
        CalcBalancedCurrent(0);

        // No current left, or Overload (2x Maxmains)?
        if (NoCurrent > 2 || (Imeasured > (MaxMains * 20))) {
            // STOP charging for all EVSE's
            // Display error message
            ErrorFlags |= LESS_6A; //NOCURRENT;
            // Broadcast Error code over RS485
            ModbusWriteSingleRequest(BROADCAST_ADR, 0x0001, LESS_6A);
            NoCurrent = 0;
        } else if (LoadBl) BroadcastCurrent();                                  // Master sends current to all connected EVSE's

        if ((State == STATE_B) || (State == STATE_C)) {
            // Set current for Master EVSE in Smart Mode
            SetCurrent(Balanced[0]);
        }
        printStatus();  //for debug purposes
    }
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
        if (RB2count++ > 20 || RB2low) {
            RB2last = digitalRead(PIN_SW_IN);
            if (RB2last == 0) {
                // Switch input pulled low
                switch (Switch) {
                    case 1: // Access Button
                        setAccess(!Access_bit);                             // Toggle Access bit on/off
                        _LOG_I("Access: %d\n", Access_bit);
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

            // When the pilot line is disconnected, wait for PilotDisconnectTime, then reconnect
            if (PilotDisconnected) {
                if (PilotDisconnectTime == 0 && pilot == PILOT_NOK ) {          // Pilot should be ~ 0V when disconnected
                    PILOT_CONNECTED;
                    PilotDisconnected = false;
                    Serial.print("Pilot Connected\n");
                }
            } else if (pilot == PILOT_12V) {                                    // Check if we are disconnected, or forced to State A, but still connected to the EV
                // If the RFID reader is set to EnableOne or EnableAll mode, and the Charging cable is disconnected
                // We start a timer to re-lock the EVSE (and unlock the cable) after 60 seconds.
                if ((RFIDReader == 2 || RFIDReader == 1) && AccessTimer == 0 && Access_bit == 1) AccessTimer = RFIDLOCKTIME;

                if (State != STATE_A) setState(STATE_A);                        // reset state, incase we were stuck in STATE_COMM_B
                ChargeDelay = 0;                                                // Clear ChargeDelay when disconnected.

                if (!ResetKwh) ResetKwh = 1;                                    // when set, reset EV kWh meter on state B->C change.
            } else if ( pilot == PILOT_9V && ErrorFlags == NO_ERROR 
                && ChargeDelay == 0 && Access_bit
#if MODEM
                && State != STATE_COMM_B && State != STATE_MODEM_REQUEST && State != STATE_MODEM_WAIT && State != STATE_MODEM_DONE) {                                     // switch to State B ?

#else
                && State != STATE_COMM_B) {                                     // switch to State B ?
#endif
                                                                                // Allow to switch to state C directly if STATE_A_TO_C is set to PILOT_6V (see EVSE.h)
                DiodeCheck = 0;

                ProximityPin();                                                 // Sample Proximity Pin

                _LOG_I("Cable limit: %uA  Max: %uA\n", MaxCapacity, MaxCurrent);
                if (MaxCurrent > MaxCapacity) ChargeCurrent = MaxCapacity * 10; // Do not modify Max Cable Capacity or MaxCurrent (fix 2.05)
                else ChargeCurrent = MinCurrent * 10;                           // Instead use new variable ChargeCurrent

                // Load Balancing : Node
                if (LoadBl > 1) {                                               // Send command to Master, followed by Max Charge Current
                    setState(STATE_COMM_B);                                     // Node wants to switch to State B

                // Load Balancing: Master or Disabled
                } else if (IsCurrentAvailable()) {                             
                    BalancedMax[0] = MaxCapacity * 10;
                    Balanced[0] = ChargeCurrent;                                // Set pilot duty cycle to ChargeCurrent (v2.15)
                    if (Modem == EXPERIMENT && ModemStage == 0){
                        setState(STATE_MODEM_REQUEST);
                    }else{
                        setState(STATE_B);                                          // switch to State B
                    }
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

                    SetCPDuty(0);                                               // PWM off,  channel 0, duty cycle 0%
                                                                                // Control pilot static -12V
                }
            }
            if (pilot == PILOT_DIODE) {
                DiodeCheck = 1;                                                 // Diode found, OK
                _LOG_A("Diode OK\n");
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
            //_LOG_A("EVSEStates task free ram: %u\n", uxTaskGetStackHighWaterMark( NULL ));
            GLCD();
            LCDupdate = 0;
        }    

        if ((ErrorFlags & CT_NOCOMM) && timeout >= COMM_TIMEOUT) ErrorFlags &= ~CT_NOCOMM; // Clear communication error, if present
        
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
        case EM_ABB:
            // Note:
            // - ABB uses 64bit values for this register (size 2)
            if (Export) requestMeasurement(Meter, Address, EMConfig[Meter].ERegister_Exp, 2);
            else        requestMeasurement(Meter, Address, EMConfig[Meter].ERegister, 2);
            break;
        case EM_FINDER_7E:
        case EM_EASTRON3P:
        case EM_EASTRON1P:
        case EM_WAGO:
            if (Export) requestMeasurement(Meter, Address, EMConfig[Meter].ERegister_Exp, 1);
            else        requestMeasurement(Meter, Address, EMConfig[Meter].ERegister, 1);
            break;
        case EM_EASTRON3P_INV:
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
        if (ModbusRequest) {                                                    // Slaves all have ModbusRequest at 0 so they never enter here
            switch (ModbusRequest++) {                                          // State
                case 1:                                                         // PV kwh meter
                    ModbusRequest++;
                    // fall through
                case 2:                                                         // Sensorbox or kWh meter that measures -all- currents
                    if (MainsMeter && MainsMeter != EM_API) {                   // we don't want modbus meter currents to conflict with EM_API currents
                        _LOG_D("ModbusRequest %u: Request MainsMeter Measurement\n", ModbusRequest);
                        requestCurrentMeasurement(MainsMeter, MainsMeterAddress);
                        break;
                    }
                    ModbusRequest++;
                    // fall through
                case 3:
                    // Find next online SmartEVSE
                    do {
                        PollEVNode++;
                        if (PollEVNode >= NR_EVSES) PollEVNode = 0;
                    } while(!Node[PollEVNode].Online);

                    // Request Configuration if changed
                    if (Node[PollEVNode].ConfigChanged) {
                        _LOG_D("ModbusRequest %u: Request Configuration Node %u\n", ModbusRequest, PollEVNode);
                        requestNodeConfig(PollEVNode);
                        break;
                    }
                    ModbusRequest++;
                    // fall through
                case 4:                                                         // EV kWh meter, Energy measurement (total charged kWh)
                    // Request Energy if EV meter is configured
                    if (Node[PollEVNode].EVMeter && Node[PollEVNode].EVMeter != EM_API) {
                        _LOG_D("ModbusRequest %u: Request Energy Node %u\n", ModbusRequest, PollEVNode);
                        requestEnergyMeasurement(Node[PollEVNode].EVMeter, Node[PollEVNode].EVAddress, 0);
                        break;
                    }
                    ModbusRequest++;
                    // fall through
                case 5:                                                         // EV kWh meter, Power measurement (momentary power in Watt)
                    // Request Power if EV meter is configured
                    if (Node[PollEVNode].EVMeter && Node[PollEVNode].EVMeter != EM_API) {
                        requestMeasurement(Node[PollEVNode].EVMeter, Node[PollEVNode].EVAddress,EMConfig[Node[PollEVNode].EVMeter].PRegister, 1);
                        break;
                    }
                    ModbusRequest++;
                    // fall through
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
                    // fall through
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
                    // fall through
                case 20:                                                         // EV kWh meter, Current measurement
                    // Request Current if EV meter is configured
                    if (EVMeter && EVMeter != EM_API) {
                        _LOG_D("ModbusRequest %u: Request EVMeter Current Measurement\n", ModbusRequest);
                        requestCurrentMeasurement(EVMeter, EVMeterAddress);
                        break;
                    }
                    ModbusRequest++;
                    // fall through
                case 21:
                    // Request active energy if Mainsmeter is configured
                    if (MainsMeter && MainsMeter != EM_API) {                   // EM_API does not support energy postings
                        energytimer++; //this ticks approx every second?!?
                        if (energytimer == 30) {
                            _LOG_D("ModbusRequest %u: Request MainsMeter Import Active Energy Measurement\n", ModbusRequest);
                            requestEnergyMeasurement(MainsMeter, MainsMeterAddress, 0);
                            break;
                        }
                        if (energytimer >= 60) {
                            _LOG_D("ModbusRequest %u: Request MainsMeter Export Active Energy Measurement\n", ModbusRequest);
                            requestEnergyMeasurement(MainsMeter, MainsMeterAddress, 1);
                            energytimer = 0;
                            break;
                        }
                    }
                    ModbusRequest++;
                    // fall through
                default:
                    if (Mode) {                                                 // Smart/Solar mode
                        if ((ErrorFlags & CT_NOCOMM) == 0) UpdateCurrentData();      // No communication error with Sensorbox /Kwh meter?
                                                                                // then update the data and send broadcast to all connected EVSE's
                    } else {                                                    // Normal Mode
                        CalcBalancedCurrent(0);                                 // Calculate charge current for connected EVSE's
                        if (LoadBl == 1) BroadcastCurrent();                    // Send to all EVSE's (only in Master mode)
                        if ((State == STATE_B || State == STATE_C) && !CPDutyOverride) SetCurrent(Balanced[0]); // set PWM output for Master
                    }
                    ModbusRequest = 0;
                    //_LOG_A("Timer100ms task free ram: %u\n", uxTaskGetStackHighWaterMark( NULL ));
                    break;
            } //switch
        }


        // Pause the task for 100ms
        vTaskDelay(100 / portTICK_PERIOD_MS);

    } //while(1) loop

}

#if MQTT
void mqtt_receive_callback(const String &topic, const String &payload) {
    if (topic == MQTTprefix + "/Set/Mode") {
        if (payload == "Off") {
            ToModemWaitStateTimer = 0;
            ToModemDoneStateTimer = 0;
            LeaveModemDoneStateTimer = 0;
            setAccess(0);
        } else if (payload == "Normal") {
            setMode(MODE_NORMAL);
        } else if (payload == "Solar") {
            OverrideCurrent = 0;
            setMode(MODE_SOLAR);
        } else if (payload == "Smart") {
            OverrideCurrent = 0;
            setMode(MODE_SMART);
        }
    } else if (topic == MQTTprefix + "/Set/CurrentOverride") {
        uint16_t RequestedCurrent = payload.toInt();
        if (RequestedCurrent == 0) {
            OverrideCurrent = 0;
        } else if (LoadBl < 2 && (Mode == MODE_NORMAL || Mode == MODE_SMART)) { // OverrideCurrent not possible on Slave
            if (RequestedCurrent >= (MinCurrent * 10) && RequestedCurrent <= (MaxCurrent * 10)) {
                OverrideCurrent = RequestedCurrent;
            }
        }
    } else if (topic == MQTTprefix + "/Set/CurrentMaxSumMains" && LoadBl < 2) {
        uint16_t RequestedCurrent = payload.toInt();
        if (RequestedCurrent == 0) {
            MaxSumMains = 0;
        } else if (RequestedCurrent >= (10 * 10) && RequestedCurrent <= (600 * 10)) {
                MaxSumMains = RequestedCurrent;
        }
    } else if (topic == MQTTprefix + "/Set/CPPWMOverride") {
        int pwm = payload.toInt();
        if (pwm == -1) {
            SetCPDuty(1024);
            CP_ON;
            CPDutyOverride = false;
        } else if (pwm == 0) {
            SetCPDuty(0);
            CP_OFF;
            CPDutyOverride = true;
        } else if (pwm <= 1024) {
            SetCPDuty(pwm);
            CP_ON;
            CPDutyOverride = true;
        }
    } else if (topic == MQTTprefix + "/Set/MainsMeter") {
        if (MainsMeter != EM_API || LoadBl >= 2)
            return;

        int32_t L1, L2, L3;
        int n = sscanf(payload.c_str(), "%d:%d:%d", &L1, &L2, &L3);

        if (n == 3 && L1 < 1000 && L2 < 1000 && L3 < 1000) {
            if (LoadBl < 2)
                timeout = COMM_TIMEOUT;
            if ((ErrorFlags & CT_NOCOMM))
                ErrorFlags &= ~CT_NOCOMM; // Clear communication error, if present

            Irms[0] = L1;
            Irms[1] = L2;
            Irms[2] = L3;
            CalcIsum();
            UpdateCurrentData();
        }
    } else if (topic == MQTTprefix + "/Set/EVMeter") {
        if (EVMeter != EM_API)
            return;

        int32_t L1, L2, L3, W, WH;
        int n = sscanf(payload.c_str(), "%d:%d:%d:%d:%d", &L1, &L2, &L3, &W, &WH);

        // We expect 5 values (and accept -1 for unknown values)
        if (n == 5) {
            if ((L1 > -1 && L1 < 1000) && (L2 > -1 && L2 < 1000) && (L3 > -1 && L3 < 1000)) {
                // RMS currents
                Irms_EV[0] = L1;
                Irms_EV[1] = L2;
                Irms_EV[2] = L3;

                UpdateCurrentData();
            }

            if (W > -1) {
                // Power measurement
                PowerMeasured = W;
            }

            if (WH > -1) {
                // Energy measurement
                EnergyEV = WH;
                if (ResetKwh == 2)
                    EnergyMeterStart = EnergyEV;             // At powerup, set EnergyEV to kwh meter value
                EnergyCharged = EnergyEV - EnergyMeterStart; // Calculate Energy
                RecomputeSoC();                              // Recalculate SoC
            }
        }
    } else if (topic == MQTTprefix + "/Set/HomeBatteryCurrent") {
        if (LoadBl >= 2)
            return;
        homeBatteryCurrent = payload.toInt();
        homeBatteryLastUpdate = time(NULL);
    } else if (topic == MQTTprefix + "/Set/RequiredEVCCID") {
        strncpy(RequiredEVCCID, payload.c_str(), sizeof(RequiredEVCCID));
        if (preferences.begin("settings", false) ) {                        //false = write mode
            preferences.putString("RequiredEVCCID", String(RequiredEVCCID));
            preferences.end();
        }
    }

    // Make sure MQTT updates directly to prevent debounces
    lastMqttUpdate = 10;
}

void SetupMQTTClient() {
    // Disconnect existing connection if connected
    if (MQTTclient.connected())
        MQTTclient.disconnect();

    // No need to attempt connections if we aren't configured
    if (MQTTHost == "")
        return;

    // Setup and connect MQTT client instance
    MQTTclient.setHost(MQTTHost.c_str(), MQTTPort);

    MQTTclient.setWill(String(MQTTprefix + "/connected").c_str(), "offline", true, 0);

    if (MQTTuser != "" && MQTTpassword != "") {
        MQTTclient.connect(APhostname.c_str(), MQTTuser.c_str(), MQTTpassword.c_str());
    } else {
        MQTTclient.connect(APhostname.c_str());
    }

    // Keepalive every 15s
    MQTTclient.setKeepAlive(15);

    if (MQTTclient.connected()) {
        // Set up global subscribe callback
        MQTTclient.onMessage(mqtt_receive_callback);

        // Set up subscriptions
        MQTTclient.subscribe(String(MQTTprefix + "/Set/#"));

        MQTTclient.publish(MQTTprefix+"/connected", "online", true, 0);
    }

    //publish MQTT discovery topics
    //we need something to make all this JSON stuff readable, without doing all this assign and serialize stuff
#define jsn(x, y) String(R"(")") + x + R"(" : ")" + y + R"(")"
    //jsn(device_class, current) expands to:
    // R"("device_class" : "current")"

#define jsna(x, y) String(R"(, )") + jsn(x, y)
    //json add expansion, same as above but now with a comma prepended

    //first all device stuff:
    const String device_payload = String(R"("device": {)") + jsn("model","SmartEVSE v3") + jsna("identifiers", MQTTprefix) + jsna("name", MQTTprefix) + jsna("manufacturer","Stegen") + jsna("configuration_url", "http://" + WiFi.localIP().toString().c_str()) + jsna("sw_version", String(VERSION)) + "}";
    //a device SmartEVSE-1001 consists of multiple entities, and an entity can be in the domains sensor, number, select etc.
    String entity_suffix, entity_name, optional_payload;

    //some self-updating variables here:
#define entity_id MQTTprefix + "-" + entity_suffix
#define entity_path MQTTprefix + "/" + entity_suffix
#define entity_name(x) entity_name = x; entity_suffix = entity_name; entity_suffix.replace(" ", "");

    //create template to announce an entity in it's own domain:
#define announce(x, entity_domain) entity_name(x); \
    MQTTclient.publish("homeassistant/" + String(entity_domain) + "/" + entity_id + "/config", \
     "{" \
        + jsn("name", entity_name) \
        + jsna("object_id", entity_id) \
        + jsna("unique_id", entity_id) \
        + jsna("state_topic", entity_path) \
        + jsna("availability_topic",String(MQTTprefix+"/connected")) \
        + ", " + device_payload + optional_payload \
        + "}", \
    true, 0); // Retain + QoS 0

    //set the parameters for and announce sensors with device class 'current':
    optional_payload = jsna("device_class","current") + jsna("unit_of_measurement","A") + jsna("value_template", R"({{ value | int / 10 }})");
    announce("Charge Current", "sensor");
    announce("Max Current", "sensor");
    if (MainsMeter) {
        announce("Mains Current L1", "sensor");
        announce("Mains Current L2", "sensor");
        announce("Mains Current L3", "sensor");
    }
    if (EVMeter) {
        announce("EV Current L1", "sensor");
        announce("EV Current L2", "sensor");
        announce("EV Current L3", "sensor");
    }
    if (homeBatteryLastUpdate) {
        announce("Home Battery Current", "sensor");
    }

    if (Modem) {
        //set the parameters for modem/SoC sensor entities:
        optional_payload = jsna("unit_of_measurement","%") + jsna("value_template", R"({{ none if (value | int == -1) else (value | int) }})");
        announce("EV Initial SoC", "sensor");
        announce("EV Full SoC", "sensor");
        announce("EV Computed SoC", "sensor");
        announce("EV Remaining SoC", "sensor");

        optional_payload = jsna("device_class","duration") + jsna("unit_of_measurement","m") + jsna("value_template", R"({{ none if (value | int == -1) else (value | int / 60) | round }})");
        announce("EV Time Until Full", "sensor");

        optional_payload = jsna("device_class","energy") + jsna("unit_of_measurement","Wh") + jsna("value_template", R"({{ none if (value | int == -1) else (value | int) }})");
        announce("EV Energy Capacity", "sensor");
        announce("EV Energy Request", "sensor");

        optional_payload = jsna("value_template", R"({{ none if (value == '') else value }})");
        announce("EVCCID", "sensor");
        optional_payload = jsna("state_topic", String(MQTTprefix + "/RequiredEVCCID")) + jsna("command_topic", String(MQTTprefix + "/Set/RequiredEVCCID"));
        announce("Required EVCCID", "text");
    }

    if (EVMeter) {
        //set the parameters for and announce other sensor entities:
        optional_payload = jsna("device_class","power") + jsna("unit_of_measurement","W");
        announce("EV Charge Power", "sensor");
        optional_payload = jsna("device_class","energy") + jsna("unit_of_measurement","Wh");
        announce("EV Energy Charged", "sensor");
        optional_payload = jsna("device_class","energy") + jsna("unit_of_measurement","Wh") + jsna("state_class","total_increasing");
        announce("EV Total Energy Charged", "sensor");
    }

    //set the parameters for and announce sensor entities without device_class or unit_of_measurement:
    optional_payload = "";
    announce("EV Plug State", "sensor");
    announce("Access", "sensor");
    announce("State", "sensor");
    announce("RFID", "sensor");

    //set the parameters for and announce diagnostic sensor entities:
    optional_payload = jsna("entity_category","diagnostic");
    announce("Error", "sensor");
    announce("WiFi SSID", "sensor");
    announce("WiFi BSSID", "sensor");
    optional_payload = jsna("entity_category","diagnostic") + jsna("device_class","signal_strength") + jsna("unit_of_measurement","dBm");
    announce("WiFi RSSI", "sensor");
    optional_payload = jsna("entity_category","diagnostic") + jsna("device_class","temperature") + jsna("unit_of_measurement","°C");
    announce("ESP Temp", "sensor");
    optional_payload = jsna("entity_category","diagnostic") + jsna("device_class","duration") + jsna("unit_of_measurement","s") + jsna("entity_registry_enabled_default","False");
    announce("ESP Uptime", "sensor");

    if (Modem) {
        optional_payload = jsna("unit_of_measurement","%") + jsna("value_template", R"({{ (value | int / 1024 * 100) | round(0) }})");
        announce("CP PWM", "sensor");

        optional_payload = jsna("value_template", R"({{ none if (value | int == -1) else (value | int / 1024 * 100) | round }})");
        optional_payload += jsna("command_topic", String(MQTTprefix + "/Set/CPPWMOverride")) + jsna("min", "-1") + jsna("max", "100") + jsna("mode","slider");
        optional_payload += jsna("command_template", R"({{ (value | int * 1024 / 100) | round }})");
        announce("CP PWM Override", "number");
    }

    //set the parameters for and announce select entities, overriding automatic state_topic:
    optional_payload = jsna("state_topic", String(MQTTprefix + "/Mode")) + jsna("command_topic", String(MQTTprefix + "/Set/Mode"));
    optional_payload += String(R"(, "options" : ["Off", "Normal", "Smart", "Solar"])");
    announce("Mode", "select");

    //set the parameters for and announce number entities:
    optional_payload = jsna("command_topic", String(MQTTprefix + "/Set/CurrentOverride")) + jsna("min", "0") + jsna("max", MaxCurrent ) + jsna("mode","slider");
    optional_payload += jsna("value_template", R"({{ none if (value | int == 0) else (value | int / 10) }})") + jsna("command_template", R"({{ value | int * 10 }})");
    announce("Charge Current Override", "number");
}

void mqttPublishData() {
    lastMqttUpdate = 0;

    if (MQTTclient.connected()) {
        if (MainsMeter) {
            MQTTclient.publish(MQTTprefix + "/MainsCurrentL1", String(Irms[0]), false, 0);
            MQTTclient.publish(MQTTprefix + "/MainsCurrentL2", String(Irms[1]), false, 0);
            MQTTclient.publish(MQTTprefix + "/MainsCurrentL3", String(Irms[2]), false, 0);
        }
        if (EVMeter) {
            MQTTclient.publish(MQTTprefix + "/EVCurrentL1", String(Irms_EV[0]), false, 0);
            MQTTclient.publish(MQTTprefix + "/EVCurrentL2", String(Irms_EV[1]), false, 0);
            MQTTclient.publish(MQTTprefix + "/EVCurrentL3", String(Irms_EV[2]), false, 0);
        }
        MQTTclient.publish(MQTTprefix + "/ESPUptime", String((esp_timer_get_time() / 1000000)), false, 0);
        MQTTclient.publish(MQTTprefix + "/ESPTemp", String(TempEVSE), false, 0);
        MQTTclient.publish(MQTTprefix + "/Mode", Access_bit == 0 ? "Off" : Mode > 3 ? "N/A" : StrMode[Mode], true, 0);
        MQTTclient.publish(MQTTprefix + "/MaxCurrent", String(MaxCurrent * 10), true, 0);
        MQTTclient.publish(MQTTprefix + "/ChargeCurrent", String(Balanced[0]), true, 0);
        MQTTclient.publish(MQTTprefix + "/ChargeCurrentOverride", String(OverrideCurrent), true, 0);
        MQTTclient.publish(MQTTprefix + "/Access", String(StrAccessBit[Access_bit]), true, 0);
        MQTTclient.publish(MQTTprefix + "/RFID", !RFIDReader ? "Not Installed" : RFIDstatus >= 8 ? "NOSTATUS" : StrRFIDStatusWeb[RFIDstatus], true, 0);
        MQTTclient.publish(MQTTprefix + "/State", getStateNameWeb(State), true, 0);
        MQTTclient.publish(MQTTprefix + "/Error", getErrorNameWeb(ErrorFlags), true, 0);
        MQTTclient.publish(MQTTprefix + "/EVPlugState", (pilot != PILOT_12V) ? "Connected" : "Disconnected", true, 0);
        MQTTclient.publish(MQTTprefix + "/WiFiSSID", String(WiFi.SSID()), true, 0);
        MQTTclient.publish(MQTTprefix + "/WiFiBSSID", String(WiFi.BSSIDstr()), true, 0);
        MQTTclient.publish(MQTTprefix + "/WiFiRSSI", String(WiFi.RSSI()), false, 0);
        if (Modem) {
            MQTTclient.publish(MQTTprefix + "/CPPWM", String(CurrentPWM), false, 0);
            MQTTclient.publish(MQTTprefix + "/CPPWMOverride", String(CPDutyOverride ? String(CurrentPWM) : "-1"), true, 0);
            MQTTclient.publish(MQTTprefix + "/EVInitialSoC", String(InitialSoC), true, 0);
            MQTTclient.publish(MQTTprefix + "/EVFullSoC", String(FullSoC), true, 0);
            MQTTclient.publish(MQTTprefix + "/EVComputedSoC", String(ComputedSoC), true, 0);
            MQTTclient.publish(MQTTprefix + "/EVRemainingSoC", String(RemainingSoC), true, 0);
            MQTTclient.publish(MQTTprefix + "/EVTimeUntilFull", String(TimeUntilFull), false, 0);
            MQTTclient.publish(MQTTprefix + "/EVEnergyCapacity", String(EnergyCapacity), true, 0);
            MQTTclient.publish(MQTTprefix + "/EVEnergyRequest", String(EnergyRequest), true, 0);
            MQTTclient.publish(MQTTprefix + "/EVCCID", String(EVCCID), true, 0);
            MQTTclient.publish(MQTTprefix + "/RequiredEVCCID", String(RequiredEVCCID), true, 0);
        }
        if (EVMeter) {
            MQTTclient.publish(MQTTprefix + "/EVChargePower", String(PowerMeasured), false, 0);
            MQTTclient.publish(MQTTprefix + "/EVEnergyCharged", String(EnergyCharged), true, 0);
            MQTTclient.publish(MQTTprefix + "/EVTotalEnergyCharged", String(EnergyEV), false, 0);
        }
        if (homeBatteryLastUpdate)
            MQTTclient.publish(MQTTprefix + "/HomeBatteryCurrent", String(homeBatteryCurrent), false, 0);
    } else {
        if (WiFi.status() == WL_CONNECTED) {
            // Setup MQTT client again so we can reconnect
            SetupMQTTClient();
        }
    }
}
#endif

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
#if MODEM
        if (State == STATE_MODEM_REQUEST){
            if (ToModemWaitStateTimer) ToModemWaitStateTimer--;
            else{
                setState(STATE_MODEM_WAIT);                                         // switch to state Modem 2
                GLCD();
            }
        }

        if (State == STATE_MODEM_WAIT){
            if (ToModemDoneStateTimer) ToModemDoneStateTimer--;
            else{
                setState(STATE_MODEM_DONE); 
                GLCD();
            }
        }

        if (State == STATE_MODEM_DONE){
            if (LeaveModemDoneStateTimer) LeaveModemDoneStateTimer--;
            else{
                // Here's what happens:
                //  - State STATE_MODEM_DONE set the CP pin off, to reset connection with car. Since some cars don't support AC charging via ISO15118, SoC is extracted via DC. 
                //  - Negotiation fails between pyPLC and car. Some cars then won't accept charge via AC it seems after, so we just "re-plug" and start charging without the modem communication protocol 
                //  - State STATE_B will enable CP pin again, if disabled. 
                // This stage we are now in is just before we enable CP_PIN and resume via STATE_B

                // Reset CP to idle & turn off, it will be turned on again later for another try
                SetCPDuty(1024);
                CP_OFF;

                // Check whether the EVCCID matches the one required
                if (strcmp(RequiredEVCCID, "") == 0 || strcmp(RequiredEVCCID, EVCCID) == 0) {
                    // We satisfied the EVCCID requirements, skip modem stages next time
                    ModemStage = 1;

                    setState(STATE_B);                                     // switch to STATE_B
                    GLCD();                                                // Re-init LCD (200ms delay)
                } else {
                    // We actually do not want to continue charging and re-start at modem request after 60s
                    ModemStage = 0;
                    LeaveModemDeniedStateTimer = 60;

                    // Change to MODEM_DENIED state
                    setState(STATE_MODEM_DENIED);
                    GLCD();                                                // Re-init LCD (200ms delay)
                }
            }
        }

        if (State == STATE_MODEM_DENIED){
            if (LeaveModemDeniedStateTimer) LeaveModemDeniedStateTimer--;
            else{
                LeaveModemDeniedStateTimer = -1;           // reset ModemStateDeniedTimer
                setState(STATE_A);                         // switch to STATE_B
                CP_ON;
                GLCD();                                    // Re-init LCD (200ms delay)
            }
        }

#endif
        if (State == STATE_C1) {
            if (C1Timer) C1Timer--;                                         // if the EV does not stop charging in 6 seconds, we will open the contactor.
            else {
                _LOG_A("State C1 timeout!\n");
                setState(STATE_B1);                                         // switch back to STATE_B1
                GLCD_init();                                                // Re-init LCD (200ms delay)
            }
        }

#if MODEM
        // Normally, the modem is enabled when Modem == Experiment. However, after a succesfull communication has been set up, EVSE will restart communication by replugging car and moving back to state B.
        // This time, communication is not initiated. When a car is disconnected, we want to enable the modem states again, but using 12V signal is not reliable (we just "replugged" via CP pin, remember).
        // This counter just enables the state after 3 seconds of success.
        if (DisconnectTimeCounter >= 0){
            DisconnectTimeCounter++;
        }

        if (DisconnectTimeCounter > 3){
            pilot = Pilot();
            if (pilot == PILOT_12V){
                DisconnectTimeCounter = -1;
                DisconnectEvent();
            } else{ // Run again
                DisconnectTimeCounter = 0; 
            }
        }
#endif

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
            }
        }

        if (ChargeDelay) ChargeDelay--;                                     // Decrease Charge Delay counter
        if (PilotDisconnectTime) PilotDisconnectTime--;                     // Decrease PilotDisconnectTimer

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
            _LOG_I("No sun/current Errors Cleared.\n");
            ModbusWriteSingleRequest(BROADCAST_ADR, 0x0001, ErrorFlags);    // Broadcast
        }


        // Charge timer
        for (x = 0; x < NR_EVSES; x++) {
            if (BalancedState[x] == STATE_C) Node[x].Timer++;
        }

        if ( (timeout == 0) && !(ErrorFlags & CT_NOCOMM) && (Mode != MODE_NORMAL)) { // timeout if current measurement takes > 10 secs
            // In Normal mode do not timeout; there might be MainsMeter/EVMeter configured that can be retrieved through the API,
            // but in Normal mode we just want to charge ChargeCurrent, irrespective of communication problems.
            ErrorFlags |= CT_NOCOMM;
            if (State == STATE_C) setState(STATE_C1);                       // tell EV to stop charging
            else setState(STATE_B1);                                        // when we are not charging switch to State B1
            _LOG_W("Error, communication error!\n");
            // Try to broadcast communication error to Nodes if we are Master
            if (LoadBl < 2) ModbusWriteSingleRequest(BROADCAST_ADR, 0x0001, ErrorFlags);         
        } else if (timeout) timeout--;

        if (TempEVSE > maxTemp && !(ErrorFlags & TEMP_HIGH))                         // Temperature too High?
        {
            ErrorFlags |= TEMP_HIGH;
            if (State == STATE_C) setState(STATE_C1);                       // tell EV to stop charging
            else setState(STATE_B1);                                        // when we are not charging switch to State B1
            _LOG_W("Error, temperature %i C !\n", TempEVSE);
        }

        if (ErrorFlags & (NO_SUN | LESS_6A)) {
            if (Mode == MODE_SOLAR) {
                if (ChargeDelay == 0) { _LOG_I("Waiting for Solar power...\n"); }
            } else {
                if (ChargeDelay == 0) { _LOG_I("Not enough current available!\n"); }
            }
            if (State == STATE_C) setState(STATE_C1);                       // If we are charging, tell EV to stop charging
            else if (State != STATE_C1) setState(STATE_B1);                 // If we are not in State C1, switch to State B1
            ChargeDelay = CHARGEDELAY;                                      // Set Chargedelay
        }

        // set flag to update the LCD once every second
        LCDupdate = 1;

        // Every two seconds request measurement data from sensorbox/kwh meters.
        // and send broadcast to Node controllers.
        if (LoadBl < 2 && !Broadcast--) {                // Load Balancing mode: Master or Disabled
            ModbusRequest = 1;                                          // Start with state 1, also in Normal mode we want MainsMeter and EVmeter updated 
            //timeout = COMM_TIMEOUT; not sure if necessary, statement was missing in original code    // reset timeout counter (not checked for Master)
            Broadcast = 1;                                                  // repeat every two seconds
        }

        // in Normal mode UpdateCurrentData is never called, so we have to show debug info here...
        // same for Slave
        if (Mode == 0 || LoadBl > 1)
            printStatus();  //for debug purposes

        //_LOG_A("Timer1S task free ram: %u\n", uxTaskGetStackHighWaterMark( NULL ));


#if MQTT
        // Process MQTT data
        MQTTclient.loop();

        if (lastMqttUpdate++ >= 10) {
            // Publish latest data, every 10 seconds
            // We will try to publish data faster if something has changed
            mqttPublishData();
        }
#endif

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
        case EM_ABB:
            // Note:
            // - ABB uses 32-bit values, except for this measurement it uses 64bit unsigned int format
            // We skip the first 4 bytes (effectivaly creating uint 32). Will work as long as the value does not exeed  roughly 20 million
            return receiveMeasurement(buf, 1, EMConfig[Meter].Endianness, MB_DATATYPE_INT32, EMConfig[Meter].EDivisor-3);
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

// stores energy responses; returns 0 if it stored a value, returns 1 if the response didnt match
int StoreEnergyResponse(uint8_t Meter, int32_t& Import, int32_t& Export ) {
    if (MB.Register == EMConfig[Meter].ERegister) {
        //import active energy
        if (Meter == EM_EASTRON3P_INV)
            Export = receiveEnergyMeasurement(MB.Data, Meter);
        else
            Import = receiveEnergyMeasurement(MB.Data, Meter);
        return 0;
    }
    else if (MB.Register == EMConfig[Meter].ERegister_Exp) {
        //export active energy
        if (Meter == EM_EASTRON3P_INV)
            Import = receiveEnergyMeasurement(MB.Data, Meter);
        else
            Export = receiveEnergyMeasurement(MB.Data, Meter);
        return 0;
    }
    return 1;
}

// Monitor EV Meter responses, and update Enery and Power and Current measurements
// Does not send any data back.
//
ModbusMessage MBEVMeterResponse(ModbusMessage request) {
    
    uint8_t x;
    int32_t EV[3]={0, 0, 0};

    ModbusDecode( (uint8_t*)request.data(), request.size());

    if (MB.Type == MODBUS_RESPONSE) {
       // _LOG_A("EVMeter Response\n");
        // Packet from EV electric meter
        if (!StoreEnergyResponse(EVMeter, EV_import_active_energy, EV_export_active_energy)) {
            // Energy measurement
            EnergyEV = EV_import_active_energy - EV_export_active_energy;
            if (ResetKwh == 2) EnergyMeterStart = EnergyEV;                 // At powerup, set EnergyEV to kwh meter value
            EnergyCharged = EnergyEV - EnergyMeterStart;                    // Calculate Energy
            if (Modem)
                RecomputeSoC();
        } else if (MB.Register == EMConfig[EVMeter].PRegister) {
            // Power measurement
            PowerMeasured = receivePowerMeasurement(MB.Data, EVMeter);
        } else if (MB.Register == EMConfig[EVMeter].IRegister) {
            // Current measurement
            x = receiveCurrentMeasurement(MB.Data, EVMeter, EV );
            if (x && LoadBl <2) timeout = COMM_TIMEOUT;                     // only reset timeout when data is ok, and Master/Disabled
            for (x = 0; x < 3; x++) {
                // CurrentMeter and PV values are MILLI AMPERE
                Irms_EV[x] = (signed int)(EV[x] / 100);            // Convert to AMPERE * 10
            }
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

        //_LOG_A("Mains Meter Response\n");
            x = receiveCurrentMeasurement(MB.Data, MainsMeter, CM);
            if (x && LoadBl <2) timeout = COMM_TIMEOUT;         // only reset timeout when data is ok, and Master/Disabled

            // Calculate Isum (for nodes and master)
            for (x = 0; x < 3; x++) {
                // Calculate difference of Mains and PV electric meter
                Irms[x] = (signed int)(CM[x] / 100);            // Convert to AMPERE * 10
            }
            CalcIsum();
        }
        else
            StoreEnergyResponse(MainsMeter, Mains_import_active_energy, Mains_export_active_energy);
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
                _LOG_D("Broadcast FC06 Item:%u val:%u\n",ItemID, MB.Value);
                break;
            case 0x10: // (Write multiple register))
                // 0x0020: Balance currents
                if (MB.Register == 0x0020 && LoadBl > 1) {      // Message for Node(s)
                    Balanced[0] = (MB.Data[(LoadBl - 1) * 2] <<8) | MB.Data[(LoadBl - 1) * 2 + 1];
                    if (Balanced[0] == 0 && State == STATE_C) setState(STATE_C1);               // tell EV to stop charging if charge current is zero
                    else if ((State == STATE_B) || (State == STATE_C)) SetCurrent(Balanced[0]); // Set charge current, and PWM output
                    _LOG_V("Broadcast received, Node %u.%1u A\n", Balanced[0]/10, Balanced[0]%10);
                    timeout = COMM_TIMEOUT;                     // reset 10 second timeout
                } else {
                    //WriteMultipleItemValueResponse();
                    if (ItemID) {
                        for (i = 0; i < MB.RegisterCount; i++) {
                            value = (MB.Data[i * 2] <<8) | MB.Data[(i * 2) + 1];
                            OK += setItemValue(ItemID + i, value);
                        }
                    }

                    if (OK && ItemID < STATUS_STATE) write_settings();
                    _LOG_D("Other Broadcast received\n");
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
        //_LOG_A("MainsMeter data\n");
        MBMainsMeterResponse(msg);
    } else if (Address == EVMeterAddress) {
        //_LOG_A("EV Meter data\n");
        MBEVMeterResponse(msg);
    // Only responses to FC 03/04 are handled here. FC 06/10 response is only a acknowledge.
    } else {
        ModbusDecode( (uint8_t*)msg.data(), msg.size());

        if (MB.Address > 1 && MB.Address <= NR_EVSES && (MB.Function == 03 || MB.Function == 04)) {
        
            // Packet from Node EVSE
            if (MB.Register == 0x0000) {
                // Node status
            //    _LOG_A("Node Status received\n");
                receiveNodeStatus(MB.Data, MB.Address - 1u);
            }  else if (MB.Register == 0x0108) {
                // Node EV meter settings
            //    _LOG_A("Node EV Meter settings received\n");
                receiveNodeConfig(MB.Data, MB.Address - 1u);
            }
        }
    }

}


void MBhandleError(Error error, uint32_t token) 
{
  // ModbusError wraps the error code and provides a readable error message for it
  ModbusError me(error);
  uint8_t address, function;
  uint16_t reg;
  address = token >> 24;
  function = (token >> 16);
  reg = token & 0xFFFF;
  if (LoadBl == 1 && address>=2 && address <=8 && function == 4 && reg == 0) {  //master sends out messages to nodes 2-8, if no EVSE is connected with that address
                                                                                //a timeout will be generated. This is legit!
    _LOG_V("Error response: %02X - %s, address: %02x, function: %02x, reg: %04x.\n", error, (const char *)me,  address, function, reg);
  }
  else {
    _LOG_A("Error response: %02X - %s, address: %02x, function: %02x, reg: %04x.\n", error, (const char *)me,  address, function, reg);
  }
}


  
void ConfigureModbusMode(uint8_t newmode) {

    _LOG_A("changing LoadBl from %u to %u\n",LoadBl, newmode);
    
    if ((LoadBl < 2 && newmode > 1) || (LoadBl > 1 && newmode < 2) || (newmode == 255) ) {
        
        if (newmode != 255 ) LoadBl = newmode;

        // Setup Modbus workers for Node
        if (LoadBl > 1 ) {
            
            _LOG_A("Setup MBserver/Node workers, end Master/Client\n");
            // Stop Master background task (if active)
            if (newmode != 255 ) MBclient.end();    
            _LOG_A("ConfigureModbusMode1 task free ram: %u\n", uxTaskGetStackHighWaterMark( NULL ));

            // Register worker. at serverID 'LoadBl', all function codes
            MBserver.registerWorker(LoadBl, ANY_FUNCTION_CODE, &MBNodeRequest);      
            // Also add handler for all broadcast messages from Master.
            MBserver.registerWorker(BROADCAST_ADR, ANY_FUNCTION_CODE, &MBbroadcast);


            if (MainsMeter && MainsMeter != EM_API) MBserver.registerWorker(MainsMeterAddress, ANY_FUNCTION_CODE, &MBMainsMeterResponse);
            if (EVMeter && EVMeter != EM_API) MBserver.registerWorker(EVMeterAddress, ANY_FUNCTION_CODE, &MBEVMeterResponse);

            // Start ModbusRTU Node background task
            MBserver.begin(Serial1);

        } else if (LoadBl < 2 ) {
            // Setup Modbus workers as Master 
            // Stop Node background task (if active)
            _LOG_A("Setup Modbus as Master/Client, stop Server/Node handler\n");

            if (newmode != 255) MBserver.end();
            _LOG_A("ConfigureModbusMode2 task free ram: %u\n", uxTaskGetStackHighWaterMark( NULL ));

            MBclient.setTimeout(100);       // timeout 100ms
            MBclient.onDataHandler(&MBhandleData);
            MBclient.onErrorHandler(&MBhandleError);

            // Start ModbusRTU Master background task
            MBclient.begin(Serial1);
        } 
    } else if (newmode > 1) {
        // Register worker. at serverID 'LoadBl', all function codes
        _LOG_A("Registering new LoadBl worker at id %u\n", newmode);
        LoadBl = newmode;
        MBserver.registerWorker(newmode, ANY_FUNCTION_CODE, &MBNodeRequest);   
    }
    
}


// Generate random password for AP
void SetRandomAPpassword(void) {
    uint8_t i, c;
    // Set random password
    for (i=0; i<8 ;i++) {
            c = random(16) + '0';
            if (c > '9') c += 'a'-'9'-1;
            APpassword[i] = c;
    }
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
    //    _LOG_A("value %s set to %i\n",MenuStr[i].LCD, value );
        if (value > MenuStr[i].Max || value < MenuStr[i].Min) {
            value = MenuStr[i].Default;
    //        _LOG_A("set default value for %s to %i\n",MenuStr[i].LCD, value );
            setItemValue(i, value);
        }
    }

    // Sensorbox v2 has always address 0x0A
    if (MainsMeter == EM_SENSORBOX) MainsMeterAddress = 0x0A;
    // set Lock variables for Solenoid or Motor
    if (Lock == 1) { lock1 = LOW; lock2 = HIGH; }                               // Solenoid
    else if (Lock == 2) { lock1 = HIGH; lock2 = LOW; }                          // Motor
    // Erase all RFID cards from ram + eeprom if set to EraseAll
    if (RFIDReader == 5) {
       DeleteAllRFID();
    }
    // If mainsmeter disabled we can only run in Normal Mode
    if (!MainsMeter)
        Mode = MODE_NORMAL;

    // It doesnt make sense to have ImportCurrent lower than StartCurrent + MinCurrent,
    // because you will never start charging in Solar Mode
    if (ImportCurrent < StartCurrent + MinCurrent)
        ImportCurrent = StartCurrent + MinCurrent;
    // Update master node config
    if (LoadBl < 2) {
        Node[0].EVMeter = EVMeter;
        Node[0].EVAddress = EVMeterAddress;
    }

    // Check if AP password is unitialized. 
    // Create random AP password.
    if (!Initialized) {
        SetRandomAPpassword();
        Initialized = 1;
    }
          
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

void read_settings() {
    
    if (preferences.begin("settings", false) == true) {
        Initialized = preferences.getUChar("Initialized", INITIALIZED);
        Config = preferences.getUChar("Config", CONFIG); 
        Lock = preferences.getUChar("Lock", LOCK); 
        Mode = preferences.getUChar("Mode", MODE); 
        //first determine default value for Access_bit:
        uint8_t Default_Access_bit = 0;
        // RFID reader set to Enable One card, the EVSE is disabled by default
        if (RFIDReader == 2) Default_Access_bit = 0;
        // Enable access if no access switch used
        else if (Switch != 1 && Switch != 2) Default_Access_bit = 1;
        // Now we know default value, lets read if from memory:
        Access_bit = preferences.getUChar("Access", Default_Access_bit);
        CardOffset = preferences.getUChar("CardOffset", CARD_OFFSET);
        LoadBl = preferences.getUChar("LoadBl", LOADBL); 
        MaxMains = preferences.getUShort("MaxMains", MAX_MAINS); 
        MaxSumMains = preferences.getUShort("MaxSumMains", MAX_SUMMAINS);
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
        DelayedStartTime.epoch2 = preferences.getULong("DelayedStartTim", DELAYEDSTARTTIME); //epoch2 is 4 bytes long on arduino; NVS key has reached max size
        DelayedStopTime.epoch2 = preferences.getULong("DelayedStopTime", DELAYEDSTOPTIME);    //epoch2 is 4 bytes long on arduino
        TZname = preferences.getString("Timezone","Europe/Berlin");

        EnableC2 = (EnableC2_t) preferences.getUShort("EnableC2", ENABLE_C2);
        Modem = (Modem_t) preferences.getUShort("Modem", NOTPRESENT);
        strncpy(RequiredEVCCID, preferences.getString("RequiredEVCCID", "").c_str(), sizeof(RequiredEVCCID));
        maxTemp = preferences.getUShort("maxTemp", MAX_TEMPERATURE);

#if MQTT
        MQTTpassword = preferences.getString("MQTTpassword");
        MQTTuser = preferences.getString("MQTTuser");
        MQTTprefix = preferences.getString("MQTTprefix", APhostname);
        MQTTHost = preferences.getString("MQTTHost", "");
        MQTTPort = preferences.getUShort("MQTTPort", 1883);
#endif

        preferences.end();                                  

        // Store settings when not initialized
        if (!Initialized) write_settings();

    } else {
        _LOG_A("Can not open preferences!\n");
    }
}

void write_settings(void) {

    validate_settings();

 if (preferences.begin("settings", false) ) {

    preferences.putUChar("Config", Config); 
    preferences.putUChar("Lock", Lock); 
    preferences.putUChar("Mode", Mode); 
    preferences.putUChar("Access", Access_bit);
    preferences.putUChar("CardOffset", CardOffset);
    preferences.putUChar("LoadBl", LoadBl); 
    preferences.putUShort("MaxMains", MaxMains); 
    preferences.putUShort("MaxSumMains", MaxSumMains);
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
    preferences.putUChar("Initialized", Initialized);
    preferences.putULong("DelayedStartTim", DelayedStartTime.epoch2); //epoch2 only needs 4 bytes; NVS key has reached max size
    preferences.putULong("DelayedStopTime", DelayedStopTime.epoch2);   //epoch2 only needs 4 bytes

    preferences.putUShort("EnableC2", EnableC2);
    preferences.putUShort("Modem", Modem);
    preferences.putString("RequiredEVCCID", String(RequiredEVCCID));
    preferences.putUShort("maxTemp", maxTemp);

#if MQTT
    preferences.putString("MQTTpassword", MQTTpassword);
    preferences.putString("MQTTuser", MQTTuser);
    preferences.putString("MQTTprefix", MQTTprefix);
    preferences.putString("MQTTHost", MQTTHost);
    preferences.putUShort("MQTTPort", MQTTPort);
#endif

    preferences.end();

    _LOG_I("settings saved\n");

 } else {
     _LOG_A("Can not open preferences!\n");
 }


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

/* Takes TimeString in format
 * String = "2023-04-14T11:31"
 * and store it in the DelayedTimeStruct
 * returns 0 on success, 1 on failure
*/
int StoreTimeString(String DelayedTimeStr, DelayedTimeStruct *DelayedTime) {
    // Parse the time string
    tm delayedtime_tm = {};
    if (strptime(DelayedTimeStr.c_str(), "%Y-%m-%dT%H:%M", &delayedtime_tm)) {
        delayedtime_tm.tm_isdst = -1;                 //so mktime is going to figure out whether DST is there or not
        DelayedTime->epoch2 = mktime(&delayedtime_tm) - EPOCH2_OFFSET;
        // Compare the times
        time_t now = time(nullptr);             //get current local time
        DelayedTime->diff = DelayedTime->epoch2 - (mktime(localtime(&now)) - EPOCH2_OFFSET);
        return 0;
    }
    //error TODO not sure whether we keep the old time or reset it to zero?
    //DelayedTime.epoch2 = 0;
    //DelayedTime.diff = 0;
    return 1;
}

void StartwebServer(void) {

    webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        _LOG_A("page / (root) requested and sent\n");
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
        request->send(200, "text/html", "First flash firmware.bin to update the main firmware.<br>Then flash spiffs.bin to update the SPIFFS partition, which provides the webserver user interface.<br>You should only flash files with those exact names.<br>If you want to telnet to your SmartEVSE to see the debug messages you should rename firmware.debug.bin to firmware.bin and flash that file.<br><form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>");
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
            _LOG_A("\nUpdate Start: %s\n", filename.c_str());
                if (filename == "spiffs.bin" ) {
                    _LOG_A("\nSPIFFS partition write\n");
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
            } else {
                _LOG_A("bytes written %u\r", index+len);
            }
        }
        if(final) {
            if(Update.end(true)) {
                _LOG_A("\nUpdate Success\n");
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

        if (ErrorFlags & NO_SUN) {
            evstate += " - " + error;
            error = "None";
            errorId = 0;
        }

        boolean evConnected = pilot != PILOT_12V;                    //when access bit = 1, p.ex. in OFF mode, the STATEs are no longer updated

        DynamicJsonDocument doc(1600); // https://arduinojson.org/v6/assistant/
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
        }
        
        doc["evse"]["temp"] = TempEVSE;
        doc["evse"]["temp_max"] = maxTemp;
        doc["evse"]["connected"] = evConnected;
        doc["evse"]["access"] = Access_bit == 1;
        doc["evse"]["mode"] = Mode;
        doc["evse"]["loadbl"] = LoadBl;
        doc["evse"]["pwm"] = CurrentPWM;
        doc["evse"]["solar_stop_timer"] = SolarStopTimer;
        doc["evse"]["state"] = evstate;
        doc["evse"]["state_id"] = State;
        doc["evse"]["error"] = error;
        doc["evse"]["error_id"] = errorId;
        doc["evse"]["rfid"] = !RFIDReader ? "Not Installed" : RFIDstatus >= 8 ? "NOSTATUS" : StrRFIDStatusWeb[RFIDstatus];

        doc["settings"]["charge_current"] = Balanced[0];
        doc["settings"]["override_current"] = OverrideCurrent;
        doc["settings"]["current_min"] = MinCurrent;
        doc["settings"]["current_max"] = MaxCurrent;
        doc["settings"]["current_main"] = MaxMains;
        doc["settings"]["current_max_circuit"] = MaxCircuit;
        doc["settings"]["current_max_sum_mains"] = MaxSumMains;
        doc["settings"]["solar_max_import"] = ImportCurrent;
        doc["settings"]["solar_start_current"] = StartCurrent;
        doc["settings"]["solar_stop_time"] = StopTime;
        doc["settings"]["enable_C2"] = StrEnableC2[EnableC2];
        doc["settings"]["modem"] = StrModem[Modem];
        doc["settings"]["mains_meter"] = EMConfig[MainsMeter].Desc;
        doc["settings"]["starttime"] = (DelayedStartTime.epoch2 ? DelayedStartTime.epoch2 + EPOCH2_OFFSET : 0);
        doc["settings"]["stoptime"] = (DelayedStopTime.epoch2 ? DelayedStopTime.epoch2 + EPOCH2_OFFSET : 0);
        doc["settings"]["repeat"] = DelayedRepeat;
        if (Modem) {
            doc["settings"]["required_evccid"] = RequiredEVCCID;
            doc["ev_state"]["initial_soc"] = InitialSoC;
            doc["ev_state"]["remaining_soc"] = RemainingSoC;
            doc["ev_state"]["full_soc"] = FullSoC;
            doc["ev_state"]["energy_capacity"] = EnergyCapacity > 0 ? round(EnergyCapacity / 100)/10 : -1; //in kWh, precision 1 decimal;
            doc["ev_state"]["energy_request"] = EnergyRequest > 0 ? round(EnergyRequest / 100)/10 : -1; //in kWh, precision 1 decimal
            doc["ev_state"]["computed_soc"] = ComputedSoC;
            doc["ev_state"]["evccid"] = EVCCID;
            doc["ev_state"]["time_until_full"] = TimeUntilFull;
        }

#if MQTT
        doc["mqtt"]["host"] = MQTTHost;
        doc["mqtt"]["port"] = MQTTPort;
        doc["mqtt"]["topic_prefix"] = MQTTprefix;
        doc["mqtt"]["username"] = MQTTuser;
        doc["mqtt"]["password_set"] = MQTTpassword != "";

        if (MQTTclient.connected()) {
            doc["mqtt"]["status"] = "Connected";
        } else {
            doc["mqtt"]["status"] = "Disconnected";
        }
#endif

        doc["home_battery"]["current"] = homeBatteryCurrent;
        doc["home_battery"]["last_update"] = homeBatteryLastUpdate;

        doc["ev_meter"]["description"] = EMConfig[EVMeter].Desc;
        doc["ev_meter"]["address"] = EVMeterAddress;
        doc["ev_meter"]["import_active_power"] = round(PowerMeasured / 100)/10; //in kW, precision 1 decimal
        doc["ev_meter"]["total_kwh"] = round(EnergyEV / 100)/10; //in kWh, precision 1 decimal
        doc["ev_meter"]["charged_kwh"] = round(EnergyCharged / 100)/10; //in kWh, precision 1 decimal
        doc["ev_meter"]["currents"]["TOTAL"] = Irms_EV[0] + Irms_EV[1] + Irms_EV[2];
        doc["ev_meter"]["currents"]["L1"] = Irms_EV[0];
        doc["ev_meter"]["currents"]["L2"] = Irms_EV[1];
        doc["ev_meter"]["currents"]["L3"] = Irms_EV[2];
        doc["ev_meter"]["import_active_energy"] = round(EV_import_active_energy / 100)/10; //in kWh, precision 1 decimal
        doc["ev_meter"]["export_active_energy"] = round(EV_export_active_energy / 100)/10; //in kWh, precision 1 decimal

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
            int backlight = request->getParam("backlight")->value().toInt();
            BacklightTimer = backlight * BACKLIGHT;
            doc["Backlight"] = backlight;
        }

        if(request->hasParam("current_min")) {
            int current = request->getParam("current_min")->value().toInt();
            if(current >= 6 && current <= 16 && LoadBl < 2) {
                MinCurrent = current;
                doc["current_min"] = MinCurrent;
                write_settings();
            } else {
                doc["current_min"] = "Value not allowed!";
            }
        }

        if(request->hasParam("current_max_sum_mains")) {
            int current = request->getParam("current_max_sum_mains")->value().toInt();
            if(current >= 10 && current <= 600 && LoadBl < 2) {
                MaxSumMains = current;
                doc["current_max_sum_mains"] = MaxSumMains;
                write_settings();
            } else {
                doc["current_max_sum_mains"] = "Value not allowed!";
            }
        }

        if(request->hasParam("disable_override_current")) {
            OverrideCurrent = 0;
            doc["disable_override_current"] = "OK";
        }

        if(request->hasParam("mode")) {
            String mode = request->getParam("mode")->value();

            //first check if we have a delayed mode switch
            if(request->hasParam("starttime")) {
                String DelayedStartTimeStr = request->getParam("starttime")->value();
                //string time_str = "2023-04-14T11:31";
                if (!StoreTimeString(DelayedStartTimeStr, &DelayedStartTime)) {
                    //parse OK
                    if (DelayedStartTime.diff > 0)
                        setAccess(0);                         //switch to OFF, we are Delayed Charging
                    else {//we are in the past so no delayed charging
                        DelayedStartTime.epoch2 = DELAYEDSTARTTIME;
                        DelayedStopTime.epoch2 = DELAYEDSTOPTIME;
                        DelayedRepeat = 0;
                    }
                }
                else {
                    //we couldn't parse the string, so we are NOT Delayed Charging
                    DelayedStartTime.epoch2 = DELAYEDSTARTTIME;
                    DelayedStopTime.epoch2 = DELAYEDSTOPTIME;
                    DelayedRepeat = 0;
                }

                // so now we might have a starttime and we might be Delayed Charging
                if (DelayedStartTime.epoch2) {
                    //we only accept a DelayedStopTime if we have a valid DelayedStartTime
                    if(request->hasParam("stoptime")) {
                        String DelayedStopTimeStr = request->getParam("stoptime")->value();
                        //string time_str = "2023-04-14T11:31";
                        if (!StoreTimeString(DelayedStopTimeStr, &DelayedStopTime)) {
                            //parse OK
                            if (DelayedStopTime.diff <= 0 || DelayedStopTime.epoch2 <= DelayedStartTime.epoch2)
                                //we are in the past or DelayedStopTime before DelayedStartTime so no DelayedStopTime
                                DelayedStopTime.epoch2 = DELAYEDSTOPTIME;
                        }
                        else
                            //we couldn't parse the string, so no DelayedStopTime
                            DelayedStopTime.epoch2 = DELAYEDSTOPTIME;
                        doc["stoptime"] = (DelayedStopTime.epoch2 ? DelayedStopTime.epoch2 + EPOCH2_OFFSET : 0);
                        if(request->hasParam("repeat")) {
                            int Repeat = request->getParam("repeat")->value().toInt();
                            if (Repeat >= 0 && Repeat <= 1) {                                   //boundary check
                                DelayedRepeat = Repeat;
                                doc["repeat"] = Repeat;
                            }
                        }
                    }

                }
                doc["starttime"] = (DelayedStartTime.epoch2 ? DelayedStartTime.epoch2 + EPOCH2_OFFSET : 0);
            }

            switch(mode.toInt()) {
                case 0: // OFF
                    ToModemWaitStateTimer = 0;
                    ToModemDoneStateTimer = 0;
                    LeaveModemDoneStateTimer = 0;
                    LeaveModemDeniedStateTimer = 0;
                    setAccess(0);
                    break;
                case 1:
                    setMode(MODE_NORMAL);
                    break;
                case 2:
                    setMode(MODE_SOLAR);
                    break;
                case 3:
                    setMode(MODE_SMART);
                    break;
                default:
                    mode = "Value not allowed!";
            }
            doc["mode"] = mode;
        }

        if(request->hasParam("enable_C2")) {
            EnableC2 = (EnableC2_t) request->getParam("enable_C2")->value().toInt();
            write_settings();
            doc["settings"]["enable_C2"] = StrEnableC2[EnableC2];
        }

        if(request->hasParam("modem")) {
            Modem = (Modem_t) request->getParam("modem")->value().toInt();
            doc["settings"]["modem"] = StrModem[Modem];
        }

        if(request->hasParam("stop_timer")) {
            int stop_timer = request->getParam("stop_timer")->value().toInt();

            if(stop_timer >= 0 && stop_timer <= 60) {
                StopTime = stop_timer;
                doc["stop_timer"] = true;
                write_settings();
            } else {
                doc["stop_timer"] = false;
            }

        }

        if(Mode == MODE_NORMAL || Mode == MODE_SMART) {
            if(request->hasParam("override_current")) {
                int current = request->getParam("override_current")->value().toInt();
                if (LoadBl < 2 && (current == 0 || (current >= ( MinCurrent * 10 ) && current <= ( MaxCurrent * 10 )))) { //OverrideCurrent not possible on Slave
                    OverrideCurrent = current;
                    doc["override_current"] = OverrideCurrent;
                } else {
                    doc["override_current"] = "Value not allowed!";
                }
            }
        }

        if(request->hasParam("solar_start_current")) {
            int current = request->getParam("solar_start_current")->value().toInt();
            if(current >= 0 && current <= 48) {
                StartCurrent = current;
                doc["solar_start_current"] = StartCurrent;
                write_settings();
            } else {
                doc["solar_start_current"] = "Value not allowed!";
            }
        }

        if(request->hasParam("solar_max_import")) {
            int current = request->getParam("solar_max_import")->value().toInt();
            if(current >= 0 && current <= 48) {
                ImportCurrent = current;
                doc["solar_max_import"] = ImportCurrent;
                write_settings();
            } else {
                doc["solar_max_import"] = "Value not allowed!";
            }
        }

        //special section to post stuff for experimenting with an ISO15118 modem
        if(request->hasParam("override_pwm")) {
            int pwm = request->getParam("override_pwm")->value().toInt();
            if (pwm == 0){
                CP_OFF;
                CPDutyOverride = true;
            } else if (pwm < 0){
                CP_ON;
                CPDutyOverride = false;
                pwm = 100; // 10% until next loop, to be safe, corresponds to 6A
            } else{
                CP_ON;
                CPDutyOverride = true;
            }

            SetCPDuty(pwm);
            doc["override_pwm"] = pwm;
        }

        //allow basic plug 'n charge based on evccid
        //if required_evccid is set to a value, SmartEVSE will only allow charging requests from said EVCCID
        if(request->hasParam("required_evccid")) {
            if (request->getParam("required_evccid")->value().length() <= 32) {
                strncpy(RequiredEVCCID, request->getParam("required_evccid")->value().c_str(), sizeof(RequiredEVCCID));
                doc["required_evccid"] = RequiredEVCCID;
                write_settings();
            } else {
                doc["required_evccid"] = "EVCCID too long (max 32 char)";
            }
        }

#if MQTT
        if(request->hasParam("mqtt_update")) {
            if (request->getParam("mqtt_update")->value().toInt() == 1) {

                if(request->hasParam("mqtt_host")) {
                    MQTTHost = request->getParam("mqtt_host")->value();
                    doc["mqtt_host"] = MQTTHost;
                }

                if(request->hasParam("mqtt_port")) {
                    MQTTPort = request->getParam("mqtt_port")->value().toInt();
                    if (MQTTPort == 0) MQTTPort = 1883;
                    doc["mqtt_port"] = MQTTPort;
                }

                if(request->hasParam("mqtt_topic_prefix")) {
                    MQTTprefix = request->getParam("mqtt_topic_prefix")->value();
                    if (!MQTTprefix || MQTTprefix == "") {
                        MQTTprefix = APhostname;
                    }
                    doc["mqtt_topic_prefix"] = MQTTprefix;
                }

                if(request->hasParam("mqtt_username")) {
                    MQTTuser = request->getParam("mqtt_username")->value();
                    if (!MQTTuser || MQTTuser == "") {
                        MQTTuser.clear();
                    }
                    doc["mqtt_username"] = MQTTuser;
                }

                if(request->hasParam("mqtt_password")) {
                    MQTTpassword = request->getParam("mqtt_password")->value();
                    if (!MQTTpassword || MQTTpassword == "") {
                        MQTTpassword.clear();
                    }
                    doc["mqtt_password_set"] = (MQTTpassword != "");
                }

                SetupMQTTClient();
                write_settings();
            }
        }
#endif

        String json;
        serializeJson(doc, json);

        request->send(200, "application/json", json);
    },[](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    });

    webServer.on("/currents", HTTP_POST, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(200);

        if(request->hasParam("battery_current")) {
            if (LoadBl < 2) {
                homeBatteryCurrent = request->getParam("battery_current")->value().toInt();
                homeBatteryLastUpdate = time(NULL);
                doc["battery_current"] = homeBatteryCurrent;
            } else
                doc["battery_current"] = "not allowed on slave";
        }

        if(MainsMeter == EM_API) {
            if(request->hasParam("L1") && request->hasParam("L2") && request->hasParam("L3")) {
                if (LoadBl < 2) {
                    Irms[0] = request->getParam("L1")->value().toInt();
                    Irms[1] = request->getParam("L2")->value().toInt();
                    Irms[2] = request->getParam("L3")->value().toInt();

                    CalcIsum();
                    for (int x = 0; x < 3; x++) {
                        doc["original"]["L" + x] = IrmsOriginal[x];
                        doc["L" + x] = Irms[x];
                    }
                    doc["TOTAL"] = Isum;

                    timeout = COMM_TIMEOUT;

                    UpdateCurrentData();
                } else
                    doc["TOTAL"] = "not allowed on slave";
            }
        }

        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);

    },[](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    });

    webServer.on("/ev_meter", HTTP_POST, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(200);

        if(EVMeter == EM_API) {
            if(request->hasParam("L1") && request->hasParam("L2") && request->hasParam("L3")) {

                Irms_EV[0] = request->getParam("L1")->value().toInt();
                Irms_EV[1] = request->getParam("L2")->value().toInt();
                Irms_EV[2] = request->getParam("L3")->value().toInt();

                if (LoadBl < 2) timeout = COMM_TIMEOUT;

                UpdateCurrentData();
            }

            if(request->hasParam("import_active_energy") && request->hasParam("export_active_energy") && request->hasParam("import_active_power")) {

                EV_import_active_energy = request->getParam("import_active_energy")->value().toInt();
                EV_export_active_energy = request->getParam("export_active_energy")->value().toInt();

                PowerMeasured = request->getParam("import_active_power")->value().toInt();
                
                EnergyEV = EV_import_active_energy - EV_export_active_energy;
                if (ResetKwh == 2) EnergyMeterStart = EnergyEV;                 // At powerup, set EnergyEV to kwh meter value
                EnergyCharged = EnergyEV - EnergyMeterStart;                    // Calculate Energy
                if (Modem)
                    RecomputeSoC();
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

    webServer.on("/ev_state", HTTP_POST, [](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(200);

        //State of charge posting
        int current_soc = request->getParam("current_soc")->value().toInt();
        int full_soc = request->getParam("full_soc")->value().toInt();

        // Energy requested by car
        int energy_request = request->getParam("energy_request")->value().toInt();

        // Total energy capacity of car's battery
        int energy_capacity = request->getParam("energy_capacity")->value().toInt();

        // Update EVCCID of car
        if (request->hasParam("evccid")) {
            if (request->getParam("evccid")->value().length() <= 32) {
                strncpy(EVCCID, request->getParam("evccid")->value().c_str(), sizeof(EVCCID));
                doc["evccid"] = EVCCID;
            }
        }

        if (full_soc >= FullSoC) // Only update if we received it, since sometimes it's there, sometimes it's not
            FullSoC = full_soc;

        if (energy_capacity >= EnergyCapacity) // Only update if we received it, since sometimes it's there, sometimes it's not
            EnergyCapacity = energy_capacity;

        if (energy_request >= EnergyRequest) // Only update if we received it, since sometimes it's there, sometimes it's not
            EnergyRequest = energy_request;

        if (current_soc >= 0 && current_soc <= 100) {
            // We set the InitialSoC for our own calculations
            InitialSoC = current_soc;

            // We also set the ComputedSoC to allow for app integrations
            ComputedSoC = current_soc;

            // Skip waiting, charge since we have what we've got
            if (State == STATE_MODEM_REQUEST || State == STATE_MODEM_WAIT || State == STATE_MODEM_DONE){
                _LOG_A("Received SoC via REST. Shortcut to State Modem Done\n");
                setState(STATE_MODEM_DONE); // Go to State B, which means in this case setting PWM
            }
        }

        RecomputeSoC();

        doc["current_soc"] = current_soc;
        doc["full_soc"] = full_soc;
        doc["energy_capacity"] = energy_capacity;
        doc["energy_request"] = energy_request;

        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    },[](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    });

#if FAKE_RFID
    //this can be activated by: http://smartevse-xxx.lan/debug?showrfid=1
    webServer.on("/debug", HTTP_GET, [](AsyncWebServerRequest *request) {
        if(request->hasParam("showrfid")) {
            Show_RFID = strtol(request->getParam("showrfid")->value().c_str(),NULL,0);
        }
        _LOG_A("DEBUG: Show_RFID=%u.\n",Show_RFID);
        request->send(200, "text/html", "Finished request");
    });
#endif

#if AUTOMATED_TESTING
    //this can be activated by: http://smartevse-xxx.lan/automated_testing?current_max=100
    //WARNING: because of automated testing, no limitations here!
    //THAT IS DANGEROUS WHEN USED IN PRODUCTION ENVIRONMENT
    //FOR SMARTEVSE's IN A TESTING BENCH ONLY!!!!
    webServer.on("/automated_testing", HTTP_POST, [](AsyncWebServerRequest *request) {
        if(request->hasParam("current_max")) {
            MaxCurrent = strtol(request->getParam("current_max")->value().c_str(),NULL,0);
        }
        if(request->hasParam("current_main")) {
            MaxMains = strtol(request->getParam("current_main")->value().c_str(),NULL,0);
        }
        if(request->hasParam("current_max_circuit")) {
            MaxCircuit = strtol(request->getParam("current_max_circuit")->value().c_str(),NULL,0);
        }
        if(request->hasParam("mainsmeter")) {
            MainsMeter = strtol(request->getParam("mainsmeter")->value().c_str(),NULL,0);
        }
        if(request->hasParam("evmeter")) {
            EVMeter = strtol(request->getParam("evmeter")->value().c_str(),NULL,0);
        }
        if(request->hasParam("config")) {
            Config = strtol(request->getParam("config")->value().c_str(),NULL,0);
            setState(STATE_A);                                                  // so the new value will actually be read
        }
        if(request->hasParam("loadbl")) {
            int LBL = strtol(request->getParam("loadbl")->value().c_str(),NULL,0);
            ConfigureModbusMode(LBL);
            LoadBl = LBL;
        }
        request->send(200, "text/html", "Finished request");
    },[](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    });
#endif

    // attach filesystem root at URL /
    webServer.serveStatic("/", SPIFFS, "/");

    // setup 404 handler 'onRequest'
    webServer.onNotFound(onRequest);

    // Setup async webserver
    webServer.begin();
    _LOG_A("HTTP server started\n");

}

void onWifiEvent(WiFiEvent_t event) {
    switch (event) {
        case WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP:
            _LOG_A("Connected to AP: %s\nLocal IP: %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
            break;
        case WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED:
            _LOG_A("Connected or reconnected to WiFi\n");
            break;
        case WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            if (WIFImode == 1) {
                _LOG_A("WiFi Disconnected. Reconnecting...\n");
                //WiFi.setAutoReconnect(true);  //I know this is very counter-intuitive, you would expect this line in WiFiSetup but this is according to docs
                                                //look at: https://github.com/alanswx/ESPAsyncWiFiManager/issues/92
                                                //but somehow it doesnt work reliably, depending on how the disconnect happened...
                WiFi.reconnect();               //this works better!
            }
            break;
        default: break;
  }
}

// turns out getLocalTime only checks if the current year > 2016, and if so, decides NTP must have synced;
// this callback function actually checks if we are synced!
void timeSyncCallback(struct timeval *tv)
{
    LocalTimeSet = true;
    _LOG_A("Synced clock to NTP server!");    // somehow adding a \n here hangs the device after printing this message ?!?
}


// Setup Wifi 
void WiFiSetup(void) {

    //ESPAsync_wifiManager.resetSettings();   //reset saved settings
    //ESPAsync_wifiManager.setDebugOutput(true);
    ESPAsync_wifiManager.setMinimumSignalQuality(-1);

    // Start the mDNS responder so that the SmartEVSE can be accessed using a local hostame: http://SmartEVSE-xxxxxx.local
    if (!MDNS.begin(APhostname.c_str())) {                
        _LOG_A("Error setting up MDNS responder!\n");
    } else {
        _LOG_A("mDNS responder started. http://%s.local\n",APhostname.c_str());
        MDNS.addService("http", "tcp", 80);   // announce Web server
    }

    //WiFi.setAutoReconnect(true);
    //WiFi.persistent(true);
    WiFi.onEvent(onWifiEvent);

    // Init and get the time
    sntp_servermode_dhcp(1);                                                    //try to get the ntp server from dhcp
    sntp_setservername(1, "europe.pool.ntp.org");                               //fallback server
    sntp_set_time_sync_notification_cb(timeSyncCallback);
    sntp_init();
    String TZ_INFO = ESPAsync_wifiManager.getTZ(TZname.c_str());
    setenv("TZ",TZ_INFO.c_str(),1);
    tzset();

#if DBG == 1
    // Initialize the server (telnet or web socket) of RemoteDebug
    Debug.begin(APhostname, 23, 1);
    Debug.showColors(true); // Colors
#endif
#if DBG == 2
    Debug.setSerialEnabled(true); // if you wants serial echo - only recommended if ESP is plugged in USB
#endif
    handleWIFImode();                                                           //go into the mode that was saved in nonvolatile memory
    StartwebServer();
}

void SetupPortalTask(void * parameter) {
    _LOG_A("Start Portal...\n");
    StopwebServer();
    WiFi.disconnect(true);
    // Set config portal channel, default = 1. Use 0 => random channel from 1-13
    ESPAsync_wifiManager.setConfigPortalChannel(0);
    ESPAsync_wifiManager.setAPStaticIPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));

    ESPAsync_wifiManager.setConfigPortalTimeout(120);   // Portal will be available 2 minutes to connect to, then close. (if connected within this time, it will remain active)
    delay(1000);
    ESPAsync_wifiManager.startConfigPortal(APhostname.c_str(), APpassword.c_str());         // blocking until connected or timeout.
    //_LOG_A("SetupPortalTask free ram: %u\n", uxTaskGetStackHighWaterMark( NULL ));
    WiFi.disconnect(true);

    // this function only works in portal mode, so we have to save the timezone:
    TZname = ESPAsync_wifiManager.getTimezoneName();
    if (preferences.begin("settings", false) ) {
        preferences.putString("Timezone",TZname);
        preferences.end();
    }

    WIFImode = 1;
    handleWIFImode();
    write_settings();
    LCDNav = 0;
    StartwebServer();                                                           //restart webserver
    vTaskDelete(NULL);                                                          //end this task so it will not take up resources
}

void handleWIFImode() {

    if (WIFImode == 2 && WiFi.getMode() != WIFI_AP_STA)
        //now start the portal in the background, so other tasks keep running
        xTaskCreate(
            SetupPortalTask,     // Function that should be called
            "SetupPortalTask",   // Name of the task (for debugging)
            10000,                // Stack size (bytes)                              // printf needs atleast 1kb
            NULL,                 // Parameter to pass
            1,                    // Task priority
            NULL                  // Task handleCTReceive
        );

    if (WIFImode == 1 && WiFi.getMode() == WIFI_OFF) {
        _LOG_A("Starting WiFi..\n");
        WiFi.mode(WIFI_STA);
        WiFi.begin();
    }    

    if (WIFImode == 0 && WiFi.getMode() != WIFI_OFF) {
        _LOG_A("Stopping WiFi..\n");
        WiFi.disconnect(true);
    }    
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
    digitalWrite(PIN_SSR, LOW);             // SSR1 OFF
    digitalWrite(PIN_SSR2, LOW);            // SSR2 OFF
    digitalWrite(PIN_LCD_LED, HIGH);        // LCD Backlight ON
    CP_OFF;           // CP signal OFF

 
    // Uart 0 debug/program port
    Serial.begin(115200);
    while (!Serial);
    _LOG_A("\nSmartEVSE v3 powerup\n");

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

    SetCPDuty(1024);                            // channel 0, duty cycle 100%
    ledcWrite(RED_CHANNEL, 255);
    ledcWrite(GREEN_CHANNEL, 0);
    ledcWrite(BLUE_CHANNEL, 255);
    ledcWrite(LCD_CHANNEL, 0);

    // Setup PIN interrupt on rising edge
    // the timer interrupt will be reset in the ISR.
    attachInterrupt(PIN_CP_OUT, onCPpulse, RISING);   
   
    // Uart 1 is used for Modbus @ 9600 8N1
    RTUutils::prepareHardwareSerial(Serial1);
    Serial1.begin(MODBUS_BAUDRATE, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);

   
    //Check type of calibration value used to characterize ADC
    _LOG_A("Checking eFuse Vref settings: ");
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        _LOG_A("OK\n");
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        _LOG_A("Two Point\n");
    } else {
        _LOG_A("not programmed!!!\n");
    }
    
    // Initialize SPIFFS
    if (!SPIFFS.begin(true)) {
        _LOG_A("SPIFFS failed! Already tried formatting. HALT\n");
        while (true) {
          delay(1);
        }
    }
    _LOG_A("Total SPIFFS bytes: %u, Bytes used: %u\n",SPIFFS.totalBytes(),SPIFFS.usedBytes());


   // Read all settings from non volatile memory
    read_settings();                                                            // initialize with default data when starting for the first time
    validate_settings();
    ReadRFIDlist();                                                             // Read all stored RFID's from storage
    _LOG_A("APpassword: %s\n",APpassword.c_str());

    // We might need some sort of authentication in the future.
    // SmartEVSE v3 have programmed ECDSA-256 keys stored in nvs
    // Unused for now.
    if (preferences.begin("KeyStorage", true) == true) {                        // readonly
//prevent compiler warning
#if DBG != 0
        uint16_t hwversion = preferences.getUShort("hwversion");                // 0x0101 (01 = SmartEVSE,  01 = hwver 01)
#endif
        serialnr = preferences.getUInt("serialnr");      
        String ec_private = preferences.getString("ec_private");
        String ec_public = preferences.getString("ec_public");
        preferences.end(); 

        // overwrite APhostname if serialnr is programmed
        APhostname = "SmartEVSE-" + String( serialnr & 0xffff, 10);           // SmartEVSE access point Name = SmartEVSE-xxxxx
        _LOG_A("hwversion %04x serialnr:%u \n",hwversion, serialnr);
        //_LOG_A(ec_public);

    } else {
        _LOG_A("No KeyStorage found in nvs!\n");
    }


    // Create Task EVSEStates, that handles changes in the CP signal
    xTaskCreate(
        EVSEStates,     // Function that should be called
        "EVSEStates",   // Name of the task (for debugging)
        4096,           // Stack size (bytes)                              // printf needs atleast 1kb
        NULL,           // Parameter to pass
        5,              // Task priority - high
        NULL            // Task handle
    );

    // Create Task BlinkLed (10ms)
    xTaskCreate(
        BlinkLed,       // Function that should be called
        "BlinkLed",     // Name of the task (for debugging)
        1024,           // Stack size (bytes)                              // printf needs atleast 1kb
        NULL,           // Parameter to pass
        1,              // Task priority - low
        NULL            // Task handle
    );

    // Create Task 100ms Timer
    xTaskCreate(
        Timer100ms,     // Function that should be called
        "Timer100ms",   // Name of the task (for debugging)
        4608,           // Stack size (bytes)
        NULL,           // Parameter to pass
        3,              // Task priority - medium
        NULL            // Task handle
    );

    // Create Task Second Timer (1000ms)
    xTaskCreate(
        Timer1S,        // Function that should be called
        "Timer1S",      // Name of the task (for debugging)
        4096,           // Stack size (bytes)                              
        NULL,           // Parameter to pass
        3,              // Task priority - medium
        NULL            // Task handle
    );

    // Setup WiFi, webserver and firmware OTA
    // Please be aware that after doing a OTA update, its possible that the active partition is set to OTA1.
    // Uploading a new firmware through USB will however update OTA0, and you will not notice any changes...
    WiFiSetup();

    // Set eModbus LogLevel to 1, to suppress possible E5 errors
    MBUlogLvl = LOG_LEVEL_CRITICAL;
    ConfigureModbusMode(255);
  
    BacklightTimer = BACKLIGHT;
    GLCD_init();

    CP_ON;           // CP signal ACTIVE

#if MQTT
    // Setup MQTT client
    MQTTclient.begin(client);
    SetupMQTTClient();
#endif

}

void loop() {

    delay(1000);
    getLocalTime(&timeinfo, 1000U);
    if (!LocalTimeSet) {
        _LOG_A("Time not synced with NTP yet.\n");
    }

#ifndef DEBUG_DISABLED
    // Remote debug over WiFi
    Debug.handle();
#endif

    // TODO move this to a once a minute loop?
    if (DelayedStartTime.epoch2 && LocalTimeSet) {
        // Compare the times
        time_t now = time(nullptr);             //get current local time
        DelayedStartTime.diff = DelayedStartTime.epoch2 - (mktime(localtime(&now)) - EPOCH2_OFFSET);
        if (DelayedStartTime.diff > 0) {
            if (Access_bit != 0 && (DelayedStopTime.epoch2 == 0 || DelayedStopTime.epoch2 > DelayedStartTime.epoch2))
                setAccess(0);                         //switch to OFF, we are Delayed Charging
        }
        else {
            //starttime is in the past so we are NOT Delayed Charging, or we are Delayed Charging but the starttime has passed!
            if (DelayedRepeat == 1)
                DelayedStartTime.epoch2 += 24 * 3600;                           //add 24 hours so we now have a new starttime
            else
                DelayedStartTime.epoch2 = DELAYEDSTARTTIME;
            setAccess(1);
        }
    }
    //only update StopTime.diff if starttime has already passed
    if (DelayedStopTime.epoch2 && LocalTimeSet) {
        // Compare the times
        time_t now = time(nullptr);             //get current local time
        DelayedStopTime.diff = DelayedStopTime.epoch2 - (mktime(localtime(&now)) - EPOCH2_OFFSET);
        if (DelayedStopTime.diff <= 0) {
            //DelayedStopTime has passed
            if (DelayedRepeat == 1)                                         //we are on a daily repetition schedule
                DelayedStopTime.epoch2 += 24 * 3600;                        //add 24 hours so we now have a new starttime
            else
                DelayedStopTime.epoch2 = DELAYEDSTOPTIME;
            setAccess(0);                         //switch to OFF
        }
    }
}

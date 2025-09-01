/*
 * This file has shared code between SmartEVSE-3, SmartEVSE-4 and SmartEVSE-4_CH32
 * #if SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40  //SmartEVSEv3 code
 * #if SMARTEVSE_VERSION >= 40  //SmartEVSEv4 code
 * #ifndef SMARTEVSE_VERSION   //CH32 code
 */

//prevent MQTT compiling on CH32
#if defined(MQTT) && !defined(ESP32)
#error "MQTT requires ESP32 to be defined!"
#endif

#include "main.h"
#include "stdio.h"
#include "stdlib.h"
#include "meter.h"
#include "modbus.h"
#include "memory.h"  //for memcpy
#include <time.h>

#ifdef SMARTEVSE_VERSION //ESP32
#define EXT extern
#define _GLCD GLCD()
#include "esp32.h"
#include <ArduinoJson.h>
#include <SPI.h>
#include <Preferences.h>

#include <FS.h>

#include <WiFi.h>
#include "network_common.h"
#include "esp_ota_ops.h"
#include "mbedtls/md_internal.h"

#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <Update.h>

#include <Logging.h>
#include <ModbusServerRTU.h>        // Slave/node
#include <ModbusClientRTU.h>        // Master

#include <soc/sens_reg.h>
#include <soc/sens_struct.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>

//OCPP includes
#if ENABLE_OCPP && defined(SMARTEVSE_VERSION) //run OCPP only on ESP32
#include <MicroOcpp.h>
#include <MicroOcppMongooseClient.h>
#include <MicroOcpp/Core/Configuration.h>
#include <MicroOcpp/Core/Context.h>
#endif //ENABLE_OCPP

extern Preferences preferences;
struct DelayedTimeStruct DelayedStartTime;
struct DelayedTimeStruct DelayedStopTime;
extern unsigned char RFID[8];
extern uint16_t LCDPin;
extern uint8_t PIN_SW_IN, PIN_ACTA, PIN_ACTB, PIN_RCM_FAULT; //these pins have to be assigned dynamically because of hw version v3.1
#else //CH32
#define EXT extern "C"
#define _GLCD                                                                   // the GLCD doesnt have to be updated on the CH32
#include "ch32.h"
#include "utils.h"
extern "C" {
    #include "ch32v003fun.h"
    void RCmonCtrl(uint8_t enable);
    void delay(uint32_t ms);
    void testRCMON(void);
}
extern void CheckRS485Comm(void);
#endif


// Global data

#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=40   //CH32 and v4 ESP32
#if SMARTEVSE_VERSION >= 40 //v4 ESP32
#define RETURN return;
#define CHIP "ESP32"
extern void RecomputeSoC(void);
extern uint8_t modem_state;
#include <qca.h>
#else
#define RETURN
#define CHIP "CH32"
#endif

//CALL_ON_RECEIVE(setStatePowerUnavailable) setStatePowerUnavailable() when setStatePowerUnavailable is received
#define CALL_ON_RECEIVE(X) \
    ret = strstr(SerialBuf, #X);\
    if (ret) {\
/*        printf("@MSG: %s DEBUG CALL_ON_RECEIVE: calling %s().\n", CHIP, #X); */ \
        X();\
        RETURN \
    }

//CALL_ON_RECEIVE_PARAM(State:, setState) calls setState(param) when State:param is received
#define CALL_ON_RECEIVE_PARAM(X,Y) \
    ret = strstr(SerialBuf, #X);\
    if (ret) {\
/*        printf("@MSG: %s DEBUG CALL_ON_RECEIVE_PARAM: calling %s(%u).\n", CHIP, #X, atoi(ret+strlen(#X))); */ \
        Y(atoi(ret+strlen(#X)));\
        RETURN \
    }
//SET_ON_RECEIVE(Pilot:, pilot) sets pilot=parm when Pilot:param is received
#define SET_ON_RECEIVE(X,Y) \
    ret = strstr(SerialBuf, #X);\
    if (ret) {\
/*        printf("@MSG: %s DEBUG SET_ON_RECEIVE: setting %s to %u.\n", CHIP, #Y, atoi(ret+strlen(#X))); */ \
        Y = atoi(ret+strlen(#X));\
        RETURN \
    }

uint8_t RCMTestCounter = 0;                                                     // nr of seconds the RCM test is allowed to take
Charging_Protocol_t Charging_Protocol = IEC; // IEC 61851-1 (low-level signaling through PWM), the others are high-level signalling via the modem
#endif

// The following data will be updated by eeprom/storage data at powerup:
uint16_t MaxMains = MAX_MAINS;                                              // Max Mains Amps (hard limit, limited by the MAINS connection) (A)
uint16_t MaxSumMains = MAX_SUMMAINS;                                        // Max Mains Amps summed over all 3 phases, limit used by EU capacity rate
                                                                            // see https://github.com/serkri/SmartEVSE-3/issues/215
                                                                            // 0 means disabled, allowed value 10 - 600 A
uint8_t MaxSumMainsTime = MAX_SUMMAINSTIME;                                 // Number of Minutes we wait when MaxSumMains is exceeded, before we stop charging
uint16_t MaxSumMainsTimer = 0;
uint16_t GridRelayMaxSumMains = GRID_RELAY_MAX_SUMMAINS;                    // Max Mains Amps summed over all 3 phases, switched by relay provided by energy provider
                                                                            // Meant to obey par 14a of Energy Industry Act, where the provider can switch a device
                                                                            // down to 4.2kW by a relay connected to the "switch" connectors.
                                                                            // you will have to set the "Switch" setting to "GridRelay",
                                                                            // and connect the relay to the switch terminals
                                                                            // When the relay opens its contacts, power will be reduced to 4.2kW
                                                                            // The relay is only allowed on the Master
bool GridRelayOpen = false;                                                 // The read status of the relay
bool CustomButton = false;                                                  // The status of the custom button
bool MqttButtonState = false;                                               // The status of the button send via MQTT
uint16_t MaxCurrent = MAX_CURRENT;                                          // Max Charge current (A)
uint16_t MinCurrent = MIN_CURRENT;                                          // Minimal current the EV is happy with (A)
uint8_t Mode = MODE;                                                        // EVSE mode (0:Normal / 1:Smart / 2:Solar)
uint32_t CurrentPWM = 0;                                                    // Current PWM duty cycle value (0 - 1024)
bool CPDutyOverride = false;
uint8_t Lock = LOCK;                                                        // Cable lock device (0:Disable / 1:Solenoid / 2:Motor)
uint8_t CableLock = CABLE_LOCK;                                             // 0 = Disabled (default), 1 = Enabled; when enabled the cable is locked at all times, when disabled only when STATE != A
uint16_t MaxCircuit = MAX_CIRCUIT;                                          // Max current of the EVSE circuit (A)
uint8_t Config = CONFIG;                                                    // Configuration (0:Socket / 1:Fixed Cable)
uint8_t LoadBl = LOADBL;                                                    // Load Balance Setting (0:Disable / 1:Master / 2-8:Node)
uint8_t Switch = SWITCH;                                                    // External Switch (0:Disable / 1:Access B / 2:Access S / 
                                                                            // 3:Smart-Solar B / 4:Smart-Solar S / 5: Grid Relay
                                                                            // 6:Custom B / 7:Custom S)
                                                                            // B=momentary push <B>utton, S=toggle <S>witch
uint8_t AutoUpdate = AUTOUPDATE;                                            // Automatic Firmware Update (0:Disable / 1:Enable)
uint16_t StartCurrent = START_CURRENT;
uint16_t StopTime = STOP_TIME;
uint16_t ImportCurrent = IMPORT_CURRENT;
uint8_t Grid = GRID;                                                        // Type of Grid connected to Sensorbox (0:4Wire / 1:3Wire )
uint8_t SB2_WIFImode = SB2_WIFI_MODE;                                       // Sensorbox-2 WiFi Mode (0:Disabled / 1:Enabled / 2:Start Portal)
uint8_t RFIDReader = RFID_READER;                                           // RFID Reader (0:Disabled / 1:Enabled / 2:Enable One / 3:Learn / 4:Delete / 5:Delete All / 6: Remote via OCPP)
#if FAKE_RFID
uint8_t Show_RFID = 0;
#endif

EnableC2_t EnableC2 = ENABLE_C2;                                            // Contactor C2
uint16_t maxTemp = MAX_TEMPERATURE;

Meter MainsMeter(MAINS_METER, MAINS_METER_ADDRESS, COMM_TIMEOUT);
Meter EVMeter(EV_METER, EV_METER_ADDRESS, COMM_EVTIMEOUT);
uint8_t Nr_Of_Phases_Charging = 3;                                          // nr of phases
Switch_Phase_t Switching_Phases_C2 = NO_SWITCH;                             // switching phases only used in SOLAR mode with Contactor C2 = AUTO

uint8_t State = STATE_A;
uint8_t ErrorFlags;
uint8_t pilot;

uint16_t MaxCapacity;                                                       // Cable limit (A) (limited by the wire in the charge cable, set automatically, or manually if Config=Fixed Cable)
uint16_t ChargeCurrent;                                                     // Calculated Charge Current (Amps *10)
uint16_t OverrideCurrent = 0;                                               // Temporary assigned current (Amps *10) (modbus)
int16_t Isum = 0;                                                           // Sum of all measured Phases (Amps *10) (can be negative)

// Load Balance variables
int16_t IsetBalanced = 0;                                                   // Max calculated current (Amps *10) available for all EVSE's
uint16_t Balanced[NR_EVSES] = {0, 0, 0, 0, 0, 0, 0, 0};                     // Amps value per EVSE
#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40   //CH32 and v3 ESP32
uint16_t BalancedMax[NR_EVSES] = {0, 0, 0, 0, 0, 0, 0, 0};                  // Max Amps value per EVSE
uint8_t BalancedState[NR_EVSES] = {0, 0, 0, 0, 0, 0, 0, 0};                 // State of all EVSE's 0=not active (state A), 1=charge request (State B), 2= Charging (State C)
uint16_t BalancedError[NR_EVSES] = {0, 0, 0, 0, 0, 0, 0, 0};                // Error state of EVSE

Node_t Node[NR_EVSES] = {                                                        // 0: Master / 1: Node 1 ...
   /*         Config   EV     EV       Min      Used    Charge Interval Solar *          // Interval Time   : last Charge time, reset when not charging
    * Online, Changed, Meter, Address, Current, Phases,  Timer,  Timer, Timer, Mode */   // Min Current     : minimal measured current per phase the EV consumes when starting to charge @ 6A (can be lower then 6A)
    {      1,       0,     0,       0,       0,      0,      0,      0,     0,    0 },   // Used Phases     : detected nr of phases when starting to charge (works with configured EVmeter meter, and might work with sensorbox)
    {      0,       1,     0,       0,       0,      0,      0,      0,     0,    0 },
    {      0,       1,     0,       0,       0,      0,      0,      0,     0,    0 },
    {      0,       1,     0,       0,       0,      0,      0,      0,     0,    0 },    
    {      0,       1,     0,       0,       0,      0,      0,      0,     0,    0 },
    {      0,       1,     0,       0,       0,      0,      0,      0,     0,    0 },
    {      0,       1,     0,       0,       0,      0,      0,      0,     0,    0 },
    {      0,       1,     0,       0,       0,      0,      0,      0,     0,    0 }            
};
void ModbusRequestLoop(void);
uint8_t C1Timer = 0;
uint8_t ModemStage = 0;                                                     // 0: Modem states will be executed when Modem is enabled 1: Modem stages will be skipped, as SoC is already extracted
int8_t DisconnectTimeCounter = -1;                                          // Count for how long we're disconnected, so we can more reliably throw disconnect event. -1 means counter is disabled
uint8_t ToModemWaitStateTimer = 0;                                          // Timer used from STATE_MODEM_REQUEST to STATE_MODEM_WAIT
uint8_t ToModemDoneStateTimer = 0;                                          // Timer used from STATE_MODEM_WAIT to STATE_MODEM_DONE
uint8_t LeaveModemDoneStateTimer = 0;                                       // Timer used from STATE_MODEM_DONE to other, usually STATE_B
uint8_t LeaveModemDeniedStateTimer = 0;                                     // Timer used from STATE_MODEM_DENIED to STATE_B to re-try authentication
uint8_t ModbusRequest = 0;                                                  // Flag to request Modbus information
bool PilotDisconnected = false;
uint8_t PilotDisconnectTime = 0;                                            // Time the Control Pilot line should be disconnected (Sec)
#endif
uint8_t AccessTimer = 0; //FIXME ESP32 vs CH32
int8_t TempEVSE = 0;                                                        // Temperature EVSE in deg C (-50 to +125)
uint8_t ButtonState = 0x07;                                                 // Holds latest push Buttons state (LSB 2:0)
uint8_t OldButtonState = 0x07;                                              // Holds previous push Buttons state (LSB 2:0)
uint8_t LCDNav = 0;
uint8_t SubMenu = 0;
uint8_t ChargeDelay = 0;                                                    // Delays charging at least 60 seconds in case of not enough current available.
uint8_t NoCurrent = 0;                                                      // counts overcurrent situations.
uint8_t TestState = 0;
uint8_t NodeNewMode = 0;
AccessStatus_t AccessStatus = OFF;                                          // 0: OFF, 1: ON, 2: PAUSE
uint8_t ConfigChanged = 0;

uint16_t SolarStopTimer = 0;
#ifdef SMARTEVSE_VERSION //ESP32 v3 and v4
uint8_t RCmon = RC_MON;                                                     // Residual Current Monitor (0:Disable / 1:Enable)
uint8_t DelayedRepeat;                                                      // 0 = no repeat, 1 = daily repeat
uint8_t LCDlock = LCD_LOCK;                                                 // 0 = LCD buttons operational, 1 = LCD buttons disabled
uint16_t BacklightTimer = 0;                                                // Backlight timer (sec)
uint8_t BacklightSet = 0;
uint8_t LCDTimer = 0;
uint16_t CardOffset = CARD_OFFSET;                                          // RFID card used in Enable One mode
uint8_t RFIDstatus = 0;
EXT hw_timer_t * timerA;
esp_adc_cal_characteristics_t * adc_chars_CP;
#endif

uint8_t ActivationMode = 0, ActivationTimer = 0;
volatile uint16_t adcsample = 0;
volatile uint16_t ADCsamples[25];                                           // declared volatile, as they are used in a ISR
volatile uint8_t sampleidx = 0;
char str[20];
extern volatile uint16_t ADC_CP[NUM_ADC_SAMPLES];

int phasesLastUpdate = 0;
bool phasesLastUpdateFlag = false;
int16_t IrmsOriginal[3]={0, 0, 0};
int16_t homeBatteryCurrent = 0;
int homeBatteryLastUpdate = 0; // Time in milliseconds
// set by EXTERNAL logic through MQTT/REST to indicate cheap tariffs ahead until unix time indicated
uint8_t ColorOff[3] = {0, 0, 0};          // off
uint8_t ColorNormal[3] = {0, 255, 0};   // Green
uint8_t ColorSmart[3] = {0, 255, 0};    // Green
uint8_t ColorSolar[3] = {255, 170, 0};    // Orange
uint8_t ColorCustom[3] = {0, 0, 255};    // Blue

//#define FW_UPDATE_DELAY 30        //DINGO TODO                                            // time between detection of new version and actual update in seconds
#define FW_UPDATE_DELAY 3600                                                    // time between detection of new version and actual update in seconds
uint16_t firmwareUpdateTimer = 0;                                               // timer for firmware updates in seconds, max 0xffff = approx 18 hours
                                                                                // 0 means timer inactive
                                                                                // 0 < timer < FW_UPDATE_DELAY means we are in countdown for an actual update
                                                                                // FW_UPDATE_DELAY <= timer <= 0xffff means we are in countdown for checking
                                                                                //                                              whether an update is necessary
#if ENABLE_OCPP && defined(SMARTEVSE_VERSION) //run OCPP only on ESP32
uint8_t OcppMode = OCPP_MODE; //OCPP Client mode. 0:Disable / 1:Enable

unsigned char OcppRfidUuid [7];
size_t OcppRfidUuidLen;
unsigned long OcppLastRfidUpdate;
unsigned long OcppTrackLastRfidUpdate;

bool OcppForcesLock = false;
std::shared_ptr<MicroOcpp::Configuration> OcppUnlockConnectorOnEVSideDisconnect; // OCPP Config for RFID-based transactions: if false, demand same RFID card again to unlock connector
std::shared_ptr<MicroOcpp::Transaction> OcppLockingTx; // Transaction which locks connector until same RFID card is presented again

bool OcppTrackPermitsCharge = false;
bool OcppTrackAccessBit = false;
uint8_t OcppTrackCPvoltage = PILOT_NOK; //track positive part of CP signal for OCPP transaction logic
MicroOcpp::MOcppMongooseClient *OcppWsClient;

float OcppCurrentLimit = -1.f; // Negative value: no OCPP limit defined

unsigned long OcppStopReadingSyncTime; // Stop value synchronization: delay StopTransaction by a few seconds so it reports an accurate energy reading

bool OcppDefinedTxNotification;
MicroOcpp::TxNotification OcppTrackTxNotification;
unsigned long OcppLastTxNotification;
#endif //ENABLE_OCPP

EXT uint32_t elapsedmax, elapsedtime;

//functions
EXT void setup();
EXT void setState(uint8_t NewState);
EXT void setErrorFlags(uint8_t flags);
EXT int8_t TemperatureSensor();
uint8_t OneWireReadCardId();
EXT uint8_t ProximityPin();
EXT void PowerPanicCtrl(uint8_t enable);
EXT uint8_t ReadESPdata(char *buf);

extern void requestEnergyMeasurement(uint8_t Meter, uint8_t Address, bool Export);
extern void requestNodeConfig(uint8_t NodeNr);
extern void requestPowerMeasurement(uint8_t Meter, uint8_t Address, uint16_t PRegister);
extern void requestNodeStatus(uint8_t NodeNr);
extern uint8_t processAllNodeStates(uint8_t NodeNr);
extern void BroadcastCurrent(void);
extern void CheckRFID(void);
extern void mqttPublishData();
extern void DisconnectEvent(void);
extern char EVCCID[32];
extern char RequiredEVCCID[32];
extern bool CPDutyOverride;
extern uint8_t ModbusRequest;
extern unsigned char ease8InOutQuad(unsigned char i);
extern unsigned char triwave8(unsigned char in);

extern const char StrStateName[15][13] = {"A", "B", "C", "D", "COMM_B", "COMM_B_OK", "COMM_C", "COMM_C_OK", "Activate", "B1", "C1", "MODEM_REQ", "MODEM_WAIT", "MODEM_DONE", "MODEM_DENIED"}; //note that the extern is necessary here because the const will point the compiler to internal linkage; https://cplusplus.com/forum/general/81640/
extern const char StrEnableC2[5][12] = { "Not present", "Always Off", "Solar Off", "Always On", "Auto" };

//TODO perhaps move those routines from modbus to main?
extern void ReadItemValueResponse(void);
extern void WriteItemValueResponse(void);
extern void WriteMultipleItemValueResponse(void);
uint8_t ModbusRx[256];                          // Modbus Receive buffer



//constructor
Button::Button(void) {
    // in case of a press button, we do nothing
    // in case of a toggle switch, we have to check the switch position since it might have been changed
    // since last powerup
    //     0            1          2           3           4            5              6          7
    // "Disabled", "Access B", "Access S", "Sma-Sol B", "Sma-Sol S", "Grid Relay", "Custom B", "Custom S"
    CheckSwitch(true);
}


//since in v4 ESP32 only a copy of ErrorFlags is available, we need to have functions so v4 ESP32 can set CH32 ErrorFlags
void setErrorFlags(uint8_t flags) {
    ErrorFlags |= flags;
#if SMARTEVSE_VERSION >= 40 //v4 ESP32
    Serial1.printf("@setErrorFlags:%u\n", flags);
#endif
}

void clearErrorFlags(uint8_t flags) {
    ErrorFlags &= ~flags;
#if SMARTEVSE_VERSION >= 40 //v4 ESP32
    Serial1.printf("@clearErrorFlags:%u\n", flags);
#endif
}

// ChargeDelay owned by CH32 so ESP32 gets a copy
void setChargeDelay(uint8_t delay) {
#if SMARTEVSE_VERSION >= 40 //v4 ESP32
    Serial1.printf("@ChargeDelay:%u\n", delay);
#else
    ChargeDelay = delay;
#endif
}


#ifndef SMARTEVSE_VERSION //CH32 version
void Button::HandleSwitch(void) {
    printf("@ExtSwitch:%u.\n", Pressed);
}
#else //v3 and v4
void Button::HandleSwitch(void) 
{
    if (Pressed) {
        // Switch input pulled low
        switch (Switch) {
            case 1: // Access Button
                setAccess(AccessStatus == ON ? OFF : ON);           // Toggle AccessStatus OFF->ON->OFF (old behaviour) or PAUSE->ON
                _LOG_I("Access: %d\n", AccessStatus);
                MqttButtonState = !MqttButtonState;
                break;
            case 2: // Access Switch
                setAccess(ON);
                MqttButtonState = true;
                break;
            case 3: // Smart-Solar Button
                MqttButtonState = true;
                break;
            case 4: // Smart-Solar Switch
                if (Mode == MODE_SOLAR) {
                    setMode(MODE_SMART);
                }
                MqttButtonState = true;
                break;
            case 5: // Grid relay
                GridRelayOpen = false;
                MqttButtonState = true;
                break;
            case 6: // Custom button B
                CustomButton = !CustomButton;
                MqttButtonState = CustomButton;
                break;
            case 7: // Custom button S
                CustomButton = true;
                MqttButtonState = CustomButton;
                break;
            default:
                if (State == STATE_C) {                             // Menu option Access is set to Disabled
                    setState(STATE_C1);
                    if (!TestState) setChargeDelay(15);             // Keep in State B for 15 seconds, so the Charge cable can be removed.

                }
                break;
        }
        #if MQTT
                MQTTclient.publish(MQTTprefix + "/CustomButton", MqttButtonState ? "On" : "Off", false, 0);
        #endif  

        // Reset RCM error when switch is pressed/toggled
        // RCM was tripped, but RCM level is back to normal
        if ((ErrorFlags & RCM_TRIPPED) && (digitalRead(PIN_RCM_FAULT) == LOW || RCmon == 0)) {
            clearErrorFlags(RCM_TRIPPED);
        }
        // Also light up the LCD backlight
        BacklightTimer = BACKLIGHT;                                 // Backlight ON

    } else {
        // Switch input released
        uint32_t tmpMillis = millis();

        switch (Switch) {
            case 2: // Access Switch
                setAccess(OFF);
                MqttButtonState = false;
                break;
            case 3: // Smart-Solar Button
                if (tmpMillis < TimeOfPress + 1500) {                            // short press
                    if (Mode == MODE_SMART) {
                        setMode(MODE_SOLAR);
                    } else if (Mode == MODE_SOLAR) {
                        setMode(MODE_SMART);
                    }
                    ErrorFlags &= ~(LESS_6A);                       // Clear All errors
                    ChargeDelay = 0;                                // Clear any Chargedelay
                    setSolarStopTimer(0);                           // Also make sure the SolarTimer is disabled.
                    MaxSumMainsTimer = 0;
                    LCDTimer = 0;
                }
                MqttButtonState = false;
                break;
            case 4: // Smart-Solar Switch
                if (Mode == MODE_SMART) setMode(MODE_SOLAR);
                MqttButtonState = false;
                break;
            case 5: // Grid relay
                GridRelayOpen = true;
                MqttButtonState = false;
                break;
            case 6: // Custom button B
                break;
            case 7: // Custom button S
                CustomButton = false;
                MqttButtonState = CustomButton;
                break;
            default:
                break;
        }
        #if MQTT
                MQTTclient.publish(MQTTprefix + "/CustomButton", MqttButtonState ? "On" : "Off", false, 0);
                MQTTclient.publish(MQTTprefix + "/CustomButtonPressTime", (tmpMillis - TimeOfPress), false, 0);
        #endif
    }
}
#endif

void Button::CheckSwitch(bool force) {
#if SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40
    uint8_t Read = digitalRead(PIN_SW_IN);
#endif
#ifndef SMARTEVSE_VERSION //CH32
    uint8_t Read = funDigitalRead(SW_IN) && funDigitalRead(BUT_SW_IN);          // BUT_SW_IN = LED pushbutton, SW_IN = 12pin plug at bottom
#endif

#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40   //CH32 and v3 ESP32
    static uint8_t RB2count = 0, RB2last = 2;

    if (force)                                                                  // force to read switch position
        RB2last = 2;

    if ((RB2last == 2) && (Switch == 1 || Switch == 3 || Switch == 6))          // upon initialization we want the toggle switch to be read
        RB2last = 1;                                                            // but not the push buttons, because this would toggle the state
                                                                                // upon reboot

    // External switch changed state?
    if (Read != RB2last) {
        // make sure that noise on the input does not switch
        if (RB2count++ > 10) {
            RB2last = Read;
            Pressed = !RB2last;
            if (Pressed)
                TimeOfPress = millis();
            HandleSwitch();
            RB2count = 0;
        }
    } else { // no change in key....
        RB2count = 0;
        if (Pressed && Switch == 3 && millis() > TimeOfPress + 1500) {
            if (State == STATE_C) {
                setState(STATE_C1);
                if (!TestState) setChargeDelay(15);                             // Keep in State B for 15 seconds, so the Charge cable can be removed.
            }
        }
    }
#endif
}

Button ExtSwitch;

//similar to setAccess; OverrideCurrent owned by ESP32
void setOverrideCurrent(uint16_t Current) { //c
#ifdef SMARTEVSE_VERSION //v3 and v4
    OverrideCurrent = Current;
    SEND_TO_CH32(OverrideCurrent)

    //write_settings TODO doesnt include OverrideCurrent
#if MQTT
    // Update MQTT faster
    lastMqttUpdate = 10;
#endif //MQTT
#else //CH32
    SEND_TO_ESP32(OverrideCurrent)
#endif //SMARTEVSE_VERSION
}


/**
 * Set EVSE mode
 * 
 * @param uint8_t Mode
 */
void setMode(uint8_t NewMode) {
#ifdef SMARTEVSE_VERSION //v3 and v4
    if (NewMode > MODE_SOLAR) { //this should never happen
        _LOG_A("ERROR: setMode tries to set Mode to %u.\n", NewMode);
        return;
    }

    // If mainsmeter disabled we can only run in Normal Mode, unless we are a Node
    if (LoadBl <2 && !MainsMeter.Type && NewMode != MODE_NORMAL)
        return;

    // Take care of extra conditionals/checks for custom features
    setAccess(DelayedStartTime.epoch2 ? OFF : ON); //if DelayedStartTime not zero then we are Delayed Charging
    if (NewMode == MODE_SOLAR) {
        // Reset OverrideCurrent if mode is SOLAR
        setOverrideCurrent(0);
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
            setAccess(OFF);                                                     //switch to OFF
            switchOnLater = true;
        }
    }

    /* rob040: similar to the above, when solar charging at 1P and mode change, we need to switch back to 3P */
    if ((EnableC2 == AUTO) && (Mode != NewMode) && (Mode == MODE_SOLAR) /* && solar 1P*/) {
        setAccess(OFF);                                                       //switch to OFF
        switchOnLater = true;
    }

#if MQTT
    // Update MQTT faster
    lastMqttUpdate = 10;
#endif

    if (NewMode == MODE_SMART) {                                                // the smart-solar button used to clear all those flags toggling between those modes
        clearErrorFlags(LESS_6A);                                               // Clear All errors
        setSolarStopTimer(0);                                                   // Also make sure the SolarTimer is disabled.
        MaxSumMainsTimer = 0;
    }
    setChargeDelay(0);                                                          // Clear any Chargedelay
    BacklightTimer = BACKLIGHT;                                                 // Backlight ON
    if (Mode != NewMode) NodeNewMode = NewMode + 1;
    Mode = NewMode;    
    SEND_TO_CH32(Mode); //d

    if (switchOnLater)
        setAccess(ON);

    //make mode and start/stoptimes persistent on reboot
    if (preferences.begin("settings", false) ) {                        //false = write mode
        preferences.putUChar("Mode", Mode);
        preferences.putULong("DelayedStartTim", DelayedStartTime.epoch2); //epoch2 only needs 4 bytes
        preferences.putULong("DelayedStopTime", DelayedStopTime.epoch2);   //epoch2 only needs 4 bytes
        preferences.putUShort("DelayedRepeat", DelayedRepeat);
        preferences.end();
    }
#else //CH32
    printf("@Mode:%u.\n", NewMode); //a
    _LOG_V("[<-] Mode:%u\n", NewMode);
#endif //SMARTEVSE_VERSION
}


/**
 * Set the solar stop timer
 *
 * @param unsigned int Timer (seconds)
 */
void setSolarStopTimer(uint16_t Timer) {
    if (SolarStopTimer == Timer)
        return;                                                             // prevent unnecessary publishing of SolarStopTimer
    SolarStopTimer = Timer;
    SEND_TO_ESP32(SolarStopTimer);
    SEND_TO_CH32(SolarStopTimer);
#if MQTT
    MQTTclient.publish(MQTTprefix + "/SolarStopTimer", SolarStopTimer, false, 0);
#endif
}

#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40   //CH32 and v3 ESP32
/**
 * Checks all parameters to determine whether
 * we are going to force single phase charging
 * Returns true if we are going to do single phase charging
 * Returns false if we are going to do (traditional) 3 phase charging
 * This is only relevant on a 3P mains and 3P car installation!
 * 1P car will always charge 1P undetermined by CONTACTOR2
 */
uint8_t Force_Single_Phase_Charging() {                                         // abbreviated to FSPC
    switch (EnableC2) {
        case NOT_PRESENT:                                                       //no use trying to switch a contactor on that is not present
            return 0;   //3P charging
        case ALWAYS_OFF:
            return 1;   //1P charging
        case SOLAR_OFF:
            return (Mode == MODE_SOLAR); //1P solar charging
        case AUTO:
            return (Nr_Of_Phases_Charging == 1);
        case ALWAYS_ON:
            return 0;   //3P charging
    }
    //in case we don't know, stick to 3P charging
    return 0;
}
#endif

// Write duty cycle to pin
// Value in range 0 (0% duty) to 1024 (100% duty) for ESP32, 1000 (100% duty) for CH32
void SetCPDuty(uint32_t DutyCycle){
#if SMARTEVSE_VERSION >= 40 //ESP32
    Serial1.printf("@SetCPDuty:%u\n", DutyCycle);
#else //CH32 and v3 ESP32
#if SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40 //v3 ESP32
    ledcWrite(CP_CHANNEL, DutyCycle);                                       // update PWM signal
#endif
#ifndef SMARTEVSE_VERSION  //CH32
    // update PWM signal
    TIM1->CH1CVR = DutyCycle;
#endif
#endif //v4
    CurrentPWM = DutyCycle;
}

// Set Charge Current 
// Current in Amps * 10 (160 = 16A)
void SetCurrent(uint16_t current) {
#if SMARTEVSE_VERSION >= 40 //ESP32
    Serial1.printf("@SetCurrent:%u\n", current);
#else
    uint32_t DutyCycle;

    if ((current >= (MIN_CURRENT * 10)) && (current <= 510)) DutyCycle = current / 0.6;
                                                                            // calculate DutyCycle from current
    else if ((current > 510) && (current <= 800)) DutyCycle = (current / 2.5) + 640;
    else DutyCycle = 100;                                                   // invalid, use 6A
#if SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40 //v3 ESP32
    DutyCycle = DutyCycle * 1024 / 1000;                                    // conversion to 1024 = 100%
#endif
    SetCPDuty(DutyCycle);
#endif
}


void setStatePowerUnavailable(void) {
#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40   //CH32 and v3 ESP32
    if (State == STATE_A)
       return;
    //State changes between A,B,C,D are caused by EV or by the user
    //State changes between x1 and x2 are created by the EVSE
    //State changes between x1 and x2 indicate availability (x2) of unavailability (x1) of power supply to the EV
    if (State == STATE_C) setState(STATE_C1);                       // If we are charging, tell EV to stop charging
    else if (State != STATE_C1) setState(STATE_B1);                 // If we are not in State C1, switch to State B1
#else //v4 ESP32
    printf("@setStatePowerUnavailable\n");
#endif
}


//this replaces old CP_OFF and CP_ON and PILOT_CONNECTED and PILOT_DISCONNECTED macros
//setPilot(true) switches the PILOT ON (CONNECT), setPilot(false) switches it OFF
void setPilot(bool On) {
    if (On) {
#if SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40   //ESP32 v3
        digitalWrite(PIN_CPOFF, LOW);
    } else
        digitalWrite(PIN_CPOFF, HIGH);
#endif
#ifndef SMARTEVSE_VERSION //CH32
        funDigitalWrite(CPOFF, FUN_LOW);
    } else
        funDigitalWrite(CPOFF, FUN_HIGH);
#endif
#if SMARTEVSE_VERSION >=40 //ESP32 v4
        Serial1.printf("@setPilot:%u\n", On);
    }
#endif
}

// State is owned by the CH32
// because it is highly subject to machine interaction
// and also charging is supposed to function if ESP32 is hung/rebooted
// If the CH32 wants to change that variable, it calls setState
// which sends a message to the ESP32. No other function may change State!
// If the ESP32 wants to change the State it sends a message to CH32
// and if the change is honored, the CH32 sends an update
// to the CH32 through the setState routine
// So the setState code of the CH32 is the only routine that
// is allowed to change the value of State on CH32
// All other code has to use setState
// so for v4 we need:
// a. ESP32 setState sends message to CH32              in ESP32 src/main.cpp (this file)
// b. CH32 receiver that calls local setState           in CH32 src/evse.c
// c. CH32 setState full functionality                  in ESP32 src/main.cpp (this file) to be copied to CH32
// d. CH32 sends message to ESP32                       in ESP32 src/main.cpp (this file) to be copied to CH32
// e. ESP32 receiver that sets local variable           in ESP32 src/main.cpp


void setState(uint8_t NewState) { //c
    if (State != NewState) {
#ifdef SMARTEVSE_VERSION //v3 and v4
        char Str[50];
        snprintf(Str, sizeof(Str), "%02d:%02d:%02d STATE %s -> %s\n",timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, StrStateName[State], StrStateName[NewState] );
        _LOG_A("%s",Str);
#if SMARTEVSE_VERSION >= 40
        Serial1.printf("@State:%u\n", NewState); //a
#endif
#else //CH32
        printf("@State:%u.\n", NewState); //d
#endif
    }

#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40   //CH32 and v3 ESP32 //a
    switch (NewState) {
        case STATE_B1:
            if (!ChargeDelay) setChargeDelay(3);                                // When entering State B1, wait at least 3 seconds before switching to another state.
            if (State != STATE_C1 && State != STATE_B1 && State != STATE_B && !PilotDisconnected) {
                PILOT_DISCONNECTED;
                PilotDisconnected = true;
                PilotDisconnectTime = 5;                                       // Set PilotDisconnectTime to 5 seconds

                _LOG_A("Pilot Disconnected\n");
            }
            // fall through
        case STATE_A:                                                           // State A1
            CONTACTOR1_OFF;
            CONTACTOR2_OFF;
#ifdef SMARTEVSE_VERSION //v3
            SetCPDuty(1024);                                                    // PWM off,  channel 0, duty cycle 100%
            timerAlarmWrite(timerA, PWM_100, true);                             // Alarm every 1ms, auto reload
#else //CH32
            TIM1->CH1CVR = 1000;                                               // Set CP output to +12V
#endif

            if (NewState == STATE_A) {
                ModemStage = 0;                                                 // Start modem if EV connects
                clearErrorFlags(LESS_6A);
                setChargeDelay(0);
                Switching_Phases_C2 = NO_SWITCH;
                // Reset Node
                Node[0].Timer = 0;
                Node[0].IntTimer = 0;
                Node[0].Phases = 0;
                Node[0].MinCurrent = 0;                                         // Clear ChargeDelay when disconnected.
            }

#if MODEM
            if (DisconnectTimeCounter == -1){
                DisconnectTimeCounter = 0;                                      // Start counting disconnect time. If longer than 60 seconds, throw DisconnectEvent
            }
            break;
        case STATE_MODEM_REQUEST: // After overriding PWM, and resetting the safe state is 10% PWM. To make sure communication recovers after going to normal, we do this. Ugly and temporary
            ToModemWaitStateTimer = 0;
            PILOT_DISCONNECTED;                                                 // CP 0V = STATE E
            DisconnectTimeCounter = -1;                                         // Disable Disconnect timer. Car is connected
            SetCPDuty(1024); //TODO try 0 to emulate STATE_E
            CONTACTOR1_OFF;
            CONTACTOR2_OFF;
            break;
        case STATE_MODEM_WAIT:
            PILOT_CONNECTED;
            SetCPDuty(51); // 5% * 1024/1000
            ToModemDoneStateTimer = 60;
            break;
        //TODO how about STATE_MODEM_DENIED?
        case STATE_MODEM_DONE:  // This state is reached via STATE_MODEM_WAIT after 60s (timeout condition, nothing received) or after REST/MODEM request (success, shortcut to immediate charging).
            PILOT_DISCONNECTED;
            DisconnectTimeCounter = -1;                                         // Disable Disconnect timer. Car is connected
            LeaveModemDoneStateTimer = 5;                                       // Disconnect CP for 5 seconds, restart charging cycle but this time without the modem steps.
#endif
            break;
        case STATE_B:
#if MODEM
            PILOT_CONNECTED;
            DisconnectTimeCounter = -1;                                         // Disable Disconnect timer. Car is connected
#endif
            CONTACTOR1_OFF;
            CONTACTOR2_OFF;
#ifdef SMARTEVSE_VERSION //v3
            timerAlarmWrite(timerA, PWM_95, false);                             // Enable Timer alarm, set to diode test (95%)
#endif
            SetCurrent(ChargeCurrent);                                          // Enable PWM
#ifndef SMARTEVSE_VERSION //CH32
            TIM1->CH4CVR = PWM_96;                                              // start ADC sampling at 96% (Diode Check)
#endif
            break;
        case STATE_C:                                                           // State C2
            ActivationMode = 255;                                               // Disable ActivationMode
#ifdef SMARTEVSE_VERSION //v3
            LCDTimer = 0;
#else //CH32
            printf("@LCDTimer:0\n");
            RCMTestCounter = RCM_TEST_DURATION;
            SEND_TO_ESP32(RCMTestCounter);
            testRCMON();
#endif

            if (Switching_Phases_C2 == GOING_TO_SWITCH_1P) {
                    CONTACTOR2_OFF;
                    setSolarStopTimer(0);
                    MaxSumMainsTimer = 0;
                    Nr_Of_Phases_Charging = 1;                                  // switch to 1F
                    Switching_Phases_C2 = NO_SWITCH;                            // we finished the switching process,
                                                                                // BUT we don't know which is the single phase
            }

            if (Switching_Phases_C2 == GOING_TO_SWITCH_3P) {
                    setSolarStopTimer(0);
                    MaxSumMainsTimer = 0;
                    Nr_Of_Phases_Charging = 3;                                  // switch to 3P
                    SEND_TO_ESP32(Nr_Of_Phases_Charging);
                    Switching_Phases_C2 = NO_SWITCH;                            // we finished the switching process,
            }

            CONTACTOR1_ON;
            if (!Force_Single_Phase_Charging()) {                               // in AUTO mode we start with 3phases
                CONTACTOR2_ON;                                                  // Contactor2 ON
            }
            break;
        case STATE_C1:
#ifdef SMARTEVSE_VERSION //v3
            SetCPDuty(1024);                                                    // PWM off,  channel 0, duty cycle 100%
            timerAlarmWrite(timerA, PWM_100, true);                             // Alarm every 1ms, auto reload
#else //CH32                                                                          // EV should detect and stop charging within 3 seconds
            TIM1->CH1CVR = 1000;                                                // Set CP output to +12V
#endif
            C1Timer = 6;                                                        // Wait maximum 6 seconds, before forcing the contactor off.
            setChargeDelay(15);
            break;
        default:
            break;
    }

    BalancedState[0] = NewState;
    BalancedState[LoadBl] = NewState;
    State = NewState;

#if MQTT
    // Update MQTT faster
    lastMqttUpdate = 10;
#endif

#ifdef SMARTEVSE_VERSION //v3
    BacklightTimer = BACKLIGHT;                                                 // Backlight ON
#else //CH32
    printf("@BacklightTimer:%u\n", BACKLIGHT);
#endif

#endif //SMARTEVSE_VERSION
}

// make it possible to call setAccess with an int parameter
void setAccess(uint8_t Access) { //c
    setAccess((AccessStatus_t) Access);
}

// the Access_bit is owned by the ESP32
// because it is highly subject to human interaction
// and also its status is supposed to get saved in NVS
// so if the CH32 wants to change that variable,
// it sends a message to the ESP32
// and if the change is honored, the ESP32 sends an update
// to the CH32 through the ConfigItem routine
// So the receiving code of the CH32 is the only routine that
// is allowed to change the value of Acces_bit on CH32
// All other code has to use setAccess
// so for v4 we need:
// a. CH32 setAccess sends message to ESP32           in CH32 src/evse.c and/or in src/main.cpp (this file)
// b. ESP32 receiver that calls local setAccess       in ESP32 src/main.cpp
// c. ESP32 setAccess full functionality              in ESP32 src/main.cpp (this file)
// d. ESP32 sends message to CH32                     in ESP32 src/main.cpp (this file)
// e. CH32 receiver that sets local variable          in CH32 src/evse.c

// same for Mode/setMode

void setAccess(AccessStatus_t Access) { //c
#ifdef SMARTEVSE_VERSION //v3 and v4
    AccessStatus = Access;
#if SMARTEVSE_VERSION >= 40
    Serial1.printf("@Access:%u\n", AccessStatus); //d
#endif
    if (Access == OFF || Access == PAUSE) {
        //TODO:setStatePowerUnavailable() ?
        if (State == STATE_C) setState(STATE_C1);                               // Determine where to switch to.
        else if (State != STATE_C1 && (State == STATE_B || State == STATE_MODEM_REQUEST || State == STATE_MODEM_WAIT || State == STATE_MODEM_DONE || State == STATE_MODEM_DENIED)) setState(STATE_B1);
    }

    //make mode and start/stoptimes persistent on reboot
    if (preferences.begin("settings", false) ) {                        //false = write mode
        preferences.putUChar("Access", AccessStatus);
        preferences.putUShort("CardOffs16", CardOffset);
        preferences.end();
    }

#if MQTT
    // Update MQTT faster
    lastMqttUpdate = 10;
#endif //MQTT
#else //CH32
    SEND_TO_ESP32(Access) //a
#endif //SMARTEVSE_VERSION
}


#ifndef SMARTEVSE_VERSION //CH32
// Determine the state of the Pilot signal
//
uint8_t Pilot() {

    uint16_t sample, Min = 4095, Max = 0;
    uint8_t n, ret;
    static uint8_t old_pilot = 255;

    // calculate Min/Max of last 32 CP measurements (32 ms)
    for (n=0 ; n<NUM_ADC_SAMPLES ;n++) {

        sample = ADC_CP[n];
        if (sample < Min) Min = sample;                                   // store lowest value
        if (sample > Max) Max = sample;                                   // store highest value
    }

    //printf("@MSG: min:%u max:%u\n",Min ,Max);

    // test Min/Max against fixed levels    (needs testing)
    ret = PILOT_NOK;                                                        // Pilot NOT ok
    if (Min >= 4000 ) ret = PILOT_12V;                                      // Pilot at 12V
    if ((Min >= 3300) && (Max < 4000)) ret = PILOT_9V;                      // Pilot at 9V
    if ((Min >= 2400) && (Max < 3300)) ret = PILOT_6V;                      // Pilot at 6V
    if ((Min >= 2000) && (Max < 2400)) ret = PILOT_3V;                      // Pilot at 3V
    if ((Min > 100) && (Max < 350)) ret = PILOT_DIODE;                      // Diode Check OK
    if (ret != old_pilot) {
        printf("@Pilot:%u\n", ret); //d
        old_pilot = ret;
    }
    return ret;
}
#endif
#if defined(SMARTEVSE_VERSION) && SMARTEVSE_VERSION < 40 //ESP32 v4
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
#endif

#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40   //CH32 and v3 ESP32
// Is there at least 6A(configurable MinCurrent) available for a new EVSE?
// Look whether there would be place for one more EVSE if we could lower them all down to MinCurrent
// returns 1 if there is 6A available
// returns 0 if there is no current available
// only runs on the Master or when loadbalancing Disabled
// only runs on CH32 for SmartEVSEv4
char IsCurrentAvailable(void) {
    uint8_t n, ActiveEVSE = 0;
    int Baseload, Baseload_EV, TotalCurrent = 0;
//TODO debug:
//    printf("@MSG: BalancedStates=%s,%s,%s,%s,%s,%s,%s,%s.\n", StrStateName[BalancedState[0]],StrStateName[BalancedState[1]],StrStateName[BalancedState[2]],StrStateName[BalancedState[3]],StrStateName[BalancedState[4]],StrStateName[BalancedState[5]],StrStateName[BalancedState[6]],StrStateName[BalancedState[7]]);
    for (n = 0; n < NR_EVSES; n++) if (BalancedState[n] == STATE_C)             // must be in STATE_C
    {
        ActiveEVSE++;                                                           // Count nr of active (charging) EVSE's
        TotalCurrent += Balanced[n];                                            // Calculate total of all set charge currents
    }

//TODO debug:
//    printf("@MSG: Mode=%d.\n", Mode);

    // Allow solar Charging if surplus current is above 'StartCurrent' (sum of all phases)
    // Charging will start after the timeout (chargedelay) period has ended
     // Only when StartCurrent configured or Node MinCurrent detected or Node inactive
    if (Mode == MODE_SOLAR) {                                                   // no active EVSE yet?
        if (ActiveEVSE == 0 && Isum >= ((signed int)StartCurrent *-10)) {
            _LOG_D("No current available StartCurrent line %d. ActiveEVSE=%u, TotalCurrent=%d.%dA, StartCurrent=%uA, Isum=%d.%dA, ImportCurrent=%uA.\n", __LINE__, ActiveEVSE, TotalCurrent/10, abs(TotalCurrent%10), StartCurrent, Isum/10, abs(Isum%10), ImportCurrent);
            return 0;
        }
        else if ((ActiveEVSE * MinCurrent * 10) > TotalCurrent) {               // check if we can split the available current between all active EVSE's
            _LOG_D("No current available StartCurrent line %d. ActiveEVSE=%u, TotalCurrent=%d.%dA, StartCurrent=%uA, Isum=%d.%dA, ImportCurrent=%uA.\n", __LINE__, ActiveEVSE, TotalCurrent/10, abs(TotalCurrent%10), StartCurrent, Isum/10, abs(Isum%10), ImportCurrent);
            return 0;
        }
        else if (ActiveEVSE > 0 && Isum > ((signed int)ImportCurrent * 10) + TotalCurrent - (ActiveEVSE * MinCurrent * 10)) {
            _LOG_D("No current available StartCurrent line %d. ActiveEVSE=%u, TotalCurrent=%d.%dA, StartCurrent=%uA, Isum=%d.%dA, ImportCurrent=%uA.\n", __LINE__, ActiveEVSE, TotalCurrent/10, abs(TotalCurrent%10), StartCurrent, Isum/10, abs(Isum%10), ImportCurrent);
            return 0;
        }
    }

    ActiveEVSE++;                                                           // Do calculations with one more EVSE
    if (ActiveEVSE > NR_EVSES) ActiveEVSE = NR_EVSES;
    Baseload = MainsMeter.Imeasured - TotalCurrent;                         // Calculate Baseload (load without any active EVSE)
    Baseload_EV = EVMeter.Imeasured - TotalCurrent;                         // Load on the EV subpanel excluding any active EVSE
    if (Baseload_EV < 0) Baseload_EV = 0;                                   // so Baseload_EV = 0 when no EVMeter installed

    // Check if the lowest charge current(6A) x ActiveEV's + baseload would be higher then the MaxMains.
    if (Mode != MODE_NORMAL && (ActiveEVSE * (MinCurrent * 10) + Baseload) > (MaxMains * 10)) {
        printf("@MSG: No current available MaxMains line %d. ActiveEVSE=%u, Baseload=%d.%dA, MinCurrent=%uA, MaxMains=%uA.\n", __LINE__, ActiveEVSE, Baseload/10, abs(Baseload%10), MinCurrent, MaxMains);
        return 0;                                                           // Not enough current available!, return with error
    }
    if (((LoadBl == 0 && EVMeter.Type && Mode != MODE_NORMAL) || LoadBl == 1) // Conditions in which MaxCircuit has to be considered
        && ((ActiveEVSE * (MinCurrent * 10) + Baseload_EV) > (MaxCircuit * 10))) { // MaxCircuit is exceeded
        printf("@MSG: No current available MaxCircuit line %d. ActiveEVSE=%u, Baseload_EV=%d.%dA, MinCurrent=%uA, MaxCircuit=%uA.\n", __LINE__, ActiveEVSE, Baseload_EV/10, abs(Baseload_EV%10), MinCurrent, MaxCircuit);
        return 0;                                                           // Not enough current available!, return with error
    } //else
        //printf("@MSG: Current available MaxCircuit line %d. ActiveEVSE=%u, Baseload_EV=%d.%dA, MinCurrent=%uA, MaxCircuit=%uA.\n", __LINE__, ActiveEVSE, Baseload_EV/10, abs(Baseload_EV%10), MinCurrent, MaxCircuit);
    //assume the current should be available on all 3 phases
    int Phases = Force_Single_Phase_Charging() ? 1 : 3;
    if (Mode != MODE_NORMAL && MaxSumMains && ((Phases * ActiveEVSE * MinCurrent * 10) + Isum > MaxSumMains * 10)) {
        //printf("@MSG: No current available MaxSumMains line %d. ActiveEVSE=%u, MinCurrent=%uA, Isum=%d.%dA, MaxSumMains=%uA.\n", __LINE__, ActiveEVSE, MinCurrent, Isum/10, abs(Isum%10), MaxSumMains);
        return 0;                                                           // Not enough current available!, return with error
    }

// Use OCPP Smart Charging if Load Balancing is turned off
#if ENABLE_OCPP && defined(SMARTEVSE_VERSION) //run OCPP only on ESP32
    if (OcppMode &&                            // OCPP enabled
            !LoadBl &&                         // Internal LB disabled
            OcppCurrentLimit >= 0.f &&         // OCPP limit defined
            OcppCurrentLimit < MinCurrent) {  // OCPP suspends charging
        printf("@MSG: OCPP Smart Charging suspends EVSE\n");
        return 0;
    }
#endif //ENABLE_OCPP

    printf("@MSG: Current available checkpoint D. ActiveEVSE increased by one=%u, TotalCurrent=%d.%dA, StartCurrent=%uA, Isum=%d.%dA, ImportCurrent=%uA.\n", ActiveEVSE, TotalCurrent/10, abs(TotalCurrent%10), StartCurrent, Isum/10, abs(Isum%10), ImportCurrent);
    return 1;
}
#else //v4 ESP32
bool Shadow_IsCurrentAvailable; // this is a global variable that will be kept uptodate by Timer1S on CH32
char IsCurrentAvailable(void) {
    //TODO debug:
    _LOG_A("Shadow_IsCurrentAvailable=%d.\n", Shadow_IsCurrentAvailable);
    return Shadow_IsCurrentAvailable;
}
#endif


// Calculates Balanced PWM current for each EVSE
// mod =0 normal
// mod =1 we have a new EVSE requesting to start charging.
// only runs on the Master or when loadbalancing Disabled
void CalcBalancedCurrent(char mod) {
#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40   //CH32 and v3 ESP32
    int Average, MaxBalanced, Idifference, Baseload_EV;
    int ActiveEVSE = 0;
    signed int IsumImport = 0;
    int ActiveMax = 0, TotalCurrent = 0, Baseload;
    char CurrentSet[NR_EVSES] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t n;
    bool LimitedByMaxSumMains = false;
    // ############### first calculate some basic variables #################
    if (BalancedState[0] == STATE_C && MaxCurrent > MaxCapacity && !Config)
        ChargeCurrent = MaxCapacity * 10;
    else
        ChargeCurrent = MaxCurrent * 10;                                        // Instead use new variable ChargeCurrent.

// Use OCPP Smart Charging if Load Balancing is turned off
#if ENABLE_OCPP && defined(SMARTEVSE_VERSION) //run OCPP only on ESP32
    if (OcppMode &&                      // OCPP enabled
            !LoadBl &&                   // Internal LB disabled
            OcppCurrentLimit >= 0.f) {   // OCPP limit defined

        if (OcppCurrentLimit < MinCurrent) {
            ChargeCurrent = 0;
        } else {
            ChargeCurrent = std::min(ChargeCurrent, (uint16_t) (10.f * OcppCurrentLimit));
        }
    }
#endif //ENABLE_OCPP

    // Override current temporary if set
    if (OverrideCurrent)
        ChargeCurrent = OverrideCurrent;

    BalancedMax[0] = ChargeCurrent;
                                                                                // update BalancedMax[0] if the MAX current was adjusted using buttons or CLI
    for (n = 0; n < NR_EVSES; n++) if (BalancedState[n] == STATE_C) {
            ActiveEVSE++;                                                       // Count nr of Active (Charging) EVSE's
            ActiveMax += BalancedMax[n];                                        // Calculate total Max Amps for all active EVSEs
            TotalCurrent += Balanced[n];                                        // Calculate total of all set charge currents
    }

    _LOG_V("Checkpoint 1 Isetbalanced=%d.%d A Imeasured=%d.%d A MaxCircuit=%d Imeasured_EV=%d.%d A, Battery Current = %d.%d A, mode=%u.\n", IsetBalanced/10, abs(IsetBalanced%10), MainsMeter.Imeasured/10, abs(MainsMeter.Imeasured%10), MaxCircuit, EVMeter.Imeasured/10, abs(EVMeter.Imeasured%10), homeBatteryCurrent/10, abs(homeBatteryCurrent%10), Mode);

    Baseload_EV = EVMeter.Imeasured - TotalCurrent;                             // Calculate Baseload (load without any active EVSE)
    if (Baseload_EV < 0)
        Baseload_EV = 0;
    Baseload = MainsMeter.Imeasured - TotalCurrent;                             // Calculate Baseload (load without any active EVSE)

    // ############### now calculate IsetBalanced #################

    if (Mode == MODE_NORMAL)                                                    // Normal Mode
    {
        if (LoadBl == 1)                                                        // Load Balancing = Master? MaxCircuit is max current for all active EVSE's;
            IsetBalanced = (MaxCircuit * 10 ) - Baseload_EV;
                                                                                // limiting is per phase so no Nr_Of_Phases_Charging here!
        else
            IsetBalanced = ChargeCurrent;                                       // No Load Balancing in Normal Mode. Set current to ChargeCurrent (fix: v2.05)
    } //end MODE_NORMAL
    else { // start MODE_SOLAR || MODE_SMART
        if (Mode == MODE_SOLAR && State == STATE_B) {
            // Prepare for switching to state C
            _LOG_D("waiting for Solar (B) Isum=%d dA, phases=%d\n", Isum, Nr_Of_Phases_Charging);
            if (EnableC2 == AUTO) {
                // Mains isn't loaded, so the Isum must be negative for solar charging
                // determine if enough current is available for 3-phase or 1-phase charging
                // TODO: deal with strong fluctuations in startup
                if (-Isum >= (30*MinCurrent+30)) { // 30x for 3-phase and 0.1A resolution; +30 to have 3x1.0A room for regulation
                    if (Nr_Of_Phases_Charging != 3) {
                        Switching_Phases_C2 = GOING_TO_SWITCH_3P;
                        _LOG_D("Solar starting in 3-phase mode\n");
                    } else
                        _LOG_D("Solar continuing in 3-phase mode\n");
                } else /*if (-Isum >= (10*MinCurrent+2))*/ {
                    if (Nr_Of_Phases_Charging != 1) {
                        Switching_Phases_C2 = GOING_TO_SWITCH_1P;
                        _LOG_D("Solar starting in 1-phase mode\n");
                    } else
                        _LOG_D("Solar continuing in 1-phase mode\n");
                }
            }
        }
        // we want to obey EnableC2 settings at all times, after switching modes and/or C2 settings
        // TODO move this to setMode and glcd.cpp C2_MENU?
        if (EnableC2 != AUTO) {
            if (Force_Single_Phase_Charging()) {
                if (Nr_Of_Phases_Charging != 1) {
                    Switching_Phases_C2 = GOING_TO_SWITCH_1P;
                }
            } else {
                if (Nr_Of_Phases_Charging != 3) {
                    Switching_Phases_C2 = GOING_TO_SWITCH_3P;
                }
            }
        } else if (Mode == MODE_SMART && Nr_Of_Phases_Charging != 3) {          // in SMART AUTO mode go back to the old 3P
                    Switching_Phases_C2 = GOING_TO_SWITCH_3P;
        }
        // adapt IsetBalanced in Smart Mode, and ensure the MaxMains/MaxCircuit settings for Solar

        if ((LoadBl == 0 && EVMeter.Type) || LoadBl == 1)                       // Conditions in which MaxCircuit has to be considered;
                                                                                // mode = Smart/Solar so don't test for that
            Idifference = min((MaxMains * 10) - MainsMeter.Imeasured, (MaxCircuit * 10) - EVMeter.Imeasured);
        else
            Idifference = (MaxMains * 10) - MainsMeter.Imeasured;
        int ExcessMaxSumMains = ((MaxSumMains * 10) - Isum)/Nr_Of_Phases_Charging;
        if (MaxSumMains && (Idifference > ExcessMaxSumMains)) {
            Idifference = ExcessMaxSumMains;
            LimitedByMaxSumMains = true;
            _LOG_V("Current is limited by MaxSumMains: MaxSumMains=%uA, Isum=%d.%dA, Nr_Of_Phases_Charging=%u.\n", MaxSumMains, Isum/10, abs(Isum%10), Nr_Of_Phases_Charging);
        }

        if (!mod) {                                                             // no new EVSE's charging
                                                                                // For Smart mode, no new EVSE asking for current
            if (phasesLastUpdateFlag) {                                         // only increase or decrease current if measurements are updated
                _LOG_V("phaseLastUpdate=%u.\n", phasesLastUpdate);
                if (Idifference > 0) {
                    if (Mode == MODE_SMART) IsetBalanced += (Idifference / 4);  // increase with 1/4th of difference (slowly increase current)
                }                                                               // in Solar mode we compute increase of current later on!
                else
                    IsetBalanced += Idifference;                                // last PWM setting + difference (immediately decrease current) (Smart and Solar mode)
            }

            if (IsetBalanced < 0) IsetBalanced = 0;
            if (IsetBalanced > 800) IsetBalanced = 800;                         // hard limit 80A (added 11-11-2017)
        }
        _LOG_V("Checkpoint 2 Isetbalanced=%d.%d A, Idifference=%d.%d, mod=%u.\n", IsetBalanced/10, abs(IsetBalanced%10), Idifference/10, abs(Idifference%10), mod);

        if (Mode == MODE_SOLAR)                                                 // Solar version
        {
            IsumImport = Isum - (10 * ImportCurrent);                           // Allow Import of power from the grid when solar charging
            // when there is NO charging, do not change the setpoint (IsetBalanced); except when we are in Master/Slave configuration
            if (ActiveEVSE > 0 && Idifference > 0) {                            // so we had some room for power as far as MaxCircuit and MaxMains are concerned
                if (phasesLastUpdateFlag) {                                     // only increase or decrease current if measurements are updated.
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
                }
            }                                                                   // we already corrected Isetbalance in case of NOT enough power MaxCircuit/MaxMains
            _LOG_V("Checkpoint 3 Solar Isetbalanced=%d.%d A, IsumImport=%d.%d, Isum=%d.%d, ImportCurrent=%u.\n", IsetBalanced/10, abs(IsetBalanced%10), IsumImport/10, abs(IsumImport%10), Isum/10, abs(Isum%10), ImportCurrent);
        } //end MODE_SOLAR
        else { // MODE_SMART
        // New EVSE charging, and only if we have active EVSE's
            if (mod && ActiveEVSE) {                                            // if we have an ActiveEVSE and mod=1, we must be Master, so MaxCircuit has to be
                                                                                // taken into account

                IsetBalanced = min((MaxMains * 10) - Baseload, (MaxCircuit * 10 ) - Baseload_EV ); //assume the current should be available on all 3 phases
                if (MaxSumMains)
                    IsetBalanced = min((int) IsetBalanced, ((MaxSumMains * 10) - Isum)/3); //assume the current should be available on all 3 phases
                _LOG_V("Checkpoint 3 Smart Isetbalanced=%d.%d A, IsumImport=%d.%d, Isum=%d.%d, ImportCurrent=%u.\n", IsetBalanced/10, abs(IsetBalanced%10), IsumImport/10, abs(IsumImport%10), Isum/10, abs(Isum%10), ImportCurrent);
            }
        } //end MODE_SMART
    } // end MODE_SOLAR || MODE_SMART

    // ############### make sure the calculated IsetBalanced doesnt exceed any boundaries #################

    // Note: all boundary rules must be duplicated to check for HARD shortage of power
    // HARD shortage of power: boundaries are exceeded, we must stop charging!
    // SOFT shortage of power: we have timers running to stop charging in the future
    // guard MaxMains
    if (MainsMeter.Type && Mode != MODE_NORMAL)
        IsetBalanced = min((int) IsetBalanced, (MaxMains * 10) - Baseload); //limiting is per phase so no Nr_Of_Phases_Charging here!
    // guard MaxCircuit
    if ((LoadBl == 0 && EVMeter.Type && Mode != MODE_NORMAL) || LoadBl == 1)    // Conditions in which MaxCircuit has to be considered
        IsetBalanced = min((int) IsetBalanced, (MaxCircuit * 10) - Baseload_EV); //limiting is per phase so no Nr_Of_Phases_Charging here!
    // guard GridRelay
    if (GridRelayOpen) {
        int Phases = Force_Single_Phase_Charging() ? 1 : 3;
        IsetBalanced = min((int) IsetBalanced, (GridRelayMaxSumMains * 10)/Phases); //assume the current should be available on all 3 phases
    }
    _LOG_V("Checkpoint 4 Isetbalanced=%d.%d A.\n", IsetBalanced/10, abs(IsetBalanced%10));

    // ############### the rest of the work we only do if there are ActiveEVSEs #################

    int saveActiveEVSE = ActiveEVSE;                                            // TODO remove this when calcbalancedcurrent2 is approved
    if (ActiveEVSE && (phasesLastUpdateFlag || Mode == MODE_NORMAL)) {          // Only if we have active EVSE's and if we have new phase currents

        // ############### we now check shortage of power  #################

        if (IsetBalanced < (ActiveEVSE * MinCurrent * 10)) {

            // ############### shortage of power  #################

            IsetBalanced = ActiveEVSE * MinCurrent * 10;                        // retain old software behaviour: set minimal "MinCurrent" charge per active EVSE
            if (Mode == MODE_SOLAR) {
                // ----------- Check to see if we have to continue charging on solar power alone ----------
                                              // Importing too much?
                if (ActiveEVSE && IsumImport > 0 &&
                        // Would a stop free so much current that StartCurrent would immediately restart charging?
                        (Isum > (ActiveEVSE * MinCurrent * Nr_Of_Phases_Charging - StartCurrent) * 10 ||
                         // don't apply that rule if we are 3P charging and we could switch to 1P
                         (Nr_Of_Phases_Charging > 1 && EnableC2 == AUTO))) {
                    if (Nr_Of_Phases_Charging > 1 && EnableC2 == AUTO) {
                        // not enough current for 3-phase operation; we can switch to 1-phase after some time
                        // start solar stop timer
                        if (SolarStopTimer == 0) {
                            // for a small current deficiency, we wait full StopTime, to try to stay in 3P mode
                            if (IsumImport < (10 * MinCurrent)) {
                                setSolarStopTimer(StopTime * 60); // Convert minutes into seconds
                            }
                            if (SolarStopTimer == 0) setSolarStopTimer(30); // timer goes off when switching 3P->1P
                        }
                        // near end of solar stop timer, instruct to go to 1P charging and restart
                        if (SolarStopTimer <= 2) {
                            _LOG_A("Switching to single phase.\n");
                            Switching_Phases_C2 = GOING_TO_SWITCH_1P;
                            setState(STATE_C1);               // tell EV to stop charging
                            setSolarStopTimer(0);
                        }
                    }
                    else {
                        if (SolarStopTimer == 0) setSolarStopTimer(StopTime * 60); // timer that expires when 1P not enough power
                    }
                } else {
                    _LOG_D("Checkpoint a: Resetting SolarStopTimer, IsetBalanced=%d.%dA, ActiveEVSE=%u.\n", IsetBalanced/10, abs(IsetBalanced%10), ActiveEVSE);
                    setSolarStopTimer(0);
                }
            }

            // check for HARD shortage of power
            // with HARD shortage we stop charging
            // with SOFT shortage we have a timer running
            // IsetBalanced is already set to the minimum needed power to charge all Nodes
            bool hardShortage = false;
            // guard MaxMains
            if (MainsMeter.Type && Mode != MODE_NORMAL)
                if (IsetBalanced > (MaxMains * 10) - Baseload)
                    hardShortage = true;
            // guard MaxCircuit
            if (((LoadBl == 0 && EVMeter.Type && Mode != MODE_NORMAL) || LoadBl == 1) // Conditions in which MaxCircuit has to be considered
                && (IsetBalanced > (MaxCircuit * 10) - Baseload_EV))
                    hardShortage = true;
            if (!MaxSumMainsTime && LimitedByMaxSumMains)                       // if we don't use the Capacity timer, we want a hard stop
                hardShortage = true;
            if (hardShortage && Switching_Phases_C2 != GOING_TO_SWITCH_1P) {    // because switching to single phase might solve the shortage
                // ############ HARD shortage of power
                NoCurrent++;                                                    // Flag NoCurrent left
                _LOG_I("No Current!!\n");
            } else {
                // ############ soft shortage of power
                // the expiring of both SolarStopTimer and MaxSumMainsTimer is handled in the Timer1S loop
                if (LimitedByMaxSumMains && MaxSumMainsTime) {
                    if (MaxSumMainsTimer == 0)                                  // has expired, so set timer
                        MaxSumMainsTimer = MaxSumMainsTime * 60;
                }
            }
        } else {                                                                // we have enough current
            // ############### no shortage of power  #################

            // Solar mode with C2=AUTO and enough power for switching from 1P to 3P solar charge?
            if (Mode == MODE_SOLAR && Nr_Of_Phases_Charging == 1 && EnableC2 == AUTO && IsetBalanced + 8 >= MaxCurrent * 10) {
                    // are we at max regulation at 1P (Iset hovers at 15.2-16.0A on 16A MaxCurrent)(warning: Iset can also be at max when EV limits current)
                    // and is there enough spare that we can go to 3P charging?
                    // Can it take the step from 1x16A to 3x7A (in regular config)?
                    // Note that we do not take 3P MinCurrent but 3x1A above that to give it some regulation room;
                    // It also needs to sustain that minimal room for 60 seconds before it may switch to 3P
                    int spareCurrent = (3*(MinCurrent+1)-MaxCurrent);  // constant, gap between 1P range and 3P range
                    if (spareCurrent < 0) spareCurrent = 3;  // const, when 1P range overlaps 3P range
                    if (-Isum > (10*spareCurrent)) { // note that Isum is surplus current, which is negative
                        // start solar stop timer
                        if (SolarStopTimer == 0) setSolarStopTimer(63);
                        // near end of solar stop timer, instruct to go to 3P charging
                        if (SolarStopTimer <= 3) {
                            _LOG_A("Solar charge: Switching to 3P.\n");
                            Switching_Phases_C2 = GOING_TO_SWITCH_3P;
                            setState(STATE_C1);               // tell EV to stop charging //FIXME how about slaves
                            setSolarStopTimer(0);
                        }
                        else {
                            _LOG_D("Solar charge: we can switch 1P->3P; Isum=%.1fA, spare=%dA\n", (float)-Isum/10, spareCurrent);
                        }
                    }
                    else {
                        // not enough spare current to switch to 3P
                        setSolarStopTimer(0);
                        _LOG_D("Solar charge: not enough spare current to switch to 3P; Isum=%.1fA, spare=%dA\n", (float)-Isum/10, spareCurrent);
                    }

            }
            else {

                _LOG_D("Checkpoint b: Resetting SolarStopTimer, MaxSumMainsTimer, IsetBalanced=%.1fA, ActiveEVSE=%i.\n", (float)IsetBalanced/10, ActiveEVSE);
                setSolarStopTimer(0);
                MaxSumMainsTimer = 0;
                NoCurrent = 0;
            }
        }

        // ############### we now distribute the calculated IsetBalanced over the EVSEs  #################

        if (IsetBalanced > ActiveMax) IsetBalanced = ActiveMax;                 // limit to total maximum Amps (of all active EVSE's)
                                                                                // TODO not sure if Nr_Of_Phases_Charging should be involved here
        MaxBalanced = IsetBalanced;                                             // convert to Amps

        // Calculate average current per EVSE
        n = 0;
        while (n < NR_EVSES && ActiveEVSE) {
            Average = MaxBalanced / ActiveEVSE;                                 // Average current for all active EVSE's

            // Active EVSE, and current not yet calculated?
            if ((BalancedState[n] == STATE_C) && (!CurrentSet[n])) {            

                // Check for EVSE's that are starting with Solar charging
                if ((Mode == MODE_SOLAR) && (Node[n].IntTimer < SOLARSTARTTIME)) {
                    Balanced[n] = MinCurrent * 10;                              // Set to MinCurrent
                    _LOG_V("[S]Node %u = %u.%u A\n", n, Balanced[n]/10, Balanced[n]%10);
                    CurrentSet[n] = 1;                                          // mark this EVSE as set.
                    ActiveEVSE--;                                               // decrease counter of active EVSE's
                    MaxBalanced -= Balanced[n];                                 // Update total current to new (lower) value
                    IsetBalanced = TotalCurrent;
                    n = 0;                                                      // reset to recheck all EVSE's
                    continue;                                                   // ensure the loop restarts from the beginning
                
                // Check for EVSE's that have a Max Current that is lower then the average
                } else if (Average >= BalancedMax[n]) {
                    Balanced[n] = BalancedMax[n];                               // Set current to Maximum allowed for this EVSE
                    _LOG_V("[L]Node %u = %u.%u A\n", n, Balanced[n]/10, Balanced[n]%10);
                    CurrentSet[n] = 1;                                          // mark this EVSE as set.
                    ActiveEVSE--;                                               // decrease counter of active EVSE's
                    MaxBalanced -= Balanced[n];                                 // Update total current to new (lower) value
                    n = 0;                                                      // reset to recheck all EVSE's
                    continue;                                                   // ensure the loop restarts from the beginning
                }

            }
            n++;
        }

        // All EVSE's which had a Max current lower then the average are set.
        // Now calculate the current for the EVSE's which had a higher Max current
        n = 0;
        while (n < NR_EVSES && ActiveEVSE) {                                    // Check for EVSE's that are not set yet
            if ((BalancedState[n] == STATE_C) && (!CurrentSet[n])) {            // Active EVSE, and current not yet calculated?
                Balanced[n] = MaxBalanced / ActiveEVSE;                         // Set current to Average
                _LOG_V("[H]Node %u = %u.%u A.\n", n, Balanced[n]/10, Balanced[n]%10);
                CurrentSet[n] = 1;                                              // mark this EVSE as set.
                ActiveEVSE--;                                                   // decrease counter of active EVSE's
                MaxBalanced -= Balanced[n];                                     // Update total current to new (lower) value
            }                                                                   //TODO since the average has risen the other EVSE's should be checked for exceeding their MAX's too!
            n++;
        }
    } //ActiveEVSE && phasesLastUpdateFlag

    if (!saveActiveEVSE) { // no ActiveEVSEs so reset all timers
        _LOG_D("Checkpoint c: Resetting SolarStopTimer, MaxSumMainsTimer, IsetBalanced=%d.%dA, saveActiveEVSE=%u.\n", IsetBalanced/10, abs(IsetBalanced%10), saveActiveEVSE);
        setSolarStopTimer(0);
        MaxSumMainsTimer = 0;
        NoCurrent = 0;
    }

    // Reset flag that keeps track of new MainsMeter measurements
    phasesLastUpdateFlag = false;

    // ############### print all the distributed currents #################

    _LOG_V("Checkpoint 5 Isetbalanced=%d.%d A.\n", IsetBalanced/10, abs(IsetBalanced%10));
    if (LoadBl == 1) {
        _LOG_D("Balance: ");
        for (n = 0; n < NR_EVSES; n++) {
            _LOG_D_NO_FUNC("EVSE%u:%s(%u.%uA) ", n, StrStateName[BalancedState[n]], Balanced[n]/10, Balanced[n]%10);
        }
        _LOG_D_NO_FUNC("\n");
    }
    SEND_TO_ESP32(ChargeCurrent)
#ifndef SMARTEVSE_VERSION //CH32
    uint16_t Balanced0 = Balanced[0];
#endif
    SEND_TO_ESP32(Balanced0)
    SEND_TO_ESP32(IsetBalanced)
#else //ESP32v4
    printf("@CalcBalancedCurrent:%i\n", mod);
#endif
} //CalcBalancedCurrent


void Timer1S_singlerun(void) {
#ifndef SMARTEVSE_VERSION //CH32
printf("@MSG: DINGO State=%d, pilot=%d, AccessTimer=%d, PilotDisconnected=%d.\n", State, pilot, AccessTimer, PilotDisconnected);
#endif
#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40 //not on ESP32 v4
    static uint8_t Broadcast = 1;
#endif
#ifdef SMARTEVSE_VERSION //ESP32
    if (BacklightTimer) BacklightTimer--;                               // Decrease backlight counter every second.
    //_LOG_A("DINGO: RCMTestCounter=%u.\n", RCMTestCounter);
#endif
    // wait for Activation mode to start
    if (ActivationMode && ActivationMode != 255) {
        ActivationMode--;                                               // Decrease ActivationMode every second.
    }

    // activation Mode is active
    if (ActivationTimer) ActivationTimer--;                             // Decrease ActivationTimer every second.
#if MODEM
    if (State == STATE_MODEM_REQUEST){
#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40   //CH32 and v3 ESP32
        if (ToModemWaitStateTimer) ToModemWaitStateTimer--;
        else {
            setState(STATE_MODEM_WAIT);                                         // switch to state Modem 2
            _GLCD;
        }
#endif
    }

#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40   //CH32 and v3 ESP32
    if (State == STATE_MODEM_WAIT){
        if (ToModemDoneStateTimer) {
#if SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40 //v3 ; v4 stays in STATE_MODEM_WAIT indefinitely
            ToModemDoneStateTimer--;
#endif
        }
        else{
            setState(STATE_MODEM_DONE); 
            _GLCD;
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
            PILOT_DISCONNECTED;

            // Check whether the EVCCID matches the one required
            if (strcmp(RequiredEVCCID, "") == 0 || strcmp(RequiredEVCCID, EVCCID) == 0) {
                // We satisfied the EVCCID requirements, skip modem stages next time
                ModemStage = 1;

                setState(STATE_B);                                     // switch to STATE_B
                _GLCD;                                                // Re-init LCD (200ms delay)
            } else {
                // We actually do not want to continue charging and re-start at modem request after 60s
                ModemStage = 0;
                LeaveModemDeniedStateTimer = 60;

                // Change to MODEM_DENIED state
                setState(STATE_MODEM_DENIED);
                _GLCD;                                                // Re-init LCD (200ms delay)
            }
        }
    }

    if (State == STATE_MODEM_DENIED){
        if (LeaveModemDeniedStateTimer) LeaveModemDeniedStateTimer--;
        else{
            LeaveModemDeniedStateTimer = -1;           // reset ModemStateDeniedTimer
            setState(STATE_A);                         // switch to STATE_A
            PILOT_CONNECTED;
            _GLCD;                                     // Re-init LCD (200ms delay)
        }
    }
#endif

#endif
#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40   //CH32 and v3 ESP32
    if (State == STATE_C1) {
        if (C1Timer) C1Timer--;                                         // if the EV does not stop charging in 6 seconds, we will open the contactor.
        else {
            _LOG_A("State C1 timeout!\n");
            setState(STATE_B1);                                         // switch back to STATE_B1
#ifdef SMARTEVSE_VERSION //not CH32
            GLCD_init();                                                // Re-init LCD (200ms delay); necessary because switching contactors can cause LCD to mess up
#endif
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
        if (pilot == PILOT_12V){
            DisconnectTimeCounter = -1;
            printf("@DisconnectEvent\n");
        } else{ // Run again
            DisconnectTimeCounter = 0; 
        }
    }
#endif
#endif //CH32 and v3 ESP32

#if SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40 //ESP32 v3
    // Check if there is a RFID card in front of the reader
    if (RFIDReader) {
        if (OneWireReadCardId() ) {
            CheckRFID();
        } else {
            RFIDstatus = 0;
        }
    }
#endif
#if SMARTEVSE_VERSION >=40
    if (RFIDReader) Serial1.printf("@OneWireReadCardId\n");
    if (State == STATE_A && modem_state > MODEM_CONFIGURED && modem_state < MODEM_PRESET_NMK)
        modem_state = MODEM_PRESET_NMK;                                  // if we are not connected and the modem still thinks we are, we force the modem to start the NMK set procedure
#endif
    // When Solar Charging, once the current drops to MINcurrent a timer is started.
    // Charging is stopped when the timer reaches the time set in 'StopTime' (in minutes)
    // Except when Stoptime =0, then charging will continue.

#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40   //CH32 and v3 ESP32
    // once a second, measure temperature
    // range -40 .. +125C
    TempEVSE = TemperatureSensor();

    if (SolarStopTimer) {
        SolarStopTimer--;
        SEND_TO_ESP32(SolarStopTimer)
#if MQTT
        MQTTclient.publish(MQTTprefix + "/SolarStopTimer", SolarStopTimer, false, 0);
#endif
        if (SolarStopTimer == 0) {
            if (State == STATE_C) setState(STATE_C1);                   // tell EV to stop charging
            setErrorFlags(LESS_6A);                                     // Set error: not enough sun
        }
    }

    if (PilotDisconnectTime) PilotDisconnectTime--;                     // Decrease PilotDisconnectTimer

    // Charge timer
    for (uint8_t x = 0; x < NR_EVSES; x++) {
        if (BalancedState[x] == STATE_C) {
            Node[x].IntTimer++;
            Node[x].Timer++;
         } else Node[x].IntTimer = 0;                                    // Reset IntervalTime when not charging
    }

    // Every two seconds request measurement data from sensorbox/kwh meters.
    // and send broadcast to Node controllers.
    if (LoadBl < 2 && !Broadcast--) {                                   // Load Balancing mode: Master or Disabled
        ModbusRequest = 1;                                              // Start with state 1, also in Normal mode we want MainsMeter and EVmeter updated
        ModbusRequestLoop();
        //timeout = COMM_TIMEOUT; not sure if necessary, statement was missing in original code    // reset timeout counter (not checked for Master)
        Broadcast = 1;                                                  // repeat every two seconds
    }
#endif

    // When Smart or Solar Charging, once MaxSumMains is exceeded, a timer is started
    // Charging is stopped when the timer reaches the time set in 'MaxSumMainsTime' (in minutes)
    // Except when MaxSumMainsTime =0, then charging will continue.
    if (MaxSumMainsTimer) {
        MaxSumMainsTimer--;                                             // Decrease MaxSumMains counter every second.
        if (MaxSumMainsTimer == 0) {
            if (State == STATE_C) setState(STATE_C1);                   // tell EV to stop charging
            setErrorFlags(LESS_6A);                                     // Set error: LESS_6A
        }
    }

    if (ChargeDelay) setChargeDelay(ChargeDelay-1);                     // Decrease Charge Delay counter

    if (AccessTimer && State == STATE_A) {
        if (--AccessTimer == 0) {
            setAccess(OFF);                                             // re-lock EVSE
        }
    } else AccessTimer = 0;                                             // Not in state A, then disable timer

#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40   //CH32 and v3 ESP32
    if ((TempEVSE < (maxTemp - 10)) && (ErrorFlags & TEMP_HIGH)) {                  // Temperature below limit?
        clearErrorFlags(TEMP_HIGH); // clear Error
    }

    if ( (ErrorFlags & (LESS_6A) ) && (LoadBl < 2) && (IsCurrentAvailable())) {
        clearErrorFlags(LESS_6A);                                         // Clear Errors if there is enough current available, and Load Balancing is disabled or we are Master
        _LOG_I("No power/current Errors Cleared.\n");
    }

    // Mainsmeter defined, and power sharing set to Disabled or Master
    if (MainsMeter.Type && LoadBl < 2) {
        if ( MainsMeter.Timeout == 0 && !(ErrorFlags & CT_NOCOMM) && Mode != MODE_NORMAL) { // timeout if current measurement takes > 10 secs
            // When power sharing is set to Disabled/Master, and in Normal mode, do not timeout;
            // there might be MainsMeter/EVMeter configured that can be retrieved through the API.
            setErrorFlags(CT_NOCOMM);
            setStatePowerUnavailable();
            SB2.SoftwareVer = 0;
            _LOG_W("Error, MainsMeter communication error!\n");
        } else {
            if (MainsMeter.Timeout) MainsMeter.Timeout--;
        }
    // We are a Node, we will timeout if there is no communication with the Master controller.
    } else if (LoadBl > 1) {
        if (MainsMeter.Timeout == 0 && !(ErrorFlags & CT_NOCOMM)) {
            setErrorFlags(CT_NOCOMM);
            setStatePowerUnavailable();
            SB2.SoftwareVer = 0;
            _LOG_W("Error, Master communication error!\n");
        } else {
            if (MainsMeter.Timeout) MainsMeter.Timeout--;
        }
    } else
        MainsMeter.setTimeout(COMM_TIMEOUT);

    if (EVMeter.Type) {
        if ( EVMeter.Timeout == 0 && !(ErrorFlags & EV_NOCOMM) && Mode != MODE_NORMAL) {
            setErrorFlags(EV_NOCOMM);
            setStatePowerUnavailable();
            _LOG_W("Error, EV Meter communication error!\n");
        } else {
            if (EVMeter.Timeout) EVMeter.Timeout--;
        }
    } else
        EVMeter.setTimeout(COMM_EVTIMEOUT);
    
    // Clear communication error, if present
    if ((ErrorFlags & CT_NOCOMM) && MainsMeter.Timeout) clearErrorFlags(CT_NOCOMM);

    if ((ErrorFlags & EV_NOCOMM) && EVMeter.Timeout) clearErrorFlags(EV_NOCOMM);

    if (TempEVSE > maxTemp && !(ErrorFlags & TEMP_HIGH))                // Temperature too High?
    {
        setErrorFlags(TEMP_HIGH);
        setStatePowerUnavailable();
        _LOG_W("Error, temperature %u C !\n", TempEVSE);
    }

    if (ErrorFlags & LESS_6A) {
        if (ChargeDelay == 0) {
            if (Mode == MODE_SOLAR) { _LOG_I("Waiting for Solar power...\n"); }
            else { _LOG_I("Not enough current available!\n"); }
        }
        setStatePowerUnavailable();
        setChargeDelay(CHARGEDELAY);                                    // Set Chargedelay
    }
#endif

    //_LOG_A("Timer1S task free ram: %u\n", uxTaskGetStackHighWaterMark( NULL ));


#if MQTT
    if (lastMqttUpdate++ >= 10) {
        // Publish latest data, every 10 seconds
        // We will try to publish data faster if something has changed
        mqttPublishData();
    }
#endif

#ifndef SMARTEVSE_VERSION //CH32
    if (ErrorFlags & RCM_TEST) {
        if (RCMTestCounter) RCMTestCounter--;
        SEND_TO_ESP32(RCMTestCounter);                                        // CH32 needs it to prevent switch led blinking fast red during RCM test
        if (ErrorFlags & RCM_TRIPPED) {                                         // RCM test succeeded
            RCMTestCounter = 0;                                                 // disable counter
            SEND_TO_ESP32(RCMTestCounter);
            clearErrorFlags(RCM_TEST | RCM_TRIPPED);
        } else {
            if (RCMTestCounter == 1) {                                          // RCM test finished and failed, so RCM_TRIPPED is left false and RCM_TEST is left true
                if (State) setState(STATE_B1);
                printf("@LCDTimer:0\n");                                        // display the correct error message on the LCD
            }
        }
    }

 //   printf("10ms loop:%lu uS systick:%lu millis:%lu\n", elapsedmax/12, (uint32_t)SysTick->CNT, millis());
    // this section sends outcomes of functions and variables to ESP32 to fill Shadow variables
    // FIXME this section preferably should be empty
    printf("@IsCurrentAvailable:%u\n", IsCurrentAvailable());
    SEND_TO_ESP32(ErrorFlags)
    elapsedmax = 0;
#endif
} //Timer1S_singlerun




#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40   //CH32 and v3 ESP32
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
    //prepare registers 0x0020 thru 0x002A (including) to be sent
    uint8_t buf[sizeof(Balanced)+ 6], i;
    uint8_t *p=buf;
    memcpy(p, Balanced, sizeof(Balanced));
    p = p + sizeof(Balanced);
    // Irms values, we only send the 16 least significant bits (range -327.6A to +327.6A) per phase
    for ( i=0; i<3; i++) {
        p[i * 2] = MainsMeter.Irms[i] & 0xff;
        p[(i * 2) + 1] = MainsMeter.Irms[i] >> 8;
    }
    ModbusWriteMultipleRequest(BROADCAST_ADR, 0x0020, (uint16_t *) buf, 8 + 3);
}

/**
 * EVSE Register 0x02*: System configuration (same on all SmartEVSE in a LoadBalancing setup)
Regis 	Access 	Description 	                                        Unit 	Values
0x0200 	R/W 	EVSE mode 		                                        0:Normal / 1:Smart / 2:Solar
0x0201 	R/W 	EVSE Circuit max Current 	                        A 	10 - 160
0x0202 	R/W 	Grid type to which the Sensorbox is connected 		        0:4Wire / 1:3Wire
0x0203 	R/W 	Sensorbox 2 WiFi Mode                                   0:Disabled / 1:Enabled / 2:Portal
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
0x0001 	R/W 	Error 	                Bit 	1:LESS_6A / 2:NO_COMM / 4:TEMP_HIGH / 8:EV_NOCOMM / 16:RCD
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
0x0028 - 0x0030
        W 	Broadcast MainsMeter currents L1 - L3.
                                        0.1 A
**/

/**
 * Master receives Node status over modbus
 * Node -> Master
 *
 * @param uint8_t NodeAdr (1-7)
 */
void receiveNodeStatus(uint8_t *buf, uint8_t NodeNr) {
    Node[NodeNr].Online = 5;

    BalancedState[NodeNr] = buf[1];                                             // Node State
    BalancedError[NodeNr] = buf[3];                                             // Node Error status
    // Update Mode when changed on Node and not Smart/Solar Switch on the Master
    // Also make sure we are not in the menu.
    Node[NodeNr].Mode = buf[7];

    if ((Node[NodeNr].Mode != Mode) && Switch != 4 && !LCDNav && !NodeNewMode) {
        NodeNewMode = Node[NodeNr].Mode + 1;        // Store the new Mode in NodeNewMode, we'll update Mode in 'ProcessAllNodeStates'
#ifndef SMARTEVSE_VERSION //CH32
        printf("@NodeNewMode:%u.\n", Node[NodeNr].Mode + 1); //CH32 sends new value to ESP32
#endif
    }
    Node[NodeNr].SolarTimer = (buf[8] * 256) + buf[9];
    Node[NodeNr].ConfigChanged = buf[13] | Node[NodeNr].ConfigChanged;
    BalancedMax[NodeNr] = buf[15] * 10;                                         // Node Max ChargeCurrent (0.1A)
    _LOG_D("ReceivedNode[%u]Status State:%u (%s) Error:%u, BalancedMax:%u, Mode:%u, ConfigChanged:%u.\n", NodeNr, BalancedState[NodeNr], StrStateName[BalancedState[NodeNr]], BalancedError[NodeNr], BalancedMax[NodeNr], Node[NodeNr].Mode, Node[NodeNr].ConfigChanged);
}


/**
 * Send Energy measurement request over modbus
 *
 * @param uint8_t Meter
 * @param uint8_t Address
 * @param bool    Export (if exported energy is requested)
 */
void requestEnergyMeasurement(uint8_t Meter, uint8_t Address, bool Export) {
    uint8_t Count = 1;                                                          // by default it only takes 1 register to get the energy measurement
    uint16_t Register = EMConfig[Meter].ERegister;
    if (Export)
        Register = EMConfig[Meter].ERegister_Exp;

    switch (Meter) {
        case EM_FINDER_7E:
        case EM_EASTRON3P:
        case EM_EASTRON1P:
        case EM_WAGO:
            break;
        case EM_SOLAREDGE:
            // Note:
            // - SolarEdge uses 16-bit values, except for this measurement it uses 32bit int format
            // - EM_SOLAREDGE should not be used for EV Energy Measurements
            // fallthrough
        case EM_SINOTIMER:
            // Note:
            // - Sinotimer uses 16-bit values, except for this measurement it uses 32bit int format
            // fallthrough
        case EM_ABB:
            // Note:
            // - ABB uses 64bit values for this register (size 2)
            Count = 2;
            break;
        case EM_EASTRON3P_INV:
            if (Export)
                Register = EMConfig[Meter].ERegister;
            else
                Register = EMConfig[Meter].ERegister_Exp;
            break;
        default:
            if (Export)
                Count = 0; //refuse to do a request on exported energy if the meter doesnt support it
            break;
    }
    if (Count)
        requestMeasurement(Meter, Address, Register, Count);
}

/**
 * Send Power measurement request over modbus
 *
 * @param uint8_t Meter
 * @param uint8_t Address
 */
void requestPowerMeasurement(uint8_t Meter, uint8_t Address, uint16_t PRegister) {
    uint8_t Count = 1;                                                          // by default it only takes 1 register to get power measurement
    switch (Meter) {
        case EM_SINOTIMER:
            // Note:
            // - Sinotimer does not output total power but only individual power of the 3 phases
            Count = 3;
            break;
    }
    requestMeasurement(Meter, Address, PRegister, Count);
}


/**
 * Master checks node status requests, and responds with new state
 * Master -> Node
 *
 * @param uint8_t NodeAdr (1-7)
 * @return uint8_t success
 */
uint8_t processAllNodeStates(uint8_t NodeNr) {
    uint16_t values[5];
    uint8_t current, write = 0, regs = 2;                                       // registers are written when Node needs updating.

    values[0] = BalancedState[NodeNr];

    current = IsCurrentAvailable();
    if (current) {                                                              // Yes enough current
        if (BalancedError[NodeNr] & LESS_6A) {
            BalancedError[NodeNr] &= ~(LESS_6A);                                // Clear Error flags
            write = 1;
        }
    }

    if ((ErrorFlags & CT_NOCOMM) && !(BalancedError[NodeNr] & CT_NOCOMM)) {
        BalancedError[NodeNr] |= CT_NOCOMM;                                     // Send Comm Error on Master to Node
        write = 1;
    }

    // Check EVSE for request to charge states
    switch (BalancedState[NodeNr]) {
        case STATE_A:
            // Reset Node
            Node[NodeNr].IntTimer = 0;
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
                if ((BalancedError[NodeNr] & LESS_6A) == 0) {                   // Error flags cleared?
                    BalancedError[NodeNr] |= LESS_6A;                           // Normal or Smart Mode: Not enough current available
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
                if ((BalancedError[NodeNr] & LESS_6A) == 0) {          // Error flags cleared?
                    BalancedError[NodeNr] |= LESS_6A;                      // Normal or Smart Mode: Not enough current available
                    write = 1;
                }
                _LOG_I("- Not enough current!\n");
            }
            break;

        default:
            break;

    }

    // Here we set the Masters Mode to the one we received from a Slave/Node
    if (NodeNewMode) {
        setMode(NodeNewMode -1);
        NodeNewMode = 0;
#ifndef SMARTEVSE_VERSION //CH32
        printf("@NodeNewMode:%u.\n", 0); //CH32 sends new value to ESP32
#endif
    }    

    // Error Flags
    values[1] = BalancedError[NodeNr];
    // Charge Current
    values[2] = 0;                                                              // This does nothing for Nodes. Currently the Chargecurrent can only be written to the Master
    // Mode
    if (Node[NodeNr].Mode != Mode) {
        regs = 4;
        write = 1;
    }    
    values[3] = Mode;
    
    // SolarStopTimer
    if (abs((int16_t)SolarStopTimer - (int16_t)Node[NodeNr].SolarTimer) > 3) {  // Write SolarStoptimer to Node if time is off by 3 seconds or more.
        regs = 5;
        write = 1;
        values[4] = SolarStopTimer;
    }    

    if (write) {
        _LOG_D("processAllNode[%u]States State:%u (%s), BalancedError:%u, Mode:%u, SolarStopTimer:%u\n",NodeNr, BalancedState[NodeNr], StrStateName[BalancedState[NodeNr]], BalancedError[NodeNr], Mode, SolarStopTimer);
        ModbusWriteMultipleRequest(NodeNr+1 , 0x0000, values, regs);            // Write State, Error, Charge Current, Mode and Solar Timer to Node
    }

    return write;
}
#endif


#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=40 //CH32 and v4 ESP32
bool ReadIrms(char *SerialBuf) {
    char *ret;
    char token[64];
    strncpy(token, "Irms:", sizeof(token));
    //Irms:011,312,123,124 means: the meter on address 11(dec) has Irms[0] 312 dA, Irms[1] of 123 dA, Irms[2] of 124 dA.
    ret = strstr(SerialBuf, token);
    if (ret != NULL) {
        short unsigned int Address;
        int16_t Irms[3];
        int n = sscanf(ret,"Irms:%03hu,%hi,%hi,%hi", &Address, &Irms[0], &Irms[1], &Irms[2]);
        if (n == 4) {   //success
            if (Address == MainsMeter.Address) {
                for (int x = 0; x < 3; x++)
                    MainsMeter.Irms[x] = Irms[x];
                MainsMeter.setTimeout(COMM_TIMEOUT);
                CalcIsum();
            } else if (Address == EVMeter.Address) {
                for (int x = 0; x < 3; x++)
                    EVMeter.Irms[x] = Irms[x];
                EVMeter.setTimeout(COMM_EVTIMEOUT);
                EVMeter.CalcImeasured();
            }
            return true; //success
        } else {
            _LOG_A("Received corrupt %s, n=%d, message:%s.\n", token, n, SerialBuf);
        }
    }
    return false; //did not parse
}


bool ReadPowerMeasured(char *SerialBuf) {
    char *ret;
    char token[64];
    strncpy(token, "PowerMeasured:", sizeof(token));
    //printf("@PowerMeasured:%03u,%d\n", Address, PowerMeasured);
    ret = strstr(SerialBuf, token);
    if (ret != NULL) {
        short unsigned int Address;
        int16_t PowerMeasured;
        int n = sscanf(ret,"PowerMeasured:%03hu,%hi", &Address, &PowerMeasured);
        if (n == 2) {   //success
            if (Address == MainsMeter.Address) {
                MainsMeter.PowerMeasured = PowerMeasured;
            } else if (Address == EVMeter.Address) {
                EVMeter.PowerMeasured = PowerMeasured;
            }
            return true; //success
        } else {
            _LOG_A("Received corrupt %s, n=%d, message from WCH:%s.\n", token, n, SerialBuf);
        }
    }
    return false; //did not parse
}
#endif


#ifndef SMARTEVSE_VERSION //CH32 version
void ResetModemTimers(void) {
    ToModemWaitStateTimer = 0;
    ToModemDoneStateTimer = 0;
    LeaveModemDoneStateTimer = 0;
    LeaveModemDeniedStateTimer = 0;
    setAccess(OFF);
}


// CH32 receives info from ESP32
void CheckSerialComm(void) {
    static char SerialBuf[512];
    uint16_t len;
    char *ret;

    len = ReadESPdata(SerialBuf);
/*    char prnt[256];
    memcpy(prnt, SerialBuf, len);
    for (char *p = prnt; *p; p++) if (*p == '\n') *p = ';'; //replace \n by ;
    prnt[len]='\0'; //terminate NULL
    printf("@MSG: ReadESPdata[%u]=%s.\n", len, prnt);*/
    RxRdy1 = 0;
#ifndef WCH_VERSION
#define WCH_VERSION 0 //if WCH_VERSION not defined compile time, 0 means this firmware will be overwritten by any other version; it will be re-flashed every boot
//if you compile with
//    PLATFORMIO_BUILD_FLAGS='-DWCH_VERSION='"`date +%s`" pio run -e v4 -t upload
//the current time (in epoch) is compiled via WCH_VERSION in the CH32 firmware
//which will prevent it to be reflashed every reboot
//if you compile with -DWCH_VERSION=0 it will be reflashed every reboot (handy for dev's!)
//if you compile with -DWCH_VERSION=2000000000 if will be reflashed somewhere after 2033
#endif
    // Is it a request?
    char token[64];
    strncpy(token, "version?", sizeof(token));
    ret = strstr(SerialBuf, token);
    if (ret != NULL) printf("@version:%lu\n", (unsigned long) WCH_VERSION);          // Send WCH software version

    uint8_t tmp;
    CALL_ON_RECEIVE_PARAM(State:, setState)
    CALL_ON_RECEIVE_PARAM(SetCPDuty:, SetCPDuty)
    CALL_ON_RECEIVE_PARAM(SetCurrent:, SetCurrent)
    CALL_ON_RECEIVE_PARAM(CalcBalancedCurrent:, CalcBalancedCurrent)
    CALL_ON_RECEIVE_PARAM(setPilot:,setPilot)
    CALL_ON_RECEIVE_PARAM(PowerPanicCtrl:, PowerPanicCtrl)
    CALL_ON_RECEIVE_PARAM(RCmon:, RCmonCtrl);
    CALL_ON_RECEIVE(setStatePowerUnavailable)
    CALL_ON_RECEIVE(OneWireReadCardId)
    CALL_ON_RECEIVE_PARAM(setErrorFlags:, setErrorFlags)
    CALL_ON_RECEIVE_PARAM(clearErrorFlags:, clearErrorFlags)
    CALL_ON_RECEIVE(BroadcastSettings)
    CALL_ON_RECEIVE(ResetModemTimers)

    // these variables are owned by ESP32 and copies are kept in CH32:
    SET_ON_RECEIVE(Config:, Config)
    SET_ON_RECEIVE(Lock:, Lock)
    SET_ON_RECEIVE(CableLock:, CableLock)
    SET_ON_RECEIVE(Mode:, Mode)
    SET_ON_RECEIVE(Access:, tmp); if (ret) AccessStatus = (AccessStatus_t) tmp;
    SET_ON_RECEIVE(OverrideCurrent:, OverrideCurrent)
    SET_ON_RECEIVE(LoadBl:, LoadBl)
    SET_ON_RECEIVE(MaxMains:, MaxMains)
    SET_ON_RECEIVE(MaxSumMains:, MaxSumMains)
    SET_ON_RECEIVE(MaxCurrent:, MaxCurrent)
    SET_ON_RECEIVE(MinCurrent:, MinCurrent)
    SET_ON_RECEIVE(MaxCircuit:, MaxCircuit)
    SET_ON_RECEIVE(Switch:, Switch)
    SET_ON_RECEIVE(StartCurrent:, StartCurrent)
    SET_ON_RECEIVE(StopTime:, StopTime)
    SET_ON_RECEIVE(ImportCurrent:, ImportCurrent)
    SET_ON_RECEIVE(Grid:, Grid)
    SET_ON_RECEIVE(RFIDReader:, RFIDReader)
    SET_ON_RECEIVE(MainsMeterType:, MainsMeter.Type)
    SET_ON_RECEIVE(MainsMAddress:, MainsMeter.Address)
    SET_ON_RECEIVE(EVMeterType:, EVMeter.Type)
    SET_ON_RECEIVE(EVMeterAddress:, EVMeter.Address)
    //code from validate_settings for v4:
    if (LoadBl < 2) {
        Node[0].EVMeter = EVMeter.Type;
        Node[0].EVAddress = EVMeter.Address;
    }

    SET_ON_RECEIVE(EMEndianness:, EMConfig[EM_CUSTOM].Endianness)
    SET_ON_RECEIVE(EMIRegister:, EMConfig[EM_CUSTOM].IRegister)
    SET_ON_RECEIVE(EMIDivisor:, EMConfig[EM_CUSTOM].IDivisor)
    SET_ON_RECEIVE(EMURegister:, EMConfig[EM_CUSTOM].URegister)
    SET_ON_RECEIVE(EMUDivisor:, EMConfig[EM_CUSTOM].UDivisor)
    SET_ON_RECEIVE(EMPRegister:, EMConfig[EM_CUSTOM].PRegister)
    SET_ON_RECEIVE(EMPDivisor:, EMConfig[EM_CUSTOM].PDivisor)
    SET_ON_RECEIVE(EMERegister:, EMConfig[EM_CUSTOM].ERegister)
    SET_ON_RECEIVE(EMEDivisor:, EMConfig[EM_CUSTOM].EDivisor)
    SET_ON_RECEIVE(EMDataType:, tmp); if (ret) EMConfig[EM_CUSTOM].DataType = (mb_datatype) tmp;
    SET_ON_RECEIVE(EMFunction:, EMConfig[EM_CUSTOM].Function)
    SET_ON_RECEIVE(EnableC2:, tmp); if (ret) EnableC2 = (EnableC2_t) tmp;
    SET_ON_RECEIVE(maxTemp:, maxTemp)
    SET_ON_RECEIVE(MainsMeterTimeout:, MainsMeter.Timeout)
    SET_ON_RECEIVE(EVMeterTimeout:, EVMeter.Timeout)
    SET_ON_RECEIVE(ConfigChanged:, ConfigChanged)

    SET_ON_RECEIVE(ModemStage:, ModemStage)
    SET_ON_RECEIVE(homeBatteryCurrent:, homeBatteryCurrent); if (ret) homeBatteryLastUpdate=time(NULL);

    //these variables are owned by CH32 and copies are sent to ESP32:
    SET_ON_RECEIVE(SolarStopTimer:, SolarStopTimer)

    // Wait till initialized is set by ESP
    strncpy(token, "Initialized:", sizeof(token));
    ret = strstr(SerialBuf, token);          //no need to check the value of Initialized since we always send 1
    if (ret != NULL) {
        printf("@Config:OK\n"); //only print this on reception of string
        //we now have initialized the CH32 so here are some setup() like statements:
        Nr_Of_Phases_Charging = Force_Single_Phase_Charging() ? 1 : 3;              // to prevent unnecessary switching after boot
        SEND_TO_ESP32(Nr_Of_Phases_Charging)
    }
#if MODEM
    strncpy(token, "RequiredEVCCID:", sizeof(token));
    ret = strstr(SerialBuf, token);
    if (ret) {
        strncpy(RequiredEVCCID, ret+strlen(token), sizeof(RequiredEVCCID));
        if (RequiredEVCCID[0] == 0x0a) //empty string was sent
            RequiredEVCCID[0] = '\0';
    }

    strncpy(token, "EVCCID:", sizeof(token));
    ret = strstr(SerialBuf, token);
    if (ret) {
        strncpy(EVCCID, ret+strlen(token), sizeof(EVCCID));
        if (EVCCID[0] == 0x0a) //empty string was sent
            EVCCID[0] = '\0';
    }
#endif

    ReadIrms(SerialBuf);
    ReadPowerMeasured(SerialBuf);

    //if (LoadBl) {
    //    printf("Config@OK %u,Lock@%u,Mode@%u,Current@%u,Switch@%u,RCmon@%u,PwrPanic@%u,RFID@%u\n", Config, Lock, Mode, ChargeCurrent, Switch, RCmon, PwrPanic, RFIDReader);
//        ConfigChanged = 1;
    //}

    memset(SerialBuf, 0, len);    // clear SerialBuffer

}
#endif


// Task that handles the Cable Lock and modbus
// 
// called every 100ms
//
void Timer100ms_singlerun(void) {
#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40   //CH32 and v3 ESP32
static unsigned int locktimer = 0, unlocktimer = 0;
#endif

#ifndef SMARTEVSE_VERSION //CH32
    //Check Serial communication with ESP32
    if (RxRdy1) CheckSerialComm();
//make stuff compatible with CH32 terminology
#define digitalRead funDigitalRead
#define PIN_LOCK_IN LOCK_IN
#endif

#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40   //CH32 and v3 ESP32
    // Check if the cable lock is used
    if (!Config && Lock) {                                      // Socket used and Cable lock enabled?
        // UnlockCable takes precedence over LockCable
        if ((RFIDReader == 2 && AccessStatus == OFF) ||        // One RFID card can Lock/Unlock the charging socket (like a public charging station)
#if ENABLE_OCPP && defined(SMARTEVSE_VERSION) //run OCPP only on ESP32
        (OcppMode &&!OcppForcesLock) ||
#endif
            State == STATE_A) {                                 // The charging socket is unlocked when unplugged from the EV
            if (CableLock != 1 && Lock != 0) {                  // CableLock is Enabled, do not unlock
                if (unlocktimer == 0) {                         // 600ms pulse
                    ACTUATOR_UNLOCK;
                } else if (unlocktimer == 6) {
                    ACTUATOR_OFF;
                }
                if (unlocktimer++ > 7) {
                    if (digitalRead(PIN_LOCK_IN) == (Lock == 2 ? 1:0 ))         // still locked...
                    {
                        if (unlocktimer > 50) unlocktimer = 0;      // try to unlock again in 5 seconds
                    } else unlocktimer = 7;
                }
                locktimer = 0;
            }
        // Lock Cable    
        } else if (State != STATE_A                            // Lock cable when connected to the EV
#if ENABLE_OCPP && defined(SMARTEVSE_VERSION) //run OCPP only on ESP32
        || (OcppMode && OcppForcesLock)
#endif
        ) {
            if (locktimer == 0) {                               // 600ms pulse
                ACTUATOR_LOCK;
            } else if (locktimer == 6) {
                ACTUATOR_OFF;
            }
            if (locktimer++ > 7) {
                if (digitalRead(PIN_LOCK_IN) == (Lock == 2 ? 0:1 ))         // still unlocked...
                {
                    if (locktimer > 50) locktimer = 0;          // try to lock again in 5 seconds
                } else locktimer = 7;
            }
            unlocktimer = 0;
        }
    }
}

// Sequentially call the Mains/EVmeters, and polls Nodes
// Called by MBHandleError, and MBHandleData response functions.
// Once every two seconds started by Timer1s()
//
void ModbusRequestLoop() {

    static uint8_t PollEVNode = NR_EVSES;
    static uint16_t energytimer = 0;
    uint8_t updated = 0;

    // Every 2 seconds, request measurements from modbus meters
        // Slaves all have ModbusRequest at 0 so they never enter here
        switch (ModbusRequest) {                                            // State
            case 1:                                                         // PV kwh meter
                ModbusRequest++;
                // fall through
            case 2:                                                         // Sensorbox or kWh meter that measures -all- currents
                if (MainsMeter.Type && MainsMeter.Type != EM_API && MainsMeter.Type != EM_HOMEWIZARD_P1) { // we don't want modbus meter currents to conflict with EM_API and EM_HOMEWIZARD_P1 currents
                    _LOG_D("ModbusRequest %u: Request MainsMeter Measurement\n", ModbusRequest);
                    requestCurrentMeasurement(MainsMeter.Type, MainsMeter.Address);
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
                    // This will do the following:
                    // - Send a modbus request to the Node for it's EVmeter
                    // - Node responds with the Type and Address of the EVmeter
                    // - Master writes configuration flag reset value to Node
                    // - Node acks with the exact same message
                    // This takes around 50ms in total
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
                    updated = 1;
                    switch(EVMeter.Type) {
                        //these meters all have their power measured via receiveCurrentMeasurement already
                        case EM_EASTRON1P:
                        case EM_EASTRON3P:
                        case EM_EASTRON3P_INV:
                        case EM_ABB:
                        case EM_FINDER_7M:
                        case EM_SCHNEIDER:
                            updated = 0;
                            break;
                        default:
                            requestPowerMeasurement(Node[PollEVNode].EVMeter, Node[PollEVNode].EVAddress,EMConfig[Node[PollEVNode].EVMeter].PRegister);
                            break;
                    }
                    if (updated) break;  // do not break when EVmeter is one of the above types
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
                    requestNodeStatus(ModbusRequest - 5u);                   // Master, Request Node 1-8 status
                    break;
                }
                ModbusRequest = 13;
                // fall through
            case 13:
            case 14:
            case 15:
            case 16:
            case 17:
            case 18:
            case 19:
                // Here we write State, Error, Mode and SolarTimer to Online Nodes
                updated = 0;
                if (LoadBl == 1) {
                    do {       
                        if (Node[ModbusRequest - 12u].Online) {             // Skip if not online
                            if (processAllNodeStates(ModbusRequest - 12u) ) {
                                updated = 1;                                // Node updated 
                                break;
                            }
                        }
                    } while (++ModbusRequest < 20);

                } else ModbusRequest = 20;
                if (updated) break;  // break when Node updated
                // fall through
            case 20:                                                         // EV kWh meter, Current measurement
                // Request Current if EV meter is configured
                if (Node[PollEVNode].EVMeter && Node[PollEVNode].EVMeter != EM_API) {
                    _LOG_D("ModbusRequest %u: Request EVMeter Current Measurement Node %u\n", ModbusRequest, PollEVNode);
                    requestCurrentMeasurement(Node[PollEVNode].EVMeter, Node[PollEVNode].EVAddress);
                    break;
                }
                ModbusRequest++;
                // fall through
            case 21:
                // Request active energy if Mainsmeter is configured
                    if (MainsMeter.Type && MainsMeter.Type != EM_API && MainsMeter.Type != EM_HOMEWIZARD_P1 && MainsMeter.Type != EM_SENSORBOX ) { // EM_API, EM_HOMEWIZARD_P1 and Sensorbox do not support energy postings
                    energytimer++; //this ticks approx every second?!?
                    if (energytimer == 30) {
                        _LOG_D("ModbusRequest %u: Request MainsMeter Import Active Energy Measurement\n", ModbusRequest);
                        requestEnergyMeasurement(MainsMeter.Type, MainsMeter.Address, 0);
                        break;
                    }
                    if (energytimer >= 60) {
                        _LOG_D("ModbusRequest %u: Request MainsMeter Export Active Energy Measurement\n", ModbusRequest);
                        requestEnergyMeasurement(MainsMeter.Type, MainsMeter.Address, 1);
                        energytimer = 0;
                        break;
                    }
                }
                ModbusRequest++;
                // fall through
            default:
                // slave never gets here
                // what about normal mode with no meters attached?
                CalcBalancedCurrent(0);
                // No current left, or Overload (2x Maxmains)?
                if (Mode && (NoCurrent > 2 || MainsMeter.Imeasured > (MaxMains * 20))) { // I guess we don't want to set this flag in Normal mode, we just want to charge ChargeCurrent
                    // STOP charging for all EVSE's
                    // Display error message
                    setErrorFlags(LESS_6A); //NOCURRENT;
                    // Broadcast Error code over RS485
                    ModbusWriteSingleRequest(BROADCAST_ADR, 0x0001, ErrorFlags);
                    NoCurrent = 0;
                }
                if (LoadBl == 1 && !(ErrorFlags & CT_NOCOMM) ) BroadcastCurrent();               // When there is no Comm Error, Master sends current to all connected EVSE's

                if ((State == STATE_B || State == STATE_C) && !CPDutyOverride) SetCurrent(Balanced[0]); // set PWM output for Master //mind you, the !CPDutyOverride was not checked in Smart/Solar mode, but I think this was a bug!
                ModbusRequest = 0;
                //_LOG_A("Timer100ms task free ram: %u\n", uxTaskGetStackHighWaterMark( NULL ));
                break;
        } //switch
        if (ModbusRequest) ModbusRequest++;
#endif

#ifndef SMARTEVSE_VERSION //CH32
//not sure this is necessary
#undef digitalRead
#undef PIN_LOCK_IN
#endif
}

#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40   //CH32 and v3 ESP32
// Blink the RGB LED and LCD Backlight.
//
// NOTE: need to add multiple colour schemes 
//
// Task is called every 10ms
void BlinkLed_singlerun(void) {
static uint8_t RedPwm = 0, GreenPwm = 0, BluePwm = 0;
static uint8_t LedCount = 0;                                                   // Raw Counter before being converted to PWM value
static unsigned int LedPwm = 0;                                                // PWM value 0-255

    // RGB LED
#ifndef SMARTEVSE_VERSION //CH32
    if ((ErrorFlags & (CT_NOCOMM | EV_NOCOMM | TEMP_HIGH) ) || (((ErrorFlags & RCM_TRIPPED) != (ErrorFlags & RCM_TEST)) && !RCMTestCounter)) {
#else //v3 ESP32
    if (ErrorFlags & (RCM_TRIPPED | CT_NOCOMM | EV_NOCOMM | TEMP_HIGH) ) {
#endif
            LedCount += 20;                                                 // Very rapid flashing, RCD tripped or no Serial Communication.
            if (LedCount > 128) LedPwm = ERROR_LED_BRIGHTNESS;              // Red LED 50% of time on, full brightness
            else LedPwm = 0;
            RedPwm = LedPwm;
            GreenPwm = 0;
            BluePwm = 0;
    } else if (AccessStatus == OFF && CustomButton) {
        RedPwm = ColorCustom[0];
        GreenPwm = ColorCustom[1];
        BluePwm = ColorCustom[2];
    } else if (AccessStatus == OFF || State == STATE_MODEM_DENIED) {
        RedPwm = ColorOff[0];
        GreenPwm = ColorOff[1];
        BluePwm = ColorOff[2];
    } else if (ErrorFlags || ChargeDelay) {                                 // Waiting for Solar power or not enough current to start charging
            LedCount += 2;                                                  // Slow blinking.
            if (LedCount > 230) LedPwm = WAITING_LED_BRIGHTNESS;            // LED 10% of time on, full brightness
            else LedPwm = 0;

            if (CustomButton) {                                             // Blue for Custom, unless configured otherwise
                RedPwm = LedPwm * ColorCustom[0] / 255;
                GreenPwm = LedPwm * ColorCustom[1] / 255;
                BluePwm = LedPwm * ColorCustom[2] / 255;
            } else if (Mode == MODE_SOLAR) {                                // Orange for Solar, unless configured otherwise
                RedPwm = LedPwm * ColorSolar[0] / 255;
                GreenPwm = LedPwm * ColorSolar[1] / 255;
                BluePwm = LedPwm * ColorSolar[2] / 255;
            } else if (Mode == MODE_SMART) {                                // Green for Smart, unless configured otherwise
                RedPwm = LedPwm * ColorSmart[0] / 255;
                GreenPwm = LedPwm * ColorSmart[1] / 255;
                BluePwm = LedPwm * ColorSmart[2] / 255;
            } else {                                                        // Green for Normal, unless configured otherwise
                RedPwm = LedPwm * ColorNormal[0] / 255;
                GreenPwm = LedPwm * ColorNormal[1] / 255;
                BluePwm = LedPwm * ColorNormal[2] / 255;
            }    

#if ENABLE_OCPP && defined(SMARTEVSE_VERSION) //run OCPP only on ESP32
    } else if (OcppMode && (RFIDReader == 6 || RFIDReader == 0) &&
                millis() - OcppLastRfidUpdate < 200) {
        RedPwm = 128;
        GreenPwm = 128;
        BluePwm = 128;
    } else if (OcppMode && (RFIDReader == 6 || RFIDReader == 0) &&
                millis() - OcppLastTxNotification < 1000 && OcppTrackTxNotification == MicroOcpp::TxNotification::Authorized) {
        RedPwm = 0;
        GreenPwm = 255;
        BluePwm = 0;
    } else if (OcppMode && (RFIDReader == 6 || RFIDReader == 0) &&
                millis() - OcppLastTxNotification < 2000 && (OcppTrackTxNotification == MicroOcpp::TxNotification::AuthorizationRejected ||
                                                             OcppTrackTxNotification == MicroOcpp::TxNotification::DeAuthorized ||
                                                             OcppTrackTxNotification == MicroOcpp::TxNotification::ReservationConflict)) {
        RedPwm = 255;
        GreenPwm = 0;
        BluePwm = 0;
    } else if (OcppMode && (RFIDReader == 6 || RFIDReader == 0) &&
                millis() - OcppLastTxNotification < 300 && (OcppTrackTxNotification == MicroOcpp::TxNotification::AuthorizationTimeout ||
                                                            OcppTrackTxNotification == MicroOcpp::TxNotification::ConnectionTimeout)) {
        RedPwm = 255;
        GreenPwm = 0;
        BluePwm = 0;
    } else if (OcppMode && (RFIDReader == 6 || RFIDReader == 0) &&
                getChargePointStatus() == ChargePointStatus_Reserved) {
        RedPwm = 196;
        GreenPwm = 64;
        BluePwm = 0;
    } else if (OcppMode && (RFIDReader == 6 || RFIDReader == 0) &&
                (getChargePointStatus() == ChargePointStatus_Unavailable ||
                 getChargePointStatus() == ChargePointStatus_Faulted)) {
        RedPwm = 255;
        GreenPwm = 0;
        BluePwm = 0;
#endif //ENABLE_OCPP
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

        if (CustomButton) {                                             // Blue for Custom, unless configured otherwise
            RedPwm = LedPwm * ColorCustom[0] / 255;
            GreenPwm = LedPwm * ColorCustom[1] / 255;
            BluePwm = LedPwm * ColorCustom[2] / 255;
        } else if (Mode == MODE_SOLAR) {                                // Orange for Solar, unless configured otherwise
            RedPwm = LedPwm * ColorSolar[0] / 255;
            GreenPwm = LedPwm * ColorSolar[1] / 255;
            BluePwm = LedPwm * ColorSolar[2] / 255;
        } else if (Mode == MODE_SMART) {                                // Green for Smart, unless configured otherwise
            RedPwm = LedPwm * ColorSmart[0] / 255;
            GreenPwm = LedPwm * ColorSmart[1] / 255;
            BluePwm = LedPwm * ColorSmart[2] / 255;
        } else {                                                        // Green for Normal, unless configured otherwise
            RedPwm = LedPwm * ColorNormal[0] / 255;
            GreenPwm = LedPwm * ColorNormal[1] / 255;
            BluePwm = LedPwm * ColorNormal[2] / 255;
        }    

    }
#if SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40
    ledcWrite(RED_CHANNEL, RedPwm);
    ledcWrite(GREEN_CHANNEL, GreenPwm);
    ledcWrite(BLUE_CHANNEL, BluePwm);

#else // CH32
    // somehow the CH32 chokes on 255 values
    if (RedPwm > 254) RedPwm = 254;
    if (GreenPwm > 254) GreenPwm = 254;
    if (BluePwm > 254) BluePwm = 254;

    TIM3->CH1CVR = RedPwm;
    TIM3->CH2CVR = GreenPwm;
    TIM3->CH3CVR = BluePwm;
#endif
}
#endif

#if SMARTEVSE_VERSION >=40
void SendConfigToCH32() {
    // send configuration to WCH IC
    Serial1.printf("@Access:%u\n", AccessStatus);
    Serial1.printf("@MainsMeterType:%u\n", MainsMeter.Type);
    Serial1.printf("@MainsMAddress:%u\n", MainsMeter.Address);
    Serial1.printf("@EVMeterType:%u\n", EVMeter.Type);
    Serial1.printf("@EVMeterAddress:%u\n", EVMeter.Address);
    Serial1.printf("@EMEndianness:%u\n", EMConfig[EM_CUSTOM].Endianness);
    Serial1.printf("@EMIRegister:%u\n", EMConfig[EM_CUSTOM].IRegister);
    Serial1.printf("@EMIDivisor:%u\n", EMConfig[EM_CUSTOM].IDivisor);
    Serial1.printf("@EMURegister:%u\n", EMConfig[EM_CUSTOM].URegister);
    Serial1.printf("@EMUDivisor:%u\n", EMConfig[EM_CUSTOM].UDivisor);
    Serial1.printf("@EMPRegister:%u\n", EMConfig[EM_CUSTOM].PRegister);
    Serial1.printf("@EMPDivisor:%u\n", EMConfig[EM_CUSTOM].PDivisor);
    Serial1.printf("@EMERegister:%u\n", EMConfig[EM_CUSTOM].ERegister);
    Serial1.printf("@EMEDivisor:%u\n", EMConfig[EM_CUSTOM].EDivisor);
/*    uint8_t tmp;
    Serial1.printf("@EMDataType:%u\n", tmp); EMConfig[EM_CUSTOM].DataType = (mb_datatype) tmp;
    */
    Serial1.printf("@EMDataType:%u\n", EMConfig[EM_CUSTOM].DataType);
    Serial1.printf("@EMFunction:%u\n", EMConfig[EM_CUSTOM].Function);
#if MODEM
    Serial1.printf("@RequiredEVCCID:%s\n", RequiredEVCCID);
#endif
    SEND_TO_CH32(Config)
    SEND_TO_CH32(EnableC2)
    SEND_TO_CH32(Grid)
    SEND_TO_CH32(ImportCurrent)
    SEND_TO_CH32(LoadBl)
    SEND_TO_CH32(Lock)
    SEND_TO_CH32(CableLock)
    SEND_TO_CH32(MaxCircuit)
    SEND_TO_CH32(MaxCurrent)
    SEND_TO_CH32(MaxMains)
    SEND_TO_CH32(MaxSumMains)
    SEND_TO_CH32(MaxSumMainsTime)
    SEND_TO_CH32(maxTemp)
    SEND_TO_CH32(MinCurrent)
    SEND_TO_CH32(Mode)
    SEND_TO_CH32(RCmon)
    SEND_TO_CH32(RFIDReader)
    SEND_TO_CH32(StartCurrent)
    SEND_TO_CH32(StopTime)
    SEND_TO_CH32(Switch)
}


void Handle_ESP32_Message(char *SerialBuf, uint8_t *CommState) {
    char *ret;
    //since we read per separation character we know we have only one token per message,
    //so we can return if we have found one
    //TODO malformed messages when -DDBG_CH32=1 still disturb it all.....
    if (memcmp(SerialBuf, "MSG:", 4) == 0) {
        return;
    }
    if (memcmp(SerialBuf, "!Panic", 6) == 0) {
        PowerPanicESP();
        return;
    }

    char token[64];
    strncpy(token, "ExtSwitch:", sizeof(token));
    ret = strstr(SerialBuf, token);
    if (ret != NULL) {
        ExtSwitch.Pressed = atoi(ret+strlen(token));
        if (ExtSwitch.Pressed)
            ExtSwitch.TimeOfPress = millis();
        ExtSwitch.HandleSwitch();
        return;
    }
    //these variables are owned by ESP32, so if CH32 changes it it has to send copies:
    SET_ON_RECEIVE(NodeNewMode:, NodeNewMode)
    SET_ON_RECEIVE(ConfigChanged:, ConfigChanged)

    CALL_ON_RECEIVE_PARAM(Access:, setAccess)
    CALL_ON_RECEIVE_PARAM(OverrideCurrent:, setOverrideCurrent)
    CALL_ON_RECEIVE_PARAM(Mode:, setMode)
    CALL_ON_RECEIVE(write_settings)
#if MODEM
    CALL_ON_RECEIVE(DisconnectEvent)
#endif
    //these variables do not exist in CH32 so values are sent to ESP32
    SET_ON_RECEIVE(RFIDstatus:, RFIDstatus)
    SET_ON_RECEIVE(GridActive:, GridActive)
    SET_ON_RECEIVE(LCDTimer:, LCDTimer)
    SET_ON_RECEIVE(BacklightTimer:, BacklightTimer)

    //these variables are owned by CH32 and copies are sent to ESP32:
    SET_ON_RECEIVE(Pilot:, pilot)
    SET_ON_RECEIVE(Temp:, TempEVSE)
    SET_ON_RECEIVE(State:, State)
    SET_ON_RECEIVE(IsetBalanced:, IsetBalanced)
    SET_ON_RECEIVE(ChargeCurrent:, ChargeCurrent)
    SET_ON_RECEIVE(IsCurrentAvailable:, Shadow_IsCurrentAvailable)
    SET_ON_RECEIVE(ErrorFlags:, ErrorFlags)
    SET_ON_RECEIVE(ChargeDelay:, ChargeDelay)
    SET_ON_RECEIVE(SolarStopTimer:, SolarStopTimer)
    SET_ON_RECEIVE(Nr_Of_Phases_Charging:, Nr_Of_Phases_Charging)
    SET_ON_RECEIVE(RCMTestCounter:, RCMTestCounter)

    strncpy(token, "version:", sizeof(token));
    ret = strstr(SerialBuf, token);
    if (ret != NULL) {
        unsigned long WCHRunningVersion = atoi(ret+strlen(token));
        _LOG_V("version %lu received\n", WCHRunningVersion);
        SendConfigToCH32();
        Serial1.printf("@Initialized:1\n");      // this finalizes the Config setup phase
        *CommState = COMM_CONFIG_SET;
        return;
    }

    ret = strstr(SerialBuf, "Config:OK");
    if (ret != NULL) {
        _LOG_V("Config set\n");
        *CommState = COMM_STATUS_REQ;
        return;
    }

    strncpy(token, "EnableC2:", sizeof(token));
    ret = strstr(SerialBuf, token);
    if (ret != NULL) {
        EnableC2 = (EnableC2_t) atoi(ret+strlen(token)); //e
        return;
    }

    if (ReadIrms(SerialBuf)) return;
    if (ReadPowerMeasured(SerialBuf)) return;

    strncpy(token, "RFID:", sizeof(token));
    ret = strstr(SerialBuf, token);
    if (ret != NULL) {
        int n = sscanf(ret,"RFID:%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx", &RFID[0], &RFID[1], &RFID[2], &RFID[3], &RFID[4], &RFID[5], &RFID[6], &RFID[7]);
        if (n == 8) {   //success
            CheckRFID();
        } else {
            _LOG_A("Received corrupt %s, n=%d, message from WCH:%s.\n", token, n, SerialBuf);
        }
        return;
    }

    ret = strstr(SerialBuf, "Balanced0:");
    if (ret) {
        Balanced[0] = atoi(ret+strlen("Balanced0:"));
    }

    int32_t temp;
#define READMETER(X) \
    ret = strstr(SerialBuf, #X ":"); \
    if (ret) { \
        short unsigned int Address; \
        int n = sscanf(ret + strlen(#X), ":%03hu,%" SCNd32, &Address, &temp); \
        if (n == 2) { \
            if (Address == MainsMeter.Address) { \
                MainsMeter.X = temp; \
            } else if (Address == EVMeter.Address) { \
                EVMeter.X = temp; \
            } \
        } else { \
            _LOG_A("Received corrupt %s, n=%d, message from WCH:%s.\n", #X, n, SerialBuf); \
        } \
        return; \
    }

    READMETER(Energy);
    READMETER(EnergyMeterStart);
    READMETER(EnergyCharged);
    READMETER(Import_active_energy);
    READMETER(Export_active_energy);
}
#endif




void Timer10ms_singlerun(void) {
#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40   //CH32 and v3 ESP32
    static uint8_t DiodeCheck = 0;
    static uint16_t StateTimer = 0;                                                 // When switching from State B to C, make sure pilot is at 6v for 100ms
    BlinkLed_singlerun();
#else //v4
    static uint16_t idx = 0;
    static char SerialBuf[512];
    static uint8_t CommState = COMM_VER_REQ;
    static uint8_t CommTimeout = 0;
#endif

#ifndef SMARTEVSE_VERSION //CH32
    static uint32_t log1S = millis();
    //Check RS485 communication
    if (ModbusRxLen) CheckRS485Comm();
#else //v3 and v4
    static uint8_t LcdPwm = 0;

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

// Task that handles EVSE State Changes
// Reads buttons, and updates the LCD.
    static uint16_t old_sec = 0;
    getButtonState();

    // When one or more button(s) are pressed, we call GLCDMenu
    if (((ButtonState != 0x07) || (ButtonState != OldButtonState)) ) {
        // RCM was tripped, but RCM level is back to normal
        if ((ErrorFlags & RCM_TRIPPED) && (RCMFAULT == LOW || RCmon == 0)) {
            clearErrorFlags(RCM_TRIPPED);         // Clear RCM error bit
        }
        if (!LCDlock) GLCDMenu(ButtonState);    // LCD is unlocked, enter menu
    }

    // Update/Show Helpmenu
    if (LCDNav > MENU_ENTER && LCDNav < MENU_EXIT && (!SubMenu)) GLCDHelp();

    if (timeinfo.tm_sec != old_sec) {
        old_sec = timeinfo.tm_sec;
        _GLCD;
    }
#endif

#ifndef SMARTEVSE_VERSION // CH32
#define LOG1S(fmt, ...) \
    if (millis() > log1S + 1000) printf("@MSG: " fmt, ##__VA_ARGS__);
#else
#define LOG1S(fmt, ...) //dummy
#endif

#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40 //CH32 and v3
    // Check the external switch and RCM sensor
    ExtSwitch.CheckSwitch();
    // sample the Pilot line
    pilot = Pilot();
    LOG1S("WCH 10ms: state=%d, pilot=%d, ErrorFlags=%d, ChargeDelay=%d, AccessStatus=%d, MainsMeter.Type=%d.\n", State, pilot, ErrorFlags, ChargeDelay, AccessStatus, MainsMeter.Type);

    // ############### EVSE State A #################

    if (State == STATE_A || State == STATE_COMM_B || State == STATE_B1) {
        // When the pilot line is disconnected, wait for PilotDisconnectTime, then reconnect
        if (PilotDisconnected) {
#ifdef SMARTEVSE_VERSION //ESP32 v3
            if (PilotDisconnectTime == 0 && pilot == PILOT_NOK ) {          // Pilot should be ~ 0V when disconnected
#else //CH32
            if (PilotDisconnectTime == 0 && pilot == PILOT_3V ) {          // Pilot should be ~ 3V when disconnected TODO is this ok?
#endif
                PILOT_CONNECTED;
                PilotDisconnected = false;
                _LOG_A("Pilot Connected\n");
            }
        } else if (pilot == PILOT_12V) {                                    // Check if we are disconnected, or forced to State A, but still connected to the EV
            // If the RFID reader is set to EnableOne or EnableAll mode, and the Charging cable is disconnected
            // We start a timer to re-lock the EVSE (and unlock the cable) after 60 seconds.
            if ((RFIDReader == 2 || RFIDReader == 1) && AccessTimer == 0 && AccessStatus == ON) AccessTimer = RFIDLOCKTIME;

            if (State != STATE_A) setState(STATE_A);                        // reset state, incase we were stuck in STATE_COMM_B
            setChargeDelay(0);                                              // Clear ChargeDelay when disconnected.
            if (!EVMeter.ResetKwh) EVMeter.ResetKwh = 1;                    // when set, reset EV kWh meter on state B->C change.
        } else if ( pilot == PILOT_9V && ErrorFlags == NO_ERROR
            && ChargeDelay == 0 && AccessStatus == ON && State != STATE_COMM_B
#if MODEM
            && State != STATE_MODEM_REQUEST && State != STATE_MODEM_WAIT && State != STATE_MODEM_DONE   // switch to State B ?
#endif
                )
        {                                                                    // Allow to switch to state C directly if STATE_A_TO_C is set to PILOT_6V (see EVSE.h)
            DiodeCheck = 0;

            MaxCapacity = ProximityPin();                                   // Sample Proximity Pin

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
#if MODEM
                if (ModemStage == 0)
                    setState(STATE_MODEM_REQUEST);
                else
#endif
                    setState(STATE_B);                                          // switch to State B
                ActivationMode = 30;                                        // Activation mode is triggered if state C is not entered in 30 seconds.
                AccessTimer = 0;
            } else setErrorFlags(LESS_6A);                                   // Not enough power available
        } else if (pilot == PILOT_9V && State != STATE_B1 && State != STATE_COMM_B && AccessStatus == ON) {
            setState(STATE_B1);
        }
    } // State == STATE_A || State == STATE_COMM_B || State == STATE_B1

    if (State == STATE_COMM_B_OK) {
        setState(STATE_B);
        ActivationMode = 30;                                                // Activation mode is triggered if state C is not entered in 30 seconds.
        AccessTimer = 0;
    }

    // ############### EVSE State B #################

    if (State == STATE_B || State == STATE_COMM_C) {

        if (pilot == PILOT_12V) {                                           // Disconnected?
            setState(STATE_A);                                              // switch to STATE_A

        } else if (pilot == PILOT_6V && ++StateTimer > 50) {                // When switching from State B to C, make sure pilot is at 6V for at least 500ms
                                                                            // Fixes https://github.com/dingo35/SmartEVSE-3.5/issues/40
            if (DiodeCheck == 1 && ErrorFlags == NO_ERROR && ChargeDelay == 0 && AccessStatus == ON) {
                if (EVMeter.Type && EVMeter.ResetKwh) {
                    EVMeter.EnergyMeterStart = EVMeter.Energy;              // store kwh measurement at start of charging.
                    EVMeter.EnergyCharged = EVMeter.Energy - EVMeter.EnergyMeterStart; // Calculate Energy
                    EVMeter.ResetKwh = 0;                                   // clear flag, will be set when disconnected from EVSE (State A)
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
#ifdef SMARTEVSE_VERSION //not on CH32
                        if (!LCDNav) _GLCD;                                // Don't update the LCD if we are navigating the menu
#endif                                                                      // immediately update LCD (20ms)
                    } else setErrorFlags(LESS_6A);                          // Not enough power available
                }
            }

        // PILOT_9V
        } else if (pilot == PILOT_9V) {

            StateTimer = 0;                                                 // Reset State B->C transition timer
            if (ActivationMode == 0) {
                setState(STATE_ACTSTART);
                ActivationTimer = 3;
#ifdef SMARTEVSE_VERSION //v3 and v4
                SetCPDuty(0);                                               // PWM off,  channel 0, duty cycle 0%
#else //CH32
                TIM1->CH1CVR = 0;
#endif
            }
        }
        if (pilot == PILOT_DIODE) {
            DiodeCheck = 1;                                                 // Diode found, OK
            _LOG_A("Diode OK\n");
#ifdef SMARTEVSE_VERSION //v3 and v4
            timerAlarmWrite(timerA, PWM_5, false);                          // Enable Timer alarm, set to start of CP signal (5%)
#else //CH32
            TIM1->CH4CVR = PWM_5;
#endif
        }

    }

    // ############### EVSE State C1 #################

    if (State == STATE_C1)
    {
        if (pilot == PILOT_12V)
        {                                                                   // Disconnected or connected to EV without PWM
            setState(STATE_A);                                              // switch to STATE_A
#ifdef SMARTEVSE_VERSION //not on CH32
            GLCD_init();                                                    // Re-init LCD
#endif
        }
        else if (pilot == PILOT_9V)
        {
            setState(STATE_B1);                                             // switch to State B1
#ifdef SMARTEVSE_VERSION //not on CH32
            GLCD_init();                                                    // Re-init LCD
#endif
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
#ifdef SMARTEVSE_VERSION //not on CH32
        if (!LCDNav) _GLCD;                                                // immediately update LCD
#endif
    }

    // ############### EVSE State C #################

    if (State == STATE_C) {

        if (pilot == PILOT_12V) {                                           // Disconnected ?
            setState(STATE_A);                                              // switch back to STATE_A
#ifdef SMARTEVSE_VERSION //not on CH32
            GLCD_init();                                                    // Re-init LCD; necessary because switching contactors can cause LCD to mess up
#endif
        } else if (pilot == PILOT_9V) {
            setState(STATE_B);                                              // switch back to STATE_B
            DiodeCheck = 0;
#ifdef SMARTEVSE_VERSION //not on CH32
            GLCD_init();                                                    // Re-init LCD (200ms delay); necessary because switching contactors can cause LCD to mess up
#endif                                                                            // Mark EVSE as inactive (still State B)
        } else if (pilot != PILOT_6V) {                                     // Pilot level at anything else is an error
            if (++StateTimer > 50) {                                        // make sure it's not a glitch, by delaying by 500mS (re-using StateTimer here)
                StateTimer = 0;                                             // Reset StateTimer for use in State B
                setState(STATE_B);
                DiodeCheck = 0;
#ifdef SMARTEVSE_VERSION //not on CH32
                GLCD_init();                                                // Re-init LCD (200ms delay); necessary because switching contactors can cause LCD to mess up
#endif
            }

        } else StateTimer = 0;

    } // end of State C code
#if SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40 //v3 ESP32 only, v4 has this in CH32 evse.c interrupt routine
    // Residual current monitor active, and DC current > 6mA ?
    if (RCmon == 1 && digitalRead(PIN_RCM_FAULT) == HIGH) {
        delay(1);
        // check again, to prevent voltage spikes from tripping the RCM detection
        if (digitalRead(PIN_RCM_FAULT) == HIGH) {
            if (State) setState(STATE_B1);
            setErrorFlags(RCM_TRIPPED);
            LCDTimer = 0;                                                   // display the correct error message on the LCD
        }
    }
#endif //v3
#endif //v3 and CH32
#if SMARTEVSE_VERSION >= 40 //v4
    //ESP32 receives info from CH32
    //each message starts with @, : separates variable name from value, ends with \n
    //so @State:2\n would be a valid message
    while (Serial1.available()) {       // Process ALL available messages in one cycle
        idx = Serial1.readBytesUntil('\n', SerialBuf, sizeof(SerialBuf)-1);
        if (idx > 0) {
            SerialBuf[idx++] = '\n';
            SerialBuf[idx] = '\0';  // Null terminate for safety
            
            if (SerialBuf[0] == '@') {
                _LOG_D("[(%u)<-] %.*s", idx, idx, SerialBuf);
                Handle_ESP32_Message(SerialBuf, &CommState);
            } else {
                _LOG_W("Invalid message,SerialBuf: [(%u)] %.*s", idx, idx, SerialBuf);
            }
        } else {
            break; // No more complete messages
        }
    }

    // process data from mainboard
    if (CommTimeout == 0 && CommState != COMM_STATUS_RSP) {
        switch (CommState) {

            case COMM_VER_REQ:
                CommTimeout = 10;
                Serial1.print("@version?\n");            // send command to WCH ic
                _LOG_V("[->] version?\n");        // send command to WCH ic
                break;

            case COMM_CONFIG_SET:                       // Set mainboard configuration
                CommTimeout = 10;
                break;

            case COMM_STATUS_REQ:                       // Ready to receive status from mainboard
                CommTimeout = 10;
                Serial1.printf("@PowerPanicCtrl:0\n");
                Serial1.printf("@RCmon:%u\n", RCmon);
                CommState = COMM_STATUS_RSP;
        }
    }


    if (CommTimeout) CommTimeout--;

#endif //SMARTEVSE_VERSION v4

#ifndef SMARTEVSE_VERSION //CH32
    // Clear communication error, if present
    if ((ErrorFlags & CT_NOCOMM) && MainsMeter.Timeout == 10) clearErrorFlags(CT_NOCOMM);
    if (millis() > log1S + 1000) {
        log1S = millis();
    }
#endif

}

#ifdef SMARTEVSE_VERSION //v3 and v4
void Timer10ms(void * parameter) {
    // infinite loop
    while(1) {
        Timer10ms_singlerun();
        // Pause the task for 10ms
        vTaskDelay(10 / portTICK_PERIOD_MS);
    } // while(1) loop
}

void Timer100ms(void * parameter) {
    // infinite loop
    while(1) {
        Timer100ms_singlerun();
        // Pause the task for 100ms
        vTaskDelay(100 / portTICK_PERIOD_MS);
    } // while(1) loop
}

void Timer1S(void * parameter) {
    // infinite loop
    while(1) {
        Timer1S_singlerun();
        // Pause the task for 1000ms
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    } // while(1) loop
}
#endif //SMARTEVSE_VERSION


/**
 * Check minimum and maximum of a value and set the variable
 *
 * @param uint8_t MENU_xxx
 * @param uint16_t value
 * @return uint8_t success
 */
uint8_t setItemValue(uint8_t nav, uint16_t val) {
#ifdef SMARTEVSE_VERSION //TODO THIS SHOULD BE FIXED
    if (nav < MENU_EXIT) {
        if (val < MenuStr[nav].Min || val > MenuStr[nav].Max) return 0;
    }
#endif
    switch (nav) {
//TODO not sure if we have receivers for all ESP32 senders?
#define SETITEM(M, V) \
        case M: \
            V = val; \
            SEND_TO_CH32(V) \
            SEND_TO_ESP32(V) \
            break;
        SETITEM(MENU_MAX_TEMP, maxTemp)
        SETITEM(MENU_CONFIG, Config)
        SETITEM(MENU_MODE, Mode)
        SETITEM(MENU_START, StartCurrent)
        SETITEM(MENU_STOP, StopTime)
        SETITEM(MENU_IMPORT, ImportCurrent)
        SETITEM(MENU_MAINS, MaxMains)
        SETITEM(MENU_SUMMAINS, MaxSumMains)
        SETITEM(MENU_SUMMAINSTIME, MaxSumMainsTime)
        SETITEM(MENU_MIN, MinCurrent)
        SETITEM(MENU_MAX, MaxCurrent)
        SETITEM(MENU_CIRCUIT, MaxCircuit)
        SETITEM(MENU_LOCK, Lock)
        SETITEM(MENU_SWITCH, Switch)
        SETITEM(MENU_GRID, Grid)
        SETITEM(MENU_SB2_WIFI, SB2_WIFImode)
        SETITEM(MENU_MAINSMETER, MainsMeter.Type)
        SETITEM(MENU_MAINSMETERADDRESS, MainsMeter.Address)
        SETITEM(MENU_EVMETER, EVMeter.Type)
        SETITEM(MENU_EVMETERADDRESS, EVMeter.Address)
        SETITEM(MENU_EMCUSTOM_ENDIANESS, EMConfig[EM_CUSTOM].Endianness)
        SETITEM(MENU_EMCUSTOM_FUNCTION, EMConfig[EM_CUSTOM].Function)
        SETITEM(MENU_EMCUSTOM_UREGISTER, EMConfig[EM_CUSTOM].URegister)
        SETITEM(MENU_EMCUSTOM_UDIVISOR, EMConfig[EM_CUSTOM].UDivisor)
        SETITEM(MENU_EMCUSTOM_IREGISTER, EMConfig[EM_CUSTOM].IRegister)
        SETITEM(MENU_EMCUSTOM_IDIVISOR, EMConfig[EM_CUSTOM].IDivisor)
        SETITEM(MENU_EMCUSTOM_PREGISTER, EMConfig[EM_CUSTOM].PRegister)
        SETITEM(MENU_EMCUSTOM_PDIVISOR, EMConfig[EM_CUSTOM].PDivisor)
        SETITEM(MENU_EMCUSTOM_EREGISTER, EMConfig[EM_CUSTOM].ERegister)
        SETITEM(MENU_EMCUSTOM_EDIVISOR, EMConfig[EM_CUSTOM].EDivisor)
        SETITEM(MENU_RFIDREADER, RFIDReader)
        SETITEM(MENU_AUTOUPDATE, AutoUpdate)
        SETITEM(STATUS_SOLAR_TIMER, SolarStopTimer)
        SETITEM(STATUS_CONFIG_CHANGED, ConfigChanged)
        case MENU_C2:
            EnableC2 = (EnableC2_t) val;
            SEND_TO_CH32(EnableC2)
            SEND_TO_ESP32(EnableC2)
            break;
        case STATUS_MODE:
            if (Mode != val)                                                    // this prevents slave from waking up from OFF mode when Masters'
                                                                                // solarstoptimer starts to count
                setMode(val);
            break;
        case MENU_LOADBL:
#if SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40
            ConfigureModbusMode(val);
#endif
            LoadBl = val;
            break;
        case MENU_EMCUSTOM_DATATYPE:
            EMConfig[EM_CUSTOM].DataType = (mb_datatype)val;
            break;
#ifdef SMARTEVSE_VERSION
        case MENU_RCMON:
            RCmon = val;
            Serial1.printf("@RCmon:%u\n", RCmon);
            break;
        case MENU_WIFI:
            WIFImode = val;
            break;
        case MENU_LCDPIN:
            LCDPin = val;
            break;
#endif
        // Status writeable
        case STATUS_STATE:
            if (val != State) setState(val);
            break;
        case STATUS_ERROR:
            //we want ErrorFlags = val so:
            clearErrorFlags(0xFF);
            setErrorFlags(val);
            if (ErrorFlags) {                                                   // Is there an actual Error? Maybe the error got cleared?
                if (ErrorFlags & CT_NOCOMM) MainsMeter.setTimeout(0);           // clear MainsMeter.Timeout on a CT_NOCOMM error, so the error will be immediate.
                setStatePowerUnavailable();
                setChargeDelay(CHARGEDELAY);
                _LOG_V("Error message received!\n");
            } else {
                _LOG_V("Errors Cleared received!\n");
            }
            break;
        case STATUS_CURRENT:
            setOverrideCurrent(val);
            if (LoadBl < 2) MainsMeter.setTimeout(COMM_TIMEOUT);                // reset timeout when register is written
            break;
        case STATUS_ACCESS:
            setAccess((AccessStatus_t) val);
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
        case MENU_SUMMAINSTIME:
            return MaxSumMainsTime;
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
        case MENU_GRID:
            return Grid;
        case MENU_SB2_WIFI:
            return SB2_WIFImode;
        case MENU_MAINSMETER:
            return MainsMeter.Type;
        case MENU_MAINSMETERADDRESS:
            return MainsMeter.Address;
        case MENU_EVMETER:
            return EVMeter.Type;
        case MENU_EVMETERADDRESS:
            return EVMeter.Address;
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
#ifdef SMARTEVSE_VERSION //not on CH32
        case MENU_WIFI:
            return WIFImode;    
        case MENU_LCDPIN:
            return LCDPin;
#endif
        case MENU_AUTOUPDATE:
            return AutoUpdate;

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
            return AccessStatus;
        case STATUS_CONFIG_CHANGED:
            return ConfigChanged;

        // Status readonly
        case STATUS_MAX:
            return min(MaxCapacity,MaxCurrent);
        case STATUS_TEMP:
            return (signed int)TempEVSE;
#ifdef SMARTEVSE_VERSION //not on CH32
        case MENU_RCMON:
            return RCmon;
        case STATUS_SERIAL:
            return serialnr;
#endif
        default:
            return 0;
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
int16_t getBatteryCurrent(void) {
    if (Mode == MODE_SOLAR && ((uint32_t)homeBatteryLastUpdate > (millis()-60000))) {
        return homeBatteryCurrent;
    } else {
        homeBatteryCurrent = 0;
        homeBatteryLastUpdate = 0;
        return 0;
    }
}


void CalcIsum(void) {
    phasesLastUpdate = time(NULL);
    phasesLastUpdateFlag = true;                        // Set flag if a new Irms measurement is received.
    int16_t BatteryCurrent = getBatteryCurrent();
    int16_t batteryPerPhase = getBatteryCurrent() / 3;
    Isum = 0;
#if FAKE_SUNNY_DAY
    int32_t temp[3]={0, 0, 0};
    temp[0] = INJECT_CURRENT_L1 * 10;                   //Irms is in units of 100mA
    temp[1] = INJECT_CURRENT_L2 * 10;
    temp[2] = INJECT_CURRENT_L3 * 10;
#endif

    for (int x = 0; x < 3; x++) {
#if FAKE_SUNNY_DAY
        MainsMeter.Irms[x] = MainsMeter.Irms[x] - temp[x];
#endif
        IrmsOriginal[x] = MainsMeter.Irms[x];
        if (EnableC2 != ALWAYS_OFF) {                                           // so single phase users can signal to only correct battery current on first phase
            MainsMeter.Irms[x] -= batteryPerPhase;
        } else {
            if (x == 0) {
                MainsMeter.Irms[x] -= BatteryCurrent;
                //MainsMeter.Irms[0] -= getBatteryCurrent(); //for some strange reason this would f*ck up the CH32 ?!?!
            }
        }
        Isum = Isum + MainsMeter.Irms[x];
    }
    MainsMeter.CalcImeasured();
}


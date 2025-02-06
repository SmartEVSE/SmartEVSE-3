/*
 * This file has shared code between SmartEVSE-3, SmartEVSE-4 and SmartEVSE-4_CH32
 * #if SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40  //SmartEVSEv3 code
 * #if SMARTEVSE_VERSION >= 40  //SmartEVSEv4 code
 * #ifndef SMARTEVSE_VERSION   //CH32 code
 */

#include "main.h"
#include "stdio.h"
#include "stdlib.h"
#include "meter.h"
#include "modbus.h"
#include "memory.h"  //for memcpy
#include <time.h>

#ifdef SMARTEVSE_VERSION //ESP32
#define EXT extern
#include "esp32.h"
#include <ArduinoJson.h>
#include <SPI.h>
#include <Preferences.h>

#include <FS.h>

#include <WiFi.h>
#include "network.h"
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

#include <soc/rtc_io_struct.h>

//OCPP includes
#if ENABLE_OCPP
#include <MicroOcpp.h>
#include <MicroOcppMongooseClient.h>
#include <MicroOcpp/Core/Configuration.h>
#include <MicroOcpp/Core/Context.h>
#endif //ENABLE_OCPP

extern Preferences preferences;
struct DelayedTimeStruct DelayedStartTime;
struct DelayedTimeStruct DelayedStopTime;
#else //CH32
#define EXT extern "C"
#include "ch32.h"
#include "utils.h"
extern "C" {
    #include "ch32v003fun.h"
    void RCmonCtrl(uint8_t enable);
}
extern void CheckRS485Comm(void);
#endif

// Global data

//CALL_ON_RECEIVE(setStatePowerUnavailable) setStatePowerUnavailable() when setStatePowerUnavailable is received
#define CALL_ON_RECEIVE(X) \
    ret = strstr(SerialBuf, #X);\
    if (ret) \
        X();

//CALL_ON_RECEIVE_PARAM(State, setState) calls setState(param) when State:param is received
#define CALL_ON_RECEIVE_PARAM(X,Y) \
    ret = strstr(SerialBuf, #X);\
    if (ret) \
        Y(atoi(ret+strlen(#X)));
//SET_ON_RECEIVE(Pilot:, pilot) sets pilot=parm when Pilot:param is received
#define SET_ON_RECEIVE(X,Y) \
    ret = strstr(SerialBuf, #X);\
    if (ret) \
        Y = atoi(ret+strlen(#X));


uint8_t Initialized = INITIALIZED;                                          // When first powered on, the settings need to be initialized.
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
uint16_t MaxCurrent = MAX_CURRENT;                                          // Max Charge current (A)
uint16_t MinCurrent = MIN_CURRENT;                                          // Minimal current the EV is happy with (A)
uint8_t Mode = MODE;                                                        // EVSE mode (0:Normal / 1:Smart / 2:Solar)
uint32_t CurrentPWM = 0;                                                    // Current PWM duty cycle value (0 - 1024)
bool CPDutyOverride = false;
uint8_t Lock = LOCK;                                                        // Cable lock (0:Disable / 1:Solenoid / 2:Motor)
uint16_t MaxCircuit = MAX_CIRCUIT;                                          // Max current of the EVSE circuit (A)
uint8_t Config = CONFIG;                                                    // Configuration (0:Socket / 1:Fixed Cable)
uint8_t LoadBl = LOADBL;                                                    // Load Balance Setting (0:Disable / 1:Master / 2-8:Node)
uint8_t Switch = SWITCH;                                                    // External Switch (0:Disable / 1:Access B / 2:Access S / 
                                                                            // 3:Smart-Solar B / 4:Smart-Solar S / 5: Grid Relay
                                                                            // 6:Custom B / 7:Custom S)
                                                                            // B=momentary push <B>utton, S=toggle <S>witch
uint8_t RCmon = RC_MON;                                                     // Residual Current Monitor (0:Disable / 1:Enable)
uint8_t AutoUpdate = AUTOUPDATE;                                            // Automatic Firmware Update (0:Disable / 1:Enable)
uint16_t StartCurrent = START_CURRENT;
uint16_t StopTime = STOP_TIME;
uint16_t ImportCurrent = IMPORT_CURRENT;
uint8_t DelayedRepeat;                                                      // 0 = no repeat, 1 = daily repeat
uint8_t LCDlock = LCD_LOCK;                                                 // 0 = LCD buttons operational, 1 = LCD buttons disabled
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
uint8_t Nr_Of_Phases_Charging = 0;                                          // 0 = Undetected, 1,2,3 = nr of phases that was detected at the start of this charging session
Single_Phase_t Switching_To_Single_Phase = FALSE;

uint8_t State = STATE_A;
ErrorFl_t Error2;
uint8_t NextState;
uint8_t pilot;
uint8_t prev_pilot;

uint16_t MaxCapacity;                                                       // Cable limit (A) (limited by the wire in the charge cable, set automatically, or manually if Config=Fixed Cable)
uint16_t ChargeCurrent;                                                     // Calculated Charge Current (Amps *10)
uint16_t OverrideCurrent = 0;                                               // Temporary assigned current (Amps *10) (modbus)
int16_t Isum = 0;                                                           // Sum of all measured Phases (Amps *10) (can be negative)

// Load Balance variables
int16_t IsetBalanced = 0;                                                   // Max calculated current (Amps *10) available for all EVSE's
uint16_t Balanced[NR_EVSES] = {0, 0, 0, 0, 0, 0, 0, 0};                     // Amps value per EVSE
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

uint8_t lock1 = 0, lock2 = 1;
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
uint8_t NodeNewMode = 0;
uint8_t Access_bit = 0;                                                     // 0:No Access 1:Access to SmartEVSE
uint16_t CardOffset = CARD_OFFSET;                                          // RFID card used in Enable One mode

uint8_t ConfigChanged = 0;
uint8_t GridActive = 0;                                                     // When the CT's are used on Sensorbox2, it enables the GRID menu option.

uint16_t SolarStopTimer = 0;
uint8_t RFIDstatus = 0;
bool PilotDisconnected = false;
uint8_t PilotDisconnectTime = 0;                                            // Time the Control Pilot line should be disconnected (Sec)

uint8_t ActivationMode = 0, ActivationTimer = 0;
volatile uint16_t adcsample = 0;
volatile uint16_t ADCsamples[25];                                           // declared volatile, as they are used in a ISR
volatile uint8_t sampleidx = 0;
char str[20];
extern volatile uint16_t ADC_CP[NUM_ADC_SAMPLES];

int phasesLastUpdate = 0;
bool phasesLastUpdateFlag = false;
int16_t IrmsOriginal[3]={0, 0, 0};
int homeBatteryCurrent = 0;
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

#if ENABLE_OCPP
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
#ifdef SMARTEVSE_VERSION //v3 and v4
EXT hw_timer_t * timerA;
esp_adc_cal_characteristics_t * adc_chars_CP;
#endif

//functions
EXT void setup();
EXT void setState(uint8_t NewState);
EXT int8_t TemperatureSensor();
uint8_t OneWireReadCardId();
EXT uint8_t ProximityPin();
EXT void PowerPanicCtrl(uint8_t enable);
EXT void ModemPower(uint8_t enable);
EXT uint16_t WchVersion;
EXT uint8_t ReadESPdata(char *buf);

extern void printStatus(void);
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

extern const char StrStateName[15][13] = {"A", "B", "C", "D", "COMM_B", "COMM_B_OK", "COMM_C", "COMM_C_OK", "Activate", "B1", "C1", "MODEM1", "MODEM2", "MODEM_OK", "MODEM_DENIED"}; //note that the extern is necessary here because the const will point the compiler to internal linkage; https://cplusplus.com/forum/general/81640/
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
void clearErrorFlags(void) {
    ErrorFlags = NO_ERROR;
#if SMARTEVSE_VERSION >= 40 //v4 ESP32
    Serial1.printf("ErrorFlags:%u\n", ErrorFlags);
#endif
}


#ifndef SMARTEVSE_VERSION //CH32 version
void Button::HandleSwitch(void) {
    printf("ExtSwitch:%1u.\n", Pressed);
}
#else //v3 and v4
void Button::HandleSwitch(void) {
    if (Pressed) {
        // Switch input pulled low
        switch (Switch) {
            case 1: // Access Button
                setAccess(!Access_bit);                             // Toggle Access bit on/off
                _LOG_I("Access: %d\n", Access_bit);
                break;
            case 2: // Access Switch
                setAccess(true);
                break;
            case 3: // Smart-Solar Button
                break;
            case 4: // Smart-Solar Switch
                if (Mode == MODE_SOLAR) {
                    setMode(MODE_SMART);
                }
                break;
            case 5: // Grid relay
                GridRelayOpen = false;
                break;
            case 6: // Custom button B
                CustomButton = !CustomButton;
                break;
            case 7: // Custom button S
                CustomButton = true;
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
                if (millis() < TimeOfPress + 1500) {                            // short press
                    if (Mode == MODE_SMART) {
                        setMode(MODE_SOLAR);
                    } else if (Mode == MODE_SOLAR) {
                        setMode(MODE_SMART);
                    }
                    LCDTimer = 0;
                }
                break;
            case 4: // Smart-Solar Switch
                if (Mode == MODE_SMART) setMode(MODE_SOLAR);
                break;
            case 5: // Grid relay
                GridRelayOpen = true;
                break;
            case 6: // Custom button B
                break;
            case 7: // Custom button S
                CustomButton = false;
                break;
            default:
                break;
        }
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
        //TODO howto do this in v4 / CH32?
        if (Pressed && Switch == 3 && millis() > TimeOfPress + 1500) {
            if (State == STATE_C) {
                setState(STATE_C1);
                if (!TestState) ChargeDelay = 15;                               // Keep in State B for 15 seconds, so the Charge cable can be removed.
            }
        }
    }
#endif
}

Button ExtSwitch;

/**
 * Set EVSE mode
 * 
 * @param uint8_t Mode
 */
void setMode(uint8_t NewMode) {
#ifdef SMARTEVSE_VERSION //v3 and v4
#if SMARTEVSE_VERSION >= 40
    Serial1.printf("Mode:%u\n", Mode); //d
#endif
    // If mainsmeter disabled we can only run in Normal Mode
    if (!MainsMeter.Type && NewMode != MODE_NORMAL)
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

    if (NewMode == MODE_SMART || NewMode == MODE_SOLAR) {                       // the smart-solar button used to clear all those flags toggling between those modes
        ErrorFlags &= ~(NO_SUN | LESS_6A);                                      // Clear All errors //FIXME v4
        setSolarStopTimer(0);                                                   // Also make sure the SolarTimer is disabled.
        MaxSumMainsTimer = 0;
    }
    ChargeDelay = 0;                                                            // Clear any Chargedelay
    BacklightTimer = BACKLIGHT;                                                 // Backlight ON
    if (Mode != NewMode) NodeNewMode = NewMode + 1;
    Mode = NewMode;    

    if (switchOnLater)
        setAccess(1);

    //make mode and start/stoptimes persistent on reboot
    if (preferences.begin("settings", false) ) {                        //false = write mode
        preferences.putUChar("Mode", Mode);
        preferences.putULong("DelayedStartTim", DelayedStartTime.epoch2); //epoch2 only needs 4 bytes
        preferences.putULong("DelayedStopTime", DelayedStopTime.epoch2);   //epoch2 only needs 4 bytes
        preferences.putUShort("DelayedRepeat", DelayedRepeat);
        preferences.end();
    }
#else //CH32
    printf("Mode:%1u.\n", Mode); //a
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
#if MQTT
    MQTTclient.publish(MQTTprefix + "/SolarStopTimer", SolarStopTimer, false, 0);
#endif
}

#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40   //CH32 and v3 ESP32
/**
 * Checks all parameters to determine whether
 * we are going to force single phase charging
 * Returns true if we are going to do single phase charging
 * Returns false if we are going to do (traditional) 3 phase charing
 * This is only relevant on a 3f mains and 3f car installation!
 * 1f car will always charge 1f undetermined by CONTACTOR2
 */
uint8_t Force_Single_Phase_Charging() {                                         // abbreviated to FSPC
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
#endif

// Write duty cycle to pin
// Value in range 0 (0% duty) to 1024 (100% duty) for ESP32, 1000 (100% duty) for CH32
void SetCPDuty(uint32_t DutyCycle){
#if SMARTEVSE_VERSION >= 40 //ESP32
    Serial1.printf("SetCPDuty:%u\n", DutyCycle);
#else
#if SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40 //v3 ESP32
    ledcWrite(CP_CHANNEL, DutyCycle);                                       // update PWM signal
#endif
#ifndef SMARTEVSE_VERSION  //CH32
    // update PWM signal
    TIM1->CH1CVR = DutyCycle;
#endif
    CurrentPWM = DutyCycle;
#endif //v4
}

// Set Charge Current 
// Current in Amps * 10 (160 = 16A)
void SetCurrent(uint16_t current) {
#if SMARTEVSE_VERSION >= 40 //ESP32
    Serial1.printf("SetCurrent:%u\n", current);
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
    printf("setStatePowerUnavailable\n");
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
// a. ESP32 setState sends message to CH32              in ESP32 src/common.cpp (this file)
// b. CH32 receiver that calls local setState           in CH32 src/evse.c
// c. CH32 setState full functionality                  in ESP32 src/common.cpp (this file) to be copied to CH32
// d. CH32 sends message to ESP32                       in ESP32 src/common.cpp (this file) to be copied to CH32
// e. ESP32 receiver that sets local variable           in ESP32 src/main.cpp


void setState(uint8_t NewState) { //c
    if (State != NewState) {
#ifdef SMARTEVSE_VERSION //v3 and v4
        char Str[50];
        snprintf(Str, sizeof(Str), "%02d:%02d:%02d STATE %s -> %s\n",timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, StrStateName[State], StrStateName[NewState] );
        _LOG_A("%s",Str);
#if SMARTEVSE_VERSION >= 40
        Serial1.printf("State:%u\n", NewState); //a
#endif
#else //CH32
        printf("State:%1u.\n", NewState); //d
#endif
    }

#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40   //CH32 and v3 ESP32 //a
    switch (NewState) {
        case STATE_B1:
            if (!ChargeDelay) ChargeDelay = 3;                                  // When entering State B1, wait at least 3 seconds before switching to another state.
            if (State != STATE_B1 && State != STATE_B && !PilotDisconnected) {
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
                ErrorFlags &= ~NO_SUN;
                ErrorFlags &= ~LESS_6A;
                ChargeDelay = 0;
                Switching_To_Single_Phase = FALSE;
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
#if MODEM
            CP_ON;
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

            if (Switching_To_Single_Phase == GOING_TO_SWITCH) {
                    CONTACTOR2_OFF;
                    setSolarStopTimer(0); //TODO still needed? now we switched contactor2 off, review if we need to stop solar charging
                    MaxSumMainsTimer = 0;
                    //Nr_Of_Phases_Charging = 1; this will be detected automatically
                    Switching_To_Single_Phase = AFTER_SWITCH;                   // we finished the switching process,
                                                                                // BUT we don't know which is the single phase
            }

            CONTACTOR1_ON;
            if (!Force_Single_Phase_Charging() && Switching_To_Single_Phase != AFTER_SWITCH) {                               // in AUTO mode we start with 3phases
                CONTACTOR2_ON;                                                  // Contactor2 ON
            }
            LCDTimer = 0;
            break;
        case STATE_C1:
#ifdef SMARTEVSE_VERSION //v3
            SetCPDuty(1024);                                                    // PWM off,  channel 0, duty cycle 100%
            timerAlarmWrite(timerA, PWM_100, true);                             // Alarm every 1ms, auto reload
#else //CH32                                                                          // EV should detect and stop charging within 3 seconds
            TIM1->CH1CVR = 1000;                                                // Set CP output to +12V
#endif
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

#endif //SMARTEVSE_VERSION
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
// a. CH32 setAccess sends message to ESP32           in CH32 src/evse.c and/or in src/common.cpp (this file)
// b. ESP32 receiver that calls local setAccess       in ESP32 src/main.cpp
// c. ESP32 setAccess full functionality              in ESP32 src/common.cpp (this file)
// d. ESP32 sends message to CH32                     in ESP32 src/common.cpp (this file)
// e. CH32 receiver that sets local variable          in CH32 src/evse.c

// same for Mode/setMode

void setAccess(bool Access) { //c
#ifdef SMARTEVSE_VERSION //v3 and v4
    Access_bit = Access;
#if SMARTEVSE_VERSION >= 40
    Serial1.printf("Access:%u\n", Access_bit); //d
#endif
    if (Access == 0) {
        //TODO:setStatePowerUnavailable() ?
        if (State == STATE_C) setState(STATE_C1);                               // Determine where to switch to.
        else if (State != STATE_C1 && (State == STATE_B || State == STATE_MODEM_REQUEST || State == STATE_MODEM_WAIT || State == STATE_MODEM_DONE || State == STATE_MODEM_DENIED)) setState(STATE_B1);
    }

    //make mode and start/stoptimes persistent on reboot
    if (preferences.begin("settings", false) ) {                        //false = write mode
        preferences.putUChar("Access", Access_bit);
        preferences.putUShort("CardOffs16", CardOffset);
        preferences.end();
    }

#if MQTT
    // Update MQTT faster
    lastMqttUpdate = 10;
#endif //MQTT
#else //CH32
    printf("Access:%1u.\n", Access); //a
#endif //SMARTEVSE_VERSION
}


#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40   //CH32 and v3 ESP32
// Set global var Nr_Of_Phases_Charging
// 0 = undetected, 1 - 3 nr of phases we are charging
// returns nr of phases we are charging, and 3 if undetected
int Set_Nr_of_Phases_Charging(void) {
    uint32_t Max_Charging_Prob = 0;
    uint32_t Charging_Prob=0;                                        // Per phase, the probability that Charging is done at this phase
    Nr_Of_Phases_Charging = 0;
#define THRESHOLD 40
#define BOTTOM_THRESHOLD 25
    _LOG_D("Detected Charging Phases: ChargeCurrent=%u, Balanced[0]=%u.%u A, IsetBalanced=%u.%u A.\n", ChargeCurrent, Balanced[0]/10, Balanced[0]%10, IsetBalanced/10, IsetBalanced%10);
    for (int i=0; i<3; i++) {
        if (EVMeter.Type) {
            Charging_Prob = 10 * (abs(EVMeter.Irms[i] - IsetBalanced)) / IsetBalanced;  //100% means this phase is charging, 0% mwans not charging
                                                                                        //TODO does this work for the slaves too?
            _LOG_D("Trying to detect Charging Phases END EVMeter.Irms[%u]=%d.%d A.\n", i, EVMeter.Irms[i]/10, EVMeter.Irms[i]%10);
        }
        Max_Charging_Prob = max(Charging_Prob, Max_Charging_Prob);

        //normalize percentages so they are in the range [0-100]
        if (Charging_Prob >= 200)
            Charging_Prob = 0;
        if (Charging_Prob > 100)
            Charging_Prob = 200 - Charging_Prob;
#ifdef SMARTEVSE_VERSION //ESP32
        _LOG_I("Detected Charging Phases: Charging_Prob[%u]=%u.\n", i, Charging_Prob);
#else
        _LOG_I("Detected Charging Phases: Charging_Prob[%u]=%lu.\n", i, Charging_Prob);
#endif
        if (Charging_Prob == Max_Charging_Prob) {
            _LOG_D("Suspect I am charging at phase: L%u.\n", i+1);
            Nr_Of_Phases_Charging++;
        }
        else {
            if ( Charging_Prob <= BOTTOM_THRESHOLD ) {
                _LOG_D("Suspect I am NOT charging at phase: L%u.\n", i+1);
            }
            else {
                if ( Max_Charging_Prob - Charging_Prob <= THRESHOLD ) {
                    _LOG_D("Serious candidate for charging at phase: L%u.\n", i+1);
                    Nr_Of_Phases_Charging++;
                }
            }
        }
    }

    // sanity checks
    if (EnableC2 != AUTO && EnableC2 != NOT_PRESENT) {                         // no further sanity checks possible when AUTO or NOT_PRESENT
        if (Nr_Of_Phases_Charging != 1 && (EnableC2 == ALWAYS_OFF || (EnableC2 == SOLAR_OFF && Mode == MODE_SOLAR))) {
            _LOG_A("Error in detecting phases: EnableC2=%s and Nr_Of_Phases_Charging=%u.\n", StrEnableC2[EnableC2], Nr_Of_Phases_Charging);
            Nr_Of_Phases_Charging = 1;
            _LOG_A("Setting Nr_Of_Phases_Charging to 1.\n");
        }
        if (!Force_Single_Phase_Charging() && Nr_Of_Phases_Charging != 3) {//TODO 2phase charging very rare?
            _LOG_A("Possible error in detecting phases: EnableC2=%s and Nr_Of_Phases_Charging=%u.\n", StrEnableC2[EnableC2], Nr_Of_Phases_Charging);
        }
    }

    _LOG_A("Charging at %u phases.\n", Nr_Of_Phases_Charging);
    if (Nr_Of_Phases_Charging == 0)
        return 3;
    return Nr_Of_Phases_Charging;
}
#endif

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

    //printf("MSG: min:%u max:%u\n",Min ,Max);

    // test Min/Max against fixed levels    (needs testing)
    ret = PILOT_NOK;                                                        // Pilot NOT ok
    if (Min >= 4000 ) ret = PILOT_12V;                                      // Pilot at 12V
    if ((Min >= 3300) && (Max < 4000)) ret = PILOT_9V;                      // Pilot at 9V
    if ((Min >= 2400) && (Max < 3300)) ret = PILOT_6V;                      // Pilot at 6V
    if ((Min >= 2000) && (Max < 2400)) ret = PILOT_3V;                      // Pilot at 3V
    if ((Min > 100) && (Max < 350)) ret = PILOT_DIODE;                      // Diode Check OK
    if (ret != old_pilot) {
        printf("Pilot:%u\n", ret); //d
        old_pilot = ret;
    }
    return ret;
}
#else //v3 or v4
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

    for (n = 0; n < NR_EVSES; n++) if (BalancedState[n] == STATE_C)             // must be in STATE_C
    {
        ActiveEVSE++;                                                           // Count nr of active (charging) EVSE's
        TotalCurrent += Balanced[n];                                            // Calculate total of all set charge currents
    }

    // Allow solar Charging if surplus current is above 'StartCurrent' (sum of all phases)
    // Charging will start after the timeout (chargedelay) period has ended
     // Only when StartCurrent configured or Node MinCurrent detected or Node inactive
    if (Mode == MODE_SOLAR) {                                                   // no active EVSE yet?
        if (ActiveEVSE == 0 && Isum >= ((signed int)StartCurrent *-10)) {
            _LOG_D("No current available StartCurrent line %d. ActiveEVSE=%u, TotalCurrent=%d.%dA, StartCurrent=%uA, Isum=%d.%dA, ImportCurrent=%uA.\n", __LINE__, ActiveEVSE, TotalCurrent/10, TotalCurrent%10, StartCurrent, Isum/10, Isum%10, ImportCurrent);
            return 0;
        }
        else if ((ActiveEVSE * MinCurrent * 10) > TotalCurrent) {               // check if we can split the available current between all active EVSE's
            _LOG_D("No current available StartCurrent line %d. ActiveEVSE=%u, TotalCurrent=%d.%dA, StartCurrent=%uA, Isum=%d.%dA, ImportCurrent=%uA.\n", __LINE__, ActiveEVSE, TotalCurrent/10, TotalCurrent%10, StartCurrent, Isum/10, Isum%10, ImportCurrent);
            return 0;
        }
        else if (ActiveEVSE > 0 && Isum > ((signed int)ImportCurrent * 10) + TotalCurrent - (ActiveEVSE * MinCurrent * 10)) {
            _LOG_D("No current available StartCurrent line %d. ActiveEVSE=%u, TotalCurrent=%d.%dA, StartCurrent=%uA, Isum=%d.%dA, ImportCurrent=%uA.\n", __LINE__, ActiveEVSE, TotalCurrent/10, TotalCurrent%10, StartCurrent, Isum/10, Isum%10, ImportCurrent);
            return 0;
        }
    }

    ActiveEVSE++;                                                           // Do calculations with one more EVSE
    if (ActiveEVSE > NR_EVSES) ActiveEVSE = NR_EVSES;
    Baseload = MainsMeter.Imeasured - TotalCurrent;                         // Calculate Baseload (load without any active EVSE)
    Baseload_EV = EVMeter.Imeasured - TotalCurrent;                         // Load on the EV subpanel excluding any active EVSE
    if (Baseload_EV < 0) Baseload_EV = 0;                                   // so Baseload_EV = 0 when no EVMeter installed

    // Check if the lowest charge current(6A) x ActiveEV's + baseload would be higher then the MaxMains.
    if ((ActiveEVSE * (MinCurrent * 10) + Baseload) > (MaxMains * 10)) {
        _LOG_D("No current available MaxMains line %d. ActiveEVSE=%u, Baseload=%d.%dA, MinCurrent=%uA, MaxMains=%uA.\n", __LINE__, ActiveEVSE, Baseload/10, Baseload%10, MinCurrent, MaxMains);
        return 0;                                                           // Not enough current available!, return with error
    }
    if (((LoadBl == 0 && EVMeter.Type && Mode != MODE_NORMAL) || LoadBl == 1) // Conditions in which MaxCircuit has to be considered
        && ((ActiveEVSE * (MinCurrent * 10) + Baseload_EV) > (MaxCircuit * 10))) { // MaxCircuit is exceeded
        _LOG_D("No current available MaxCircuit line %d. ActiveEVSE=%u, Baseload_EV=%d.%dA, MinCurrent=%uA, MaxCircuit=%uA.\n", __LINE__, ActiveEVSE, Baseload_EV/10, Baseload_EV%10, MinCurrent, MaxCircuit);
        return 0;                                                           // Not enough current available!, return with error
    }
    //assume the current should be available on all 3 phases
    bool must_be_single_phase_charging = (EnableC2 == ALWAYS_OFF || (Mode == MODE_SOLAR && EnableC2 == SOLAR_OFF) ||
            (Mode == MODE_SOLAR && EnableC2 == AUTO && Switching_To_Single_Phase == AFTER_SWITCH));
    int Phases = must_be_single_phase_charging ? 1 : 3;
    if (MaxSumMains && ((Phases * ActiveEVSE * MinCurrent * 10) + Isum > MaxSumMains * 10)) {
        _LOG_D("No current available MaxSumMains line %d. ActiveEVSE=%u, MinCurrent=%uA, Isum=%d.%dA, MaxSumMains=%uA.\n", __LINE__, ActiveEVSE, MinCurrent, Isum/10, Isum%10, MaxSumMains);
        return 0;                                                           // Not enough current available!, return with error
    }

// Use OCPP Smart Charging if Load Balancing is turned off
#if ENABLE_OCPP
    if (OcppMode &&                            // OCPP enabled
            !LoadBl &&                         // Internal LB disabled
            OcppCurrentLimit >= 0.f &&         // OCPP limit defined
            OcppCurrentLimit < MinCurrent) {  // OCPP suspends charging
        _LOG_D("OCPP Smart Charging suspends EVSE\n");
        return 0;
    }
#endif //ENABLE_OCPP

    _LOG_D("Current available checkpoint D. ActiveEVSE increased by one=%u, TotalCurrent=%u.%uA, StartCurrent=%uA, Isum=%u.%uA, ImportCurrent=%uA.\n", ActiveEVSE, TotalCurrent/10, TotalCurrent%10, StartCurrent, Isum/10, Isum%10, ImportCurrent);
    return 1;
}
#else //v4 ESP32
bool Shadow_IsCurrentAvailable; // this is a global variable that will be kept uptodate by Timer1S on CH32
char IsCurrentAvailable(void) {
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
#if ENABLE_OCPP
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

    _LOG_V("Checkpoint 1 Isetbalanced=%d.%d A Imeasured=%d.%d A MaxCircuit=%d Imeasured_EV=%d.%d A, Battery Current = %u.%u A, mode=%u.\n", IsetBalanced/10, IsetBalanced%10, MainsMeter.Imeasured/10, MainsMeter.Imeasured%10, MaxCircuit, EVMeter.Imeasured/10, EVMeter.Imeasured%10, homeBatteryCurrent/10, homeBatteryCurrent%10, Mode);

    Baseload_EV = EVMeter.Imeasured - TotalCurrent;                             // Calculate Baseload (load without any active EVSE)
    if (Baseload_EV < 0)
        Baseload_EV = 0;
    Baseload = MainsMeter.Imeasured - TotalCurrent;                             // Calculate Baseload (load without any active EVSE)

    // ############### now calculate IsetBalanced #################

    if (Mode == MODE_NORMAL)                                                    // Normal Mode
    {
        if (LoadBl == 1)                                                        // Load Balancing = Master? MaxCircuit is max current for all active EVSE's;
            IsetBalanced = min((MaxMains * 10) - Baseload, (MaxCircuit * 10 ) - Baseload_EV);
                                                                                // limiting is per phase so no Nr_Of_Phases_Charging here!
        else
            IsetBalanced = ChargeCurrent;                                       // No Load Balancing in Normal Mode. Set current to ChargeCurrent (fix: v2.05)
    } //end MODE_NORMAL
    else { // start MODE_SOLAR || MODE_SMART
        // adapt IsetBalanced in Smart Mode, and ensure the MaxMains/MaxCircuit settings for Solar

        uint8_t Temp_Phases;
        Temp_Phases = (Nr_Of_Phases_Charging ? Nr_Of_Phases_Charging : 3);      // in case nr of phases not detected, assume 3
        if ((LoadBl == 0 && EVMeter.Type) || LoadBl == 1)                       // Conditions in which MaxCircuit has to be considered;
                                                                                // mode = Smart/Solar so don't test for that
            Idifference = min((MaxMains * 10) - MainsMeter.Imeasured, (MaxCircuit * 10) - EVMeter.Imeasured);
        else
            Idifference = (MaxMains * 10) - MainsMeter.Imeasured;
        if (MaxSumMains && (Idifference > ((MaxSumMains * 10) - Isum)/Temp_Phases)) {
            Idifference = ((MaxSumMains * 10) - Isum)/Temp_Phases;
            LimitedByMaxSumMains = true;
            _LOG_V("Current is limited by MaxSumMains: MaxSumMains=%uA, Isum=%d.%dA, Temp_Phases=%u.\n", MaxSumMains, Isum/10, Isum%10, Temp_Phases);
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
        _LOG_V("Checkpoint 2 Isetbalanced=%d.%d A, Idifference=%d.%d, mod=%u.\n", IsetBalanced/10, IsetBalanced%10, Idifference/10, Idifference%10, mod);

        if (Mode == MODE_SOLAR)                                                 // Solar version
        {
            IsumImport = Isum - (10 * ImportCurrent);                           // Allow Import of power from the grid when solar charging
            if (Idifference > 0) {                                              // so we had some room for power as far as MaxCircuit and MaxMains are concerned
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
            _LOG_V("Checkpoint 3 Isetbalanced=%d.%d A, IsumImport=%d.%d, Isum=%d.%d, ImportCurrent=%u.\n", IsetBalanced/10, IsetBalanced%10, IsumImport/10, IsumImport%10, Isum/10, Isum%10, ImportCurrent);
        } //end MODE_SOLAR
        else { // MODE_SMART
        // New EVSE charging, and only if we have active EVSE's
            if (mod && ActiveEVSE) {                                            // if we have an ActiveEVSE and mod=1, we must be Master, so MaxCircuit has to be
                                                                                // taken into account

                IsetBalanced = min((MaxMains * 10) - Baseload, (MaxCircuit * 10 ) - Baseload_EV ); //assume the current should be available on all 3 phases
                if (MaxSumMains)
                    IsetBalanced = min((int) IsetBalanced, ((MaxSumMains * 10) - Isum)/3); //assume the current should be available on all 3 phases
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
        IsetBalanced = min((int) IsetBalanced, (GridRelayMaxSumMains * 10)/Set_Nr_of_Phases_Charging()); //assume the current should be available on all 3 phases
    }
    _LOG_V("Checkpoint 4 Isetbalanced=%d.%d A.\n", IsetBalanced/10, IsetBalanced%10);

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
                if (ActiveEVSE && StopTime && IsumImport > 0 &&
                        // Would a stop free so much current that StartCurrent would immediately restart charging?
                        Isum > (ActiveEVSE * MinCurrent * Set_Nr_of_Phases_Charging() - StartCurrent) * 10) {
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
                    _LOG_D("Checkpoint a: Resetting SolarStopTimer, IsetBalanced=%d.%dA, ActiveEVSE=%u.\n", IsetBalanced/10, IsetBalanced%10, ActiveEVSE);
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
            if (hardShortage && Switching_To_Single_Phase != GOING_TO_SWITCH) { // because switching to single phase might solve the shortage
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

            _LOG_D("Checkpoint b: Resetting SolarStopTimer, MaxSumMainsTimer, IsetBalanced=%d.%dA, ActiveEVSE=%u.\n", IsetBalanced/10, IsetBalanced%10,  ActiveEVSE);
            setSolarStopTimer(0);
            MaxSumMainsTimer = 0;
            NoCurrent = 0;
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
                    _LOG_V("[S]Node %u = %u.%u A", n, Balanced[n]/10, Balanced[n]%10);
                    CurrentSet[n] = 1;                                          // mark this EVSE as set.
                    ActiveEVSE--;                                               // decrease counter of active EVSE's
                    MaxBalanced -= Balanced[n];                                 // Update total current to new (lower) value
                    IsetBalanced = TotalCurrent;
                    n = 0;                                                      // reset to recheck all EVSE's
                    continue;                                                   // ensure the loop restarts from the beginning
                
                // Check for EVSE's that have a Max Current that is lower then the average
                } else if (Average >= BalancedMax[n]) {
                    Balanced[n] = BalancedMax[n];                               // Set current to Maximum allowed for this EVSE
                    _LOG_V("[L]Node %u = %u.%u A", n, Balanced[n]/10, Balanced[n]%10);
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
        _LOG_D("Checkpoint c: Resetting SolarStopTimer, MaxSumMainsTimer, IsetBalanced=%d.%dA, saveActiveEVSE=%u.\n", IsetBalanced/10, IsetBalanced%10, saveActiveEVSE);
        setSolarStopTimer(0);
        MaxSumMainsTimer = 0;
        NoCurrent = 0;
    }

    // Reset flag that keeps track of new MainsMeter measurements
    phasesLastUpdateFlag = false;

    // ############### print all the distributed currents #################

    _LOG_V("Checkpoint 5 Isetbalanced=%d.%d A.\n", IsetBalanced/10, IsetBalanced%10);
    if (LoadBl == 1) {
        _LOG_D("Balance: ");
        for (n = 0; n < NR_EVSES; n++) {
            _LOG_D_NO_FUNC("EVSE%u:%s(%u.%uA) ", n, StrStateName[BalancedState[n]], Balanced[n]/10, Balanced[n]%10);
        }
        _LOG_D_NO_FUNC("\n");
    }
#ifndef SMARTEVSE_VERSION //CH32 only
    printf("Balanced0:%u\n", Balanced[0]);
#endif
#else
    printf("CalcBalancedCurrent:%i\n", mod);
#endif
} //CalcBalancedCurrent


void Timer1S_singlerun(void) {
static uint8_t Broadcast = 1;
#ifdef SMARTEVSE_VERSION //ESP32

static uint8_t x;

    if (BacklightTimer) BacklightTimer--;                               // Decrease backlight counter every second.
#else //CH32
uint8_t ow = 0, x;
#endif
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
            DisconnectEvent();
        } else{ // Run again
            DisconnectTimeCounter = 0; 
        }
    }
#endif

#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40   //CH32 and v3 ESP32
    // once a second, measure temperature
    // range -40 .. +125C
    TempEVSE = TemperatureSensor();                                                             
#endif

#ifdef SMARTEVSE_VERSION //ESP32
    // Check if there is a RFID card in front of the reader
    CheckRFID();
#else //CH32
    if (RFIDReader) ow = OneWireReadCardId();
#endif
             
    // When Solar Charging, once the current drops to MINcurrent a timer is started.
    // Charging is stopped when the timer reaches the time set in 'StopTime' (in minutes)
    // Except when Stoptime =0, then charging will continue.

    if (SolarStopTimer) {
        SolarStopTimer--;
#if MQTT
        MQTTclient.publish(MQTTprefix + "/SolarStopTimer", SolarStopTimer, false, 0);
#endif
        if (SolarStopTimer == 0) {
            if (State == STATE_C) setState(STATE_C1);                   // tell EV to stop charging
            ErrorFlags |= NO_SUN;                                       // Set error: NO_SUN //FIXME v4
        }
    }

    // When Smart or Solar Charging, once MaxSumMains is exceeded, a timer is started
    // Charging is stopped when the timer reaches the time set in 'MaxSumMainsTime' (in minutes)
    // Except when MaxSumMainsTime =0, then charging will continue.
    if (MaxSumMainsTimer) {
        MaxSumMainsTimer--;                                             // Decrease MaxSumMains counter every second.
        if (MaxSumMainsTimer == 0) {
            if (State == STATE_C) setState(STATE_C1);                   // tell EV to stop charging
            ErrorFlags |= LESS_6A;                                      // Set error: LESS_6A //FIXME v4
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
    }


    // Charge timer
    for (x = 0; x < NR_EVSES; x++) {
        if (BalancedState[x] == STATE_C) {
            Node[x].IntTimer++;
            Node[x].Timer++;
         } else Node[x].IntTimer = 0;                                    // Reset IntervalTime when not charging
    }

    if (MainsMeter.Type) {
        if ( MainsMeter.Timeout == 0 && !(ErrorFlags & CT_NOCOMM) && Mode != MODE_NORMAL) { // timeout if current measurement takes > 10 secs
            // In Normal mode do not timeout; there might be MainsMeter/EVMeter configured that can be retrieved through the API,
            // but in Normal mode we just want to charge ChargeCurrent, irrespective of communication problems.
            ErrorFlags |= CT_NOCOMM;
            setStatePowerUnavailable();
            SB2.SoftwareVer = 0;
            _LOG_W("Error, MainsMeter communication error!\n");
        } else {
            if (MainsMeter.Timeout) MainsMeter.Timeout--;
        }
    } else
        MainsMeter.Timeout = COMM_TIMEOUT;

    if (EVMeter.Type) {
        if ( EVMeter.Timeout == 0 && !(ErrorFlags & EV_NOCOMM) && Mode != MODE_NORMAL) {
            ErrorFlags |= EV_NOCOMM;
            setStatePowerUnavailable();
            _LOG_W("Error, EV Meter communication error!\n");
        } else {
            if (EVMeter.Timeout) EVMeter.Timeout--;
        }
    } else
        EVMeter.Timeout = COMM_EVTIMEOUT;
    
    // Clear communication error, if present
    if ((ErrorFlags & CT_NOCOMM) && MainsMeter.Timeout) ErrorFlags &= ~CT_NOCOMM;

    if ((ErrorFlags & EV_NOCOMM) && EVMeter.Timeout) ErrorFlags &= ~EV_NOCOMM;


    if (TempEVSE > maxTemp && !(ErrorFlags & TEMP_HIGH))                // Temperature too High?
    {
        ErrorFlags |= TEMP_HIGH;
        setStatePowerUnavailable();
        _LOG_W("Error, temperature %u C !\n", TempEVSE);
    }

    if (ErrorFlags & (NO_SUN | LESS_6A)) {
        if (ChargeDelay == 0) {
            if (Mode == MODE_SOLAR) { _LOG_I("Waiting for Solar power...\n"); }
            else { _LOG_I("Not enough current available!\n"); }
        }
        setStatePowerUnavailable();
        ChargeDelay = CHARGEDELAY;                                      // Set Chargedelay
    }

    // Every two seconds request measurement data from sensorbox/kwh meters.
    // and send broadcast to Node controllers.
    if (LoadBl < 2 && !Broadcast--) {                                   // Load Balancing mode: Master or Disabled
        if (!ModbusRequest) ModbusRequest = 1;                          // Start with state 1, also in Normal mode we want MainsMeter and EVmeter updated 
        //timeout = COMM_TIMEOUT; not sure if necessary, statement was missing in original code    // reset timeout counter (not checked for Master)
        Broadcast = 1;                                                  // repeat every two seconds
    }

#ifdef SMARTEVSE_VERSION //ESP32
    // for Slave modbusrequest loop is never called, so we have to show debug info here...
    if (LoadBl > 1)
        printStatus();  //for debug purposes
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
/*
    // Send data to ESP
    // Wait till configuration is received 
    // change LoadBl to something else
    if (LoadBl) {
        printf("Pilot:%u,State:%u,ChargeDelay:%u,Error:%u,Temp:%d,Lock:%u,Mode:%u,Access:%u ", Pilot(), State, ChargeDelay, ErrorFlags, TempEVSE, Lock, Mode, Access_bit);
        if (ow == 1) {
            printf(",RFID:");
            for (x=1 ; x<7 ; x++) printf("%02x",RFID[x]);
            ow = 0;
        }
        printf("\n");
    }
*/
 //   printf("10ms loop:%lu uS systick:%lu millis:%lu\n", elapsedmax/12, (uint32_t)SysTick->CNT, millis());
    // this section sends outcomes of functions and variables to ESP32 to fill Shadow variables
    // FIXME this section preferably should be empty
    printf("IsCurrentAvalable:%u", IsCurrentAvailable());
    printf("ErrorFlags:%u", ErrorFlags);
    elapsedmax = 0;
#endif
}




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
0x0001 	R/W 	Error 	                Bit 	1:LESS_6A / 2:NO_COMM / 4:TEMP_HIGH / 8:EV_NOCOMM / 16:RCD / 32:NO_SUN
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
        if (BalancedError[NodeNr] & (LESS_6A|NO_SUN)) {
            BalancedError[NodeNr] &= ~(LESS_6A | NO_SUN);                       // Clear Error flags
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

    // Here we set the Masters Mode to the one we received from a Slave/Node
    if (NodeNewMode) {
        setMode(NodeNewMode -1);
        NodeNewMode = 0;
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


#ifndef SMARTEVSE_VERSION //CH32 version
// Used for reading configuration data from ESP
typedef enum {
    UINT8,
    INT8,
    UINT16
} ValueType;

// Used for reading configuration data from ESP
typedef union {
    uint8_t* u8;
    int8_t* i8;
    uint16_t* u16;
} ValuePtr;

// Used for reading configuration data from ESP
typedef struct {
    const char* keyword;
    ValuePtr valuePtr;
    ValueType type;
} ConfigItem;


// Configuration items array
ConfigItem configItems[] = {
    {"Config:",{.u8 = &Config}, UINT8},
    {"Lock:", {.u8 = &Lock}, UINT8},
    {"Mode:", {.u8 = &Mode}, UINT8}, //e
    {"Access:", {.u8 = &Access_bit}, UINT8}, //e
    {"CardOffset:", {.u16 = &CardOffset}, UINT16},
    {"LoadBl:", {.u8 = &LoadBl}, UINT8},
    {"MaxMains:", {.u16 = &MaxMains}, UINT16},
    {"MaxSumMains:", {.u16 = &MaxSumMains},  UINT16},
    {"MaxCurrent:", {.u16 = &MaxCurrent}, UINT16},
    {"MinCurrent:", {.u16 = &MinCurrent}, UINT16},
    {"MaxCircuit:", {.u16 = &MaxCircuit}, UINT16},
    {"Switch:", {.u8 = &Switch}, UINT8},
    {"RCmon:", {.u8 = &RCmon}, UINT8},
    {"StartCurrent:", {.u16 = &StartCurrent}, UINT16},
    {"StopTime:", {.u16 = &StopTime}, UINT16},
    {"ImportCurrent:", {.u16 = &ImportCurrent}, UINT16},
    {"Grid:", {.u8 = &Grid}, UINT8},
    {"RFIDReader:", {.u8 = &RFIDReader}, UINT8},
    {"MainsMeterType:", {.u8 = &MainsMeter.Type}, UINT8},
    {"MainsMAddress:", {.u8 = &MainsMeter.Address}, UINT8},
    {"EVMeterType:", {.u8 = &EVMeter.Type}, UINT8},
    {"EVMeterAddress:", {.u8 = &EVMeter.Address}, UINT8},
    {"EMEndianness:", {.u8 = &EMConfig[EM_CUSTOM].Endianness}, UINT8},
    {"EMIRegister:", {.u16 = &EMConfig[EM_CUSTOM].IRegister}, UINT16},
    {"EMIDivisor:", {.i8 = &EMConfig[EM_CUSTOM].IDivisor}, INT8},
    {"EMURegister:", {.u16 = &EMConfig[EM_CUSTOM].URegister}, UINT16},
    {"EMUDivisor:", {.i8 = &EMConfig[EM_CUSTOM].UDivisor}, INT8},
    {"EMPRegister:", {.u16 = &EMConfig[EM_CUSTOM].PRegister}, UINT16},
    {"EMPDivisor:", {.i8 = &EMConfig[EM_CUSTOM].PDivisor}, INT8},
    {"EMERegister:", {.u16 = &EMConfig[EM_CUSTOM].ERegister}, UINT16},
    {"EMEDivisor:", {.i8 = &EMConfig[EM_CUSTOM].EDivisor}, INT8},
    {"EMDataType:", {.u8 = (uint8_t*)&EMConfig[EM_CUSTOM].DataType}, UINT8},
    {"EMFunction:", {.u8 = &EMConfig[EM_CUSTOM].Function}, UINT8},
    {"Initialized:", {.u8 = &Initialized}, UINT8},
    {"EnableC2:", {.u8 = (uint8_t*)&EnableC2}, UINT8},
    {"maxTemp:", {.u16 = &maxTemp}, UINT16},

    // the following variables are not configuration registers

//    {"State:", {.u8 = &State}, UINT8},
    {"ChargeCurrent:", {.u16 = &ChargeCurrent}, UINT16},
    {"PwrPanic:", {.u8 = &PwrPanic}, UINT8},
    {"ModemPwr:", {.u8 = &ModemPwr}, UINT8},

//    {NULL, {NULL}, UINT8} // End of array
};


// CH32 receives info from ESP32
void CheckSerialComm(void) {
    char SerialBuf[256];
    uint8_t len;
    char *ret;

    len = ReadESPdata(SerialBuf);
    RxRdy1 = 0;

    // Is it a request?
    char token[64];
    strncpy(token, "version?", sizeof(token));
    ret = strstr(SerialBuf, token);
    if (ret != NULL) printf("version:%04u\n", WchVersion);          // Send WCH software version

    CALL_ON_RECEIVE_PARAM(State:, setState)
    CALL_ON_RECEIVE_PARAM(SetCPDuty:, SetCPDuty)
    CALL_ON_RECEIVE_PARAM(SetCurrent:, SetCurrent)
    CALL_ON_RECEIVE_PARAM(CalcBalancedCurrent:, CalcBalancedCurrent)
    CALL_ON_RECEIVE(setStatePowerUnavailable)
    SET_ON_RECEIVE(ErrorFlags:, ErrorFlags)                                     //used to clear the ErrorFlags
    // We received configuration settings from the ESP.
    // Scan for all variables, and update values
    for (ConfigItem* item = configItems; item->keyword != NULL; item++) { //e
        const char* found = strstr(SerialBuf, item->keyword);
        if (found) {
            const char* valueStr = found + strlen(item->keyword);
            switch (item->type) {
                case UINT8:
                    *(item->valuePtr.u8) = (uint8_t)atoi(valueStr);
                    break;
                case INT8:
                    *(item->valuePtr.i8) = (int8_t)atoi(valueStr);
                    break;
                case UINT16:
                    *(item->valuePtr.u16) = (uint16_t)atoi(valueStr);
                    break;
            }
        }
    }

    // Wait till initialized is set by ESP
    if (Initialized) printf("Config:OK\n");

    // Enable/disable modem power
    ModemPower(ModemPwr);

    // Enable/disable Power Panic function
    PowerPanicCtrl(PwrPanic);

    //if (LoadBl) {
    //    printf("Config:OK %u,Lock:%u,Mode:%u,Current:%u,Switch:%u,RCmon:%u,PwrPanic:%u,RFID:%u\n", Config, Lock, Mode, ChargeCurrent, Switch, RCmon, PwrPanic, RFIDReader);
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
static unsigned int locktimer = 0, unlocktimer = 0;
#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40   //CH32 and v3 ESP32
static unsigned int energytimer = 0;
static uint8_t PollEVNode = NR_EVSES, updated = 0;
#endif

#ifndef SMARTEVSE_VERSION //CH32
    //Check Serial communication with ESP32
    if (RxRdy1) CheckSerialComm();
//make stuff compatible with CH32 terminology
#define digitalRead funDigitalRead
#define PIN_LOCK_IN LOCK_IN
#endif

    // Check if the cable lock is used
    if (Lock) {                                                 // Cable lock enabled?
        // UnlockCable takes precedence over LockCable
        if ((RFIDReader == 2 && Access_bit == 0) ||             // One RFID card can Lock/Unlock the charging socket (like a public charging station)
#if ENABLE_OCPP
        (OcppMode &&!OcppForcesLock) ||
#endif
            State == STATE_A) {                                 // The charging socket is unlocked when unplugged from the EV
            if (unlocktimer == 0) {                             // 600ms pulse
                ACTUATOR_UNLOCK;
            } else if (unlocktimer == 6) {
                ACTUATOR_OFF;
            }
            if (unlocktimer++ > 7) {
                if (digitalRead(PIN_LOCK_IN) == lock1 )         // still locked...
                {
                    if (unlocktimer > 50) unlocktimer = 0;      // try to unlock again in 5 seconds
                } else unlocktimer = 7;
            }
            locktimer = 0;
        // Lock Cable    
        } else if (State != STATE_A                            // Lock cable when connected to the EV
#if ENABLE_OCPP
        || (OcppMode && OcppForcesLock)
#endif
        ) {
            if (locktimer == 0) {                               // 600ms pulse
                ACTUATOR_LOCK;
            } else if (locktimer == 6) {
                ACTUATOR_OFF;
            }
            if (locktimer++ > 7) {
                if (digitalRead(PIN_LOCK_IN) == lock2 )         // still unlocked...
                {
                    if (locktimer > 50) locktimer = 0;          // try to lock again in 5 seconds
                } else locktimer = 7;
            }
            unlocktimer = 0;
        }
    }

#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40   //CH32 and v3 ESP32
    // Every 2 seconds, request measurements from modbus meters
    if (ModbusRequest) {                                                    // Slaves all have ModbusRequest at 0 so they never enter here
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
                    switch(EVMeter.Type) {
                        //these meters all have their power measured via receiveCurrentMeasurement already
                        case EM_EASTRON1P:
                        case EM_EASTRON3P:
                        case EM_EASTRON3P_INV:
                        case EM_ABB:
                        case EM_FINDER_7M:
                            break;
                        default:
                            requestPowerMeasurement(Node[PollEVNode].EVMeter, Node[PollEVNode].EVAddress,EMConfig[Node[PollEVNode].EVMeter].PRegister);
                            break;
                    }
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
                break;  // TODO: remove break; read modbus registers more evenly. 
                //  For now this gives the next modbus broadcast some room.
            default:
                // slave never gets here
                // what about normal mode with no meters attached?
                CalcBalancedCurrent(0);
                // No current left, or Overload (2x Maxmains)?
                if (Mode && (NoCurrent > 2 || MainsMeter.Imeasured > (MaxMains * 20))) { // I guess we don't want to set this flag in Normal mode, we just want to charge ChargeCurrent
                    // STOP charging for all EVSE's
                    // Display error message
                    ErrorFlags |= LESS_6A; //NOCURRENT;
                    // Broadcast Error code over RS485
                    ModbusWriteSingleRequest(BROADCAST_ADR, 0x0001, ErrorFlags);
                    NoCurrent = 0;
                }
                if (LoadBl == 1 && !(ErrorFlags & CT_NOCOMM) ) BroadcastCurrent();               // When there is no Comm Error, Master sends current to all connected EVSE's

                if ((State == STATE_B || State == STATE_C) && !CPDutyOverride) SetCurrent(Balanced[0]); // set PWM output for Master //mind you, the !CPDutyOverride was not checked in Smart/Solar mode, but I think this was a bug!
#ifdef SMARTEVSE_VERSION //ESP32
                printStatus();  //for debug purposes
#endif
                ModbusRequest = 0;
                //_LOG_A("Timer100ms task free ram: %u\n", uxTaskGetStackHighWaterMark( NULL ));
                break;
        } //switch
        if (ModbusRequest) ModbusRequest++;
    }
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
#if SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40
static uint8_t LcdPwm = 0;
static uint8_t RedPwm = 0, GreenPwm = 0, BluePwm = 0;
static uint8_t LedCount = 0;                                                   // Raw Counter before being converted to PWM value
static unsigned int LedPwm = 0;                                                // PWM value 0-255

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

        if (ErrorFlags & (RCM_TRIPPED | CT_NOCOMM | EV_NOCOMM) ) {
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

#if ENABLE_OCPP
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
    } else if (Access_bit == 0 && CustomButton) {
        RedPwm = ColorCustom[0];
        GreenPwm = ColorCustom[1];
        BluePwm = ColorCustom[2];
    } else if (Access_bit == 0 || State == STATE_MODEM_DENIED) {
        RedPwm = ColorOff[0];
        GreenPwm = ColorOff[1];
        BluePwm = ColorOff[2];
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
    ledcWrite(RED_CHANNEL, RedPwm);
    ledcWrite(GREEN_CHANNEL, GreenPwm);
    ledcWrite(BLUE_CHANNEL, BluePwm);

#else // CH32
    static uint8_t RedPwm = 0, GreenPwm = 0, BluePwm = 0;
    static uint8_t LedCount = 0;                                            // Raw Counter before being converted to PWM value
    static uint16_t LedPwm = 0;                                             // PWM value 0-255


    // RGB LED
    if (ErrorFlags || ChargeDelay) {

        if (ErrorFlags & (RCM_TRIPPED | CT_NOCOMM | EV_NOCOMM) ) {
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

    } else if (Access_bit == 0 || State == STATE_MODEM_DENIED) {            // No Access, LEDs off
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

    TIM3->CH1CVR = RedPwm;
    TIM3->CH2CVR = GreenPwm;
    TIM3->CH3CVR = BluePwm;
#endif
}
#endif


void Timer10ms_singlerun(void) {
#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40   //CH32 and v3 ESP32
    static uint8_t DiodeCheck = 0;
    static uint16_t StateTimer = 0;                                                 // When switching from State B to C, make sure pilot is at 6v for 100ms
    BlinkLed_singlerun();
#else //v4
    static uint8_t RXbyte, idx = 0;
    static char SerialBuf[256];
    static uint8_t CommState = COMM_VER_REQ;
    static uint8_t CommTimeout = 0;
    static char *ret;
#endif

#ifndef SMARTEVSE_VERSION //CH32
    //Check RS485 communication
    if (ModbusRxLen) CheckRS485Comm();
#else //v3 and v4
// Task that handles EVSE State Changes
// Reads buttons, and updates the LCD.
    static uint16_t old_sec = 0;
    getButtonState();

    // When one or more button(s) are pressed, we call GLCDMenu
    if (((ButtonState != 0x07) || (ButtonState != OldButtonState)) && !LCDlock) GLCDMenu(ButtonState);

    // Update/Show Helpmenu
    if (LCDNav > MENU_ENTER && LCDNav < MENU_EXIT && (ScrollTimer + 5000 < millis() ) && (!SubMenu)) GLCDHelp();

    if (timeinfo.tm_sec != old_sec) {
        old_sec = timeinfo.tm_sec;
        GLCD();
    }
#endif

#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40 //CH32 and v3
    // Check the external switch and RCM sensor
    ExtSwitch.CheckSwitch();
    // sample the Pilot line
    pilot = Pilot();

    // ############### EVSE State A #################

    if (State == STATE_A || State == STATE_COMM_B || State == STATE_B1) {
        // When the pilot line is disconnected, wait for PilotDisconnectTime, then reconnect
        if (PilotDisconnected) {
            if (PilotDisconnectTime == 0 && pilot == PILOT_NOK ) {          // Pilot should be ~ 0V when disconnected
                PILOT_CONNECTED;
                PilotDisconnected = false;
                _LOG_A("Pilot Connected\n");
            }
        } else if (pilot == PILOT_12V) {                                    // Check if we are disconnected, or forced to State A, but still connected to the EV
            // If the RFID reader is set to EnableOne or EnableAll mode, and the Charging cable is disconnected
            // We start a timer to re-lock the EVSE (and unlock the cable) after 60 seconds.
            if ((RFIDReader == 2 || RFIDReader == 1) && AccessTimer == 0 && Access_bit == 1) AccessTimer = RFIDLOCKTIME;

            if (State != STATE_A) setState(STATE_A);                        // reset state, incase we were stuck in STATE_COMM_B
            ChargeDelay = 0;                                                // Clear ChargeDelay when disconnected.
            if (!EVMeter.ResetKwh) EVMeter.ResetKwh = 1;                    // when set, reset EV kWh meter on state B->C change.
        } else if ( pilot == PILOT_9V && ErrorFlags == NO_ERROR
            && ChargeDelay == 0 && Access_bit && State != STATE_COMM_B
#if MODEM
            && State != STATE_MODEM_REQUEST && State != STATE_MODEM_WAIT && State != STATE_MODEM_DONE   // switch to State B ?
#endif
                )
        {                                                                    // Allow to switch to state C directly if STATE_A_TO_C is set to PILOT_6V (see EVSE.h)
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
#if MODEM
                if (ModemStage == 0)
                    setState(STATE_MODEM_REQUEST);
                else
#endif
                    setState(STATE_B);                                          // switch to State B
                ActivationMode = 30;                                        // Activation mode is triggered if state C is not entered in 30 seconds.
                AccessTimer = 0;
            } else if (Mode == MODE_SOLAR) {                                // Not enough power:
                ErrorFlags |= NO_SUN;                                       // Not enough solar power
            } else ErrorFlags |= LESS_6A;                                   // Not enough power available
        } else if (pilot == PILOT_9V && State != STATE_B1 && State != STATE_COMM_B && Access_bit) {
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
            if ((DiodeCheck == 1) && (ErrorFlags == NO_ERROR) && (ChargeDelay == 0)) {
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
                        if (!LCDNav) GLCD();                                // Don't update the LCD if we are navigating the menu
#endif                                                                      // immediately update LCD (20ms)
                    }
                    else if (Mode == MODE_SOLAR) {                          // Not enough power:
                        ErrorFlags |= NO_SUN;                               // Not enough solar power
                    } else ErrorFlags |= LESS_6A;                           // Not enough power available
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
        if (!LCDNav) GLCD();                                                // immediately update LCD
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
            ErrorFlags = RCM_TRIPPED;
            LCDTimer = 0;                                                   // display the correct error message on the LCD
        }
    }
#endif //v3
#endif //v3 and CH32
#if SMARTEVSE_VERSION >= 40 //v4
    //ESP32 receives info from CH32
    if (Serial1.available()) {
        //Serial.printf("[<-] ");        // Data available from mainboard?
        while (Serial1.available()) {
            RXbyte = Serial1.read();
            //Serial.printf("%c",RXbyte);
            SerialBuf[idx] = RXbyte;
            idx++;
        }
        SerialBuf[idx] = '\0'; //null terminate
        _LOG_D("[<-] %s.\n", SerialBuf);
    }
    // process data from mainboard
    if (idx > 5) {
        if (memcmp(SerialBuf, "!Panic", 6) == 0) PowerPanicESP();

        char token[64];

        strncpy(token, "MSG:", sizeof(token));                              // if a command starts with MSG: the rest of the line is no longer parsed
        ret = strstr(SerialBuf, token);
        if (ret == NULL) {
/*                ret = strstr(SerialBuf, "Pilot:");
            //  [<-] Pilot:6,State:0,ChargeDelay:0,Error:0,Temp:34,Lock:0,Mode:0,Access:1
            if (ret != NULL) {
                int hit = sscanf(SerialBuf, "Pilot:%u,State:%u,ChargeDelay:%u,Error:%u,Temp:%u,Lock:%u,Mode:%u, Access:%u", &pilot, &State, &ChargeDelay, &ErrorFlags, &TempEVSE, &Lock, &Mode, &Access_bit);
                if (hit != 8) {
                    _LOG_A("ERROR parsing line from WCH, hit=%u, line=%s.\n", hit, SerialBuf);
                } else {
                    _LOG_A("DINGO: pilot=%u, State=%u, ChargeDelay=%u, ErrorFlags = %u, TempEVSE=%u, Lock=%u, Mode=%u, Access_bit=%u.\n", pilot, State,ChargeDelay, ErrorFlags, TempEVSE, Lock, Mode, Access_bit);
                }
            }*/

            strncpy(token, "ExtSwitch:", sizeof(token));
            ret = strstr(SerialBuf, token);
            if (ret != NULL) {
                ExtSwitch.Pressed = atoi(ret+strlen(token));
                ExtSwitch.TimeOfPress = millis();
                ExtSwitch.HandleSwitch();
            }
            CALL_ON_RECEIVE_PARAM(Access:, setAccess)
            CALL_ON_RECEIVE_PARAM(Mode:, setMode)
            CALL_ON_RECEIVE(write_settings)
            //these variables are owned by CH32 and copies are sent to ESP32:
            SET_ON_RECEIVE(Pilot:, pilot)
            SET_ON_RECEIVE(Temp:, TempEVSE)
            SET_ON_RECEIVE(State:, State)
            SET_ON_RECEIVE(Balanced0:, Balanced[0])
            SET_ON_RECEIVE(IsCurrentAvailable:, Shadow_IsCurrentAvailable)
            SET_ON_RECEIVE(ErrorFlags:, ErrorFlags)

            strncpy(token, "version:", sizeof(token));
            ret = strstr(SerialBuf, token);
            if (ret != NULL) {
                MainVersion = atoi(ret+strlen(token));
                Serial.printf("version %u received\n", MainVersion);
                CommState = COMM_CONFIG_SET;
            }

            ret = strstr(SerialBuf, "Config:OK");
            if (ret != NULL) {
                Serial.printf("Config set\n");
                CommState = COMM_STATUS_REQ;
            }

            strncpy(token, "EnableC2:", sizeof(token));
            ret = strstr(SerialBuf, token);
            if (ret != NULL) {
                EnableC2 = (EnableC2_t) atoi(ret+strlen(token)); //e
            }

            strncpy(token, "Irms:", sizeof(token));
            //Irms:011,312,123,124 means: the meter on address 11(dec) has Irms[0] 312 dA, Irms[1] of 123 dA, Irms[2] of 124 dA.
            ret = strstr(SerialBuf, token);
            if (ret != NULL) {
                short unsigned int Address;
                int16_t Irms[3];
                int n = sscanf(SerialBuf,"Irms:%03hu,%hi,%hi,%hi", &Address, &Irms[0], &Irms[1], &Irms[2]);
                if (n == 4) {   //success
                    if (Address == MainsMeter.Address) {
                        for (int x = 0; x < 3; x++)
                            MainsMeter.Irms[x] = Irms[x];
                        MainsMeter.Timeout = COMM_TIMEOUT;
                        CalcIsum();
                    } else if (Address == EVMeter.Address) {
                        for (int x = 0; x < 3; x++)
                            EVMeter.Irms[x] = Irms[x];
                        EVMeter.Timeout = COMM_EVTIMEOUT;
                        EVMeter.CalcImeasured();
                    }
                } else
                    _LOG_A("Received corrupt %s, n=%d, message from WCH:%s.\n", token, n, SerialBuf);
            }

            strncpy(token, "PowerMeasured:", sizeof(token));
            //printf("PowerMeasured:%03u,%d\n", Address, PowerMeasured);
            ret = strstr(SerialBuf, token);
            if (ret != NULL) {
                short unsigned int Address;
                int16_t PowerMeasured;
                int n = sscanf(SerialBuf,"PowerMeasured:%03hu,%hi", &Address, &PowerMeasured);
                if (n == 2) {   //success
                    if (Address == MainsMeter.Address) {
                        MainsMeter.PowerMeasured = PowerMeasured;
                    } else if (Address == EVMeter.Address) {
                        EVMeter.PowerMeasured = PowerMeasured;
                    }
                } else
                    _LOG_A("Received corrupt %s, n=%d, message from WCH:%s.\n", token, n, SerialBuf);
            }

/*
            strncpy(token, "State:", sizeof(token));
            ret = strstr(SerialBuf, token);
            if (ret != NULL ) {
                State = atoi(ret+strlen(token)); //e
                if (State == STATE_COMM_B) NewState = STATE_COMM_B_OK;
                else if (State == STATE_COMM_C) NewState = STATE_COMM_C_OK;

                if (NewState) {    // only send confirmation when state needs to change.
                    Serial1.printf("WchState:%u\n",NewState );        // send confirmation back to WCH
                    Serial.printf("[->] WchState:%u\n",NewState );    // send confirmation back to WCH
                    NewState = 0;
                }
            }*/
        }
        memset(SerialBuf,0,idx);        // Clear buffer
        idx = 0;
    }

    if (CommTimeout == 0) {
        switch (CommState) {

            case COMM_VER_REQ:
                CommTimeout = 10;
                Serial1.print("version?\n");            // send command to WCH ic
                Serial.print("[->] version?\n");        // send command to WCH ic
                break;

            case COMM_CONFIG_SET:                       // Set mainboard configuration
                CommTimeout = 10;
                // send configuration to WCH IC
                Serial1.printf("Config:%u,Lock:%u,Mode:%u,CardOffset:%u,LoadBl:%u,MaxMains:%u,MaxSumMains:%u, MaxCurrent:%u, MinCurrent:%u, MaxCircuit:%u, Switch:%u, RCmon:%u, StartCurrent:%u\n", Config, Lock, Mode, CardOffset, LoadBl, MaxMains, MaxSumMains, MaxCurrent, MinCurrent, MaxCircuit, Switch, RCmon, StartCurrent);
                delay(1000);
                Serial1.printf("StopTime:%u, ImportCurrent:%u, Grid:%u, RFIDReader:%u, MainsMeterType:%u, MainsMAddress:%u, EVMeterType:%u, EVMeterAddress:%u, Initialized:%u, EnableC2:%u, maxTemp:%u, PwrPanic:%u, ModemPwr:%u\n", StopTime, ImportCurrent, Grid, RFIDReader, MainsMeter.Type, MainsMeter.Address, EVMeter.Type, EVMeter.Address, Initialized, EnableC2, maxTemp, PwrPanic, ModemPwr);
                //Serial1.printf("Config:%u,Lock:%u,Mode:%u,CardOffset:%u,LoadBl:%u,MaxMains:%u,MaxSumMains:%u, MaxCurrent:%u, MinCurrent:%u, MaxCircuit:%u, Switch:%u, RCmon:%u, StartCurrent:%u, StopTime:%u, ImportCurrent:%u, Grid:%u, RFIDReader:%u, MainsMeterType:%u, MainsMAddress:%u, EVMeterType:%u, EVMeterAddress:%u, Initialized:%u, EnableC2:%u, maxTemp:%u, PwrPanic:%u, ModemPwr:%u\n", Config, Lock, Mode, CardOffset, LoadBl, MaxMains, MaxSumMains, MaxCurrent, MinCurrent, MaxCircuit, Switch, RCmon, StartCurrent, StopTime, ImportCurrent, Grid, RFIDReader, MainsMeter.Type, MainsMeter.Address, EVMeter.Type, EVMeter.Address, Initialized, EnableC2, maxTemp, PwrPanic, ModemPwr);

//TODO ChargeCurrent?
//FIXME  EMEndianness:%u, EMIRegister:%u, EMIDivisor:%u, EMURegister:%u, EMUDivisor:%u, EMPRegister:%u, EMPDivisor:%u, EMERegister:%u, EMEDivisor:%u, EMDataType:%u, EMFunction:%u,
//  EMEndianness, EMIRegister, EMIDivisor, EMURegister, EMUDivisor, EMPRegister, EMPDivisor, EMERegister, EMEDivisor, EMDataType, EMFunction,
                break;

            case COMM_STATUS_REQ:                       // Ready to receive status from mainboard
                CommTimeout = 10;
                /*
                State: A
                Temp: 28
                Error: 0
                */
        }
    }


    if (CommTimeout) CommTimeout--;

#endif //SMARTEVSE_VERSION v4

#ifndef SMARTEVSE_VERSION //CH32
    // Clear communication error, if present
    if ((ErrorFlags & CT_NOCOMM) && MainsMeter.Timeout == 10) ErrorFlags &= ~CT_NOCOMM;
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

#if SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40 //does not run on v4
void BlinkLed(void * parameter) {
    // infinite loop
    while(1) {
        BlinkLed_singlerun();
        // Pause the task for 10ms
        vTaskDelay(10 / portTICK_PERIOD_MS);
    } // while(1) loop
}
#endif
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
        case MENU_MAX_TEMP:
            maxTemp = val;
            break;
        case MENU_C2:
            EnableC2 = (EnableC2_t) val;
#ifdef SMARTEVSE_VERSION
            Serial1.printf("EnableC2:%u\n", EnableC2);
#else
            printf("EnableC2:%u\n", EnableC2);
#endif
            break;
        case MENU_CONFIG:
            Config = val;
            break;
        case STATUS_MODE:
            if (Mode != val)                                                    // this prevents slave from waking up from OFF mode when Masters'
                                                                                // solarstoptimer starts to count
                setMode(val);
            break;
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
#if SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40
            ConfigureModbusMode(val);
#endif
            LoadBl = val;
            break;
        case MENU_MAINS:
            MaxMains = val;
            break;
        case MENU_SUMMAINS:
            MaxSumMains = val;
            break;
        case MENU_SUMMAINSTIME:
            MaxSumMainsTime = val;
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
#if !defined(SMARTEVSE_VERSION) //CH32
            RCmonCtrl(RCmon);
#endif
            break;
        case MENU_GRID:
            Grid = val;
            break;
        case MENU_SB2_WIFI:
            SB2_WIFImode = val;
            break;
        case MENU_MAINSMETER:
            MainsMeter.Type = val;
            break;
        case MENU_MAINSMETERADDRESS:
            MainsMeter.Address = val;
            break;
        case MENU_EVMETER:
            EVMeter.Type = val;
            break;
        case MENU_EVMETERADDRESS:
            EVMeter.Address = val;
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
#ifdef SMARTEVSE_VERSION //TODO THIS SHOULD BE FIXED
        case MENU_WIFI:
            WIFImode = val;
            break;
#endif
        case MENU_AUTOUPDATE:
            AutoUpdate = val;
            break;

        // Status writeable
        case STATUS_STATE:
            if (val != State) setState(val);
            break;
        case STATUS_ERROR:
            ErrorFlags = val; //FIXME v4
            if (ErrorFlags) {                                                   // Is there an actual Error? Maybe the error got cleared?
                if (ErrorFlags & CT_NOCOMM) MainsMeter.Timeout = 0;             // clear MainsMeter.Timeout on a CT_NOCOMM error, so the error will be immediate.
                setStatePowerUnavailable();
                ChargeDelay = CHARGEDELAY;
                _LOG_V("Error message received!\n");
            } else {
                _LOG_V("Errors Cleared received!\n");
            }
            break;
        case STATUS_CURRENT:
            OverrideCurrent = val;
            if (LoadBl < 2) MainsMeter.Timeout = COMM_TIMEOUT;                  // reset timeout when register is written
            break;
        case STATUS_SOLAR_TIMER:
            SolarStopTimer = val;
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
        case MENU_RCMON:
            return RCmon;
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
#endif
        case MENU_AUTOUPDATE:
            return AutoUpdate;

        // Status writeable
        case STATUS_STATE:
            return State;
        case STATUS_ERROR:
            return ErrorFlags; //FIXME v4
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
            return min(MaxCapacity,MaxCurrent);
        case STATUS_TEMP:
            return (signed int)TempEVSE;
#ifdef SMARTEVSE_VERSION //not on CH32
        case STATUS_SERIAL:
            return serialnr;
#endif
        default:
            return 0;
    }
}

//#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40 //not on ESP32 v4
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


void CalcIsum(void) {
    phasesLastUpdate = time(NULL);
    phasesLastUpdateFlag = true;                        // Set flag if a new Irms measurement is received.
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
        MainsMeter.Irms[x] = MainsMeter.Irms[x] - temp[x];
#endif
        IrmsOriginal[x] = MainsMeter.Irms[x];
        MainsMeter.Irms[x] -= batteryPerPhase;
        Isum = Isum + MainsMeter.Irms[x];
    }
    MainsMeter.CalcImeasured();
}
//#endif


/*
 * This file has shared code between SmartEVSE-3, SmartEVSE-4 and SmartEVSE-4_CH32
 * #if SMARTEVSE_VERSION == 3  //SmartEVSEv3 code
 * #if SMARTEVSE_VERSION == 4  //SmartEVSEv4 code
 * #ifndef SMARTEVSE_VERSION   //CH32 code
 */

#include "main.h"
#include "common.h"
#include "stdio.h"
#include "stdlib.h"

#ifdef SMARTEVSE_VERSION //ESP32
#define EXT extern
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
#include <time.h>

#include <soc/sens_reg.h>
#include <soc/sens_struct.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>

#include <soc/rtc_io_struct.h>
extern Preferences preferences;
#else //CH32
#define EXT extern "C"
extern "C" {
    #include "ch32v003fun.h"
    #include "utils.h"
}
#endif
EnableC2_t EnableC2 = NOT_PRESENT;
uint8_t pilot;
uint8_t LoadBl = LOADBL;

//#ifdef SMARTEVSE_VERSION //v3 or v4
#include "meter.h"
//#endif


// gateway to the outside world
// here declarations are placed for variables that are both used on CH32 as ESP32
// (either temporarily while developing or definite)
// and they are mainly used in the main.cpp/common.cpp code
EXT uint32_t elapsedmax, elapsedtime;
EXT int8_t TempEVSE;
EXT int homeBatteryCurrent, phasesLastUpdate;
EXT uint16_t SolarStopTimer, MaxCapacity, MainsCycleTime, ChargeCurrent, MinCurrent, MaxCurrent, BalancedMax[NR_EVSES], ADC_CP[NUM_ADC_SAMPLES], ADCsamples[25], Balanced[NR_EVSES], MaxMains, MaxCircuit, OverrideCurrent, StartCurrent, StopTime, MaxSumMains, ImportCurrent, GridRelayMaxSumMains;
EXT uint8_t RFID[8], Access_bit, Mode, Lock, ErrorFlags, ChargeDelay, State, LoadBl, PilotDisconnectTime, AccessTimer, ActivationMode, ActivationTimer, RFIDReader, C1Timer, UnlockCable, LockCable, RxRdy1, MainsMeterTimeout, PilotDisconnected, ModbusRxLen, PowerPanicFlag, Switch, RCmon, TestState, Config, PwrPanic, ModemPwr, Initialized, pilot, NoCurrent, MaxSumMainsTime;
EXT int16_t IsetBalanced;
EXT bool CustomButton, GridRelayOpen, phasesLastUpdateFlag;
#ifdef SMARTEVSE_VERSION //v3 and v4
EXT hw_timer_t * timerA;
esp_adc_cal_characteristics_t * adc_chars_CP;
#endif
EXT struct Node_t Node[NR_EVSES];
EXT uint8_t BalancedState[NR_EVSES];
#if ENABLE_OCPP
EXT float OcppCurrentLimit;
#endif

//functions
EXT void setup();
EXT void setState(uint8_t NewState);
EXT int8_t TemperatureSensor();
EXT void CheckSerialComm(void);
EXT uint8_t OneWireReadCardId();
EXT void CheckRS485Comm(void);
EXT void BlinkLed(void);
EXT uint8_t ProximityPin();
EXT void PowerPanic(void);
EXT const char * getStateName(uint8_t StateCode);
EXT void SetCurrent(uint16_t current);
EXT char IsCurrentAvailable(void);
EXT int Set_Nr_of_Phases_Charging(void);

Single_Phase_t Switching_To_Single_Phase = FALSE;
uint16_t MaxSumMainsTimer = 0;
uint8_t LCDTimer = 0;
int16_t Isum = 0;                                                           // Sum of all measured Phases (Amps *10) (can be negative)
uint8_t Nr_Of_Phases_Charging = 0;                                          // 0 = Undetected, 1,2,3 = nr of phases that was detected at the start of this charging session
Meter MainsMeter(MAINS_METER, MAINS_METER_ADDRESS, COMM_TIMEOUT);
Meter EVMeter(EV_METER, EV_METER_ADDRESS, COMM_EVTIMEOUT);
bool phasesLastUpdateFlag = false;
bool GridRelayOpen = false;                                                 // The read status of the relay
uint8_t MaxSumMainsTime = MAX_SUMMAINSTIME;                                 // Number of Minutes we wait when MaxSumMains is exceeded, before we stop charging
uint16_t maxTemp = MAX_TEMPERATURE;
uint8_t AutoUpdate = AUTOUPDATE;                                            // Automatic Firmware Update (0:Disable / 1:Enable)
uint8_t ConfigChanged = 0;

//constructor
Button::Button(void) {
    // in case of a press button, we do nothing
    // in case of a toggle switch, we have to check the switch position since it might have been changed
    // since last powerup
    //     0            1          2           3           4            5              6          7
    // "Disabled", "Access B", "Access S", "Sma-Sol B", "Sma-Sol S", "Grid Relay", "Custom B", "Custom S"
    CheckSwitch(true);
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
                    //TODO isnt all this stuff done in setMode?
                    ErrorFlags &= ~(NO_SUN | LESS_6A);                   // Clear All errors
                    ChargeDelay = 0;                                // Clear any Chargedelay
                    setSolarStopTimer(0);                           // Also make sure the SolarTimer is disabled.
                    MaxSumMainsTimer = 0;
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
#if SMARTEVSE_VERSION == 3
    uint8_t Read = digitalRead(PIN_SW_IN);
#endif
#ifndef SMARTEVSE_VERSION //CH32
    uint8_t Read = funDigitalRead(SW_IN) && funDigitalRead(BUT_SW_IN);          // BUT_SW_IN = LED pushbutton, SW_IN = 12pin plug at bottom
#endif

#if SMARTEVSE_VERSION != 4   //this code executed in CH32V, not in ESP32
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
#ifdef SMARTEVSE_VERSION //both v3 and v4
    // TODO This piece of code doesnt really belong in CheckSwitch but should be called every 10ms
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
#endif
}

Button ExtSwitch;

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

void setAccess(bool Access) { //c
#ifdef SMARTEVSE_VERSION //v3 and v4
    Access_bit = Access;
#if SMARTEVSE_VERSION == 4
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

//TODO #if SMARTEVSE_VERSION != 4
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
//#endif

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
        snprintf(Str, sizeof(Str), "%02d:%02d:%02d STATE %s -> %s\n",timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, getStateName(State), getStateName(NewState) );
        _LOG_A("%s",Str);
#if SMARTEVSE_VERSION == 4
        Serial1.printf("State:%u\n", NewState); //a
#endif
#else //CH32
        printf("State:%1u.\n", NewState); //d
#endif
    }

#if SMARTEVSE_VERSION != 4 //a
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


// Set global var Nr_Of_Phases_Charging
// 0 = undetected, 1 - 3 nr of phases we are charging
// returns nr of phases we are charging, and 3 if undetected
int Set_Nr_of_Phases_Charging(void) {
    uint32_t Max_Charging_Prob = 0;
    uint32_t Charging_Prob=0;                                        // Per phase, the probability that Charging is done at this phase
    Nr_Of_Phases_Charging = 0;
#define THRESHOLD 40
#define BOTTOM_THRESHOLD 25
    _LOG_D("Detected Charging Phases: ChargeCurrent=%u, Balanced[0]=%u, IsetBalanced=%u.\n", ChargeCurrent, Balanced[0],IsetBalanced);
    for (int i=0; i<3; i++) {
        if (EVMeter.Type) {
            Charging_Prob = 10 * (abs(EVMeter.Irms[i] - IsetBalanced)) / IsetBalanced;  //100% means this phase is charging, 0% mwans not charging
                                                                                        //TODO does this work for the slaves too?
            _LOG_D("Trying to detect Charging Phases END EVMeter.Irms[%i]=%.1f A.\n", i, (float)EVMeter.Irms[i]/10);
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
    if (Nr_Of_Phases_Charging == 0)
        return 3;
    return Nr_Of_Phases_Charging;
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

    //printf("MSG: min:%u max:%u\n",Min ,Max);

    // test Min/Max against fixed levels    (needs testing)
    ret = PILOT_NOK;                                                        // Pilot NOT ok
    if (Min >= 4000 ) ret = PILOT_12V;                                      // Pilot at 12V
    if ((Min >= 3300) && (Max < 4000)) ret = PILOT_9V;                      // Pilot at 9V
    if ((Min >= 2400) && (Max < 3300)) ret = PILOT_6V;                      // Pilot at 6V
    if ((Min >= 2000) && (Max < 2400)) ret = PILOT_3V;                      // Pilot at 3V
    if ((Min > 100) && (Max < 350)) ret = PILOT_DIODE;                      // Diode Check OK
    if (ret != old_pilot) {
        //printf("Pilot:%u\n", ret); //d
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


// Calculates Balanced PWM current for each EVSE
// mod =0 normal
// mod =1 we have a new EVSE requesting to start charging.
// only runs on the Master or when loadbalancing Disabled
void CalcBalancedCurrent(char mod) {
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

    _LOG_V("Checkpoint 1 Isetbalanced=%.1f A Imeasured=%.1f A MaxCircuit=%i Imeasured_EV=%.1f A, Battery Current = %.1f A, mode=%i.\n", (float)IsetBalanced/10, (float)MainsMeter.Imeasured/10, MaxCircuit, (float)EVMeter.Imeasured/10, (float)homeBatteryCurrent/10, Mode);

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
            _LOG_V("Current is limited by MaxSumMains: MaxSumMains=%iA, Isum=%.1fA, Temp_Phases=%i.\n", MaxSumMains, (float)Isum/10, Temp_Phases);
        }

        if (!mod) {                                                             // no new EVSE's charging
                                                                                // For Smart mode, no new EVSE asking for current
            if (phasesLastUpdateFlag) {                                         // only increase or decrease current if measurements are updated
                _LOG_V("phaseLastUpdate=%i.\n", phasesLastUpdate);
                if (Idifference > 0) {
                    if (Mode == MODE_SMART) IsetBalanced += (Idifference / 4);  // increase with 1/4th of difference (slowly increase current)
                }                                                               // in Solar mode we compute increase of current later on!
                else
                    IsetBalanced += Idifference;                                // last PWM setting + difference (immediately decrease current) (Smart and Solar mode)
            }

            if (IsetBalanced < 0) IsetBalanced = 0;
            if (IsetBalanced > 800) IsetBalanced = 800;                         // hard limit 80A (added 11-11-2017)
        }
        _LOG_V("Checkpoint 2 Isetbalanced=%.1f A, Idifference=%.1f, mod=%i.\n", (float)IsetBalanced/10, (float)Idifference/10, mod);

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
            _LOG_V("Checkpoint 3 Isetbalanced=%.1f A, IsumImport=%.1f, Isum=%.1f, ImportCurrent=%i.\n", (float)IsetBalanced/10, (float)IsumImport/10, (float)Isum/10, ImportCurrent);
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
    _LOG_V("Checkpoint 4 Isetbalanced=%.1f A.\n", (float)IsetBalanced/10);

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
                    _LOG_D("Checkpoint a: Resetting SolarStopTimer, IsetBalanced=%.1fA, ActiveEVSE=%i.\n", (float)IsetBalanced/10, ActiveEVSE);
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
                // the expiring of both SolarStopTimer and MaxSumMainsTimer is handled in the Timer1s loop
                if (LimitedByMaxSumMains && MaxSumMainsTime) {
                    if (MaxSumMainsTimer == 0)                                  // has expired, so set timer
                        MaxSumMainsTimer = MaxSumMainsTime * 60;
                }
            }
        } else {                                                                // we have enough current
            // ############### no shortage of power  #################

            _LOG_D("Checkpoint b: Resetting SolarStopTimer, MaxSumMainsTimer, IsetBalanced=%.1fA, ActiveEVSE=%i.\n", (float)IsetBalanced/10, ActiveEVSE);
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
        _LOG_D("Checkpoint c: Resetting SolarStopTimer, MaxSumMainsTimer, IsetBalanced=%.1fA, saveActiveEVSE=%i.\n", (float)IsetBalanced/10, saveActiveEVSE);
        setSolarStopTimer(0);
        MaxSumMainsTimer = 0;
        NoCurrent = 0;
    }

    // Reset flag that keeps track of new MainsMeter measurements
    phasesLastUpdateFlag = false;

    // ############### print all the distributed currents #################

    _LOG_V("Checkpoint 5 Isetbalanced=%.1f A.\n", (float)IsetBalanced/10);
    if (LoadBl == 1) {
        _LOG_D("Balance: ");
        for (n = 0; n < NR_EVSES; n++) {
            _LOG_D_NO_FUNC("EVSE%u:%s(%.1fA) ", n, getStateName(BalancedState[n]), (float)Balanced[n]/10);
        }
        _LOG_D_NO_FUNC("\n");
    }
} //CalcBalancedCurrent


void Timer10ms_singlerun(void) {
#ifndef SMARTEVSE_VERSION //CH32
//NOTE that CH32 has a 10ms routine that has to be called every 10ms
//and ESP32 has a 10ms routine that is called once and has a while loop with 10ms delay in it
    static uint8_t DiodeCheck = 0;
    static uint16_t StateTimer = 0;                                                 // When switching from State B to C, make sure pilot is at 6v for 100ms

    BlinkLed();

    //Check RS485 communication
    if (ModbusRxLen) CheckRS485Comm();

#else //v3 and v4
// Task that handles EVSE State Changes
// Reads buttons, and updates the LCD.
    static uint16_t old_sec = 0;
#if SMARTEVSE_VERSION == 3
    static uint8_t DiodeCheck = 0;
    static uint16_t StateTimer = 0;                                                 // When switching from State B to C, make sure pilot is at 6v for 100ms
#else
    static uint8_t RXbyte, idx = 0;
    static char SerialBuf[256];
    static uint8_t CommState = COMM_VER_REQ;
    static uint8_t CommTimeout = 0;
    static char *ret;
#endif
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

//common code here
    // Check the external switch and RCM sensor
    ExtSwitch.CheckSwitch();

#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION == 3 //CH32 and v3
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
#endif //v3
#if SMARTEVSE_VERSION == 4 //v4

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
        if (ret != NULL) {
            _LOG_A("WCH %s.\n,", SerialBuf);
        } else {                                                            // parse the line
/*                ret = strstr(SerialBuf, "Pilot:");
            //  [<-] Pilot:6,State:0,ChargeDelay:0,Error:0,Temp:34,Lock:0,Mode:0,Access:1
            if (ret != NULL) {
                int hit = sscanf(SerialBuf, "Pilot:%u,State:%u,ChargeDelay:%u,Error:%u,Temp:%i,Lock:%u,Mode:%u, Access:%u", &pilot, &State, &ChargeDelay, &ErrorFlags, &TempEVSE, &Lock, &Mode, &Access_bit);
                if (hit != 8) {
                    _LOG_A("ERROR parsing line from WCH, hit=%i, line=%s.\n", hit, SerialBuf);
                } else {
                    _LOG_A("DINGO: pilot=%u, State=%u, ChargeDelay=%u, ErrorFlags = %u, TempEVSE=%i, Lock=%u, Mode=%u, Access_bit=%i.\n", pilot, State,ChargeDelay, ErrorFlags, TempEVSE, Lock, Mode, Access_bit);
                }
            }*/

            strncpy(token, "ExtSwitch:", sizeof(token));
            ret = strstr(SerialBuf, token);
            if (ret != NULL) {
                ExtSwitch.Pressed = atoi(ret+strlen(token));
                ExtSwitch.TimeOfPress = millis();
                ExtSwitch.HandleSwitch();
            }

            strncpy(token, "Access:", sizeof(token)); //b
            ret = strstr(SerialBuf, token);
            if (ret != NULL) {
                setAccess(atoi(ret+strlen(token)));
            }

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

            strncpy(token, "Pilot:", sizeof(token));
            ret = strstr(SerialBuf, token);
            if (ret != NULL) {
                pilot = atoi(ret+strlen(token)); //e
            }

            strncpy(token, "EnableC2:", sizeof(token));
            ret = strstr(SerialBuf, token);
            if (ret != NULL) {
                EnableC2 = (EnableC2_t) atoi(ret+strlen(token)); //e
            }

            strncpy(token, "State:", sizeof(token));
            ret = strstr(SerialBuf, token);
            if (ret != NULL ) {
                State = atoi(ret+strlen(token)); //e
/*
                if (State == STATE_COMM_B) NewState = STATE_COMM_B_OK;
                else if (State == STATE_COMM_C) NewState = STATE_COMM_C_OK;

                if (NewState) {    // only send confirmation when state needs to change.
                    Serial1.printf("WchState:%u\n",NewState );        // send confirmation back to WCH
                    Serial.printf("[->] WchState:%u\n",NewState );    // send confirmation back to WCH
                    NewState = 0;
                }*/
            }
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
                Serial1.printf("Config:%u,Lock:%u,Mode:%u,LoadBl:%u,Current:%u,Switch:%u,RCmon:%u,PwrPanic:%u,RFIDReader:%u,ModemPwr:%u,Initialized:%u\n", Config, Lock, Mode, LoadBl, ChargeCurrent, Switch, RCmon, PwrPanic, RFIDReader, ModemPwr, Initialized);
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
    if ((ErrorFlags & CT_NOCOMM) && MainsMeterTimeout == 10) ErrorFlags &= ~CT_NOCOMM;
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
#endif //SMARTEVSE_VERSION


void setStatePowerUnavailable(void) {
    if (State == STATE_A)
       return;
    //State changes between A,B,C,D are caused by EV or by the user
    //State changes between x1 and x2 are created by the EVSE
    //State changes between x1 and x2 indicate availability (x2) of unavailability (x1) of power supply to the EV
    if (State == STATE_C) setState(STATE_C1);                       // If we are charging, tell EV to stop charging
    else if (State != STATE_C1) setState(STATE_B1);                 // If we are not in State C1, switch to State B1
}


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
#if SMARTEVSE_VERSION == 3
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
            ErrorFlags = val;
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



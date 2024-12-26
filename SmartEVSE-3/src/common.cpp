/*
 * This file has shared code between SmartEVSE-3, SmartEVSE-4 and SmartEVSE-4_CH32
 * #if SMARTEVSE_VERSION == 3  //SmartEVSEv3 code
 * #if SMARTEVSE_VERSION == 4  //SmartEVSEv4 code
 * #ifndef SMARTEVSE_VERSION   //CH32 code
 */

#include "main.h"
#include "common.h"
#include "stdio.h"

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

#ifdef SMARTEVSE_VERSION //v3 or v4
#include "meter.h"
#endif


// gateway to the outside world
// here declarations are placed for variables that are both used on CH32 as ESP32
// (either temporarily while developing or definite)
// and they are mainly used in the main.cpp/common.cpp code
EXT uint32_t elapsedmax, elapsedtime;
EXT int8_t TempEVSE;
EXT uint16_t SolarStopTimer, MaxCapacity, MainsCycleTime, ChargeCurrent, MinCurrent, MaxCurrent, BalancedMax[NR_EVSES], ADC_CP[NUM_ADC_SAMPLES], ADCsamples[25];
EXT uint8_t RFID[8], Access_bit, Mode, Lock, ErrorFlags, ChargeDelay, State, LoadBl, PilotDisconnectTime, AccessTimer, ActivationMode, ActivationTimer, RFIDReader, C1Timer, UnlockCable, LockCable, RxRdy1, MainsMeterTimeout, PilotDisconnected, ModbusRxLen, PowerPanicFlag, Switch, RCmon, TestState, Config, PwrPanic, ModemPwr, Initialized, pilot;
EXT bool CustomButton, GridRelayOpen;
#ifdef SMARTEVSE_VERSION //v3 and v4
EXT hw_timer_t * timerA;
esp_adc_cal_characteristics_t * adc_chars_CP;
#endif
EXT struct Node_t Node[NR_EVSES];
EXT uint8_t BalancedState[NR_EVSES];


//functions
EXT void setup();
//EXT void setAccess(uint8_t Access);
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
EXT void CalcBalancedCurrent(char mod);

Single_Phase_t Switching_To_Single_Phase = FALSE;
uint16_t MaxSumMainsTimer = 0;
uint8_t LCDTimer = 0;

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
// a. CH32 setAccess sends message to ESP32           in CH32 src/evse.c
// b. ESP32 receiver that calls local setState        in ESP32 src/main.cpp
// c. ESP32 setAccess full functionality              in ESP32 src/common.cpp (this file)
// d. ESP32 sends message to CH32                     in ESP32 src/common.cpp (this file)
// e. CH32 receiver that sets local variable          in CH32 src/evse.c

#ifdef SMARTEVSE_VERSION //v3 and v4
void setAccess(bool Access) { //c
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
}
#endif //SMARTEVSE_VERSION


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

#ifndef SMARTEVSE_VERSION //CH32
// Determine the state of the Pilot signal
//
uint8_t Pilot() {

    uint16_t sample, Min = 4095, Max = 0;
    uint8_t n, ret;

    // calculate Min/Max of last 32 CP measurements (32 ms)
    for (n=0 ; n<NUM_ADC_SAMPLES ;n++) {

        sample = ADC_CP[n];
        if (sample < Min) Min = sample;                                   // store lowest value
        if (sample > Max) Max = sample;                                   // store highest value
    }

    //printf("min:%u max:%u\n",Min ,Max);

    // test Min/Max against fixed levels    (needs testing)
    if (Min >= 4000 ) ret = PILOT_12V;                                      // Pilot at 12V
    if ((Min >= 3300) && (Max < 4000)) ret = PILOT_9V;                      // Pilot at 9V
    if ((Min >= 2400) && (Max < 3300)) ret = PILOT_6V;                      // Pilot at 6V
    if ((Min >= 2000) && (Max < 2400)) ret = PILOT_3V;                      // Pilot at 3V
    if ((Min > 100) && (Max < 350)) ret = PILOT_DIODE;                      // Diode Check OK
    ret = PILOT_NOK;                                                        // Pilot NOT ok
    printf("Pilot=%u\n", ret); //d
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


#ifndef SMARTEVSE_VERSION //CH32
//NOTE that CH32 has a 10ms routine that has to be called every 10ms
//and ESP32 has a 10ms routine that is called once and has a while loop with 10ms delay in it
void Timer10ms_singlerun(void) {
    static uint8_t pilot, DiodeCheck = 0;

    BlinkLed();

    // Check the external switch and RCM sensor
    ExtSwitch.CheckSwitch();

    //Check RS485 communication
    if (ModbusRxLen) CheckRS485Comm();

    // sample the Pilot line
    pilot = Pilot();

    // ############### EVSE State A #################

    if (State == STATE_A || State == STATE_COMM_B || State == STATE_B1)
    {

        // When the pilot line is disconnected, wait for PilotDisconnectTime, then reconnect
        if (PilotDisconnected) {
            if (PilotDisconnectTime == 0 && pilot == PILOT_NOK ) {          // Pilot should be ~ 0V when disconnected
                PILOT_CONNECTED;
                PilotDisconnected = 0;
                printf("Pilot Connected\n");
            }
        } else if (pilot == PILOT_12V) {                                    // Check if we are disconnected, or forced to State A, but still connected to the EV

            // If the RFID reader is set to EnableOne mode, and the Charging cable is disconnected
            // We start a timer to re-lock the EVSE (and unlock the cable) after 60 seconds.
            if (RFIDReader == 2 && AccessTimer == 0 && Access_bit == 1) AccessTimer = RFIDLOCKTIME;

            if (State != STATE_A) setState(STATE_A);                        // reset state, in case we were stuck in STATE_COMM_B
            ChargeDelay = 0;                                                // Clear ChargeDelay when disconnected.

        } else if ( pilot == PILOT_9V && ErrorFlags == NO_ERROR
            && ChargeDelay == 0 && Access_bit
            && State != STATE_COMM_B) {                                     // switch to State B ?

            DiodeCheck = 0;

            MaxCapacity = ProximityPin();                                   // Sample Proximity Pin

            printf("Cable limit: %uA\n", MaxCapacity);
//            if (MaxCurrent > MaxCapacity) ChargeCurrent = MaxCapacity * 10; // Do not modify Max Cable Capacity or MaxCurrent (fix 2.05)
//            else ChargeCurrent = MinCurrent * 10;                           // Instead use new variable ChargeCurrent

            // Load Balancing : Node
            setState(STATE_COMM_B);                                         // Node wants to switch to State B
        }
    }

    // ########### EVSE State Comm B OK ##############

    if (State == STATE_COMM_B_OK) {
        setState(STATE_B);
        ActivationMode = 30;                                                // Activation mode is triggered if state C is not entered in 30 seconds.
        AccessTimer = 0;
    }

    // ############### EVSE State B #################

    if ((State == STATE_B) || (State == STATE_COMM_C)) {

        // PILOT 12V
        if (pilot == PILOT_12V) {                                           // Disconnected?
            setState(STATE_A);                                              // switch to STATE_A

        // PILOT 6V
        } else if (pilot == PILOT_6V) {

            if ((DiodeCheck == 1) && (ErrorFlags == NO_ERROR) && (ChargeDelay == 0) && (State == STATE_B)) {
                setState(STATE_COMM_C);                                     // Send request to Master
            }

        // PILOT_9V
        } else if (pilot == PILOT_9V) {

            if (ActivationMode == 0) {
                setState(STATE_ACTSTART);
                ActivationTimer = 3;
                TIM1->CH1CVR = 0;                                           // CP PWM off, duty cycle 0%
            }
        // PILOT DIODE
        } else if (pilot == PILOT_DIODE) {
            DiodeCheck = 1;                                                 // Diode found, OK
            printf("Diode OK\n");
            TIM1->CH4CVR = PWM_5;                                           // start ADC sampling at 5%
        }

    }

    // ############### EVSE State C1 #################

    if (State == STATE_C1)
    {
        if (pilot == PILOT_12V)
        {                                                                   // Disconnected or connected to EV without PWM
            setState(STATE_A);                                              // switch to STATE_A
        }
        else if (pilot == PILOT_9V)
        {
            setState(STATE_B1);                                             // switch to State B1
        }
    }

    // ########### EVSE ActivationMode End ############

    if (State == STATE_ACTSTART && ActivationTimer == 0) {
        setState(STATE_B);                                                  // Switch back to State B
        ActivationMode = 255;                                               // Disable ActivationMode
    }

    // ########### EVSE State Comm C OK ##############

    if (State == STATE_COMM_C_OK) {
        DiodeCheck = 0;
        setState(STATE_C);                                                  // switch to STATE_C
     }

    // ############### EVSE State C ##################

    if (State == STATE_C) {

        if (pilot == PILOT_12V) {                                           // Disconnected ?
            setState(STATE_A);                                              // switch back to STATE_A

        } else if (pilot == PILOT_9V) {
            setState(STATE_B);                                              // switch back to STATE_B
            DiodeCheck = 0;
        }

    } // end of State C code

    // Clear communication error, if present
    if ((ErrorFlags & CT_NOCOMM) && MainsMeterTimeout == 10) ErrorFlags &= ~CT_NOCOMM;



}


#else //v3 or v4
// Task that handles EVSE State Changes
// Reads buttons, and updates the LCD.
//
// called every 10ms
void Timer10ms(void * parameter) {
    uint16_t old_sec = 0;
#if SMARTEVSE_VERSION == 3
    uint8_t DiodeCheck = 0;
    uint16_t StateTimer = 0;                                                 // When switching from State B to C, make sure pilot is at 6v for 100ms
#else
    uint8_t RXbyte, idx = 0;
    char SerialBuf[256];
    uint8_t CommState = COMM_VER_REQ;
    uint8_t CommTimeout = 0;
    char *ret;
#endif
    // infinite loop
    while(1) {

        getButtonState();

        // When one or more button(s) are pressed, we call GLCDMenu
        if (((ButtonState != 0x07) || (ButtonState != OldButtonState)) && !LCDlock) GLCDMenu(ButtonState);

        // Update/Show Helpmenu
        if (LCDNav > MENU_ENTER && LCDNav < MENU_EXIT && (ScrollTimer + 5000 < millis() ) && (!SubMenu)) GLCDHelp();

        if (timeinfo.tm_sec != old_sec) {
            old_sec = timeinfo.tm_sec;
            GLCD();
        }

        // Check the external switch and RCM sensor
        ExtSwitch.CheckSwitch();

#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION == 3 //v3 and CH32
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
                            if (!LCDNav) GLCD();                                // Don't update the LCD if we are navigating the menu
                                                                                // immediately update LCD (20ms)
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
                GLCD_init();                                                    // Re-init LCD; necessary because switching contactors can cause LCD to mess up

            } else if (pilot == PILOT_9V) {
                setState(STATE_B);                                              // switch back to STATE_B
                DiodeCheck = 0;
                GLCD_init();                                                    // Re-init LCD (200ms delay); necessary because switching contactors can cause LCD to mess up
                                                                                // Mark EVSE as inactive (still State B)
            } else if (pilot != PILOT_6V) {                                     // Pilot level at anything else is an error
                if (++StateTimer > 50) {                                        // make sure it's not a glitch, by delaying by 500mS (re-using StateTimer here)
                    StateTimer = 0;                                             // Reset StateTimer for use in State B
                    setState(STATE_B);
                    DiodeCheck = 0;
                    GLCD_init();                                                // Re-init LCD (200ms delay); necessary because switching contactors can cause LCD to mess up
                }

            } else StateTimer = 0;

        } // end of State C code
#endif
#if SMARTEVSE_VERSION == 4

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

                ret = strstr(SerialBuf, "version:");
                if (ret != NULL) {
                    MainVersion = atoi(ret+8);
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
                    pilot = atoi(ret+6); //e
                }

                ret = strstr(SerialBuf, "State:"); // current State (request) received from Wch
                if (ret != NULL ) {
                    State = atoi(ret+6); //e
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

#endif //SMARTEVSE_VERSION

        // Pause the task for 10ms
        vTaskDelay(10 / portTICK_PERIOD_MS);
    } // while(1) loop
}
#endif //SMARTEVSE_VERSION

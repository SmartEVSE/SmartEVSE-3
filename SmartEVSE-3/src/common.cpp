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


// gateway to the outside world
EXT uint32_t elapsedmax, elapsedtime;
EXT int8_t TempEVSE;
EXT uint16_t SolarStopTimer, MaxCapacity, MainsCycleTime, MaxSumMainsTimer;
EXT uint8_t RFID[8], Access_bit, Mode, Lock, ErrorFlags, ChargeDelay, State, LoadBl, PilotDisconnectTime, AccessTimer, ActivationMode, ActivationTimer, RFIDReader, C1Timer, UnlockCable, LockCable, RxRdy1, MainsMeterTimeout, PilotDisconnected, ModbusRxLen, PowerPanicFlag, Switch, RCmon, TestState;
EXT bool CustomButton, GridRelayOpen;


EXT void setup();
EXT uint8_t Pilot();
EXT void setAccess(uint8_t Access);
EXT void setState(uint8_t NewState);
EXT int8_t TemperatureSensor();
EXT void CheckSerialComm(void);
EXT uint8_t OneWireReadCardId();
EXT void CheckRS485Comm(void);
EXT void BlinkLed(void);
EXT uint8_t ProximityPin();
EXT void PowerPanic(void);
EXT //void delay(uint32_t ms);

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

#ifdef SMARTEVSE_VERSION //v3 and v4
void setAccess(bool Access) {
    Access_bit = Access;
#if SMARTEVSE_VERSION == 4
    Serial1.printf("Access:%u\n", Access_bit);
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

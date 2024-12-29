#ifndef __COMMON
#define __COMMON

//here should only be declarations for code that will not run on the CH32

#ifdef SMARTEVSE_VERSION //v3 and v4
#include <Arduino.h>
#include "glcd.h"
#endif

#include "debug.h"
#include "stdint.h"
#include "common_c.h"

#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION == 3 //CH32 and v3
//#include "meter.h"
#endif

#define SOLARSTARTTIME 40                                                       // Seconds to keep chargecurrent at 6A
#define MAX_SUMMAINSTIME 0
#define AUTOUPDATE 0                                                            // default for Automatic Firmware Update: 0 = disabled, 1 = enabled

class Button {
  public:
    void CheckSwitch(bool force = false);
    bool Pressed;                                                               // when io = low key is pressed
    uint32_t TimeOfPress;                                                       // we need the time when the button or switch was pressed to detect longpress
    void HandleSwitch(void);

    // constructor
    Button(void);
};

extern Button ExtSwitch;
extern uint8_t SB2_WIFImode;
extern uint8_t SubMenu;
extern uint8_t LCDNav;
extern uint8_t GridActive;
extern uint8_t Grid;
extern void Timer10ms(void * parameter);
extern void Timer100ms(void * parameter);
extern void Timer1S(void * parameter);
extern void getButtonState();
extern void PowerPanicESP();

extern uint8_t LCDlock, MainVersion;
enum Single_Phase_t { FALSE, GOING_TO_SWITCH, AFTER_SWITCH };
extern void CalcBalancedCurrent(char mod);
extern void write_settings(void);
extern void setStatePowerUnavailable(void);

struct Sensorbox {
    uint8_t SoftwareVer;        // Sensorbox 2 software version
    uint8_t WiFiConnected;      // 0:not connected / 1:connected to WiFi
    uint8_t WiFiAPSTA;          // 0:no portal /  1: portal active
    uint8_t WIFImode;           // 0:Wifi Off / 1:WiFi On / 2: Portal Start
    uint8_t IP[4];
    uint8_t APpassword[9];      // 8 characters + null termination
};


#endif

#ifndef __COMMON
#define __COMMON

#ifdef SMARTEVSE_VERSION //v3 and v4
#include <Arduino.h>
#endif

#include "debug.h"
#include "stdint.h"
#include "common_c.h"

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

enum Single_Phase_t { FALSE, GOING_TO_SWITCH, AFTER_SWITCH };

#endif

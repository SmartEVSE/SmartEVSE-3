#ifndef __COMMON
#define __COMMON

#ifdef SMARTEVSE_VERSION //v3 and v4
#include <Arduino.h>
#include "debug.h"
#endif

#include "stdint.h"


class Button {
  public:
    bool Pressed;                                                               // when io = low key is pressed
    uint32_t TimeOfToggle;                                                      // the time when the button or switch was pressed or released or toggled
    void CheckSwitch(bool force = false);
    void HandleSwitch(void);
    // constructor
    Button(void);
  private:
    bool handling_longpress = false;
};

extern Button ExtSwitch;

#endif

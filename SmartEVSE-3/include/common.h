#ifndef __COMMON
#define __COMMON


#include <Arduino.h>
#include "debug.h"
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

#endif

extern Button ExtSwitch;

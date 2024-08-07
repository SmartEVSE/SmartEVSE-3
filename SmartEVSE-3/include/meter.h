
/*
;    Project: Smart EVSE v3
;
;
; Permission is hereby granted, free of charge, to any person obtaining a copy
; of this software and associated documentation files (the "Software"), to deal
; in the Software without restriction, including without limitation the rights
; to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
; copies of the Software, and to permit persons to whom the Software is
; furnished to do so, subject to the following conditions:
;
; The above copyright notice and this permission notice shall be included in
; all copies or substantial portions of the Software.
;
; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
; IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
; FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
; AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
; LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
; OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
; THE SOFTWARE.
 */

#ifndef __EVSE_METER

#define __EVSE_METER

#include "evse.h"


extern struct EMstruct EMConfig[EM_CUSTOM + 1];
extern struct ModBus MB;

class Meter {
  public:
    uint8_t Type;                                                               // previously: MainsMeter; Type of Mains electric meter (0: Disabled / Constants EM_*)
    uint8_t Address;
    int16_t Irms[3];                                                            // Momentary current per Phase (23 = 2.3A) (resolution 100mA)
    int16_t Imeasured;                                                          // Max of all Phases (Amps *10) of mains power
    int16_t Power[3];
    int16_t PowerMeasured;                                                      // Measured Charge power in Watt by kWh meter (sum of all phases)
    uint8_t Timeout;
    int32_t Import_active_energy;                                               // Imported active energy
    int32_t Export_active_energy;                                               // Exported active energy
    int32_t Energy;                                                             // Wh -> Import_active_energy - Export_active_energy
    int32_t EnergyCharged;                                                      // kWh meter value energy charged. (Wh) (will reset if state changes from A->B)
    int32_t EnergyMeterStart;                                                   // kWh meter value is stored once EV is connected to EVSE (Wh)
    uint8_t ResetKwh;                                                           // if set, reset kwh meter at state transition B->C
                                                                                // cleared when charging, reset to 1 when disconnected (state A)
    // constructor
    Meter(uint8_t type, uint8_t address, uint8_t timeout);
    void UpdateEnergies();
    void ResponseToMeasurement();
    void CalcImeasured(void);
  private:
    uint8_t receiveCurrentMeasurement(uint8_t *buf);
    signed int receivePowerMeasurement(uint8_t *buf);
    signed int receiveEnergyMeasurement(uint8_t *buf);
    void combineBytes(void *var, uint8_t *buf, uint8_t pos, uint8_t endianness, MBDataType dataType);
    signed int decodeMeasurement(uint8_t *buf, uint8_t Count, signed char Divisor);
    signed int decodeMeasurement(uint8_t *buf, uint8_t Count, uint8_t Endianness, MBDataType dataType, signed char Divisor);
};

extern Meter MainsMeter;
extern Meter EVMeter;                                                         // Type of EV electric meter (0: Disabled / Constants EM_*)

#endif

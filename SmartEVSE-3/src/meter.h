
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

#include "main.h"
#ifndef SMARTEVSE_VERSION //CH32
#include "ch32.h"
#endif


#define EM_SENSORBOX 1                                                          // Mains meter types
#define EM_PHOENIX_CONTACT 2
#define EM_FINDER_7E 3
#define EM_EASTRON3P 4
#define EM_EASTRON3P_INV 5
#define EM_ABB 6
#define EM_SOLAREDGE 7
#define EM_WAGO 8
#define EM_API 9
#define EM_EASTRON1P 10
#define EM_FINDER_7M 11
#define EM_SINOTIMER 12
#define EM_HOMEWIZARD_P1 13
#define EM_UNUSED_SLOT2 14
#define EM_UNUSED_SLOT3 15
#define EM_UNUSED_SLOT4 16
#define EM_CUSTOM 17

typedef enum mb_datatype {
    MB_DATATYPE_INT32 = 0,
    MB_DATATYPE_FLOAT32 = 1,
    MB_DATATYPE_INT16 = 2,
    MB_DATATYPE_MAX,
} MBDataType;

struct EMstruct {
    uint8_t Desc[10];
    uint8_t Endianness;     // 0: low byte first, low word first, 1: low byte first, high word first, 2: high byte first, low word first, 3: high byte first, high word first
    uint8_t Function;       // 3: holding registers, 4: input registers
    MBDataType DataType;    // How data is represented on this Modbus meter
    uint16_t URegister;     // Single phase voltage (V)
    int8_t UDivisor;        // 10^x
    uint16_t IRegister;     // Single phase current (A)
    int8_t IDivisor;        // 10^x
    uint16_t PRegister;     // Total power (W) -- only used for EV/PV meter momentary power
    int8_t PDivisor;        // 10^x
    uint16_t ERegister;     // Total imported energy (kWh); equals total energy if meter doesnt support exported energy
    int8_t EDivisor;        // 10^x
    uint16_t ERegister_Exp; // Total exported energy (kWh)
    int8_t EDivisor_Exp;    // 10^x
};

extern struct EMstruct EMConfig[EM_CUSTOM + 1];
extern struct Sensorbox SB2;

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
    void ResponseToMeasurement(struct ModBus MB);
    void CalcImeasured(void);
    void setTimeout(uint8_t Timeout);
  private:
    uint8_t receiveCurrentMeasurement(ModBus MB);
    signed int receivePowerMeasurement(uint8_t *buf);
    signed int receiveEnergyMeasurement(uint8_t *buf);
    void combineBytes(void *var, uint8_t *buf, uint8_t pos, uint8_t endianness, MBDataType dataType);
    signed int decodeMeasurement(uint8_t *buf, uint8_t Count, signed char Divisor);
    signed int decodeMeasurement(uint8_t *buf, uint8_t Count, uint8_t Endianness, MBDataType dataType, signed char Divisor);
};

extern Meter MainsMeter;
extern Meter EVMeter;                                                         // Type of EV electric meter (0: Disabled / Constants EM_*)

#endif

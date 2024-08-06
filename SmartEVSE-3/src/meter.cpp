#include <Arduino.h>
#include <meter.h>
#include <modbus.h>

extern unsigned long pow_10[10];
extern void CalcIsum(void);
extern void RecomputeSoC(void);

#define ENDIANESS_LBF_LWF 0
#define ENDIANESS_LBF_HWF 1
#define ENDIANESS_HBF_LWF 2
#define ENDIANESS_HBF_HWF 3

struct EMstruct EMConfig[EM_CUSTOM + 1] = {
    /* DESC,      ENDIANNESS,      FCT, DATATYPE,            U_REG,DIV, I_REG,DIV, P_REG,DIV, E_REG_IMP,DIV, E_REG_EXP, DIV */
    {"Disabled",  ENDIANESS_LBF_LWF, 0, MB_DATATYPE_INT32,        0, 0,      0, 0,      0, 0,      0, 0,0     , 0}, // First entry!
    {"Sensorbox", ENDIANESS_HBF_HWF, 4, MB_DATATYPE_FLOAT32, 0xFFFF, 0,      0, 0, 0xFFFF, 0, 0xFFFF, 0,0     , 0}, // Sensorbox (Own routine for request/receive)
    {"Phoenix C", ENDIANESS_HBF_LWF, 4, MB_DATATYPE_INT32,      0x0, 1,    0xC, 3,   0x28, 1,   0x3E, 1,0     , 0}, // PHOENIX CONTACT EEM-350-D-MCB (0,1V / mA / 0,1W / 0,1kWh) max read count 11
    {"Finder 7E", ENDIANESS_HBF_HWF, 4, MB_DATATYPE_FLOAT32, 0x1000, 0, 0x100E, 0, 0x1026, 0, 0x1106, 3,0x110E, 3}, // Finder 7E.78.8.400.0212 (V / A / W / Wh) max read count 127
    {"Eastron3P", ENDIANESS_HBF_HWF, 4, MB_DATATYPE_FLOAT32,    0x0, 0,    0x6, 0,   0x34, 0,  0x48 , 0,0x4A  , 0}, // Eastron SDM630 (V / A / W / kWh) max read count 80
    {"InvEastrn", ENDIANESS_HBF_HWF, 4, MB_DATATYPE_FLOAT32,    0x0, 0,    0x6, 0,   0x34, 0,  0x48 , 0,0x4A  , 0}, // Since Eastron SDM series are bidirectional, sometimes they are connected upsidedown, so positive current becomes negative etc.; Eastron SDM630 (V / A / W / kWh) max read count 80
    {"ABB",       ENDIANESS_HBF_HWF, 3, MB_DATATYPE_INT32,   0x5B00, 1, 0x5B0C, 2, 0x5B14, 2, 0x5000, 2,0x5004, 2}, // ABB B23 212-100 (0.1V / 0.01A / 0.01W / 0.01kWh) RS485 wiring reversed / max read count 125
    {"SolarEdge", ENDIANESS_HBF_HWF, 3, MB_DATATYPE_INT16,    40196, 0,  40191, 0,  40206, 0,  40234, 3, 40226, 3}, // SolarEdge SunSpec (0.01V (16bit) / 0.1A (16bit) / 1W  (16bit) / 1 Wh (32bit))
    {"WAGO",      ENDIANESS_HBF_HWF, 3, MB_DATATYPE_FLOAT32, 0x5002, 0, 0x500C, 0, 0x5012, -3, 0x600C, 0,0x6018, 0}, // WAGO 879-30x0 (V / A / kW / kWh)//TODO maar WAGO heeft ook totaal
    {"API",       ENDIANESS_HBF_HWF, 3, MB_DATATYPE_FLOAT32, 0x5002, 0, 0x500C, 0, 0x5012, 3, 0x6000, 0,0x6018, 0}, // WAGO 879-30x0 (V / A / kW / kWh)
    {"Eastron1P", ENDIANESS_HBF_HWF, 4, MB_DATATYPE_FLOAT32,    0x0, 0,    0x6, 0,   0x0C, 0,  0x48 , 0,0x4A  , 0}, // Eastron SDM630 (V / A / W / kWh) max read count 80
    {"Finder 7M", ENDIANESS_HBF_HWF, 4, MB_DATATYPE_FLOAT32,   2500, 0,   2516, 0,   2536, 0,   2638, 3,     0, 0}, // Finder 7M.38.8.400.0212 (V / A / W / Wh) / Backlight 10173
    {"Sinotimer", ENDIANESS_HBF_HWF, 4, MB_DATATYPE_INT16,      0x0, 1,    0x3, 2,    0x8, 0, 0x0027, 2,0x0031, 2}, // Sinotimer DTS6619 (0.1V (16bit) / 0.01A (16bit) / 1W  (16bit) / 1 Wh (32bit))
    {"Unused 1",  ENDIANESS_LBF_LWF, 4, MB_DATATYPE_INT32,        0, 0,      0, 0,      0, 0,      0, 0,     0, 0}, // unused slot for future new meters
    {"Unused 2",  ENDIANESS_LBF_LWF, 4, MB_DATATYPE_INT32,        0, 0,      0, 0,      0, 0,      0, 0,     0, 0}, // unused slot for future new meters
    {"Unused 3",  ENDIANESS_LBF_LWF, 4, MB_DATATYPE_INT32,        0, 0,      0, 0,      0, 0,      0, 0,     0, 0}, // unused slot for future new meters
    {"Unused 4",  ENDIANESS_LBF_LWF, 4, MB_DATATYPE_INT32,        0, 0,      0, 0,      0, 0,      0, 0,     0, 0}, // unused slot for future new meters
    {"Custom",    ENDIANESS_LBF_LWF, 4, MB_DATATYPE_INT32,        0, 0,      0, 0,      0, 0,      0, 0,     0, 0}  // Last entry!
};


Meter::Meter(uint8_t type, uint8_t address, uint8_t timeout) {
    for (int x = 1; x < 3; x++) {
        Irms[x] = 0;
        Power[x] = 0;
    }
    Type = type;
    Address = address;
    Imeasured = 0;
    Import_active_energy = 0;
    Export_active_energy = 0;
    Energy = 0;
    Timeout = timeout;
    EnergyCharged = 0;                                                  // kWh meter value energy charged. (Wh) (will reset if state changes from A->B)
    EnergyMeterStart = 0;                                               // kWh meter value is stored once EV is connected to EVSE (Wh)
    PowerMeasured = 0;                                                  // Measured Charge power in Watt by kWh meter
    ResetKwh = 2;                                                       // if set, reset EV kwh meter at state transition B->C
                                                                        // cleared when charging, reset to 1 when disconnected (state A)
}

/**
 * Combine Bytes received over modbus
 *
 * @param pointer to var
 * @param pointer to buf
 * @param uint8_t pos
 * @param uint8_t endianness:\n
 *        0: low byte first, low word first (little endian)\n
 *        1: low byte first, high word first\n
 *        2: high byte first, low word first\n
 *        3: high byte first, high word first (big endian)
 * @param MBDataType dataType: used to determine how many bytes should be combined
 */
void Meter::combineBytes(void *var, uint8_t *buf, uint8_t pos, uint8_t endianness, MBDataType dataType) {
    char *pBytes;
    pBytes = (char *)var;

    // ESP32 is little endian
    switch(endianness) {
        case ENDIANESS_LBF_LWF: // low byte first, low word first (little endian)
            *pBytes++ = (uint8_t)buf[pos + 0];
            *pBytes++ = (uint8_t)buf[pos + 1];
            if (dataType != MB_DATATYPE_INT16) {
                *pBytes++ = (uint8_t)buf[pos + 2];
                *pBytes   = (uint8_t)buf[pos + 3];
            }
            break;
        case ENDIANESS_LBF_HWF: // low byte first, high word first
            if (dataType != MB_DATATYPE_INT16) {
                *pBytes++ = (uint8_t)buf[pos + 2];
                *pBytes++ = (uint8_t)buf[pos + 3];
            }
            *pBytes++ = (uint8_t)buf[pos + 0];
            *pBytes   = (uint8_t)buf[pos + 1];
            break;
        case ENDIANESS_HBF_LWF: // high byte first, low word first
            *pBytes++ = (uint8_t)buf[pos + 1];
            *pBytes++ = (uint8_t)buf[pos + 0];
            if (dataType != MB_DATATYPE_INT16) {
                *pBytes++ = (uint8_t)buf[pos + 3];
                *pBytes   = (uint8_t)buf[pos + 2];
            }
            break;
        case ENDIANESS_HBF_HWF: // high byte first, high word first (big endian)
            if (dataType != MB_DATATYPE_INT16) {
                *pBytes++ = (uint8_t)buf[pos + 3];
                *pBytes++ = (uint8_t)buf[pos + 2];
            }
            *pBytes++ = (uint8_t)buf[pos + 1];
            *pBytes   = (uint8_t)buf[pos + 0];
            break;
        default:
            break;
    }
}

/**
 * Decode measurement value
 *
 * @param pointer to buf
 * @param uint8_t Count
 * @param signed char Divisor
 * @return signed int Measurement
 */

signed int Meter::decodeMeasurement(uint8_t *buf, uint8_t Count, signed char Divisor) {
    return decodeMeasurement(buf, Count, EMConfig[Type].Endianness, EMConfig[Type].DataType, Divisor);
}

signed int Meter::decodeMeasurement(uint8_t *buf, uint8_t Count, uint8_t Endianness, MBDataType dataType, signed char Divisor) {
    float dCombined;
    signed int lCombined;

    if (dataType == MB_DATATYPE_FLOAT32) {
        combineBytes(&dCombined, buf, Count * (dataType == MB_DATATYPE_INT16 ? 2u : 4u), Endianness, dataType);
        if (Divisor >= 0) {
            lCombined = (signed int)(dCombined / (signed int)pow_10[(unsigned)Divisor]);
        } else {
            lCombined = (signed int)(dCombined * (signed int)pow_10[(unsigned)-Divisor]);
        }
    } else {
        combineBytes(&lCombined, buf, Count * (dataType == MB_DATATYPE_INT16 ? 2u : 4u), Endianness, dataType);
        if (dataType == MB_DATATYPE_INT16) {
            lCombined = (signed int)((int16_t)lCombined); /* sign extend 16bit into 32bit */
        }
        if (Divisor >= 0) {
            lCombined = lCombined / (signed int)pow_10[(unsigned)Divisor];
        } else {
            lCombined = lCombined * (signed int)pow_10[(unsigned)-Divisor];
        }
    }

    return lCombined;
}

/**
 * Read current measurement from modbus
 *
 * @param pointer to buf
 * @param uint8_t Meter
 * @param pointer to Current (mA)
 * @return uint8_t error
 */
uint8_t Meter::receiveCurrentMeasurement(uint8_t *buf) {
    uint8_t x, offset;
    int32_t var[3];

    switch(Type) {
        case EM_API:
            break;
        case EM_SENSORBOX:
            // return immediately if the data contains no new P1 or CT measurement
            if (buf[3] == 0) return 0;  // error!!
            // determine if there is P1 data present, otherwise use CT data
            if (buf[3] & 0x80) offset = 4;                                      // P1 data present
            else offset = 7;                                                    // Use CTs
            // offset 16 is Smart meter P1 current
            for (x = 0; x < 3; x++) {
                // SmartEVSE works with Amps * 10
                var[x] = decodeMeasurement(buf, offset + x, EMConfig[Type].IDivisor - 3u);
                if (offset == 7) {
                    // When MaxMains is set to >100A, it's assumed 200A:50ma CT's are used.
                    if (getItemValue(MENU_MAINS) > 100) var[x] = var[x] * 2;                    // Multiply measured currents with 2
                    // very small negative currents are shown as zero.
                    if ((var[x] > -1) && (var[x] < 1)) var[x] = 0;
                }
            }
            // Set Sensorbox 2 to 3/4 Wire configuration (and phase Rotation) (v2.16)
            if (buf[1] >= 0x10 && offset == 7) {
                GridActive = 1;                                                 // Enable the GRID menu option
                if ((buf[1] & 0x3) != (Grid << 1) && (LoadBl < 2)) ModbusWriteSingleRequest(0x0A, 0x800, Grid << 1);
            } else GridActive = 0;
            break;
        case EM_SOLAREDGE:
        {
            // Need to handle the extra scaling factor
            int scalingFactor = -(int)decodeMeasurement(buf, 3, 0);
            // Now decode the three Current values using that scaling factor
            for (x = 0; x < 3; x++) {
                var[x] = decodeMeasurement(buf, x, scalingFactor - 3);
            }
            break;
        }
        default:
            for (x = 0; x < 3; x++) {
                var[x] = decodeMeasurement(buf, x, EMConfig[Type].IDivisor - 3);
            }
            break;
    }

    // Get sign from power measurement on some electric meters
    offset = 0;
    switch(Type) {
        case EM_EASTRON1P:                                                  // for some reason the EASTRON1P also needs to loop through the 3 var[x]
                                                                            // if you only loop through x=0, the minus sign of the current is incorrect
                                                                            // when exporting current
            //fallthrough
        case EM_EASTRON3P:
            //fallthrough
        case EM_EASTRON3P_INV:
            offset = 3u;
            break;
        case EM_ABB:
            offset = 5u;
            break;
        case EM_FINDER_7M:
            offset = 7u;
            break;
    }
    if (offset) {                                                               // this is one of the meters that has to measure power to determine current direction
        PowerMeasured = 0;                                                      // so we calculate PowerMeasured so we dont have to poll for this again
        for (x = 0; x < 3; x++) {
            Power[x] = decodeMeasurement(buf, x + offset, EMConfig[Type].PDivisor);
            PowerMeasured += Power[x];
            if (Type != EM_EASTRON3P_INV) {
                if (Power[x] < 0) var[x] = -var[x];
            } else {
                if (Power[x] > 0) var[x] = -var[x];
            }
        }
    }

    // Convert Irms from mA to deciAmpère (A * 10)
    for (x = 0; x < 3; x++) {
        Irms[x] = (var[x] / 100);            // Convert to AMPERE * 10
    }

    // all OK
    return 1;
}

/**
 * Read energy measurement from modbus
 *
 * @param pointer to buf
 * @param uint8_t Meter
 * @return signed int Energy (Wh)
 */
signed int Meter::receiveEnergyMeasurement(uint8_t *buf) {
    switch (Type) {
        case EM_ABB:
            // Note:
            // - ABB uses 32-bit values, except for this measurement it uses 64bit unsigned int format
            // We skip the first 4 bytes (effectivaly creating uint 32). Will work as long as the value does not exeed  roughly 20 million
            return decodeMeasurement(buf, 1, EMConfig[Type].Endianness, MB_DATATYPE_INT32, EMConfig[Type].EDivisor-3);
        case EM_SOLAREDGE:
            // Note:
            // - SolarEdge uses 16-bit values, except for this measurement it uses 32bit int format
            // - EM_SOLAREDGE should not be used for EV Energy Measurements
            return decodeMeasurement(buf, 0, EMConfig[Type].Endianness, MB_DATATYPE_INT32, EMConfig[Type].EDivisor - 3);
        case EM_SINOTIMER:
            // Note:
            // - Sinotimer uses 16-bit values, except for this measurement it uses 32bit int format
            return decodeMeasurement(buf, 0, EMConfig[Type].Endianness, MB_DATATYPE_INT32, EMConfig[Type].EDivisor - 3);
        default:
            return decodeMeasurement(buf, 0, EMConfig[Type].Endianness, EMConfig[Type].DataType, EMConfig[Type].EDivisor - 3);
    }
}

/**
 * Read Power measurement from modbus
 *
 * @param pointer to buf
 * @param uint8_t Meter
 * @return signed int Power (W)
  */
signed int Meter::receivePowerMeasurement(uint8_t *buf) {
    switch (Type) {
        case EM_SOLAREDGE:
        {
            // Note:
            // - SolarEdge uses 16-bit values, with a extra 16-bit scaling factor
            // - EM_SOLAREDGE should not be used for EV power measurements, only PV power measurements are supported
            int scalingFactor = -(int)decodeMeasurement( buf, 1, 0);
            return decodeMeasurement(buf, 0, scalingFactor);
        }
        case EM_EASTRON3P_INV:
            return -decodeMeasurement(buf, 0, EMConfig[Type].PDivisor);
        case EM_SINOTIMER:
        {
            //Note:
            // - Sinotimer does not output total power but only individual power of the 3 phases which we need to add to eachother.
            Power[0] = (int)decodeMeasurement(buf, 0, EMConfig[Type].PDivisor);
            Power[1] = (int)decodeMeasurement(buf, 1, EMConfig[Type].PDivisor);
            Power[2] = (int)decodeMeasurement(buf, 2, EMConfig[Type].PDivisor);
            _LOG_V("Received power EVmeter L1=(%iW), L2=(%iW), L3=(%iW)\n", Power[0], Power[1], Power[2]);
            return (Power[0] + Power[1] + Power[2]);
        }
        default:
            return decodeMeasurement(buf, 0, EMConfig[Type].PDivisor);
    }
}


void Meter::UpdateEnergies() {
    Energy = Import_active_energy - Export_active_energy;
    if (ResetKwh == 2) EnergyMeterStart = Energy;                               // At powerup, set Energy to kwh meter value
    EnergyCharged = Energy - EnergyMeterStart;                                  // Calculate Energy
#if MODEM
    RecomputeSoC();
#endif
}

// Calls appropriate measurement from response
void Meter::ResponseToMeasurement() {
    if (MB.Type == MODBUS_RESPONSE) {
        if (MB.Register == EMConfig[Type].IRegister) {
            if (Address == MainsMeter.Address) {
                if (receiveCurrentMeasurement(MB.Data)) {
                    Timeout = COMM_TIMEOUT;
                }
                CalcIsum();
            } else if (Address == EVMeter.Address) {
                if (receiveCurrentMeasurement(MB.Data)) {
                    Timeout = COMM_EVTIMEOUT;
                }
                CalcImeasured();
            }
        } else if (MB.Register == EMConfig[Type].PRegister) {
            PowerMeasured = receivePowerMeasurement(MB.Data);
        } else if (MB.Register == EMConfig[Type].ERegister) {
            //import active energy
            if (Type == EM_EASTRON3P_INV)
                Export_active_energy = receiveEnergyMeasurement(MB.Data);
            else
                Import_active_energy = receiveEnergyMeasurement(MB.Data);
            UpdateEnergies();
        } else if (MB.Register == EMConfig[Type].ERegister_Exp) {
            //export active energy
            if (Type == EM_EASTRON3P_INV)
                Import_active_energy = receiveEnergyMeasurement(MB.Data);
            else
                Export_active_energy = receiveEnergyMeasurement(MB.Data);
            UpdateEnergies();
        }

    }
}

void Meter::CalcImeasured(void) {
    // Initialize Imeasured (max power used) to first channel.
    Imeasured = Irms[0];
    for (int x = 1; x < 3; x++) {
        if (Irms[x] > Imeasured) Imeasured = Irms[x];
    }
}


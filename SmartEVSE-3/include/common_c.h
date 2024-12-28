#ifndef __COMMON_C
#define __COMMON_C


//here should be declarations for code that will run on both the CH32 and the ESP32
//THUS it can only be C-code, NO C++ here!!

struct Node_t {
    uint8_t Online;
    uint8_t ConfigChanged;
    uint8_t EVMeter;
    uint8_t EVAddress;
    uint8_t MinCurrent;     // 0.1A
    uint8_t Phases;
    uint32_t Timer;         // 1s
    uint32_t IntTimer;      // 1s
    uint16_t SolarTimer;    // 1s
    uint8_t Mode;
};

typedef enum { NOT_PRESENT, ALWAYS_OFF, SOLAR_OFF, ALWAYS_ON, AUTO } EnableC2_t;

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

#ifdef __cplusplus
#define EXTC extern "C"
#else
#define EXTC extern
#endif

#define NUM_ADC_SAMPLES 32

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
#define EM_UNUSED_SLOT1 13
#define EM_UNUSED_SLOT2 14
#define EM_UNUSED_SLOT3 15
#define EM_UNUSED_SLOT4 16
#define EM_CUSTOM 17

EXTC uint8_t Force_Single_Phase_Charging(void);
EXTC EnableC2_t EnableC2;
EXTC void Timer10ms(void * parameter);
EXTC uint8_t Pilot();
EXTC uint8_t pilot;
EXTC int16_t Isum;
#endif

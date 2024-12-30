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
#define SB2_WIFI_MODE 0 

#ifdef __cplusplus
#define EXTC extern "C"
#else
#define EXTC extern
#define bool _Bool
#endif

#define NUM_ADC_SAMPLES 32

// Node specific configuration
#define MENU_ENTER 1
#define MENU_CONFIG 2                                                           // 0x0100: Configuration
#define MENU_LOCK 3                                                             // 0x0101: Cable lock
#define MENU_MIN 4                                                              // 0x0102: MIN Charge Current the EV will accept
#define MENU_MAX 5                                                              // 0x0103: MAX Charge Current for this EVSE
#define MENU_LOADBL 6                                                           // 0x0104: Load Balance
#define MENU_SWITCH 7                                                           // 0x0105: External Start/Stop button
#define MENU_RCMON 8                                                            // 0x0106: Residual Current Monitor
#define MENU_RFIDREADER 9                                                       // 0x0107: Use RFID reader
#define MENU_EVMETER 10                                                         // 0x0108: Type of EV electric meter
#define MENU_EVMETERADDRESS 11                                                  // 0x0109: Address of EV electric meter

// System configuration (same on all SmartEVSE in a LoadBalancing setup)
#define MENU_MODE 12                                                            // 0x0200: EVSE mode
#define MENU_CIRCUIT 13                                                         // 0x0201: EVSE Circuit max Current
#define MENU_GRID 14                                                            // 0x0202: Grid type to which the Sensorbox is connected
#define MENU_SB2_WIFI 15                                                        // 0x0203: WiFi mode of the Sensorbox 2
#define MENU_MAINS 16                                                           // 0x0204: Max Mains Current
#define MENU_START 17                                                           // 0x0205: Surplus energy start Current
#define MENU_STOP 18                                                            // 0x0206: Stop solar charging at 6A after this time
#define MENU_IMPORT 19                                                          // 0x0207: Allow grid power when solar charging
#define MENU_MAINSMETER 20                                                      // 0x0208: Type of Mains electric meter
#define MENU_MAINSMETERADDRESS 21                                               // 0x0209: Address of Mains electric meter
#define MENU_EMCUSTOM_ENDIANESS 22                                              // 0x020D: Byte order of custom electric meter
#define MENU_EMCUSTOM_DATATYPE 23                                               // 0x020E: Data type of custom electric meter
#define MENU_EMCUSTOM_FUNCTION 24                                               // 0x020F: Modbus Function (3/4) of custom electric meter
#define MENU_EMCUSTOM_UREGISTER 25                                              // 0x0210: Register for Voltage (V) of custom electric meter
#define MENU_EMCUSTOM_UDIVISOR 26                                               // 0x0211: Divisor for Voltage (V) of custom electric meter (10^x)
#define MENU_EMCUSTOM_IREGISTER 27                                              // 0x0212: Register for Current (A) of custom electric meter
#define MENU_EMCUSTOM_IDIVISOR 28                                               // 0x0213: Divisor for Current (A) of custom electric meter (10^x)
#define MENU_EMCUSTOM_PREGISTER 29                                              // 0x0214: Register for Power (W) of custom electric meter
#define MENU_EMCUSTOM_PDIVISOR 30                                               // 0x0215: Divisor for Power (W) of custom electric meter (10^x)
#define MENU_EMCUSTOM_EREGISTER 31                                              // 0x0216: Register for Energy (kWh) of custom electric meter
#define MENU_EMCUSTOM_EDIVISOR 32                                               // 0x0217: Divisor for Energy (kWh) of custom electric meter (10^x)
#define MENU_EMCUSTOM_READMAX 33                                                // 0x0218: Maximum register read (ToDo)
#define MENU_WIFI 34                                                            // 0x0219: WiFi mode
#define MENU_AUTOUPDATE 35
#define MENU_C2 36
#define MENU_MAX_TEMP 37
#define MENU_SUMMAINS 38
#define MENU_SUMMAINSTIME 39
#define MENU_OFF 40                                                             // so access bit is reset and charging stops when pressing < button 2 seconds
#define MENU_ON 41                                                              // so access bit is set and charging starts when pressing > button 2 seconds
#define MENU_EXIT 42

#define MENU_STATE 50

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
EXTC uint16_t getItemValue(uint8_t nav);
EXTC EnableC2_t EnableC2;
EXTC uint8_t Pilot();
EXTC uint8_t pilot;
EXTC int16_t Isum;
EXTC uint8_t LoadBl;
EXTC uint8_t setItemValue(uint8_t nav, uint16_t val); //this somehow prevents undefined reference in CH32 compile
EXTC char IsCurrentAvailable(void);
EXTC void receiveNodeStatus(uint8_t *buf, uint8_t NodeNr);
EXTC void receiveNodeConfig(uint8_t *buf, uint8_t NodeNr);
EXTC void requestMeasurement(uint8_t Meter, uint8_t Address, uint16_t Register, uint8_t Count);
EXTC void requestCurrentMeasurement(uint8_t Meter, uint8_t Address);
EXTC void ModbusWriteSingleRequest(uint8_t address, uint16_t reg, uint16_t value);
EXTC void ModbusReadInputRequest(uint8_t address, uint8_t function, uint16_t reg, uint16_t quantity);
EXTC void ModbusWriteMultipleRequest(uint8_t address, uint16_t reg, uint16_t *values, uint8_t count);



#endif

#ifndef __MAIN_C
#define __MAIN_C


//here should be declarations for code that will run on both the CH32 and the ESP32
//THUS it can only be C-code, NO C++ here!!

#define SB2_WIFI_MODE 0 

#define NUM_ADC_SAMPLES 32

#define STATE_A 0                                                               // A Vehicle not connected
#define STATE_B 1                                                               // B Vehicle connected / not ready to accept energy
#define STATE_C 2                                                               // C Vehicle connected / ready to accept energy / ventilation not required
#define STATE_D 3                                                               // D Vehicle connected / ready to accept energy / ventilation required (not implemented)
#define STATE_COMM_B 4                                                          // E State change request A->B (set by node)
#define STATE_COMM_B_OK 5                                                       // F State change A->B OK (set by master)
#define STATE_COMM_C 6                                                          // G State change request B->C (set by node)
#define STATE_COMM_C_OK 7                                                       // H State change B->C OK (set by master)
#define STATE_ACTSTART 8                                                        // I Activation mode in progress
#define STATE_B1 9                                                              // J Vehicle connected / EVSE not ready to deliver energy: no PWM signal
#define STATE_C1 10                                                             // K Vehicle charging / EVSE not ready to deliver energy: no PWM signal (temp state when stopping charge from EVSE)
//#if SMARTEVSE_VERSION == 3 TODO
#define STATE_MODEM_REQUEST 11                                                          // L Vehicle connected / requesting ISO15118 communication, 0% duty
#define STATE_MODEM_WAIT 12                                                          // M Vehicle connected / requesting ISO15118 communication, 5% duty
#define STATE_MODEM_DONE 13                                                // Modem communication succesful, SoCs extracted. Here, re-plug vehicle
#define STATE_MODEM_DENIED 14                                                // Modem access denied based on EVCCID, re-plug vehicle and try again
//#else
//#define STATE_E 11                  // disconnected pilot / powered down
//#define STATE_F 12                  // -12V Fault condition

#define NOSTATE 255

#define NR_EVSES 8
#define BROADCAST_ADR 0x09

extern uint8_t Force_Single_Phase_Charging(void);
extern uint16_t getItemValue(uint8_t nav);
extern uint8_t Pilot();
extern uint8_t pilot;
extern int16_t Isum;
extern uint8_t LoadBl;
extern uint8_t setItemValue(uint8_t nav, uint16_t val); //this somehow prevents undefined reference in CH32 compile
extern char IsCurrentAvailable(void);
extern void receiveNodeStatus(uint8_t *buf, uint8_t NodeNr);
extern void receiveNodeConfig(uint8_t *buf, uint8_t NodeNr);
extern void requestMeasurement(uint8_t Meter, uint8_t Address, uint16_t Register, uint8_t Count);
extern void requestCurrentMeasurement(uint8_t Meter, uint8_t Address);
extern void ModbusWriteSingleRequest(uint8_t address, uint16_t reg, uint16_t value);
extern void ModbusReadInputRequest(uint8_t address, uint8_t function, uint16_t reg, uint16_t quantity);
extern void ModbusWriteMultipleRequest(uint8_t address, uint16_t reg, uint16_t *values, uint8_t count);
extern void SetCurrent(uint16_t current);

extern uint8_t ModbusRx[256];
extern void ReadItemValueResponse(void);
extern void WriteItemValueResponse(void);
extern void WriteMultipleItemValueResponse(void);
extern void ModbusDecode(uint8_t * buf, uint8_t len);
extern void SetCPDuty(uint32_t DutyCycle);

extern uint8_t Initialized;
extern uint8_t PwrPanic;
extern uint8_t ModemPwr;
extern volatile uint8_t RxRdy1;
extern volatile uint8_t ModbusRxLen;

#endif

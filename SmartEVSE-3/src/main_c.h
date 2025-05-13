#ifndef __MAIN_C
#define __MAIN_C


//here should be declarations for code that will run on both the CH32 and the ESP32
//THUS it can only be C-code, NO C++ here!!

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
//#if SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40 TODO
#define STATE_MODEM_REQUEST 11                                                          // L Vehicle connected / requesting ISO15118 communication, 0% duty
#define STATE_MODEM_WAIT 12                                                          // M Vehicle connected / requesting ISO15118 communication, 5% duty
#define STATE_MODEM_DONE 13                                                // Modem communication succesful, SoCs extracted. Here, re-plug vehicle
#define STATE_MODEM_DENIED 14                                                // Modem access denied based on EVCCID, re-plug vehicle and try again
//#else
//#define STATE_E 11                  // disconnected pilot / powered down
//#define STATE_F 12                  // -12V Fault condition

#define NOSTATE 255

#define PWM_5 50                                                                // 5% of PWM
#define PWM_95 950                                                              // 95% of PWM
#define PWM_96 960                                                              // PWM 96%
#define PWM_100 1000                                                            // 100% of PWM


extern uint8_t LoadBl;
extern void SetCurrent(uint16_t current);

extern uint8_t ModbusRx[256];
extern void SetCPDuty(uint32_t DutyCycle);

extern volatile uint8_t RxRdy1;
extern volatile uint8_t ModbusRxLen;

//#if !defined(SMARTEVSE_VERSION) || SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40 //CH32 and v3
// Error flags are base in CH32 or v3 ESP32
#define NO_ERROR 0
#define LESS_6A 1
#define CT_NOCOMM 2
#define TEMP_HIGH 4
#define EV_NOCOMM 8
#define RCM_TRIPPED 16                                                          // RCM tripped. >6mA DC residual current detected.
//#define unused 32
#define Test_IO 64
#define BL_FLASH 128

extern uint8_t ErrorFlags;
extern void clearErrorFlags(uint8_t flags);
//#endif

#endif

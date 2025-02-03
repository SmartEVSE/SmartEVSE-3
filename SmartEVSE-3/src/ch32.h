/*
;    Project: Smart EVSE v4
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

#ifndef __CH32
#define __CH32

#define LOG_DEBUG 3                                                             // Debug messages including measurement data
#define LOG_INFO 2                                                              // Information messages without measurement data
#define LOG_WARN 1                                                              // Warning or error messages
#define LOG_OFF 0

#define LOG_EVSE LOG_INFO                                                       // Default: LOG_INFO
#define LOG_MODBUS LOG_WARN                                                    // Default: LOG_WARN


#define WCH_VERSION 1               // software version (this software)

// GPIO PortA
#define PP_IN       PA0      // Proximity pilot input
#define CP_IN       PA1      // Control pilot input
#define RS485_TX    PA2      // RS485 transmit
#define RS485_RX    PA3      // RS485 receive
#define TEMP        PA4      // Temperature input
#define VCC_EN      PA5      // Enable modem VCC output
#define LEDR        PA6      // RED Led output
#define LEDG        PA7      // GREEN Led output
#define CP_OUT      PA8      // Control pilot out
#define USART_RX    PA9      // transmit to ESP32
#define USART_TX    PA10     // receive from ESP32
#define PA11_UN     PA11     // Unused
#define PA12_UN     PA12     // Unused
#define SWDIO       PA13     // SWDIO to ESP32 (input) / Power Panic output
#define SWCLK       PA14     // SWCLK to ESP32 (input) / Boot0 select
#define CPOFF       PA15     // Disable CP output


// GPIO PortB
#define LEDB        PB0      // BLUE Led output
#define PB1_UN      PB1      // Unused
#define RS485_DIR   PB2      // RS485 direction output
#define LOCK_IN     PB3      // Lock input
#define BUT_SW_IN   PB4      // Led push button input (connector)
#define SW_IN       PB5      // Switch input (12pin plug bottom)
#define RCMTEST     PB6      // RCM test output (active low)
#define PB7_UN      PB7      // Unused
#define ZC          PB8      // Zero Cross input
#define RCMFAULT    PB9      // RCM fault input
#define SSR2        PB10     // SSR2 output
#define SSR1        PB11     // SSR1 output
#define ACTB        PB12     // Actuator/Lock B output
#define ACTA        PB13     // Actuator/Lock A output
#define PB14_UN     PB14     // Unused
#define PB15_UN     PB15     // Unused

// define LOG level
#if LOG_EVSE >= LOG_DEBUG
#define LOG_DEBUG_EVSE
#endif
#if LOG_EVSE >= LOG_INFO
#define LOG_INFO_EVSE
#endif
#if LOG_EVSE >= LOG_WARN
#define LOG_WARN_EVSE
#endif
#if LOG_MODBUS >= LOG_DEBUG
#define LOG_DEBUG_MODBUS
#endif
#if LOG_MODBUS >= LOG_INFO
#define LOG_INFO_MODBUS
#endif
#if LOG_MODBUS >= LOG_WARN
#define LOG_WARN_MODBUS
#endif


#define true 1
#define false 0

#define INITIALIZED 0



// Error flags
#define NO_ERROR 0
#define LESS_6A 1
#define CT_NOCOMM 2
#define TEMP_HIGH 4
#define EV_NOCOMM 8
#define RCM_TRIPPED 16                                                          // RCM tripped. >6mA DC residual current detected.
#define NO_SUN 32
#define Test_IO 64
#define BL_FLASH 128

#define PWM_5       50              // PWM 5%
#define PWM_96      960             // PWM 96%

void delayMicroseconds(uint32_t us);
uint32_t millis();

#endif

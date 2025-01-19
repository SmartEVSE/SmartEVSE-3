/*
;    Project:       Smart EVSE v4
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
#ifndef SMARTEVSE_VERSION //CH32
#include "ch32v003fun.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "ch32.h"
#include "main.h"
#include "utils.h"


extern "C"{
    void setup();
    void setState(uint8_t NewState);
}


extern "C" uint32_t elapsedmax, elapsedtime;
extern "C" uint16_t MainsCycleTime;
extern "C" uint8_t Lock, LockCable, PowerPanicFlag;
uint32_t SysTimer10ms, SysTimer100ms, SysTimer1s;

extern void Timer10ms_singlerun(void);
extern void Timer100ms_singlerun(void);
extern void Timer1S_singlerun(void);
extern "C" void delay(uint32_t ms);

// PowerLoss Detected, shutdown
// - Powerdown ESP32 and QCA modem
// - Unlock Charging cable (if locked)
// - LEDs off
//
void PowerPanic(void) {
    // Modem power Off
    funDigitalWrite(VCC_EN, FUN_LOW);
  //USART_SendData(USART2, 'Z');
    // LEDs Off
    TIM3->CH1CVR = 0;
    TIM3->CH2CVR = 0;
    TIM3->CH3CVR = 0;

    funDigitalWrite(CPOFF, FUN_LOW);
    funDigitalWrite(RS485_DIR, FUN_LOW);
    funDigitalWrite(RCMTEST, FUN_HIGH);    
    funDigitalWrite(SSR1, FUN_LOW);
    funDigitalWrite(SSR2, FUN_LOW);

    TIM1->CTLR1 &= ~TIM_CEN;                    // Disable PWM output

    // Reset MainsCycleTime
    MainsCycleTime = 0;

    // Signal the ESP32 that it has to sleep
    printf("!Panic\n");

    // Unlock cable if used, and cable locked
    if (Lock && LockCable) {
        funDigitalWrite(ACTA, FUN_HIGH);        // Unlock
        funDigitalWrite(ACTB, FUN_LOW);
        delay(600);
        funDigitalWrite(ACTA, FUN_LOW);         // Both outputs LOW
    }


    TIM4->INTFR = 0;
    // Wait for power to be restored
    while (MainsCycleTime < 15000) {
       delay(100);
    }
    printf("Power restored\n");

    // Toggle SWDIO to signal the ESP32 that power is back
    // First reconfigure the SWDIO pin for I/O

    RCC->APB2PRSTR |= RCC_APB2Periph_AFIO;      // Reset AFIO
    RCC->APB2PRSTR &= ~RCC_APB2Periph_AFIO;
    AFIO->PCFR1 |= AFIO_PCFR1_SWJ_CFG_DISABLE;  // Disable SDDIO/SWCLK pins, and enable GPIO

    funDigitalWrite(SWDIO, FUN_LOW);            // Set the SWDIO/PA13 pin Low
    delay(10);
    funDigitalWrite(SWDIO, FUN_HIGH);           // And back to open drain again.

    AFIO->PCFR1 &= ~AFIO_PCFR1_SWJ_CFG_DISABLE; // Re-enable SDDIO/SWCLK pins

    setState(STATE_A);

    PowerPanicFlag = 0;
    TIM1->CTLR1 |= TIM_CEN;        // Re-enable PWM output
    TIM4->CNT = 0;
    TIM4->INTFR = 0;

}


/*********************************************************************
 * Main program.
 *
 *
 *
 */

int main(void)
{
    setup();
    // After (re)boot, first request configuration from ESP.
    SysTimer10ms  = (uint32_t)SysTick->CNT;     // load with SysTick
    SysTimer100ms = (uint32_t)SysTick->CNT;
    SysTimer1s    = (uint32_t)SysTick->CNT;

    // main loop
    while(1)
    {
        // Power lost! Unlock charging cable, notify ESP32.
        if (PowerPanicFlag) {
            PowerPanic();
        }
        // Handle 3 Timer functions
        if ((uint32_t)SysTick->CNT - SysTimer10ms > (FUNCONF_SYSTEM_CORE_CLOCK / 800) ) { // compare durations
            SysTimer10ms = (uint32_t)SysTick->CNT; //reset 10ms Timer
            Timer10ms_singlerun();       // handle tasks every 10ms

            elapsedtime = (uint32_t)SysTick->CNT - SysTimer10ms;
            if (elapsedtime> elapsedmax) elapsedmax = elapsedtime;

        }
        if ((uint32_t)SysTick->CNT - SysTimer100ms > (FUNCONF_SYSTEM_CORE_CLOCK / 80) ) {
            SysTimer100ms = (uint32_t)SysTick->CNT; //reset 100ms Timer

            Timer100ms_singlerun();       // handle tasks every 100ms
        }
        if ((uint32_t)SysTick->CNT - SysTimer1s > (FUNCONF_SYSTEM_CORE_CLOCK / 8) ) {
            SysTimer1s = (uint32_t)SysTick->CNT; //reset 1s Timer

            Timer1S_singlerun();          // handle tasks every second
        }
    } // while(1) loop
}


// Delay in microseconds
// We don't reset SysTick counter, but instead set the SysTick compare register
void delayMicroseconds(uint32_t us)
{
    uint32_t i;

    // Clear Status register
    SysTick->SR &= ~(1 << 0);
    // SystemCoreClock / 8000000 = 1 uS (12 cycles @ 96Mhz)
    i = (uint32_t)us * (FUNCONF_SYSTEM_CORE_CLOCK / 8000000);
    // Set compare register to current counter value + delay
    SysTick->CMP = SysTick->CNT + i;
    // Wait (blocking) for flag to be set
    while((SysTick->SR & 0x01) == 0);
}



// returns elapsed milliseconds since powerup
// will overflow in ~50 days
uint32_t millis()
{
   return (uint32_t)(SysTick->CNT / (FUNCONF_SYSTEM_CORE_CLOCK / 8000));
}
#endif

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
#include "evse.h"

extern uint8_t State;

uint8_t LockCable = 0;

uint32_t elapsedtime, elapsedmax=0;

// the following variables are used in interrupts
//
volatile uint16_t ADC_CP[NUM_ADC_SAMPLES];      // CP pin samples
volatile uint16_t ADC_PP[NUM_ADC_SAMPLES];      // PP pin samples
volatile uint16_t ADC_Temp[NUM_ADC_SAMPLES];    // Temperature samples
volatile uint8_t ADCidx = 0;                    // index in sample buffers
volatile uint16_t MainsCycleTime = 0;           // mains cycle time (20ms for 50Hz) Convert to Hz : 10000 / (MainsCycleTime/100))
volatile uint8_t PowerPanicFlag = 0;

uint8_t RxBuffer2[256];                         // USART2 Receive buffer

volatile uint8_t RxRdy1 = 0;
volatile uint8_t RxIdx2 = 0;
volatile uint8_t ModbusRxLen = 0;
//volatile uint32_t ModbusTimer = 0;
volatile uint8_t DmaBusy = 0;

// Circular buffers for USART1 and TX of USART2
CircularBuffer RxBuffer = {{0}, 0, 0};          // USART1 Receive buffer ESP->WCH
CircularBuffer TxBuffer = {{0}, 0, 0};          // USART1 Transmit ringbuffer WCH->ESP (DMA)
CircularBuffer ModbusTx = {{0}, 0, 0};          // USART2 Transmit buffer (modbus)


// -------------------------- Interrupt Handlers ---------------------------------

void ADC1_2_IRQHandler(void) __attribute__((interrupt));
void ADC1_2_IRQHandler()
{
    if(ADC1->STATR & ADC_FLAG_JEOC) {
        ADC1->STATR &= ~ADC_FLAG_JEOC;          // Clear Flag

        ADC_CP[ADCidx] = ADC1->IDATAR1;
        ADC_PP[ADCidx] = ADC1->IDATAR2;
        ADC_Temp[ADCidx++] = ADC1->IDATAR3;
        if (ADCidx == NUM_ADC_SAMPLES) ADCidx = 0;

        //printf("@MSG: ADC ISR! in=%d\r\n", ADC_CP[0]);
    }
}


void DMA1_Channel4_IRQHandler(void) __attribute__((interrupt));
void DMA1_Channel4_IRQHandler()
{
    if (DMA1->INTFR & DMA_TCIF4) {              // Check if transfer is complete
        DMA1->INTFCR |= DMA_CTCIF4;             // Clear transfer complete flag

        DMA1_Channel4->CFGR &= ~DMA_CFG4_EN;    // Disable DMA channel
        DmaBusy = 0;                            // Flag DMA ready for more data

        if (TxBuffer.head != TxBuffer.tail) {   // Check if more data needs to be sent
            uart_start_dma_transfer();
        }
    }
}


void TIM2_IRQHandler(void) __attribute__((interrupt));
void TIM2_IRQHandler()
{
    if (TIM2->INTFR & TIM_UIF) {            // Check if update interrupt flag is set
        TIM2->INTFR &= ~TIM_UIF;            // Clear update interrupt flag

        if (RxIdx2) {
                                            // Copy data in Uart receive buffer to modbus receive buffer
            memcpy(ModbusRx, RxBuffer2, RxIdx2);
            ModbusRxLen = RxIdx2;           // Flag to main loop that we received modbus data
            RxIdx2 = 0;
        }
        TIM2->CTLR1 &= ~TIM_CEN;            // Disable Timer 2
                                            // Will be re-enabled after we receive modbus data
    }

}


void TIM4_IRQHandler(void) __attribute__((interrupt));
void TIM4_IRQHandler()
{
    TIM4->CNT = 0;                          // Reset Counter

    if(TIM4->INTFR & TIM_FLAG_CC3) {        // Capture flag
        TIM4->INTFR &= ~TIM_FLAG_CC3;
        MainsCycleTime = TIM4->CH3CVR;      // Store the Cycle time of the Mains
    }

    if(TIM4->INTFR & TIM_FLAG_CC3OF) {      // Overcapture flag
        TIM4->INTFR &= ~TIM_FLAG_CC3OF;
    }
    if(TIM4->INTFR & TIM_FLAG_Update) {     // Update flag
        TIM4->INTFR &= ~TIM_FLAG_Update;

        // Counter CNT counted to 65536 = 65 mS and overflowed.
        // At 50Hz line frequency we normally expect 3 cycles in 65mS (1 cycle = 20mS)
        // So we most likely lost power. As there is still power left in the PSU and 10mF cap,
        // we shutdown the ESP32 and QCA modem, and unlock the charging cable (if locked)

        PowerPanicFlag = 1;
    }
}

// Residual current monitor fault trigger.
//
void EXTI9_5_IRQHandler(void) __attribute__((interrupt));
void EXTI9_5_IRQHandler()
{
    printf("@MSG: EXTI 9 Interrupt\n");
    delay(1);
    // check again, to prevent voltage spikes from tripping the RCM detection
    if (funDigitalRead(RCMFAULT) == FUN_HIGH ) {
        if (State) setState(STATE_B1);
        setErrorFlags(RCM_TRIPPED);
    }

    EXTI->INTFR = 0x1ffffff;                        // clear interrupt flag register
}

// Serial comm interrupt handler between WCH and ESP
// FUNCONF_UART_PRINTF_BAUD bps
void USART1_IRQHandler(void) __attribute__((interrupt));
void USART1_IRQHandler()
{
    uint8_t data;

    // Receive interrupt
    if (USART1->STATR & USART_FLAG_RXNE) {
        data = (uint8_t)USART1->DATAR;                  // read data

        if (!buffer_enqueue(&RxBuffer, data)) {         // Store the data in the RxBuffer
            // TODO: handle buffer full
        }
        if (data == '\n') RxRdy1 = 1;                   // flag data ready
    }

    // Transmit interrupt
  /*  if (USART1->STATR & USART_FLAG_TXE) {
        // get data from the queue
        if (buffer_dequeue(&TxBuffer, &data)) {
            // Send the next byte
            USART1->DATAR = data;
        } else {
            // Disable the TXE interrupt if no more data
            USART1->CTLR1 &= ~USART_CTLR1_TXEIE;
        }
    }
   */
    USART1->STATR &= ~USART_FLAG_ORE;                   // Clear possible overrun flag
}


// Serial comm interrupt handler RS485, also handle modbus t1.5 and t3.5 timeouts
// 9600 bps
void USART2_IRQHandler(void) __attribute__((interrupt));
void USART2_IRQHandler()
{
    char data;

    // Receive interrupt
    if (USART2->STATR & USART_FLAG_RXNE) {

        if (TIM2->CNT > 1500) RxIdx2 = 0;               // if time between characters is more then 1.5ms, we'll flush the buffer.

        TIM2->CNT = 0;                                  // Reset modbus t3.5 timer
        TIM2->CTLR1 |= TIM_CEN;                         // (re)Enable Update interrupt, called when no reception for 3.5ms

        if(RxIdx2 == 255) RxIdx2--;                     // Do not wrap around when buffer is full.
        data = (uint8_t)USART2->DATAR;                  // read data
        RxBuffer2[RxIdx2++] = data;

    }

    // Transmission complete interrupt
    if (USART2->STATR & USART_FLAG_TC) {

        USART2->STATR &= ~USART_FLAG_TC;                // clear Transmission complete flag
        funDigitalWrite(RS485_DIR, FUN_LOW);            // switch RS485 transceiver back to receive
    }
    // Transmit interrupt
    else if (USART2->STATR & USART_FLAG_TXE) {
        if (buffer_dequeue(&ModbusTx, &data)) {
            USART2->DATAR = data;                       // Send the next byte
        } else {
            USART2->CTLR1 &= ~USART_CTLR1_TXEIE;        // Disable the TXE interrupt if no more data
        }
    }

    USART2->STATR &= ~USART_FLAG_ORE;                   // Clear possible overrun flag
}


// --------------------------- END of ISR's -------------------------------------------
/*
// Function to check if the buffer is full
uint8_t buffer_full(CircularBuffer *cb) {
    //return (((cb->head + 1) % sizeof(cb->buffer)) == cb->tail);
    return (((cb->head + 1) & 0xff ) == cb->tail);
}
*/

// Function to add an element to the buffer
uint8_t buffer_enqueue(CircularBuffer *cb, char data) {
    if ( ((cb->head + 1) & (CIRCULARBUFFER - 1) ) == cb->tail ) {
        return 0; // Buffer is full
    }
    cb->buffer[cb->head] = data;
    cb->head = (cb->head + 1) & (CIRCULARBUFFER - 1);
    return 1;
}


// Function to remove an element from the buffer
uint8_t buffer_dequeue(CircularBuffer *cb, char *data) {
    if (cb->head == cb->tail) {
        return 0; // Buffer is empty
    }
    *data = cb->buffer[cb->tail];
    cb->tail = (cb->tail + 1) & (CIRCULARBUFFER - 1);
    return 1;
}


void PowerPanicCtrl(uint8_t enable)
{
    if (enable) {
        TIM4->CTLR1 |= TIM_CEN |TIM_ARPE;               // Enable TIM1 / PowerPanic
    } else {
        TIM4->CTLR1 &= ~(TIM_CEN |TIM_ARPE);            // Disable TIM1 / PowerPanic
    }
}


void RCmonCtrl(uint8_t enable)
{
    if (enable) {
        EXTI->INTENR |= EXTI_Line9;                     // Enable EXT9
    } else {
        EXTI->INTENR &= ~EXTI_Line9;                    // Disable EXT9
    }
}

void ModemPower(uint8_t enable)
{
    if (enable) {
        funDigitalWrite(VCC_EN, FUN_HIGH);              // Modem power ON
    } else {
        funDigitalWrite(VCC_EN, FUN_LOW);               // Modem power OFF
    }
}


// test RCMON
// enable test signal to RCM14-03 sensor. Should trigger the fault output
void testRCMON(void) {
    setErrorFlags(RCM_TEST);
    funDigitalWrite(RCMTEST, FUN_LOW);
    delay(100);
    funDigitalWrite(RCMTEST, FUN_HIGH);
}


//============================ Peripheral Init Functions ==============================
//



void GPIOInit(void)
{
    // Enable APB1 peripheral clocks
    RCC->APB1PCENR |= RCC_APB1Periph_USART2 | RCC_APB1Periph_TIM2 | RCC_APB1Periph_TIM3 | RCC_APB1Periph_TIM4;
    // Enable APB2 peripheral clocks
    RCC->APB2PCENR |= RCC_APB2Periph_USART1 | RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_TIM1 | RCC_APB2Periph_ADC1;
    // Enable DMA clock
    RCC->AHBPCENR |= RCC_AHBPeriph_DMA1;


    // Port A
    funPinMode(PP_IN, GPIO_CFGLR_IN_ANALOG);
    funPinMode(CP_IN, GPIO_CFGLR_IN_ANALOG);
    funPinMode(RS485_TX, GPIO_CFGLR_OUT_10Mhz_AF_PP);
    funPinMode(RS485_RX, GPIO_CFGLR_IN_FLOAT);
    funPinMode(TEMP, GPIO_CFGLR_IN_ANALOG);
    funPinMode(CP_OUT, GPIO_CFGLR_OUT_10Mhz_AF_PP);
    funPinMode(USART_RX, GPIO_CFGLR_OUT_10Mhz_AF_PP);       // Output to ESP
    funPinMode(USART_TX, GPIO_CFGLR_IN_FLOAT);              // Input from ESP
    funPinMode(VCC_EN, GPIO_CFGLR_OUT_2Mhz_PP);
    funPinMode(CPOFF, GPIO_CFGLR_OUT_2Mhz_PP);
    funPinMode(LEDR, GPIO_CFGLR_OUT_10Mhz_AF_PP);
    funPinMode(LEDG, GPIO_CFGLR_OUT_10Mhz_AF_PP);
    funPinMode(SWDIO, GPIO_CFGLR_OUT_2Mhz_OD);

    // Port B
    funPinMode(LEDB, GPIO_CFGLR_OUT_10Mhz_AF_PP);
    funPinMode(RS485_DIR, GPIO_CFGLR_OUT_10Mhz_PP);
    funPinMode(RCMTEST, GPIO_CFGLR_OUT_2Mhz_PP);
    funPinMode(SSR1, GPIO_CFGLR_OUT_2Mhz_PP);
    funPinMode(SSR2, GPIO_CFGLR_OUT_2Mhz_PP);
    funPinMode(ACTA, GPIO_CFGLR_OUT_2Mhz_PP);
    funPinMode(ACTB, GPIO_CFGLR_OUT_2Mhz_PP);
    funPinMode(ZC, GPIO_CFGLR_IN_FLOAT);
    funPinMode(RCMFAULT, GPIO_CFGLR_IN_PUPD);            // pull up
    funPinMode(SW_IN, GPIO_CFGLR_IN_PUPD);                // pull up
    funPinMode(LOCK_IN, GPIO_CFGLR_IN_FLOAT);
    GPIOB->OUTDR |= 0x0220;                             // Enable pull up on PB5 and PB9

}


void UsartInit(void)
{
    RCC->APB2PRSTR |= RCC_APB2Periph_USART1;
    RCC->APB2PRSTR &= ~RCC_APB2Periph_USART1;

    USART1->BRR = FUNCONF_SYSTEM_CORE_CLOCK / FUNCONF_UART_PRINTF_BAUD;             // USART1 Serial comm between ESP32 and WCH @ FUNCONF_UART_PRINTF_BAUDbps
    // Enable Uart1, TX, RX and Receive interrupt
    USART1->CTLR1 = USART_CTLR1_UE  | USART_CTLR1_TE | USART_CTLR1_RE | USART_CTLR1_RXNEIE;// | USART_CTLR1_TXEIE;

    // Enable Uart1 DMA transmitter
    USART1->CTLR3 |= USART_CTLR3_DMAT;

    // Enable interrupts for USART1
    NVIC_EnableIRQ(USART1_IRQn);

    RCC->APB1PRSTR |= RCC_APB1Periph_USART2;
    RCC->APB1PRSTR &= ~RCC_APB1Periph_USART2;

    USART2->BRR = FUNCONF_SYSTEM_CORE_CLOCK / 9600 / 2;           // USART2 9600bps RS485
    // Enable Uart2, TX, RX, Receive and Transmission Complete interrupt
    USART2->CTLR1 = USART_CTLR1_UE  | USART_CTLR1_TE | USART_CTLR1_RE | USART_CTLR1_RXNEIE | USART_CTLR1_TCIE;

    // Enable interrupts for USART2
    NVIC_EnableIRQ(USART2_IRQn);

}


void DMAInit(void)
{
    // DMA controller has no peripheral reset bits

    // Memory increment and read from memory. Enable transfer complete interrupt
    DMA1_Channel4->CFGR = DMA_CFG4_MINC | DMA_CFG4_DIR |DMA_CFG4_TCIE;

    // Set peripheral address (USART1 data register)
    DMA1_Channel4->PADDR = (uint32_t)&USART1->DATAR;

    // Enable DMA1 Channel4 interrupt
    NVIC_EnableIRQ(DMA1_Channel4_IRQn);

}


void TIM1Init( void )
{
    // Reset TIM1 to init all regs
    RCC->APB2PRSTR |= RCC_APB2Periph_TIM1;
    RCC->APB2PRSTR &= ~RCC_APB2Periph_TIM1;

    // Prescaler (96Mhz/(95+1) = 1Mhz)
    TIM1->PSC = (FUNCONF_SYSTEM_CORE_CLOCK / 1000000) - 1;
    // Set period (Auto Reload)
    TIM1->ATRLR = 1000-1;
    // Reload immediately
    TIM1->SWEVGR |= TIM_UG;

    // CH1 Mode is output, PWM mode 2, Preload enable
    TIM1->CHCTLR1 |= TIM_OC1M_2 | TIM_OC1M_1 | TIM_OC1M_0 | TIM_OC1PE;
    // CH4 Mode is output (no actual output on pin!), PWM mode 2, Preload enable
    TIM1->CHCTLR2 |= TIM_OC4M_2 | TIM_OC4M_1 | TIM_OC4M_0 | TIM_OC4PE;
    // Set TRGO signal to trigger on compare reg4
    TIM1->CTLR2 = TIM_MMS_0 | TIM_MMS_1| TIM_MMS_2;

    // Set the CP Duty Cycle value to 10% initially
    TIM1->CH1CVR = 100;
    // CH sets the ADC sample start at 5% (and 96%)
    TIM1->CH4CVR = PWM_5;

    // CH1 positive pol, output enabled
    TIM1->CCER |= TIM_CC1E | TIM_CC1P;
    // CH4 positive pol, output disabled
    TIM1->CCER |= (TIM_CC4E & 0) | TIM_CC4P;

    // Enable TIM1 outputs
    TIM1->BDTR |= TIM_MOE;
    // Enable TIM1
    TIM1->CTLR1 |= TIM_CEN |TIM_ARPE;

}


// Timer used for Modbus timeout
void TIM2Init( void )
{
    // Reset TIM2 to init all regs
    RCC->APB1PRSTR |= RCC_APB1Periph_TIM2;
    RCC->APB1PRSTR &= ~RCC_APB1Periph_TIM2;

    // Prescaler (96Mhz/(95+1) = 1Mhz)
    TIM2->PSC = (FUNCONF_SYSTEM_CORE_CLOCK / 1000000) - 1;
    // Set period (Auto Reload) to 3500us = 3.5ms
    TIM2->ATRLR = 3500-1;
    // Reload immediately
    TIM2->SWEVGR |= TIM_UG;

    // Update interrupt enable
    TIM2->DMAINTENR |= TIM_UIE;

    TIM2->CTLR1 |= TIM_ARPE | TIM_URS;

    // Clear Counter and interrupt flags before enabling Interrupts
    TIM2->CNT = 0;
    TIM2->INTFR = 0;
    NVIC_EnableIRQ(TIM2_IRQn);

    // Enable TIM2
    TIM2->CTLR1 |= TIM_CEN;

}


void TIM3Init( void )
{
    // Reset TIM3 to init all regs
    RCC->APB1PRSTR |= RCC_APB1Periph_TIM3;
    RCC->APB1PRSTR &= ~RCC_APB1Periph_TIM3;

    // Prescaler (96Mhz/(95+1) = 1Mhz)
    TIM3->PSC = (FUNCONF_SYSTEM_CORE_CLOCK / 1000000) - 1;
    // Set period (Auto Reload) to 3906 Hz
    TIM3->ATRLR = 256-1;
    // Reload immediately
    TIM3->SWEVGR |= TIM_UG;

    // CH1 Mode is output, PWM mode 2, Preload enable
    TIM3->CHCTLR1 |= TIM_OC1M_2 | TIM_OC1M_1 | TIM_OC1M_0 | TIM_OC1PE;
    // CH2 Mode is output, PWM mode 2, Preload enable
    TIM3->CHCTLR1 |= TIM_OC2M_2 | TIM_OC2M_1 | TIM_OC2M_0 | TIM_OC2PE;
    // CH3 Mode is output, PWM mode 2, Preload enable
    TIM3->CHCTLR2 |= TIM_OC3M_2 | TIM_OC3M_1 | TIM_OC3M_0 | TIM_OC3PE;

    // Set the Capture Compare Register (Duty Cycle) value
    TIM3->CH1CVR = 0;       // Red Channel off
    TIM3->CH2CVR = 0;       // Green Channel off
    TIM3->CH3CVR = 0;       // Blue Channel off

    // CH1, CH2, CH3 positive pol, output enabled
    TIM3->CCER |= TIM_CC1E | TIM_CC1P | TIM_CC2E | TIM_CC2P | TIM_CC3E | TIM_CC3P;

    // Enable TIM1
    TIM3->CTLR1 |= TIM_CEN |TIM_ARPE;

}

// Timer 4 is set up to monitor the ZC(CH3) input.
// On each mains cycle the Timer 4 interrupt handler is called
// It's also called when the counter overflows (loss of mains)
void TIM4Init( void )
{
    // Reset TIM4 to init all regs
    RCC->APB1PRSTR |= RCC_APB1Periph_TIM4;
    RCC->APB1PRSTR &= ~RCC_APB1Periph_TIM4;

    // Prescaler (96Mhz/(95+1) = 1Mhz)
    TIM4->PSC = (FUNCONF_SYSTEM_CORE_CLOCK / 1000000) - 1;
    // Set period to 65 mS
    TIM4->ATRLR = 65535;
    // Reload immediately
    TIM4->SWEVGR |= TIM_UG;

    // CH3 Mode is input on TI3
    TIM4->CHCTLR2 |= TIM_CC3S_0;

    // capture on rising edge (TIM_CC3P = 0)
    // capture enable TIM_CC3E = 1
    TIM4->CCER |= TIM_CC3E;

    // CC3IE interrupt enable
    // Trigger interrupt enable (external valid edge detected)
    // Update interrupt enable (counter overflow)
    TIM4->DMAINTENR |= TIM_CC3IE | (TIM_TIE & 0) | TIM_UIE;

    // Clear Counter and interrupt flags before enabling Interrupts
    TIM4->CNT = 0;
    TIM4->INTFR = 0;
    NVIC_EnableIRQ(TIM4_IRQn);

    // Enable TIM4
    TIM4->CTLR1 |= TIM_CEN |TIM_ARPE;

}


// External interrupt on the PB9 / RCMFAULT input
//
void EXTInit( void )
{
    AFIO->EXTICR[2] |= 1<<4;                        // EXTICR3 register. EXTI9 Port B
    EXTI->INTENR |= EXTI_Line9;                     // Enable EXT9
    EXTI->RTENR |= EXTI_Line9;                      // Rising Edge on RB9
    EXTI->INTFR = 0x1ffffff;                        // clear interrupt flag register

    NVIC_EnableIRQ(EXTI9_5_IRQn);                   // enable interrupt

}


/*
 * initialize ADC
 */
void ADCInit( void )
{
    // ADCCLK = 12 MHz => RCC_ADCPRE = 3. Divide 96MHz by 8
    RCC->CFGR0 |= RCC_PCLK2_Div8; // (0x3 << 14);

    // Reset the ADC to init all regs
    RCC->APB2PRSTR |= RCC_APB2Periph_ADC1;
    RCC->APB2PRSTR &= ~RCC_APB2Periph_ADC1;

    // Set up 3 conversions 1st PA1(CP), 2nd PA0(PP), 3rd PA4(Temp)
    // JL[21:20]= 10-> 3 conversions
    // JSQ2: PA1(CP), JSQ3: PA0(PP), JSQ4: PA4(Temp)
    ADC1->ISQR |= ADC_JL_1 | ADC_JSQ2_0 | ADC_JSQ4_2;
    //ADC1->ISQR |= ADC_JL_1 | ADC_JSQ2_0 | ADC_JSQ3_2;

    // set sampling time for all channels to 6=>71.5 cycles
    // 0:7 => 1.5/7.5/13.5/28.5/41.5/55.5/71.5/239.5 cycles
    // @ 12Mhz each conversion takes 5,95 uS
    ADC1->SAMPTR2 |= ADC_SampleTime_71Cycles5 << (3*ADC_Channel_4) |
                     ADC_SampleTime_71Cycles5 << (3*ADC_Channel_1) |
                     ADC_SampleTime_71Cycles5 << (3*ADC_Channel_0);

    // Enable Scan mode to convert all selected channels
    // Enable the INPUT signal buffer
    ADC1->CTLR1 |= ADC_SCAN | ADC_OutputBuffer_Enable;

    // turn on ADC
    ADC1->CTLR2 |= ADC_ADON;

    // Reset calibration
    ADC1->CTLR2 |= ADC_RSTCAL;
    while(ADC1->CTLR2 & ADC_RSTCAL);
    // Calibrate
    ADC1->CTLR2 |= ADC_CAL;
    while(ADC1->CTLR2 & ADC_CAL);

    // turn on ADC and set rule group to TRGO event triggering
    ADC1->CTLR2 = ADC_ADON | ADC_JEXTTRIG;
    // should be ready for conversion now

    // Enable Interrupt on Injected channels
    ADC1->STATR = 0;
    ADC1->CTLR1 |= ADC_JEOCIE;
    NVIC_EnableIRQ(ADC_IRQn);

}

// ------------------------------------- END of INIT functions -------------------------------------

// Read the Temperature sensor data.
// Range -50 - +125 C
//
int8_t TemperatureSensor() {
    uint32_t TempAvg = 0;
    uint8_t n;
    int8_t Temperature;
    static int8_t Old_Temperature = 255;

    for(n=0; n<NUM_ADC_SAMPLES; n++) TempAvg += ADC_Temp[n];
    TempAvg = TempAvg / NUM_ADC_SAMPLES ;


    // The MCP9700A temperature sensor outputs 500mV at 0C, and has a 10mV/C change in output voltage.
    // 750mV is 25C, 400mV = -10C
    // 3.3V / 4096(12bit ADC) = ~ 800uV/step. Convert measurement to mV*10 by multiplying by 8
    // Subtract 500mV offset, and finally divide by 100 to convert to C.
    Temperature = (int16_t)((TempAvg *8)- 5000)/100;
    if (Temperature != Old_Temperature) {
        printf("@Temp:%u\n", Temperature); //send data to ESP32
        Old_Temperature = Temperature;
    }
    return Temperature;
}


// Read the Proximity Pin data, and determine the maximum current the cable can handle.
//
uint8_t ProximityPin() {
    uint32_t PPAvg = 0;
    uint8_t n, MaxCap;

    for(n=0; n<NUM_ADC_SAMPLES; n++) PPAvg += ADC_PP[n];
    PPAvg = PPAvg / NUM_ADC_SAMPLES ;


    printf("@MSG: PP pin: %u \n", (uint16_t)PPAvg);
    MaxCap = 13;                                                   // No resistor, Max cable current = 13A
    if ((PPAvg > 1300) && (PPAvg < 1800)) MaxCap = 16;             // Max cable current = 16A  680R -> should be around 1.3V
    if ((PPAvg > 600) && (PPAvg < 900)) MaxCap = 32;               // Max cable current = 32A  220R -> should be around 0.6V
    if ((PPAvg > 200) && (PPAvg < 500)) MaxCap = 63;               // Max cable current = 63A  100R -> should be around 0.3V

    return MaxCap;
}


// Copy data to circular buffer
// return nr of bytes written
// Note! Can't use printf here, as it's used by write_
int buffer_write(CircularBuffer *cb, char *data, uint16_t size)
{
    uint16_t i;
    for (i = 0; i < size; i++) {
        if(!buffer_enqueue(cb, data[i])) return 0;      // Buffer full?
    }

    return i; // Number of bytes written
}

// Called by _write, putchar and DMA ISR
void uart_start_dma_transfer(void)
{
    if (DmaBusy == 0 && (DMA1_Channel4->CFGR & DMA_CFG4_EN) == 0 && TxBuffer.head != TxBuffer.tail) {

        // Prevent calls from the DMA ISR and regular calls to this function from interfering with each other.
        // This flag can not be moved to the end of this function!
        DmaBusy = 1;

        // Set memory address
        DMA1_Channel4->MADDR = (uint32_t)(&TxBuffer.buffer[TxBuffer.tail]);
        if (TxBuffer.head > TxBuffer.tail) {
            // Linear segment, no wrap-around
            DMA1_Channel4->CNTR = TxBuffer.head - TxBuffer.tail;        // Set number of bytes to transfer
        } else {
            // Wrap-around segment
            DMA1_Channel4->CNTR = CIRCULARBUFFER - TxBuffer.tail;       // Set number of bytes to transfer
        }

        TxBuffer.tail = (TxBuffer.tail + DMA1_Channel4->CNTR) & (CIRCULARBUFFER-1);   // Update tail for next potential transfer
        DMA1_Channel4->CFGR |= DMA_CFG4_EN;                             // Enable DMA channel

    }
}



// Used by printf as std output
//
int _write(int fd, const char *buffer, int size)
{
    NVIC_DisableIRQ(DMA1_Channel4_IRQn);                // Disable DMA interrupt during buffer write

    int ret = buffer_write(&TxBuffer, (char *) buffer, size);

    NVIC_EnableIRQ(DMA1_Channel4_IRQn);                 // Re-enable interrupt before starting DMA

    if (ret) uart_start_dma_transfer();
    return ret;
}

// used by printf when only one character is sent
//
int putchar(int c)
{
    NVIC_DisableIRQ(DMA1_Channel4_IRQn);
    
    int ret = buffer_enqueue(&TxBuffer, c);
    
    NVIC_EnableIRQ(DMA1_Channel4_IRQn);
    
    if (ret) uart_start_dma_transfer();
    return ret;
}


uint8_t ReadESPdata(char *buf) {
    uint8_t i = 0;
    while (buffer_dequeue(&RxBuffer, &buf[i])) {
        i++;
    }
    return i; // Return the number of bytes read
}


void setup(void) {
    SystemInit();
//  NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
//  SystemCoreClockUpdate();

    SysTick->CTLR = 1;                              // Enable SysTick counter HCLK/8, count up

    GPIOInit();
    UsartInit();                                    // Usart1 = FUNCONF_UART_PRINTF_BAUD bps. Usart2 = Modbus 9600bps 8N1
    DMAInit();                                      // DMA transfer for Uart1 TX

    // Note that printf will only actually send data to the uart, when it detects a newline, or after a timeout
    //

    printf("@MSG: SmartEVSE mainboard startup\n");
    printf("@MSG: SystemClk:%d\n",FUNCONF_SYSTEM_CORE_CLOCK);
    //printf("@MSG: ChipID:%08x\n", DBGMCU_GetCHIPID() );
    //printf("@MSG: UID:%08x%04x\n", *( uint32_t * )0x1FFFF7E8 , (*( uint32_t * )0x1FFFF7EC)>>16 );

    funDigitalWrite(VCC_EN, FUN_LOW);               // Modem power control OFF
    funDigitalWrite(CPOFF, FUN_LOW);                // CP enabled
    funDigitalWrite(RS485_DIR, FUN_LOW);
    funDigitalWrite(RCMTEST, FUN_HIGH);             // RCM_TEST signal OFF (small current draw ~2mA)
    funDigitalWrite(SSR1, FUN_LOW);                 // Contactor 1 OFF
    funDigitalWrite(SSR2, FUN_LOW);                 // Contactor 2 OFF
    funDigitalWrite(ACTA, FUN_HIGH);                // Actuator output R at 12V
    funDigitalWrite(ACTB, FUN_HIGH);                // Actuator output W at 12V
    funDigitalWrite(SWDIO, FUN_HIGH);               // SWDIO High (pull up) unused

    EXTInit();                                      // Interrupt on RCMFAULT pin
    ADCInit();                                      // CP, PP and Temp inputs
    TIM1Init();                                     // Timebase for CP (PWM)signal and CP/PP/Temp ADC reading (1kHz)
    TIM2Init();                                     // Modbus t3.5 timeout timer, calls ISR after 3.5ms of silence on the bus
    TIM3Init();                                     // LED PWM ~4Khz
    TIM4Init();                                     // ZC input monitoring 50Hz

    ModemPower(1);
    RCmonCtrl(DISABLE);
    PowerPanicCtrl(DISABLE);
}


// Delay in milliseconds
// We don't reset SysTick counter, but instead set the SysTick compare register
void delay(uint32_t ms) {
    uint32_t i;

    // Clear Status register
    SysTick->SR &= ~(1 << 0);
    // SystemCoreClock / 8000 = 1 mS (12000 cycles @ 96Mhz)
    i = (uint32_t)ms * (FUNCONF_SYSTEM_CORE_CLOCK / 8000);
    // Set compare register to current counter value + delay
    SysTick->CMP = SysTick->CNT + i;
    // Wait (blocking) for flag to be set
    while((SysTick->SR & 0x01) == 0);
}
#endif

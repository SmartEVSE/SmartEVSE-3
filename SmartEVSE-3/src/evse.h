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

#ifndef __EVSE_H
#define __EVSE_H


#include "main_c.h"

// USART Circular buffers
typedef struct {
    char buffer[256];
    volatile uint16_t head;
    volatile uint16_t tail;
} CircularBuffer;

// used in modbus.c
extern CircularBuffer ModbusTx;                 // USART2 Transmit buffer (modbus)


void setState(uint8_t NewState);
uint8_t buffer_enqueue(CircularBuffer *cb, char data);
uint8_t buffer_dequeue(CircularBuffer *cb, char *data);
int buffer_write(CircularBuffer *cb, char *data, uint16_t size);
void uart_start_dma_transfer(void);
int _write(int fd, const char *buffer, int size);
void delay(uint32_t ms);
#endif

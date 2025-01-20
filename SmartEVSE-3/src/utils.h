/*
;    Project:       Smart EVSE
;
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

// This is a guard condition so that contents of this file are not included
// more than once.  
#ifndef UTILS_H
#define	UTILS_H

extern unsigned long pow_10[10];
unsigned char crc8(unsigned char *buf, unsigned char len);
uint8_t triwave8(uint8_t in);
uint8_t scale8(uint8_t i, uint8_t scale);
uint8_t ease8InOutQuad(uint8_t i);

#ifdef SMARTEVSE_VERSION //ESP32
uint32_t MacId();
void sprintfl(char *str, const char *Format, signed long Value, unsigned char Divisor, unsigned char Decimal);
unsigned char triwave8(unsigned char in);
unsigned char scale8(unsigned char i, unsigned char scale);
unsigned char ease8InOutQuad(unsigned char i);
#else //CH32

#define max(a,b) ((a)>(b)?(a):(b))
#define min(a,b) ((a)<(b)?(a):(b))
uint16_t crc16(uint8_t *buf, uint8_t len);
//int _write(int fd, char *buf, int size);

#endif
#endif	/* UTILS_H */

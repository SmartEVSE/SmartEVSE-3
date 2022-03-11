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

uint32_t MacId();
unsigned char crc8(unsigned char *buf, unsigned char len);
unsigned int crc16(unsigned char *buf, unsigned char len);
void sprintfl(char *str, const char *Format, signed long Value, unsigned char Divisor, unsigned char Decimal);
unsigned char triwave8(unsigned char in);
unsigned char scale8(unsigned char i, unsigned char scale);
unsigned char ease8InOutQuad(unsigned char i);

#endif	/* UTILS_H */

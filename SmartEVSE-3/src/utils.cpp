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

#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>

#include "evse.h"
#include "utils.h"

unsigned long pow_10[10] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000};


// read Mac, and reverse to ID
uint32_t MacId() {

    uint32_t id = 0;

    for (int i=0; i<17; i=i+8) {
        id |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
    }
    return id >> 2;         // low two bits are unused.
}


unsigned char crc8(unsigned char *buf, unsigned char len) {
	unsigned char crc = 0, i, mix, inbyte;

	while (len--) {
		inbyte = *buf++;
		for (i = 8; i; i--) {
			mix = (crc ^ inbyte) & 0x01;
			crc >>= 1;
			if (mix) crc ^= 0x8C;
			inbyte >>= 1;
		}
	}
	return crc;
}

/**
 * Calculates 16-bit CRC of given data
 * used for Frame Check Sequence on data frame
 * 
 * @param unsigned char pointer to buffer
 * @param unsigned char length of buffer
 * @return unsigned int CRC
 */
/*
unsigned int crc16(unsigned char *buf, unsigned char len) {
    unsigned int pos, crc = 0xffff;
    unsigned char i;

    // Poly used is x^16+x^15+x^2+x
    for (pos = 0; pos < len; pos++) {
        crc ^= (unsigned int)buf[pos];                                          // XOR byte into least sig. byte of crc

        for (i = 8; i != 0; i--) {                                              // Loop over each bit
            if ((crc & 0x0001) != 0) {                                          // If the LSB is set
                crc >>= 1;                                                      // Shift right and XOR 0xA001
                crc ^= 0xA001;
            } else                                                              // Else LSB is not set
                crc >>= 1;                                                      // Just shift right
        }
    }

    return crc;
}
*/


/**
 * Insert rounded value into string in printf style
 * 
 * @param pointer to string
 * @param string Format
 * @param signed long Value to round and insert
 * @param unsinged char Divisor where to set decimal point
 * @param unsigned char Decimal place count
 */
void sprintfl(char *str, const char *Format, signed long Value, unsigned char Divisor, unsigned char Decimal) {
    signed long val;

    val = Value / (signed long) pow_10[Divisor - Decimal - 1];
    // Round value
    if(val < 0) val -= 5;
    else val += 5;
    val /= 10;
    // Split value
    if(Decimal > 0) sprintf(str, Format, (signed int) (val / (signed long) pow_10[Decimal]), (unsigned int) (abs(val) % pow_10[Decimal]));
    else sprintf(str, Format, (signed int) val);
}

/* triwave8: triangle (sawtooth) wave generator.  Useful for
           turning a one-byte ever-increasing value into a
           one-byte value that oscillates up and down.

           input         output
           0..127        0..254 (positive slope)
           128..255      254..0 (negative slope)
 */
unsigned char triwave8(unsigned char in) {
    if (in & 0x80) {
        in = 255u - in;
    }
    unsigned char out = in << 1;
    return out;
}

unsigned char scale8(unsigned char i, unsigned char scale) {
    return (((unsigned int) i) * (1 + (unsigned int) (scale))) >> 8;
}

/* easing functions; see http://easings.net

    ease8InOutQuad: 8-bit quadratic ease-in / ease-out function
 */
unsigned char ease8InOutQuad(unsigned char i) {
    unsigned char j = i;
    if (j & 0x80) {
        j = 255u - j;
    }
    unsigned char jj = scale8(j, j);
    unsigned char jj2 = jj << 1;
    if (i & 0x80) {
        jj2 = 255u - jj2;
    }
    return jj2;
}

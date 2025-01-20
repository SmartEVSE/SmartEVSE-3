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

#include <stdio.h>
#include <stdlib.h>

unsigned long pow_10[10] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000};

uint8_t crc8(uint8_t *buf, uint8_t len) {
    uint8_t crc = 0, i, mix, inbyte;

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



/* triwave8: triangle (sawtooth) wave generator.  Useful for
           turning a one-byte ever-increasing value into a
           one-byte value that oscillates up and down.

           input         output
           0..127        0..254 (positive slope)
           128..255      254..0 (negative slope)
 */
uint8_t triwave8(uint8_t in) {
    if (in & 0x80) {
        in = 255u - in;
    }
    uint8_t out = in << 1;
    return out;
}


uint8_t scale8(uint8_t i, uint8_t scale) {
    return (((uint16_t) i) * (1 + (uint16_t) (scale))) >> 8;
}


/* easing functions; see http://easings.net

    ease8InOutQuad: 8-bit quadratic ease-in / ease-out function
*/
uint8_t ease8InOutQuad(uint8_t i) {
    uint8_t j = i;
    if (j & 0x80) {
        j = 255u - j;
    }
    uint8_t jj = scale8(j, j);
    uint8_t jj2 = jj << 1;
    if (i & 0x80) {
        jj2 = 255u - jj2;
    }
    return jj2;
}

#ifdef SMARTEVSE_VERSION //ESP32
#include "esp32.h"
#include "utils.h"

// read Mac, and reverse to ID
uint32_t MacId() {

    uint32_t id = 0;

    for (int i=0; i<17; i=i+8) {
        id |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
    }
    return id >> 2;         // low two bits are unused.
}



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

#else //CH32
#include "ch32v003fun.h"
#include "ch32.h"
#include "evse.h"
#include "utils.h"


/**
 * Calculates 16-bit CRC of given data
 * used for Frame Check Sequence on data frame
 *
 * @param unsigned char pointer to buffer
 * @param unsigned char length of buffer
 * @return unsigned int CRC
 */
uint16_t crc16(uint8_t *buf, uint8_t len) {
    uint16_t pos, crc = 0xffff;
    uint8_t i;

    // Poly used is x^16+x^15+x^2+x
    for (pos = 0; pos < len; pos++) {
        crc ^= (uint16_t)buf[pos];                  // XOR byte into least sig. byte of crc

        for (i = 8; i != 0; i--) {                  // Loop over each bit
            if ((crc & 0x0001) != 0) {              // If the LSB is set
                crc >>= 1;                          // Shift right and XOR 0xA001
                crc ^= 0xA001;
            } else                                  // Else LSB is not set
                crc >>= 1;                          // Just shift right
        }
    }

    return crc;
}
#endif

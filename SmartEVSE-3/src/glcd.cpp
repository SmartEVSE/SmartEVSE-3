/*
;	 Project:       Smart EVSE
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
#include <string.h>
#include <SPI.h>
#include <WiFi.h>

#include "evse.h"
#include "glcd.h"
#include "utils.h"

#include "font.cpp"
#include "font2.cpp"


const unsigned char LCD_Flow [] = {
0x00, 0x00, 0x98, 0xCC, 0x66, 0x22, 0x22, 0x22, 0xF2, 0xAA, 0x26, 0x2A, 0xF2, 0x22, 0x22, 0x22,
0x66, 0xCC, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0xC0, 0x60, 0x30, 0x60, 0xC0,
0x90, 0x20, 0x40, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x42, 0x04, 0xE0, 0x10, 0x08,
0x0B, 0x08, 0x10, 0xE0, 0x04, 0x42, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1C, 0x22, 0x41, 0x4F,
0x49, 0x22, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x40, 0x61, 0x31, 0x18, 0x08, 0x08, 0x08, 0x08, 0xFF, 0x08, 0x8D, 0x4A, 0xFF, 0x08, 0x08, 0x08,
0x08, 0x18, 0x31, 0x61, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
0x01, 0x03, 0x06, 0x0C, 0x19, 0x32, 0x64, 0xC8, 0x10, 0x00, 0x00, 0x08, 0x04, 0x00, 0x01, 0x02,
0x1A, 0x02, 0x01, 0x00, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x05, 0x88, 0x50, 0xFF, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0xFF, 0x00, 0xF8, 0x08, 0x08, 0x08, 0x08, 0xF8, 0x00, 0x00, 0x00, 0xF0, 0x10,
0x10, 0x10, 0x10, 0x10, 0xF0, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x80, 0x60, 0x10, 0x08, 0x04, 0x02, 0x82, 0x81, 0x81, 0x81, 0x01, 0x01, 0x01, 0x01,
0x01, 0x01, 0x01, 0x02, 0x02, 0x04, 0x84, 0x8C, 0x88, 0x88, 0x10, 0x10, 0x20, 0x40, 0x80, 0x00,
0x00, 0x00, 0x00, 0x40, 0x60, 0x30, 0x18, 0x0C, 0x07, 0x05, 0x04, 0x04, 0x07, 0x0C, 0x18, 0x30,
0x68, 0x48, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
0x08, 0x08, 0x00, 0x7F, 0x40, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x40, 0x40, 0x40, 0x7F, 0x40,
0x40, 0x40, 0x42, 0x40, 0x7F, 0x40, 0x40, 0x7F, 0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
0x00, 0x06, 0x19, 0x10, 0x10, 0x1C, 0x02, 0x19, 0x24, 0x42, 0x42, 0x24, 0x19, 0x02, 0x1C, 0x10,
0x10, 0x10, 0x10, 0x1C, 0x02, 0x19, 0x24, 0x42, 0x42, 0x24, 0x19, 0x02, 0x1C, 0x10, 0x10, 0x1F
};

uint8_t LCDpos = 0;
bool LCDToggle = false;                                                         // Toggle display between two values
unsigned char LCDText = 0;                                                      // Cycle through text messages
unsigned int GLCDx, GLCDy;
uint8_t GLCDbuf[512];                                                       // GLCD buffer (half of the display)

void st7565_command(unsigned char data) {
    _A0_0;
    SPI.transfer(data);
}

void st7565_data(unsigned char data) {
    _A0_1;
    SPI.transfer(data);
}

void goto_row(unsigned char y) {
    unsigned char pattern;
    pattern = 0xB0 | (y & 0xBF);                                                // put row address on data port set command
    st7565_command(pattern);
}
//--------------------

void goto_col(unsigned char x) {
    unsigned char pattern;
    pattern = ((0xF0 & x) >> 4) | 0x10;
    st7565_command(pattern);                                                    // set high byte column command
    pattern = ((0x0F & x)) | 0x00;
    st7565_command(pattern);                                                    // set low byte column command;
}
//--------------------

void goto_xy(unsigned char x, unsigned char y) {
    goto_col(x);
    goto_row(y);
}

void glcd_clrln(unsigned char ln, unsigned char data) {
    unsigned char i;
    goto_xy(0, ln);
    for (i = 0; i < 128; i++) {
        st7565_data(data);                                                      // put data on data port
    }
}

/*
void glcd_clrln_buffer(unsigned char ln) {
    unsigned char i;
    if (ln > 7) return;
    for (i = 0; i < 128; i++) {
        GLCDbuf[i+ (ln * 128)] = 0;
    }
}
*/
void glcd_clear(void) {
    unsigned char i;
    for (i = 0; i < 8; i++) {
        glcd_clrln(i, 0);
    }
}

void GLCD_buffer_clr(void) {
    unsigned char x = 0;
    do {
        GLCDbuf[x++] = 0;                                                       // clear first 256 bytes of GLCD buffer
    } while (x != 0);
}

void GLCD_sendbuf(unsigned char RowAdr, unsigned char Rows) {
    unsigned char i, y = 0;
    unsigned int x = 0;

    do {
        goto_xy(0, RowAdr + y);
        for (i = 0; i < 128; i++) st7565_data(GLCDbuf[x++]);                    // put data on data port
    } while (++y < Rows);
}

void GLCD_font_condense(unsigned char c, unsigned char *start, unsigned char *end, unsigned char space) {
    if(c >= '0' && c <= '9') return;
    if(c == ' ' && space) return;
    if(font[c][0] == 0) {
        if(font[c][1] == 0) *start = 2;
        else *start = 1;
    }
    if(font[c][4] == 0) *end = 4;
}

unsigned char GLCD_text_length(const char *str) {
    unsigned char i = 0, length = 0;
    unsigned char s, e;

    while (str[i]) {
        s = 0;
        e = 5;
        GLCD_font_condense(str[i], &s, &e, 0);
        length += (e - s) + 1;
        i++;
    }

    return length - 1;
}

#ifdef GLCD_HIRES_FONT
unsigned char GLCD_text_length2(const char *str) {
    unsigned char i = 0, length = 0;

    while (str[i]) {
        length += font2[(int) str[i]][0] + 2;
        i++;
    }

    return length - 2;
}
#endif

/**
 * Write character to buffer
 *
 * @param c
 * @param 00000000 Options
 *            |+++ Shift font down
 *            +--- Merge with buffer content
*/
void GLCD_write_buf(unsigned int c, unsigned char Options) {
    unsigned int f, x;
    unsigned char i = 0, m = 5, shift = 0;
    bool merge = false;

    x = 128 * GLCDy;
    x += GLCDx;

    GLCD_font_condense(c, &i, &m, 1);                                           // remove whitespace from font
    GLCDx += (m - i) + 1;

    if (Options) {
        shift = Options & 0b00000111;
        if (Options & GLCD_MERGE) merge = true;
    }

    do {
        f = font[c][i];
        if(shift) f <<= shift;
        if(merge) f |= GLCDbuf[x];
        GLCDbuf[x] = f;
        x++;
    } while (++i < m);
}

// Write one double height character to the GLCD buffer
// special characters '.' and ' ' will use reduced width in the buffer
void GLCD_write_buf2(unsigned int c) {
#ifdef GLCD_HIRES_FONT
    unsigned char i = 1;

    while ((i < (font2[c][0] * 2)) && (GLCDx < 128)) {
        GLCDbuf[GLCDx] = font2[c][i];
        GLCDbuf[GLCDx + 128] = font2[c][i + 1];
        i += 2;
        GLCDx ++;
    }
#else
    unsigned char i = 0, m = 5, ch, z1;

    GLCD_font_condense(c, &i, &m, 0);

    while ((i < m) && (GLCDx < 127)) {
        z1 = 0;
        ch = font[c][i];
        if (ch & 0x01u) z1 |= 0x3u;
        if (ch & 0x02u) z1 |= 0xcu;
        if (ch & 0x04u) z1 |= 0x30u;
        if (ch & 0x08u) z1 |= 0xc0u;
        GLCDbuf[GLCDx] = z1;
        GLCDbuf[GLCDx + 1] = z1;
        z1 = 0;
        ch = ch >> 4;
        if (ch & 0x01u) z1 |= 0x3u;
        if (ch & 0x02u) z1 |= 0xcu;
        if (ch & 0x04u) z1 |= 0x30u;
        if (ch & 0x08u) z1 |= 0xc0u;
        GLCDbuf[GLCDx + 128] = z1;
        GLCDbuf[GLCDx + 129] = z1;
        i++;
        GLCDx += 2;
    };
#endif
    GLCDx += 2;
}

/**
 * Write a string to LCD buffer
 *
 * @param str
 * @param x
 * @param y
 * @param Options
 */
void GLCD_write_buf_str(unsigned char x, unsigned char y, const char* str, unsigned char Options) {
    unsigned char i = 0;

    switch(Options) {
        case GLCD_ALIGN_LEFT:
            GLCDx = x;
            break;
        case GLCD_ALIGN_CENTER:
            GLCDx = x - (GLCD_text_length(str) / 2);
            break;
        case GLCD_ALIGN_RIGHT:
            GLCDx = x - GLCD_text_length(str);
            break;
    }

    GLCDy = y;
    while (str[i]) {
        GLCD_write_buf(str[i++], 0);
    }
}

void GLCD_write_buf_str2(const char *str, unsigned char Options) {
    unsigned char i = 0;

#ifdef GLCD_HIRES_FONT
    if (Options == GLCD_ALIGN_CENTER) GLCDx = 64 - GLCD_text_length2(str) / 2;
#else
    if (Options == GLCD_ALIGN_CENTER) GLCDx = 64 - GLCD_text_length(str);
#endif
    else GLCDx = 2;

    while (str[i]) {
        GLCD_write_buf2(str[i++]);
    }
}

void GLCD_print_buf(unsigned char y, const char *str) {
    GLCD_buffer_clr();                                                          // Clear buffer
    GLCD_write_buf_str(0, y, str, GLCD_ALIGN_LEFT);
    GLCD_sendbuf(y, 1);                                                         // copy buffer to LCD
}

// uses buffer
void GLCD_print_buf2_left(const char *data) {
    GLCD_buffer_clr();                                                          // Clear buffer
    GLCD_write_buf_str2(data, GLCD_ALIGN_LEFT);
    GLCD_sendbuf(2, 2);                                                         // copy buffer to LCD
}

void GLCD_print_buf2(unsigned char y, const char* str) {
    GLCD_buffer_clr();                                                          // Clear buffer
    GLCD_write_buf_str2(str, GLCD_ALIGN_CENTER);
    GLCD_sendbuf(y, 2);                                                        // copy buffer to LCD
}

// Write Menu to buffer, then send to GLCD
void GLCD_print_menu(unsigned char y, const char* str) {
    GLCD_buffer_clr();                                                          // Clear buffer
    GLCD_write_buf_str2(str, GLCD_ALIGN_CENTER);

    if ((SubMenu && y == 4) || (!SubMenu && y == 2)) {                          // navigation arrows
        GLCDx = 0;
        GLCD_write_buf2('<');
        GLCDx = 10 * 12;                                                        // last character of line
        GLCD_write_buf2('>');
    }

    GLCD_sendbuf(y, 2);
}


/**
 * Increase or decrease int value
 * 
 * @param unsigned int Buttons
 * @param unsigned int Value
 * @param unsigned int Min
 * @param unsigned int Max
 * @return unsigned int Value
 */
unsigned int MenuNavInt(unsigned char Buttons, unsigned int Value, unsigned int Min, unsigned int Max) {
    if (Buttons == 0x3) {
        if (Value >= Max) Value = Min;
        else Value++;
    } else if (Buttons == 0x6) {
        if (Value <= Min) Value = Max;
        else Value--;
    }

    return Value;
}

/**
 * Get to next or previous value of an char array
 * 
 * @param unsigned char Buttons
 * @param unsigned char Value
 * @param unsigned char Count
 * @param unsigned char array Values
 * @return unsigned char Value
 */
unsigned char MenuNavCharArray(unsigned char Buttons, unsigned char Value, unsigned char Values[], unsigned char Count) {
    unsigned int i;

    for (i = 0; i < Count; i++) {
        if (Value == Values[i]) break;
    }
    i = MenuNavInt(Buttons, i, 0, Count - 1u);

    return Values[i];
}

// uses buffer
void GLCDHelp(void)                                                             // Display/Scroll helptext on LCD 
{
    unsigned int x;

    x = strlen(MenuStr[LCDNav].Desc);
    GLCD_print_buf2_left(MenuStr[LCDNav].Desc + LCDpos);

    if (LCDpos++ == 0) ScrollTimer = millis() - 4000;
    else if (LCDpos > (x - 10)) {
        ScrollTimer = millis() - 3000;
        LCDpos = 0;
    } else ScrollTimer = millis() - 4700;
}


// called once a second
void GLCD(void) {
    unsigned char x;
    unsigned int seconds, minutes;
    static unsigned char energy_mains = 20; // X position
    static unsigned char energy_ev = 74; // X position
    char Str[26];

    LCDTimer++;
    
    if (LCDNav) {
        if (LCDNav == MENU_RFIDREADER && SubMenu) {
            if (RFIDstatus == 2) GLCD_print_buf(0, (const char*) "Card Stored");
            else if (RFIDstatus == 3) GLCD_print_buf(0, (const char*) "Card Deleted");
            else if (RFIDstatus == 4) GLCD_print_buf(0, (const char*) "Card already stored!");
            else if (RFIDstatus == 5) GLCD_print_buf(0, (const char*) "Card not in storage!");
            else if (RFIDstatus == 6) GLCD_print_buf(0, (const char*) "Card storage full!");
            else glcd_clrln(0, 0x00);                                           // Clear line
            LCDTimer = 0;                                                       // reset timer, so it will not exit the menu when learning/deleting cards
        } else {

            GLCD_buffer_clr();
            // When connected to Wifi, display IP and time in top row
            uint8_t WIFImode = getItemValue(MENU_WIFI);
            if (WIFImode == 1 ) {   // Wifi Enabled

                if (WiFi.status() == WL_CONNECTED) {
                    sprintf(Str, "%s %i%cC",WiFi.localIP().toString().c_str(), getItemValue(STATUS_TEMP), 0x0C);
                    GLCD_write_buf_str(0,0, Str, GLCD_ALIGN_LEFT);
                    if (LocalTimeSet) sprintf(Str, "%02u:%02u",timeinfo.tm_hour, timeinfo.tm_min);
                    else sprintf(Str, "--:--");
                    GLCD_write_buf_str(127,0, Str, GLCD_ALIGN_RIGHT);
                } else GLCD_write_buf_str(0,0, "Not connected to WiFi", GLCD_ALIGN_LEFT);

            // When Wifi Setup is selected, show password and SSID of the Access Point
            } else if (WIFImode == 2 && SubMenu) {
                if (LCDTimer <= 10 && WiFi.getMode() != WIFI_AP_STA) {           // Do not show if AP_STA mode is started 
                    sprintf(Str, "Start portal in %u sec", 10-LCDTimer);
                    GLCD_write_buf_str(0,0, Str, GLCD_ALIGN_LEFT);                                        
                } else {
                    // Show Access Point name
                    sprintf(Str, "AP:%u", MacId() & 0xffff);                
                    GLCD_write_buf_str(0,0, Str, GLCD_ALIGN_LEFT);
                    // and password
                    sprintf(Str, "PW:%s", APpassword.c_str());
                    GLCD_write_buf_str(127,0, Str, GLCD_ALIGN_RIGHT);
                }
            } 
            // update LCD 
            GLCD_sendbuf(0, 1);
        }

        if (LCDTimer > 120) {
            LCDNav = 0;                                                         // Exit Setup menu after 120 seconds.
            read_settings(false);                                               // don't save, but restore settings
        } else return;                                                          // disable LCD status messages when navigating LCD Menu
    }
   
    if (LCDTimer == 1) {
        LCDText = 0;
    } else if (LCDTimer > 4) {
        LCDTimer = 1;
        LCDToggle = 1u - LCDToggle;
        LCDText++;
    }

    if (ErrorFlags) {                                                           // We switch backlight on, as we exit after displaying the error
        // BacklightTimer = BACKLIGHT;                                             // Backlight timer is set to 60 seconds

        if (ErrorFlags & CT_NOCOMM) {                                           // No serial communication for 10 seconds
            GLCD_print_buf2(0, (const char *) "ERROR NO");
            GLCD_print_buf2(2, (const char *) "SERIAL COM");
            GLCD_print_buf2(4, (const char *) "CHECK");
            GLCD_print_buf2(6, (const char *) "WIRING");
            return;
        } else if (ErrorFlags & TEMP_HIGH) {                                    // Temperature reached 65C
            GLCD_print_buf2(0, (const char *) "HIGH TEMP");
            GLCD_print_buf2(2, (const char *) "ERROR");
            GLCD_print_buf2(4, (const char *) "CHARGING");
            GLCD_print_buf2(6, (const char *) "STOPPED");
            return;
        } else if (ErrorFlags & RCM_TRIPPED) {                                  // Residual Current Sensor tripped
            if (!LCDToggle) {
                GLCD_print_buf2(0, (const char *) "RESIDUAL");
                GLCD_print_buf2(2, (const char *) "FAULT");
                GLCD_print_buf2(4, (const char *) "CURRENT");
                GLCD_print_buf2(6, (const char *) "DETECTED");
            } else {
                GLCD_print_buf2(0, (const char *) "PRESS");
                GLCD_print_buf2(2, (const char *) "BUTTON");
                GLCD_print_buf2(4, (const char *) "TO");
                GLCD_print_buf2(6, (const char *) "RESET");
            }
            return;
        } else if (ErrorFlags & Test_IO) {                                      // Only used when testing the module
            GLCD_print_buf2(2, (const char *) "IO Test");
            sprintf(Str, "FAILED! %u", TestState);
            GLCD_print_buf2(4, Str);
            return;
        } else if (ErrorFlags & BL_FLASH) {                                     // Bootloader update error
            GLCD_print_buf2(2, (const char *) "BOOTLOADER");
            GLCD_print_buf2(4, (const char *) "UPDATE ERR");
            return;
        }
    }   // end of ERROR()                                                       // more specific error handling in the code below

    if (TestState == 80) {                                                      // Only used when testing the module
        GLCD_print_buf2(2, (const char *) "IO Test");
        GLCD_print_buf2(4, (const char *) "Passed");
        return;
    }

                                                                                // MODE NORMAL
    if (Mode == MODE_NORMAL || !Access_bit) {

        glcd_clrln(0, 0x00);
        glcd_clrln(1, 0x04);                                                    // horizontal line
        glcd_clrln(6, 0x10);                                                    // horizontal line
        glcd_clrln(7, 0x00);

        if (ErrorFlags & LESS_6A) {
            GLCD_print_buf2(2, (const char *) "WAITING");
            GLCD_print_buf2(4, (const char *) "FOR POWER");
        } else if (State == STATE_C) {                                          // STATE C
            
            BacklightTimer = BACKLIGHT;
            
            GLCD_print_buf2(2, (const char *) "CHARGING");
            sprintf(Str, "%u.%uA",Balanced[0] / 10, Balanced[0] % 10);
            GLCD_print_buf2(4, Str);
        } else {                                                                // STATE A and STATE B
            if (Access_bit) {
                GLCD_print_buf2(2, (const char *) "READY TO");
                sprintf(Str, "CHARGE %u", ChargeDelay);
                if (ChargeDelay) {
                    // BacklightTimer = BACKLIGHT;
                } else Str[6] = '\0';
                GLCD_print_buf2(4, Str);
            } else {
                if (getItemValue(MENU_RFIDREADER)) {
                    if (RFIDstatus == 7) {
                        GLCD_print_buf2(2, (const char *) "INVALID");
                        GLCD_print_buf2(4, (const char *) "RFID CARD");
                    } else {
                        GLCD_print_buf2(2, (const char *) "PRESENT");
                        GLCD_print_buf2(4, (const char *) "RFID CARD");
                    }
                } else {
                    GLCD_print_buf2(2, (const char *) "ACCESS");
                    GLCD_print_buf2(4, (const char *) "DENIED");
                }
            }
        }
    }                                                                           // MODE SMART or SOLAR
    else if ((Mode == MODE_SMART) || (Mode == MODE_SOLAR)) {

        memcpy (GLCDbuf, LCD_Flow, 512);                                        // copy Flow Menu to LCD buffer

        if (Mode == MODE_SMART) {                                               // remove the Sun from the LCD buffer
            for (x=0; x<13; x++) {
                GLCDbuf[x+74u] = 0;
                GLCDbuf[x+74u+128u] = 0;
            }
        }
        if (SolarStopTimer) {
            seconds = SolarStopTimer;                                           // display remaining time before charging is stopped
            minutes = seconds / 60;
            seconds = seconds % 60;
            sprintf(Str, "%02u:%02u", minutes, seconds);
            GLCD_write_buf_str(100, 0, Str, GLCD_ALIGN_LEFT);                   // print to buffer
        } else {
            for (x = 0; x < 8; x++) GLCDbuf[x + 92u] = 0;                       // remove the clock from the LCD buffer
        }


        if (Isum < 0) {
            energy_mains -= 3;                                                  // animate the flow of Mains energy on LCD.
            if (energy_mains < 20) energy_mains = 44;                           // Only in Mode: Smart or Solar
        } else {
            energy_mains += 3;
            if (energy_mains > 44) energy_mains = 20;
        }

        GLCDx = energy_mains;
        GLCDy = 3;

        if (abs(Isum) >3 ) GLCD_write_buf(0x0A, 0);                             // Show energy flow 'blob' between Grid and House
                                                                                // If current flow is < 0.3A don't show the blob

        if (EVMeter) {                                                          // If we have a EV kWh meter configured, Show total charged energy in kWh on LCD.
            //sprintfl(Str, "%2u.%1ukWh", EnergyCharged, 3, 1);                   // Will reset to 0.0kWh when charging cable reconnected, and state change from STATE B->C
            //GLCD_write_buf_str(89, 1, Str,GLCD_ALIGN_LEFT);                     // print to buffer
        }

        // Write number of used phases into the car
   /*     if (Node[0].Phases) {
            GLCDx = 110;
            GLCDy = 2;
            GLCD_write_buf(Node[0].Phases, 2 | GLCD_MERGE);
        }
*/
        if (State == STATE_C) {
            BacklightTimer = BACKLIGHT;

            energy_ev += 3;                                                     // animate energy flow to EV
            if (energy_ev > 89) energy_ev = 74;

            GLCDx = energy_ev;
            GLCDy = 3;
            GLCD_write_buf(0x0A, 0);                                            // Show energy flow 'blob' between House and Car

            if (LCDToggle && EVMeter) {
                if (PowerMeasured < 9950) {
                    sprintfl(Str, "%1u.%1ukW", PowerMeasured, 3, 1);
                } else {
                    sprintfl(Str, "%ukW", PowerMeasured, 3, 0);
                }
            } else {
                sprintfl(Str, "%uA", Balanced[0], 1, 0);
            }
            GLCD_write_buf_str(127,1, Str, GLCD_ALIGN_RIGHT);
        } else if (State == STATE_A) {
            // Remove line between House and Car
            for (x = 73; x < 96; x++) GLCDbuf[3u * 128u + x] = 0;
        }

        if (LCDToggle && Mode == MODE_SOLAR) {                                  // Show Sum of currents when solar charging.
            GLCDx = 41;
            GLCDy = 1;
            GLCD_write_buf(0x0B, 0);                                            // Sum symbol

            sprintfl(Str, "%dA", Isum, 1, 0);
            GLCD_write_buf_str(46, 2, Str, GLCD_ALIGN_RIGHT);                   // print to buffer
        } else {                                                                // Displayed only in Smart and Solar modes
            for (x = 0; x < 3; x++) {                                           // Display L1, L2 and L3 currents on LCD
                sprintfl(Str, "%dA", Irms[x], 1, 0);
                GLCD_write_buf_str(46, x, Str, GLCD_ALIGN_RIGHT);               // print to buffer
                sprintfl(Str, "%dA", Irms_EV[x], 1, 0);
                if (Mode == MODE_SOLAR)
                    GLCD_write_buf_str(100, x, Str, GLCD_ALIGN_RIGHT);          // in Solar mode the sun needs a little more room
                else
                    GLCD_write_buf_str(90, x, Str, GLCD_ALIGN_RIGHT);           // print to buffer
            }
        }
        GLCD_sendbuf(0, 4);                                                     // Copy LCD buffer to GLCD

        glcd_clrln(4, 0);                                                       // Clear line 4
        if (ErrorFlags & LESS_6A) {
            if (!LCDToggle) {
                GLCD_print_buf2(5, (const char *) "WAITING");
            } else GLCD_print_buf2(5, (const char *) "FOR POWER");
        } else if (ErrorFlags & NO_SUN) {
            if (!LCDToggle) {
                GLCD_print_buf2(5, (const char *) "WAITING");
            } else GLCD_print_buf2(5, (const char *) "FOR SOLAR");
        } else if (State != STATE_C) {
                switch (Switching_To_Single_Phase) {
                    case FALSE:
                        sprintf(Str, "READY %u", ChargeDelay);
                        if (!ChargeDelay) Str[5] = '\0';
                        break;
                    case GOING_TO_SWITCH:
                        sprintf(Str, "3F -> 1F %u", ChargeDelay);
                        if (!ChargeDelay) Str[7] = '\0';
                        break;
                    case AFTER_SWITCH:                                          // never getting here, just preventing compiler warning
                        break;
                }
                GLCD_print_buf2(5, Str);
        } else if (State == STATE_C) {
            switch (LCDText) {
                default:
                    LCDText = 0;
                    if (Mode != MODE_NORMAL) {
                        if (Mode == MODE_SOLAR) sprintf(Str, "SOLAR");
                            else sprintf(Str, "SMART");
                        if (Nr_Of_Phases_Charging != 0) {
                            sprintf(Str+5," %uF", Nr_Of_Phases_Charging);
                        }
                        GLCD_print_buf2(5, Str);
                        break;
                    } else LCDText++;
                case 1:
                    GLCD_print_buf2(5, (const char *) "CHARGING");
                    break;
                case 2:
                    if (EVMeter) {
                        sprintfl(Str, "%u.%01u kW", PowerMeasured, 3, 1);
                        GLCD_print_buf2(5, Str);
                        break;
                    } else LCDText++;
                case 3:
                    if (EVMeter) {
                        sprintfl(Str, "%u.%02u kWh", EnergyCharged, 3, 2);
                        GLCD_print_buf2(5, Str);
                        break;
                    } else LCDText++;
                case 4:
                    sprintf(Str, "%u.%u A", Balanced[0] / 10, Balanced[0] % 10);
                    GLCD_print_buf2(5, Str);
                    break;
            }
        }
        glcd_clrln(7, 0x00);
    } // End Mode SMART or SOLAR

}


/**
 * Counts nr of menu options currently available
 * 
 * @param unsigned char count
 * @return unsigned char postion
 */
unsigned char GetPosInMenu (unsigned char count) {
    unsigned char i;

    for (i = 0; i < count; i++) {
        if (MenuItems[i] == LCDNav) return i + 1u;
    }
    return 0;
}


/**
 * Get active option of an menu item
 *
 * @param uint8_t nav
 * @return uint8_t[] MenuItemOption
 */
const char * getMenuItemOption(uint8_t nav) {
    static char Str[12]; // must be declared static, since it's referenced outside of function scope

    // Text
    const static char StrFixed[]   = "Fixed";
    const static char StrSocket[]  = "Socket";
    const static char StrSmart[]   = "Smart";
    const static char StrNormal[]  = "Normal";
    const static char StrSolar[]   = "Solar";
    const static char StrSolenoid[] = "Solenoid";
    const static char StrMotor[]   = "Motor";
    const static char StrDisabled[] = "Disabled";
    const static char StrLoadBl[9][9]  = {"Disabled", "Master", "Node 1", "Node 2", "Node 3", "Node 4", "Node 5", "Node 6", "Node 7"};
    const static char StrSwitch[5][10] = {"Disabled", "Access B", "Access S", "Sma-Sol B", "Sma-Sol S"};
    const static char StrGrid[2][10] = {"4Wire", "3Wire"};
    const static char StrEnabled[] = "Enabled";
    const static char StrExitMenu[] = "MENU";
    const static char StrMainsAll[] = "All";
    const static char StrMainsHomeEVSE[] = "Home+EVSE";
    const static char StrRFIDReader[6][10] = {"Disabled", "EnableAll", "EnableOne", "Learn", "Delete", "DeleteAll"};
    const static char StrWiFi[3][10] = {"Disabled", "Enabled", "SetupWifi"};

    unsigned int value = getItemValue(nav);

    switch (nav) {
        case MENU_MAX_TEMP:
            sprintf(Str, "%2u C", value);
            return Str;
        case MENU_C2:
            return StrEnableC2[value];
        case MENU_CONFIG:
            if (value) return StrFixed;
            else return StrSocket;
        case MENU_MODE:
            if (Mode == MODE_SMART) return StrSmart;
            else if (Mode == MODE_SOLAR) return StrSolar;
            else return StrNormal;
        case MENU_START:
                sprintf(Str, "-%2u A", value);
                return Str;
        case MENU_STOP:
            if (value) {
                sprintf(Str, "%2u min", value);
                return Str;
            } else return StrDisabled;
        case MENU_LOADBL:
            if (ExternalMaster && value == 1) return "Node 0";
            else return StrLoadBl[LoadBl];
        case MENU_MAINS:
        case MENU_MIN:
        case MENU_MAX:
        case MENU_CIRCUIT:
        case MENU_IMPORT:
            sprintf(Str, "%2u A", value);
            return Str;
        case MENU_LOCK:
            if (value == 1) return StrSolenoid;
            else if (value == 2) return StrMotor;
            else return StrDisabled;
        case MENU_SWITCH:
            return StrSwitch[value];
        case MENU_RCMON:
            if (value) return StrEnabled;
            else return StrDisabled;
        case MENU_MAINSMETER:
        case MENU_PVMETER:
        case MENU_EVMETER:
            return (const char*)EMConfig[value].Desc;
        case MENU_GRID:
            return StrGrid[value];
        case MENU_MAINSMETERADDRESS:
        case MENU_PVMETERADDRESS:
        case MENU_EVMETERADDRESS:
        case MENU_EMCUSTOM_UREGISTER:
        case MENU_EMCUSTOM_IREGISTER:
        case MENU_EMCUSTOM_PREGISTER:
        case MENU_EMCUSTOM_EREGISTER:
            if(value < 0x1000) sprintf(Str, "%u (%02X)", value, value);     // This just fits on the LCD.
            else sprintf(Str, "%u %X", value, value);
            return Str;
        case MENU_MAINSMETERMEASURE:
            if (value) return StrMainsHomeEVSE;
            else return StrMainsAll;
        case MENU_EMCUSTOM_ENDIANESS:
            switch(value) {
                case 0: return "LBF & LWF";
                case 1: return "LBF & HWF";
                case 2: return "HBF & LWF";
                case 3: return "HBF & HWF";
                default: return "";
            }
        case MENU_EMCUSTOM_DATATYPE:
            switch (value) {
                case MB_DATATYPE_INT16: return "INT16";
                case MB_DATATYPE_INT32: return "INT32";
                case MB_DATATYPE_FLOAT32: return "FLOAT32";
            }
        case MENU_EMCUSTOM_FUNCTION:
            switch (value) {
                case 3: return "3:Hold. Reg";
                case 4: return "4:Input Reg";
                default: return "";
            }
        case MENU_EMCUSTOM_UDIVISOR:
        case MENU_EMCUSTOM_IDIVISOR:
        case MENU_EMCUSTOM_PDIVISOR:
        case MENU_EMCUSTOM_EDIVISOR:
            sprintf(Str, "%lu", pow_10[value]);
            return Str;
        case MENU_RFIDREADER:
            return StrRFIDReader[value];
        case MENU_WIFI:
            return StrWiFi[value];
        case MENU_EXIT:
            return StrExitMenu;
        default:
            return "";
    }
}


/**
 * Create an array of available menu items
 * Depends on configuration settings like CONFIG/MODE/LoadBL
 *
 * @return uint8_t MenuItemCount
 */
uint8_t getMenuItems (void) {
    uint8_t m = 0;

    MenuItems[m++] = MENU_CONFIG;                                               // Configuration (0:Socket / 1:Fixed Cable)
    if (!getItemValue(MENU_CONFIG)) {                                                              // ? Fixed Cable?
        MenuItems[m++] = MENU_LOCK;                                             // - Cable lock (0:Disable / 1:Solenoid / 2:Motor)
    }
    MenuItems[m++] = MENU_MODE;                                                 // EVSE mode (0:Normal / 1:Smart)
    if (Mode == MODE_SOLAR && LoadBl < 2) {                                     // ? Solar mode and Load Balancing Disabled/Master?
        MenuItems[m++] = MENU_START;                                            // - Start Surplus Current (A)
        MenuItems[m++] = MENU_STOP;                                             // - Stop time (min)
        MenuItems[m++] = MENU_IMPORT;                                           // - Import Current from Grid (A)
    }
    MenuItems[m++] = MENU_LOADBL;                                               // Load Balance Setting (0:Disable / 1:Master / 2-8:Node)
    if (Mode && LoadBl < 2) {                                                   // ? Mode Smart/Solar and Load Balancing Disabled/Master?
        MenuItems[m++] = MENU_MAINS;                                            // - Max Mains Amps (hard limit, limited by the MAINS connection) (A) (Mode:Smart/Solar)
    }
    if (Mode && (LoadBl < 2 || LoadBl == 1)) {                                  // ? Mode Smart/Solar or LoadBl Master?
        MenuItems[m++] = MENU_MIN;                                              // - Minimal current the EV is happy with (A) (Mode:Smart/Solar or LoadBl:Master)
    }
    if (LoadBl == 1 || (LoadBl == 0 && Mode != MODE_NORMAL)) {                  // ? Load balancing Master?
                                                                                // Also, when not in Normal Mode, MaxCircuit will limit
                                                                                // the total current (subpanel configuration)
        MenuItems[m++] = MENU_CIRCUIT;                                          // - Max current of the EVSE circuit (A)
    }
    if (LoadBl == 0)
        MenuItems[m++] = MENU_C2;
    MenuItems[m++] = MENU_MAX;                                                  // Max Charge current (A)
    MenuItems[m++] = MENU_SWITCH;                                               // External Switch on SW (0:Disable / 1:Access / 2:Smart-Solar)
    MenuItems[m++] = MENU_RCMON;                                                // Residual Current Monitor on RCM (0:Disable / 1:Enable)
    MenuItems[m++] = MENU_RFIDREADER;                                           // RFID Reader connected to SW (0:Disable / 1:Enable / 2:Learn / 3:Delete / 4:Delate All)
    uint8_t MainsMeter = getItemValue(MENU_MAINSMETER);
    if (Mode) {                                                                 // ? Smart or Solar mode?
        if (LoadBl < 2) {                                                       // - ? Load Balancing Disabled/Master?
            MenuItems[m++] = MENU_MAINSMETER;                                   // - - Type of Mains electric meter (0: Disabled / Constants EM_*)
            if (MainsMeter == EM_SENSORBOX || MainsMeter == EM_API) {                                   // - - ? Sensorbox?
                if (GridActive == 1) MenuItems[m++] = MENU_GRID;
                if (CalActive == 1) MenuItems[m++] = MENU_CAL;                  // - - - Sensorbox CT measurement calibration
            } else if(MainsMeter) {                                             // - - ? Other?
                MenuItems[m++] = MENU_MAINSMETERADDRESS;                        // - - - Address of Mains electric meter (5 - 254)
                MenuItems[m++] = MENU_MAINSMETERMEASURE;                        // - - - What does Mains electric meter measure (0: Mains (Home+EVSE+PV) / 1: Home+EVSE / 2: Home)
                if (getItemValue(MENU_MAINSMETERMEASURE)) {                     // - - - ? PV not measured by Mains electric meter?
                    MenuItems[m++] = MENU_PVMETER;                              // - - - - Type of PV electric meter (0: Disabled / Constants EM_*)
                    if (PVMeter) MenuItems[m++] = MENU_PVMETERADDRESS;          // - - - - - Address of PV electric meter (5 - 254)
                }
            }
        }
        MenuItems[m++] = MENU_EVMETER;                                          // - Type of EV electric meter (0: Disabled / Constants EM_*)
        if (EVMeter) {                                                          // - ? EV meter configured?
            MenuItems[m++] = MENU_EVMETERADDRESS;                               // - - Address of EV electric meter (5 - 254)
        }
        if (LoadBl < 2) {                                                       // - ? Load Balancing Disabled/Master?
            if (MainsMeter == EM_CUSTOM || PVMeter == EM_CUSTOM || EVMeter == EM_CUSTOM) { // ? Custom electric meter used?
                MenuItems[m++] = MENU_EMCUSTOM_ENDIANESS;                       // - - Byte order of custom electric meter
                MenuItems[m++] = MENU_EMCUSTOM_DATATYPE;                        // - - Data type of custom electric meter
                MenuItems[m++] = MENU_EMCUSTOM_FUNCTION;                        // - - Modbus Function of custom electric meter
                MenuItems[m++] = MENU_EMCUSTOM_UREGISTER;                       // - - Starting register for voltage of custom electric meter
                MenuItems[m++] = MENU_EMCUSTOM_UDIVISOR;                        // - - Divisor for voltage of custom electric meter
                MenuItems[m++] = MENU_EMCUSTOM_IREGISTER;                       // - - Starting register for current of custom electric meter
                MenuItems[m++] = MENU_EMCUSTOM_IDIVISOR;                        // - - Divisor for current of custom electric meter
                MenuItems[m++] = MENU_EMCUSTOM_PREGISTER;                       // - - Starting register for power of custom electric meter
                MenuItems[m++] = MENU_EMCUSTOM_PDIVISOR;                        // - - Divisor for power of custom electric meter
                MenuItems[m++] = MENU_EMCUSTOM_EREGISTER;                       // - - Starting register for energy of custom electric meter
                MenuItems[m++] = MENU_EMCUSTOM_EDIVISOR;                        // - - Divisor for energy of custom electric meter
            }
        }
    }
    MenuItems[m++] = MENU_WIFI;                                                 // Wifi Disabled / Enabled / Portal
    MenuItems[m++] = MENU_MAX_TEMP;
    MenuItems[m++] = MENU_EXIT;

    return m;
}




/**
 * Called when one of the SmartEVSE buttons is pressed
 * 
 * @param Buttons: < o >
 *          Value: 1 2 4
 *            Bit: 0:Pressed / 1:Released
 */
void GLCDMenu(uint8_t Buttons) {
    static unsigned long ButtonTimer = 0;
    static uint8_t ButtonRelease = 0;                                           // keeps track of LCD Menu Navigation
    static uint16_t CT1, value, ButtonRepeat = 0;
    char Str[24];

    unsigned char MenuItemsCount = getMenuItems();

    // Main Menu Navigation
    BacklightTimer = BACKLIGHT;                                                 // delay before LCD backlight turns off.

    if (getItemValue(MENU_RCMON) == 1 && (ErrorFlags & RCM_TRIPPED) && RCMFAULT == LOW) {          // RCM was tripped, but RCM level is back to normal
        ErrorFlags &= ~RCM_TRIPPED;                                             // Clear RCM error bit, by pressing any button
    }

    if ((LCDNav == 0) && (Buttons == 0x5) && (ButtonRelease == 0)) {            // Button 2 pressed ?
        LCDNav = MENU_ENTER;                                                    // about to enter menu
        ButtonTimer = millis();
    } else if (LCDNav == MENU_ENTER && ((ButtonTimer + 2000) < millis() )) {    // <CONFIG>
        LCDNav = MENU_CONFIG;                                                   // Main Menu entered
        ButtonRelease = 1;
    } else if ((LCDNav == MENU_ENTER) && (Buttons == 0x7)) {                    // Button 2 released before entering menu?
        LCDNav = 0;
        ButtonRelease = 0;
        GLCD();
    } else if (Buttons == 0x2 && (ButtonRelease == 0)) {                        // Buttons < and > pressed ?
        if ((LCDNav == MENU_CAL) &&  SubMenu ) {                                // While in CT CAL submenu ?
            ICal = ICAL;                                                        // reset Calibration value
            SubMenu = 0;                                                        // Exit Submenu
        } else GLCD_init();                                                     // re-initialize LCD
        ButtonRelease = 1;
    } else if ((LCDNav > 1) && (Buttons == 0x2 || Buttons == 0x3 || Buttons == 0x6)) { // Buttons < or > or both pressed
        if (ButtonRelease == 0) {                                               // We are navigating between sub menu options
            if (SubMenu) {
                switch (LCDNav) {
                    case MENU_CAL:
                        CT1 = MenuNavInt(Buttons, CT1, 100, 999);
                        break;
                    case MENU_EVMETER:                                          // do not display the Sensorbox or EM_API here
                        value = getItemValue(LCDNav);
                        do {
                            value = MenuNavInt(Buttons, value, MenuStr[LCDNav].Min, MenuStr[LCDNav].Max);
                        } while (value == EM_SENSORBOX || value == EM_API);
                        setItemValue(LCDNav, value);
                        break;
                    default:
                        value = getItemValue(LCDNav);
                        value = MenuNavInt(Buttons, value, MenuStr[LCDNav].Min, MenuStr[LCDNav].Max);
                        setItemValue(LCDNav, value);
                        break;
                }
            } else {
                LCDNav = MenuNavCharArray(Buttons, LCDNav, MenuItems, MenuItemsCount);
            }
            ButtonRelease = 1;
        // Repeat button after 0.5 second
        } else if (ButtonRelease == 2 && ButtonRepeat == 0) {
            ButtonRepeat = 500;
            ButtonTimer = millis() + ButtonRepeat;
        } else if (ButtonRepeat && millis() > ButtonTimer) {
            ButtonRelease = 0;
            if (ButtonRepeat > 1) {
                ButtonRepeat -= (ButtonRepeat / 8);
                ButtonTimer = millis() + ButtonRepeat;
            }
        }
    } else if (LCDNav > 1 && Buttons == 0x5 && ButtonRelease == 0) {            // Button 2 pressed?
        ButtonRelease = 1;
        if (SubMenu) {                                                          // We are currently in Submenu
            SubMenu = 0;                                                        // Exit Submenu now
            if (LCDNav == MENU_CAL) {                                           // Exit CT1 calibration?
                ICal = ((unsigned long)CT1 * 10 + 5) * ICAL / Iuncal;           // Calculate new Calibration value
                Irms[0] = CT1;                                                  // Set the Irms value, so the LCD update is instant
            }
        } else {                                                                // We are currently not in Submenu.
            SubMenu = 1;                                                        // Enter Submenu now
            if (LCDNav == MENU_CAL) {                                           // CT1 calibration start
                CT1 = (unsigned int) abs(Irms[0]);                              // make working copy of CT1 value
            } else if (LCDNav == MENU_EXIT) {                                   // Exit Main Menu
                LCDNav = 0;
                SubMenu = 0;
                ErrorFlags = NO_ERROR;                                          // Clear All Errors when exiting the Main Menu
                TestState = 0;                                                  // Clear TestState
                ChargeDelay = 0;                                                // Clear ChargeDelay
                setSolarStopTimer(0);                                           // Disable Solar Timer
                GLCD();
                write_settings();                                               // Write to eeprom
                ButtonRelease = 2;                                              // Skip updating of the LCD 
            }
        }

    } else if (Buttons == 0x7) {                                                // Buttons released
        ButtonRelease = 0;
        ButtonRepeat = 0;
        delay(10);                                                              // debounce keys (blocking)
    }

    //
    // here we update the LCD
    //
    if ( ButtonRelease == 1 || LCDNav == 1) {
    
        if (LCDNav == 1) {
            glcd_clrln(0, 0x00);
            glcd_clrln(1, 0x04);                                                // horizontal line
            GLCD_print_buf2(2, (const char *) "Hold 2 sec");
            GLCD_print_buf2(4, (const char *) "for Menu");
            glcd_clrln(6, 0x10);                                                // horizontal line
            glcd_clrln(7, 0x00);

        } else {
            
            GLCD_print_menu(2, MenuStr[LCDNav].LCD);                            // add navigation arrows on both sides
            switch (LCDNav) {
                case MENU_CAL:
                    if (SubMenu) {
                        sprintf(Str, "%u.%uA", CT1 / 10, CT1 % 10);
                    } else {
                        sprintf(Str, "%u.%uA",((unsigned int) abs(Irms[0]) / 10), ((unsigned int) abs(Irms[0]) % 10) );
                    }
                    GLCD_print_menu(4, Str);
                    break;
                default:
                    GLCD_print_menu(4, getMenuItemOption(LCDNav));              // print Menu
                    break;
            }

            // Bottom row of the GLCD
            GLCD_buffer_clr();
            GLCD_write_buf_str(0, 0, (const char *) VERSION, GLCD_ALIGN_LEFT);// show software version in bottom right corner.
            sprintf(Str, "%u/%u", GetPosInMenu(MenuItemsCount), MenuItemsCount);                              // ° Degree symbol
            GLCD_write_buf_str(122, 0, Str, GLCD_ALIGN_RIGHT);                     // show the internal temperature
            GLCD_sendbuf(7, 1);
        }
        ButtonRelease = 2;                                                      // Set value to 2, so that LCD will be updated only once
    }


    ScrollTimer = millis();                                                     // reset timer for HelpMenu text
    LCDpos = 0;                                                                 // reset position of scrolling text
    OldButtonState = Buttons;
    LCDTimer = 0;
}


void GLCD_init(void) {
    delay(200);                                                                 // transients on the line could have garbled the LCD, wait 200ms then re-init.
    _A0_0;                                                                      // A0=0
    _RSTB_0;                                                                    // Reset GLCD module
    delayMicroseconds(4);
    _RSTB_1;                                                                    // Reset line high
    delayMicroseconds(4);
    
    st7565_command(0xA2);                                                       // (11) set bias at duty cycle 1.65 (0xA2=1.9 0xA3=1.6)
    st7565_command(0xA0);                                                       // (8) SEG direction (0xA0 or 0xA1)
    st7565_command(0xC8);                                                       // (15) comm direction normal =0xC0 comm reverse= 0xC8
   
    st7565_command(0x20 | 0x04);                                                // (17) set Regulation Ratio (0-7)

    st7565_command(0xF8);                                                       // (19) send Booster command
    st7565_command(0x01);                                                       // set Booster value 00=4x 01=5x

    st7565_command(0x81);                                                       // (18) send Electronic Volume command 0x81
    st7565_command(0x24);                                                       // set Electronic volume (0x00-0x3f)

    st7565_command(0xA6);                                                       // (9) Inverse display (0xA7=inverse 0xA6=normal)
    st7565_command(0xA4);                                                       // (10) ALL pixel on (A4=normal, A5=all ON)

    st7565_command(0x28 | 0x07);                                                // (16) ALL Power Control ON

    glcd_clear();                                                               // clear internal GLCD buffer
    goto_row(0x00);                                                             // (3) Set page address
    goto_col(0x00);                                                             // (4) Set column addr LSB
 
    st7565_command(0xAF);                                                       // (1) ON command

}


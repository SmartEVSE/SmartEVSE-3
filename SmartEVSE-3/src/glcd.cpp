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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SPI.h>
#include <WiFi.h>
#include "esp32.h"
#include "glcd.h"
#include "utils.h"
#include "meter.h"
#include "network_common.h"
#include "font.cpp"
#include "font2.cpp"
#include <MicroOcpp.h>

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
0x00, 0x00, 0x80, 0x80, 0x40, 0x40, 0x20, 0x10, 0x10, 0x08, 0x08, 0x04, 0x04, 0x84, 0xC4, 0xE4, 
0x94, 0x84, 0x84, 0x04, 0x08, 0x10, 0x20, 0x40, 0x40, 0x40, 0x40, 0x80, 0x80, 0x80, 0x00, 0x00, 
0x00, 0x00, 0x00, 0x40, 0x60, 0x30, 0x18, 0x0C, 0x07, 0x05, 0x04, 0x04, 0x07, 0x0C, 0x18, 0x30, 
0x68, 0x48, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 
0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 
0x08, 0x08, 0x00, 0x7F, 0x40, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x40, 0x40, 0x40, 0x7F, 0x40, 
0x40, 0x40, 0x42, 0x40, 0x7F, 0x40, 0x40, 0x7F, 0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 
0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 
0x00, 0x0F, 0x10, 0x10, 0x00, 0x18, 0x24, 0x5A, 0x5A, 0x24, 0x18, 0x00, 0x10, 0x10, 0x10, 0x14, 
0x13, 0x11, 0x10, 0x10, 0x10, 0x10, 0x00, 0x18, 0x24, 0x5A, 0x5A, 0x24, 0x18, 0x00, 0x11, 0x0E
};

uint8_t LCDpos = 0;
bool LCDToggle = false;                                                         // Toggle display between two values
unsigned char LCDText = 0;                                                      // Cycle through text messages
unsigned int GLCDx, GLCDy;
uint8_t GLCDbuf[512];                                                           // GLCD buffer (half of the display)
uint8_t GLCDbuf2[1024];                                                         // Buffer that mirrors the complete LCD.    
tm DelayedStartTimeTM;
time_t DelayedStartTime_Old;
uint8_t MenuItems[MENU_EXIT];
uint8_t GridActive = 0;                                                         // When the CT's are used on Sensorbox2, it enables the GRID menu option.
uint32_t ScrollTimer = 0;

extern void CheckSwitch(bool force = false);
extern void handleWIFImode(void);
extern Button ExtSwitch;
extern String APpassword;
unsigned char activeRow;
extern Switch_Phase_t Switching_Phases_C2;
extern uint8_t RCMTestCounter;

void st7565_command(unsigned char data) {
    _A0_0;
    if (EthPresent) {
        etherlcd_lcd_transfer(data);
    } else {
        SPI.transfer(data);
    }
}

void st7565_data(unsigned char data) {
    _A0_1;
    if (EthPresent) {
        etherlcd_lcd_transfer(data);
    } else {
        SPI.transfer(data);
    }
}

void st7565_data_buf(const uint8_t *buf, size_t len) {
    _A0_1;
    if (EthPresent) {
        etherlcd_lcd_transfer_buf(buf, len);
    } else {
        SPI.writeBytes(buf, len);
    }
}

void goto_row(unsigned char y) {
    unsigned char pattern;
    pattern = 0xB0 | (y & 0xBF);                                                // put row address on data port set command
    st7565_command(pattern);
    activeRow = y;
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
    uint8_t linebuf[128];
    memset(linebuf, data, 128);
    goto_xy(0, ln);
    st7565_data_buf(linebuf, 128);
    memcpy(&GLCDbuf2[activeRow * 128], linebuf, 128);
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
    unsigned char y = 0;
    unsigned int x = 0;

    do {
        goto_xy(0, RowAdr + y);
        // Send entire 128-byte row in one bulk SPI transfer.
        st7565_data_buf(&GLCDbuf[x], 128);
        memcpy(&GLCDbuf2[activeRow * 128], &GLCDbuf[x], 128);              // Also update buffer copy
        x += 128;
    } while (++y < Rows);
}

void GLCD_font_condense(unsigned char c, unsigned char *start, unsigned char *end, unsigned char space) {
    if(c >= '0' && c <= '9') return;
    if(c == ' ' && space) return;
    if(font[c][0] == 0) {
        if(font[c][1] == 0) *start = 2;
        else *start = 1;
    }
    if(font[c][4] == 0) {
        if(font[c][3] == 0) *end = 3;
        else *end = 4;
    }
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
        //length += font2[(unsigned char)str[i]][0] + 2;
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
  if (ScrollTimer + 5000 < millis()) {
    unsigned int x = strlen(MenuStr[LCDNav].Desc);
    GLCD_print_buf2_left(MenuStr[LCDNav].Desc + LCDpos);

    if (LCDpos++ == 0) ScrollTimer = millis() - 4000;
    else if (LCDpos > (x - 10)) {
        ScrollTimer = millis() - 3000;
        LCDpos = 0;
    } else ScrollTimer = millis() - 4700;
  }
}


// print starting time when delayed charging
#define STRLEN 26
void printStartingTime(char *Str) {
    GLCD_print_buf2(2, (const char *) "STARTING @");
#define _24H 24*60*60
#define _WEEK 7*_24H
    String StrFormat;
    if (DelayedStartTime.diff <= _24H)
        //if it starts in the next 24 hours, just print hours : minutes
        StrFormat = "%R";
    else {
        //if it starts in the next week, print day of week, day of month, hours: minutes
        if (DelayedStartTime.diff <= _WEEK)
            StrFormat = "%a %e %R";
        else
        //if it starts later, print day of week, day of month, month, year perhaps scrolling hours/minutes?
            StrFormat = "%a %e %b";
            //StrFormat = "%a %e %b '%C %R";
    }
    if (DelayedStartTime.epoch2 && LocalTimeSet && DelayedStartTime.epoch2 != DelayedStartTime_Old) {
        time_t epoch = DelayedStartTime.epoch2 + EPOCH2_OFFSET;
        struct tm epoch_tm;
        DelayedStartTimeTM = *localtime_r(&epoch, &epoch_tm);
    }
    if (!strftime(Str, STRLEN, StrFormat.c_str(), &DelayedStartTimeTM))
        sprintf(Str, "later...");
    GLCD_print_buf2(4, Str);
    //print current time
    if (LocalTimeSet) {
        GLCD_buffer_clr();
        if (strftime(Str, STRLEN, "%a %e %b '%y %R", &timeinfo))
            GLCD_write_buf_str(0,0, Str, GLCD_ALIGN_LEFT);
        GLCD_sendbuf(7, 1);
    }
}

// called once a second
void GLCD(void) {
    unsigned char x;
    unsigned int seconds, minutes;
    static unsigned char energy_mains = 20; // X position
    static unsigned char energy_ev = 74; // X position
    char Str[STRLEN];
    LCDTimer++;
    
    if (LCDNav) {
        GLCD_buffer_clr();
        // top line
        if (LCDNav == MENU_RFIDREADER && SubMenu) {
            if (RFIDstatus == 2) GLCD_write_buf_str(0, 0, "Card Stored", GLCD_ALIGN_LEFT);
            else if (RFIDstatus == 3) GLCD_write_buf_str(0, 0, "Card Deleted", GLCD_ALIGN_LEFT);
            else if (RFIDstatus == 4) GLCD_write_buf_str(0, 0, "Card already stored!", GLCD_ALIGN_LEFT);
            else if (RFIDstatus == 5) GLCD_write_buf_str(0, 0, "Card not in storage!", GLCD_ALIGN_LEFT);
            else if (RFIDstatus == 6) GLCD_write_buf_str(0, 0, "Card storage full!", GLCD_ALIGN_LEFT);
            else glcd_clrln(0, 0x00);                                           // Clear line
            LCDTimer = 0;                                                       // reset timer, so it will not exit the menu when learning/deleting cards
        // Sensorbox 2 WiFi settings
        } else if (LCDNav == MENU_SB2_WIFI) {
            // wait for sensorbox to switch to selected mode
            if ( (SB2.WIFImode != SB2_WIFImode) && SubMenu && SB2_WIFImode !=2) {
                GLCD_write_buf_str(0,0, "one moment please...", GLCD_ALIGN_LEFT);

            } else if (SB2_WIFImode == 1) {         // Enable WiFi on Sensorbox 2

                if (SB2.WiFiConnected) {
                    sprintf(Str, "SB2: %u.%u.%u.%u",SB2.IP[0],SB2.IP[1],SB2.IP[2],SB2.IP[3]);
                    GLCD_write_buf_str(0,0, Str, GLCD_ALIGN_LEFT);
                } else GLCD_write_buf_str(0,0, "Not connected to WiFi", GLCD_ALIGN_LEFT);

            } else if (SB2_WIFImode == 2) {         // Enable Portal on Sensorbox 2
                if (SubMenu) {
                    sprintf(Str, "O button starts portal");
                    GLCD_write_buf_str(0,0, Str, GLCD_ALIGN_LEFT);
                } else {
                    // Show Portal Password
                    sprintf(Str, "Portal PW: %s", SB2.APpassword);
                    GLCD_write_buf_str(0,0, Str, GLCD_ALIGN_LEFT);
                }
                LCDTimer = 0;                                                       // reset timer, so it will not exit the menu when setting up WiFi
            }        
        // Show extra Contactor 2 information on top row
        } else if (LCDNav == MENU_C2 && SubMenu) {
            if (EnableC2 == NOT_PRESENT)     GLCD_write_buf_str(0, 0, "Three-phase Charging", GLCD_ALIGN_LEFT);
            else if (EnableC2 == ALWAYS_OFF) GLCD_write_buf_str(0, 0, "Single-phase Charging", GLCD_ALIGN_LEFT);
            else if (EnableC2 == SOLAR_OFF)  GLCD_write_buf_str(0, 0, "Solar 1P - Smart 3P", GLCD_ALIGN_LEFT);
            else if (EnableC2 == ALWAYS_ON)  GLCD_write_buf_str(0, 0, "Three-phase Charging", GLCD_ALIGN_LEFT);
            else if (EnableC2 == AUTO)       GLCD_write_buf_str(0, 0, "Auto 3P <> 1P Charging", GLCD_ALIGN_LEFT);
        } else if (LCDNav == MENU_PAIRING && SubMenu) {
            sprintf(Str, "SmartEVSE-%u", serialnr);
            GLCD_write_buf_str(0,0, Str, GLCD_ALIGN_LEFT);
        } else if (LCDNav == MENU_APPSERVER && SubMenu) {
            if (MQTTclientSmartEVSE.connected) GLCD_write_buf_str(0, 0, "Connected to server", GLCD_ALIGN_LEFT);
            else GLCD_write_buf_str(0, 0, "No server connection", GLCD_ALIGN_LEFT);
        } else {
            // Display IP and time in top row (Ethernet takes priority over WiFi)
            uint8_t WIFImode = getItemValue(MENU_WIFI);
            if (EthHasIP) {
                if (LCDNav == MENU_WIFI && WIFImode != 0) {
                    GLCD_write_buf_str(0,0, "Disconnect Eth first", GLCD_ALIGN_LEFT);
                } else {
                    sprintf(Str, "%s", ch390_get_ip());
                    GLCD_write_buf_str(0,0, Str, GLCD_ALIGN_LEFT);
                    if (LocalTimeSet) sprintf(Str, "%02u:%02u",timeinfo.tm_hour, timeinfo.tm_min);
                    else sprintf(Str, "--:--");
                    GLCD_write_buf_str(127,0, Str, GLCD_ALIGN_RIGHT);
                }
            } else if (WIFImode == 1 ) {   // Wifi Enabled

                if (WiFi.status() == WL_CONNECTED) {
                    sprintf(Str, "%s",WiFi.localIP().toString().c_str());
                    GLCD_write_buf_str(0,0, Str, GLCD_ALIGN_LEFT);
                    if (LocalTimeSet) sprintf(Str, "%02u:%02u",timeinfo.tm_hour, timeinfo.tm_min);
                    else sprintf(Str, "--:--");
                    GLCD_write_buf_str(127,0, Str, GLCD_ALIGN_RIGHT);
                } else GLCD_write_buf_str(0,0, "Not connected to WiFi", GLCD_ALIGN_LEFT);

            // When Wifi Setup is selected, show password and SSID of the Access Point
            } else if (WIFImode == 2) {
                if (SubMenu && WiFi.getMode() != WIFI_AP_STA) {           // Do not show if AP_STA mode is started
                    sprintf(Str, "O button starts portal");
                    GLCD_write_buf_str(0,0, Str, GLCD_ALIGN_LEFT);
                } else {
                    sprintf(Str, "Portal PW: %s", APpassword.c_str());
                    GLCD_write_buf_str(0,0, Str, GLCD_ALIGN_LEFT);
                }
                LCDTimer = 0;                                                   // reset timer, so we will not exit the menu while setting up WiFi
            }
        }
        // update LCD
        GLCD_sendbuf(0, 1);

        if (LCDTimer > 120) {
            LCDNav = 0;                                                         // Exit Setup menu after 120 seconds.
            PairingPin = "";                                                    // Reset PairingPin when exiting menu.
            read_settings();                                                    // don't save, but restore settings
        } else return;                                                          // disable LCD status messages when navigating LCD Menu
    } // if LCDNav
   
    if (LCDTimer == 1) {
        LCDText = 0;
    } else if (LCDTimer > 4) {
        LCDTimer = 1;
        LCDToggle = 1u - LCDToggle;
        LCDText++;
    }

    if (ErrorFlags) {                                                           // We switch backlight on, as we exit after displaying the error
        if (ErrorFlags & ~LESS_6A) BacklightTimer = BACKLIGHT;                  // Backlight timer is set to 120 seconds, except while waiting for enough (solar) power

        if (ErrorFlags & (CT_NOCOMM | EV_NOCOMM | CIRCUIT_NOCOMM)) {                             // No serial communication for 10 seconds
            if (ErrorFlags & EV_NOCOMM) {
                GLCD_print_buf2(0, (const char *) "CAN'T READ");
                GLCD_print_buf2(2, (const char *) "EV METER");
            } else if (ErrorFlags & CIRCUIT_NOCOMM) {
                GLCD_print_buf2(0, (const char *) "CAN'T READ");
                GLCD_print_buf2(2, (const char *) "CIRCUIT METER");
            } else if (MainsMeter.Type == EM_API || MainsMeter.Type == EM_HOMEWIZARD) {
                GLCD_print_buf2(0, (const char *) "CAN'T READ");
                GLCD_print_buf2(2, (const char *) "MAINS METER");
            } else {
                GLCD_print_buf2(0, (const char *) "ERROR NO");
                GLCD_print_buf2(2, (const char *) "SERIAL COM");
            }           
            GLCD_print_buf2(4, (const char *) "CHECK CFG");
            GLCD_print_buf2(6, (const char *) "OR WIRING");
            return;
        } else if (ErrorFlags & TEMP_HIGH) {                                    // Temperature reached 65C
            GLCD_print_buf2(0, (const char *) "HIGH TEMP");
            GLCD_print_buf2(2, (const char *) "ERROR");
            GLCD_print_buf2(4, (const char *) "CHARGING");
            GLCD_print_buf2(6, (const char *) "STOPPED");
            return;
        } else if ((ErrorFlags & RCM_TRIPPED) && !(ErrorFlags & RCM_TEST)) {    // Residual Current Sensor tripped
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
        }
    }   // end of ERROR()                                                       // more specific error handling in the code below

    if (TestState == 80) {                                                      // Only used when testing the module
        GLCD_print_buf2(2, (const char *) "IO Test");
        GLCD_print_buf2(4, (const char *) "Passed");
        return;
    }

                                                                                // MODE NORMAL
    if (Mode == MODE_NORMAL || AccessStatus == OFF) {

        glcd_clrln(0, 0x00);
        glcd_clrln(1, 0x04);                                                    // horizontal line
        glcd_clrln(6, 0x10);                                                    // horizontal line
        glcd_clrln(7, 0x00);

        if (OcppMode && 
                (getItemValue(MENU_RFIDREADER) == 6 || getItemValue(MENU_RFIDREADER) == 0) && // RFID in OCPP mode or disabled
                ocppHasTxNotification()) {                                      // There is an OCPP event to display
            BacklightTimer = BACKLIGHT;
            switch(ocppGetTxNotification()) {
                case MicroOcpp::TxNotification::Authorized:
                    GLCD_print_buf2(2, (const char *) "ACCEPTED");
                    GLCD_print_buf2(4, (const char *) "RFID CARD");
                    break;
                case MicroOcpp::TxNotification::AuthorizationRejected:
                case MicroOcpp::TxNotification::DeAuthorized:
                    GLCD_print_buf2(2, (const char *) "INVALID");
                    GLCD_print_buf2(4, (const char *) "RFID CARD");
                    break;
                case MicroOcpp::TxNotification::AuthorizationTimeout:
                    GLCD_print_buf2(2, (const char *) "OFFLINE");
                    GLCD_print_buf2(4, (const char *) "TRY LATER");
                    break;
                case MicroOcpp::TxNotification::ReservationConflict:
                    GLCD_print_buf2(2, (const char *) "BLOCKED BY");
                    GLCD_print_buf2(4, (const char *) "RESERVATION");
                    break;
                case MicroOcpp::TxNotification::ConnectionTimeout:
                    GLCD_print_buf2(2, (const char *) "TIMEOUT");
                    GLCD_print_buf2(4, (const char *) "TRY AGAIN");
                    break;
                case MicroOcpp::TxNotification::RemoteStart:
                    if (!ocppIsConnectorPlugged()) {
                        GLCD_print_buf2(2, (const char *) "PLUG IN");
                        GLCD_print_buf2(4, (const char *) "VEHICLE");
                    }
                    break;
                case MicroOcpp::TxNotification::StartTx:
                    GLCD_print_buf2(2, (const char *) "STARTED");
                    GLCD_print_buf2(4, (const char *) "TRANSACTION");
                    break;
                case MicroOcpp::TxNotification::StopTx:
                    GLCD_print_buf2(2, (const char *) "STOPPED");
                    GLCD_print_buf2(4, (const char *) "TRANSACTION");
                    break;
                default:
                    break;
            }
        } else if (ErrorFlags & LESS_6A && AccessStatus == ON) {
            GLCD_print_buf2(2, (const char *) "WAITING");
            GLCD_print_buf2(4, (const char *) "FOR POWER");
#if MODEM
        } else if (State == STATE_MODEM_REQUEST || State == STATE_MODEM_WAIT || State == STATE_MODEM_DONE) {                                          // Modem states

            BacklightTimer = BACKLIGHT;

            GLCD_print_buf2(2, (const char *) "MODEM");
            GLCD_print_buf2(4, (const char *) "COMM");
        } else if (State == STATE_MODEM_DENIED) {                               // Modem denied state

            BacklightTimer = BACKLIGHT;

            GLCD_print_buf2(2, (const char *) "MODEM");
            GLCD_print_buf2(4, (const char *) "DENIED");
#endif
        } else if (State == STATE_C) {                                          // STATE C
            
            BacklightTimer = BACKLIGHT;
            
            if (GridRelayOpen)
                GLCD_print_buf2(2, (const char *) "LIMITED");
            else
                GLCD_print_buf2(2, (const char *) "CHARGING");
            sprintf(Str, "%u.%uA",Balanced[0] / 10, Balanced[0] % 10);
            GLCD_print_buf2(4, Str);
        } else {                                                                // STATE A and STATE B
            if (AccessStatus == ON) {
                GLCD_print_buf2(2, (const char *) "READY TO");
                sprintf(Str, "CHARGE %u", ChargeDelay);
                if (ChargeDelay) {
                    // BacklightTimer = BACKLIGHT;
                } else Str[6] = '\0';
                GLCD_print_buf2(4, Str);
            } else if (AccessStatus == PAUSE) {
                GLCD_print_buf2(2, (const char *) "PAUSE");
            } else {
                if (OcppMode &&                                  // OCPP enabled
                        (getItemValue(MENU_RFIDREADER) == 6 || getItemValue(MENU_RFIDREADER) == 0)) { // RFID in OCPP mode or disabled
                    switch (getChargePointStatus()) {
                        case ChargePointStatus_Available:
                            GLCD_print_buf2(2, (const char *) "AVAILABLE");
                            GLCD_print_buf2(4, (const char *) "");
                            break;
                        case ChargePointStatus_Preparing:
                            if (!ocppIsConnectorPlugged()) {
                                GLCD_print_buf2(2, (const char *) "PLUG IN");
                                GLCD_print_buf2(4, (const char *) "VEHICLE");
                            } else {
                                GLCD_print_buf2(2, (const char *) "PLEASE");
                                GLCD_print_buf2(4, (const char *) "AUTHORIZE");
                            }
                            break;
                        case ChargePointStatus_Charging:
                        case ChargePointStatus_SuspendedEVSE:
                        case ChargePointStatus_SuspendedEV:
                            if (DelayedStartTime.epoch2) {
                                printStartingTime(Str);
                            } else {
                                GLCD_print_buf2(2, (const char *) "CHARGING");
                                GLCD_print_buf2(4, (const char *) "IN PROGRESS");
                            }
                            break;
                        case ChargePointStatus_Finishing:
                            if (ocppLockingTxDefined()) {
                                GLCD_print_buf2(2, (const char *) "UNLOCK BY");
                                GLCD_print_buf2(4, (const char *) "RFID CARD");
                            } else {
                                GLCD_print_buf2(2, (const char *) "FINISHED");
                                GLCD_print_buf2(4, (const char *) "CHARGING");
                            }
                            break;
                        case ChargePointStatus_Reserved:
                            GLCD_print_buf2(2, (const char *) "RESERVED");
                            GLCD_print_buf2(4, (const char *) "");
                            break;
                        case ChargePointStatus_Unavailable:
                            GLCD_print_buf2(2, (const char *) "OUT OF");
                            GLCD_print_buf2(4, (const char *) "ORDER");
                            break;
                        case ChargePointStatus_Faulted:
                            GLCD_print_buf2(2, (const char *) "NO SERVICE");
                            GLCD_print_buf2(4, (const char *) "");
                            break;
                        default:
                            break;
                    }
                } else if (getItemValue(MENU_RFIDREADER)) {
                    if (RFIDstatus == 7) {
                        GLCD_print_buf2(2, (const char *) "INVALID");
                        GLCD_print_buf2(4, (const char *) "RFID CARD");
                    } else {
                        GLCD_print_buf2(2, (const char *) "PRESENT");
                        GLCD_print_buf2(4, (const char *) "RFID CARD");
                    }
                } else {
                    if (DelayedStartTime.epoch2) {
                        printStartingTime(Str);
                    } else {
                        GLCD_print_buf2(2, (const char *) "ACCESS");
                        GLCD_print_buf2(4, (const char *) "DENIED");
                    }
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
        if (MaxSumMainsTimer) {                                                 // When Capacity limit is active, change Mains energy line
            for (x=18; x<48; x+=4) {                                            // ______ -> _ _ _ _ 
                GLCDbuf[x+(128*3)] = 0;
                GLCDbuf[x+(128*3)+1] = 0;
            }
        }
        if (SolarStopTimer || MaxSumMainsTimer) {                               // display remaining time before charging is stopped
            if (SolarStopTimer != 0 && (MaxSumMainsTimer == 0 || SolarStopTimer < MaxSumMainsTimer)) {
                seconds = SolarStopTimer;
            } else {                                                            // use either SolarStopTimer or MaxSumMainsTimer, whichever is
                seconds = MaxSumMainsTimer;
            }              
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

        if (EVMeter.Type) {                                                     // If we have a EV kWh meter configured, Show total charged energy in kWh on LCD.
            sprintfl(Str, "%2d.%1dkWh", EVMeter.EnergyCharged, 3, 1);           // Will reset to 0.0kWh when charging cable reconnected, and state change from STATE B->C
            GLCD_write_buf_str(89, 1, Str,GLCD_ALIGN_LEFT);                     // print to buffer
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

            if (LCDToggle && EVMeter.Type) {
                if (EVMeter.PowerMeasured < 9950) {
                    sprintfl(Str, "%1d.%1dkW", EVMeter.PowerMeasured, 3, 1);
                } else {
                    sprintfl(Str, "%dkW", EVMeter.PowerMeasured, 3, 0);
                }
            } else {
                sprintfl(Str, "%uA", Balanced[0], 1, 0);
            }
            GLCD_write_buf_str(85, 2, Str, GLCD_ALIGN_CENTER);
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
                sprintfl(Str, "%dA", MainsMeter.Irms[x], 1, 0);
                GLCD_write_buf_str(46, x, Str, GLCD_ALIGN_RIGHT);               // print to buffer
            }
        }
        GLCD_sendbuf(0, 4);                                                     // Copy LCD buffer to GLCD

        glcd_clrln(4, 0);                                                       // Clear line 4
        if (ErrorFlags & LESS_6A) {
            if (!LCDToggle) {
                GLCD_print_buf2(5, (const char *) "WAITING");
            } else {
                if (Mode == MODE_SMART) {
                    GLCD_print_buf2(5, (const char *) "FOR POWER");
                } else {
                    GLCD_print_buf2(5, (const char *) "FOR SOLAR");
                }
            }

#if MODEM
        } else if (State == STATE_MODEM_REQUEST || State == STATE_MODEM_WAIT || State == STATE_MODEM_DONE) {                                          // Modem states
            GLCD_print_buf2(5, (const char *) "MODEM");
#endif
        } else if (AccessStatus == PAUSE) {
                    GLCD_print_buf2(5, "PAUSE");
        } else if (State != STATE_C) {
                switch (Switching_Phases_C2) {
                    case NO_SWITCH:
                        sprintf(Str, "READY %u", ChargeDelay);
                        if (!ChargeDelay) Str[5] = '\0';
                        break;
                    case GOING_TO_SWITCH_1P:
                        sprintf(Str, "3P -> 1P %u", ChargeDelay);
                        if (!ChargeDelay) Str[8] = '\0';
                        break;
                    case GOING_TO_SWITCH_3P:
                        sprintf(Str, "1P -> 3P %u", ChargeDelay);
                        if (!ChargeDelay) Str[8] = '\0';
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
                            sprintf(Str+5," %uP", Nr_Of_Phases_Charging);
                        GLCD_print_buf2(5, Str);
                        break;
                    } else LCDText++;
                    // fall through
                case 1:
                    if (GridRelayOpen) {
                        GLCD_print_buf2(5, (const char *) "LIMITED");
                        break;
                    } else LCDText++;
                    // fall through
                case 2:
                    GLCD_print_buf2(5, (const char *) "CHARGING");
                    break;
                case 3:
                    if (EVMeter.Type) {
                        sprintfl(Str, "%d.%01d kW", EVMeter.PowerMeasured, 3, 1);
                        GLCD_print_buf2(5, Str);
                        break;
                    } else LCDText++;
                    // fall through
                case 4:
                    if (EVMeter.Type) {
                        sprintfl(Str, "%d.%02d kWh", EVMeter.EnergyCharged, 3, 2);
                        GLCD_print_buf2(5, Str);
                        break;
                    } else LCDText++;
                    // fall through
                case 5:
                    sprintf(Str, "%u.%u A", Balanced[0] / 10, Balanced[0] % 10);
                    GLCD_print_buf2(5, Str);
                    break;
            }
        }
        glcd_clrln(7, 0x00);
    } // End Mode SMART or SOLAR

}


/**
 * Get active option of an menu item
 *
 * @param uint8_t nav
 * @return uint8_t[] MenuItemOption
 */
static const char *getMeterHostStr(const Meter &meter, uint8_t value) {
    static char Str[12]; // must be declared static, since it's referenced outside of function scope
    if (isMDNSDiscoveryInProgress()) { // If mDNS discovery is in progress, show "Discovering" for all options in the menu
        return "Discovering";
    }
    if (value == 0) { //Manual discovery option selected, show "Discover" if discovery is not in progress, otherwise show "Discovering"
        return "Discover";

    }
    else if (value == 1) { //Currently stored host entry selected, show the host name stored for this meter, or "Not Set" if it's empty
        if (strlen(meter.DeviceHostName) == 0) return "Not Set";
        compileServiceName(meter.Type, meter.DeviceHostName, Str, sizeof(Str));
        if (Str[0] == '\0') return "Not Set";
        return Str;
    }
    else{   //Discovered mDNS service selected ( value >= 2 ), show the host name of the selected mDNS entry
        const mDNSServiceEntry *service = getCompatiblemDNSServiceByIndex(meter.Type, value - 2);
        if (service && !service->HostName.isEmpty()) {
            compileServiceName(meter.Type, service->HostName.c_str(), Str, sizeof(Str));
            if (Str[0] != '\0') {
                return Str;
            }
        }
        return "Not Set";
    }
}

/**
 * Commit the selected host entry for one meter menu.
 * This only updates the meter that was actually edited, and clears that meter's
 * pending menu selection so the LCD menu returns to its neutral state.
 * 
 * @param Meter &meter
 * @param uint8_t menu selection index (0 for "Discover",1 for "Unchanged", 2 for first mDNS entry, etc.)
 * @return void
 */
static void commitMeterHostSelection(Meter &meter, uint8_t value) {
    // If discovery is in progress, ignore any selection and just clear the pending menu selection, since the user should wait until discovery is finished to make a selection
    if (isMDNSDiscoveryInProgress()) {
        meter.HostMenuSelection = 1;
        return;
    }
    // If the user selected an mDNS entry (value >= 2) attempt to update the meter's DeviceHostName
    if (value >= 2) {
        const mDNSServiceEntry *service = getCompatiblemDNSServiceByIndex(meter.Type, value - 2);
        if (service && !service->HostName.isEmpty()) {
            strncpy(meter.DeviceHostName, service->HostName.c_str(), sizeof(meter.DeviceHostName));
            meter.DeviceHostName[sizeof(meter.DeviceHostName) - 1] = '\0';
        }
    }
    // If the user selected "Discover" (value == 0), start mDNS discovery
    if (value == 0) {
        clearmDNSServices();
    }

    meter.HostMenuSelection = 1;
}

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
    const static char StrSwitch[8][11] = {"Disabled", "Access B", "Access S", "Sma-Sol B", "Sma-Sol S", "Grid Relay", "Custom B", "Custom S"};
    const static char StrGrid[2][10] = {"4Wire", "3Wire"};
    const static char StrEnabled[] = "Enabled";
    const static char StrExitMenu[] = "MENU";
    extern const char StrRFIDReader[7][10];
    const static char StrWiFi[3][10] = {"Disabled", "Enabled", "SetupWifi"};
    const static char StrCapMode[][9] = {"Disabled", "Fixed", "Interval", "Flanders"};

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
        case MENU_CAPACITY_MODE:
            return StrCapMode[value];
        case MENU_SUMMAINSTIME:
        case MENU_STOP:
            if (value) {
                sprintf(Str, "%2u min", value);
                return Str;
            } else return StrDisabled;
        case MENU_LOADBL:
            return StrLoadBl[LoadBl]; //TODO shouldnt this be [value] ?
        case MENU_SUMMAINS:
            if (value)
                sprintf(Str, "%2u A", value);
            else
                sprintf(Str, "Disabled");
            return Str;
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
        case MENU_PAIRING:
            if (SubMenu) {
                uint32_t tempPin = random(1, 1000000);                                 // generate random PIN 1-999999 when selecting sub menu
                sprintf(Str, "%06u", tempPin);
                PairingPin = Str;
            } else {
                PairingPin = "";
                sprintf(Str, "Create PIN");
            }    
            return Str;
        case MENU_APPSERVER:        
        case MENU_AUTOUPDATE:
        case MENU_RCMON:
            if (value) return StrEnabled;
            else return StrDisabled;
        case MENU_MAINSMETER:
        case MENU_EVMETER:
        case MENU_CIRCUITMETER:
            return (const char*)EMConfig[value].Desc;
        case MENU_GRID:
            return StrGrid[value];
        case MENU_LCDPIN:
            sprintf(Str, "%04u", value);
            return Str;
        case MENU_EVMETERHOST:
            return getMeterHostStr(EVMeter, value);
        case MENU_MAINSMETERHOST:
            return getMeterHostStr(MainsMeter, value);
        case MENU_CIRCUITMETERHOST:
            return getMeterHostStr(CircuitMeter, value);
        case MENU_MAINSMETERADDRESS:
        case MENU_EVMETERADDRESS:
        case MENU_CIRCUITMETERADDRESS:
        case MENU_EMCUSTOM_UREGISTER:
        case MENU_EMCUSTOM_IREGISTER:
        case MENU_EMCUSTOM_PREGISTER:
        case MENU_EMCUSTOM_EREGISTER:
            if(value < 0x1000) sprintf(Str, "%u (%02X)", value, value);     // This just fits on the LCD.
            else sprintf(Str, "%u %X", value, value);
            return Str;
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
            // fall through
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
        case MENU_SB2_WIFI:
            return StrWiFi[value];
        case MENU_LEDMODE:
            if (value) return "Public";
            else return "Standard";
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

    MenuItems[m++] = MENU_MODE;                                                 // EVSE mode (0:Normal / 1:Smart / 2: Solar)
    MenuItems[m++] = MENU_CONFIG;                                               // Configuration (0:Socket / 1:Fixed Cable)
    if (!getItemValue(MENU_CONFIG)) {                                                              // ? Fixed Cable?
        MenuItems[m++] = MENU_LOCK;                                             // - Cable lock (0:Disable / 1:Solenoid / 2:Motor)
    }
    MenuItems[m++] = MENU_LOADBL;                                               // Load Balance Setting (0:Disable / 1:Master / 2-8:Node)
    if (Mode) {                                                                 // ? Smart or Solar mode?
        if (LoadBl < 2) {                                                       // - ? Load Balancing Disabled/Master?
            MenuItems[m++] = MENU_MAINSMETER;                                   // - - Type of Mains electric meter (0: Disabled / Constants EM_*)
            if (MainsMeter.Type == EM_SENSORBOX) {                              // - - ? Sensorbox?
                if (GridActive == 1) MenuItems[m++] = MENU_GRID;
                if (SB2.SoftwareVer == 0x01 || SB2.SoftwareVer == 0x03) {
                    MenuItems[m++] = MENU_SB2_WIFI;                             // Sensorbox-2 Wifi  0:Disabled / 1:Enabled / 2:Portal
                }
            } else if (MainsMeter.Type && MainsMeter.Type != EM_API && MainsMeter.Type != EM_HOMEWIZARD) { // - - ? Other?
                MenuItems[m++] = MENU_MAINSMETERADDRESS;                        // - - - Address of Mains electric meter (9 - 247)
            }
            if (MainsMeter.Type && MainsMeter.Type == EM_HOMEWIZARD) {                           // - ? EV meter configured?
                MenuItems[m++] = MENU_MAINSMETERHOST;                      // - - Device hostname selector for EV electric meter (0: current / 1-9 discovered hostnames)
            }
            MenuItems[m++] = MENU_CIRCUITMETER;                                 // - - Type of Circuit electric meter (0: Disabled / Constants EM_*)
            if (CircuitMeter.Type && CircuitMeter.Type != EM_API) {             // - ? Circuit meter configured?
                MenuItems[m++] = MENU_CIRCUITMETERADDRESS;                      // - - Address of Circuit electric meter (9 - 247)
            }
            if (CircuitMeter.Type && CircuitMeter.Type == EM_HOMEWIZARD) {                           // - ? EV meter configured?
                MenuItems[m++] = MENU_CIRCUITMETERHOST;                      // - - Device hostname selector for EV electric meter (0: current / 1-9 discovered hostnames)
            }
            if (CircuitMeter.Type || LoadBl == 1)                               // old MaxCircuit behaviour without a CircuitMeter present: we will guard the max the Master gives out!
                MenuItems[m++] = MENU_CIRCUIT;                                          // - Max current of the EVSE circuit (A)
        }
        MenuItems[m++] = MENU_EVMETER;                                          // - Type of EV electric meter (0: Disabled / Constants EM_*)
        if (EVMeter.Type && EVMeter.Type != EM_API && EVMeter.Type != EM_HOMEWIZARD) {                           // - ? EV meter configured?
            MenuItems[m++] = MENU_EVMETERADDRESS;                               // - - Address of EV electric meter (9 - 247)
        }
        if (EVMeter.Type && EVMeter.Type == EM_HOMEWIZARD) {                           // - ? EV meter configured?
            MenuItems[m++] = MENU_EVMETERHOST;                      // - - Device hostname selector for EV electric meter (0: current / 1-9 discovered hostnames)
        }
        if (LoadBl < 2) {                                                       // - ? Load Balancing Disabled/Master?
            if (MainsMeter.Type == EM_CUSTOM || EVMeter.Type == EM_CUSTOM || CircuitMeter.Type == EM_CUSTOM) { // ? Custom electric meter used?
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
            if (MainsMeter.Type) {                                              // Mainsmeter is configured and Load Balancing Disabled/Master?
                MenuItems[m++] = MENU_MAINS;                                    // - Max Mains Amps (hard limit, limited by the MAINS connection) (A) (Mode:Smart/Solar)
                MenuItems[m++] = MENU_MIN;                                      // - Minimal current the EV is happy with (A) (Mode:Smart/Solar or LoadBl:Master)
            }
        }
    }
    MenuItems[m++] = MENU_MAX;                                                  // Max Charge current (A)
    if (Mode == MODE_SOLAR && LoadBl < 2) {                                     // ? Solar mode and Load Balancing Disabled/Master?
        MenuItems[m++] = MENU_START;                                            // - Start Surplus Current (A)
        MenuItems[m++] = MENU_STOP;                                             // - Stop time (min)
        MenuItems[m++] = MENU_IMPORT;                                           // - Import Current from Grid (A)
    }
    if (Mode != MODE_NORMAL)
        MenuItems[m++] = MENU_C2;
    MenuItems[m++] = MENU_SWITCH;                                               // External Switch on SW (0:Disable / 1:Access / 2:Smart-Solar)
    MenuItems[m++] = MENU_LEDMODE;                                              // LED mode (0:Standard / 1:Public charging station colors)
    MenuItems[m++] = MENU_RCMON;                                                // Residual Current Monitor on RCM (0:Disable / 1:Enable)
    MenuItems[m++] = MENU_RFIDREADER;                                           // RFID Reader connected to SW (0:Disable / 1:Enable / 2:Learn / 3:Delete / 4:Delate All)
    MenuItems[m++] = MENU_WIFI;                                                 // Wifi Disabled / Enabled / Portal
    if (getItemValue(MENU_WIFI)  == 1) {                                        // only show AutoUpdate menu if Wifi enabled
        MenuItems[m++] = MENU_AUTOUPDATE;                                       // Firmware automatic update Disabled / Enabled
        MenuItems[m++] = MENU_PAIRING;                                          // Generate PairingPin for SmartEVSE App
        MenuItems[m++] = MENU_APPSERVER;                                        // App Server (0:Disable / 1:Enable)
    }
    MenuItems[m++] = MENU_MAX_TEMP;
    if (MainsMeter.Type && LoadBl < 2) {
        MenuItems[m++] = MENU_CAPACITY_MODE;
        if (getItemValue(MENU_CAPACITY_MODE) == FIXED) {
            MenuItems[m++] = MENU_SUMMAINS;
            if (getItemValue(MENU_SUMMAINS) != 0)
                MenuItems[m++] = MENU_SUMMAINSTIME;
        }
    }
    MenuItems[m++] = MENU_LCDPIN;
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
    static uint16_t value, ButtonRepeat = 0;
    static uint8_t digit = 3;
    char Str[24];

    unsigned char MenuItemsCount = getMenuItems();

    // Main Menu Navigation
    BacklightTimer = BACKLIGHT;                                                 // delay before LCD backlight turns off.

    if ((LCDNav == 0) && (Buttons == 0x5) && (ButtonRelease == 0)) {            // Button 2 pressed ?
        LCDNav = MENU_ENTER;                                                    // about to enter menu
        ButtonTimer = millis();
    } else if (LCDNav == MENU_ENTER && ((ButtonTimer + 2000) < millis() )) {    // <CONFIG>
        LCDNav = MenuItems[0];                                                  // Main Menu entered
        ButtonRelease = 1;
    } else if ((LCDNav == MENU_ENTER) && (Buttons == 0x7)) {                    // Button 2 released before entering menu?
        LCDNav = 0;
        ButtonRelease = 0;
        GLCD();
    // stop charging if < button is pressed longer then 2 seconds
    } else if ((LCDNav == 0) && (Buttons == 0x6) && (ButtonRelease == 0)) {     // Button 1 pressed ?
        LCDNav = MENU_OFF;                                                      // about to cancel charging
        ButtonTimer = millis();
    } else if (LCDNav == MENU_OFF && ((ButtonTimer + 2000) < millis() )) {
        LCDNav = 0;                                                             // Charging canceled
        setAccess(OFF);
        ButtonRelease = 1;
    } else if ((LCDNav == MENU_OFF) && (Buttons == 0x7)) {                      // Button 1 released before entering menu?
        //if < button is pressed shorter then 2 seconds we are switching from Smart mode to Solar mode and vice versa
        if (Mode)
            setMode(~Mode & 0x3);                                               // Change from Solar to Smart mode and vice versa.
        LCDNav = 0;
        ButtonRelease = 0;
        GLCD();
    // start charging if > button is pressed longer then 2 seconds
    } else if ((AccessStatus == OFF) && (LCDNav == 0) && (Buttons == 0x3) && (ButtonRelease == 0)) {     // Button 3 pressed ?
        LCDNav = MENU_ON;                                                       // about to start charging
        ButtonTimer = millis();
    } else if (LCDNav == MENU_ON && ((ButtonTimer + 2000) < millis() )) {
        LCDNav = 0;                                                             // Charging canceled
        setAccess(ON);
        ButtonRelease = 1;
    } else if ((LCDNav == MENU_ON) && (Buttons == 0x7)) {                      // Button 1 released before entering menu?
        LCDNav = 0;
        ButtonRelease = 0;
        GLCD();
    ////////////////
    } else if (Buttons == 0x2 && (ButtonRelease < 2)) {                         // Buttons < and > pressed ?
        if (LCDNav == 0) GLCD_init();                                           // When not in Menu, re-initialize LCD
        ButtonRelease = 1;
    } else if ((LCDNav > 1) && LCDNav != MENU_OFF && LCDNav != MENU_ON && (Buttons == 0x2 || Buttons == 0x3 || Buttons == 0x6)) { // Buttons < or > or both pressed
        if (ButtonRelease == 0) {                                               // We are navigating between sub menu options
            if (SubMenu) {
                value = getItemValue(LCDNav);
                switch (LCDNav) {
                    case MENU_MAINSMETER:
                        do {
                            value = MenuNavInt(Buttons, value, MenuStr[LCDNav].Min, MenuStr[LCDNav].Max);
                        } while (value == EM_UNUSED_SLOT4);
                        setItemValue(LCDNav, value);
                        break;
                    case MENU_CIRCUITMETER:                                     // do not display the Sensorbox or unused slots here
                    case MENU_EVMETER:                                          // do not display the Sensorbox or unused slots here
                        do {
                            value = MenuNavInt(Buttons, value, MenuStr[LCDNav].Min, MenuStr[LCDNav].Max);
                        } while (value == EM_SENSORBOX || value == EM_UNUSED_SLOT4);
                        setItemValue(LCDNav, value);
                        break;
                    case MENU_EVMETERHOST:
                        value = MenuNavInt(Buttons, value, 0, getCompatiblemDNSServiceCount(EVMeter.Type) + 1);
                        setItemValue(LCDNav, value);
                        break;
                    case MENU_MAINSMETERHOST:
                        value = MenuNavInt(Buttons, value, 0, getCompatiblemDNSServiceCount(MainsMeter.Type) + 1);
                        setItemValue(LCDNav, value);
                        break;
                    case MENU_CIRCUITMETERHOST:
                        value = MenuNavInt(Buttons, value, 0, getCompatiblemDNSServiceCount(CircuitMeter.Type) + 1);
                        setItemValue(LCDNav, value);
                        break;
                    case MENU_WIFI:
                        value = MenuNavInt(Buttons, value, MenuStr[LCDNav].Min, MenuStr[LCDNav].Max);
                        setItemValue(LCDNav, value);
                        if (value !=2 )
                            handleWIFImode();                                   //postpone handling WIFImode == 2 to moving to upper line
                        break;
                    case MENU_SUMMAINS:                                         // do not display the Sensorbox or unused slots here
                        do {
                            value = MenuNavInt(Buttons, value, MenuStr[LCDNav].Min, MenuStr[LCDNav].Max);
                        } while (value > 0 && value < 10);
                        setItemValue(LCDNav, value);
                        break;
                    case MENU_SWITCH:
                        do {
                            value = MenuNavInt(Buttons, value, MenuStr[LCDNav].Min, MenuStr[LCDNav].Max);
                        } while (LoadBl >=2 && value == 5);                     // do not allow GridRelay on Slave
                        setItemValue(LCDNav, value);
                        if (value != 5)
                            GridRelayOpen = false;                              // so we don't have limiting current when not on GridRelay
                        //on Toggle switches we want to read the existing switch position, so we re-call the constructor:
                        ExtSwitch = Button();                                   //this recreates ExtSwitch object, thus calling the constructor
                        break;
                    case MENU_LCDPIN: //left button changes digit, middle button goes out of menu, right button moves to next digit
                        static uint8_t digits[4]; //numbered 3,2,
                        /// pin 0478 is digit 3210 so digit[3]=0, digit[2]=4 etc
                        if (Buttons == 0x3) digit--;                            //right button pressed, 
                        digit = digit & 0x3;
                        digits[digit]= (uint16_t)(value/pow_10[digit]) % 10;
                        value -= digits[digit]*pow_10[digit];                   //we subtract the old digit's value
                        if (Buttons == 0x6) {                                   //left button pressed
                            digits[digit]++;
                            if (digits[digit]>9) digits[digit]=0;
                        }
                        value += digits[digit]*pow_10[digit];                   //we add the new digit's value
                        setItemValue(LCDNav, value);
                      break;
                    case MENU_C2:                                               // do not display AUTO when slave
                        do {
                            value = MenuNavInt(Buttons, value, MenuStr[LCDNav].Min, MenuStr[LCDNav].Max);
                        } while (LoadBl >=2 && value == AUTO);
                        setItemValue(LCDNav, value);
                        break;
                    default:
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
    } else if (LCDNav > 1 && LCDNav != MENU_OFF && LCDNav != MENU_ON && Buttons == 0x5 && ButtonRelease == 0) {            // Button 2 pressed?
        ButtonRelease = 1;
        if (SubMenu) {                                                          // We are currently in Submenu
            if (LCDNav == MENU_EVMETERHOST){
                commitMeterHostSelection(EVMeter, getItemValue(MENU_EVMETERHOST));
            }
            if (LCDNav == MENU_MAINSMETERHOST){
                commitMeterHostSelection(MainsMeter, getItemValue(MENU_MAINSMETERHOST));
            }
            if (LCDNav == MENU_CIRCUITMETERHOST){
                commitMeterHostSelection(CircuitMeter, getItemValue(MENU_CIRCUITMETERHOST));
            }
            SubMenu = 0;                                                        // Exit Submenu now
            digit = 3;
            uint8_t WIFImode = getItemValue(MENU_WIFI);
            if (LCDNav == MENU_WIFI && WIFImode == 2)
                handleWIFImode();
        } else {                                                                // We are currently not in Submenu.
            SubMenu = 1;                                                        // Enter Submenu now
            if (LCDNav == MENU_EXIT) {                                          // Exit Main Menu
                LCDNav = 0;
                SubMenu = 0;
                clearErrorFlags(0xFF);                                          // Clear All Errors when exiting the Main Menu
                TestState = 0;                                                  // Clear TestState
                setChargeDelay(0);                                              // Clear ChargeDelay
                setSolarStopTimer(0);                                           // Disable Solar Timer
                GLCD();
                write_settings();                                               // Mark all settings dirty (rate-limited) ...
                shadowPrefs.loop(true);                                         // ... and force an immediate NVS write, independent of the 60s rate-limit timer
                ButtonRelease = 2;                                              // Skip updating of the LCD 
                PairingPin = "";                                                // Reset PairingPin
            }
        }

    } else if (Buttons == 0x7) {                                                // Buttons released
        ButtonRelease = 0;
        ButtonRepeat = 0;
    }

    //
    // here we update the LCD
    //
    String HoldStr = "";
    switch (LCDNav) {
        case 1:
            HoldStr = "for Menu";
            break;
        case MENU_OFF:
            HoldStr = "to Stop";
            break;
        case MENU_ON:
            HoldStr = "to Start";
            break;
        default:
            //so we are anything else then 1, MENU_OFF, MENU_ON
            if (ButtonRelease == 1) {
                GLCD_print_menu(4, getMenuItemOption(LCDNav));                  // print Menu
                if (LCDNav != 0) {
                    GLCD_print_menu(2, MenuStr[LCDNav].LCD);                            // add navigation arrows on both sides
                    // Bottom row of the GLCD
                    GLCD_buffer_clr();
                    sprintf(Str, "%i%cC", getItemValue(STATUS_TEMP), 0x0C);                              // ° Degree symbol
                    GLCD_write_buf_str(0, 0, Str, GLCD_ALIGN_LEFT);                     // show the internal temperature
                    sprintf(Str, "%.19s",(const char *) VERSION);
                    GLCD_write_buf_str(127, 0, Str, GLCD_ALIGN_RIGHT);// show software version in bottom right corner.
                    GLCD_sendbuf(7, 1);
                }
                ButtonRelease = 2;                                                      // Set value to 2, so that LCD will be updated only once
            }
    }

    if (HoldStr != "") {
            glcd_clrln(0, 0x00);
            glcd_clrln(1, 0x04);                                                // horizontal line
            GLCD_print_buf2(2, (const char *) "Hold 2 sec");
            GLCD_print_buf2(4, HoldStr.c_str());
            glcd_clrln(6, 0x10);                                                // horizontal line
            glcd_clrln(7, 0x00);
            ButtonRelease = 2;                                                  // Set value to 2, so that LCD will be updated only once
    }

    if (LCDNav == MENU_LCDPIN) {                                                // Draw a underline indicator below the digit of the PIN that can be changed
        glcd_clrln(6, 0x10);                                                    // draw the normal line ____________ at row 6
        if (SubMenu) {
            if (Buttons != 0x5) {                                               // Center button not pressed
                uint8_t ind_x = 39 + (3 - digit) * 13;
                goto_xy(ind_x, 6);
                for (uint8_t i = 0; i < 11; i++) {                              // write underline indicator
                    st7565_data(0x10 | 0x03);
                    GLCDbuf2[128 * 6 + ind_x + i] = (0x10 | 0x03);              // and a copy for the LCD on the webpage
                }
            }
        }
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

// 62-byte BMP header for a 1-bit monochrome image of BMP_WIDTH x BMP_HEIGHT.
// All multi-byte fields are computed from the constants in glcd.h at compile time
// (the array initializer is fully constexpr), so the resulting bytes live in flash.
static constexpr uint8_t BMP_HEADER[62] = {
    'B', 'M',                                          // 'BM' signature
    (uint8_t)( BMP_IMAGE_SIZE        & 0xFF),          // File size (LE, 4 bytes)
    (uint8_t)((BMP_IMAGE_SIZE >>  8) & 0xFF),
    (uint8_t)((BMP_IMAGE_SIZE >> 16) & 0xFF),
    (uint8_t)((BMP_IMAGE_SIZE >> 24) & 0xFF),
    0, 0, 0, 0,                                        // Reserved
    0x3E, 0, 0, 0,                                     // Pixel-data offset = 14 + 40 + 8 = 62

    40, 0, 0, 0,                                       // DIB header size
    (uint8_t)( BMP_WIDTH        & 0xFF),               // Width  (LE, 4 bytes)
    (uint8_t)((BMP_WIDTH  >>  8) & 0xFF),
    (uint8_t)((BMP_WIDTH  >> 16) & 0xFF),
    (uint8_t)((BMP_WIDTH  >> 24) & 0xFF),
    (uint8_t)( BMP_HEIGHT        & 0xFF),              // Height (LE, 4 bytes)
    (uint8_t)((BMP_HEIGHT >>  8) & 0xFF),
    (uint8_t)((BMP_HEIGHT >> 16) & 0xFF),
    (uint8_t)((BMP_HEIGHT >> 24) & 0xFF),
    1, 0,                                              // Planes
    1, 0,                                              // Bits per pixel (1-bit monochrome)
    0, 0, 0, 0,                                        // No compression
    0, 0, 0, 0,                                        // Image size (0 OK for uncompressed)
    0, 0, 0, 0,                                        // X pixels per meter (unused)
    0, 0, 0, 0,                                        // Y pixels per meter (unused)
    2, 0, 0, 0,                                        // Number of colors in the palette
    0, 0, 0, 0,                                        // Important colors

    // Color palette
    0xFF, 0xFF, 0xFF, 0x00,                            // White (0)
    0x00, 0x00, 0xFF, 0x00,                            // Red   (1)
};

/**
 * Transposes a 8x8 bit matrix stored in a byte array.
 *
 * This function takes an 8-byte input, where each byte represents a row of 8 bits,
 * and transposes it so that each output byte represents a column of 8 bits.
 * This operation is commonly used for graphical displays that store pixel data
 * in a column-major format.
 *
 * @param[in] input  An array of 8 bytes, where each byte represents a row of 8 bits.
 * @param[out] output An array of 8 bytes, where each byte represents a transposed column of 8 bits.
 *
 */
void transpose8x8(const std::array<uint8_t, 8>& input, std::array<uint8_t, 8>& output) {
    for (int bitPos = 0; bitPos < 8; ++bitPos) {
        uint8_t newByte = 0;
        for (int i = 0; i < 8; ++i) {
            newByte |= (input[i] >> (7 - bitPos) & 0x01) << (7 - i);
        }
        output[bitPos] = newByte;
    }
}

/**
 * Processes GLCD buffer data and converts it into a BMP-formatted image.
 *
 * Writes the BMP into a single 1086-byte static buffer (62 B header + 1024 B pixels).
 * The buffer is reused across calls, so no heap allocations happen per frame --
 * important because this function runs once per second, per connected LCD-preview websocket client.
 *
 * @param[out] outSize  Set to the total size of the BMP image in bytes (always BMP_IMAGE_SIZE).
 * @return Pointer to the static buffer holding the BMP image.
 */
const uint8_t* createImageFromGLCDBuffer(size_t &outSize) {
    constexpr int CHUNK_SIZE = 128;
    constexpr int BLOCK_SIZE = 8;

    static uint8_t bmpBuf[BMP_IMAGE_SIZE];
    // Header is constant and lives in flash -- copy once into the working buffer.
    memcpy(bmpBuf, BMP_HEADER, sizeof(BMP_HEADER));

    uint8_t *out = bmpBuf + sizeof(BMP_HEADER);

    // Walk GLCDbuf2 from end to start in 128-byte chunks, transposing each 8x8 block.
    for (size_t chunkOffset = sizeof(GLCDbuf2); chunkOffset > 0; chunkOffset -= CHUNK_SIZE) {
        const uint8_t *chunk = GLCDbuf2 + (chunkOffset - CHUNK_SIZE);

        // Process the 128-byte chunk in groups of 8 directly into `out`.
        for (int byteIndex = 0; byteIndex < CHUNK_SIZE; byteIndex += BLOCK_SIZE) {
            std::array<uint8_t, BLOCK_SIZE> input{};
            std::array<uint8_t, BLOCK_SIZE> output{};
            std::copy_n(chunk + byteIndex, BLOCK_SIZE, input.begin());

            transpose8x8(input, output);

            // Distribute transposed bytes interleaved across the 128-byte row.
            const int newByteIndex = byteIndex / BLOCK_SIZE;
            for (int j = 0; j < BLOCK_SIZE; ++j) {
                out[newByteIndex + j * (CHUNK_SIZE / BLOCK_SIZE)] = output[j];
            }
        }
        out += CHUNK_SIZE;
    }

    outSize = BMP_IMAGE_SIZE;
    return bmpBuf;
}


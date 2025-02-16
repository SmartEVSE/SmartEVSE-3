/*
;
;   WCH Serial ISP programming for use with WCH CH32V203 ICs
;
;
;   This microcontroller series can be programmed using a WCH-link USB debug adapter
;   Or by resetting the chip into its bootloader (hold BOOT0 high) and a USB or Serial connection to the host software (WCHISPTool)
;
;   Both programming options are undocumented and WCH doesn't provide any documentation on the protocols used.
;
;   This software offers a solution for the serial bootloader option. i.e., a WCH chip that will be programmed by another microcontroller.
;   Please note that the flash programming obfuscation is only verified on two V203 chips. It's not guaranteed to be correct for every chip.
;   
;   Call WchFirmwareUpdate() from your main program. It will read a binary file from SPIFFS, and flash the microcontroller.
;   Serial1 should be set to 115200 8N1
;
;
;   MIT License
;
;   (C) 2023  Michael Stegen / Stegen Electronics
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

#if SMARTEVSE_VERSION >= 40


#include <Arduino.h>
#include <SPIFFS.h>
#include <stdio.h>
#include <string.h>

#include "esp32.h"
#include "wchisp.h"
#include "mongoose.h"

const uint8_t wch_start[] = {0x31, 0x19, 0x4d, 0x43, 0x55, 0x20, 0x49, 0x53, 0x50, 0x20, 0x26, 0x20, 0x57, 0x43, 0x48, 0x2e ,0x43, 0x4e};
const uint8_t wch_read_option[] = {0x1f, 0x00};
const uint8_t wch_write_option[] = {0x07, 0x00, 0xa5, 0x5a, 0x3f, 0xc0, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff};
const uint8_t wch_set_key[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t wch_erase_flash[] = {0xe0, 0x00, 0x00, 0x00};     // Size of Full Flash Erase, set to 224kB for a CH32V203 with 64kB flash?
const uint8_t wch_stop[] = {0x01};      // this will also soft reset the CPU

uint8_t WchUID[8];
unsigned long WchTimeout;
uint8_t WchWaitRX = 0;

const char* WCHfirmware = "/data/CH32V203.bin";
extern void glcd_clrln(unsigned char ln, unsigned char data);
extern void GLCD_print_buf2(unsigned char y, const char* str);

void WchEnterBootloader(void) {

    pinMode(WCH_NRST, OUTPUT);
    pinMode(WCH_SWCLK, OUTPUT);
    digitalWrite(WCH_NRST, LOW);              // keep WCH32V in reset
    digitalWrite(WCH_SWCLK, HIGH);            // BOOT0 high, start bootloader
    delay(100);
    pinMode(WCH_NRST, INPUT);                 // WCH32V reset high
    // WCH32V should now be in bootloader mode (system memory).
    
    delay(100);
    digitalWrite(WCH_SWCLK, LOW);             // BOOT0 low
    pinMode(WCH_SWCLK, INPUT);
}

void WchReset(void) {

    pinMode(WCH_SWCLK, OUTPUT);
    digitalWrite(WCH_SWCLK, LOW);             // BOOT0 low
    pinMode(WCH_NRST, OUTPUT);                // connected to NRST pin  
    digitalWrite(WCH_NRST, LOW);              // keep WCH32V in reset
    delay(200);
    pinMode(WCH_NRST, INPUT);                 // WCH32V reset high
    pinMode(WCH_SWCLK, INPUT);
    // WCH32V should now be running program flash.
}

void WchSendData(const uint8_t * command_data, uint16_t len, uint8_t wch_cmd ) {
    uint8_t x, sum = 0;
    uint8_t WchTXbuf[200];
    
    WchTXbuf[0] = 0x57;
    WchTXbuf[1] = 0xAB;
    WchTXbuf[2] = wch_cmd;
    WchTXbuf[3] = len & 0xff;
    WchTXbuf[4] = (len >> 8) & 0xff;
    if (len >= 190) len = 190;                                  // should never be more then 190 bytes
    memcpy(WchTXbuf+5, command_data, len);

    for (x=0; x<len+3; x++) sum += WchTXbuf[x+2];               // calculate checksum
    WchTXbuf[5 + len] = sum;
    len += 6;

    Serial1.write(WchTXbuf, len);                               // write to WCH
#ifdef WCHDEBUG    
    Serial.print("[->] ");
    for (x=0; x<len; x++) Serial.printf("%02X ", WchTXbuf[x]);  // debug
    Serial.print("\r\n");
#endif
    WchTimeout = millis() + 1000;                               // 1000ms timeout
    WchWaitRX = 1;
}


struct mg_fs *filesystem = &mg_fs_packed;

void WchProgram(void *fp, size_t filesize) {
    uint8_t WchState = WCH_START, RXbyte, RXlen=0, x, sum, XorKey=0;
    uint16_t len;
    uint8_t filebuffer[100];
    uint8_t WchRXbuf[200];
    uint32_t filepointer=0;

    WchEnterBootloader();

    _LOG_D("filesize %u bytes", filesize);
    _LOG_D("Programming CH32V203..");
    
    while (Serial1.available()) RXbyte = Serial1.read();        // flush Uart
    
    do {
        if (!WchWaitRX) { 
            switch (WchState) {
                case WCH_START:
                    WchSendData(wch_start, sizeof(wch_start), WCH_START); 
                    break;
                case WCH_READ_OPTION:
                    WchSendData(wch_read_option, sizeof(wch_read_option), WCH_READ_OPTION);
                    break;    
                case WCH_WRITE_OPTION:
                    WchSendData(wch_write_option, sizeof(wch_write_option), WCH_WRITE_OPTION);
                    break;      
                case WCH_SET_KEY:
                    WchSendData(wch_set_key, sizeof(wch_set_key), WCH_SET_KEY);
                    break;      
                case WCH_ERASE_FLASH:
                    WchSendData(wch_erase_flash, sizeof(wch_erase_flash), WCH_ERASE_FLASH);
                    break;      
                case WCH_PROGRAM_FLASH:
                case WCH_VERIFY_FLASH:
                    filesystem->sk(fp, filepointer);
                    len = filesystem->rd(fp, filebuffer+5, 56);                         // we read one line at a time
                    for(x=0; x< len; x++) {
                        if ((x & 7) == 7) filebuffer[x+5] ^= (XorKey + 0x31);           // 8th byte is special
                        else filebuffer[x+5] ^= XorKey;                                 // 'encrypt' with XorKey
                    } 
                    if (filepointer > 0x1fff0000) break;                                // prevent writes outside of flash area

                    filebuffer[0] = filepointer & 0xff;
                    filebuffer[1] = (filepointer >> 8) & 0xff;
                    filebuffer[2] = (filepointer >> 16) & 0xff;
                    filebuffer[3] = (filepointer >> 24) & 0xff;
                    filebuffer[4] = 0;
                    WchSendData(filebuffer, len + 5, WchState);
                    break;

                case WCH_STOP:
                    WchSendData(wch_stop, sizeof(wch_stop), WCH_STOP);
                    break;

                default:
                    break; 
            }
        }

        while (Serial1.available()) {                           // Uart1 data available?
#ifdef WCHDEBUG
            if (!RXlen) Serial.print("[<-] ");
#endif
            RXbyte = Serial1.read();
#ifdef WCHDEBUG
            Serial.printf("%02X ",RXbyte);
#endif
            WchRXbuf[RXlen] = RXbyte;
            if (++RXlen >= 200) RXlen = 199;
        }

        if (RXlen >= 8) {                                       // minimal 8 bytes in reply
#ifdef WCHDEBUG
            Serial.printf("\r\n");
#endif
            WchWaitRX = 0;                                      // now process the data
            sum = 0;

            for(x=2; x<RXlen-1; x++) sum += WchRXbuf[x];
            
            // Check if received data starts with 55 AA, and the checksum matches.
            if (WchRXbuf[0] == 0x55 && WchRXbuf[1] == 0xAA && WchRXbuf[2] == WchState && sum == WchRXbuf[RXlen-1]) {
                
                len = WchRXbuf[4];
                // Verify the length of the received data
                if ((len + 7) == RXlen) {

                    switch (WchState) {
                        case WCH_START:
                            if (WchRXbuf[6] == 0x31 && WchRXbuf[7] == 0x19) {
                                WchState = WCH_READ_OPTION;
                            } else log_e("Start Error");    
                            break;

                        case WCH_READ_OPTION:
                            memcpy(WchUID, WchRXbuf+24, 8);                         // Store chip UID (unused)
                            XorKey = 0;
                            for (x=0; x<8 ;x++) XorKey += WchRXbuf[24+x];           // calculate XorKey
                            WchState = WCH_WRITE_OPTION;
                            break;

                        case WCH_WRITE_OPTION:
                            if (WchRXbuf[6] == 0 && WchRXbuf[7] == 0) {
                                WchState = WCH_SET_KEY;
                            } else log_e("Write Error");
                            break;

                        case WCH_SET_KEY:                                           // BTVER 2.6 reports 0x09. BTVER 2.7 reports 0x89 ot 0x57 here
                            if (((WchRXbuf[6] & 0x0f) == 0x09) && WchRXbuf[7] == 0) {
                                WchState = WCH_ERASE_FLASH;
                                log_d("Erasing...");
                            } else log_e("Key Error");
                            break;

                        case WCH_ERASE_FLASH:
                            if (WchRXbuf[6] == 0 && WchRXbuf[7] == 0) {
                                WchState = WCH_PROGRAM_FLASH;
                                log_d("Programming...");
                            } else log_e("Erase Error");
                            break;

                        case WCH_PROGRAM_FLASH:
                            if (WchRXbuf[6] == 0 && WchRXbuf[7] == 0) {
                                filepointer += 56;                          // point to next data in file
                                if (filepointer > filesize) {
                                    log_d("Verifying...");
                                    WchState = WCH_VERIFY_FLASH;
                                    filepointer = 0;
                                } 
                            } else log_e("Program Error");
                            break;

                        case WCH_VERIFY_FLASH:
                            if (WchRXbuf[7] == 0) {     // (WchRXbuf[6] == 0 && WchRXbuf[7] == 0)      TODO: NEEDS FIX!
                                filepointer += 56;
                                if (filepointer > filesize) WchState = WCH_STOP;
                            } else log_e("Verify Error");
                            break;

                        case WCH_STOP:
                            if (WchRXbuf[6] == 0 && WchRXbuf[7] == 0) {
                                WchState = WCH_EXIT;                           // Exit.
                            } else log_e("Stop Error");
                            break;

                        default:
                            break; 
                    }

                } else log_e("Length Error");

            } else log_e("Header or Sum error");

            RXlen = 0;
        } 

        if (WchTimeout < millis() && WchWaitRX) {
            log_e("Timeout");
            WchWaitRX = 0;
        }


    } while (WchState != WCH_EXIT);             // keep looping until 0
}

/*
void setTimeZone(char *tzname) {

        bool found = false;
        char line[80];
        int pos = 0;
        char c;
        do {
            pos = 0;
            do {
                fs->rd(fp, &c, 1);
                if (filepos < filelen)
                    line[pos]=c;
                pos++;
                filepos++;
            } while (pos < sizeof(line) - 1 && c != '\n' && filepos < filelen);
            //terminate with NULL character
            line[pos]=0;
            found = strstr(line, tzname);
        } while (filepos < filelen && !found);
    }
}*/
time_t WCHfirmware_timestamp = 0;
uint8_t WchFirmwareUpdate(unsigned long WCHRunningVersion) {

    void *fp;
    size_t filelen = 0;
    filesystem->st(WCHfirmware, &filelen, &WCHfirmware_timestamp);
    if (!filesystem) {
        _LOG_A("ERROR cannot find CH32 flash file:%s.\n", WCHfirmware);
        return 1;
    }
    if ((fp = filesystem->op(WCHfirmware, MG_FS_READ)) == NULL) {
        _LOG_A("ERROR cannot open CH32 flash file:%s.\n", WCHfirmware);
        return 2;
    }
    if (WCHfirmware_timestamp > WCHRunningVersion + 60)  { //CH32 may take 60s to compile
        _LOG_A("Flashing WCHfirmware version %lu over %lu.\n", WCHfirmware_timestamp, WCHRunningVersion);
        glcd_clrln(0, 0x00);
        glcd_clrln(1, 0x04);                                                // horizontal line
        GLCD_print_buf2(2, (const char *) "SmartEVSE 4");
        GLCD_print_buf2(4, (const char *) "Flashing WCH");
        glcd_clrln(6, 0x10);                                                // horizontal line
        glcd_clrln(7, 0x00);

        WchProgram(fp, filelen);                                                       	// Program Chip
    } else {
        _LOG_A("NOT Flashing WCHfirmware version %lu over %lu.\n", WCHfirmware_timestamp, WCHRunningVersion);
    }
    filesystem->cl(fp);
    return 0;
}
#endif

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

unsigned char RFID[8] = {0, 0, 0, 0, 0, 0, 0, 0};
#ifdef SMARTEVSE_VERSION //ESP32
#include <string.h>
#include <Preferences.h>

#include "esp32.h"
#include "utils.h"
#include "OneWire.h"
#include "OneWireESP32.h"

#define RFIDSIZE 700

unsigned char RFIDlist[RFIDSIZE];                                               // holds up to 100 RFIDs

OneWire32 ds(PIN_SW_IN, 0, 1, 0);                                               //gpio pin, tx, rx, parasite power

// ############################# OneWire functions #############################

#if FAKE_RFID
unsigned char OneWireReadCardId(void) {
    if (!RFIDstatus && Show_RFID) { //if ready to accept new card and it is shown
        RFID[0] = 0x01; //Family Code
        RFID[1] = 0x01; //RFID id = "01 02 03 04 05 06"
        RFID[2] = 0x02;
        RFID[3] = 0x03;
        RFID[4] = 0x04;
        RFID[5] = 0x05;
        RFID[6] = 0x06;
        RFID[7] = 0xf0; //crc8 code  TODO is this ok?
        Show_RFID = 0; //this makes "showing" the RFID a one shot event
        return 1;
    }
    else
        return 0; //card is already read, no new card
}

#endif
#if SMARTEVSE_VERSION >= 30 && SMARTEVSE_VERSION < 40 //ESP32v3
unsigned char OneWireReadCardId(void) {
    uint8_t x;

    // Use ReadRom command (33)
    if (!ds.readRom(RFID)) {                                                    // read Family code (0x01) RFID ID (6 bytes) and crc8
        if (crc8(RFID,8)) {
            RFID[0] = 0;                                                        // CRC incorrect, clear first byte of RFID buffer
            return 0;
        } else {
            for (x=0 ; x<7 ; x++) _LOG_A_NO_FUNC("%02x",RFID[x]);
            _LOG_A_NO_FUNC("\r\n");
            return 1;
        }
    }
    return 0;
}
#endif


// ############################## RFID functions ##############################



// Write a list of 20 RFID's to the eeprom
//
void WriteRFIDlist(void) {

    if (preferences.begin("RFIDlist", false) ) {                                // read/write
        preferences.putBytes("RFID", RFIDlist, RFIDSIZE);                       // write 700 bytes to storage
        preferences.putUChar("RFIDinit", 3);                                    // data initialized to 700byte mode
        preferences.end();
    } else {
        _LOG_A("Error opening preferences!\n");
    }
    

    _LOG_I("\nRFID list saved\n");
}


// Expand the old 6 byte UID array to a 7 byte UID array
//
void expandUIDArray(unsigned char* oldArray, unsigned char* newArray, uint16_t oldSize) {
    int oldIdx = 0;
    int newIdx = 0;

    while (oldIdx <= oldSize - 6 && newIdx <= RFIDSIZE - 7) {
        memcpy(&newArray[newIdx], &oldArray[oldIdx], 6);

        newArray[newIdx + 6] = 0xff;

        oldIdx += 6;
        newIdx += 7;
    }

    // If there's any remaining space in the new array, fill it with 0xFF
    while (newIdx < RFIDSIZE) {
        newArray[newIdx] = 0xff;
        newIdx++;
    }
}


// Read a list of 100 RFID's from preferences
//
void ReadRFIDlist(void) {
    uint8_t initialized = 0;
    unsigned char RFIDold[600];                                                 // old RFID buffer

    if (preferences.begin("RFIDlist", false) ) {                                // read/write
        initialized = preferences.getUChar("RFIDinit", 0);
        switch (initialized) {
            case 1:                                                             // we were initialized to old 120 bytes mode
                preferences.getBytes("RFID", RFIDold, 120);                     // read 120 bytes from storage
                //we are now going to convert from RFIDold 120bytes to RFIDlist 700bytes
                expandUIDArray(RFIDold, RFIDlist, 120);
                preferences.remove("RFID");
                preferences.end();
                WriteRFIDlist();
                break;
            case 0:
                preferences.end();
                DeleteAllRFID();                                                // when unitialized, delete all cardIDs
                setItemValue(MENU_RFIDREADER, 0);                               // RFID Reader Disabled
                break;
            case 2:
                preferences.getBytes("RFID", RFIDold, 600);                     // expand v2 RFIDlist to 100 card id's with 7 bytes
                //we are now going to convert from RFIDold 600bytes to RFIDlist 700bytes
                expandUIDArray(RFIDold, RFIDlist, 600);
                preferences.remove("RFID");
                preferences.end();
                WriteRFIDlist();
                break;
            case 3:                                                             // extended RFIDlist with room for 100tags of 7 bytes = 700 bytes
                preferences.getBytes("RFID", RFIDlist, RFIDSIZE);               // read 700 bytes from storage
                preferences.end();
                break;
        }

    } else {
        _LOG_A("Error opening preferences!\n") ;
    }
}

// scan for matching RFID in RFIDlist
// returns offset+7 when found, 0 when not found
uint16_t MatchRFID(void) {
    uint16_t offset = 0, r;

    do {
        if (RFID[0] == 0x01) {  // old reader 6 byte UID starts at RFID[1]
            r = memcmp(RFID + 1, RFIDlist + offset, 6);                        // compare only 6 bytes
        } else {                // new reader 7 byte UID starts ar RFID[0]
            r = memcmp(RFID, RFIDlist + offset, 7);                            // compare 7 bytes
        }
        offset += 7;
    } while (r !=0 && offset < RFIDSIZE-7);

    if (r == 0) return offset;                                                  // return offset + 7 in RFIDlist
    else return 0;
}


// Store RFID card in memory and eeprom
// returns 1 when successful
// returns 2 when already stored
// returns 0 when all slots are full.
unsigned char StoreRFID(void) {
    uint16_t offset = 0, r;
    unsigned char empty[7] = {0xff,0xff,0xff,0xff,0xff,0xff,0xff};

    // first check if the Card ID was already stored.
    if ( MatchRFID() ) return 2;                                                // already stored, that's ok.

    do {
        r = memcmp(empty, RFIDlist + offset, 7);
        offset += 7;
    } while (r !=0 && offset < RFIDSIZE);
    if (r != 0) return 0;                                                       // no more room to store RFID
    offset -= 7;
    _LOG_A("offset %u ",offset);
    if (RFID[0] == 0x01) {
        memcpy(RFIDlist + offset, RFID+1, 6);                                   // Store 6 byte UID
    } else {
        memcpy(RFIDlist + offset, RFID, 7);                                     // Store 7 byte UID
    }


    _LOG_I("\nRFIDlist:");
    for (r=0; r<RFIDSIZE; r++) _LOG_I_NO_FUNC("%02x",RFIDlist[r]);

    WriteRFIDlist();
    return 1;
}

//load and store parameter RFIDparm into global variable RFID
void LoadandStoreRFID(unsigned int *RFIDparam) {
    for (int i = 0; i < 8; i++)
        RFID[i]=RFIDparam[i];
    StoreRFID();
}

// Delete RFID card in memory and eeprom
// returns 1 when successful, 0 when RFID was not found
unsigned char DeleteRFID(void) {
    uint16_t offset = 0, r;

    offset = MatchRFID();                                                       // find RFID in list
    if (offset) {
        offset -= 7;
        for (r = 0; r < 7; r++) RFIDlist[offset + r] = 0xff;
    } else return 0;

    _LOG_A("deleted %u ",offset);
    for (r=0; r<RFIDSIZE; r++) _LOG_A_NO_FUNC("%02x",RFIDlist[r]);
    
    WriteRFIDlist();
    return 1;
}

void DeleteAllRFID(void) {
    uint16_t i;

    for (i = 0; i < RFIDSIZE; i++) RFIDlist[i] = 0xff;
    WriteRFIDlist();
    _LOG_I("All RFID cards erased!\n");
}

void CheckRFID(void) {
    uint16_t x;
    // When RFID is enabled, a OneWire RFID reader is expected on the SW input
    uint8_t RFIDReader = getItemValue(MENU_RFIDREADER);
#if SMARTEVSE_VERSION < 40                                                      // v3 has to read card ID; in v4, CheckRFID is called after reception of a valid RFID
#endif
#if ENABLE_OCPP
            if (OcppMode && RFIDReader == 6) {                                      // Remote authorization via OCPP?
                // Use OCPP
                if (!RFIDstatus) {
                    if (RFID[0] == 0x01) {                                      // old 6 byte reader
                        if (RFID[5] == 0 && RFID[6] == 0) {                     // so we have 4 byte UID
                            ocppUpdateRfidReading(RFID + 1, 4); // UID starts at RFID+1; Pad / truncate UID to 4 bytes
                        } else {                                                // 7 byte UID
                            ocppUpdateRfidReading(RFID + 1, 6); // UID starts at RFID+1; Pad / truncate UID to 6 bytes for now since 7th byte is missing
                        }
                    } else {                                                    // new 7 byte reader
                        if (RFID[4] == 0 && RFID[5] == 0 && RFID[6] == 0) {     // so we have 4 byte UID
                            ocppUpdateRfidReading(RFID, 4); // UID starts at RFID; 4 byte UID
                        } else {                                                // 7 byte UID
                            ocppUpdateRfidReading(RFID, 7); // UID starts at RFID; 7 byte UID
                        }
                    }
                }
                RFIDstatus = 1;
            } else
#endif
            {
                // Use local whitelist

                switch (RFIDReader) {
                    case 1:                                                         // EnableAll. All learned cards accepted for locking /unlocking
                        x = MatchRFID();
                        if (x && !RFIDstatus) {
                            _LOG_A("RFID card found!\n");
                            if (Access_bit) {
                                setAccess(false);                                   // Access Off, Switch back to state B1/C1
                            } else setAccess(true);

                            RFIDstatus = 1;
                        }  else if (!x) RFIDstatus = 7;                             // invalid card
                        BacklightTimer = BACKLIGHT;
                        break;
                    case 2:                                                         // EnableOne. Only the card that unlocks, can re-lock the EVSE
                        x = MatchRFID();
                        if (x && !RFIDstatus) {
                            _LOG_A("RFID card found!\n");
                            if (!Access_bit) {
                                CardOffset = x;                                     // store cardoffset from current card
                                setAccess(true);                                    // Access On
                            } else if (CardOffset == x) {
                                setAccess(false);                                   // Access Off, Switch back to state B1/C1
                            }
                            RFIDstatus = 1;
                        }  else if (!x) RFIDstatus = 7;                             // invalid card
                        BacklightTimer = BACKLIGHT;
                        break;
                    case 3:                                                         // Learn Card
                        x = StoreRFID();
                        if (x == 1) {
                            _LOG_A("RFID card stored!\n");
                            RFIDstatus = 2;
                        } else if (x == 2 && !RFIDstatus) {
                            _LOG_A("RFID card was already stored!\n");
                            RFIDstatus = 4;
                        } else if (!RFIDstatus) {
                            _LOG_A("RFID storage full! Delete card first\n");
                            RFIDstatus = 6;
                        }
                        break;
                    case 4:                                                         // Delete Card
                        x = DeleteRFID();
                        if (x) {
                            _LOG_A("RFID card deleted!\n");
                            RFIDstatus = 3;
                        } else if (!RFIDstatus) {
                            _LOG_A("RFID card not in list!\n");
                            RFIDstatus = 5;
                        }
                        break;
                    default:
                        break;
                }
            }
#if SMARTEVSE_VERSION < 40                                                      // v3 has to read card ID; in v4, CheckRFID is called after reception of a valid RFID
#endif
}
#else //CH32

#include "ch32v003fun.h"
#include "ch32.h"
#include "utils.h"
extern "C" {
    #include "evse.h"
}


// ############################# OneWire functions #############################

// SW set to 0, set to output (driven low)
void ONEWIRE_LOW(void)
{
    funDigitalWrite(SW_IN, FUN_LOW);
    funPinMode(SW_IN, GPIO_CFGLR_OUT_2Mhz_PP);
}

// SW set to 1, set to output (driven high)
void ONEWIRE_HIGH(void)
{
    funDigitalWrite(SW_IN, FUN_HIGH);
    funPinMode(SW_IN, GPIO_CFGLR_OUT_2Mhz_PP);
}

// SW input (floating high)
void ONEWIRE_FLOATHIGH(void)
{
    GPIOB->BSHR = 0x0020;           // Set OUTDR bit 5 -> enabling Pull up (might be able to do the same with funDigitalWrite)
    funPinMode(SW_IN, GPIO_CFGLR_IN_PUPD);
}


// Reset 1-Wire device on SW input
// returns:  1 Device found
//           0 No device found
//         255 Error. Line is pulled low (short, or external button pressed?)
//
uint8_t OneWireReset(void) {
    unsigned char r;

    if (funDigitalRead(SW_IN) == FUN_LOW) return 255;                 // Error, pulled low by external device?

    ONEWIRE_LOW();                                                // Drive wire low
    delayMicroseconds(480);
    ONEWIRE_FLOATHIGH();                                          // don't drive high, but use pullup
    delayMicroseconds(70);

    if (funDigitalRead(SW_IN) == FUN_HIGH) r = 0;                     // sample pin to see if there is a OneWire device..
    else r = 1;

    delayMicroseconds(410);
    return r;
}

void OneWireWriteBit(uint8_t v) {

    if (v & 1) {                                                  // write a '1'
        ONEWIRE_LOW();                                            // Drive low
        delayMicroseconds(10);
        ONEWIRE_HIGH();                                           // Drive high
        delayMicroseconds(55);
    } else {                                                      // write a '0'
        ONEWIRE_LOW();                                            // Drive low
        delayMicroseconds(65);
        ONEWIRE_HIGH();                                           // Drive high
        delayMicroseconds(5);
    }
}

uint8_t OneWireReadBit(void) {
    unsigned char r;

    ONEWIRE_LOW();
    delayMicroseconds(3);
    ONEWIRE_FLOATHIGH();
    delayMicroseconds(10);

    if (funDigitalRead(SW_IN) == FUN_HIGH) r = 1u;                    // sample pin
    else r = 0;

    delayMicroseconds(53);
    return r;
}

void OneWireWrite(uint8_t v) {
    unsigned char bitmask;
    for (bitmask = 0x01; bitmask ; bitmask <<= 1) {
        OneWireWriteBit( (bitmask & v) ? 1u : 0);
    }
}

uint8_t OneWireRead(void) {
    unsigned char bitmask, r = 0;

    for (bitmask = 0x01; bitmask ; bitmask <<= 1) {
        if ( OneWireReadBit()) r |= bitmask;
    }
    return r;
}

uint8_t OneWireReadCardId() {
    unsigned char x;

    if (OneWireReset() == 1) {                                    // RFID card detected
        OneWireWrite(0x33);                                       // OneWire ReadRom Command
        for (x=0 ; x<8 ; x++) RFID[x] = OneWireRead();            // read Family code (0x01) RFID ID (6 bytes) and crc8

        if (crc8(RFID,8)) {
            RFID[0] = 0;                                          // CRC incorrect, clear first byte of RFID buffer
            printf("RFIDstatus@0\n");                             // signal RFIDstatus = 0
            return 0;
        } else {
            printf("RFID@");
            for (x=0 ; x<8 ; x++) printf("%02x",RFID[x]);
            printf("\n");
            return 1;
        }
    }
    printf("RFIDstatus@0\n");                                    // signal RFIDstatus = 0
    return 0;
}
#endif

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
#include <Preferences.h>

#include "evse.h"
#include "utils.h"
#include "OneWire.h"

unsigned char RFID[8] = {0, 0, 0, 0, 0, 0, 0, 0};
unsigned char RFIDlist[120];                                                    // holds up to 20 RFIDs


// ############################# OneWire functions #############################



// Reset 1-Wire device on SW input
// returns:  1 Device found
//           0 No device found
//         255 Error. Line is pulled low (short, or external button pressed?)
//
unsigned char OneWireReset(void) {
    unsigned char r;

    if (digitalRead(PIN_SW_IN) == LOW) return 255;                              // Error, pulled low by external device?

    ONEWIRE_LOW;                                                                // Drive wire low
    delayMicroseconds(480);
    RTC_ENTER_CRITICAL();                                                       // Disable interrupts
	ONEWIRE_FLOATHIGH;                                                          // don't drive high, but use pullup
    delayMicroseconds(70);
    if (digitalRead(PIN_SW_IN) == HIGH) r = 0;                                  // sample pin to see if there is a OneWire device..
    else r = 1;
    RTC_EXIT_CRITICAL();                                                        // Restore interrupts
    delayMicroseconds(410);
    return r;
}

void OneWireWriteBit(unsigned char v) {

    if (v & 1) {                                                                // write a '1'
        RTC_ENTER_CRITICAL();                                                   // Disable interrupts
        ONEWIRE_LOW;                                                            // Drive low
        delayMicroseconds(10);
        ONEWIRE_HIGH;                                                           // Drive high
        RTC_EXIT_CRITICAL();                                                    // Restore interrupts
        delayMicroseconds(55);
    } else {                                                                    // write a '0'
        RTC_ENTER_CRITICAL();                                                   // Disable interrupts
        ONEWIRE_LOW;                                                            // Drive low
        delayMicroseconds(65);
        ONEWIRE_HIGH;                                                           // Drive high
        RTC_EXIT_CRITICAL();                                                    // Restore interrupts
        delayMicroseconds(5);
    }
}

unsigned char OneWireReadBit(void) {
    unsigned char r;

    RTC_ENTER_CRITICAL();                                                       // Disable interrupts
    ONEWIRE_LOW;
    delayMicroseconds(3);
    ONEWIRE_FLOATHIGH;
    delayMicroseconds(10);
    if (digitalRead(PIN_SW_IN) == HIGH) r = 1;                                  // sample pin
    else r = 0;
    RTC_EXIT_CRITICAL();                                                        // Restore interrupts
    delayMicroseconds(53);
    return r;
}

void OneWireWrite(unsigned char v) {
    unsigned char bitmask;
    for (bitmask = 0x01; bitmask ; bitmask <<= 1) {
        OneWireWriteBit( (bitmask & v) ? 1u : 0);
    }
}

unsigned char OneWireRead(void) {
    unsigned char bitmask, r = 0;

    for (bitmask = 0x01; bitmask ; bitmask <<= 1) {
        if ( OneWireReadBit()) r |= bitmask;
    }
    return r;
}

#ifdef FAKE_RFID
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

#else
unsigned char OneWireReadCardId(void) {
    unsigned char x;

    if (OneWireReset() == 1) {                                                  // RFID card detected
        OneWireWrite(0x33);                                                     // OneWire ReadRom Command
        for (x=0 ; x<8 ; x++) RFID[x] = OneWireRead();                          // read Family code (0x01) RFID ID (6 bytes) and crc8
        if (crc8(RFID,8)) {
            RFID[0] = 0;                                                        // CRC incorrect, clear first byte of RFID buffer
            return 0;
        } else {
            for (x=1 ; x<7 ; x++) _LOG_A("%02x",RFID[x]);
            _LOG_A("\r\n");
            return 1;
        }
    }
    return 0;
}
#endif


// ############################## RFID functions ##############################



// Read a list of 20 RFID's from preferences
//
void ReadRFIDlist(void) {
    uint8_t initialized = 0;

    if (preferences.begin("RFIDlist", false) ) {
        initialized = preferences.getUChar("RFIDinit", 0);
        if (initialized) preferences.getBytes("RFID", RFIDlist, 120);     // read 120 bytes from storage
        preferences.end();

        if (initialized == 0 ) DeleteAllRFID();           // when unitialized, delete all cardIDs 

    } else _LOG_A("Error opening preferences!\n");
}

// Write a list of 20 RFID's to the eeprom
//
void WriteRFIDlist(void) {

    if (preferences.begin("RFIDlist", false) ) {
        preferences.putBytes("RFID", RFIDlist, 120);                                // write 120 bytes to storage
        preferences.putUChar("RFIDinit", 1);                                      // data initialized
        preferences.end();
    } else _LOG_A("Error opening preferences!\n");
    

    _LOG_I("\nRFID list saved\n");
}

// scan for matching RFID in RFIDlist
// returns offset+6 when found, 0 when not found
unsigned char MatchRFID(void) {
    unsigned char offset = 0, r;

    do {
        r = memcmp(RFID + 1, RFIDlist + offset, 6);                            // compare read RFID with list of stored RFID's
        offset += 6;
    } while (r !=0 && offset < 114);

    if (r == 0) return offset;                                                  // return offset + 6 in RFIDlist
    else return 0;
}


// Store RFID card in memory and eeprom
// returns 1 when successful
// returns 2 when already stored
// returns 0 when all slots are full.
unsigned char StoreRFID(void) {
    unsigned char offset = 0, r;
    unsigned char empty[6] = {0xff,0xff,0xff,0xff,0xff,0xff};

    // first check if the Card ID was already stored.
    if ( MatchRFID() ) return 2;                                                // already stored, that's ok.

    do {
        r = memcmp(empty, RFIDlist + offset, 6);
        offset += 6;
    } while (r !=0 && offset < 120);
    if (r != 0) return 0;                                                       // no more room to store RFID
    offset -= 6;
    _LOG_A("offset %u ",offset);
    memcpy(RFIDlist + offset, RFID+1, 6);

    _LOG_I("\nRFIDlist:");
    for (r=0; r<120; r++) _LOG_I("%02x",RFIDlist[r]);

    WriteRFIDlist();
    return 1;
}

// Delete RFID card in memory and eeprom
// returns 1 when successful, 0 when RFID was not found
unsigned char DeleteRFID(void) {
    unsigned char offset = 0, r;

    offset = MatchRFID();                                                       // find RFID in list
    if (offset) {
        offset -= 6;
        for (r = 0; r < 6; r++) RFIDlist[offset + r] = 0xff;
    } else return 0;

    _LOG_A("deleted %u ",offset);
    for (r=0; r<120; r++) _LOG_A("%02x",RFIDlist[r]);
    
    WriteRFIDlist();
    return 1;
}

void DeleteAllRFID(void) {
    unsigned char i;

    for (i = 0; i < 120; i++) RFIDlist[i] = 0xff;
    WriteRFIDlist();
    _LOG_I("All RFID cards erased!\n");
    setItemValue(MENU_RFIDREADER, 0);                                           // RFID Reader Disabled
}

void CheckRFID(void) {
    unsigned char x;
    static unsigned char cardoffset = 0;
    // When RFID is enabled, a OneWire RFID reader is expected on the SW input
    uint8_t RFIDReader = getItemValue(MENU_RFIDREADER);
    if (RFIDReader) {                                        // RFID Reader set to Enabled, Learn or Delete
        if (OneWireReadCardId() ) {                                             // Read card ID
            switch (RFIDReader) {
                case 1:                                                         // EnableAll. All learned cards accepted for locking /unlocking
                    x = MatchRFID();
                    if (x && !RFIDstatus) {
                        _LOG_A("RFID card found!\n");
                        if (Access_bit) {
                            setAccess(false);                                   // Access Off, Switch back to state B1/C1
                        } else Access_bit = 1;

                        RFIDstatus = 1;
                    }  else if (!x) RFIDstatus = 7;                             // invalid card
                    BacklightTimer = BACKLIGHT;
                    break;
                case 2:                                                         // EnableOne. Only the card that unlocks, can re-lock the EVSE   
                    x = MatchRFID();
                    if (x && !RFIDstatus) {
                        _LOG_A("RFID card found!\n");
                        if (!Access_bit) {
                            cardoffset = x;                                     // store cardoffset from current card
                            Access_bit = 1;                                     // Access On
                        } else if (cardoffset == x) {
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
        } else RFIDstatus = 0;
    }
}

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

#ifdef SMARTEVSE_VERSION //ESP32
#include "driver/uart.h"
#else
#include "ch32v003fun.h"
#include "main.h"
#include "main_c.h"
#include "ch32.h"
#include "utils.h"
#include "meter.h"
extern "C" {
    #include "evse.h"
}
extern struct Sensorbox SB2;
extern struct EMstruct EMConfig[EM_CUSTOM + 1];
#endif

#include "modbus.h"
struct ModBus MB; //TODO do not define for ESP32v4

#if !defined(SMARTEVSE_VERSION) || (SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40) //CH32 and v3 ESP32
extern uint16_t Balanced[NR_EVSES];
extern uint8_t State;
extern int16_t Isum;
extern void setState(uint8_t NewState);
extern void receiveNodeStatus(uint8_t *buf, uint8_t NodeNr); //TODO move to modbus.cpp?
extern void receiveNodeConfig(uint8_t *buf, uint8_t NodeNr); //TODO move to modbus.cpp?
#endif

#ifdef SMARTEVSE_VERSION //ESP32
extern ModbusMessage response;

// ########################## Modbus helper functions ##########################

/**
 * Send single value over modbus
 * 
 * @param uint8_t address
 * @param uint8_t function
 * @param uint16_t register
 * @param uint16_t data
 */
void ModbusSend8(uint8_t address, uint8_t function, uint16_t reg, uint16_t data) {
    // 0x12345678 is a token to keep track of modbus requests/responses.
    // token: first byte address, second byte function, third and fourth reg
    uint32_t token;
    token = reg;
    token += address << 24;
    token += function << 16;
#if SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40
    Error err = MBclient.addRequest(token, address, function, reg, data);
    if (err!=SUCCESS) {
        ModbusError e(err);
        _LOG_A("Error creating request: 0x%02x - %s\n", (int)e, (const char *)e);
    }
    else {
        _LOG_V("Sent packet");
    }
#endif //SMARTEVSE_VERSION
    _LOG_V_NO_FUNC(" address: 0x%02x, function: 0x%02x, reg: 0x%04x, token:0x%08x, data: 0x%04x.\n", address, function, reg, token, data);
}
#else //CH32
// ########################## Modbus helper functions ##########################

/**
 * Send data over modbus
 * 
 * @param uint8_t address
 * @param uint8_t function
 * @param uint8_t byte
 * @param uint16_t pointer to values
 * @param uint8_t count of values
 */
void ModbusSend(uint8_t address, uint8_t function, uint8_t byte, uint16_t *values, uint8_t count) {
    uint16_t cs, i, n = 0;
    uint8_t Tbuffer[MODBUS_BUFFER_SIZE];

    // Device address
    Tbuffer[n++] = address;
    // Function
    Tbuffer[n++] = function;
    // The number of data bytes to follow
    if (byte) Tbuffer[n++] = byte;
    // Values
    for (i = 0; i < count; i++) {
        Tbuffer[n++] = ((uint8_t)(values[i]>>8));
        Tbuffer[n++] = ((uint8_t)(values[i]));
    }
    // Calculate CRC16 from data
    cs = crc16(Tbuffer, n);
    Tbuffer[n++] = ((uint8_t)(cs));
    Tbuffer[n++] = ((uint8_t)(cs>>8));

    printf(" address: 0x%02x, function: 0x%02x, len=%u.\n", address, function, n);
    for (i = 0; i < n; i++) printf("%02x ", Tbuffer[i]);
    printf("\n");

    // Send buffer to RS485 port
    buffer_write(&ModbusTx, (char *) &Tbuffer, n);
    // switch RS485 transceiver to transmit
    funDigitalWrite(RS485_DIR, FUN_HIGH);
    // enable transmit interrupt
    USART2->CTLR1 |= USART_CTLR1_TXEIE;
}

/**
 * Send single value over modbus
 * 
 * @param uint8_t address
 * @param uint8_t function
 * @param uint16_t register
 * @param uint16_t data
 */
void ModbusSend8(uint8_t address, uint8_t function, uint16_t reg, uint16_t data) {
    uint16_t values[2];

    values[0] = reg;
    values[1] = data;
    
    ModbusSend(address, function, 0, values, 2);
}
#endif

// ########################### Modbus main functions ###########################

#if !defined(SMARTEVSE_VERSION) || (SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40) //CH32 and v3 ESP32

/**
 * Request read holding (FC=3) or read input register (FC=04) to a device over modbus
 * 
 * @param uint8_t address
 * @param uint8_t function
 * @param uint16_t register
 * @param uint16_t quantity
 */
void ModbusReadInputRequest(uint8_t address, uint8_t function, uint16_t reg, uint16_t quantity) {
    MB.RequestAddress = address;
    MB.RequestFunction = function;
    MB.RequestRegister = reg;
    ModbusSend8(address, function, reg, quantity);
}


/**
 * Request write single register (FC=06) to a device over modbus
 * 
 * @param uint8_t address
 * @param uint16_t register
 * @param uint16_t value
 */
void ModbusWriteSingleRequest(uint8_t address, uint16_t reg, uint16_t value) {
    MB.RequestAddress = address;
    MB.RequestFunction = 0x06;
    MB.RequestRegister = reg;
    ModbusSend8(address, 0x06, reg, value);  
}
#endif

#ifdef SMARTEVSE_VERSION //ESP32

/**
 * Request write multiple register (FC=16) to a device over modbus
 * 
 * @param uint8_t address
 * @param uint16_t register
 * @param uint8_t pointer to data
 * @param uint8_t count of data
 */
void ModbusWriteMultipleRequest(uint8_t address, uint16_t reg, uint16_t *values, uint8_t count) {

    MB.RequestAddress = address;
    MB.RequestFunction = 0x10;
    MB.RequestRegister = reg;
    // 0x12345678 is a token to keep track of modbus requests/responses.
    // token: first byte address, second byte function, third and fourth reg
    uint32_t token;
    token = reg;
    token += address << 24;
    token += 0x10 << 16;
#if SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40
    Error err = MBclient.addRequest(token, address, 0x10, reg, (uint16_t) count, count * 2u, values);
    if (err!=SUCCESS) {
      ModbusError e(err);
      _LOG_A("Error creating request: 0x%02x - %s\n", (int)e, (const char *)e);
    }
    _LOG_V("Sent packet address: 0x%02x, function: 0x10, reg: 0x%04x, token: 0x%08x count: %u, values:", address, reg, token, count);
    for (uint16_t i = 0; i < count; i++) {
        _LOG_V_NO_FUNC(" %04x", values[i]);
    }
#endif
    _LOG_V_NO_FUNC("\n");
}

/**
 * Response an exception
 * 
 * @param uint8_t address
 * @param uint8_t function
 * @param uint8_t exeption
 */
void ModbusException(uint8_t address, uint8_t function, uint8_t exception) {
    response.setError(address, function, (Modbus::Error) exception);
}

#else //CH32


// ########################### Modbus main functions ###########################


/**
 * Request write multiple register (FC=16) to a device over modbus
 * 
 * @param uint8_t address
 * @param uint16_t register
 * @param uint8_t pointer to data
 * @param uint8_t count of data
 */
void ModbusWriteMultipleRequest(uint8_t address, uint16_t reg, uint16_t *values, uint8_t count) {
    uint16_t i, n = 0, cs;
    uint8_t Tbuffer[MODBUS_BUFFER_SIZE];

    MB.RequestAddress = address;
    MB.RequestFunction = 0x10;
    MB.RequestRegister = reg;
    
    // Device Address
    Tbuffer[n++] = address;
    // Function Code 16
    Tbuffer[n++] = 0x10;
    // Data Address of the first register
    Tbuffer[n++] = ((uint8_t)(reg>>8));
    Tbuffer[n++] = ((uint8_t)(reg));
    // Number of registers to write
    Tbuffer[n++] = 0x00;
    Tbuffer[n++] = count;
    // Number of data bytes to follow (2 registers x 2 bytes each = 4 bytes)
    Tbuffer[n++] = count * 2u;
    // Values
    for (i = 0; i < count; i++) {
        Tbuffer[n++] = ((uint8_t)(values[i]>>8));
        Tbuffer[n++] = ((uint8_t)(values[i]));
    }
    // Calculate CRC16 from data
    cs = crc16(Tbuffer, n);
    Tbuffer[n++] = ((uint8_t)(cs));
    Tbuffer[n++] = ((uint8_t)(cs>>8));	
    // Send buffer to RS485 port
    buffer_write(&ModbusTx, (char *) &Tbuffer, n);
    // switch RS485 transceiver to transmit
    funDigitalWrite(RS485_DIR, FUN_HIGH);
    // enable transmit interrupt
    USART2->CTLR1 |= USART_CTLR1_TXEIE;  
}

/**
 * Response an exception
 * 
 * @param uint8_t address
 * @param uint8_t function
 * @param uint8_t exeption
 */
void ModbusException(uint8_t address, uint8_t function, uint8_t exception) {
    uint16_t temp[1];
    ModbusSend(address, function, exception, temp, 0);
}


#endif

/**
 * Decode received modbus packet
 * 
 * @param uint8_t pointer to buffer
 * @param uint8_t length of buffer
 */
void ModbusDecode(uint8_t * buf, uint8_t len) {
    // Clear old values
    MB.Address = 0;
    MB.Function = 0;
    MB.Register = 0;
    MB.RegisterCount = 0;
    MB.Value = 0;
    MB.DataLength = 0;
    MB.Type = MODBUS_INVALID;
    MB.Exception = 0;

    _LOG_V("Received packet (%i bytes)", len);
    for (uint8_t x=0; x<len; x++) {
        _LOG_V_NO_FUNC(" %02x", buf[x]);
    }
    _LOG_V_NO_FUNC("\n");
#ifndef SMARTEVSE_VERSION //CH32
    if (len <= 4 || crc16(buf, len)) {//ESP32 has crc checked in modbus library
        _LOG_A("Modbus CRC16 error, len=%d!", len);
        return;
    }
    //ESP32 has crc16 chopped off:
    len = len - 2;
#endif
    // Modbus error packets length is 5 bytes
    if (len == 3) {
        MB.Type = MODBUS_EXCEPTION;
        // Modbus device address
        MB.Address = buf[0];
        // Modbus function
        MB.Function = buf[1];
        // Modbus Exception code
        MB.Exception = buf[2];
        _LOG_A("Modbus Exception 0x%02x, Address=0x%02x, Function=0x%02x.\n", MB.Exception, MB.Address, MB.Function);
    // Modbus data packets minimum length is 7 bytes (with one 16-bit register = two bytes of data.)
    } else if (len >= 5) {
        // Modbus device address
        MB.Address = buf[0];
        // Modbus function
        MB.Function = buf[1];

        _LOG_V(" valid Modbus packet: Address 0x%02x Function 0x%02x", MB.Address, MB.Function);
        switch (MB.Function) {
            case 0x03: // (Read holding register)
            case 0x04: // (Read input register)
                if (len == 6) {
                    // request packet
                    MB.Type = MODBUS_REQUEST;
                    // Modbus register
                    MB.Register = (uint16_t)(buf[2] <<8) | buf[3];
                    // Modbus register count
                    MB.RegisterCount = (uint16_t)(buf[4] <<8) | buf[5];
                } else {
                    // Modbus datacount
                    MB.DataLength = buf[2];
                    if (MB.DataLength == len - 3) {
                        // packet length OK
                        // response packet
                        MB.Type = MODBUS_RESPONSE;
                    } else {
                        _LOG_W("Invalid modbus FC=04 packet\n");
                    }
                }
                break;
            case 0x06:
                // (Write single register)
                if (len == 6) {
                    // request and response packet are the same
                    MB.Type = MODBUS_OK;
                    // Modbus register
                    MB.Register = (uint16_t)(buf[2] <<8) | buf[3];
                    // Modbus register count
                    MB.RegisterCount = 1;
                    // value
                    MB.Value = (uint16_t)(buf[4] <<8) | buf[5];
                } else {
                    _LOG_W("Invalid modbus FC=06 packet\n");
                }
                break;
            case 0x10:
                // (Write multiple register))
                // Modbus register
                MB.Register = (uint16_t)(buf[2] <<8) | buf[3];
                // Modbus register count
                MB.RegisterCount = (uint16_t)(buf[4] <<8) | buf[5];
                if (len == 6) {
                    // response packet
                    MB.Type = MODBUS_RESPONSE;
                } else {
                    // Modbus datacount
                    MB.DataLength = buf[6];
                    if (MB.DataLength == len - 7) {
                        // packet length OK
                        // request packet
                        MB.Type = MODBUS_REQUEST;
                    } else {
                        _LOG_W("Invalid modbus FC=16 packet\n");
                    }
                }
                break;
            default:
                break;
        }

        // MB.Data
        if (MB.Type && MB.DataLength) {
            // Set pointer to Data
            MB.Data = buf;
            // Modbus data is always at the end ahead the checksum
            MB.Data = MB.Data + (len - MB.DataLength);
        }
        
        // Request - Response check
        switch (MB.Type) {
            case MODBUS_REQUEST:
                MB.RequestAddress = MB.Address;
                MB.RequestFunction = MB.Function;
                MB.RequestRegister = MB.Register;
                break;
            case MODBUS_RESPONSE:
                // If address and function identical with last send or received request, it is a valid response
                if (MB.Address == MB.RequestAddress && MB.Function == MB.RequestFunction) {
                    if (MB.Function == 0x03 || MB.Function == 0x04) 
                        MB.Register = MB.RequestRegister;
                }
                MB.RequestAddress = 0;
                MB.RequestFunction = 0;
                MB.RequestRegister = 0;
                break;
            case MODBUS_OK:
                // If address and function identical with last send or received request, it is a valid response
                if (MB.Address == MB.RequestAddress && MB.Function == MB.RequestFunction && MB.Address != BROADCAST_ADR) {
                    MB.Type = MODBUS_RESPONSE;
                    MB.RequestAddress = 0;
                    MB.RequestFunction = 0;
                    MB.RequestRegister = 0;
                } else {
                    MB.Type = MODBUS_REQUEST;
                    MB.RequestAddress = MB.Address;
                    MB.RequestFunction = MB.Function;
                    MB.RequestRegister = MB.Register;
                }
            default:
                break;
        }
    }
    if(MB.Type) {
        _LOG_V_NO_FUNC(" Register 0x%04x", MB.Register);
    }
    switch (MB.Type) {
        case MODBUS_REQUEST:
            _LOG_V_NO_FUNC(" Request\n");
            break;
        case MODBUS_RESPONSE:
            _LOG_V_NO_FUNC(" Response\n");
            break;
    }
}
// ########################### EVSE modbus functions ###########################


/**
 * Send measurement request over modbus
 * 
 * @param uint8_t Meter
 * @param uint8_t Address
 * @param uint16_t Register
 * @param uint8_t Count
 */
void requestMeasurement(uint8_t Meter, uint8_t Address, uint16_t Register, uint8_t Count) {
    ModbusReadInputRequest(Address, EMConfig[Meter].Function, Register, (EMConfig[Meter].DataType == MB_DATATYPE_INT16 ? Count : (Count * 2u)));
}

/**
 * Send current measurement request over modbus
 * 
 * @param uint8_t Meter
 * @param uint8_t Address
 */
void requestCurrentMeasurement(uint8_t Meter, uint8_t Address) {
    switch(Meter) {
        case EM_API:
        case EM_HOMEWIZARD_P1:
            break;
        case EM_SENSORBOX:
            if (SB2.SoftwareVer >= 1) {
                ModbusReadInputRequest(Address, 4, 0, 32);
            } else ModbusReadInputRequest(Address, 4, 0, 20);
            break;
        case EM_EASTRON1P:
        case EM_EASTRON3P:
        case EM_EASTRON3P_INV:
            // Phase 1-3 current: Register 0x06 - 0x0B (unsigned)
            // Phase 1-3 power:   Register 0x0C - 0x11 (signed)
            ModbusReadInputRequest(Address, 4, 0x06, 12);
            break;
        case EM_ABB:
            // Phase 1-3 current: Register 0x5B0C - 0x5B11 (unsigned)
            // Phase 1-3 power:   Register 0x5B16 - 0x5B1B (signed)
            ModbusReadInputRequest(Address, 3, 0x5B0C, 16);
            break;
        case EM_SOLAREDGE:
            // Read 3 Current values + scaling factor
            ModbusReadInputRequest(Address, EMConfig[Meter].Function, EMConfig[Meter].IRegister, 4);
            break;
        case EM_FINDER_7M:
            // Phase 1-3 current: Register 2516 - 2521 (unsigned)
            // Phase 1-3 power:   Register 2530 - 2535 (signed)
            ModbusReadInputRequest(Address, 4, 2516, 20);
            break;
        default:
            // Read 3 Current values
            requestMeasurement(Meter, Address, EMConfig[Meter].IRegister, 3);
            break;
    }  
}


#if !defined(SMARTEVSE_VERSION) || (SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40) //CH32 and v3 ESP32
/**
 * Map a Modbus register to an item ID (MENU_xxx or STATUS_xxx)
 * 
 * @return uint8_t ItemID
 */
uint8_t mapModbusRegister2ItemID() {
    uint16_t RegisterStart, ItemStart, Count;

    // Register 0x00*: Status
    if (MB.Register < (MODBUS_EVSE_STATUS_START + MODBUS_EVSE_STATUS_COUNT)) {
        RegisterStart = MODBUS_EVSE_STATUS_START;
        ItemStart = STATUS_STATE;
        Count = MODBUS_EVSE_STATUS_COUNT;

    // Register 0x01*: Node specific configuration
    } else if (MB.Register >= MODBUS_EVSE_CONFIG_START && MB.Register < (MODBUS_EVSE_CONFIG_START + MODBUS_EVSE_CONFIG_COUNT)) {
        RegisterStart = MODBUS_EVSE_CONFIG_START;
        ItemStart = MENU_CONFIG;
        Count = MODBUS_EVSE_CONFIG_COUNT;

    // Register 0x02*: System configuration (same on all SmartEVSE in a LoadBalancing setup)
    } else if (MB.Register >= MODBUS_SYS_CONFIG_START && MB.Register < (MODBUS_SYS_CONFIG_START + MODBUS_SYS_CONFIG_COUNT)) {
        RegisterStart = MODBUS_SYS_CONFIG_START;
        ItemStart = MENU_MODE;
        Count = MODBUS_SYS_CONFIG_COUNT;

    } else {
        return 0;
    }
    
    if (MB.RegisterCount <= (RegisterStart + Count) - MB.Register) {
        return (MB.Register - RegisterStart + ItemStart);
    } else {
        return 0;
    }
}


/**
 * Read item values and send modbus response
 */
void ReadItemValueResponse(void) {
    uint8_t ItemID;
    uint8_t i;
    uint16_t values[MODBUS_MAX_REGISTER_READ];

    ItemID = mapModbusRegister2ItemID();
    if (ItemID) {
        for (i = 0; i < MB.RegisterCount; i++) {
            values[i] = getItemValue(ItemID + i);
        }
        // ModbusReadInputResponse:
#if SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40
        response.add(MB.Address, MB.Function, (uint8_t)(MB.RegisterCount * 2));
        for (int i = 0; i < MB.RegisterCount; i++) {
            response.add(values[i]);
        }
#else //CH32
        ModbusSend(MB.Address, MB.Function, MB.RegisterCount * 2u, values, MB.RegisterCount);
#endif
    } else {
        ModbusException(MB.Address, MB.Function, MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS);
    }
}


/**
 * Write item values and send modbus response
 */
void WriteItemValueResponse(void) {
    uint8_t ItemID;
    uint8_t OK = 0;

    ItemID = mapModbusRegister2ItemID();
    if (ItemID) {
        OK = setItemValue(ItemID, MB.Value);
    }
    _LOG_V("Broadcast received FC06 Item:%u val:%u\n",ItemID, MB.Value);

    if (OK && ItemID < STATUS_STATE) {
#if !defined(SMARTEVSE_VERSION) //CH32
        printf("write_settings\n");
#else
        write_settings();
#endif
    }

    if (MB.Address != BROADCAST_ADR || LoadBl == 0) {
        if (!ItemID) {
            ModbusException(MB.Address, MB.Function, MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS);
        } else if (!OK) {
            ModbusException(MB.Address, MB.Function, MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE);
        } else {
            //ModbusWriteSingleResponse(MB.Address, MB.Register, MB.Value);
            ModbusSend8(MB.Address, 0x06, MB.Register, MB.Value);
        }
    }
}


/**
 * Write multiple item values and send modbus response
 */
void WriteMultipleItemValueResponse(void) {
    uint8_t ItemID;
    uint16_t i, OK = 0, value;

    ItemID = mapModbusRegister2ItemID();
    if (ItemID) {
        for (i = 0; i < MB.RegisterCount; i++) {
            value = (MB.Data[i * 2] <<8) | MB.Data[(i * 2) + 1];
            OK += setItemValue(ItemID + i, value);
        }
    }

    if (OK && ItemID < STATUS_STATE) {
#if !defined(SMARTEVSE_VERSION) //CH32
        printf("write_settings\n");
#else
        write_settings();
#endif
    }

    if (MB.Address != BROADCAST_ADR || LoadBl == 0) {
        if (!ItemID) {
            ModbusException(MB.Address, MB.Function, MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS);
        } else if (!OK) {
            ModbusException(MB.Address, MB.Function, MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE);
        } else  {
            //ModbusWriteMultipleResponse(MB.Address, MB.Register, OK);
            ModbusSend8(MB.Address, 0x10, MB.Register, OK);
        }
    }
}
#endif

#if !defined(SMARTEVSE_VERSION) || (SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40) //CH32 and v3 ESP32
void HandleModbusRequest(void) {
        // Broadcast or addressed to this device
        switch (MB.Function) {
            // FC 03 and 04 are not possible with broadcast messages.
            case 0x03: // (Read holding register)
            case 0x04: // (Read input register)
                // Addressed to this device
                _LOG_V("read register(s) ");
                if (MB.Address != BROADCAST_ADR) {
                    ReadItemValueResponse();
                }
                break;
            case 0x06: // (Write single register)
                WriteItemValueResponse();
                break;
            case 0x10: // (Write multiple register))
                // 0x0020: Balance currents
                if (MB.Register == 0x0020 && LoadBl > 1) {      // Message for Node(s)
                    Balanced[0] = (MB.Data[(LoadBl - 1) * 2] <<8) | MB.Data[(LoadBl - 1) * 2 + 1];
                    if (Balanced[0] == 0 && State == STATE_C) setState(STATE_C1);               // tell EV to stop charging if charge current is zero
                    else if ((State == STATE_B) || (State == STATE_C)) SetCurrent(Balanced[0]); // Set charge current, and PWM output
                    MainsMeter.setTimeout(COMM_TIMEOUT);                          // reset 10 second timeout
                    _LOG_V("Broadcast received, Node %.1f A, MainsMeter Irms ", (float) Balanced[0]/10);

                    //now decode registers 0x0028-0x002A
                    if (MB.DataLength >= 16+6) {
                        Isum = 0;
                        for (int i=0; i<3; i++ ) {
                            int16_t combined = (MB.Data[(i * 2) + 16] <<8) + MB.Data[(i * 2) + 17]; 
                            Isum = Isum + combined;
                            MainsMeter.Irms[i] = combined;
                            _LOG_V_NO_FUNC("L%i=%.1fA,", i+1, (float)MainsMeter.Irms[i]/10);
                        }
#ifndef SMARTEVSE_VERSION //CH32
                        printf("Irms@%03u,%d,%d,%d\n", MainsMeter.Address, MainsMeter.Irms[0], MainsMeter.Irms[1], MainsMeter.Irms[2]); //Irms@011,312,123,124 means: the meter on address 11(dec) has MainsMeter.Irms[0] 312 dA, MainsMeter.Irms[1] of 123 dA, MainsMeter.Irms[2] of 124 dA.
#endif
                        _LOG_V_NO_FUNC("\n");
                    }
                } else {

                    WriteMultipleItemValueResponse();
                    _LOG_V("Other Broadcast received\n");
                }
                break;
            default:
                break;
        }
}


void HandleModbusResponse(void) {
    //printf("MSG: Modbus Response Address %u / Function %02x / Register %02x\n",MB.Address,MB.Function,MB.Register);
    switch (MB.Function) {
        case 0x03: // (Read holding register)
        case 0x04: // (Read input register)
            if (MainsMeter.Type && MB.Address == MainsMeter.Address) {
                MainsMeter.ResponseToMeasurement(MB);
            } else if (EVMeter.Type && MB.Address == EVMeter.Address) {
                EVMeter.ResponseToMeasurement(MB);
            } else if (LoadBl == 1 && MB.Address > 1 && MB.Address <= NR_EVSES) {
                // Packet from a Node EVSE, only for Master!
                if (MB.Register == 0x0000) {
                    // Node status
                    receiveNodeStatus(MB.Data, MB.Address - 1u);
                }  else if (MB.Register == 0x0108) {
                    // Node configuration
                    receiveNodeConfig(MB.Data, MB.Address - 1u);
                }
            }
            break;
        default:
            break;
    }
}
#endif

#if SMARTEVSE_VERSION >=30 && SMARTEVSE_VERSION < 40
ModbusMessage response;     // response message to be sent back
// Request handler for modbus messages addressed to -this- Node/Slave EVSE.
// Sends response back to Master
//
ModbusMessage MBNodeRequest(ModbusMessage request) {
    response.clear(); //clear global response message

    // Check if the call is for our current ServerID, or maybe for an old ServerID?
    if (LoadBl != request.getServerID()) return NIL_RESPONSE;

    ModbusDecode( (uint8_t*)request.data(), request.size());
    HandleModbusRequest();
  return response;
}


// Monitor EV Meter responses, and update Enery and Power and Current measurements
// Both the Master and Nodes will receive their own EV meter measurements here.
// Does not send any data back.
//
ModbusMessage MBEVMeterResponse(ModbusMessage request) {
    ModbusDecode( (uint8_t*)request.data(), request.size());
    EVMeter.ResponseToMeasurement(MB);
    // As this is a response to an earlier request, do not send response.

    return NIL_RESPONSE;
}


// The Node/Server receives a broadcast message from the Master
// Does not send any data back.
ModbusMessage MBbroadcast(ModbusMessage request) {
    ModbusDecode( (uint8_t*)request.data(), request.size());
    if (MB.Type == MODBUS_REQUEST) {
        HandleModbusRequest();
    }

    // As it is a broadcast message, do not send response.
    return NIL_RESPONSE;
}


// Data handler for Master
// Responses from Slaves/Nodes are handled here
void MBhandleData(ModbusMessage msg, uint32_t token)
{
    ModbusDecode( (uint8_t*)msg.data(), msg.size());
    if (MB.Function > 4) {
        _LOG_A("WARNING: response NOT HANDLED: address: %02x, function: %02x, reg: %04x.\n", MB.Address, MB.Function, MB.Register);
    }
    HandleModbusResponse(); //now responses from functions other then 3 or 4 are not handled?!?
}


void MBhandleError(Error error, uint32_t token)
{
  // ModbusError wraps the error code and provides a readable error message for it
  ModbusError me(error);
  uint8_t address, function;
  uint16_t reg;
  address = token >> 24;
  function = (token >> 16);
  reg = token & 0xFFFF;

  if (LoadBl == 1 && ((address>=2 && address <=8 && function == 4 && reg == 0) || address == 9)) {  //master sends out messages to nodes 2-8, if no EVSE is connected with that address
                                                                                //a timeout will be generated. This is legit!
                                                                                //same goes for broadcast address 9
    _LOG_V("Error response: %02X - %s, address: %02x, function: %02x, reg: %04x.\n", error, (const char *)me,  address, function, reg);
  }
  else {
    _LOG_A("Error response: %02X - %s, address: %02x, function: %02x, reg: %04x.\n", error, (const char *)me,  address, function, reg);
  }
}


void ConfigureModbusMode(uint8_t newmode) {

    _LOG_A("changing LoadBl from %u to %u\n",LoadBl, newmode);

    if ((LoadBl < 2 && newmode > 1) || (LoadBl > 1 && newmode < 2) || (newmode == 255) ) {

        if (newmode != 255 ) LoadBl = newmode;

        // Setup Modbus workers for Node
        if (LoadBl > 1 ) {

            _LOG_A("Setup MBserver/Node workers, end Master/Client\n");
            // Stop Master background task (if active)
            if (newmode != 255 ) MBclient.end();
            _LOG_A("ConfigureModbusMode1 task free ram: %u\n", uxTaskGetStackHighWaterMark( NULL ));

            // Register worker. at serverID 'LoadBl', all function codes
            MBserver.registerWorker(LoadBl, ANY_FUNCTION_CODE, &MBNodeRequest);
            // Also add handler for all broadcast messages from Master.
            MBserver.registerWorker(BROADCAST_ADR, ANY_FUNCTION_CODE, &MBbroadcast);

            if (EVMeter.Type && EVMeter.Type != EM_API) MBserver.registerWorker(EVMeter.Address, ANY_FUNCTION_CODE, &MBEVMeterResponse);

            // Start ModbusRTU Node background task
            MBserver.begin(Serial1);

        } else if (LoadBl < 2 ) {
            // Setup Modbus workers as Master
            // Stop Node background task (if active)
            _LOG_A("Setup Modbus as Master/Client, stop Server/Node handler\n");

            if (newmode != 255) MBserver.end();
            _LOG_A("ConfigureModbusMode2 task free ram: %u\n", uxTaskGetStackHighWaterMark( NULL ));

            MBclient.setTimeout(85);                        // Set modbus timeout to 85ms. 15ms lower then modbusRequestloop time of 100ms.
            MBclient.onDataHandler(&MBhandleData);
            MBclient.onErrorHandler(&MBhandleError);
            // Start ModbusRTU Master background task
            MBclient.begin(Serial1, 1);                                         //pinning it to core1 reduces modbus problems
        }
    } else if (newmode > 1) {
        // Register worker. at serverID 'LoadBl', all function codes
        _LOG_A("Registering new LoadBl worker at id %u\n", newmode);
        LoadBl = newmode;
        MBserver.registerWorker(newmode, ANY_FUNCTION_CODE, &MBNodeRequest);
    }

}

#endif


#ifndef SMARTEVSE_VERSION //CH32
// printf can be slow.
// By measuring the time the 10ms loop actually takes to execute we found that:
// it takes ~625uS to execute when using printf (and tx interrrupts)
// ~151uS without printf (with tx interrupt)
// and only ~26uS when using DMA
// printf with Circular DMA buffer takes ~536uS
// current version with snprintf takes ~296uS
//
// Called by 10ms loop when new modbus data is available
// ModbusRxLen contains length of data contained in array ModbusRx
void CheckRS485Comm(void) { //looks like MBhandleData
    ModbusDecode(ModbusRx, ModbusRxLen);

    // Data received is a response to an earlier request from the master.
    if (MB.Type == MODBUS_RESPONSE) {
        HandleModbusResponse();
    // Data received is a request from the master to a device on the bus.
    } else if (MB.Type == MODBUS_REQUEST) { //looks like MBBroadcast
        //printf("Modbus Request Address %u / Function %02x / Register %02x\n",MB.Address,MB.Function,MB.Register);

        // Broadcast or addressed to this device
        if (MB.Address == BROADCAST_ADR || (LoadBl > 0 && MB.Address == LoadBl)) {
            HandleModbusRequest();
        }
    } else if (MB.Type == MODBUS_EXCEPTION) {
        _LOG_D("Modbus Address %02x exception %u received\n", MB.Address, MB.Exception);
    } else {
        _LOG_D("\nCRC invalid\n");
    }




//    char buf[256];
//    for (uint8_t x=0; x<ModbusRxLen; x++) snprintf(buf+(x*3), 4, "%02X ", ModbusRx[x]);
//    printf("MB:%s\n", buf);

    ModbusRxLen = 0;

}

#endif

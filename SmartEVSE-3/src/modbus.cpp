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
#include "driver/uart.h"

#include "modbus.h"

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
    Error err = MBclient.addRequest(token, address, function, reg, data);
    if (err!=SUCCESS) {
        ModbusError e(err);
        _LOG_A("Error creating request: 0x%02x - %s\n", (int)e, (const char *)e);
    }
    else {
        _LOG_V("Sent packet");
    }
    _LOG_V_NO_FUNC(" address: 0x%02x, function: 0x%02x, reg: 0x%04x, token:0x%08x, data: 0x%04x.\n", address, function, reg, token, data);
}


// ########################### Modbus main functions ###########################



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
 * Response read holding (FC=3) or read input register (FC=04) to a device over modbus
 * 
 * @param uint8_t address
 * @param uint8_t function
 * @param uint16_t pointer to values
 * @param uint8_t count of values
 */
void ModbusReadInputResponse(uint8_t address, uint8_t function, uint16_t *values, uint8_t count) {
    _LOG_A("ModbusReadInputResponse, to do!\n");
    //ModbusSend(address, function, count * 2u, values, count);
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
    Error err = MBclient.addRequest(token, address, 0x10, reg, (uint16_t) count, count * 2u, values);
    if (err!=SUCCESS) {
      ModbusError e(err);
      _LOG_A("Error creating request: 0x%02x - %s\n", (int)e, (const char *)e);
    }
    _LOG_V("Sent packet address: 0x%02x, function: 0x10, reg: 0x%04x, token: 0x%08x count: %u, values:", address, reg, token, count);
    for (uint16_t i = 0; i < count; i++) {
        _LOG_V_NO_FUNC(" %04x", values[i]);
    }
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
    //uint16_t temp[1];
    _LOG_A("ModbusException, to do!\n");
    //ModbusSend(address, function, exception, temp, 0);
}

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
            break;
        case EM_SENSORBOX:
            ModbusReadInputRequest(Address, 4, 0, 20);
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
/*
void ReadItemValueResponse(void) {
    uint8_t ItemID;
    uint8_t i;
    uint16_t values[MODBUS_MAX_REGISTER_READ];

    ItemID = mapModbusRegister2ItemID();
    if (ItemID) {
        for (i = 0; i < MB.RegisterCount; i++) {
            values[i] = getItemValue(ItemID + i);
        }
        ModbusReadInputResponse(MB.Address, MB.Function, values, MB.RegisterCount);
    } else {
        ModbusException(MB.Address, MB.Function, MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS);
    }
}
*/

/**
 * Write item values and send modbus response
 */
/*
void WriteItemValueResponse(void) {
    uint8_t ItemID;
    uint8_t OK = 0;

    ItemID = mapModbusRegister2ItemID();
    if (ItemID) {
        OK = setItemValue(ItemID, MB.Value);
    }

    if (OK && ItemID < STATUS_STATE) write_settings();

    if (MB.Address != BROADCAST_ADR || LoadBl == 0) {
        if (!ItemID) {
            ModbusException(MB.Address, MB.Function, MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS);
        } else if (!OK) {
            ModbusException(MB.Address, MB.Function, MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE);
        } else {
            ModbusWriteSingleResponse(MB.Address, MB.Register, MB.Value);
        }
    }
}
*/

/**
 * Write multiple item values and send modbus response
 */
/*
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

    if (OK && ItemID < STATUS_STATE) write_settings();

    if (MB.Address != BROADCAST_ADR || LoadBl == 0) {
        if (!ItemID) {
            ModbusException(MB.Address, MB.Function, MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS);
        } else if (!OK) {
            ModbusException(MB.Address, MB.Function, MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE);
        } else  {
            ModbusWriteMultipleResponse(MB.Address, MB.Register, OK);
        }
    }
}
*/

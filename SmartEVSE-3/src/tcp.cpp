#if SMARTEVSE_VERSION >= 40
#include <Arduino.h>
#include "qca.h"
#include "ipv6.h"
#include "tcp.h"
#include "exi/projectExiConnector.h"
#include "debug.h"
#include "esp32.h"

/* Todo: implement a retry strategy, to cover the situation that single packets are lost on the way. */

#define NEXT_TCP 0x06  // the next protocol is TCP

#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10

uint8_t tcpHeaderLen;
#define TCP_PAYLOAD_LEN 200
uint8_t tcpPayloadLen;
uint8_t tcpPayload[TCP_PAYLOAD_LEN];


#define TCP_ACTIVITY_TIMER_START (5*33) /* 5 seconds */
uint16_t tcpActivityTimer;

#define TCP_TRANSMIT_PACKET_LEN 200
uint8_t TcpTransmitPacketLen;
uint8_t TcpTransmitPacket[TCP_TRANSMIT_PACKET_LEN];

#define TCPIP_TRANSMIT_PACKET_LEN 200
uint8_t TcpIpRequestLen;
uint8_t TcpIpRequest[TCPIP_TRANSMIT_PACKET_LEN];

#define TCP_STATE_CLOSED 0
#define TCP_STATE_SYN_ACK 1
#define TCP_STATE_ESTABLISHED 2
#define TCP_RECEIVE_WINDOW 1000 /* number of octets we are able to receive */

uint8_t tcpState = TCP_STATE_CLOSED;
uint32_t TcpSeqNr;
uint32_t TcpAckNr;

#define TCP_RX_DATA_LEN 1000
uint8_t tcp_rxdataLen=0;
uint8_t tcp_rxdata[TCP_RX_DATA_LEN];

#define stateWaitForSupportedApplicationProtocolRequest 0
#define stateWaitForSessionSetupRequest 1
#define stateWaitForServiceDiscoveryRequest 2
#define stateWaitForServicePaymentSelectionRequest 3
#define stateWaitForContractAuthenticationRequest 4
#define stateWaitForChargeParameterDiscoveryRequest 5
#define stateWaitForCableCheckRequest 6
#define stateWaitForPreChargeRequest 7
#define stateWaitForPowerDeliveryRequest 8

uint8_t fsmState = stateWaitForSupportedApplicationProtocolRequest;

extern char EVCCID[32];
extern int8_t InitialSoC, ComputedSoC, FullSoC;
extern void setState(uint8_t NewState);
extern void RecomputeSoC(void);
extern int32_t EnergyCapacity, EnergyRequest;

void routeDecoderInputData(void) {
    /* connect the data from the TCP to the exiDecoder */
    /* The TCP receive data consists of two parts: 1. The V2GTP header and 2. the EXI stream.
        The decoder wants only the EXI stream, so we skip the V2GTP header.
        In best case, we would check also the consistency of the V2GTP header here.
    */
    global_streamDec.data = &tcp_rxdata[V2GTP_HEADER_SIZE];
    global_streamDec.size = tcp_rxdataLen - V2GTP_HEADER_SIZE;

    /* We have something to decode, this is a good sign that the connection is fine.
        Inform the ConnectionManager that everything is fine. */
    //connMgr_ApplOk();
}


void tcp_transmit(void) {

    if (tcpState == TCP_STATE_ESTABLISHED) {
        tcpHeaderLen = 20; /* 20 bytes normal header, no options */
        if (tcpPayloadLen+tcpHeaderLen < TCP_TRANSMIT_PACKET_LEN) {
            memcpy(&TcpTransmitPacket[tcpHeaderLen], tcpPayload, tcpPayloadLen);
            tcp_prepareTcpHeader(TCP_FLAG_PSH + TCP_FLAG_ACK); /* data packets are always sent with flags PUSH and ACK. */
            tcp_packRequestIntoIp();
        } else {
            _LOG_W("Error: tcpPayload and header do not fit into TcpTransmitPacket.\n");
        }
    }
}


void addV2GTPHeaderAndTransmit(const uint8_t *exiBuffer, uint8_t exiBufferLen) {
    // takes the bytearray with exidata, and adds a header to it, according to the Vehicle-to-Grid-Transport-Protocol
    // V2GTP header has 8 bytes
    // 1 byte protocol version
    // 1 byte protocol version inverted
    // 2 bytes payload type
    // 4 byte payload length
    tcpPayload[0] = 0x01; // version
    tcpPayload[1] = 0xfe; // version inverted
    tcpPayload[2] = 0x80; // payload type. 0x8001 means "EXI data"
    tcpPayload[3] = 0x01; //
    tcpPayload[4] = (uint8_t)(exiBufferLen >> 24); // length 4 byte.
    tcpPayload[5] = (uint8_t)(exiBufferLen >> 16);
    tcpPayload[6] = (uint8_t)(exiBufferLen >> 8);
    tcpPayload[7] = (uint8_t)exiBufferLen;
    if (exiBufferLen+8 < TCP_PAYLOAD_LEN) {
        memcpy(tcpPayload+8, exiBuffer, exiBufferLen);
        tcpPayloadLen = 8 + exiBufferLen; /* 8 byte V2GTP header, plus the EXI data */
        tcp_transmit();
    } else {
        _LOG_W("Error: EXI does not fit into tcpPayload.\n");
    }
}


void decodeV2GTP(void) {

    uint16_t arrayLen, i;
    uint8_t strNamespace[50];
    uint8_t SchemaID, n;
    uint16_t NamespaceLen;

    routeDecoderInputData();
    if (fsmState) projectExiConnector_decode_DinExiDocument();      // Decode DIN EXI
    else projectExiConnector_decode_appHandExiDocument();           // Decode Handshake EXI (on first state only)
    tcp_rxdataLen = 0; /* mark the input data as "consumed" */

    if (fsmState == stateWaitForSupportedApplicationProtocolRequest) {

        // Check if we have received the correct message
        if (aphsDoc.supportedAppProtocolReq_isUsed) {

            _LOG_I("SupportedApplicationProtocolRequest\n");
            // process data when no errors occured during decoding
            if (g_errn == 0) {
                arrayLen = aphsDoc.supportedAppProtocolReq.AppProtocol.arrayLen;
                _LOG_I("The car supports %u schemas.\n", arrayLen);

                // check all schemas for DIN
                for(n=0; n<arrayLen; n++) {

                    memset(strNamespace, 0, 50);
                    NamespaceLen = aphsDoc.supportedAppProtocolReq.AppProtocol.array[n].ProtocolNamespace.charactersLen;
                    SchemaID = aphsDoc.supportedAppProtocolReq.AppProtocol.array[n].SchemaID;
                    for (i=0; i< NamespaceLen; i++) {
                        strNamespace[i] = aphsDoc.supportedAppProtocolReq.AppProtocol.array[n].ProtocolNamespace.characters[i];
                    }
                    _LOG_I("strNameSpace %s SchemaID: %u\n", strNamespace, SchemaID);

                    if (memmem((const char*)strNamespace, NamespaceLen, ":din:70121:", 11) != NULL) {
                        _LOG_I("Detected DIN\n");
                        projectExiConnector_encode_appHandExiDocument(SchemaID);
                        // Send supportedAppProtocolRes to EV
                        addV2GTPHeaderAndTransmit(global_streamEnc.data, global_streamEncPos);
                        fsmState = stateWaitForSessionSetupRequest;
                    }
                }
            }
        }

    } else if (fsmState == stateWaitForSessionSetupRequest) {

        // Check if we have received the correct message
        if (dinDocDec.V2G_Message.Body.SessionSetupReq_isUsed) {

            _LOG_I("SessionSetupReqest\n");

            //n = dinDocDec.V2G_Message.Header.SessionID.bytesLen;
            //for (i=0; i< n; i++) {
            //    _LOG_D("%02x", dinDocDec.V2G_Message.Header.SessionID.bytes[i] );
            //}
            n = dinDocDec.V2G_Message.Body.SessionSetupReq.EVCCID.bytesLen;
            if (n>6) n=6;       // out of range check
            for (i=0; i<n; i++) {
                EVCCID2[i]= dinDocDec.V2G_Message.Body.SessionSetupReq.EVCCID.bytes[i];
            }
            _LOG_I("EVCCID=%02x%02x%02x%02x%02x%02x\n", EVCCID2[0], EVCCID2[1],EVCCID2[2],EVCCID2[3],EVCCID2[4],EVCCID2[5]);

            sessionId[0] = 1;   // our SessionId is set up here, and used by _prepare_DinExiDocument
            sessionId[1] = 2;   // This SessionID will be used by the EV in future communication
            sessionId[2] = 3;
            sessionId[3] = 4;
            sessionIdLen = 4;

            // Now prepare the 'SessionSetupResponse' message to send back to the EV
            projectExiConnector_prepare_DinExiDocument();

            dinDocEnc.V2G_Message.Body.SessionSetupRes_isUsed = 1;
            init_dinSessionSetupResType(&dinDocEnc.V2G_Message.Body.SessionSetupRes);
            dinDocEnc.V2G_Message.Body.SessionSetupRes.ResponseCode = dinresponseCodeType_OK_NewSessionEstablished;

            dinDocEnc.V2G_Message.Body.SessionSetupRes.EVSEID.bytes[0] = 0;
            dinDocEnc.V2G_Message.Body.SessionSetupRes.EVSEID.bytesLen = 1;

            // Send SessionSetupResponse to EV
            global_streamEncPos = 0;
            projectExiConnector_encode_DinExiDocument();
            addV2GTPHeaderAndTransmit(global_streamEnc.data, global_streamEncPos);
            fsmState = stateWaitForServiceDiscoveryRequest;
        }

    } else if (fsmState == stateWaitForServiceDiscoveryRequest) {



        // Check if we have received the correct message
        if (dinDocDec.V2G_Message.Body.ServiceDiscoveryReq_isUsed) {

            _LOG_D("ServiceDiscoveryReqest\n");
            n = dinDocDec.V2G_Message.Header.SessionID.bytesLen;
            _LOG_D("SessionID:");
            for (i=0; i<n; i++) _LOG_D("%02x", dinDocDec.V2G_Message.Header.SessionID.bytes[i] );
            _LOG_D("\n");

            // Now prepare the 'ServiceDiscoveryResponse' message to send back to the EV
            projectExiConnector_prepare_DinExiDocument();

            dinDocEnc.V2G_Message.Body.ServiceDiscoveryRes_isUsed = 1;
            init_dinServiceDiscoveryResType(&dinDocEnc.V2G_Message.Body.ServiceDiscoveryRes);
            dinDocEnc.V2G_Message.Body.ServiceDiscoveryRes.ResponseCode = dinresponseCodeType_OK;
            /* the mandatory fields in the ISO are PaymentOptionList and ChargeService.
            But in the DIN, this is different, we find PaymentOptions, ChargeService and optional ServiceList */
            dinDocEnc.V2G_Message.Body.ServiceDiscoveryRes.PaymentOptions.PaymentOption.array[0] = dinpaymentOptionType_ExternalPayment; /* EVSE handles the payment */
            dinDocEnc.V2G_Message.Body.ServiceDiscoveryRes.PaymentOptions.PaymentOption.arrayLen = 1; /* just one single payment option in the table */
            dinDocEnc.V2G_Message.Body.ServiceDiscoveryRes.ChargeService.ServiceTag.ServiceID = 1; /* todo: not clear what this means  */
            //dinDocEnc.V2G_Message.Body.ServiceDiscoveryRes.ChargeService.ServiceTag.ServiceName
            //dinDocEnc.V2G_Message.Body.ServiceDiscoveryRes.ChargeService.ServiceTag.ServiceName_isUsed
            dinDocEnc.V2G_Message.Body.ServiceDiscoveryRes.ChargeService.ServiceTag.ServiceCategory = dinserviceCategoryType_EVCharging;
            //dinDocEnc.V2G_Message.Body.ServiceDiscoveryRes.ChargeService.ServiceTag.ServiceScope
            //dinDocEnc.V2G_Message.Body.ServiceDiscoveryRes.ChargeService.ServiceTag.ServiceScope_isUsed
            dinDocEnc.V2G_Message.Body.ServiceDiscoveryRes.ChargeService.FreeService = 0; /* what ever this means. Just from example. */
            /* dinEVSESupportedEnergyTransferType, e.g.
            dinEVSESupportedEnergyTransferType_DC_combo_core or
            dinEVSESupportedEnergyTransferType_DC_core or
            dinEVSESupportedEnergyTransferType_DC_extended
            dinEVSESupportedEnergyTransferType_AC_single_phase_core.
            DC_extended means "extended pins of an IEC 62196-3 Configuration FF connector", which is
            the normal CCS connector https://en.wikipedia.org/wiki/IEC_62196#FF) */
            dinDocEnc.V2G_Message.Body.ServiceDiscoveryRes.ChargeService.EnergyTransferType = dinEVSESupportedEnergyTransferType_DC_extended;

            // Send ServiceDiscoveryResponse to EV
            global_streamEncPos = 0;
            projectExiConnector_encode_DinExiDocument();
            addV2GTPHeaderAndTransmit(global_streamEnc.data, global_streamEncPos);
            fsmState = stateWaitForServicePaymentSelectionRequest;

        }

    } else if (fsmState == stateWaitForServicePaymentSelectionRequest) {

        routeDecoderInputData();
        projectExiConnector_decode_DinExiDocument();      // Decode EXI
        tcp_rxdataLen = 0; /* mark the input data as "consumed" */

        // Check if we have received the correct message
        if (dinDocDec.V2G_Message.Body.ServicePaymentSelectionReq_isUsed) {

            _LOG_D("ServicePaymentSelectionReqest\n");

            if (dinDocDec.V2G_Message.Body.ServicePaymentSelectionReq.SelectedPaymentOption == dinpaymentOptionType_ExternalPayment) {
                _LOG_D("OK. External Payment Selected\n");

                // Now prepare the 'ServicePaymentSelectionResponse' message to send back to the EV
                projectExiConnector_prepare_DinExiDocument();

                dinDocEnc.V2G_Message.Body.ServicePaymentSelectionRes_isUsed = 1;
                init_dinServicePaymentSelectionResType(&dinDocEnc.V2G_Message.Body.ServicePaymentSelectionRes);

                dinDocEnc.V2G_Message.Body.ServicePaymentSelectionRes.ResponseCode = dinresponseCodeType_OK;

                // Send SessionSetupResponse to EV
                global_streamEncPos = 0;
                projectExiConnector_encode_DinExiDocument();
                addV2GTPHeaderAndTransmit(global_streamEnc.data, global_streamEncPos);
                fsmState = stateWaitForContractAuthenticationRequest;
            }
        }
    } else if (fsmState == stateWaitForContractAuthenticationRequest) {

        routeDecoderInputData();
        projectExiConnector_decode_DinExiDocument();      // Decode EXI
        tcp_rxdataLen = 0; /* mark the input data as "consumed" */

        // Check if we have received the correct message
        if (dinDocDec.V2G_Message.Body.ContractAuthenticationReq_isUsed) {

            _LOG_D("ContractAuthenticationRequest\n");

            // Now prepare the 'ContractAuthenticationResponse' message to send back to the EV
            projectExiConnector_prepare_DinExiDocument();

            dinDocEnc.V2G_Message.Body.ContractAuthenticationRes_isUsed = 1;
            // Set Authorisation immediately to 'Finished'.
            dinDocEnc.V2G_Message.Body.ContractAuthenticationRes.EVSEProcessing = dinEVSEProcessingType_Finished;
            init_dinContractAuthenticationResType(&dinDocEnc.V2G_Message.Body.ContractAuthenticationRes);

            // Send SessionSetupResponse to EV
            global_streamEncPos = 0;
            projectExiConnector_encode_DinExiDocument();
            addV2GTPHeaderAndTransmit(global_streamEnc.data, global_streamEncPos);
            fsmState = stateWaitForChargeParameterDiscoveryRequest;
        }

    } else if (fsmState == stateWaitForChargeParameterDiscoveryRequest) {

        routeDecoderInputData();
        projectExiConnector_decode_DinExiDocument();      // Decode EXI
        tcp_rxdataLen = 0; /* mark the input data as "consumed" */

        // Check if we have received the correct message
        if (dinDocDec.V2G_Message.Body.ChargeParameterDiscoveryReq_isUsed) {

            _LOG_D("ChargeParameterDiscoveryRequest\n");

            // Read the SOC from the EVRESSOC data
            ComputedSoC = dinDocDec.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.DC_EVStatus.EVRESSSOC;

            _LOG_D("Current SoC %d%%\n", ComputedSoC);
            String EVCCIDstr = "";
            for (uint8_t i = 0; i < 6; i++) {
                if (EVCCID2[i] < 0x10) EVCCIDstr += "0";  // pad with zero for values less than 0x10
                EVCCIDstr += String(EVCCID2[i], HEX);
            }
            _LOG_D("EVCCID=%s.\n", EVCCIDstr.c_str());
            strncpy(EVCCID, EVCCIDstr.c_str(), sizeof(EVCCID));
            Serial1.printf("@EVCCID:%s\n", EVCCID);  //send to CH32

            const char UnitStr[][4] = {"h" , "m" , "s" , "A" , "Ah" , "V" , "VA" , "W" , "W_s" , "Wh"};
            dinPhysicalValueType Temp;

            //try to read this required field so we can test if we have communication ok with the EV
            Temp = dinDocDec.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.EVMaximumCurrentLimit;
            _LOG_A("Modem: EVMaximumCurrentLimit=%f %s.\n", Temp.Value * pow(10, Temp.Multiplier), Temp.Unit_isUsed ? UnitStr[Temp.Unit] : ""); //not using pow_10 because multiplier can be negative!

            Temp = dinDocDec.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.EVMaximumVoltageLimit;
            _LOG_A("Modem: EVMaximumVoltageLimit=%f %s.\n", Temp.Value * pow(10, Temp.Multiplier), Temp.Unit_isUsed ? UnitStr[Temp.Unit] : ""); //not using pow_10 because multiplier can be negative!
            Temp = dinDocDec.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.EVMaximumPowerLimit;
            _LOG_A("Modem: EVMaximumPowerLimit=%f %s.\n", Temp.Value * pow(10, Temp.Multiplier), Temp.Unit_isUsed ? UnitStr[Temp.Unit] : ""); //not using pow_10 because multiplier can be negative!

            if(dinDocDec.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.BulkSOC_isUsed) {
                uint8_t BulkSOC = dinDocDec.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.BulkSOC;
                _LOG_A("Modem: BulkSOC=%d.\n", BulkSOC); \
            }

            if(dinDocDec.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.FullSOC_isUsed) {
                FullSoC = dinDocDec.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.FullSOC;
                _LOG_A("Modem: set FullSoC=%d.\n", FullSoC);
            }

            uint32_t deptime = dinDocDec.V2G_Message.Body.ChargeParameterDiscoveryReq.AC_EVChargeParameter.DepartureTime;
            _LOG_A("Modem: Departure Time=%u.\n", deptime);

            if(dinDocDec.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.EVEnergyCapacity_isUsed) {
                Temp = dinDocDec.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.EVEnergyCapacity;
                EnergyCapacity = Temp.Value  * pow(10, Temp.Multiplier);
                _LOG_A("Modem: set EVEnergyCapacity=%d %s.\n", Temp.Value, Temp.Unit_isUsed ? UnitStr[Temp.Unit] : "");
            }

            if(dinDocDec.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.EVEnergyRequest_isUsed) {
                Temp = dinDocDec.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.EVEnergyRequest;
                _LOG_A("Modem: set EVEnergyRequest=%d %s, Multiplier=%d.\n", Temp.Value, Temp.Unit_isUsed ? UnitStr[Temp.Unit] : "", Temp.Multiplier);
                EnergyRequest = Temp.Value;
            }

            if (ComputedSoC >= 0 && ComputedSoC <= 100) { // valid
                // Skip waiting, charge since we have what we've got
                if (State == STATE_MODEM_REQUEST || State == STATE_MODEM_WAIT || State == STATE_MODEM_DONE){
                    _LOG_A("Received SoC via Modem. Shortcut to State Modem Done\n");
                    setState(STATE_MODEM_DONE); // Go to State B, which means in this case setting PWM
                }
                if (InitialSoC < 0) //not initialized yet
                    InitialSoC = ComputedSoC;
            }

            RecomputeSoC();

            // Now prepare the 'ChargeParameterDiscoveryResponse' message to send back to the EV
            projectExiConnector_prepare_DinExiDocument();

            dinDocEnc.V2G_Message.Body.ChargeParameterDiscoveryRes_isUsed = 1;
            init_dinChargeParameterDiscoveryResType(&dinDocEnc.V2G_Message.Body.ChargeParameterDiscoveryRes);

            // Send SessionSetupResponse to EV
            global_streamEncPos = 0;
            projectExiConnector_encode_DinExiDocument();
            addV2GTPHeaderAndTransmit(global_streamEnc.data, global_streamEncPos);
            fsmState = stateWaitForCableCheckRequest;

        }

    }

}


void tcp_packRequestIntoEthernet(void) {
    //# packs the IP packet into an ethernet packet
    uint16_t length;

    length = TcpIpRequestLen + 6 + 6 + 2; // # Ethernet header needs 14 bytes:
                                                    // #  6 bytes destination MAC
                                                    // #  6 bytes source MAC
                                                    // #  2 bytes EtherType
    //# fill the destination MAC with the MAC of the charger
    setMacAt(pevMac, 0);
    setMacAt(myMac, 6); // bytes 6 to 11 are the source MAC
    txbuffer[12] = 0x86; // # 86dd is IPv6
    txbuffer[13] = 0xdd;
    memcpy(txbuffer+14, TcpIpRequest, length);

    //_LOG_D("[TX] ");
    //for(int x=0; x<length; x++) _LOG_D("%02x",txbuffer[x]);
    //_LOG_D("\n\n");

    qcaspi_write_burst(txbuffer, length);
}

void tcp_packRequestIntoIp(void) {
    // # embeds the TCP into the lower-layer-protocol: IP, Ethernet
    uint8_t i;
    uint16_t plen;
    TcpIpRequestLen = TcpTransmitPacketLen + 8 + 16 + 16; // # IP6 header needs 40 bytes:
                                                //  #   4 bytes traffic class, flow
                                                //  #   2 bytes destination port
                                                //  #   2 bytes length (incl checksum)
                                                //  #   2 bytes checksum
    TcpIpRequest[0] = 0x60; // traffic class, flow
    TcpIpRequest[1] = 0x00;
    TcpIpRequest[2] = 0x00;
    TcpIpRequest[3] = 0x00;
    plen = TcpTransmitPacketLen; // length of the payload. Without headers.
    TcpIpRequest[4] = plen >> 8;
    TcpIpRequest[5] = plen & 0xFF;
    TcpIpRequest[6] = NEXT_TCP; // next level protocol, 0x06 = TCP in this case
    TcpIpRequest[7] = 0x40; // hop limit
    //
    // We are the EVSE. So the PevIp is our own link-local IP address.
    for (i=0; i<16; i++) {
        TcpIpRequest[8+i] = SeccIp[i]; // source IP address
    }
    for (i=0; i<16; i++) {
        TcpIpRequest[24+i] = EvccIp[i]; // destination IP address
    }
    for (i=0; i<TcpTransmitPacketLen; i++) {
        TcpIpRequest[40+i] = TcpTransmitPacket[i];
    }
    //showAsHex(TcpIpRequest, TcpIpRequestLen, "TcpIpRequest");
    tcp_packRequestIntoEthernet();
}



void tcp_prepareTcpHeader(uint8_t tcpFlag) {
    uint16_t checksum;

    // # TCP header needs at least 24 bytes:
    // 2 bytes source port
    // 2 bytes destination port
    // 4 bytes sequence number
    // 4 bytes ack number
    // 4 bytes DO/RES/Flags/Windowsize
    // 2 bytes checksum
    // 2 bytes urgentPointer
    // n*4 bytes options/fill (empty for the ACK frame and payload frames)
    TcpTransmitPacket[0] = (uint8_t)(seccPort >> 8); /* source port */
    TcpTransmitPacket[1] = (uint8_t)(seccPort);
    TcpTransmitPacket[2] = (uint8_t)(evccTcpPort >> 8); /* destination port */
    TcpTransmitPacket[3] = (uint8_t)(evccTcpPort);

    TcpTransmitPacket[4] = (uint8_t)(TcpSeqNr>>24); /* sequence number */
    TcpTransmitPacket[5] = (uint8_t)(TcpSeqNr>>16);
    TcpTransmitPacket[6] = (uint8_t)(TcpSeqNr>>8);
    TcpTransmitPacket[7] = (uint8_t)(TcpSeqNr);

    TcpTransmitPacket[8] = (uint8_t)(TcpAckNr>>24); /* ack number */
    TcpTransmitPacket[9] = (uint8_t)(TcpAckNr>>16);
    TcpTransmitPacket[10] = (uint8_t)(TcpAckNr>>8);
    TcpTransmitPacket[11] = (uint8_t)(TcpAckNr);
    TcpTransmitPacketLen = tcpHeaderLen + tcpPayloadLen;
    TcpTransmitPacket[12] = (tcpHeaderLen/4) << 4; /* 70 High-nibble: DataOffset in 4-byte-steps. Low-nibble: Reserved=0. */

    TcpTransmitPacket[13] = tcpFlag;
    TcpTransmitPacket[14] = (uint8_t)(TCP_RECEIVE_WINDOW>>8);
    TcpTransmitPacket[15] = (uint8_t)(TCP_RECEIVE_WINDOW);

    // checksum will be calculated afterwards
    TcpTransmitPacket[16] = 0;
    TcpTransmitPacket[17] = 0;

    TcpTransmitPacket[18] = 0; /* 16 bit urgentPointer. Always zero in our case. */
    TcpTransmitPacket[19] = 0;

    checksum = calculateUdpAndTcpChecksumForIPv6(TcpTransmitPacket, TcpTransmitPacketLen, SeccIp, EvccIp, NEXT_TCP);
    TcpTransmitPacket[16] = (uint8_t)(checksum >> 8);
    TcpTransmitPacket[17] = (uint8_t)(checksum);

    //_LOG_D("Source:%u Dest:%u Seqnr:%08x Acknr:%08x\n", seccPort, evccTcpPort, TcpSeqNr, TcpAckNr);
}


void tcp_sendFirstAck(void) {
   // _LOG_D("[TCP] sending first ACK\n");
    tcpHeaderLen = 20;
    tcpPayloadLen = 0;
    tcp_prepareTcpHeader(TCP_FLAG_ACK | TCP_FLAG_SYN);
    tcp_packRequestIntoIp();
}

void tcp_sendAck(void) {
//   _LOG_D("[TCP] sending ACK\n");
   tcpHeaderLen = 20; /* 20 bytes normal header, no options */
   tcpPayloadLen = 0;
   tcp_prepareTcpHeader(TCP_FLAG_ACK);
   tcp_packRequestIntoIp();
}


void evaluateTcpPacket(void) {
    uint8_t flags;
    uint32_t remoteSeqNr;
    uint32_t remoteAckNr;
    uint16_t SourcePort, DestinationPort, pLen, hdrLen, tmpPayloadLen;

    /* todo: check the IP addresses, checksum etc */
    //nTcpPacketsReceived++;
    pLen =  rxbuffer[18]*256 + rxbuffer[19]; /* length of the IP payload */
    hdrLen = (rxbuffer[66]>>4) * 4; /* header length in byte */
    if (pLen >= hdrLen) {
        tmpPayloadLen = pLen - hdrLen;
    } else {
        tmpPayloadLen = 0; /* no TCP payload data */
    }
    //_LOG_D("pLen=%u, hdrLen=%u, Payload=%u\n", pLen, hdrLen, tmpPayloadLen);
    SourcePort = rxbuffer[54]*256 +  rxbuffer[55];
    DestinationPort = rxbuffer[56]*256 +  rxbuffer[57];
    if (DestinationPort != 15118) {
        _LOG_W("[TCP] wrong port.\n");
        return; /* wrong port */
    }
    //  tcpActivityTimer=TCP_ACTIVITY_TIMER_START;
    remoteSeqNr =
            (((uint32_t)rxbuffer[58])<<24) +
            (((uint32_t)rxbuffer[59])<<16) +
            (((uint32_t)rxbuffer[60])<<8) +
            (((uint32_t)rxbuffer[61]));
    remoteAckNr =
            (((uint32_t)rxbuffer[62])<<24) +
            (((uint32_t)rxbuffer[63])<<16) +
            (((uint32_t)rxbuffer[64])<<8) +
            (((uint32_t)rxbuffer[65]));
    //_LOG_D("Source:%u Dest:%u Seqnr:%08x Acknr:%08x flags:%02x\n", SourcePort, DestinationPort, remoteSeqNr, remoteAckNr, flags);
    flags = rxbuffer[67];
    if (flags == TCP_FLAG_SYN) { /* This is the connection setup reqest from the EV. */
        if (tcpState == TCP_STATE_CLOSED) {
            evccTcpPort = SourcePort; // update the evccTcpPort to the new TCP port
            TcpSeqNr = 0x01020304; // We start with a 'random' sequence nr
            TcpAckNr = remoteSeqNr+1; // The ACK number of our next transmit packet is one more than the received seq number.
            tcpState = TCP_STATE_SYN_ACK;
            tcp_sendFirstAck();
        }
        return;
    }
    if (flags == TCP_FLAG_ACK && tcpState == TCP_STATE_SYN_ACK) {
        if (remoteAckNr == (TcpSeqNr + 1) ) {
            _LOG_I("-------------- TCP connection established ---------------\n\n");
            tcpState = TCP_STATE_ESTABLISHED;
        }
        return;
    }
    /* It is no connection setup. We can have the following situations here: */
    if (tcpState != TCP_STATE_ESTABLISHED) {
        /* received something while the connection is closed. Just ignore it. */
        _LOG_D("[TCP] ignore, not connected.\n");
        return;
    }

    // It can be an ACK, or a data package, or a combination of both. We treat the ACK and the data independent from each other,
    // to treat each combination.
   if ((tmpPayloadLen>0) && (tmpPayloadLen< TCP_RX_DATA_LEN)) {
        /* This is a data transfer packet. */
        // flag bit PSH should also be set.
        tcp_rxdataLen = tmpPayloadLen;
        TcpAckNr = remoteSeqNr + tcp_rxdataLen; // The ACK number of our next transmit packet is tcp_rxdataLen more than the received seq number.
        TcpSeqNr = remoteAckNr;                 // tcp_rxdatalen will be cleared later.
        /* rxbuffer[74] is the first payload byte. */
        memcpy(tcp_rxdata, rxbuffer+74, tcp_rxdataLen);  /* provide the received data to the application */
        //     connMgr_TcpOk();
        tcp_sendAck();  // Send Ack, then process data
        decodeV2GTP();
        return;
    }

   if (flags & TCP_FLAG_ACK) {
    //   _LOG_D("This was an ACK\n\n");
       TcpSeqNr = remoteAckNr; /* The sequence number of our next transmit packet is given by the received ACK number. */
   }
}
#endif

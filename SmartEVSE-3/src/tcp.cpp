#if SMARTEVSE_VERSION >= 40
#include <Arduino.h>
#include "qca.h"
#include "ipv6.h"

// External project headers
extern "C" {
#include "exi2/exi_basetypes.h"
#include "src/exi2/appHand_Decoder.h"
#include "src/exi2/appHand_Encoder.h"
#include "exi2/din_msgDefDecoder.h"
#include "exi2/din_msgDefEncoder.h"
#include "exi2/iso2_msgDefDecoder.h"
#include "exi2/iso2_msgDefEncoder.h"

#include "exi2/iso20_AC_Datatypes.h"
#include "exi2/iso20_AC_Decoder.h"
#include "exi2/iso20_AC_Encoder.h"
}
#include "debug.h"
#include "esp32.h"

/* Todo: implement a retry strategy, to cover the situation that single packets are lost on the way. */

#define NEXT_TCP 0x06  // the next protocol is TCP

#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10

#define TCP_HEADER_LEN 20 // 20 bytes normal header, no options
#define ETHERNET_HEADER_LEN 14                      // # Ethernet header needs 14 bytes:
                                                    // #  6 bytes destination MAC
                                                    // #  6 bytes source MAC
                                                    // #  2 bytes EtherType

#define TCP_ACTIVITY_TIMER_START (5*33) /* 5 seconds */
uint16_t tcpActivityTimer;

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

#define EXI_TRANSMIT_BUFFER_SIZE 512
uint8_t V2G_transmit_buffer[EXI_TRANSMIT_BUFFER_SIZE];

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
enum {DIN, ISO2, ISO20} Protocol;

extern char EVCCID[32];
extern int8_t InitialSoC, ComputedSoC, FullSoC;
extern void setState(uint8_t NewState);
extern void RecomputeSoC(void);
extern int32_t EnergyCapacity, EnergyRequest;
extern uint16_t MaxCurrent;


void tcp_prepareTcpHeader(uint8_t tcpFlag, uint8_t *tcpPayload, uint16_t tcpPayloadLen) {
    uint16_t checksum;
    uint16_t TcpTransmitPacketLen = TCP_HEADER_LEN + tcpPayloadLen;
    uint8_t *TcpIpRequest = txbuffer + ETHERNET_HEADER_LEN;
    uint8_t *TcpTransmitPacket = TcpIpRequest + 40;
    memcpy(&TcpTransmitPacket[TCP_HEADER_LEN], tcpPayload, tcpPayloadLen);

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
    TcpTransmitPacket[12] = (TCP_HEADER_LEN/4) << 4; /* 70 High-nibble: DataOffset in 4-byte-steps. Low-nibble: Reserved=0. */

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

    //tcp_packRequestIntoIp():
    // # embeds the TCP into the lower-layer-protocol: IP, Ethernet
    uint16_t TcpIpRequestLen = TcpTransmitPacketLen + 8 + 16 + 16; // # IP6 header needs 40 bytes:
                                                //  #   4 bytes traffic class, flow
                                                //  #   2 bytes destination port
                                                //  #   2 bytes length (incl checksum)
                                                //  #   2 bytes checksum
    //# fill the destination MAC with the MAC of the charger
    setMacAt(pevMac, 0);
    setMacAt(myMac, 6); // bytes 6 to 11 are the source MAC
    txbuffer[12] = 0x86; // # 86dd is IPv6
    txbuffer[13] = 0xdd;

    TcpIpRequest[0] = 0x60; // traffic class, flow
    TcpIpRequest[1] = 0x00;
    TcpIpRequest[2] = 0x00;
    TcpIpRequest[3] = 0x00;
    TcpIpRequest[4] = TcpTransmitPacketLen >> 8; // length of the payload. Without headers.
    TcpIpRequest[5] = TcpTransmitPacketLen & 0xFF;
    TcpIpRequest[6] = NEXT_TCP; // next level protocol, 0x06 = TCP in this case
    TcpIpRequest[7] = 0x40; // hop limit
    //
    // We are the EVSE. So the PevIp is our own link-local IP address.
    memcpy(TcpIpRequest+8, SeccIp, 16);         // source IP address
    memcpy(TcpIpRequest+24, EvccIp, 16);        // destination IP address

    //# packs the IP packet into an ethernet packet
    uint16_t length = TcpIpRequestLen + ETHERNET_HEADER_LEN;
                                                    // #  6 bytes destination MAC
                                                    // #  6 bytes source MAC
                                                    // #  2 bytes EtherType

    _LOG_D("[TX:%u]", length);
    for(int x=0; x<length; x++) _LOG_D_NO_FUNC("%02x",txbuffer[x]);
    _LOG_D_NO_FUNC("\n\n");

    qcaspi_write_burst(txbuffer, length);
}


void addV2GTPHeaderAndTransmit(const uint8_t *exiBuffer, uint16_t exiBufferLen) {
    // takes the bytearray with exidata, and adds a header to it, according to the Vehicle-to-Grid-Transport-Protocol
    // V2GTP header has 8 bytes
    // 1 byte protocol version
    // 1 byte protocol version inverted
    // 2 bytes payload type
    // 4 byte payload length
    uint8_t tcpPayload[8 + exiBufferLen];
    tcpPayload[0] = 0x01; // version
    tcpPayload[1] = 0xfe; // version inverted
    tcpPayload[2] = 0x80; // payload type. 0x8001 means "EXI data"
    tcpPayload[3] = 0x01; //
    tcpPayload[4] = (uint8_t)(exiBufferLen >> 24); // length 4 byte.
    tcpPayload[5] = (uint8_t)(exiBufferLen >> 16);
    tcpPayload[6] = (uint8_t)(exiBufferLen >> 8);
    tcpPayload[7] = (uint8_t)exiBufferLen;
    memcpy(tcpPayload+8, exiBuffer, exiBufferLen);
    //_LOG_V("EXI transmit[%u]:", exiBufferLen);
    //for (uint16_t i=0; i< exiBufferLen; i++)
    //    _LOG_V_NO_FUNC(" %02X",exiBuffer[i]);
    //_LOG_V_NO_FUNC("\n.");

    //tcp_transmit:
    if (tcpState == TCP_STATE_ESTABLISHED) {
        tcp_prepareTcpHeader(TCP_FLAG_PSH + TCP_FLAG_ACK, tcpPayload, 8 + exiBufferLen); // data packets are always sent with flags PUSH and ACK; 8 byte V2GTP header, plus the EXI data 
    }
}


void EncodeAndTransmit(struct appHand_exiDocument* exiDoc) {
    uint8_t g_errn;
    exi_bitstream_t tx_stream; //TODO perhaps reuse stream?
    exi_bitstream_init(&tx_stream, V2G_transmit_buffer, sizeof(V2G_transmit_buffer), 0, NULL);
    g_errn = encode_appHand_exiDocument(&tx_stream, exiDoc);
    // Send supportedAppProtocolRes to EV
    if (!g_errn)
        //data_size=256, bit_count=4, byte_pos=3, flag_byte=0 for appHand
        addV2GTPHeaderAndTransmit(tx_stream.data, tx_stream.byte_pos + 1); //not sure if byte_pos is the right variable
}


void EncodeAndTransmit(struct din_exiDocument* dinDoc) {
    uint8_t g_errn;
    exi_bitstream_t tx_stream; //TODO perhaps reuse stream?
    exi_bitstream_init(&tx_stream, V2G_transmit_buffer, sizeof(V2G_transmit_buffer), 0, NULL);
    g_errn = encode_din_exiDocument(&tx_stream, dinDoc);
    // Send supportedAppProtocolRes to EV
    if (!g_errn)
        //data_size=256, bit_count=4, byte_pos=3, flag_byte=0 for appHand
        addV2GTPHeaderAndTransmit(tx_stream.data, tx_stream.byte_pos + 1); //not sure if byte_pos is the right variable
}


void EncodeAndTransmit(struct iso2_exiDocument* dinDoc) {
    uint8_t g_errn;
    exi_bitstream_t tx_stream; //TODO perhaps reuse stream?
    exi_bitstream_init(&tx_stream, V2G_transmit_buffer, sizeof(V2G_transmit_buffer), 0, NULL);
    g_errn = encode_iso2_exiDocument(&tx_stream, dinDoc);
    // Send supportedAppProtocolRes to EV
    if (!g_errn) {
        _LOG_A("DINGO: transmitting iso2 exiDocument.\n");
        //data_size=256, bit_count=4, byte_pos=3, flag_byte=0 for appHand
        addV2GTPHeaderAndTransmit(tx_stream.data, tx_stream.byte_pos + 1); //not sure if byte_pos is the right variable
    } else
        _LOG_A("ERROR no %u: Could not encode iso2 document, not transmitting response!\n", g_errn);
}


void decodeV2GTP(void) {
    exi_bitstream_t stream;
    exi_bitstream_init(&stream, &tcp_rxdata[V2GTP_HEADER_SIZE], tcp_rxdataLen - V2GTP_HEADER_SIZE, 0, NULL);
    uint8_t g_errn;
    tcp_rxdataLen = 0; /* mark the input data as "consumed" */

    if (fsmState == stateWaitForSupportedApplicationProtocolRequest) {
        struct appHand_exiDocument exiDoc;
        g_errn = decode_appHand_exiDocument(&stream, &exiDoc);

        // Check if we have received the correct message
        if (g_errn == 0 && exiDoc.supportedAppProtocolReq_isUsed) {

            _LOG_I("SupportedApplicationProtocolRequest\n");
            _LOG_I("The car supports %u schemas.\n", exiDoc.supportedAppProtocolReq.AppProtocol.arrayLen);
            for (uint16_t i=0; i< exiDoc.supportedAppProtocolReq.AppProtocol.arrayLen; i++) {
                struct appHand_AppProtocolType* app_proto = &exiDoc.supportedAppProtocolReq.AppProtocol.array[i];
                char* proto_ns = strndup(static_cast<const char*>(app_proto->ProtocolNamespace.characters),
                                 app_proto->ProtocolNamespace.charactersLen);
                if (!proto_ns) {
                    _LOG_A("ERROR: out-of-memory condition.\n");
                    return;
                }
                _LOG_A("handshake_req: Namespace: %s, Version: %" PRIu32 ".%" PRIu32 ", SchemaID: %" PRIu8 ", Priority: %" PRIu8 ".\n", proto_ns, app_proto->VersionNumberMajor, app_proto->VersionNumberMinor, app_proto->SchemaID, app_proto->Priority);

#define ISO_15118_2013_MSG_DEF "urn:iso:15118:2:2013:MsgDef"
#define ISO_15118_2013_MAJOR   2
#define ISO_15118_2010_MSG_DEF "urn:iso:15118:2:2010:MsgDef"
#define ISO_15118_2010_MAJOR   1
#define DIN_70121_MSG_DEF "urn:din:70121:2012:MsgDef"
#define DIN_70121_MAJOR   2

                if (!strcmp(proto_ns, DIN_70121_MSG_DEF) && app_proto->VersionNumberMajor == DIN_70121_MAJOR) {
                    _LOG_A("Selecting DIN70121.\n");
                    init_appHand_exiDocument(&exiDoc);
                    exiDoc.supportedAppProtocolRes_isUsed = 1;
                    //exiDoc.supportedAppProtocolRes.ResponseCode = appHand_responseCodeType_Failed_NoNegotiation; // [V2G2-172]
                    exiDoc.supportedAppProtocolRes.ResponseCode = appHand_responseCodeType_OK_SuccessfulNegotiation;
                    exiDoc.supportedAppProtocolRes.SchemaID_isUsed = (unsigned int)1;
                    exiDoc.supportedAppProtocolRes.SchemaID = app_proto->SchemaID;
                    EncodeAndTransmit(&exiDoc);
                    Protocol = DIN;
                    fsmState = stateWaitForSessionSetupRequest;
                    //conn->ctx->selected_protocol = V2G_PROTO_DIN70121;
                } else if (!strcmp(proto_ns, ISO_15118_2013_MSG_DEF)  && app_proto->VersionNumberMajor == ISO_15118_2013_MAJOR) {
                    _LOG_I("Detected ISO15118:2\n");
                    init_appHand_exiDocument(&exiDoc);
                    exiDoc.supportedAppProtocolRes_isUsed = 1;
                    //exiDoc.supportedAppProtocolRes.ResponseCode = appHand_responseCodeType_Failed_NoNegotiation; // [V2G2-172]
                    exiDoc.supportedAppProtocolRes.ResponseCode = appHand_responseCodeType_OK_SuccessfulNegotiation;
                    exiDoc.supportedAppProtocolRes.SchemaID_isUsed = (unsigned int)1;
                    exiDoc.supportedAppProtocolRes.SchemaID = app_proto->SchemaID;
                    EncodeAndTransmit(&exiDoc);
                    Protocol = ISO2;
                    fsmState = stateWaitForSessionSetupRequest;
                }
            } //for
        }
        return;
    }
    if (Protocol == DIN) {
        struct din_exiDocument dinDoc;
        memset(&dinDoc, 0, sizeof(struct din_exiDocument));
        decode_din_exiDocument(&stream, &dinDoc);
        if (fsmState == stateWaitForSessionSetupRequest) {
            // Check if we have received the correct message
            if (dinDoc.V2G_Message.Body.SessionSetupReq_isUsed) {
                _LOG_I("SessionSetupRequest\n");

                //n = dinDoc.V2G_Message.Header.SessionID.bytesLen;
                //for (i=0; i< n; i++) {
                //    _LOG_D("%02x", dinDoc.V2G_Message.Header.SessionID.bytes[i] );
                //}
                uint8_t n = dinDoc.V2G_Message.Body.SessionSetupReq.EVCCID.bytesLen;
                if (n>6) n=6;       // out of range check
                for (uint8_t i=0; i<n; i++) {
                    EVCCID2[i]= dinDoc.V2G_Message.Body.SessionSetupReq.EVCCID.bytes[i];
                }
                _LOG_I("EVCCID=%02x%02x%02x%02x%02x%02x\n", EVCCID2[0], EVCCID2[1],EVCCID2[2],EVCCID2[3],EVCCID2[4],EVCCID2[5]);
                uint8_t sessionId[8];
                sessionId[0] = 1;   // our SessionId is set up here, and used by _prepare_DinExiDocument
                sessionId[1] = 2;   // This SessionID will be used by the EV in future communication
                sessionId[2] = 3;
                sessionId[3] = 4;
                uint8_t sessionIdLen = 4;

                // Now prepare the 'SessionSetupResponse' message to send back to the EV
                init_din_BodyType(&dinDoc.V2G_Message.Body);
                init_din_SessionSetupReqType(&dinDoc.V2G_Message.Body.SessionSetupReq);

                dinDoc.V2G_Message.Body.SessionSetupRes_isUsed = 1;
                //init_dinSessionSetupResType(&dinDocEnc.V2G_Message.Body.SessionSetupRes);
                dinDoc.V2G_Message.Body.SessionSetupRes.ResponseCode = din_responseCodeType_OK_NewSessionEstablished;
                dinDoc.V2G_Message.Body.SessionSetupRes.EVSEID.bytes[0] = 0;
                dinDoc.V2G_Message.Body.SessionSetupRes.EVSEID.bytesLen = 1;

                // Send SessionSetupResponse to EV
                EncodeAndTransmit(&dinDoc);
                fsmState = stateWaitForServiceDiscoveryRequest;
            }
            return;
        }
        if (fsmState == stateWaitForServiceDiscoveryRequest) {
            // Check if we have received the correct message
            if (dinDoc.V2G_Message.Body.ServiceDiscoveryReq_isUsed) {

                _LOG_I("ServiceDiscoveryRequest\n");
                uint8_t n = dinDoc.V2G_Message.Header.SessionID.bytesLen;
                _LOG_I("SessionID:");
                for (uint8_t i=0; i<n; i++) _LOG_I_NO_FUNC("%02x", dinDoc.V2G_Message.Header.SessionID.bytes[i] );
                _LOG_I_NO_FUNC("\n");

                // Now prepare the 'ServiceDiscoveryResponse' message to send back to the EV
                init_din_BodyType(&dinDoc.V2G_Message.Body);
                init_din_ServiceDiscoveryReqType(&dinDoc.V2G_Message.Body.ServiceDiscoveryReq);
                dinDoc.V2G_Message.Body.ServiceDiscoveryRes_isUsed = 1;

                // set the service category
                dinDoc.V2G_Message.Body.ServiceDiscoveryReq.ServiceScope_isUsed = false;
                dinDoc.V2G_Message.Body.ServiceDiscoveryReq.ServiceCategory_isUsed = true;
                dinDoc.V2G_Message.Body.ServiceDiscoveryReq.ServiceCategory = din_serviceCategoryType_EVCharging;

                //init_dinServiceDiscoveryResType(&dinDocEnc.V2G_Message.Body.ServiceDiscoveryRes);
                dinDoc.V2G_Message.Body.ServiceDiscoveryRes.ResponseCode = din_responseCodeType_OK;
                //dinDoc.V2G_Message.Body.ServiceDiscoveryRes.ResponseCode = dinresponseCodeType_OK;
                // the mandatory fields in the ISO are PaymentOptionList and ChargeService.
                //But in the DIN, this is different, we find PaymentOptions, ChargeService and optional ServiceList
                dinDoc.V2G_Message.Body.ServiceDiscoveryRes.PaymentOptions.PaymentOption.array[0] = din_paymentOptionType_ExternalPayment; // EVSE handles the payment
                dinDoc.V2G_Message.Body.ServiceDiscoveryRes.PaymentOptions.PaymentOption.arrayLen = 1; // just one single payment option in the table
                dinDoc.V2G_Message.Body.ServiceDiscoveryRes.ChargeService.ServiceTag.ServiceID = 1; // todo: not clear what this means
                //dinDocEnc.V2G_Message.Body.ServiceDiscoveryRes.ChargeService.ServiceTag.ServiceName
                //dinDocEnc.V2G_Message.Body.ServiceDiscoveryRes.ChargeService.ServiceTag.ServiceName_isUsed
                dinDoc.V2G_Message.Body.ServiceDiscoveryRes.ChargeService.ServiceTag.ServiceCategory = din_serviceCategoryType_EVCharging;
                //dinDocEnc.V2G_Message.Body.ServiceDiscoveryRes.ChargeService.ServiceTag.ServiceScope
                //dinDocEnc.V2G_Message.Body.ServiceDiscoveryRes.ChargeService.ServiceTag.ServiceScope_isUsed
                dinDoc.V2G_Message.Body.ServiceDiscoveryRes.ChargeService.FreeService = 0; // what ever this means. Just from example.
    /*          dinEVSESupportedEnergyTransferType
                dinEVSESupportedEnergyTransferType_AC_single_phase_core = 0,
                dinEVSESupportedEnergyTransferType_AC_three_phase_core = 1,
                dinEVSESupportedEnergyTransferType_DC_core = 2,
                dinEVSESupportedEnergyTransferType_DC_extended = 3,
                dinEVSESupportedEnergyTransferType_DC_combo_core = 4,
                dinEVSESupportedEnergyTransferType_DC_dual = 5,
                dinEVSESupportedEnergyTransferType_AC_core1p_DC_extended = 6,
                dinEVSESupportedEnergyTransferType_AC_single_DC_core = 7,
                dinEVSESupportedEnergyTransferType_AC_single_phase_three_phase_core_DC_extended = 8,
                dinEVSESupportedEnergyTransferType_AC_core3p_DC_extended = 9
    */
                // DC_extended means "extended pins of an IEC 62196-3 Configuration FF connector", which is
                // the normal CCS connector https://en.wikipedia.org/wiki/IEC_62196#FF)
                dinDoc.V2G_Message.Body.ServiceDiscoveryRes.ChargeService.EnergyTransferType = din_EVSESupportedEnergyTransferType_DC_extended;
                dinDoc.V2G_Message.Body.ServiceDiscoveryRes.ChargeService.EnergyTransferType = din_EVSESupportedEnergyTransferType_AC_single_phase_three_phase_core_DC_extended;

                // Send ServiceDiscoveryResponse to EV
                EncodeAndTransmit(&dinDoc);
                fsmState = stateWaitForServicePaymentSelectionRequest;
            }
            return;
        }

        if (fsmState == stateWaitForServicePaymentSelectionRequest) {
            // Check if we have received the correct message
            if (dinDoc.V2G_Message.Body.ServicePaymentSelectionReq_isUsed) {
                _LOG_I("ServicePaymentSelectionRequest\n");
                if (dinDoc.V2G_Message.Body.ServicePaymentSelectionReq.SelectedPaymentOption == din_paymentOptionType_ExternalPayment) {
                    _LOG_I("OK. External Payment Selected\n");

                    // Now prepare the 'ServicePaymentSelectionResponse' message to send back to the EV
                    init_din_BodyType(&dinDoc.V2G_Message.Body);
                    init_din_ServicePaymentSelectionResType(&dinDoc.V2G_Message.Body.ServicePaymentSelectionRes);

                    dinDoc.V2G_Message.Body.ServicePaymentSelectionRes_isUsed = 1;
                    dinDoc.V2G_Message.Body.ServicePaymentSelectionRes.ResponseCode = din_responseCodeType_OK;

                    // Send SessionSetupResponse to EV
                    EncodeAndTransmit(&dinDoc);
                    fsmState = stateWaitForContractAuthenticationRequest;
                }
            }
            return;
        }

        if (fsmState == stateWaitForContractAuthenticationRequest) {
            // Check if we have received the correct message
            if (dinDoc.V2G_Message.Body.ContractAuthenticationReq_isUsed) {
                _LOG_I("ContractAuthenticationRequest\n");

                init_din_BodyType(&dinDoc.V2G_Message.Body);
                init_din_ContractAuthenticationResType(&dinDoc.V2G_Message.Body.ContractAuthenticationRes);

                dinDoc.V2G_Message.Body.ContractAuthenticationRes_isUsed = 1;
                // Set Authorisation immediately to 'Finished'.
                dinDoc.V2G_Message.Body.ContractAuthenticationRes.EVSEProcessing = din_EVSEProcessingType_Finished;

                // Send SessionSetupResponse to EV
                EncodeAndTransmit(&dinDoc);
                fsmState = stateWaitForChargeParameterDiscoveryRequest;
            }
            return;
        }

        if (fsmState == stateWaitForChargeParameterDiscoveryRequest) {
            // Check if we have received the correct message
            if (dinDoc.V2G_Message.Body.ChargeParameterDiscoveryReq_isUsed) {
                _LOG_I("ChargeParameterDiscoveryRequest\n");

                // Read the SOC from the EVRESSOC data
                ComputedSoC = dinDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.DC_EVStatus.EVRESSSOC;

                _LOG_I("Current SoC %d%%\n", ComputedSoC);
                String EVCCIDstr = "";
                for (uint8_t i = 0; i < 6; i++) {
                    if (EVCCID2[i] < 0x10) EVCCIDstr += "0";  // pad with zero for values less than 0x10
                    EVCCIDstr += String(EVCCID2[i], HEX);
                }
                _LOG_I("EVCCID=%s.\n", EVCCIDstr.c_str());
                strncpy(EVCCID, EVCCIDstr.c_str(), sizeof(EVCCID));
                Serial1.printf("@EVCCID:%s\n", EVCCID);  //send to CH32

                const char UnitStr[][4] = {"h" , "m" , "s" , "A" , "Ah" , "V" , "VA" , "W" , "W_s" , "Wh"};
                din_PhysicalValueType Temp;

                //try to read this required field so we can test if we have communication ok with the EV
                Temp = dinDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.EVMaximumCurrentLimit;
                _LOG_A("Modem: DC EVMaximumCurrentLimit=%f %s.\n", Temp.Value * pow(10, Temp.Multiplier), Temp.Unit_isUsed ? UnitStr[Temp.Unit] : ""); //not using pow_10 because multiplier can be negative!

                Temp = dinDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.EVMaximumVoltageLimit;
                _LOG_A("Modem: DC EVMaximumVoltageLimit=%f %s.\n", Temp.Value * pow(10, Temp.Multiplier), Temp.Unit_isUsed ? UnitStr[Temp.Unit] : ""); //not using pow_10 because multiplier can be negative!
                Temp = dinDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.EVMaximumPowerLimit;
                _LOG_A("Modem: DC EVMaximumPowerLimit=%f %s.\n", Temp.Value * pow(10, Temp.Multiplier), Temp.Unit_isUsed ? UnitStr[Temp.Unit] : ""); //not using pow_10 because multiplier can be negative!

                if(dinDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.BulkSOC_isUsed) {
                    uint8_t BulkSOC = dinDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.BulkSOC;
                    _LOG_A("Modem: BulkSOC=%d.\n", BulkSOC); \
                }

                if(dinDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.FullSOC_isUsed) {
                    FullSoC = dinDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.FullSOC;
                    _LOG_A("Modem: set FullSoC=%d.\n", FullSoC);
                }

                uint32_t deptime = dinDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.AC_EVChargeParameter.DepartureTime;
                _LOG_A("Modem: Departure Time=%u.\n", deptime);

                Temp = dinDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.AC_EVChargeParameter.EAmount;
                _LOG_A("Modem: EAmount=%d %s.\n", Temp.Value, Temp.Unit_isUsed ? UnitStr[Temp.Unit] : "");
                Temp = dinDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.AC_EVChargeParameter.EVMaxVoltage;
                _LOG_A("Modem: EVMaxVoltage=%d %s.\n", Temp.Value, Temp.Unit_isUsed ? UnitStr[Temp.Unit] : "");
                Temp = dinDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.AC_EVChargeParameter.EVMaxCurrent;
                _LOG_A("Modem: EVMaxCurrent=%d %s.\n", Temp.Value, Temp.Unit_isUsed ? UnitStr[Temp.Unit] : "");
                Temp = dinDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.AC_EVChargeParameter.EVMinCurrent;
                _LOG_A("Modem: EVMinCurrent=%d %s.\n", Temp.Value, Temp.Unit_isUsed ? UnitStr[Temp.Unit] : "");

                if(dinDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.EVEnergyCapacity_isUsed) {
                    Temp = dinDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.EVEnergyCapacity;
                    EnergyCapacity = Temp.Value  * pow(10, Temp.Multiplier);
                    _LOG_A("Modem: set EVEnergyCapacity=%d %s.\n", Temp.Value, Temp.Unit_isUsed ? UnitStr[Temp.Unit] : "");
                }

                if(dinDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.EVEnergyRequest_isUsed) {
                    Temp = dinDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.EVEnergyRequest;
                    _LOG_A("Modem: set EVEnergyRequest=%d %s, Multiplier=%d.\n", Temp.Value, Temp.Unit_isUsed ? UnitStr[Temp.Unit] : "", Temp.Multiplier);
                    EnergyRequest = Temp.Value;
                }

                if (ComputedSoC >= 0 && ComputedSoC <= 100) { // valid
                    // Skip waiting, charge since we have what we've got
                    if (State == STATE_MODEM_REQUEST || State == STATE_MODEM_WAIT || State == STATE_MODEM_DONE){
                        _LOG_A("Received SoC via Modem. Shortcut to State Modem Done\n");
                        setState(STATE_MODEM_DONE); // Go to State B, which means in this case setting PWM
                        tcpState = TCP_STATE_CLOSED; //if we dont close the TCP connection the  next replug wont work TODO is this the right place, the right way?
                    }
                    if (InitialSoC < 0) //not initialized yet
                        InitialSoC = ComputedSoC;
                }


                int8_t Transfer = dinDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.EVRequestedEnergyTransferType;
                const char EnergyTransferStr[][25] = {"AC_single_phase_core","AC_three_phase_core","DC_core","DC_extended","DC_combo_core","DC_unique"};

                _LOG_A("Modem: Requested Energy Transfer Type =%s.\n", EnergyTransferStr[Transfer]);

                RecomputeSoC();

                // Now prepare the 'ChargeParameterDiscoveryResponse' message to send back to the EV
                init_din_BodyType(&dinDoc.V2G_Message.Body);
                init_din_ChargeParameterDiscoveryResType(&dinDoc.V2G_Message.Body.ChargeParameterDiscoveryRes);

                dinDoc.V2G_Message.Body.ChargeParameterDiscoveryRes_isUsed = 1;

                // Send SessionSetupResponse to EV
                EncodeAndTransmit(&dinDoc);
                //fsmState = stateWaitForCableCheckRequest;
                fsmState = stateWaitForSupportedApplicationProtocolRequest; //so we will request for SoC the next time we replug; we obviously dont know how to cleanly close a session TODO


            }
            return;
        }

        if (dinDoc.V2G_Message.Body.ChargingStatusReq_isUsed) {
            _LOG_A("Modem: ChargingStatusReq_isUsed!!\n");
        }
        return;
    } //DIN
    if (Protocol == ISO2) {
        struct iso2_exiDocument exiDoc;
        memset(&exiDoc, 0, sizeof(struct iso2_exiDocument));
        decode_iso2_exiDocument(&stream, &exiDoc);

        if (fsmState == stateWaitForSessionSetupRequest) {
            // Check if we have received the correct message
            if (exiDoc.V2G_Message.Body.SessionSetupReq_isUsed) {
                _LOG_I("SessionSetupRequest\n");

                //n = exiDoc.V2G_Message.Header.SessionID.bytesLen;
                //for (i=0; i< n; i++) {
                //    _LOG_D("%02x", exiDoc.V2G_Message.Header.SessionID.bytes[i] );
                //}
                uint8_t n = exiDoc.V2G_Message.Body.SessionSetupReq.EVCCID.bytesLen;
                if (n>6) n=6;       // out of range check
                for (uint8_t i=0; i<n; i++) {
                    EVCCID2[i]= exiDoc.V2G_Message.Body.SessionSetupReq.EVCCID.bytes[i];
                }
                _LOG_I("EVCCID=%02x%02x%02x%02x%02x%02x\n", EVCCID2[0], EVCCID2[1],EVCCID2[2],EVCCID2[3],EVCCID2[4],EVCCID2[5]);
                uint8_t sessionId[8];
                sessionId[0] = 1;   // our SessionId is set up here, and used by _prepare_DinExiDocument
                sessionId[1] = 2;   // This SessionID will be used by the EV in future communication
                sessionId[2] = 3;
                sessionId[3] = 4;
                uint8_t sessionIdLen = 4;

                // Now prepare the 'SessionSetupResponse' message to send back to the EV
                init_iso2_BodyType(&exiDoc.V2G_Message.Body);
                init_iso2_SessionSetupReqType(&exiDoc.V2G_Message.Body.SessionSetupReq);

                exiDoc.V2G_Message.Body.SessionSetupRes_isUsed = 1;
                exiDoc.V2G_Message.Body.SessionSetupRes.ResponseCode = iso2_responseCodeType_OK_NewSessionEstablished;
                char EVSEID[24]; // 23 characters + 1 for null terminator
                snprintf(EVSEID, sizeof(EVSEID), "SEV*01*SmartEVSE-%06u", serialnr);
                memcpy(exiDoc.V2G_Message.Body.SessionSetupRes.EVSEID.characters, &EVSEID, 23);
                exiDoc.V2G_Message.Body.SessionSetupRes.EVSEID.charactersLen = 23; //mandatory 23 characters

                // Send SessionSetupResponse to EV
                EncodeAndTransmit(&exiDoc);
                fsmState = stateWaitForServiceDiscoveryRequest;
            }
            return;
        }
        if (fsmState == stateWaitForServiceDiscoveryRequest) {
            // Check if we have received the correct message
            if (exiDoc.V2G_Message.Body.ServiceDiscoveryReq_isUsed) {

                _LOG_I("ServiceDiscoveryRequest\n");
                uint8_t n = exiDoc.V2G_Message.Header.SessionID.bytesLen;
                _LOG_I("SessionID:");
                for (uint8_t i=0; i<n; i++) _LOG_I_NO_FUNC("%02x", exiDoc.V2G_Message.Header.SessionID.bytes[i] );
                _LOG_I_NO_FUNC("\n");

                // Now prepare the 'ServiceDiscoveryResponse' message to send back to the EV
                init_iso2_BodyType(&exiDoc.V2G_Message.Body);
                init_iso2_ServiceDiscoveryReqType(&exiDoc.V2G_Message.Body.ServiceDiscoveryReq);
                exiDoc.V2G_Message.Body.ServiceDiscoveryRes_isUsed = 1;

                // set the service category
                exiDoc.V2G_Message.Body.ServiceDiscoveryReq.ServiceScope_isUsed = false;
                exiDoc.V2G_Message.Body.ServiceDiscoveryReq.ServiceCategory_isUsed = true;
                exiDoc.V2G_Message.Body.ServiceDiscoveryReq.ServiceCategory = iso2_serviceCategoryType_EVCharging;

                //init_dinServiceDiscoveryResType(&exiDocEnc.V2G_Message.Body.ServiceDiscoveryRes);
                exiDoc.V2G_Message.Body.ServiceDiscoveryRes.ResponseCode = iso2_responseCodeType_OK;
                //exiDoc.V2G_Message.Body.ServiceDiscoveryRes.ResponseCode = dinresponseCodeType_OK;
                // the mandatory fields in the ISO are PaymentOptionList and ChargeService.
                //But in the DIN, this is different, we find PaymentOptions, ChargeService and optional ServiceList
                exiDoc.V2G_Message.Body.ServiceDiscoveryRes.PaymentOptionList.PaymentOption.array[0] = iso2_paymentOptionType_ExternalPayment; // EVSE handles the payment
                exiDoc.V2G_Message.Body.ServiceDiscoveryRes.PaymentOptionList.PaymentOption.arrayLen = 1; // just one single payment option in the table
                exiDoc.V2G_Message.Body.ServiceDiscoveryRes.ChargeService.ServiceID = 1; // todo: not clear what this means
                //exiDocEnc.V2G_Message.Body.ServiceDiscoveryRes.ChargeService.ServiceTag.ServiceName
                //exiDocEnc.V2G_Message.Body.ServiceDiscoveryRes.ChargeService.ServiceTag.ServiceName_isUsed
                exiDoc.V2G_Message.Body.ServiceDiscoveryRes.ChargeService.ServiceCategory = iso2_serviceCategoryType_EVCharging;
                //exiDocEnc.V2G_Message.Body.ServiceDiscoveryRes.ChargeService.ServiceTag.ServiceScope
                //exiDocEnc.V2G_Message.Body.ServiceDiscoveryRes.ChargeService.ServiceTag.ServiceScope_isUsed
                exiDoc.V2G_Message.Body.ServiceDiscoveryRes.ChargeService.FreeService = 0; // what ever this means. Just from example.
                //exiDoc.V2G_Message.Body.ServiceDiscoveryRes.ChargeService.EnergyTransferType = iso2_EVSESupportedEnergyTransferType_DC_extended;
                exiDoc.V2G_Message.Body.ServiceDiscoveryRes.ChargeService.SupportedEnergyTransferMode.EnergyTransferMode.array[0] = iso2_EnergyTransferModeType_AC_three_phase_core;
                exiDoc.V2G_Message.Body.ServiceDiscoveryRes.ChargeService.SupportedEnergyTransferMode.EnergyTransferMode.arrayLen = 1;
                //exiDoc.V2G_Message.Body.ServiceDiscoveryRes.ChargeService.EnergyTransferType = iso2_EVSESupportedEnergyTransferType_AC_single_phase_three_phase_core_DC_extended;

                // Send ServiceDiscoveryResponse to EV
                EncodeAndTransmit(&exiDoc);
                fsmState = stateWaitForServicePaymentSelectionRequest;
            }
            return;
        }

        if (fsmState == stateWaitForServicePaymentSelectionRequest) {
            // Check if we have received the correct message
            if (exiDoc.V2G_Message.Body.PaymentServiceSelectionReq_isUsed) {
                _LOG_I("ServicePaymentSelectionRequest\n");
                if (exiDoc.V2G_Message.Body.PaymentServiceSelectionReq.SelectedPaymentOption == iso2_paymentOptionType_ExternalPayment) {
                    _LOG_I("OK. External Payment Selected\n");

                    // Now prepare the 'ServicePaymentSelectionResponse' message to send back to the EV
                    init_iso2_BodyType(&exiDoc.V2G_Message.Body);
                    init_iso2_PaymentServiceSelectionResType(&exiDoc.V2G_Message.Body.PaymentServiceSelectionRes);

                    exiDoc.V2G_Message.Body.PaymentServiceSelectionRes_isUsed = 1;
                    exiDoc.V2G_Message.Body.PaymentServiceSelectionRes.ResponseCode = iso2_responseCodeType_OK;

                    // Send SessionSetupResponse to EV
                    EncodeAndTransmit(&exiDoc);
                    fsmState = stateWaitForContractAuthenticationRequest;
                }
            }
            return;
        }

        if (fsmState == stateWaitForContractAuthenticationRequest) {
            // Check if we have received the correct message
            if (exiDoc.V2G_Message.Body.AuthorizationReq_isUsed) {
                _LOG_I("AuthorizationRequest\n");

                init_iso2_BodyType(&exiDoc.V2G_Message.Body);
                init_iso2_AuthorizationResType(&exiDoc.V2G_Message.Body.AuthorizationRes);

                exiDoc.V2G_Message.Body.AuthorizationRes_isUsed = 1;
                // Set Authorisation immediately to 'Finished'.
                exiDoc.V2G_Message.Body.AuthorizationRes.EVSEProcessing = iso2_EVSEProcessingType_Finished;

                // Send SessionSetupResponse to EV
                EncodeAndTransmit(&exiDoc);
                fsmState = stateWaitForChargeParameterDiscoveryRequest;
            }
            return;
        }

        if (fsmState == stateWaitForChargeParameterDiscoveryRequest) {
            // Check if we have received the correct message
            if (exiDoc.V2G_Message.Body.ChargeParameterDiscoveryReq_isUsed) {
                _LOG_I("ChargeParameterDiscoveryRequest\n");

                // Read the SOC from the EVRESSOC data
                ComputedSoC = exiDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.DC_EVStatus.EVRESSSOC;

                _LOG_I("Current SoC %d%%\n", ComputedSoC);
                String EVCCIDstr = "";
                for (uint8_t i = 0; i < 6; i++) {
                    if (EVCCID2[i] < 0x10) EVCCIDstr += "0";  // pad with zero for values less than 0x10
                    EVCCIDstr += String(EVCCID2[i], HEX);
                }
                _LOG_I("EVCCID=%s.\n", EVCCIDstr.c_str());
                strncpy(EVCCID, EVCCIDstr.c_str(), sizeof(EVCCID));
                Serial1.printf("@EVCCID:%s\n", EVCCID);  //send to CH32

                const char UnitStr[][4] = {"h" , "m" , "s" , "A" , "V" , "W" , "Wh"};
                iso2_PhysicalValueType Temp;

                //try to read this required field so we can test if we have communication ok with the EV
                Temp = exiDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.EVMaximumCurrentLimit;
                _LOG_A("Modem: DC EVMaximumCurrentLimit=%f %s.\n", Temp.Value * pow(10, Temp.Multiplier), UnitStr[Temp.Unit]); //not using pow_10 because multiplier can be negative!

                Temp = exiDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.EVMaximumVoltageLimit;
                _LOG_A("Modem: DC EVMaximumVoltageLimit=%f %s.\n", Temp.Value * pow(10, Temp.Multiplier), UnitStr[Temp.Unit]); //not using pow_10 because multiplier can be negative!
                Temp = exiDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.EVMaximumPowerLimit;
                _LOG_A("Modem: DC EVMaximumPowerLimit=%f %s.\n", Temp.Value * pow(10, Temp.Multiplier), UnitStr[Temp.Unit]); //not using pow_10 because multiplier can be negative!

                if(exiDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.BulkSOC_isUsed) {
                    uint8_t BulkSOC = exiDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.BulkSOC;
                    _LOG_A("Modem: BulkSOC=%d.\n", BulkSOC); \
                }

                if(exiDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.FullSOC_isUsed) {
                    FullSoC = exiDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.FullSOC;
                    _LOG_A("Modem: set FullSoC=%d.\n", FullSoC);
                }

                uint32_t deptime = exiDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.AC_EVChargeParameter.DepartureTime;
                _LOG_A("Modem: Departure Time=%u.\n", deptime);

                Temp = exiDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.AC_EVChargeParameter.EAmount;
                _LOG_A("Modem: EAmount=%d %s.\n", Temp.Value, UnitStr[Temp.Unit]);
                Temp = exiDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.AC_EVChargeParameter.EVMaxVoltage;
                _LOG_A("Modem: EVMaxVoltage=%d %s.\n", Temp.Value, UnitStr[Temp.Unit]);
                Temp = exiDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.AC_EVChargeParameter.EVMaxCurrent;
                _LOG_A("Modem: EVMaxCurrent=%d %s.\n", Temp.Value, UnitStr[Temp.Unit]);
                Temp = exiDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.AC_EVChargeParameter.EVMinCurrent;
                _LOG_A("Modem: EVMinCurrent=%d %s.\n", Temp.Value, UnitStr[Temp.Unit]);

                if(exiDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.EVEnergyCapacity_isUsed) {
                    Temp = exiDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.EVEnergyCapacity;
                    EnergyCapacity = Temp.Value  * pow(10, Temp.Multiplier);
                    _LOG_A("Modem: set EVEnergyCapacity=%d %s.\n", Temp.Value, UnitStr[Temp.Unit]);
                }

                if(exiDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.EVEnergyRequest_isUsed) {
                    Temp = exiDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.DC_EVChargeParameter.EVEnergyRequest;
                    _LOG_A("Modem: set EVEnergyRequest=%d %s, Multiplier=%d.\n", Temp.Value, UnitStr[Temp.Unit], Temp.Multiplier);
                    EnergyRequest = Temp.Value;
                }

                if (ComputedSoC >= 0 && ComputedSoC <= 100) { // valid
                    setState(STATE_MODEM_WAIT); //FIXME 
                    if (InitialSoC < 0) //not initialized yet
                        InitialSoC = ComputedSoC;
                }

                int8_t Transfer = exiDoc.V2G_Message.Body.ChargeParameterDiscoveryReq.RequestedEnergyTransferMode;
                const char EnergyTransferStr[][25] = {"AC_single_phase_core","AC_three_phase_core","DC_core","DC_extended","DC_combo_core","DC_unique"};

                _LOG_A("Modem: Requested Energy Transfer Type =%s.\n", EnergyTransferStr[Transfer]);

                //RecomputeSoC();

                // Now prepare the 'ChargeParameterDiscoveryResponse' message to send back to the EV
                init_iso2_BodyType(&exiDoc.V2G_Message.Body);
                init_iso2_ChargeParameterDiscoveryResType(&exiDoc.V2G_Message.Body.ChargeParameterDiscoveryRes);

                exiDoc.V2G_Message.Body.ChargeParameterDiscoveryRes_isUsed = 1;
                exiDoc.V2G_Message.Body.ChargeParameterDiscoveryRes.ResponseCode = iso2_responseCodeType_OK;
                exiDoc.V2G_Message.Body.ChargeParameterDiscoveryRes.EVSEProcessing = iso2_EVSEProcessingType_Ongoing;
                exiDoc.V2G_Message.Body.ChargeParameterDiscoveryRes.DC_EVSEChargeParameter_isUsed = 0;
                exiDoc.V2G_Message.Body.ChargeParameterDiscoveryRes.AC_EVSEChargeParameter_isUsed = 1;
                exiDoc.V2G_Message.Body.ChargeParameterDiscoveryRes.AC_EVSEChargeParameter.AC_EVSEStatus.NotificationMaxDelay = 0;
                exiDoc.V2G_Message.Body.ChargeParameterDiscoveryRes.AC_EVSEChargeParameter.AC_EVSEStatus.EVSENotification = iso2_EVSENotificationType_None;
                exiDoc.V2G_Message.Body.ChargeParameterDiscoveryRes.AC_EVSEChargeParameter.AC_EVSEStatus.RCD = (ErrorFlags & RCM_TRIPPED); //FIXME RCM_TEST
                exiDoc.V2G_Message.Body.ChargeParameterDiscoveryRes.AC_EVSEChargeParameter.EVSENominalVoltage.Value = 230;
                exiDoc.V2G_Message.Body.ChargeParameterDiscoveryRes.AC_EVSEChargeParameter.EVSENominalVoltage.Unit = iso2_unitSymbolType_V;
                exiDoc.V2G_Message.Body.ChargeParameterDiscoveryRes.AC_EVSEChargeParameter.EVSENominalVoltage.Multiplier = 0;
                exiDoc.V2G_Message.Body.ChargeParameterDiscoveryRes.AC_EVSEChargeParameter.EVSEMaxCurrent.Value = MaxCurrent;
                exiDoc.V2G_Message.Body.ChargeParameterDiscoveryRes.AC_EVSEChargeParameter.EVSEMaxCurrent.Unit = iso2_unitSymbolType_A;
                exiDoc.V2G_Message.Body.ChargeParameterDiscoveryRes.AC_EVSEChargeParameter.EVSEMaxCurrent.Multiplier = 0;

                // Send SessionSetupResponse to EV
//                exi_bitstream_t tx_stream; //TODO perhaps reuse stream?
//                exi_bitstream_init(&tx_stream, V2G_transmit_buffer, sizeof(V2G_transmit_buffer), 0, NULL);
                //g_errn = encode_din_exiDocument(&tx_stream, dinDoc);
                const char* hexString = "80980239cc442653682fd50a895a1d1d1c0e8bcbddddddcb9dcccb9bdc99cbd5148bd8d85b9bdb9a58d85b0b595e1a4bd0d5a1d1d1c0e8bcbddddddcb9dcccb9bdc99cbcc8c0c0c4bcc0d0bde1b5b191cda59cb5b5bdc9948d958d91cd84b5cda184c8d4d910311b4b218812b43a3a381d1797bbbbbb973b999737b93397aa2917b1b0b737b734b1b0b616b2bc3497a429687474703a2f2f7777772e77332e6f72672f323030312f30342f786d6c656e6323736861323536420ada970464881fee067d9353a43f210ab57b2d4c571be62bc726fd18366265d3d1280816a43b86bb9f304ad80e8395758a37fdfcdf5d1836d18f7b69d6f5f9baa566a1e648e04c5212463964554cf4c2874d91810a33f47de43195fb56ebbf73ad7108280000000000080a30503143e154200ad2c862049008000101460a001290000000c409003030c08000";
///////////////////////////
    size_t len = strlen(hexString);


    uint16_t byteCount = len / 2;
    uint8_t byteArray[byteCount];

    for (size_t i = 0; i < byteCount; i++) {
        char byteChars[3] = { hexString[i * 2], hexString[i * 2 + 1], '\0' };
        byteArray[i] = (uint8_t) strtol(byteChars, NULL, 16);
    }

/*     _LOG_A("Hex string[%zu]: %s\nBytes: ", byteCount, hexString);
    for (size_t i = 0; i < byteCount; i++) {
        _LOG_A_NO_FUNC("0x%02X ", byteArray[i]);
    }
    _LOG_A_NO_FUNC("\n");*/
//                char buf[512];
//                int len = 325;
//                for (uint16_t i=0; i < len; i++)
//                    sscanf(buf[i], "%02x", exiMsg[i*2]);
///////////////////////////
                addV2GTPHeaderAndTransmit(byteArray,byteCount); //not sure if byte_pos is the right variable
                //EncodeAndTransmit(&exiDoc);
                fsmState = stateWaitForPowerDeliveryRequest; //we did not negotiate scheduled charging so no need to wait for ScheduleExchangeReq

            }
            return;
        }
        if (fsmState == stateWaitForPowerDeliveryRequest) {
            if (exiDoc.V2G_Message.Body.PowerDeliveryReq_isUsed) {
//example PowerDeliveryRequest:
//{"V2G_Message": {"Header": {"SessionID": "E73110994DA0BF54"}, "Body": {"PowerDeliveryReq": {"ChargeProgress": "Start", "SAScheduleTupleID": 1, "ChargingProfile": {"ProfileEntry": [{"ChargingProfileEntryStart": 0, "ChargingProfileEntryMaxPower": {"Value": 11000, "Multiplier": 0, "Unit": "W"}}, {"ChargingProfileEntryStart": 86400, "ChargingProfileEntryMaxPower": {"Value": 0, "Multiplier": 0, "Unit": "W"}}]}}}}}
                const char ChargeProgressStr[][12] = {"Start" , "Stop" , "Renegotiate"};
                _LOG_I("PowerDeliveryRequest, ChargeProgress: %s.\n", ChargeProgressStr[exiDoc.V2G_Message.Body.PowerDeliveryReq.ChargeProgress]);

/*
                // Now prepare the 'ServicePaymentSelectionResponse' message to send back to the EV
                init_iso2_BodyType(&exiDoc.V2G_Message.Body);
                init_iso2_PowerDeliveryResType(&exiDoc.V2G_Message.Body.PowerDeliveryRes);

                //exiDoc.V2G_Message.Body.PaymentServiceSelectionRes_isUsed = 1;
                //exiDoc.V2G_Message.Body.PaymentServiceSelectionRes.ResponseCode = iso2_responseCodeType_OK;

                // Send SessionSetupResponse to EV
                EncodeAndTransmit(&exiDoc);
                fsmState = stateWaitForContractAuthenticationRequest;*/
            }
            return;
        }
/*

        if (dinDoc.V2G_Message.Body.ChargingStatusReq_isUsed) {
            _LOG_A("Modem: ChargingStatusReq_isUsed!!\n");
        }
        return;
*/
    } //ISO2
    _LOG_A("Modem: fsmState=%u, unknown message received.\n", fsmState);
}


void tcp_sendFirstAck(void) {
   // _LOG_D("[TCP] sending first ACK\n");
    tcp_prepareTcpHeader(TCP_FLAG_ACK | TCP_FLAG_SYN, NULL, 0);
}

void tcp_sendAck(void) {
//   _LOG_D("[TCP] sending ACK\n");
   tcp_prepareTcpHeader(TCP_FLAG_ACK, NULL, 0);
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
    flags = rxbuffer[67];
    _LOG_D("Source:%u Dest:%u Seqnr:%08x Acknr:%08x flags:%02x\n", SourcePort, DestinationPort, remoteSeqNr, remoteAckNr, flags);
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
        _LOG_I("[TCP] ignore, not connected.\n");
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

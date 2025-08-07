#if SMARTEVSE_VERSION >= 40
#include <Arduino.h>
#include "qca.h"
#include "debug.h"

const uint8_t broadcastIPv6[16] = { 0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
/* our link-local IPv6 address. Based on myMac, but with 0xFFFE in the middle, and bit 1 of MSB inverted */
uint8_t SeccIp[16];
uint8_t EvccIp[16];
uint16_t evccTcpPort; /* the TCP port number of the car */
uint8_t sourceIp[16];
uint16_t evccPort;
uint16_t seccPort;
uint16_t sourceport;
uint16_t destinationport;
uint16_t udplen;
uint16_t udpsum;
uint8_t NeighborsMac[6];
uint8_t NeighborsIp[16];
uint8_t DiscoveryReqSecurity;
uint8_t DiscoveryReqTransportProtocol;



#define NEXT_UDP 0x11 /* next protocol is UDP */
#define NEXT_ICMPv6 0x3a /* next protocol is ICMPv6 */

#define UDP_PAYLOAD_LEN 100
uint8_t udpPayload[UDP_PAYLOAD_LEN];
uint16_t udpPayloadLen;

#define V2G_FRAME_LEN 100
uint8_t v2gFrameLen;
uint8_t V2GFrame[V2G_FRAME_LEN];

#define UDP_RESPONSE_LEN 100
uint8_t UdpResponseLen;
uint8_t UdpResponse[UDP_RESPONSE_LEN];

#define IP_RESPONSE_LEN 100
uint8_t IpResponseLen;
uint8_t IpResponse[IP_RESPONSE_LEN];

#define PSEUDO_HEADER_LEN 40
uint8_t pseudoHeader[PSEUDO_HEADER_LEN];

extern void evaluateTcpPacket(void);

void setSeccIp() {
    // Create a link-local Ipv6 address based on myMac (the MAC of the ESP32).
    memset(SeccIp, 0, 16);
    SeccIp[0] = 0xfe;             // Link-local address
    SeccIp[1] = 0x80;
    // byte 2-7 are zero;
    SeccIp[8] = myMac[0] ^ 2;     // invert bit 1 of MSB
    SeccIp[9] = myMac[1];
    SeccIp[10] = myMac[2];
    SeccIp[11] = 0xff;
    SeccIp[12] = 0xfe;
    SeccIp[13] = myMac[3];
    SeccIp[14] = myMac[4];
    SeccIp[15] = myMac[5];
}


uint16_t calculateUdpAndTcpChecksumForIPv6(uint8_t *UdpOrTcpframe, uint16_t UdpOrTcpframeLen, const uint8_t *ipv6source, const uint8_t *ipv6dest, uint8_t nxt) {
	uint16_t evenFrameLen, i, value16, checksum;
	uint32_t totalSum;
    // Parameters:
    // UdpOrTcpframe: the udp frame or tcp frame, including udp/tcp header and udp/tcp payload
    // ipv6source: the 16 byte IPv6 source address. Must be the same, which is used later for the transmission.
    // ipv6source: the 16 byte IPv6 destination address. Must be the same, which is used later for the transmission.
	// nxt: The next-protocol. 0x11 for UDP, ... for TCP.
	//
    // Goal: construct an array, consisting of a 40-byte-pseudo-ipv6-header, and the udp frame (consisting of udp header and udppayload).
	// For memory efficienty reason, we do NOT copy the pseudoheader and the udp frame together into one new array. Instead, we are using
	// a dedicated pseudo-header-array, and the original udp buffer.
	evenFrameLen = UdpOrTcpframeLen;
	if ((evenFrameLen & 1)!=0) {
        /* if we have an odd buffer length, we need to add a padding byte in the end, because the sum calculation
           will need 16-bit-aligned data. */
		evenFrameLen++;
		UdpOrTcpframe[evenFrameLen-1] = 0; /* Fill the padding byte with zero. */
	}
    memset(pseudoHeader, 0, PSEUDO_HEADER_LEN);
    /* fill the pseudo-ipv6-header */
    for (i=0; i<16; i++) { /* copy 16 bytes IPv6 addresses */
        pseudoHeader[i] = ipv6source[i]; /* IPv6 source address */
        pseudoHeader[16+i] = ipv6dest[i]; /* IPv6 destination address */
	}
    pseudoHeader[32] = 0; // # high byte of the FOUR byte length is always 0
    pseudoHeader[33] = 0; // # 2nd byte of the FOUR byte length is always 0
    pseudoHeader[34] = UdpOrTcpframeLen >> 8; // # 3rd
    pseudoHeader[35] = UdpOrTcpframeLen & 0xFF; // # low byte of the FOUR byte length
    pseudoHeader[36] = 0; // # 3 padding bytes with 0x00
    pseudoHeader[37] = 0;
    pseudoHeader[38] = 0;
    pseudoHeader[39] = nxt; // # the nxt is at the end of the pseudo header
    // pseudo-ipv6-header finished.
    // Run the checksum over the concatenation of the pseudoheader and the buffer.


    totalSum = 0;
	for (i=0; i<PSEUDO_HEADER_LEN/2; i++) { // running through the pseudo header, in 2-byte-steps
        value16 = pseudoHeader[2*i] * 256 + pseudoHeader[2*i+1]; // take the current 16-bit-word
        totalSum += value16; // we start with a normal addition of the value to the totalSum
        // But we do not want normal addition, we want a 16 bit one's complement sum,
        // see https://en.wikipedia.org/wiki/User_Datagram_Protocol
        if (totalSum>=65536) { // On each addition, if a carry-out (17th bit) is produced,
            totalSum-=65536; // swing that 17th carry bit around
            totalSum+=1; // and add it to the least significant bit of the running total.
		}
	}
	for (i=0; i<evenFrameLen/2; i++) { // running through the udp buffer, in 2-byte-steps
        value16 = UdpOrTcpframe[2*i] * 256 + UdpOrTcpframe[2*i+1]; // take the current 16-bit-word
        totalSum += value16; // we start with a normal addition of the value to the totalSum
        // But we do not want normal addition, we want a 16 bit one's complement sum,
        // see https://en.wikipedia.org/wiki/User_Datagram_Protocol
        if (totalSum>=65536) { // On each addition, if a carry-out (17th bit) is produced,
            totalSum-=65536; // swing that 17th carry bit around
            totalSum+=1; // and add it to the least significant bit of the running total.
		}
	}
    // Finally, the sum is then one's complemented to yield the value of the UDP checksum field.
    checksum = (uint16_t) (totalSum ^ 0xffff);

    return checksum;
}

void packResponseIntoEthernet() {
    // packs the IP packet into an ethernet packet
    uint8_t i;
    uint16_t EthTxFrameLen;

    EthTxFrameLen = IpResponseLen + 6 + 6 + 2;  // Ethernet header needs 14 bytes:
                                                //  6 bytes destination MAC
                                                //  6 bytes source MAC
                                                //  2 bytes EtherType
    for (i=0; i<6; i++) {       // fill the destination MAC with the source MAC of the received package
        txbuffer[i] = rxbuffer[6+i];
    }
    setMacAt(myMac,6); // bytes 6 to 11 are the source MAC
    txbuffer[12] = 0x86; // 86dd is IPv6
    txbuffer[13] = 0xdd;
    for (i=0; i<IpResponseLen; i++) {
        txbuffer[14+i] = IpResponse[i];
    }

    qcaspi_write_burst(txbuffer, EthTxFrameLen);
}

void packResponseIntoIp(void) {
  // # embeds the (SDP) response into the lower-layer-protocol: IP, Ethernet
  uint8_t i;
  uint16_t plen;
  IpResponseLen = UdpResponseLen + 8 + 16 + 16; // # IP6 header needs 40 bytes:
                                              //  #   4 bytes traffic class, flow
                                              //  #   2 bytes destination port
                                              //  #   2 bytes length (incl checksum)
                                              //  #   2 bytes checksum
  IpResponse[0] = 0x60; // # traffic class, flow
  IpResponse[1] = 0;
  IpResponse[2] = 0;
  IpResponse[3] = 0;
  plen = UdpResponseLen; // length of the payload. Without headers.
  IpResponse[4] = plen >> 8;
  IpResponse[5] = plen & 0xFF;
  IpResponse[6] = 0x11; // next level protocol, 0x11 = UDP in this case
  IpResponse[7] = 0x0A; // hop limit
    for (i=0; i<16; i++) {
    IpResponse[8+i] = SeccIp[i]; // source IP address
    IpResponse[24+i] = EvccIp[i]; // destination IP address
  }
  for (i=0; i<UdpResponseLen; i++) {
    IpResponse[40+i] = UdpResponse[i];
  }
  packResponseIntoEthernet();
}


void packResponseIntoUdp(void) {
    //# embeds the (SDP) request into the lower-layer-protocol: UDP
    //# Reference: wireshark trace of the ioniq car
    uint16_t lenInclChecksum;
    uint16_t checksum;
    UdpResponseLen = v2gFrameLen + 8; // # UDP header needs 8 bytes:
                                        //           #   2 bytes source port
                                        //           #   2 bytes destination port
                                        //           #   2 bytes length (incl checksum)
                                        //           #   2 bytes checksum
    UdpResponse[0] = 15118 >> 8;
    UdpResponse[1] = 15118  & 0xFF;
    UdpResponse[2] = evccPort >> 8;
    UdpResponse[3] = evccPort & 0xFF;

    lenInclChecksum = UdpResponseLen;
    UdpResponse[4] = lenInclChecksum >> 8;
    UdpResponse[5] = lenInclChecksum & 0xFF;
    // checksum will be calculated afterwards
    UdpResponse[6] = 0;
    UdpResponse[7] = 0;
    memcpy(UdpResponse+8, V2GFrame, v2gFrameLen);
    // The content of buffer is ready. We can calculate the checksum. see https://en.wikipedia.org/wiki/User_Datagram_Protocol
    checksum =calculateUdpAndTcpChecksumForIPv6(UdpResponse, UdpResponseLen, SeccIp, EvccIp, NEXT_UDP);
    UdpResponse[6] = checksum >> 8;
    UdpResponse[7] = checksum & 0xFF;
    packResponseIntoIp();
}


// SECC Discovery Response.
// The response from the charger to the EV, which transfers the IPv6 address of the charger to the car.
void sendSdpResponse() {
    uint8_t lenSdp;
    uint8_t SdpPayload[20]; // SDP response has 20 bytes

    memcpy(SdpPayload, SeccIp, 16); // 16 bytes IPv6 address of the charger.
                                    // This IP address is based on the MAC of the ESP32, with 0xfffe in the middle.
    // Here the charger decides, on which port he will listen for the TCP communication.
    // We use port 15118, same as for the SDP. But also dynamically assigned port would be ok.
    // The alpitronics seems to use different ports on different chargers, e.g. 0xC7A7 and 0xC7A6.
    // The ABB Triple and ABB HPC are reporting port 0xD121, but in fact (also?) listening
    // to the port 15118.
    seccPort = 15118;
    SdpPayload[16] = seccPort >> 8; // SECC port high byte.
    SdpPayload[17] = seccPort & 0xff; // SECC port low byte.
    SdpPayload[18] = 0x10; // security. We only support "no transport layer security, 0x10".
    SdpPayload[19] = 0x00; // transport protocol. We only support "TCP, 0x00".

    // add the SDP header
    lenSdp = sizeof(SdpPayload);
    V2GFrame[0] = 0x01; // version
    V2GFrame[1] = 0xfe; // version inverted
    V2GFrame[2] = 0x90; // payload type. 0x9001 is the SDP response message
    V2GFrame[3] = 0x01; //
    V2GFrame[4] = (lenSdp >> 24) & 0xff; // 4 byte payload length
    V2GFrame[5] = (lenSdp >> 16) & 0xff;
    V2GFrame[6] = (lenSdp >> 8) & 0xff;
    V2GFrame[7] = lenSdp & 0xff;
    memcpy(V2GFrame+8, SdpPayload, lenSdp);         // ToDo: Check lenSdp against buffer size!
    v2gFrameLen = lenSdp + 8;
    packResponseIntoUdp();
}


void evaluateUdpPayload(void) {
    uint16_t v2gptPayloadType;
    uint32_t v2gptPayloadLen;

    if (destinationport == 15118) { // port for the SECC
      if ((udpPayload[0] == 0x01) && (udpPayload[1] == 0xFE)) { //# protocol version 1 and inverted
        // we are the charger, and it is a message from car to charger, lets save the cars IP and port for later use.
        memcpy(EvccIp, sourceIp, 16);
        evccPort = sourceport;
        //addressManager.setPevIp(EvccIp);

        // it is a V2GTP message
        // payload is usually: 01 fe 90 00 00 00 00 02 10 00
        v2gptPayloadType = udpPayload[2]*256 + udpPayload[3];
        // 0x8001 EXI encoded V2G message (Will NOT come with UDP. Will come with TCP.)
        // 0x9000 SDP request message (SECC Discovery)
        // 0x9001 SDP response message (SECC response to the EVCC)
        if (v2gptPayloadType == 0x9000) {
            // it is a SDP request from the car to the charger
            _LOG_I("it is a SDP request from the car to the charger\n");
            v2gptPayloadLen = (((uint32_t)udpPayload[4])<<24)  +
                              (((uint32_t)udpPayload[5])<<16) +
                              (((uint32_t)udpPayload[6])<<8) +
                              udpPayload[7];
            if (v2gptPayloadLen == 2) {
                //# 2 is the only valid length for a SDP request.
                DiscoveryReqSecurity = udpPayload[8]; // normally 0x10 for "no transport layer security". Or 0x00 for "TLS".
                DiscoveryReqTransportProtocol = udpPayload[9]; // normally 0x00 for TCP
                if (DiscoveryReqSecurity != 0x10) {
                    _LOG_W("DiscoveryReqSecurity %u is not supported\n", DiscoveryReqSecurity);
                } else if (DiscoveryReqTransportProtocol != 0x00) {
                    _LOG_W("DiscoveryReqTransportProtocol %u is not supported\n", DiscoveryReqTransportProtocol);
                } else {
                    // This was a valid SDP request. Let's respond, if we are the charger.
                    _LOG_I("Ok, this was a valid SDP request. We are the SECC. Sending SDP response.\n");
                    sendSdpResponse();
                }
            } else {
                _LOG_W("v2gptPayloadLen on SDP request is %u not supported\n", v2gptPayloadLen);
            }
        } else {
            _LOG_W("v2gptPayloadType %04x not supported\n", v2gptPayloadType);
        }
    }
  }
}

void evaluateNeighborSolicitation(void) {
    uint16_t checksum;

    /* The neighbor discovery protocol is used by the charger to find out the
        relation between MAC and IP. */

    /* We could extract the necessary information from the NeighborSolicitation,
        means the chargers IP and MAC address. But this is not fully necessary:
        - The chargers MAC was already discovered in the SLAC. So we do not need to extract
        it here again. But if we have not done the SLAC, because the modems are already paired,
        then it makes sense to extract the chargers MAC from the Neighbor Solicitation message.
        - For the chargers IPv6, there are two possible cases:
            (A) The charger made the SDP without NeighborDiscovery. This works, if
                we use the pyPlc.py as charger. It does not care for NeighborDiscovery,
                because the SDP is implemented independent of the address resolution of
                the operating system.
                In this case, we know the chargers IP already from the SDP.
            (B) The charger insists of doing NeighborSolitcitation in the middle of
                SDP. This behavior was observed on Alpitronics. Means, we have the
                following sequence:
                1. car sends SDP request
                2. charger sends NeighborSolicitation
                3. car sends NeighborAdvertisement
                4. charger sends SDP response
                In this case, we need to extract the chargers IP from the NeighborSolicitation,
                otherwise we have to chance to send the correct NeighborAdvertisement.
                We can do this always, because this does not hurt for case A, address
                is (hopefully) not changing. */
    /* More general approach: In the network there may be more participants than only the charger,
        e.g. a notebook for sniffing. Eeach of it may send a NeighborSolicitation, and we should NOT use the addresses from the
        NeighborSolicitation as addresses of the charger. The chargers address is only determined
        by the SDP. */

    /* save the requesters IP. The requesters IP is the source IP on IPv6 level, at byte 22. */
    memcpy(NeighborsIp, rxbuffer+22, 16);
    /* save the requesters MAC. The requesters MAC is the source MAC on Eth level, at byte 6. */
    memcpy(NeighborsMac, rxbuffer+6, 6);

    /* send a NeighborAdvertisement as response. */
    // destination MAC = neighbors MAC
    setMacAt(NeighborsMac, 0); // bytes 0 to 5 are the destination MAC
    // source MAC = my MAC
    setMacAt(myMac, 6); // bytes 6 to 11 are the source MAC
    // Ethertype 86DD
    txbuffer[12] = 0x86; // # 86dd is IPv6
    txbuffer[13] = 0xdd;
    txbuffer[14] = 0x60; // # traffic class, flow
    txbuffer[15] = 0;
    txbuffer[16] = 0;
    txbuffer[17] = 0;
    // plen
    #define ICMP_LEN 32 /* bytes in the ICMPv6 */
    txbuffer[18] = 0;
    txbuffer[19] = ICMP_LEN;
    txbuffer[20] = NEXT_ICMPv6;
    txbuffer[21] = 0xff;
    // We are the EVSE. So the SeccIp is our own link-local IP address.
    memcpy(txbuffer+22, SeccIp, 16); // source IP address
    memcpy(txbuffer+38, NeighborsIp, 16); // destination IP address
    /* here starts the ICMPv6 */
    txbuffer[54] = 0x88; /* Neighbor Advertisement */
    txbuffer[55] = 0;
    txbuffer[56] = 0; /* checksum (filled later) */
    txbuffer[57] = 0;

    /* Flags */
    txbuffer[58] = 0x60; /* Solicited, override */
    txbuffer[59] = 0;
    txbuffer[60] = 0;
    txbuffer[61] = 0;

    memcpy(txbuffer+62, SeccIp, 16); /* The own IP address */
    txbuffer[78] = 2; /* Type 2, Link Layer Address */
    txbuffer[79] = 1; /* Length 1, means 8 byte (?) */
    memcpy(txbuffer+80, myMac, 6); /* The own Link Layer (MAC) address */

    checksum = calculateUdpAndTcpChecksumForIPv6(txbuffer+54, ICMP_LEN, SeccIp, NeighborsIp, NEXT_ICMPv6);
    txbuffer[56] = checksum >> 8;
    txbuffer[57] = checksum & 0xFF;

    _LOG_I("transmitting Neighbor Advertisement\n");
    /* Length of the NeighborAdvertisement = 86*/
    qcaspi_write_burst(txbuffer, 86);
}


void IPv6Manager(uint16_t rxbytes) {
    uint16_t x;
    uint16_t nextheader;
    uint8_t icmpv6type;

   // _LOG_D("\n[RX] ");
   // for (x=0; x<rxbytes; x++) _LOG_D("%02x",rxbuffer[x]);
   // _LOG_D("\n");

    //# The evaluation function for received ipv6 packages.

    if (rxbytes > 60) {
        //# extract the source ipv6 address
        memcpy(sourceIp, rxbuffer+22, 16);
        nextheader = rxbuffer[20];
        if (nextheader == 0x11) { //  it is an UDP frame
            _LOG_I("Its a UDP.\n");
            sourceport = rxbuffer[54]*256 + rxbuffer[55];
            destinationport = rxbuffer[56]*256 + rxbuffer[57];
            udplen = rxbuffer[58]*256 + rxbuffer[59];
            udpsum = rxbuffer[60]*256 + rxbuffer[61];

            //# udplen is including 8 bytes header at the begin
            if (udplen>UDP_PAYLOAD_LEN) {
                /* ignore long UDP */
                _LOG_I("Ignoring too long UDP\n");
                return;
            }
            if (udplen>8) {
                udpPayloadLen = udplen-8;
                for (x=0; x<udplen-8; x++) {
                    udpPayload[x] = rxbuffer[62+x];
                }
                evaluateUdpPayload();
            }
        }
        if (nextheader == 0x06) { // # it is an TCP frame
        //    _LOG_D("TCP received\n");
            evaluateTcpPacket();
        }
        if (nextheader == NEXT_ICMPv6) { // it is an ICMPv6 (NeighborSolicitation etc) frame
            _LOG_I("ICMPv6 received\n");
            icmpv6type = rxbuffer[54];
            if (icmpv6type == 0x87) { /* Neighbor Solicitation */
                _LOG_I("Neighbor Solicitation received\n");
                evaluateNeighborSolicitation();
            }
        }
  }

}
#endif

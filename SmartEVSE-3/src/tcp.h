#if SMARTEVSE_VERSION >= 40
void evaluateTcpPacket(void);
void tcp_prepareTcpHeader(uint8_t tcpFlag);
void tcp_packRequestIntoIp(void);
#endif

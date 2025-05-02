

#include "projectExiConnector.h"
#include <string.h>

uint8_t exiTransmitBuffer[EXI_TRANSMIT_BUFFER_SIZE];
struct dinEXIDocument dinDocEnc;
struct dinEXIDocument dinDocDec;
struct appHandEXIDocument aphsDoc;
struct appHandEXIDocument appHandResp;
bitstream_t global_streamEnc;
bitstream_t global_streamDec;
size_t global_streamEncPos;
size_t global_streamDecPos;
int g_errn;
uint8_t sessionId[SESSIONID_LEN];
uint8_t sessionIdLen;


#if defined(__cplusplus)
extern "C"
{
#endif
void addToTrace_chararray(char *s);
#if defined(__cplusplus)
}
#endif


void projectExiConnector_decode_appHandExiDocument(void) {
  /* precondition: The global_streamDec.size and global_streamDec.data have been set to the byte array with EXI data. */

  global_streamDec.pos = &global_streamDecPos;
  *(global_streamDec.pos) = 0; /* the decoder shall start at the byte 0 */	
  g_errn = decode_appHandExiDocument(&global_streamDec, &aphsDoc);
}

void projectExiConnector_decode_DinExiDocument(void) {
  /* precondition: The global_streamDec.size and global_streamDec.data have been set to the byte array with EXI data. */

  global_streamDec.pos = &global_streamDecPos;
  *(global_streamDec.pos) = 0; /* the decoder shall start at the byte 0 */	
  g_errn = decode_dinExiDocument(&global_streamDec, &dinDocDec);
}

#ifdef NOT_USED
void projectExiConnector_testEncode(void) {
	projectExiConnector_prepare_DinExiDocument();
	dinDocEnc.V2G_Message.Body.SessionSetupReq_isUsed = 1u;
	init_dinSessionSetupReqType(&dinDocEnc.V2G_Message.Body.SessionSetupReq);
	/* In the session setup request, the session ID zero means: create a new session.
	   The format (len 8, all zero) is taken from the original Ioniq behavior. */
	dinDocEnc.V2G_Message.Header.SessionID.bytes[0] = 0;
	dinDocEnc.V2G_Message.Header.SessionID.bytes[1] = 0;
	dinDocEnc.V2G_Message.Header.SessionID.bytes[2] = 0;
	dinDocEnc.V2G_Message.Header.SessionID.bytes[3] = 0;
	dinDocEnc.V2G_Message.Header.SessionID.bytes[4] = 0;
	dinDocEnc.V2G_Message.Header.SessionID.bytes[5] = 0;
	dinDocEnc.V2G_Message.Header.SessionID.bytes[6] = 0;
	dinDocEnc.V2G_Message.Header.SessionID.bytes[7] = 0;
	dinDocEnc.V2G_Message.Header.SessionID.bytesLen = 8;
	projectExiConnector_encode_DinExiDocument();
}
#endif

void projectExiConnector_prepare_DinExiDocument(void) {
	/* before filling and encoding the dinDocEnc, we initialize here all its content. */
	init_dinEXIDocument(&dinDocEnc);
	dinDocEnc.V2G_Message_isUsed = 1u;
	init_dinMessageHeaderType(&dinDocEnc.V2G_Message.Header);
	init_dinBodyType(&dinDocEnc.V2G_Message.Body);
	/* take the sessionID from the global variable: */
	memcpy(dinDocEnc.V2G_Message.Header.SessionID.bytes, sessionId, SESSIONID_LEN);
	dinDocEnc.V2G_Message.Header.SessionID.bytesLen = sessionIdLen;
}

void projectExiConnector_encode_DinExiDocument(void) {
    /* precondition: dinDocEnc structure is filled. Output: global_stream.data and global_stream.pos. */  
	global_streamEnc.size = EXI_TRANSMIT_BUFFER_SIZE;
	global_streamEnc.data = exiTransmitBuffer;
	global_streamEnc.pos = &global_streamEncPos;	
	*(global_streamEnc.pos) = 0; /* start adding data at position 0 */
	g_errn = encode_dinExiDocument(&global_streamEnc, &dinDocEnc);

}

void projectExiConnector_encode_appHandExiDocument(uint8_t SchemaID) {
	/* before filling and encoding the appHandResp, we initialize here all its content. */
  	init_appHandEXIDocument(&appHandResp);
	appHandResp.supportedAppProtocolRes_isUsed = 1;
	appHandResp.supportedAppProtocolRes.ResponseCode = appHandresponseCodeType_OK_SuccessfulNegotiation;
	appHandResp.supportedAppProtocolRes.SchemaID = SchemaID; /* signal the protocol by the provided schema id*/
	appHandResp.supportedAppProtocolRes.SchemaID_isUsed = 1;

	global_streamEnc.size = EXI_TRANSMIT_BUFFER_SIZE;
	global_streamEnc.data = exiTransmitBuffer;
	global_streamEnc.pos = &global_streamEncPos;	
	*(global_streamEnc.pos) = 0; /* start adding data at position 0 */
	g_errn = encode_appHandExiDocument(&global_streamEnc, &appHandResp);
	
}



#ifdef NOT_USED
int projectExiConnector_test(int a) {
  unsigned int i;
  char s[100];
  char s2[100];
  strcpy(gDebugString, "");
  global_streamDec.size = mytestbufferLen;
  global_streamDec.data = mytestbuffer;
  global_streamDec.pos = &global_streamDecPos;
  *(global_streamDec.pos) = 0; /* the decoder shall start at the byte 0 */	
  
  for (i=0; i<1000; i++) { /* for runtime measuremement, run the decoder n times. */
    g_errn = decode_dinExiDocument(&global_streamDec, &dinDocDec);
  }
  //dinDocDec.V2G_Message.Header.SessionID.bytesLen
  addToTrace_chararray("Test from projectExiConnector_test");
  sprintf(s, "SessionID ");
  for (i=0; i<4; i++) {
	sprintf(s2, "%hx ", dinDocDec.V2G_Message.Header.SessionID.bytes[i]);
	strcat(s, s2);
  }
  addToTrace_chararray(s);  
  //g_errn = encode_dinExiDocument(&global_streamEnc, &dinDocEnc);
 return 2*a;
}
#endif
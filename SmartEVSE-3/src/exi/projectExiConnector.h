

#include "EXITypes.h"

#include "appHandEXIDatatypes.h"
#include "appHandEXIDatatypesEncoder.h"
#include "appHandEXIDatatypesDecoder.h"

#include "dinEXIDatatypes.h"
#include "dinEXIDatatypesEncoder.h"
#include "dinEXIDatatypesDecoder.h"

#define EXI_TRANSMIT_BUFFER_SIZE 256
extern uint8_t exiTransmitBuffer[EXI_TRANSMIT_BUFFER_SIZE]; /* after encoding, here we find the exi byte stream. */

extern struct appHandEXIDocument aphsDoc; /* The application handshake document. */
extern struct dinEXIDocument dinDocEnc; /* The DIN document. For encoder. */
extern struct dinEXIDocument dinDocDec; /* The DIN document. For decoder. */
extern bitstream_t global_streamEnc; /* The byte stream descriptor. */
extern bitstream_t global_streamDec; /* The byte stream descriptor. */
extern size_t global_streamEncPos; /* The position in the stream. */
extern size_t global_streamDecPos; /* The position in the stream. */
extern char gResultString[500]; /* Debug info from the decoder. */
extern int g_errn;

#define SESSIONID_LEN 8
extern uint8_t sessionId[SESSIONID_LEN];
extern uint8_t sessionIdLen;



/* Decoder functions *****************************************************************************************/

#if defined(__cplusplus)
extern "C"
{
#endif
void projectExiConnector_decode_appHandExiDocument(void);
  /* precondition: The global_streamDec.size and global_streamDec.data have been set to the byte array with EXI data. */
#if defined(__cplusplus)
}
#endif

#if defined(__cplusplus)
extern "C"
{
#endif
void projectExiConnector_decode_DinExiDocument(void);
  /* precondition: The global_streamDec.size and global_streamDec.data have been set to the byte array with EXI data. */
#if defined(__cplusplus)
}
#endif


/* Encoder functions ****************************************************************************************/
#if defined(__cplusplus)
extern "C"
{
#endif
void projectExiConnector_prepare_DinExiDocument(void);
/* before filling and encoding the dinDocEnc, we initialize here all its content. */
#if defined(__cplusplus)
}
#endif


#if defined(__cplusplus)
extern "C"
{
#endif
void projectExiConnector_encode_DinExiDocument(void);
  /* precondition: dinDocEnc structure is filled. Output: global_stream.data and global_stream.pos. */  
#if defined(__cplusplus)
}
#endif


#if defined(__cplusplus)
extern "C"
{
#endif
void projectExiConnector_encode_appHandExiDocument(uint8_t SchemaID);
  
#if defined(__cplusplus)
}
#endif


/* Test functions, just for experimentation *****************************************************************/
#if defined(__cplusplus)
extern "C"
{
#endif
void projectExiConnector_testEncode(void);
#if defined(__cplusplus)
}
#endif


#if defined(__cplusplus)
extern "C"
{
#endif
int projectExiConnector_test(int a);
#if defined(__cplusplus)
}
#endif

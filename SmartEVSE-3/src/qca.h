#if SMARTEVSE_VERSION >= 40
#include <SPI.h>
extern SPIClass QCA_SPI1;

uint16_t qcaspi_read_register16(uint16_t reg);
void qcaspi_write_register(uint16_t reg, uint16_t value);
void qcaspi_write_burst(uint8_t *src, uint32_t len);
uint32_t qcaspi_read_burst(uint8_t *dst);


// Pin definitions
/*====================================================================*
 *   SPI registers QCA700X
 *--------------------------------------------------------------------*/

#define QCA7K_SPI_READ (1 << 15)                // MSB(15) of each command (16 bits) is the read(1) or write(0) bit.
#define QCA7K_SPI_WRITE (0 << 15)
#define QCA7K_SPI_INTERNAL (1 << 14)            // MSB(14) sets the Internal Registers(1) or Data Buffer(0)
#define QCA7K_SPI_EXTERNAL (0 << 14)

#define	SPI_REG_BFR_SIZE        0x0100
#define SPI_REG_WRBUF_SPC_AVA   0x0200
#define SPI_REG_RDBUF_BYTE_AVA  0x0300
#define SPI_REG_SPI_CONFIG      0x0400
#define SPI_REG_INTR_CAUSE      0x0C00
#define SPI_REG_INTR_ENABLE     0x0D00
#define SPI_REG_RDBUF_WATERMARK 0x1200
#define SPI_REG_WRBUF_WATERMARK 0x1300
#define SPI_REG_SIGNATURE       0x1A00
#define SPI_REG_ACTION_CTRL     0x1B00

#define QCASPI_GOOD_SIGNATURE   0xAA55
#define QCA7K_BUFFER_SIZE       3163

#define SPI_INT_WRBUF_BELOW_WM (1 << 10)
#define SPI_INT_CPU_ON         (1 << 6)
#define SPI_INT_ADDR_ERR       (1 << 3)
#define SPI_INT_WRBUF_ERR      (1 << 2)
#define SPI_INT_RDBUF_ERR      (1 << 1)
#define SPI_INT_PKT_AVLBL      (1 << 0)

/*====================================================================*
 *   States
 *--------------------------------------------------------------------*/

#define MODEM_POWERUP 0
#define MODEM_WRITESPACE 1
#define MODEM_CM_SET_KEY_REQ 2
#define MODEM_CM_SET_KEY_CNF 3
#define MODEM_CONFIGURED 10
#define SLAC_PARAM_REQ 20
#define SLAC_PARAM_CNF 30
#define MNBC_SOUND 40
#define ATTEN_CHAR_IND 50
#define ATTEN_CHAR_RSP 60
#define SLAC_MATCH_REQ 70

#define MODEM_LINK_STATUS 80
#define MODEM_WAIT_LINK 90
#define MODEM_GET_SW_REQ 100
#define MODEM_WAIT_SW 110
#define MODEM_LINK_READY 120

#define MODEM_PRESET_NMK  255

/*====================================================================*
 *   SLAC commands
 *--------------------------------------------------------------------*/

#define CM_SET_KEY 0x6008
#define CM_GET_KEY 0x600C
#define CM_SC_JOIN 0x6010
#define CM_CHAN_EST 0x6014
#define CM_TM_UPDATE 0x6018
#define CM_AMP_MAP 0x601C
#define CM_BRG_INFO 0x6020
#define CM_CONN_NEW 0x6024
#define CM_CONN_REL 0x6028
#define CM_CONN_MOD 0x602C
#define CM_CONN_INFO 0x6030
#define CM_STA_CAP 0x6034
#define CM_NW_INFO 0x6038
#define CM_GET_BEACON 0x603C
#define CM_HFID 0x6040
#define CM_MME_ERROR 0x6044
#define CM_NW_STATS 0x6048
#define CM_SLAC_PARAM 0x6064
#define CM_START_ATTEN_CHAR 0x6068
#define CM_ATTEN_CHAR 0x606C
#define CM_PKCS_CERT 0x6070
#define CM_MNBC_SOUND 0x6074
#define CM_VALIDATE 0x6078
#define CM_SLAC_MATCH 0x607C
#define CM_SLAC_USER_DATA 0x6080
#define CM_ATTEN_PROFILE 0x6084
#define CM_GET_SW 0xA000
#define CM_LINK_STATUS 0xA0B8

#define MMTYPE_REQ 0x0000   // request
#define MMTYPE_CNF 0x0001   // confirmation = +1
#define MMTYPE_IND 0x0002
#define MMTYPE_RSP 0x0003

// Frametypes

#define FRAME_IPV6 0x86DD
#define FRAME_HOMEPLUG 0x88E1

extern uint8_t txbuffer[], rxbuffer[];
extern uint8_t myMac[];
extern uint8_t pevMac[];
extern uint8_t EVCCID2[];
void qcaspi_write_burst(uint8_t *src, uint32_t len);
void setMacAt(uint8_t *mac, uint16_t offset);
#endif

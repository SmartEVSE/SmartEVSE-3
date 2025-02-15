#if SMARTEVSE_VERSION >= 40
/*====================================================================*
 *  WCH Serial Bootloader commands
 *--------------------------------------------------------------------*/

#define WCH_START           0xA1
#define WCH_STOP            0xA2
#define WCH_SET_KEY         0xA3
#define WCH_ERASE_FLASH     0xA4
#define WCH_PROGRAM_FLASH   0xA5
#define WCH_VERIFY_FLASH    0xA6
#define WCH_READ_OPTION     0xA7
#define WCH_WRITE_OPTION    0xA8

#define WCH_SET_BAUDRATE    0xC5
#define WCH_EXIT            0x00

#define WCH_TX_HEADER       0x57
#define WCH_RX_HEADER       0x5A

#define WCHDEBUG           // Display serial comm


void WchEnterBootloader(void);
void WchReset(void);
uint8_t WchFirmwareUpdate(unsigned long);
#endif

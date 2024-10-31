#if SMARTEVSE_VERSION == 4
extern SPIClass QCA_SPI1;

uint16_t qcaspi_read_register16(uint16_t reg);
void qcaspi_write_register(uint16_t reg, uint16_t value);
void qcaspi_write_burst(uint8_t *src, uint32_t len);
uint32_t qcaspi_read_burst(uint8_t *dst);
#endif

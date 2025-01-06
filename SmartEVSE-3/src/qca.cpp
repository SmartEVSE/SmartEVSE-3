#if SMARTEVSE_VERSION == 4
#include <Arduino.h>
#include <SPI.h>
#include "esp32.h"
#include "qca.h"


uint16_t qcaspi_read_register16(uint16_t reg) {
    uint16_t tx_data;
    uint16_t rx_data;

    tx_data = QCA7K_SPI_READ | QCA7K_SPI_INTERNAL | reg;
    
    digitalWrite(PIN_QCA700X_CS, LOW);
    QCA_SPI1.transfer16(tx_data);                // send the command to read the internal register
    rx_data = QCA_SPI1.transfer16(0x0000);       // read the data on the bus
    digitalWrite(PIN_QCA700X_CS, HIGH);

    return rx_data;
}

void qcaspi_write_register(uint16_t reg, uint16_t value) {
    uint16_t tx_data;

    tx_data = QCA7K_SPI_WRITE | QCA7K_SPI_INTERNAL | reg;

    digitalWrite(PIN_QCA700X_CS, LOW);
    QCA_SPI1.transfer16(tx_data);                // send the command to write the internal register
    QCA_SPI1.transfer16(value);                  // write the value to the bus
    digitalWrite(PIN_QCA700X_CS, HIGH);

}

void qcaspi_write_burst(uint8_t *src, uint32_t len) {
    uint16_t total_len;
    uint8_t buf[10];

    buf[0] = 0xAA;
	buf[1] = 0xAA;
	buf[2] = 0xAA;
	buf[3] = 0xAA;
	buf[4] = (uint8_t)((len >> 0) & 0xFF);
	buf[5] = (uint8_t)((len >> 8) & 0xFF);
	buf[6] = 0;
	buf[7] = 0;

    total_len = len + 10;
    // Write nr of bytes to write to SPI_REG_BFR_SIZE
    qcaspi_write_register(SPI_REG_BFR_SIZE, total_len);
    //log_d("Write buffer bytes sent: %u\n", total_len);

  //  log_d("[TX] ");
  //  for(int x=0; x< len; x++) log_d("%02x ",src[x]);
  //  log_d("\n");
    
    digitalWrite(PIN_QCA700X_CS, LOW);
    QCA_SPI1.transfer16(QCA7K_SPI_WRITE | QCA7K_SPI_EXTERNAL);      // Write External
    QCA_SPI1.transfer(buf, 8);     // Header
    QCA_SPI1.transfer(src, len);   // Data
    QCA_SPI1.transfer16(0x5555);   // Footer
    digitalWrite(PIN_QCA700X_CS, HIGH);
}

uint32_t qcaspi_read_burst(uint8_t *dst) {
    uint16_t available;

    available = qcaspi_read_register16(SPI_REG_RDBUF_BYTE_AVA);

    if (available && available <= QCA7K_BUFFER_SIZE) {    // prevent buffer overflow
        // Write nr of bytes to read to SPI_REG_BFR_SIZE
        qcaspi_write_register(SPI_REG_BFR_SIZE, available);
        
        digitalWrite(PIN_QCA700X_CS, LOW);
        QCA_SPI1.transfer16(QCA7K_SPI_READ | QCA7K_SPI_EXTERNAL);
        QCA_SPI1.transfer(dst, available);
        digitalWrite(PIN_QCA700X_CS, HIGH);

        return available;   // return nr of bytes in the rxbuffer
    }
    return 0;
}
#endif

// https://github.com/htmltiger/esp32-ds18b20
#pragma once

#include <Arduino.h>
#include "driver/rmt.h"
#include "soc/gpio_periph.h"

#define OW_DURATION_RESET   480
#define OW_DURATION_SLOT    75
#define OW_DURATION_1_LOW   6
#define OW_DURATION_1_HIGH  (OW_DURATION_SLOT - OW_DURATION_1_LOW)
#define OW_DURATION_0_LOW   65
#define OW_DURATION_0_HIGH  (OW_DURATION_SLOT - OW_DURATION_0_LOW)
#define OW_DURATION_SAMPLE  (15 - 2)
#define OW_DURATION_RX_IDLE (OW_DURATION_SLOT + 2)

#define OWR_OK		0
#define OWR_CRC		1
#define OWR_BAD_DATA	2
#define OWR_TIMEOUT	3
#define OWR_DRIVER	4



class OneWire32 {
	private:
		gpio_num_t owpin;
		rmt_channel_t owtx;
		rmt_channel_t owrx;
		RingbufHandle_t owbuf;
		uint8_t power_default;
		uint8_t drvtx = 0;
		uint8_t drvrx = 0;
		void flush();
	public:
	    OneWire32(uint8_t pin, uint8_t tx = 0, uint8_t rx = 1, uint8_t parasite = 0);
		~OneWire32();
		bool reset();
		void request();
		uint8_t readRom(uint8_t data[8]);
		uint8_t getTemp(uint64_t &addr, float &temp);
		uint8_t search(uint64_t addresses[], uint8_t total);
		bool read(uint8_t &data, uint8_t len = 8);
		bool write(const uint8_t data, uint8_t len = 8);
};

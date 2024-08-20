/*
https://github.com/junkfix/esp32-ds18b20

Some of the code was taken from
https://github.com/DavidAntliff/esp32-owb/blob/master/owb_rmt.c

Created by Chris Morgan based on the nodemcu project driver. Copyright 2017 Chris Morgan <chmorgan@gmail.com>
Ported to ESP32 RMT peripheral for low-level signal generation by Arnim Laeuger.

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <Arduino.h>
#include "OneWireESP32.h"


OneWire32::OneWire32(uint8_t pin, uint8_t tx, uint8_t rx, uint8_t parasite){
	owpin = static_cast<gpio_num_t>(pin);
	owtx = static_cast<rmt_channel_t>(tx);
	owrx = static_cast<rmt_channel_t>(rx);
	power_default = parasite;	
	rmt_config_t rmt_tx = {
		.rmt_mode           = RMT_MODE_TX,
		.channel            = owtx,
		.gpio_num           = owpin,
		.clk_div            = 80,
		.mem_block_num      = 1,
		.flags 				= 0,
		.tx_config          = {
			.carrier_freq_hz = 38000,
			.carrier_level  = RMT_CARRIER_LEVEL_HIGH,
			.idle_level     = RMT_IDLE_LEVEL_HIGH,
			.carrier_duty_percent = 33,
			.carrier_en     = false,
			.loop_en        = false,
			.idle_output_en = true,
		}
	};
	if(rmt_config(&rmt_tx) == ESP_OK){
		if(rmt_driver_install(owtx, 0, ESP_INTR_FLAG_LOWMED | ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_SHARED) == ESP_OK){
			drvtx = 1;
			rmt_set_source_clk(owtx, RMT_BASECLK_APB);
			rmt_config_t rmt_rx = {
				.rmt_mode                = RMT_MODE_RX,
				.channel                 = owrx,
				.gpio_num                = owpin,
				.clk_div                 = 80,
				.mem_block_num           = 1,
				.flags 					 = 0, 
				.rx_config               = {
					.idle_threshold      = OW_DURATION_RX_IDLE,
					.filter_ticks_thresh = 30,
					.filter_en           = true,
				}
			};
			if(rmt_config(&rmt_rx) == ESP_OK){
				if(rmt_driver_install(owrx, 512, ESP_INTR_FLAG_LOWMED | ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_SHARED) == ESP_OK){
					drvrx = 1;
					rmt_set_source_clk(owrx, RMT_BASECLK_APB);
					rmt_get_ringbuf_handle(owrx, &owbuf);
					#if !ESP32
						#error ESP8266 not supported
					#elif ESP32 == 4 || ESP32 == 5 || ESP32 == 6
						//ESP32C3, ESP32C6, ESP32H2
						GPIO.enable_w1ts.val = (0x1 << owpin);
					#else
						if(owpin < 32){
							GPIO.enable_w1ts = (0x1 << owpin);
						}else{
							GPIO.enable1_w1ts.data = (0x1 << (owpin - 32));
						}
					#endif
					rmt_set_gpio(owrx, RMT_MODE_RX, owpin, false);
					rmt_set_gpio(owtx, RMT_MODE_TX, owpin, false);
					PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[owpin]);
					GPIO.pin[owpin].pad_driver = 1;				
					return;// true;
				}else{
					return;// false;
				}
			}else{
				return;// false;
			}
			drvtx = 0;
			rmt_driver_uninstall(owtx);
		}
	}
	return;// false;
}// begin


OneWire32::~OneWire32(){
	if(drvrx){
		if(drvrx == 2){rmt_rx_stop(owrx);}
		rmt_driver_uninstall(owrx);
		drvrx = 0;
	}
	if(drvtx){
		if(drvtx == 2){rmt_tx_stop(owtx);}
		rmt_driver_uninstall(owtx);
		drvtx = 0;
	}
}// end



bool OneWire32::reset(){
	rmt_item32_t tx_items[1];
	bool found = false;
	GPIO.pin[owpin].pad_driver = 1;
	tx_items[0].duration0 = OW_DURATION_RESET;
	tx_items[0].level0 = 0;
	tx_items[0].duration1 = 0;
	tx_items[0].level1 = 1;
	uint16_t old_rx_thresh;
	rmt_get_rx_idle_thresh(owrx, &old_rx_thresh);
	rmt_set_rx_idle_thresh(owrx, OW_DURATION_RESET + 60);
	flush();
	rmt_rx_start(owrx, true); drvrx = 2;
	if(rmt_write_items(owtx, tx_items, 1, true) == ESP_OK){
		size_t rx_size;
		rmt_item32_t *rx_items = (rmt_item32_t *)xRingbufferReceive(owbuf, &rx_size, 100 / portTICK_PERIOD_MS);
		if(rx_items){
			if(rx_size >= 1 * sizeof(rmt_item32_t)
			   && (rx_items[0].level0 == 0)
			   && (rx_items[0].duration0 >= OW_DURATION_RESET - 2)
			   && (rx_items[0].level1 == 1) && (rx_items[0].duration1 > 0)
			   && rx_items[1].level0 == 0){
				found = true;
			}
			vRingbufferReturnItem(owbuf, (void *)rx_items);
		}
	}
	rmt_rx_stop(owrx); drvrx = 1;
	rmt_set_rx_idle_thresh(owrx, old_rx_thresh);
	return found;
}// reset



void OneWire32::flush(){
	void *p;
	size_t s;
	while((p = xRingbufferReceive(owbuf, &s, 0))){
		vRingbufferReturnItem(owbuf, p);
	}
}


bool OneWire32::read(uint8_t &data, uint8_t len){
	rmt_item32_t tx_items[len + 1];
	uint8_t read_data = 0;
	int ret = true;
	GPIO.pin[owpin].pad_driver = 1;
	for(int i = 0; i < len; i++){
		tx_items[i].level0 = 0;
		tx_items[i].duration0 = OW_DURATION_1_LOW;
		tx_items[i].level1 = 1;
		tx_items[i].duration1 = OW_DURATION_1_HIGH;
	}
	tx_items[len].level0 = 1;
	tx_items[len].duration0 = 0;
	flush();
	rmt_rx_start(owrx, true); drvrx = 2;
	if(rmt_write_items(owtx, tx_items, len + 1, true) == ESP_OK){
		size_t rx_size;
		rmt_item32_t *rx_items = (rmt_item32_t *)xRingbufferReceive(owbuf, &rx_size, 100 / portTICK_PERIOD_MS);
		if(rx_items){
			if(rx_size >= len * sizeof( rmt_item32_t)){
				for(int i = 0; i < len; i++){
					read_data >>= 1;
					if(rx_items[i].level1 == 1 && (rx_items[i].level0 == 0) && (rx_items[i].duration0 < OW_DURATION_SAMPLE)){
						read_data |= 0x80;
					}
				}
				read_data >>= 8 - len;
			}
			vRingbufferReturnItem(owbuf, (void *)rx_items);
		}else{
			ret = false;
		}
	}else{
		ret = false;
	}
	rmt_rx_stop(owrx); drvrx = 1;
	data = read_data;
	return ret;
}// read


bool OneWire32::write(const uint8_t data, uint8_t len){
	GPIO.pin[owpin].pad_driver = power_default? 0 : 1;
	rmt_item32_t tx_items[len + 1];
	for(int i = 0; i < len; i++){
		tx_items[i].level0 = 0;
		tx_items[i].level1 = 1;
		tx_items[i].duration0 = ((data >> i) & 0x01) ? OW_DURATION_1_LOW : OW_DURATION_0_LOW;
		tx_items[i].duration1 = ((data >> i) & 0x01) ? OW_DURATION_1_HIGH : OW_DURATION_0_HIGH;
	}
	tx_items[len].level0 = 1;
	tx_items[len].duration0 = 0;
	return (rmt_write_items(owtx, tx_items, len + 1, true) == ESP_OK);
}// write


void OneWire32::request(){
	if(drvrx && reset()){
		write(0xCC);
		write(0x44);
	}
}

const uint8_t crc_table[] = {0, 94, 188, 226, 97, 63, 221, 131, 194, 156, 126, 32, 163, 253, 31, 65, 157, 195, 33, 127, 252, 162, 64, 30, 95, 1, 227, 189, 62, 96, 130, 220, 35, 125, 159, 193, 66, 28, 254, 160, 225, 191, 93, 3, 128, 222, 60, 98, 190, 224, 2, 92, 223, 129, 99, 61, 124, 34, 192, 158, 29, 67, 161, 255, 70, 24, 250, 164, 39, 121, 155, 197, 132, 218, 56, 102, 229, 187, 89, 7, 219, 133, 103, 57, 186, 228, 6, 88, 25, 71, 165, 251, 120, 38, 196, 154, 101, 59, 217, 135, 4, 90, 184, 230, 167, 249, 27, 69, 198, 152, 122, 36, 248, 166, 68, 26, 153, 199, 37, 123, 58, 100, 134, 216, 91, 5, 231, 185, 140, 210, 48, 110, 237, 179, 81, 15, 78, 16, 242, 172, 47, 113, 147, 205, 17, 79, 173, 243, 112, 46, 204, 146, 211, 141, 111, 49, 178, 236, 14, 80, 175, 241, 19, 77, 206, 144, 114, 44, 109, 51, 209, 143, 12, 82, 176, 238, 50, 108, 142, 208, 83, 13, 239, 177, 240, 174, 76, 18, 145, 207, 45, 115, 202, 148, 118, 40, 171, 245, 23, 73, 8, 86, 180, 234, 105, 55, 213, 139, 87, 9, 235, 181, 54, 104, 138, 212, 149, 203, 41, 119, 244, 170, 72, 22, 233, 183, 85, 11, 136, 214, 52, 106, 43, 117, 151, 201, 74, 20, 246, 168, 116, 42, 200, 150, 21, 75, 169, 247, 182, 232, 10, 84, 215, 137, 107, 53};

uint8_t OneWire32::readRom(uint8_t data[8]) {
	if(!drvrx) return OWR_DRIVER;
	if(reset()) {
		write(0x33);	// Read ROM
		for(uint8_t i = 0; i < 8; i++){
			if(!read(data[i])) {
				data[i] = 0;
			}
		}
	} else return OWR_TIMEOUT;
	return OWR_OK;
}


uint8_t OneWire32::getTemp(uint64_t &addr, float &temp){
	uint8_t error = OWR_OK;
	if(!drvrx){return OWR_DRIVER;}
	if(reset()){	//connected
		write(0x55);
		uint8_t *a = (uint8_t *)&addr;
		for(uint8_t i = 0; i < 8; i++){
			write(a[i]);
		}
		write(0xBE);	// Read
		uint8_t data[9]; uint16_t zero = 0; uint8_t crc = 0;
		for(uint8_t j = 0; j < 9; j++){
			if(!read(data[j])){data[j] = 0;}
			zero += data[j];
			if(j < 8 ){crc = crc_table[crc ^ data[j]];}
		}
		if(zero != 0x8f7 && zero){
			if(data[8] == crc ){	//CRC OK
				int16_t t = (data[1] << 8) | data[0];
				temp = ((float)t / 16.0);
			}else{
				error = OWR_CRC;
			}
		}else{
			error = OWR_BAD_DATA;
		}
	}else{
		error = OWR_TIMEOUT;
	}
	return error;
} // getTemp


uint8_t OneWire32::search(uint64_t addresses[], uint8_t total) {
	uint8_t last_src;
	int8_t last_dev = -1;
	uint8_t found = 0;
	uint8_t loop = 1;
	if(!drvrx){return found;}
	reset();
	uint64_t addr = 0;
	while(loop && found < total){
		loop = 0;
		last_src = last_dev;
		if(!reset()){
			found = 0;
			break;
		}
		write(0xF0);
		for(uint8_t i = 0; i < 64; i += 1){
			uint8_t bitA, bitB; uint64_t m = 1ULL << i;
			if(!read(bitA, 1) || !read(bitB, 1) || (bitA && bitB)){
				addr = found = loop = 0;
				break;
			}else if(!bitA && !bitB){
				if(i == last_src){
					write(1, 1); addr |= m;
				}else{
					if((addr & m) == 0 || i > last_src){
						write(0, 1); loop = 1; addr &= ~m;
						last_dev = i;
					}else{
						write(1, 1);
					}
				}
			}else{
				if(bitA){
					write(1, 1); addr |= m;
				}else{
					write(0, 1); addr &= ~m;
				}
			}
		}
		if(addr){
			addresses[found] = addr;
			found++;
		}
	}
	return found;
} // search

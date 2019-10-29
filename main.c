#include <stm32l433xx.h>
#include <stdint.h>
#include "gpio.h"
#include "usb.h"

int main() {
  gpio_init();
  usb_init();
  char buffer[64];
  while(1) {
  	usb_main_loop();
  	if(usb_ready()) {
      if(usb_tx_ready(0x82)) {
  			usb_write_dbl(0x82, buffer, 64);
      }
      if(usb_rx_ready(0x01)) {
        usb_read_dbl(1, buffer);
      }
		}
	}
}

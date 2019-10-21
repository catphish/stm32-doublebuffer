#include <stm32l433xx.h>
#include <stdint.h>
#include "gpio.h"
#include "usb.h"

int main() {
  gpio_init();
  usb_init();

  char data[64];
  usb_write_dbl(0x82, data, 64);
  while(1) {
  	usb_write_dbl(0x82, data, 64);
  }
}

struct USBBufTable {
  struct USBBufDesc {
    uint16_t txBufferAddr ;
    uint16_t txBufferCount ;
    uint16_t rxBufferAddr ;
    uint16_t rxBufferCount ;
  }
  ep_desc[8];
};

#define USBBUFTABLE ((volatile struct USBBufTable *)0x40006C00)
#define USBBUFRAW ((volatile uint8_t *)0x40006C00)
#define USB_EPR(n) (*(volatile uint16_t *)(USB_BASE + 4 * n))

void usb_init();
void usb_main_loop();
void usb_write_dbl(uint8_t ep, volatile char * buffer, uint32_t len);
uint8_t usb_tx_ready(uint8_t ep);
uint8_t usb_ready();
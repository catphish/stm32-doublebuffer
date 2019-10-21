#include <stm32l433xx.h>
#include <stdint.h>
#include "util.h"
#include "usb_private.h"
#include "usb.h"
#include "gpio.h"

uint8_t pending_addr = 0;

void usb_init() {
  gpio_port_mode(GPIOA, 11, 2, 10, 0, 0); // A11 AF10
  gpio_port_mode(GPIOA, 12, 2, 10, 0, 0); // A12 AF10

  // Enable Power control clock
  RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN;
  // Enable USB clock
  RCC->APB1ENR1 |= RCC_APB1ENR1_USBFSEN;
  // CRS Clock eneble
  RCC->APB1ENR1 |= RCC_APB1ENR1_CRSEN;

  // Enable HSI48
  RCC->CRRCR |= RCC_CRRCR_HSI48ON;
  while ((RCC->CRRCR & RCC_CRRCR_HSI48RDY) != RCC_CRRCR_HSI48RDY);

  // Enable USB Power
  PWR->CR2 |= (1<<10);
  usleep(10);

  // Enable USB
  USB->CNTR &= ~USB_CNTR_PDWN;
  usleep(10);
  USB->CNTR &= ~USB_CNTR_FRES;
  usleep(10);
  USB->ISTR = 0;
  USB->CNTR |= USB_CNTR_RESETM;
  USB->BCDR |= USB_BCDR_DPPU;

  NVIC->ISER[2] |= (1 << (USB_IRQn - 64));
}

uint32_t buffer_pointer;
// Types: 0=Bulk,1=Control,2=Iso,3=Interrupt, 0x10=Bulk(dbl_buf)
void usb_configure_ep(uint8_t ep, uint32_t type) {
  uint8_t in = ep & 0x80;
  ep &= 0x7f;

  int dblbuf = (type >> 4) & 1;
  uint32_t old_epr = USB_EPR(ep);
  uint32_t new_epr = 0; // Always write 1 to bits 15 and 8 for no effect.
  new_epr |= ((type & 3) << 9); // Set type
  new_epr |= (dblbuf << 8);     // Set kind
  new_epr |= ep;                // Set endpoint number

  if(in || ep == 0) {
    USBBUFTABLE->ep_desc[ep].txBufferAddr = buffer_pointer;
    buffer_pointer += 64;
    if(dblbuf) {
      // Use RX buffer as additional TX buffer
      USBBUFTABLE->ep_desc[ep].rxBufferAddr = buffer_pointer;
      buffer_pointer += 64;
      new_epr |= (old_epr & 0x4040); // DTOG=0, SW_BUF=0
    }
    new_epr |= (old_epr & 0x0030) ^ 0x0020; // NAK
  }

  if(!in) {
    USBBUFTABLE->ep_desc[ep].rxBufferAddr = buffer_pointer;
    USBBUFTABLE->ep_desc[ep].rxBufferCount = (1<<15) | (1 << 10);
    buffer_pointer += 64;

    if(dblbuf) {
      // Use TX buffer as additional RX buffer
      USBBUFTABLE->ep_desc[ep].txBufferAddr = buffer_pointer;
      USBBUFTABLE->ep_desc[ep].txBufferCount = (1<<15) | (1 << 10);
      buffer_pointer += 64;
      new_epr |= (old_epr & 0x4040); // DTOG=0, SW_BUF=0
    }

    new_epr |= (old_epr & 0x3000) ^ 0x3000; // Ready
  }

  USB_EPR(ep) = new_epr;
}
uint32_t ep_tx_ready(uint32_t ep) {
  ep &= 0x7f;
  return((USB_EPR(ep) & 0x30) == 0x20);
}

uint32_t ep_rx_ready(uint32_t ep) {
  ep &= 0x7f;
  return((USB_EPR(ep) & 0x3000) == 0x2000);
}

void usb_read(uint8_t ep, volatile char * buffer) {
  ep &= 0x7f;
  while(!ep_rx_ready(ep));
  uint32_t rxBufferAddr = USBBUFTABLE->ep_desc[ep].rxBufferAddr;
  uint32_t len = USBBUFTABLE->ep_desc[ep].rxBufferCount & 0x03ff;
  for(int n=0; n<len; n+=2) {
    *(uint16_t *)(buffer + n) = *(uint16_t *)(USBBUFRAW+rxBufferAddr+n);
  }
  // Clear CTR_RX and toggle NAK->VALID
  USB_EPR(ep) = (USB_EPR(ep) & 0x378f) ^ 0x3000;
}

void usb_read_dbl(uint8_t ep, volatile char * buffer) {
  ep &= 0x7f;

  uint32_t len, rxBufferAddr;

  if(USB_EPR(ep) & 0x40) {
    rxBufferAddr = USBBUFTABLE->ep_desc[ep].rxBufferAddr;
    len = USBBUFTABLE->ep_desc[ep].rxBufferCount & 0xff;
  } else {
    rxBufferAddr = USBBUFTABLE->ep_desc[ep].txBufferAddr;
    len = USBBUFTABLE->ep_desc[ep].txBufferCount & 0xff;
  }

  for(int n=0; n<len; n+=2) {
    *(uint16_t *)(buffer + n) = *(uint16_t *)(USBBUFRAW+rxBufferAddr+n);
  }
  // Toggle SW_BUF
  USB_EPR(ep) = (USB_EPR(ep) & 0x878f) | 0x40;
}

void usb_write(uint8_t ep, volatile char * buffer, uint32_t len) {
  ep &= 0x7f;
  while(!ep_tx_ready(ep));
  uint32_t txBufferAddr = USBBUFTABLE->ep_desc[ep].txBufferAddr;
  for(int n=0; n<len; n+=2) {
    *(volatile uint16_t *)(USBBUFRAW+txBufferAddr+n) = *(uint16_t *)(buffer + n);
  }
  USBBUFTABLE->ep_desc[ep].txBufferCount = len;

  // Toggle NAK->VALID
  USB_EPR(ep) = (USB_EPR(ep) & 0x87bf) ^ 0x30;
}

void usb_write_dbl(uint8_t ep, volatile char * buffer, uint32_t len) {
  ep &= 0x7f;

  while((USB_EPR(ep) & 0x30) == 0x30 && (((USB_EPR(ep) & 0x4000) >> 8) == (USB_EPR(ep) & 0x40)));
  uint32_t txBufferAddr;

  if(USB_EPR(ep) & 0x4000) {
    txBufferAddr = USBBUFTABLE->ep_desc[ep].rxBufferAddr;
  } else {
    txBufferAddr = USBBUFTABLE->ep_desc[ep].txBufferAddr;
  }

  for(int n=0; n<len; n+=2) {
    *(volatile uint16_t *)(USBBUFRAW+txBufferAddr+n) = *(uint16_t *)(buffer + n);
  }

  if(USB_EPR(ep) & 0x4000) {
    USBBUFTABLE->ep_desc[ep].rxBufferCount = len;
  } else {
    USBBUFTABLE->ep_desc[ep].txBufferCount = len;
  }

  // Toggle SW_BUF
  USB_EPR(ep) = (USB_EPR(ep) & 0x87bf) ^ 0x30;
  USB_EPR(ep) = (USB_EPR(ep) & 0x878f) | 0x4000;
}

void usb_reset() {
  USB->ISTR &= ~USB_ISTR_RESET;
  buffer_pointer = 64;

  usb_configure_ep(0, 1);
  usb_configure_ep(0x01, 0x10);
  usb_configure_ep(0x82, 0x10);

  USB->BTABLE = 0;

  // Enable the peripheral
  USB->DADDR = USB_DADDR_EF;
}

void handle_ep0() {
  char packet[64];
  usb_read(0, packet);

  uint8_t bmRequestType = packet[0];
  uint8_t bRequest      = packet[1];

  // Descriptor request
  if(bmRequestType == 0x80 && bRequest == 0x06) {
    uint8_t descriptor_type = packet[3];
    uint8_t descriptor_idx  = packet[2];

    if(descriptor_type == 1 && descriptor_idx == 0) {
      uint32_t len = 18;
      if(len > packet[6]) len = packet[6];
      usb_write(0, device_descriptor, len);
    }
    if(descriptor_type == 2 && descriptor_idx == 0) {
      uint32_t len = config_descriptor[2];
      if(len > packet[6]) len = packet[6];
      usb_write(0, config_descriptor, len);
    }
  }
  // Set address
  if(bmRequestType == 0x00 && bRequest == 0x05) {
    pending_addr = packet[2];
    usb_write(0,0,0);
  }
  // Set configuration
  if(bmRequestType == 0x00 && bRequest == 0x09) {
    usb_write(0,0,0);
  }

}

void USB_IRQHandler() {
  int istr = USB->ISTR;

  if(istr & USB_ISTR_RESET) {
    usb_reset();
  }

  if(istr & USB_ISTR_CTR) {
    uint32_t ep = istr & 0xf;
    if(istr & 0x10) {
      // RX
      if(ep == 0) {
        handle_ep0();
      } else if (ep == 1){
        char packet[64];
        usb_read_dbl(ep, packet);
      }
      USB_EPR(ep) &= 0x078f;
    } else {
      // TX
      if(pending_addr) {
        USB->DADDR = USB_DADDR_EF | pending_addr;
        pending_addr = 0;
      }
      USB_EPR(ep) &= 0x870f;
    }
  }
}

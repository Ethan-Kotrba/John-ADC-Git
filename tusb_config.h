#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#define CFG_TUD_ENABLED 1          // Enable USB device mode
#define CFG_TUD_CDC 1             // Enable CDC class
#define CFG_TUD_CDC_RX_BUFSIZE 1024
#define CFG_TUD_CDC_TX_BUFSIZE 1024
#define CFG_TUSB_RHPORT0_MODE OPT_MODE_DEVICE  // Configure Pico as USB device

#endif
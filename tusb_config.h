// tusb_config.h
#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#include "tusb.h"

// Enable USB device mode
#define CFG_TUD_ENABLED 1
// Enable one CDC instance
#define CFG_TUD_CDC 1
#define CFG_TUD_CDC_RX_BUFSIZE 1024
#define CFG_TUD_CDC_TX_BUFSIZE 1024
// Configure Pico as USB device
#define CFG_TUSB_RHPORT0_MODE OPT_MODE_DEVICE
// Use bare-metal (no OS)
#define CFG_TUSB_OS OPT_OS_NONE
// Memory settings
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))

#endif
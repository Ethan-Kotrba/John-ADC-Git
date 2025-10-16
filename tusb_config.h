#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

// MCU and OS configuration
#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU OPT_MCU_RP2040
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_PICO
#endif

// Enable Device stack
#ifndef CFG_TUD_ENABLED
#define CFG_TUD_ENABLED 1
#endif

// Enable vendor-specific class
#ifndef CFG_TUD_VENDOR
#define CFG_TUD_VENDOR 1
#endif

// Vendor interface configuration
#define CFG_TUD_VENDOR_EP_COUNT 1 // One endpoint pair (IN only)
#define CFG_TUD_VENDOR_EP_SIZE 512 // Bulk endpoint size

// USB buffer sizes
#define CFG_TUD_ENDPOINT0_SIZE 64 // Control endpoint size
#define CFG_TUD_VENDOR_RX_BUFSIZE 512
#define CFG_TUD_VENDOR_TX_BUFSIZE 512

// Debug level
#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG 0
#endif

// Memory alignment
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#endif

// Include Pico-specific utilities
#include "pico/util/queue.h"

#endif /* TUSB_CONFIG_H */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "tusb_config.h"
#include "tusb.h"

// Pin definitions
#define SPI_INST spi0
#define PIN_MISO 16
#define PIN_MOSI 19
#define PIN_SCLK 18
#define PIN_CS1  17
#define PIN_CS2  20

// Buffer size
#define BUFFER_SIZE 1024
uint16_t buffer[BUFFER_SIZE * 2];
uint32_t buffer_index = 0;

// DMA channels
int tx_dma_chan;
int rx_dma_chan;

// SPI command for MCP3008
uint8_t tx_cmd[3] = {0x01, 0x80, 0x00};
uint8_t rx_data[3];

// TinyUSB Device Descriptor
tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_CDC,
    .bDeviceSubClass    = 2,
    .bDeviceProtocol    = 0,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x1234,
    .idProduct          = 0x5678,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

// TinyUSB callbacks
void tud_mount_cb(void) {}
void tud_umount_cb(void) {}
void tud_suspend_cb(bool remote_wakeup_en) {}
void tud_resume_cb(void) {}

// USB Configuration Descriptor
uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 2, 0, 64, 0, 100),
    TUD_CDC_DESCRIPTOR(0, 4, 0x81, 8, 0x02, 0x82, 64),
};

// Descriptor callbacks
uint8_t const * tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

uint8_t const * tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

static char const* string_desc_arr[] = {
    (const char[]){0x09, 0x04},  // Language ID: US English
    "Example Manufacturer",
    "Pico ADC Device",
    "123456789",
    "CDC Interface",
};

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    static uint16_t desc_str[32];
    uint8_t len;

    if (index == 0) {
        desc_str[1] = 0x0409;  // US English (2 bytes)
        desc_str[0] = (TUSB_DESC_STRING << 8) | 4;  // Length: 4 bytes (2 + 2)
        return desc_str;
    }

    const char* str = string_desc_arr[index];
    len = strlen(str);
    if (len > 31) len = 31;

    for (uint8_t i = 0; i < len; i++) {
        desc_str[i + 1] = (uint16_t)(str[i]);
    }
    desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * len + 2);

    return desc_str;
}

// Read ADC sample via DMA
uint16_t read_adc_dma(uint cs_pin) {
    gpio_put(cs_pin, 0);

    dma_channel_config tx_conf = dma_channel_get_default_config(tx_dma_chan);
    channel_config_set_dreq(&tx_conf, spi_get_dreq(SPI_INST, true));
    channel_config_set_transfer_data_size(&tx_conf, DMA_SIZE_8);
    channel_config_set_read_increment(&tx_conf, true);
    channel_config_set_write_increment(&tx_conf, false);
    dma_channel_configure(tx_dma_chan, &tx_conf, &spi_get_hw(SPI_INST)->dr, tx_cmd, 3, false);

    dma_channel_config rx_conf = dma_channel_get_default_config(rx_dma_chan);
    channel_config_set_dreq(&rx_conf, spi_get_dreq(SPI_INST, false));
    channel_config_set_transfer_data_size(&rx_conf, DMA_SIZE_8);
    channel_config_set_read_increment(&rx_conf, false);
    channel_config_set_write_increment(&rx_conf, true);
    dma_channel_configure(rx_dma_chan, &rx_conf, rx_data, &spi_get_hw(SPI_INST)->dr, 3, false);

    dma_start_channel_mask((1u << rx_dma_chan) | (1u << tx_dma_chan));
    dma_channel_wait_for_finish_blocking(rx_dma_chan);

    gpio_put(cs_pin, 1);
    return ((rx_data[1] & 0x03) << 8) | rx_data[2];
}

int main() {
    stdio_init_all();
    tusb_init();

    while (!tud_cdc_connected()) {
        tud_task();
    }

    spi_init(SPI_INST, 10000000);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCLK, GPIO_FUNC_SPI);

    gpio_init(PIN_CS1);
    gpio_set_dir(PIN_CS1, GPIO_OUT);
    gpio_put(PIN_CS1, 1);
    gpio_init(PIN_CS2);
    gpio_set_dir(PIN_CS2, GPIO_OUT);
    gpio_put(PIN_CS2, 1);

    tx_dma_chan = dma_claim_unused_channel(true);
    rx_dma_chan = dma_claim_unused_channel(true);

    while (true) {
        tud_task();

        uint16_t sample1 = read_adc_dma(PIN_CS1);
        uint16_t sample2 = read_adc_dma(PIN_CS2);

        buffer[buffer_index++] = sample1;
        buffer[buffer_index++] = sample2;

        if (buffer_index >= BUFFER_SIZE * 2) {
            uint32_t bytes_to_send = BUFFER_SIZE * 2 * sizeof(uint16_t);
            const uint8_t* data_ptr = (const uint8_t*)buffer;
            uint32_t sent = 0;

            while (sent < bytes_to_send) {
                while (tud_cdc_write_available() == 0) {
                    tud_task();
                }

                uint32_t avail = tud_cdc_write_available();
                uint32_t chunk = (bytes_to_send - sent > avail) ? avail : (bytes_to_send - sent);
                uint32_t written = tud_cdc_write(data_ptr + sent, chunk);
                sent += written;

                tud_cdc_write_flush();
            }

            buffer_index = 0;
        }
    }

    return 0;
}









// #include <stdio.h>
// #include "pico/stdlib.h"
// #include "hardware/spi.h"
// #include "hardware/dma.h"
// #include "hardware/gpio.h"
// #include "tusb_config.h"
// #include "tusb.h"

// // Pin definitions (adjust as needed)
// #define SPI_INST spi0
// #define PIN_MISO 16
// #define PIN_MOSI 19
// #define PIN_SCLK 18
// #define PIN_CS1  17  // CS for sensor 1
// #define PIN_CS2  20  // CS for sensor 2

// // Buffer size (number of samples per sensor)
// #define BUFFER_SIZE 1024
// uint16_t buffer[BUFFER_SIZE * 2];  // Interleaved: sensor1[0], sensor2[0], sensor1[1], ...
// uint32_t buffer_index = 0;

// // DMA channels
// int tx_dma_chan;
// int rx_dma_chan;

// // SPI command for MCP3008 channel 0 (single-ended): start bit, single-ended, channel 0
// uint8_t tx_cmd[3] = {0x01, 0x80, 0x00};  // Adjust if your ADC differs
// uint8_t rx_data[3];

// // Function to read a single sample from a sensor using DMA
// uint16_t read_adc_dma(uint cs_pin) {
//     // Set CS low
//     gpio_put(cs_pin, 0);

//     // Configure TX DMA: send command buffer to SPI TX FIFO
//     dma_channel_config tx_conf = dma_channel_get_default_config(tx_dma_chan);
//     channel_config_set_dreq(&tx_conf, spi_get_dreq(SPI_INST, true));  // TX DREQ
//     channel_config_set_transfer_data_size(&tx_conf, DMA_SIZE_8);
//     channel_config_set_read_increment(&tx_conf, true);
//     channel_config_set_write_increment(&tx_conf, false);
//     dma_channel_configure(tx_dma_chan, &tx_conf, &spi_get_hw(SPI_INST)->dr, tx_cmd, 3, false);

//     // Configure RX DMA: read from SPI RX FIFO to rx_data
//     dma_channel_config rx_conf = dma_channel_get_default_config(rx_dma_chan);
//     channel_config_set_dreq(&rx_conf, spi_get_dreq(SPI_INST, false));  // RX DREQ
//     channel_config_set_transfer_data_size(&rx_conf, DMA_SIZE_8);
//     channel_config_set_read_increment(&rx_conf, false);
//     channel_config_set_write_increment(&rx_conf, true);
//     dma_channel_configure(rx_dma_chan, &rx_conf, rx_data, &spi_get_hw(SPI_INST)->dr, 3, false);

//     // Start RX first (waits for data), then TX (generates clocks)
//     dma_start_channel_mask((1u << rx_dma_chan) | (1u << tx_dma_chan));

//     // Wait for RX to finish
//     dma_channel_wait_for_finish_blocking(rx_dma_chan);

//     // Set CS high
//     gpio_put(cs_pin, 1);

//     // Parse 10-bit ADC value (adjust for your ADC bit depth)
//     return ((rx_data[1] & 0x03) << 8) | rx_data[2];
// }

// int main() {
//     // Initialize stdio (for debugging via UART if needed)
//     stdio_init_all();

//     // Initialize TinyUSB
//     tusb_init();

//     // Wait for USB connection
//     while (!tud_cdc_connected()) {
//         tud_task();
//     }

//     // Initialize SPI at 10 MHz
//     spi_init(SPI_INST, 10000000);
//     gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
//     gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
//     gpio_set_function(PIN_SCLK, GPIO_FUNC_SPI);

//     // Initialize CS pins as GPIO (manual control)
//     gpio_init(PIN_CS1);
//     gpio_set_dir(PIN_CS1, GPIO_OUT);
//     gpio_put(PIN_CS1, 1);
//     gpio_init(PIN_CS2);
//     gpio_set_dir(PIN_CS2, GPIO_OUT);
//     gpio_put(PIN_CS2, 1);

//     // Claim DMA channels
//     tx_dma_chan = dma_claim_unused_channel(true);
//     rx_dma_chan = dma_claim_unused_channel(true);

//     while (true) {
//         // Run USB tasks
//         tud_task();

//         // Read samples from both sensors
//         uint16_t sample1 = read_adc_dma(PIN_CS1);
//         uint16_t sample2 = read_adc_dma(PIN_CS2);

//         // Add to buffer (interleaved)
//         buffer[buffer_index++] = sample1;
//         buffer[buffer_index++] = sample2;

//         // If buffer full, send via USB CDC
//         if (buffer_index >= BUFFER_SIZE * 2) {
//             uint32_t bytes_to_send = BUFFER_SIZE * 2 * sizeof(uint16_t);
//             const uint8_t* data_ptr = (const uint8_t*)buffer;
//             uint32_t sent = 0;

//             while (sent < bytes_to_send) {
//                 // Wait for space in USB TX buffer
//                 while (tud_cdc_write_available() == 0) {
//                     tud_task();
//                 }

//                 // Send as much as possible
//                 uint32_t avail = tud_cdc_write_available();
//                 uint32_t chunk = (bytes_to_send - sent > avail) ? avail : (bytes_to_send - sent);
//                 uint32_t written = tud_cdc_write(data_ptr + sent, chunk);
//                 sent += written;

//                 // Flush to send immediately
//                 tud_cdc_write_flush();
//             }

//             // Reset buffer index
//             buffer_index = 0;
//         }
//     }

//     return 0;
// }
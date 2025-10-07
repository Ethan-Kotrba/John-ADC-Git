#include <stdio.h>
#include <string.h>
#include <unistd.h>  // For write()
#include "pico/stdio_usb.h"  // Explicitly include for stdio_usb
#include "hardware/vreg.h"   // For VREG_VOLTAGE_1_20
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/multicore.h"

// Pin definitions
#define SPI_INST spi0
#define PIN_MISO 4
#define PIN_CS   5
#define PIN_SCK  6
#define PIN_MOSI 7
#define PIN_DRDY 3  // Data Ready pin (active low)
#define PIN_MCLK 8  // MCLK pin

// SPI baud rate (MCP3564 supports up to 20 MHz)
#define SPI_BAUD 20000000

// MCP3564 constants
#define DEVICE_ADDR 0b01
#define CMD_STATIC_READ(reg) ((DEVICE_ADDR << 6) | ((reg) << 2) | 0b01)
#define CMD_INC_WRITE(reg) ((DEVICE_ADDR << 6) | ((reg) << 2) | 0b10)

#define REG_LOCK     0xD
#define REG_CONFIG0  0x1
#define REG_CONFIG1  0x2
#define REG_CONFIG2  0x3
#define REG_CONFIG3  0x4
#define REG_SCAN     0x7
#define SAMPLE_SIZE 32


// Buffer for data transfer (4 bytes per sample, 8192 samples = 32KB)
#define BUF_SIZE (8192 * 4)
uint8_t data_buf[BUF_SIZE];
volatile uint32_t wr_idx = 0;
volatile uint32_t rd_idx = 0;
volatile uint32_t dropped_samples = 0;
spin_lock_t *buf_lock;

// DMA channels
int tx_dma;
int rx_dma;

// Send debug string with marker and length
void send_debug_string(const char *str) {
    uint8_t len = strlen(str);
    if (len > 255) len = 255;  // Cap at 255 bytes
    uint8_t header[2] = {0xFF, len};  // Marker + length
    write(1, header, 2);  // Send header
    write(1, str, len);   // Send string
    fflush(stdout);       // Immediate flush for debug
}

// Function to read MCP3564 registers
uint32_t read_reg(uint8_t reg, uint8_t num_bytes) {
    uint8_t cmd = CMD_STATIC_READ(reg);
    uint8_t rx_buf[3] = {0};
    uint8_t tx_buf[3] = {0xFF, 0xFF, 0xFF};

    gpio_put(PIN_CS, 0);
    spi_write_blocking(SPI_INST, &cmd, 1);
    spi_write_read_blocking(SPI_INST, tx_buf, rx_buf, num_bytes);
    gpio_put(PIN_CS, 1);

    uint32_t value = 0;
    for (uint8_t i = 0; i < num_bytes; i++) {
        value = (value << 8) | rx_buf[i];
    }
    return value;
}

// Function to write to MCP3564 registers
void write_reg(uint8_t reg, uint32_t value, uint8_t num_bytes) {
    uint8_t cmd = CMD_INC_WRITE(reg);
    uint8_t buf[3] = {(value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF};
    uint32_t readback;
    int retries = 5;
    char debug_str[64];
    do {
        gpio_put(PIN_CS, 0);
        spi_write_blocking(SPI_INST, &cmd, 1);
        spi_write_blocking(SPI_INST, buf + (3 - num_bytes), num_bytes);
        gpio_put(PIN_CS, 1);
        sleep_us(100);
        readback = read_reg(reg, num_bytes);
        if (readback != value) {
            snprintf(debug_str, sizeof(debug_str), "Write reg 0x%X failed: wrote 0x%X, read 0x%X\n", reg, value, readback);
            send_debug_string(debug_str);
        }
    } while (readback != value && --retries > 0);
    if (retries == 0) {
        snprintf(debug_str, sizeof(debug_str), "Failed to write reg 0x%X after retries\n", reg);
        send_debug_string(debug_str);
    }
}



// Verify ADC configuration
void verify_config() {
    char debug_str[64];
    snprintf(debug_str, sizeof(debug_str), "CONFIG0: 0x%02X (expected 0x63)\n", read_reg(REG_CONFIG0, 1));
    send_debug_string(debug_str);
    snprintf(debug_str, sizeof(debug_str), "CONFIG1: 0x%02X (expected 0x40)\n", read_reg(REG_CONFIG1, 1));
    send_debug_string(debug_str);
    snprintf(debug_str, sizeof(debug_str), "CONFIG2: 0x%02X (expected 0x88)\n", read_reg(REG_CONFIG2, 1));
    send_debug_string(debug_str);
    snprintf(debug_str, sizeof(debug_str), "CONFIG3: 0x%02X (expected 0xF0)\n", read_reg(REG_CONFIG3, 1));
    send_debug_string(debug_str);
    snprintf(debug_str, sizeof(debug_str), "SCAN: 0x%06X (expected 0x000003)\n", read_reg(REG_SCAN, 3));
    send_debug_string(debug_str);
}

// DRDY interrupt handler
void drdy_handler(uint gpio, uint32_t events) {
    static uint64_t last_time = 0;
    uint64_t now = time_us_64();
    static uint32_t count = 0;
    char debug_str[64];
    if (++count % 1000 == 0) {
        snprintf(debug_str, sizeof(debug_str), "DRDY interval: %llu us, freq: %.1f Hz\n", now - last_time, 1000000.0 / (now - last_time));
        send_debug_string(debug_str);
    }
    last_time = now;

    static uint8_t tx_buf[5] = {CMD_STATIC_READ(0x0), 0xFF, 0xFF, 0xFF, 0xFF};
    static uint8_t rx_buf[5];

    gpio_put(PIN_CS, 0);
    dma_channel_set_read_addr(tx_dma, tx_buf, false);
    dma_channel_set_write_addr(rx_dma, rx_buf, false);
    dma_channel_start(rx_dma);
    dma_channel_start(tx_dma);
    dma_channel_wait_for_finish_blocking(rx_dma);
    dma_channel_wait_for_finish_blocking(tx_dma);
    gpio_put(PIN_CS, 1);

    uint32_t rx_status = dma_hw->ch[rx_dma].ctrl_trig;
    if (rx_status & (DMA_CH0_CTRL_TRIG_READ_ERROR_BITS | DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS)) {
        snprintf(debug_str, sizeof(debug_str), "DMA RX error: status 0x%08X\n", rx_status);
        send_debug_string(debug_str);
        dma_hw->ch[rx_dma].ctrl_trig = rx_status & ~(DMA_CH0_CTRL_TRIG_READ_ERROR_BITS | DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS);
        return;
    }

    uint32_t next_wr = (wr_idx + 4) % BUF_SIZE;
    uint32_t irq_state = spin_lock_blocking(buf_lock);
    if (next_wr != rd_idx) {
        uint8_t ch_id = (rx_buf[1] >> 4) & 0x0F;
        if (ch_id == 0 || ch_id == 1) {
            if (count % 1000 == 0) {
                snprintf(debug_str, sizeof(debug_str), "CH%d, sample: 0x%02X%02X%02X%02X\n", ch_id, rx_buf[1], rx_buf[2], rx_buf[3], rx_buf[4]);
                send_debug_string(debug_str);
            }
            memcpy(&data_buf[wr_idx], &rx_buf[1], 4);
            wr_idx = next_wr;
        } else {
            snprintf(debug_str, sizeof(debug_str), "Invalid channel ID: %u\n", ch_id);
            send_debug_string(debug_str);
        }
    } else {
        dropped_samples++;
        if (count % 1000 == 0) {
            snprintf(debug_str, sizeof(debug_str), "Dropped %u samples\n", dropped_samples);
            send_debug_string(debug_str);
        }
    }
    spin_unlock(buf_lock, irq_state);
}

// Core 1: Dedicated to reading from ADC
void core1_main() {
    dma_channel_config tx_config = dma_channel_get_default_config(tx_dma);
    channel_config_set_transfer_data_size(&tx_config, DMA_SIZE_8);
    channel_config_set_dreq(&tx_config, spi_get_dreq(SPI_INST, true));
    channel_config_set_read_increment(&tx_config, true);
    channel_config_set_write_increment(&tx_config, false);
    dma_channel_configure(tx_dma, &tx_config, &spi_get_hw(SPI_INST)->dr, NULL, 5, false);

    dma_channel_config rx_config = dma_channel_get_default_config(rx_dma);
    channel_config_set_transfer_data_size(&rx_config, DMA_SIZE_8);
    channel_config_set_dreq(&rx_config, spi_get_dreq(SPI_INST, false));
    channel_config_set_read_increment(&rx_config, false);
    channel_config_set_write_increment(&rx_config, true);
    dma_channel_configure(rx_dma, &rx_config, NULL, &spi_get_hw(SPI_INST)->dr, 5, false);

    gpio_set_irq_enabled_with_callback(PIN_DRDY, GPIO_IRQ_EDGE_FALL, true, &drdy_handler);
    __wfi();
}

int main() {
    stdio_init_all();
    stdio_set_translate_crlf(&stdio_usb, false);  // Disable CRLF for binary
    // vreg_set_voltage(VREG_VOLTAGE_1_20);  // For overclocking
    // set_sys_clock_khz(250000, true);      // Overclock to 250 MHz

    if (spi_init(SPI_INST, SPI_BAUD) == 0) {
        send_debug_string("SPI initialization failed\n");
        while (true);
    }
    spi_set_format(SPI_INST, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);
    gpio_init(PIN_DRDY);
    gpio_set_dir(PIN_DRDY, GPIO_IN);
    gpio_pull_up(PIN_DRDY);
    gpio_init(PIN_MCLK);
    gpio_set_dir(PIN_MCLK, GPIO_IN);
    gpio_pull_up(PIN_MCLK);  // Stabilize MCLK

    sleep_ms(100);

    tx_dma = dma_claim_unused_channel(true);
    rx_dma = dma_claim_unused_channel(true);
    if (tx_dma < 0 || rx_dma < 0) {
        send_debug_string("Failed to allocate DMA channels\n");
        while (true);
    }

    buf_lock = spin_lock_init(spin_lock_claim_unused(true));

    write_reg(REG_LOCK, 0xA5, 1);
    
    write_reg(REG_CONFIG1, 0x00, 1);  // OSR=32
    write_reg(0x05, 0x06, 1);
    // write_reg(REG_CONFIG2, 0x88, 1);
    write_reg(REG_CONFIG3, 0xF0, 1);
    write_reg(REG_SCAN, 0x000003, 3);
    write_reg(REG_CONFIG0, 0xE3, 1);  // Internal clock
    verify_config();

    multicore_launch_core1(core1_main);

    absolute_time_t last_flush = get_absolute_time();
    while (true) {
        uint32_t avail = 0;
        {
            uint32_t irq_state = spin_lock_blocking(buf_lock);
            avail = (wr_idx >= rd_idx) ? (wr_idx - rd_idx) : (BUF_SIZE - rd_idx + wr_idx);
            spin_unlock(buf_lock, irq_state);
        }

        if (avail >= 1024) {
            uint8_t send_buf[1024];
            uint32_t to_send = (avail > 1024) ? 1024 : avail;
            to_send -= to_send % 4;

            uint32_t irq_state = spin_lock_blocking(buf_lock);
            if (rd_idx + to_send > BUF_SIZE) {
                uint32_t part1 = BUF_SIZE - rd_idx;
                memcpy(send_buf, &data_buf[rd_idx], part1);
                memcpy(send_buf + part1, data_buf, to_send - part1);
                rd_idx = to_send - part1;
            } else {
                memcpy(send_buf, &data_buf[rd_idx], to_send);
                rd_idx = (rd_idx + to_send) % BUF_SIZE;
            }
            spin_unlock(buf_lock, irq_state);

            ssize_t written = write(1, send_buf, to_send);
            if (written != to_send) {
                char debug_str[64];
                snprintf(debug_str, sizeof(debug_str), "USB write error: %zd of %u bytes written\n", written, to_send);
                send_debug_string(debug_str);
            }

            if (absolute_time_diff_us(last_flush, get_absolute_time()) >= 1000000) {
                fflush(stdout);
                last_flush = get_absolute_time();
            }
        } else {
            if (dropped_samples > 0) {
                char debug_str[64];
                snprintf(debug_str, sizeof(debug_str), "Dropped %u samples due to buffer overflow\n", dropped_samples);
                send_debug_string(debug_str);
                dropped_samples = 0;
            }
            sleep_ms(1);
        }
    }

    return 0;
}
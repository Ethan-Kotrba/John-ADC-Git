#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/multicore.h"

// Pin definitions (adjust as needed for your setup)
#define SPI_INST spi0
#define PIN_MISO 4
#define PIN_CS   5
#define PIN_SCK  6
#define PIN_MOSI 7
#define PIN_DRDY 3  // Data Ready pin (active low)

// SPI baud rate (MCP3564 supports up to 20 MHz)
#define SPI_BAUD 10000000

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

// Buffer for data transfer between cores (4 bytes per sample, 1024 samples = 4KB)
#define BUF_SIZE (1024 * 4)
uint8_t data_buf[BUF_SIZE];
volatile uint32_t wr_idx = 0;
volatile uint32_t rd_idx = 0;
spin_lock_t *buf_lock;

// DMA channels
int tx_dma;
int rx_dma;

// Function to write to MCP3564 registers (handles 1-3 byte registers)
void write_reg(uint8_t reg, uint32_t value, uint8_t num_bytes) {
    uint8_t cmd = CMD_INC_WRITE(reg);
    uint8_t buf[3] = {(value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF};

    gpio_put(PIN_CS, 0);
    sleep_us(1);
    spi_write_blocking(SPI_INST, &cmd, 1);
    spi_write_blocking(SPI_INST, buf + (3 - num_bytes), num_bytes);
    gpio_put(PIN_CS, 1);
}

// Core 1: Dedicated to reading from ADC using DMA for SPI transfers
void core1_main() {
    // Preconfigure DMA channels (done once)
    dma_channel_config tx_config = dma_channel_get_default_config(tx_dma);
    channel_config_set_transfer_data_size(&tx_config, DMA_SIZE_8);
    channel_config_set_dreq(&tx_config, spi_get_dreq(SPI_INST, true));  // TX DREQ
    channel_config_set_read_increment(&tx_config, true);
    channel_config_set_write_increment(&tx_config, false);

    dma_channel_config rx_config = dma_channel_get_default_config(rx_dma);
    channel_config_set_transfer_data_size(&rx_config, DMA_SIZE_8);
    channel_config_set_dreq(&rx_config, spi_get_dreq(SPI_INST, false));  // RX DREQ
    channel_config_set_read_increment(&rx_config, false);
    channel_config_set_write_increment(&rx_config, true);

    // TX buffer: command + 4 dummies (0xFF keeps SDO active)
    uint8_t tx_buf[5] = {CMD_STATIC_READ(0x0), 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t rx_buf[5];

    while (true) {
        // Poll for DRDY falling edge (active low) - tight loop for minimal latency
        while (gpio_get(PIN_DRDY) == 1);
        // No need for debounce, assume clean signal

        // Start SPI transaction
        gpio_put(PIN_CS, 0);

        // Configure and start DMA channels
        dma_channel_set_config(tx_dma, &tx_config, false);
        dma_channel_set_read_addr(tx_dma, tx_buf, false);
        dma_channel_set_write_addr(tx_dma, &spi_get_hw(SPI_INST)->dr, false);
        dma_channel_set_trans_count(tx_dma, 5, false);

        dma_channel_set_config(rx_dma, &rx_config, false);
        dma_channel_set_read_addr(rx_dma, &spi_get_hw(SPI_INST)->dr, false);
        dma_channel_set_write_addr(rx_dma, rx_buf, false);
        dma_channel_set_trans_count(rx_dma, 5, false);

        // Start RX first, then TX for simultaneous transfer
        dma_channel_start(rx_dma);
        dma_channel_start(tx_dma);

        // Wait for completion
        dma_channel_wait_for_finish_blocking(rx_dma);
        dma_channel_wait_for_finish_blocking(tx_dma);

        gpio_put(PIN_CS, 1);

        // Copy data (rx_buf[1..4]) to shared buffer if space available
        uint32_t next_wr = (wr_idx + 4) % BUF_SIZE;
        if (next_wr != rd_idx) {  // Buffer not full
            uint32_t irq_state = spin_lock_blocking(buf_lock);
            memcpy(&data_buf[wr_idx], &rx_buf[1], 4);
            wr_idx = next_wr;
            spin_unlock(buf_lock, irq_state);
        }  // Else drop sample (overflow)
    }
}

int main() {
    // Initialize stdio (USB CDC for output to laptop)
    stdio_init_all();

    // Initialize SPI
    spi_init(SPI_INST, SPI_BAUD);
    spi_set_format(SPI_INST, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    // Initialize CS and DRDY
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);
    gpio_init(PIN_DRDY);
    gpio_set_dir(PIN_DRDY, GPIO_IN);
    // gpio_pull_up(PIN_DRDY);  // Uncomment if needed based on hardware

    // Claim DMA channels
    tx_dma = dma_claim_unused_channel(true);
    rx_dma = dma_claim_unused_channel(true);

    // Claim spin lock for buffer synchronization
    buf_lock = spin_lock_init(spin_lock_claim_unused(true));

    // Configure MCP3564 for 2-channel SCAN (single-ended CH0 and CH1 vs AGND),
    // continuous conversion, OSR=32 for max data rate (~76.8 ksps per channel),
    // gain=1x, data format=32-bit with channel ID
    write_reg(REG_LOCK, 0xA5, 1);              // Unlock registers
    write_reg(REG_CONFIG0, 0x63, 1);           // Internal clock, ADC conversion mode
    write_reg(REG_CONFIG1, 0x00, 1);           // OSR=32, PRE=1 (AMCLK=MCLK)
    write_reg(REG_CONFIG2, 0x88, 1);           // Gain=1x, boost=x1
    write_reg(REG_CONFIG3, 0xF0, 1);           // Continuous conv, 32-bit w/ CH ID
    write_reg(REG_SCAN, 0x000003, 3);          // Scan CH0 and CH1 (single-ended)

    // Launch core 1 for ADC reading
    multicore_launch_core1(core1_main);

    // Core 0: Send buffered data to USB
    while (true) {
        uint32_t avail = 0;
        {
            uint32_t irq_state = spin_lock_blocking(buf_lock);
            avail = (wr_idx >= rd_idx) ? (wr_idx - rd_idx) : (BUF_SIZE - rd_idx + wr_idx);
            spin_unlock(buf_lock, irq_state);
        }

        if (avail >= 64) {  // Send in 64-byte chunks for efficiency
            uint8_t send_buf[64];
            uint32_t to_send = (avail > 64) ? 64 : avail;
            to_send -= to_send % 4;  // Align to sample size

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

            // Send over USB (binary data)
            fwrite(send_buf, 1, to_send, stdout);
            fflush(stdout);  // Flush to ensure timely delivery
        } else {
            sleep_ms(1);  // Yield if no data
        }
    }

    return 0;
}
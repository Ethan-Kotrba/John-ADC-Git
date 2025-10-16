#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "tusb.h" // Include TinyUSB header

// Pin definitions (unchanged)
#define SPI_INST spi0
#define PIN_MISO 4
#define PIN_CS   5
#define PIN_SCK  6
#define PIN_MOSI 7
#define PIN_DRDY 3

#define SPI_BAUD 10000000

#define DEVICE_ADDR 0b01
#define CMD_STATIC_READ(reg) ((DEVICE_ADDR << 6) | ((reg) << 2) | 0b01)
#define CMD_INC_WRITE(reg) ((DEVICE_ADDR << 6) | ((reg) << 2) | 0b10)

#define REG_LOCK     0xD
#define REG_CONFIG0  0x1
#define REG_CONFIG1  0x2
#define REG_CONFIG2  0x3
#define REG_CONFIG3  0x4
#define REG_SCAN     0x7
#define SAMPLE_SIZE 6
#define TIMESTAMP_SIZE 2

#define CLOCK_PIN 0
#define CLOCK_FREQ_HZ 18000000

#define BUF_SIZE (16384 * (SAMPLE_SIZE))
uint8_t data_buf[BUF_SIZE];
volatile uint32_t wr_idx = 0;
volatile uint32_t rd_idx = 0;
volatile uint32_t dropped_samples = 0;
spin_lock_t *buf_lock;

int tx_dma;
int rx_dma;

// Function to write to MCP3564 registers (handles 1-3 byte registers)
void write_reg(uint8_t reg, uint32_t value, uint8_t num_bytes) {
    uint8_t cmd = CMD_INC_WRITE(reg);
    uint8_t buf[3] = {(value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF};

    gpio_put(PIN_CS, 0);
    spi_write_blocking(SPI_INST, &cmd, 1);
    spi_write_blocking(SPI_INST, buf + (3 - num_bytes), num_bytes);
    gpio_put(PIN_CS, 1);
    sleep_us(10);
}

void Setup_PWM_Clock(void) {
    // Set up PWM
    gpio_set_function(CLOCK_PIN, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(CLOCK_PIN); // Get PWM slice for GP0

    // Calculate PWM divider and wrap value for desired frequency
    // System clock is typically 125 MHz
    // PWM frequency = sys_clk / (divider * wrap)
    // For 50% duty cycle, set level to wrap/2
    // For 1 MHz: wrap = 125, divider = 1 (125 MHz / (1 * 125) = 1 MHz)
    uint32_t sys_hz = clock_get_hz(clk_sys); // Typically 125 MHz
    uint16_t wrap = sys_hz / CLOCK_FREQ_HZ; // E.g., 125 MHz / 1 MHz = 125
    float div = 1.0f; // Start with divider = 1
    if (wrap > 65535) {
        // If wrap exceeds 16-bit limit, increase divider
        div = (float)sys_hz / (CLOCK_FREQ_HZ * 65535);
        wrap = 65535;
    }

    // Configure PWM
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, div);
    pwm_config_set_wrap(&config, wrap);
    pwm_init(slice_num, &config, true); // Start PWM

    // Set 50% duty cycle (square wave)
    pwm_set_gpio_level(CLOCK_PIN, wrap / 2);
}

// Function to read MCP3564 registers (handles 1-3 byte registers)
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

// Verify ADC configuration
void verify_config() {
    printf("Verifying MCP3564 configuration:\n");
    printf("CONFIG0: 0x%02X (expected 0x63)\n", read_reg(REG_CONFIG0, 1));
    printf("CONFIG1: 0x%02X (expected 0x00)\n", read_reg(REG_CONFIG1, 1));
    printf("CONFIG2: 0x%02X (expected 0x88)\n", read_reg(REG_CONFIG2, 1));
    printf("CONFIG3: 0x%02X (expected 0xF0)\n", read_reg(REG_CONFIG3, 1));
    printf("SCAN: 0x%06X (expected 0x000003)\n", read_reg(REG_SCAN, 3));
}

// DRDY interrupt handler
void drdy_handler(uint gpio, uint32_t events) {
    gpio_set_irq_enabled(PIN_DRDY, GPIO_IRQ_EDGE_FALL, false);
    static uint8_t tx_buf[5] = {CMD_STATIC_READ(0x0), 0xFF, 0xFF, 0xFF, 0xFF};
    static uint8_t rx_buf[5];

    // Capture 24-bit timestamp (truncate 64-bit microsecond time)
    uint32_t time = time_us_32(); //Timestamp in 32 bit
    uint16_t timestamp = time & 0xFFFF;


    // Start SPI transaction
    gpio_put(PIN_CS, 0);

    // Start DMA transfers
    dma_channel_set_read_addr(tx_dma, tx_buf, false);
    dma_channel_set_write_addr(rx_dma, rx_buf, false);
    dma_channel_start(rx_dma);
    dma_channel_start(tx_dma);

    // Wait for completion
    dma_channel_wait_for_finish_blocking(rx_dma);
    dma_channel_wait_for_finish_blocking(tx_dma);

    gpio_put(PIN_CS, 1);

    // Check for DMA errors (read/write error flags in ctrl_trig register)
    uint32_t rx_status = dma_hw->ch[rx_dma].ctrl_trig;
    if (rx_status & (DMA_CH0_CTRL_TRIG_READ_ERROR_BITS | DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS)) {
        // printf("DMA RX error: status 0x%08X\n", rx_status);
        // Clear error flags
        dma_hw->ch[rx_dma].ctrl_trig = rx_status & ~(DMA_CH0_CTRL_TRIG_READ_ERROR_BITS | DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS);
        gpio_set_irq_enabled(PIN_DRDY, GPIO_IRQ_EDGE_FALL, true);
        return;
    }

    // Validate channel ID and copy to shared buffer
    uint32_t next_wr = (wr_idx + (4+TIMESTAMP_SIZE)) % BUF_SIZE;
    uint32_t irq_state = spin_lock_blocking(buf_lock);
    if (next_wr != rd_idx) {
        uint8_t ch_id = (rx_buf[1] >> 4) & 0x0F;  // Channel ID in bits 7:4
        if (ch_id == 0 || ch_id == 1) {  // Validate CH0 or CH1
            memcpy(&data_buf[wr_idx], &rx_buf[1], 4);
            memcpy(&data_buf[wr_idx+4], &timestamp, TIMESTAMP_SIZE);
            // data_buf[wr_idx + 4] = (timestamp >> 24) & 0xFF;
            // data_buf[wr_idx + 5] = (timestamp >> 16) & 0xFF;
            // data_buf[wr_idx + 4] = (timestamp >> 8) & 0xFF;
            // data_buf[wr_idx + 5] = timestamp & 0xFF;
            wr_idx = next_wr;
        } else {
            // printf("Invalid channel ID: %u\n", ch_id);
        }
    } else {
        dropped_samples++;
        gpio_set_irq_enabled(PIN_DRDY, GPIO_IRQ_EDGE_FALL, false);
        return;
    }
    spin_unlock(buf_lock, irq_state);
    gpio_set_irq_enabled(PIN_DRDY, GPIO_IRQ_EDGE_FALL, true);
}

// Core 1: Dedicated to reading from ADC using DMA and interrupts
void core1_main() {
    // Configure DMA channels once
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

    // Enable DRDY interrupt (falling edge)
    gpio_set_irq_enabled_with_callback(PIN_DRDY, GPIO_IRQ_EDGE_FALL, true, &drdy_handler);

    // Keep core alive, interrupt handles DRDY
    __wfi();
}

void Setup_SPI(void) {
    // Initialize SPI
    if (spi_init(SPI_INST, SPI_BAUD) == 0) {
        // printf("SPI initialization failed\n");
        while (true);
    }
    // SPI Mode 0,0 (CPOL=0, CPHA=0) as per MCP3564 datasheet Section 6.2
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
    gpio_pull_up(PIN_DRDY);  // Uncomment if needed based on hardware

    // Allow ADC to stabilize after power-on (MCP3564 datasheet Section 6.3)
    sleep_ms(100);
}


void Configure_ADC(void) {
    //AMCLK = MCLK / Prescaler
    //DMCLK = AMCLK / 4
    //DRCLK = DMCLK / OSR
    //Conversation start low pulse = 1/DMCLK
    // DMCLK == Sampling Rate, DRCLK == Data output rate
    // DR_int hightime min == 16* 1/DMCLK
    // DR_int lowtime max == OSR-16

    // Configure MCP3564 for 2-channel SCAN (single-ended CH0 and CH1 vs AGND),
    // continuous conversion, OSR=32 for max data rate (~76.8 ksps per channel),
    // gain=1x, data format=32-bit with channel ID
    write_reg(REG_LOCK, 0xA5, 1);              // Unlock registers
    // CONFIG0: Internal 3.6864 MHz clock, ADC conversion mode (~76.8 ksps/channel, OSR=32)
    write_reg(REG_CONFIG0, 0x03, 1);           // Internal clock, ADC conversion mode
    write_reg(REG_CONFIG1, 0x00, 1);           // OSR=32, PRE=1 (AMCLK=MCLK)
    //Config2 was set to 0x88, but that messed with the reserved bits
    write_reg(REG_CONFIG2, 0xC0, 1);           // Gain=1x, boost=x1
    write_reg(REG_CONFIG3, 0xF0, 1);           // Continuous conv, 32-bit w/ CH ID
    write_reg(REG_SCAN, 0x000003, 3);          // Scan CH0 and CH1 (single-ended)

    // Verify ADC configuration
    verify_config();
}

int main() {
    // Initialize stdio (only for UART if needed, USB handled by TinyUSB)
    stdio_init_all(); // Keep for initialization, but USB output will be manual

    // Initialize TinyUSB
    tusb_init();

    // Setup PWM
    Setup_PWM_Clock();

    // Claim DMA channels
    tx_dma = dma_claim_unused_channel(true);
    rx_dma = dma_claim_unused_channel(true);
    if (tx_dma < 0 || rx_dma < 0) {
        while (true); // Error handling
    }

    // Setup SPI and ADC
    Setup_SPI();
    Configure_ADC();

    // Claim spin lock for buffer synchronization
    buf_lock = spin_lock_init(spin_lock_claim_unused(true));

    // Launch core 1 for ADC reading
    multicore_launch_core1(core1_main);

    while (1) {
        tud_task(); // TinyUSB task handling
        if (tud_cdc_n_connected(0)) { // Check CDC instance 0
            uint8_t buffer[384];
            uint32_t samples = 0;
            if (multicore_fifo_pop_blocking_inline(0, &samples)) {
                for (int i = 0; i < 64; i++) {
                    buffer[i * 6 + 0] = (samples >> 24) & 0xFF;
                    buffer[i * 6 + 1] = (samples >> 16) & 0xFF;
                    buffer[i * 6 + 2] = (samples >> 8) & 0xFF;
                    buffer[i * 6 + 3] = samples & 0xFF;
                    buffer[i * 6 + 4] = '\r';
                    buffer[i * 6 + 5] = '\n';
                }
                tud_cdc_n_write(0, buffer, 384);
                tud_cdc_n_write_flush(0);
            }
        }
    }

    // // Core 0: Send buffered data to USB using TinyUSB
    // while (true) {
    //     // Process TinyUSB tasks
    //     tud_task(); // Handle USB events

    //     // Check if USB is connected
    //     if (!tud_cdc_connected()) {
    //         sleep_ms(100); // Wait until USB is connected
    //         continue;
    //     }

    //     uint32_t avail = 0;
    //     {
    //         uint32_t irq_state = spin_lock_blocking(buf_lock);
    //         avail = (wr_idx >= rd_idx) ? (wr_idx - rd_idx) : (BUF_SIZE - rd_idx + wr_idx);
    //         spin_unlock(buf_lock, irq_state);
    //     }
    //     int16_t limit = (64 * (4 + TIMESTAMP_SIZE)); // 384 bytes
    //     if (avail >= limit) {
    //         uint8_t send_buf[limit];
    //         uint32_t to_send = limit;

    //         uint32_t irq_state = spin_lock_blocking(buf_lock);
    //         if (rd_idx + to_send > BUF_SIZE) {
    //             uint32_t part1 = BUF_SIZE - rd_idx;
    //             memcpy(send_buf, &data_buf[rd_idx], part1);
    //             memcpy(send_buf + part1, data_buf, to_send - part1);
    //             rd_idx = to_send - part1;
    //         } else {
    //             memcpy(send_buf, &data_buf[rd_idx], to_send);
    //             rd_idx = (rd_idx + to_send) % BUF_SIZE;
    //         }
    //         spin_unlock(buf_lock, irq_state);

    //         // Send over USB using TinyUSB CDC
    //         uint32_t written = tud_cdc_write(send_buf, to_send);
    //         if (written > 0) {
    //             tud_cdc_write_flush(); // Ensure data is sent
    //         }
    //         if (written != to_send) {
    //             // Handle partial write (buffer full or USB not ready)
    //             sleep_us(500); // Brief delay to avoid busy looping
    //         }
    //     } else {
    //         // Report dropped samples periodically
    //         if (dropped_samples > 0) {
    //             char msg[64];
    //             snprintf(msg, sizeof(msg), "Dropped %u samples due to buffer overflow\n", dropped_samples);
    //             if (tud_cdc_connected()) {
    //                 tud_cdc_write_str(msg);
    //                 tud_cdc_write_flush();
    //             }
    //             dropped_samples = 0;
    //         }
    //         sleep_us(500); // Yield if no data
    //     }
    // }

    return 0;
}
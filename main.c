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
#include "tusb.h"
#include "tusb_config.h"

#define USB_VID 0x2E8A
#define USB_PID 0x000A
#define SPI_INST spi0
#define PIN_MISO 4
#define PIN_CS   5
#define PIN_SCK  6
#define PIN_MOSI 7
#define PIN_DRDY 3
#define SPI_BAUD 10000000
#define CLOCK_PIN 0
#define CLOCK_FREQ_HZ 18000000
#define BUF_SIZE (16384 * 6)
#define USB_PACKET_SIZE 512

uint8_t data_buf[BUF_SIZE];
volatile uint32_t wr_idx = 0;
volatile uint32_t rd_idx = 0;
volatile uint32_t dropped_samples = 0;
spin_lock_t *buf_lock;
int tx_dma, rx_dma;
uint8_t usb_buf[USB_PACKET_SIZE];

void write_reg(uint8_t reg, uint32_t value, uint8_t num_bytes) {
    if (num_bytes > 3 || num_bytes == 0) {
        printf("Invalid num_bytes: %u\n", num_bytes);
        return;
    }
    uint8_t cmd = CMD_INC_WRITE(reg);
    uint8_t buf[3] = {(value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF};
    gpio_put(PIN_CS, 0);
    spi_write_blocking(SPI_INST, &cmd, 1);
    spi_write_blocking(SPI_INST, buf + (3 - num_bytes), num_bytes);
    gpio_put(PIN_CS, 1);
    sleep_us(10);
}

void drdy_handler(uint gpio, uint32_t events) {
    gpio_set_irq_enabled(PIN_DRDY, GPIO_IRQ_EDGE_FALL, false);
    static uint8_t tx_buf[5] = {CMD_STATIC_READ(0x0), 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t rx_buf[5] = {0};

    uint32_t time = time_us_32();
    uint16_t timestamp = time & 0xFFFF;

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
        dma_hw->ch[rx_dma].ctrl_trig = rx_status & ~(DMA_CH0_CTRL_TRIG_READ_ERROR_BITS | DMA_CH0_CTRL_TRIG_WRITE_ERROR_BITS);
        gpio_set_irq_enabled(PIN_DRDY, GPIO_IRQ_EDGE_FALL, true);
        return;
    }

    uint32_t next_wr = (wr_idx + (4 + TIMESTAMP_SIZE)) % BUF_SIZE;
    uint32_t irq_state = spin_lock_blocking(buf_lock);
    uint32_t space = (rd_idx > wr_idx) ? (rd_idx - wr_idx) : (BUF_SIZE - wr_idx + rd_idx);
    if (space >= (4 + TIMESTAMP_SIZE)) {
        uint8_t ch_id = (rx_buf[1] >> 4) & 0x0F;
        if (ch_id == 0 || ch_id == 1) {
            memcpy(&data_buf[wr_idx], &rx_buf[1], 4);
            memcpy(&data_buf[wr_idx + 4], &timestamp, TIMESTAMP_SIZE);
            wr_idx = next_wr;
        }
    } else {
        dropped_samples++;
    }
    spin_unlock(buf_lock, irq_state);
    gpio_set_irq_enabled(PIN_DRDY, GPIO_IRQ_EDGE_FALL, true);
}

int main() {
    stdio_uart_init(); // Enable UART for debugging
    set_sys_clock_khz(125000, true); // Ensure 125 MHz system clock
    tusb_init();
    Setup_PWM_Clock();
    Setup_SPI();
    Configure_ADC();
    sleep_ms(100); // Allow ADC to stabilize

    tx_dma = dma_claim_unused_channel(true);
    rx_dma = dma_claim_unused_channel(true);
    if (tx_dma < 0 || rx_dma < 0) {
        printf("Failed to claim DMA channels\n");
        while (true);
    }

    buf_lock = spin_lock_init(spin_lock_claim_unused(true));
    multicore_launch_core1(core1_main);

    printf("ADC system ready, waiting for data...\n");

    static uint32_t last_log = 0;
    while (true) {
        tud_task();
        if (!tud_mounted()) {
            sleep_ms(10);
            continue;
        }

        uint32_t now = time_us_32();
        if (now - last_log > 1000000) {
            uint32_t irq_state = spin_lock_blocking(buf_lock);
            uint32_t avail = (wr_idx >= rd_idx) ? (wr_idx - rd_idx) : (BUF_SIZE - rd_idx + wr_idx);
            printf("Buffer usage: %u/%u, Dropped: %u\n", avail, BUF_SIZE, dropped_samples);
            last_log = now;
            spin_unlock(buf_lock, irq_state);
        }

        uint32_t avail = (wr_idx >= rd_idx) ? (wr_idx - rd_idx) : (BUF_SIZE - rd_idx + wr_idx);
        if (avail >= USB_PACKET_SIZE) {
            uint32_t to_send = USB_PACKET_SIZE;
            uint32_t irq_state = spin_lock_blocking(buf_lock);
            if (rd_idx + to_send > BUF_SIZE) {
                uint32_t part1 = BUF_SIZE - rd_idx;
                memcpy(usb_buf, &data_buf[rd_idx], part1);
                memcpy(usb_buf + part1, data_buf, to_send - part1);
                rd_idx = (rd_idx + to_send) % BUF_SIZE;
            } else {
                memcpy(usb_buf, &data_buf[rd_idx], to_send);
                rd_idx = (rd_idx + to_send) % BUF_SIZE;
            }
            spin_unlock(buf_lock, irq_state);

            uint32_t sent = 0;
            absolute_time_t timeout = make_timeout_time_ms(100);
            while (sent < to_send && tud_mounted() && !absolute_time_diff_us(get_absolute_time(), timeout) < 0) {
                uint32_t chunk = tud_vendor_n_write(0, &usb_buf[sent], to_send - sent);
                if (chunk > 0) {
                    sent += chunk;
                }
                tud_task();
            }
            if (sent < to_send) {
                printf("USB transfer incomplete: sent %u of %u bytes\n", sent, to_send);
            }
        }
    }

    return 0;
}
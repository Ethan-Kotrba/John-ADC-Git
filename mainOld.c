#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

// Pin definitions
#define SPI_PORT spi0
#define PIN_SCK  2
#define PIN_MOSI 3
#define PIN_MISO 4
#define PIN_CS   5

// MCP3564 registers (see datasheet)
#define REG_ADCDATA  0x00
#define REG_CONFIG0  0x01
#define REG_CONFIG1  0x02
#define REG_CONFIG2  0x03
#define REG_CONFIG3  0x04

// Write to MCP3564 register
void mcp3564_write_reg(uint8_t reg, uint8_t data) {
    uint8_t buf[2] = {(uint8_t)((reg << 2) | 0x02), data}; // Write command
    gpio_put(PIN_CS, 0); // CS low
    spi_write_blocking(SPI_PORT, buf, 2);
    gpio_put(PIN_CS, 1); // CS high
}

// Read 24-bit ADC data
int32_t mcp3564_read_adc() {
    uint8_t cmd = REG_ADCDATA << 2; // Read ADC data command
    uint8_t data[3] = {0};
    gpio_put(PIN_CS, 0);
    spi_write_blocking(SPI_PORT, &cmd, 1);
    spi_read_blocking(SPI_PORT, 0, data, 3);
    gpio_put(PIN_CS, 1);
    // Combine 3 bytes into 24-bit signed integer
    int32_t value = ((int32_t)data[0] << 16) | (data[1] << 8) | data[2];
    if (value & 0x800000) { // Sign-extend if negative
        value |= 0xFF000000;
    }
    return value;
}

// Initialize MCP3564
void mcp3564_init() {
    mcp3564_write_reg(REG_CONFIG0, 0x03); // Enable ADC, VREF internal
    mcp3564_write_reg(REG_CONFIG1, 0x03); // OSR = 256, single-ended
    mcp3564_write_reg(REG_CONFIG2, 0x80); // Boost x1, gain x1
    mcp3564_write_reg(REG_CONFIG3, 0x80); // Continuous conversion
}

int main() {
    // Initialize stdio for USB serial
    stdio_usb_init();
    sleep_ms(2000); // Wait for USB connection

    // Initialize SPI
    spi_init(SPI_PORT, 1000000); // 1 MHz
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);

    // Initialize MCP3564
    mcp3564_init();

    while (true) {
        int32_t adc_value = mcp3564_read_adc();
        float voltage = (float)adc_value / 0x7FFFFF * 3.3; // Convert to voltage (3.3V VREF)
        printf("ADC Value: %ld, Voltage: %.6f V\n", adc_value, voltage);
        sleep_ms(100); // Sample every 100ms
    }

    return 0;
}
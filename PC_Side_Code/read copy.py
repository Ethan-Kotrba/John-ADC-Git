import serial
import struct
import time
import csv
import os

# Configuration
SERIAL_PORT = '/dev/ttyACM0'  # Change to '/dev/ttyACM0' or similar on Linux/macOS
BAUD_RATE = 5000000    # USB CDC doesn't strictly use baud, but set for compatibility
OUTPUT_FILE = 'adc_data.csv'
READ_TIMEOUT = 1      # Seconds
SAMPLE_SIZE = 8

# MCP3564 settings (match Pico code)
VREF = 3.3           # Reference voltage
RESOLUTION = 24      # 24-bit ADC
GAIN = 1             # Gain setting from CONFIG2

def open_serial_port():
    while True:
        try:
            ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=READ_TIMEOUT)
            time.sleep(2)  # Wait for USB CDC to initialize
            print(f"Connected to {SERIAL_PORT}")
            ser.reset_input_buffer()  # Clear buffer to align with sample boundaries
            return ser
        except serial.SerialException as e:
            print(f"Error opening serial port: {e}")
            time.sleep(1)  # Wait before retrying

def decode_sample(data):
    """Decode 7-byte sample: 4 bytes ADC (channel ID + value), 3 bytes timestamp."""
    if len(data) != SAMPLE_SIZE:
        return None, None, None
    channel_id = (data[0] >> 4) & 0x0F  # Top 4 bits of first byte
    if channel_id not in (0, 1):  # Validate channel ID
        return None, None, None
    # Extract 24-bit signed value (bytes 1–3, 2's complement)
    raw_value = int.from_bytes(data[1:4], byteorder='big', signed=True)
    # Convert to voltage: (raw_value / 2^23) * VREF / GAIN
    voltage = (raw_value / (2**23)) * VREF / GAIN
    # Extract 24-bit timestamp (bytes 4–6)
    timestamp = int.from_bytes(data[4:SAMPLE_SIZE], byteorder='big')  # Microseconds
    return channel_id, voltage, timestamp

def main():
    ser = open_serial_port()
    start_time = time.time()
    sample_count = 0
    last_print = start_time
    error_count = 0

    with open(OUTPUT_FILE, 'w', newline='') as csvfile:
        csv_writer = csv.writer(csvfile)
        csv_writer.writerow(['Timestamp (us)', 'Channel', 'Voltage'])

        try:
            while True:
                # Read up to 252 bytes (36 samples x 7 bytes)
                data = ser.read(256)
                print(data)
                if len(data) == 0:
                    continue  # Timeout, try again
                i = 0
                while i < len(data):
                    if i + SAMPLE_SIZE <= len(data):  # Check for complete sample
                        channel_id, voltage, timestamp = decode_sample(data[i:i+SAMPLE_SIZE])
                        if channel_id is not None:
                            sample_count += 1
                            csv_writer.writerow([timestamp, channel_id, f"{voltage:.6f}"])
                        else:
                            error_count += 1
                            print(f"Invalid sample at offset {i}: {data[i:i+SAMPLE_SIZE].hex()}")
                            i += 1  # Skip one byte to re-align
                            continue
                    else:
                        error_count += 1
                        print(f"Partial sample at end: {len(data[i:])} bytes")
                        i += 1  # Skip one byte to re-align
                        continue
                    i += SAMPLE_SIZE
                
                # Flush CSV periodically
                if sample_count % 1000 == 0 and sample_count > 0:
                    csvfile.flush()
                
                # Print status every second
                if time.time() - last_print >= 1:
                    rate = sample_count / (time.time() - start_time)
                    print(f"Received {sample_count} samples, rate: {rate:.2f} sps, errors: {error_count}")
                    last_print = time.time()

        except KeyboardInterrupt:
            print("\nStopped by user")
        except serial.SerialException as e:
            print(f"Serial error: {e}")
        finally:
            ser.close()
            print(f"Closed serial port. Data saved to {OUTPUT_FILE}")
            print(f"Total samples: {sample_count}, Avg rate: {sample_count / (time.time() - start_time):.2f} sps, Errors: {error_count}")

if __name__ == "__main__":
    main()
import serial
import struct
import time
import csv
import os

# Configuration
SERIAL_PORT = 'COM3'  # Change to '/dev/ttyACM0' or similar on Linux/macOS
BAUD_RATE = 115200    # USB CDC doesn't strictly use baud, but set for compatibility
OUTPUT_FILE = 'adc_data.csv'
SAMPLE_RATE = 153600  # Total samples/sec (76.8 ksps per channel)
READ_TIMEOUT = 1      # Seconds

# MCP3564 settings (match Pico code)
VREF = 3.3           # Reference voltage (adjust if different in your setup)
RESOLUTION = 24      # 24-bit ADC
GAIN = 1             # Gain setting from CONFIG2

def open_serial_port():
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=READ_TIMEOUT)
        print(f"Connected to {SERIAL_PORT}")
        return ser
    except serial.SerialException as e:
        print(f"Error opening serial port: {e}")
        exit(1)

def decode_sample(data):
    """Decode 4-byte sample: 1 byte channel ID, 3 bytes ADC value."""
    if len(data) != 4:
        return None, None
    channel_id = data[0] >> 6  # Top 2 bits indicate channel
    # Extract 24-bit signed value (2's complement)
    raw_value = int.from_bytes(data[1:4], byteorder='big', signed=True)
    # Convert to voltage: (raw_value / 2^23) * VREF / GAIN
    voltage = (raw_value / (2**23)) * VREF / GAIN
    return channel_id, voltage

def main():
    ser = open_serial_port()
    start_time = time.time()
    sample_count = 0
    last_print = start_time

    # Open CSV file for writing
    with open(OUTPUT_FILE, 'w', newline='') as csvfile:
        csv_writer = csv.writer(csvfile)
        csv_writer.writerow(['Timestamp', 'Channel', 'Voltage'])

        try:
            while True:
                # Read 4 bytes at a time (one sample)
                data = ser.read(4)
                if len(data) == 4:
                    channel_id, voltage = decode_sample(data)
                    if channel_id is not None:
                        timestamp = time.time() - start_time
                        sample_count += 1
                        # Write to CSV
                        csv_writer.writerow([timestamp, channel_id, f"{voltage:.6f}"])
                        # Periodically print status
                        if time.time() - last_print >= 1:
                            rate = sample_count / (time.time() - start_time)
                            print(f"Received {sample_count} samples, rate: {rate:.2f} sps")
                            last_print = time.time()
                elif len(data) > 0:
                    print(f"Partial read: {len(data)} bytes, skipping")
                    # Re-align by reading one byte at a time until next sample
                    ser.read(1)
                
                # Flush CSV periodically to avoid memory issues
                if sample_count % 1000 == 0:
                    csvfile.flush()

        except KeyboardInterrupt:
            print("\nStopped by user")
        except serial.SerialException as e:
            print(f"Serial error: {e}")
        finally:
            ser.close()
            print(f"Closed serial port. Data saved to {OUTPUT_FILE}")
            print(f"Total samples: {sample_count}, Avg rate: {sample_count / (time.time() - start_time):.2f} sps")

if __name__ == "__main__":
    main()
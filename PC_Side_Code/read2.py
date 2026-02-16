import serial
import struct
import time
import pandas as pd
import os

# Configuration
SERIAL_PORT = '/dev/ttyACM0'  # Adjust for your system (e.g., '/dev/ttyACM0' on Linux/macOS)
BAUD_RATE = 5000000           # USB CDC baud rate (set for compatibility)
OUTPUT_FILE = 'adc_data.csv'
DRDY_SAMPLE_RATE = 11764.71   # Total samples/sec based on 85 µs DRDY period
READ_TIMEOUT = 2              # Seconds
VREF = 3.3                    # Reference voltage (adjust if different)
RESOLUTION = 32               # 24-bit ADC
GAIN = 1                      # Gain setting from CONFIG2

def open_serial_port():
    """Open serial port and return the port object and start time."""
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=READ_TIMEOUT)
        time.sleep(3)  # Wait for USB CDC to initialize
        print(f"Connected to {SERIAL_PORT}")
        return ser, time.time()
    except serial.SerialException as e:
        print(f"Error opening serial port: {e}")
        exit(1)

def decode_sample(data):
    """Decode 4-byte sample: 1 byte channel ID (bits 7:4), 3 bytes ADC value."""
    if len(data) != 4:
        return None, None
    channel_id = (data[0] >> 4) & 0x0F  # Extract channel ID from bits 7:4
    if channel_id not in [0, 1]:
        print(f"Invalid channel ID: {channel_id}")
        return None, None
    raw_value = int.from_bytes(data[1:4], byteorder='big', signed=True)
    voltage = (raw_value / (2**23)) * VREF / GAIN
    return channel_id, voltage

def main():
    # Initialize serial port and start time
    ser, start_time = open_serial_port()
    sample_count = 0
    last_print = start_time
    data_list = []
    debug_print_count = 0  # Limit raw data prints to avoid flooding

    # Ensure CSV file is empty or create new
    if os.path.exists(OUTPUT_FILE):
        os.remove(OUTPUT_FILE)

    try:
        while True:
            # Measure loop time to detect bottlenecks
            start_loop = time.time()
            # Read 256 bytes (64 samples) at once for efficiency
            data = ser.read(256)
            loop_time = time.time() - start_loop
            if loop_time > 85e-6:
                print(f"Warning: Loop time {loop_time*1e6:.2f} µs exceeds DRDY period (85 µs)")

            # Process each 4-byte sample
            for i in range(0, len(data), 4):
                if i + 4 <= len(data):
                    if debug_print_count < 10:  # Print first 10 samples for debugging
                        print(f"Raw data: {data[i:i+4].hex()}")
                        debug_print_count += 1
                    channel_id, voltage = decode_sample(data[i:i+4])
                    if channel_id is not None:
                        # Use DRDY-based timing for more accurate timestamps
                        timestamp = sample_count / DRDY_SAMPLE_RATE
                        sample_count += 1
                        data_list.append([timestamp, channel_id, voltage])
                else:
                    print(f"Partial read: {len(data[i:])} bytes, skipping")
                    break  # Skip incomplete sample

            # Periodic status update
            if time.time() - last_print >= 1:
                rate = sample_count / (time.time() - start_time)
                print(f"Received {sample_count} samples, rate: {rate:.2f} sps")
                last_print = time.time()

            # Write to CSV every 10,000 samples
            if sample_count % 10000 == 0 and data_list:
                df = pd.DataFrame(data_list, columns=['Timestamp', 'Channel', 'Voltage'])
                df.to_csv(OUTPUT_FILE, mode='a', index=False, header=not os.path.exists(OUTPUT_FILE))
                data_list = []
                print(f"Saved {sample_count} samples to {OUTPUT_FILE}")

    except KeyboardInterrupt:
        print("\nStopped by user")
    except serial.SerialException as e:
        print(f"Serial error: {e}")
    finally:
        # Save remaining data
        if data_list:
            df = pd.DataFrame(data_list, columns=['Timestamp', 'Channel', 'Voltage'])
            df.to_csv(OUTPUT_FILE, mode='a', index=False, header=not os.path.exists(OUTPUT_FILE))
        ser.close()
        print(f"Closed serial port. Data saved to {OUTPUT_FILE}")
        print(f"Total samples: {sample_count}, Avg rate: {sample_count / (time.time() - start_time):.2f} sps")

if __name__ == "__main__":
    main()
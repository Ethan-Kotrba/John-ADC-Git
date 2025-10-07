import serial
import struct
import time
import csv
import os

# Configuration
SERIAL_PORT = '/dev/ttyACM0'  # Adjust for your system
BAUD_RATE = 5000000
OUTPUT_FILE = 'adc_data.csv'
SAMPLE_RATE = 7200  # Total samples/sec (3.6 ksps per channel, OSR=256)
READ_TIMEOUT = 1
VREF = 3.3
RESOLUTION = 32
GAIN = 1

def open_serial_port():
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=READ_TIMEOUT)
        time.sleep(2)
        print(f"Connected to {SERIAL_PORT}")
        return ser
    except serial.SerialException as e:
        print(f"Error opening serial port: {e}")
        exit(1)

def decode_sample(data):
    if len(data) != 4:
        return None, None
    channel_id = data[0] >> 4 & 0x0F  # Channel ID in bits 7:4
    if channel_id not in (0, 1):
        return None, None
    raw_value = int.from_bytes(data[1:4], byteorder='big', signed=True)
    voltage = (raw_value / (2**23)) * VREF / GAIN
    return channel_id, voltage

def read_debug_string(ser, length):
    data = ser.read(length)
    if len(data) == length:
        return data.decode('ascii', errors='ignore')
    return None

def main():
    ser = open_serial_port()
    start_time = time.time()
    sample_count = 0
    last_print = start_time

    with open(OUTPUT_FILE, 'w', newline='') as csvfile:
        csv_writer = csv.writer(csvfile)
        csv_writer.writerow(['Timestamp', 'Channel', 'Voltage'])

        try:
            while True:
                # Read first byte to check for debug string or sample
                header = ser.read(1)
                if len(header) == 0:
                    continue

                if header[0] == 0xFF:  # Debug string marker
                    length_byte = ser.read(1)
                    if len(length_byte) == 0:
                        continue
                    length = length_byte[0]
                    debug_str = read_debug_string(ser, length)
                    if debug_str:
                        print(f"DEBUG: {debug_str}", end='')
                    else:
                        print(f"Partial debug string read: expected {length} bytes")
                    continue

                # Assume ADC sample: read 3 more bytes (total 4)
                data = header + ser.read(3)
                if len(data) == 4:
                    channel_id, voltage = decode_sample(data)
                    if channel_id is not None:
                        timestamp = time.time() - start_time
                        sample_count += 1
                        csv_writer.writerow([timestamp, channel_id, f"{voltage:.6f}"])
                        if time.time() - last_print >= 1:
                            rate = sample_count / (time.time() - start_time)
                            print(f"Received {sample_count} samples, rate: {rate:.2f} sps")
                            last_print = time.time()
                    else:
                        print(f"Invalid sample: {data.hex()}")
                else:
                    print(f"Partial read: {len(data)} bytes, skipping")

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
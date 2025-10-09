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
SAMPLE_SIZE = 0

# MCP3564 settings (match Pico code)
VREF = 3.3           # Reference voltage (adjust if different in your setup)
RESOLUTION = 24      # 24-bit ADC
GAIN = 1             # Gain setting from CONFIG2

def open_serial_port():
    while(1):
        try:
            ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=READ_TIMEOUT)
            time.sleep(2)  # Wait for USB CDC to initialize
            print(f"Connected to {SERIAL_PORT}")
            return ser
        except serial.SerialException as e:
            print(f"Error opening serial port: {e}")
            # exit(1)

def decode_sample(data):
    """Decode 8-byte sample: 1 byte channel ID, 3 bytes ADC value, 4-byte timestamp"""
    if len(data) != SAMPLE_SIZE:
        return None, None
    #extract Timestamp
    timestamp = int.from_bytes(data[0:SAMPLE_SIZE], byteorder='big')
    return timestamp

def realign_serial(ser):
    print("Realigning serial stream...")
    while True:
        data = ser.read(1)
        if len(data) == 1:
            channel_id = data[0] >> 4
            if channel_id in [0, 1]:
                ser.read(7)
                return ser.read(8)
            else:
                continue
        elif len(data) == 0:
            print("Serial timeout during realignment")
            return None

def main():
    ser = open_serial_port()
    start_time = time.time()
    sample_count = 0
    last_print = start_time


    #Ideally would just safe the data using pandas then convert to .csv file, but whatever grok
    # Open CSV file for writing
    with open(OUTPUT_FILE, 'w', newline='') as csvfile:
        csv_writer = csv.writer(csvfile)
        csv_writer.writerow(['Timestamp'])

        try:
            while True:
                # Read 4 bytes at a time (one sample)
                data = ser.read(SAMPLE_SIZE)
                if len(data) == SAMPLE_SIZE:
                    # print(data)
                    # print(f"{hex(data[0])} {hex(data[1])} {hex(data[2])} {hex(data[3])} {hex(data[4])} {hex(data[5])} {hex(data[6])} {hex(data[7])}")
                    timestamp = decode_sample(data)
                    
                    csv_writer.writerow([timestamp])
                        # csv_writer.writerow([data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]])
                        
                        # Periodically print status
                    if time.time() - last_print >= 1:
                            rate = sample_count / (time.time() - start_time)
                            print(f"Received {sample_count}, rate: {rate:.2f} sps")
                            last_print = time.time()
                elif len(data) != 0:
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
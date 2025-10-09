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
VREF = 3.3           # Reference voltage (adjust if different in your setup)
RESOLUTION = 24      # 24-bit ADC
GAIN = 1             # Gain setting from CONFIG2

PERVIOUS_SAMPLE = None
PERVIOUS_TIMESTAMP = None


# class Pyrometer(object):
#     def __init__(self):
#         self.Serial_Port = '/dev/ttyACM0'
#         self.Output_File = 'adc_data.csv'
#         self.Read_Timeout = 1

#     def Open_Serial_Port(self):
#         while(True):
#             try:
#                 self.ser = serial.Serial(self.Serial_Port, 115300, timeout=self.Read_Timeout)
#                 time.sleep(2)  # Wait for USB CDC to initialize
#                 print(f"Connected to {SERIAL_PORT}")
#                 break
#             except serial.SerialException as e:
#                 print(f"Error opening serial port: {e}")
#                 # exit(1)


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
        return None, None, None
    channel_id = data[0] >> 4  # Top 2 bits indicate channel
    
    # Extract 24-bit signed value (2's complement)
    #There might be an issue here since its assuming signed, but I thin,
    #you need the second have of the first byte for the sign
    raw_value = int.from_bytes(data[1:4], byteorder='big', signed=False)
    # Convert to voltage: (raw_value / 2^23) * VREF / GAIN
    voltage = (raw_value / (2**23)) * VREF / GAIN
    #extract Timestamp
    timestamp = int.from_bytes(data[4:SAMPLE_SIZE], byteorder='big')
    return channel_id, voltage, timestamp

def Is_Sample_Tricky(data, Tol):
    if PERVIOUS_TIMESTAMP is None:
        return False
    timestamp = int.from_bytes(data[4:SAMPLE_SIZE], byteorder='big')
    if abs(timestamp - PERVIOUS_TIMESTAMP) < Tol:
        return False
    return True
    


def Is_Sample_Valid(data):
    channel_id = (data[0] >> 4)
    sign = (data[0] & 0x0F)
    print(channel_id)
    print(sign)
    adc_data = int.from_bytes(data[1:4], byteorder='big', signed=False)
    print(adc_data)
    if channel_id not in [0, 1]:
        return False
    if sign not in [0xF, 0x0]:
        return False
    if 0 > adc_data or adc_data > 0xFFFFFF:
        return False
    if Is_Sample_Tricky(data, 10000):
        return False
    return True
        
def realign_serial(ser):
    print("Realigning")
    while True:
        data = ser.read(1)

        if len(data) != 1:
            continue

        #Check to see of channel id and sign nibble are valid
        if (data[0] >> 4) not in [0, 1] or (data[0] & 0x0F) not in [0x0, 0xF]:
            continue

        data_full = data + ser.read(7)

        if Is_Sample_Valid(data_full):
            return data_full
                    
                
            


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
                data = ser.read(SAMPLE_SIZE)
                if len(data) == SAMPLE_SIZE:
                    # print(data)
                    # print(f"{hex(data[0])} {hex(data[1])} {hex(data[2])} {hex(data[3])} {hex(data[4])} {hex(data[5])} {hex(data[6])} {hex(data[7])}")
                    channel_id, voltage, timestamp = decode_sample(data)
                    
                    if True:
                        sample_count += 1
                        # Write to CSV
                        csv_writer.writerow([timestamp, channel_id, f"{voltage:.6f}"])
                        PERVIOUS_TIMESTAMP = timestamp
                        # csv_writer.writerow([data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]])
                        
                        # Periodically print status
                        if time.time() - last_print >= 1:
                            rate = sample_count / (time.time() - start_time)
                            print(f"Received {sample_count}, rate: {rate:.2f} sps")
                            last_print = time.time()
                    else:
                        data = realign_serial(ser)
                        channel_id, voltage, timestamp = decode_sample(data)
                        csv_writer.writerow([timestamp, channel_id, f"{voltage:.6f}"])
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
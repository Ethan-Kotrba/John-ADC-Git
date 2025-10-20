
import serial
import time
import os
import argparse
from datetime import datetime



def parse_args():
    parser = argparse.ArgumentParser(description="Read raw data from USB serial and save to binary file")
    parser.add_argument('--port', default='/dev/ttyACM0', help='Serial port (e.g., /dev/ttyACM0)')
    parser.add_argument('--output', default='raw_adc_data.bin', help='Output binary file')
    parser.add_argument('--timeout', type=float, default=1.0, help='Serial read timeout in seconds')
    parser.add_argument('--chunk-size', type=int, default=1024, help='Bytes to read per iteration')
    return parser.parse_args()

def open_serial_port(port, timeout):
    while True:
        try:
            ser = serial.Serial(port, timeout=timeout)
            print(f"Connected to {port}")
            return ser
        except serial.SerialException as e:
            print(f"Error opening serial port: {e}")
            time.sleep(1)

def main():
    args = parse_args()
    
    # Generate unique output filename with timestamp
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    output_file = f"{os.path.splitext(args.output)[0]}_{timestamp}.bin"
    
    # Open serial port
    ser = open_serial_port(args.port, args.timeout)
    
    start_time = time.time()
    total_bytes = 0
    
    try:
        with open(output_file, 'wb') as f:
            while True:
                # Read large chunks to minimize overhead
                data = ser.read(args.chunk_size)
                if data:
                    f.write(data)
                    total_bytes += len(data)
                    
                    # Print throughput every second
                    elapsed = time.time() - start_time
                    if elapsed >= 1.0:
                        rate = total_bytes / elapsed / 1e6  # MB/s
                        print(f"Read {total_bytes} bytes, throughput: {rate:.2f} MB/s")
                        start_time = time.time()
                        total_bytes = 0
                    f.flush()  # Ensure data is written to disk
                else:
                    print("No data received, checking connection...")
                    time.sleep(0.1)
                
    except KeyboardInterrupt:
        print("\nStopped by user")
    except serial.SerialException as e:
        print(f"Serial error: {e}")
    except IOError as e:
        print(f"File write error: {e}")
    finally:
        ser.close()
        print(f"Closed serial port. Data saved to {output_file}")

if __name__ == "__main__":
    main()

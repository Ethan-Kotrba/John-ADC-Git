import serial
import serial.tools.list_ports
import struct
import time
import os
from datetime import datetime

def find_pico_port():
    """Find the Raspberry Pi Pico's USB serial port based on VID:PID (2E8A:0005)."""
    pico_vid_pid = "2E8A:000A"
    ports = serial.tools.list_ports.comports()
    for port in ports:
        if pico_vid_pid in port.hwid:
            return port.device
    raise RuntimeError("Raspberry Pi Pico not found. Ensure it is connected and in BOOTSEL mode or running CDC firmware.")

# def process_sample(data):
#     """Unpack 8-byte sample: 4 bytes ADC data (with channel ID), 4 bytes timestamp."""
#     if len(data) != 8:
#         print(f"Warning: Received incomplete sample of {len(data)} bytes")
#         return None
#     # Unpack 4-byte ADC data (32-bit with channel ID) and 4-byte timestamp
#     adc_data, timestamp = struct.unpack('>II', data)
#     # Extract channel ID (bits 31:28) and ADC value (bits 23:0)
#     channel_id = (adc_data >> 28) & 0x0F
#     adc_value = adc_data & 0x00FFFFFF  # 24-bit ADC value
#     # Convert to signed if needed (MCP3564 24-bit ADC uses two's complement)
#     if adc_value & 0x00800000:  # Check sign bit
#         adc_value = adc_value - 0x01000000
#     return channel_id, adc_value, timestamp

def process_sample(data):
    """Unpack 8-byte sample: 4 bytes ADC (big-endian), 4 bytes timestamp (little-endian)."""
    if len(data) != 8:
        print(f"Warning: Incomplete sample ({len(data)} bytes), skipping")
        return None
    
    # Mixed unpacking: >I for ADC (big-endian), <I for timestamp (little-endian)
    try:
        adc_data = struct.unpack('>I', data[0:4])[0]
        timestamp = struct.unpack('<I', data[4:8])[0]
    except struct.error as e:
        print(f"Unpack error: {e}")
        return None
    
    # Extract channel and ADC value
    channel_id = (adc_data >> 28) & 0x0F
    adc_value = adc_data & 0x00FFFFFF  # 24-bit
    if adc_value & 0x00800000:  # Sign extend if negative (two's complement)
        adc_value -= 0x01000000
    
    # Sanity checks: Filter junk
    if channel_id not in (0, 1):
        print(f"Invalid channel {channel_id}, skipping")
        return None
    if adc_value < 0 or adc_value > 0xFFFFFF:  # Valid 24-bit range
        print(f"Invalid ADC value {adc_value}, skipping")
        return None
    
    return channel_id, adc_value, timestamp

def main():
    # Find the Pico's serial port
    try:
        port = find_pico_port()
        print(f"Found Raspberry Pi Pico on {port}")
    except RuntimeError as e:
        print(e)
        return

    # Open serial port
    try:
        ser = serial.Serial(
            port=port,
            baudrate=115200,  # Baudrate is irrelevant for USB CDC, set for compatibility
            timeout=1
        )
    except serial.SerialException as e:
        print(f"Failed to open serial port: {e}")
        return

    # Create output file with timestamp
    timestamp_str = datetime.now().strftime("%Y%m%d_%H%M%S")
    output_file = f"pico_adc_data_{timestamp_str}.csv"
    print(f"Saving data to {output_file}")

    try:
        with open(output_file, 'w') as f:
            f.write("Timestamp,Channel,ADC_Value\n")
            sample_buffer = b""
            start_time = time.time()
            sample_count = 0

            while True:
                # Read available data
                data = ser.read(256)  # Read in chunks matching Pico's send buffer
                if not data:
                    continue

                sample_buffer += data
                # Process complete 8-byte samples
                while len(sample_buffer) >= 8:
                    sample_data = sample_buffer[:8]
                    sample_buffer = sample_buffer[8:]
                    result = process_sample(sample_data)
                    if result:
                        channel_id, adc_value, timestamp = result
                        # Write to CSV
                        f.write(f"{timestamp},{channel_id},{adc_value}\n")
                        sample_count += 1
                        # Print progress every 1000 samples
                        if sample_count % 1000 == 0:
                            elapsed = time.time() - start_time
                            rate = sample_count / elapsed if elapsed > 0 else 0
                            print(f"Collected {sample_count} samples, rate: {rate:.2f} samples/s")

                # Optional: Stop after a certain time or number of samples
                # if sample_count >= 100000 or time.time() - start_time > 60:
                #     break

    except KeyboardInterrupt:
        print("\nStopped by user")
    except Exception as e:
        print(f"Error: {e}")
    finally:
        ser.close()
        print(f"Closed serial port. Total samples collected: {sample_count}")
        print(f"Data saved to {output_file}")

if __name__ == "__main__":
    main()
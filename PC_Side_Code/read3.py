import serial
import struct
while(True):
    try:
        ser = serial.Serial('/dev/ttyACM0', timeout=1)
while True:
    data = ser.read(256)
    for i in range(0, len(data), 4):
        if len(data[i:i+4]) == 4:
            sample = struct.unpack('<I', data[i:i+4])[0]
            ch_id = (sample >> 28) & 0x0F
            value = (sample >> 4) & 0xFFFFFF
            print(f"Channel {ch_id}: {value}")
            # CSV writing logic here
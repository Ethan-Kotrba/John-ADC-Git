import serial
import struct

with serial.Serial('/dev/ttyACM0', 115200, timeout=1) as ser:
    while True:
        data = ser.read(4096)
        if data:
            samples = struct.unpack('<' + 'H' * (len(data) // 2), data)
            for i in range(0, len(samples), 2):
                print(f"Sensor1: {samples[i]}, Sensor2: {samples[i+1]}")
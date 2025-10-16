import serial, time


ser = serial.Serial('/dev/ttyACM0', timeout=1)
start = time.time()
total = 0
while time.time() - start < 10:
    total += len(ser.read(1024))
print(f"Throughput: {total / (time.time() - start) / 1e6} MB/s")
ser.close()
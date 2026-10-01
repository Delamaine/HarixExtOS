import serial
import time
import sys

s = serial.Serial('COM3', 115200, timeout=0.1)
time.sleep(2)

print('=== Reading serial output for 10 seconds ===')
start = time.time()
while time.time() - start < 10:
    if s.in_waiting > 0:
        data = s.read(s.in_waiting)
        sys.stdout.buffer.write(data)
        sys.stdout.flush()
    time.sleep(0.05)

print('\n=== Done ===')

import serial
import time
import sys

s = serial.Serial('COM3', 115200, timeout=1)
time.sleep(2)

commands = [
    'help',
    'sensor list',
    'servo list',
    'help sensor',
    'help servo',
    'heap',
    'uptime',
    'info',
    'reboot',
]

print('=== Comprehensive Serial Test ===')
print()

for cmd in commands:
    print(f'Sending: {cmd}')
    s.write(f'{cmd}\r\n'.encode())
    time.sleep(1)
    data = s.read(s.in_waiting)
    if data:
        text = data.decode('utf-8', errors='replace').strip()
        # Show first and last 50 chars if response is long
        if len(text) > 100:
            print(f'Response: {text[:50]}...{text[-50:]}')
        else:
            print(f'Response: {text}')
    else:
        print('Response: (empty)')
    print()

print('=== Test Complete ===')

"""MQTT shell round-trip test.

Publishes a command to <prefix>/shell/in and asserts the reply on
<prefix>/shell/out. Written as the failing (RED) test for making `vars`
available through the script dispatcher.

Usage:
  python test/scripts/test_mqtt_shell.py [command] [expected-substring]

Defaults: command='vars list', expected='Variables:'

Exit code 0 = pass, 1 = fail.
"""
import sys
import time

import paho.mqtt.client as mqtt

BROKER = '192.168.20.5'
PORT = 1883
PREFIX = 'harixos/a590a8'

COMMAND = sys.argv[1] if len(sys.argv) > 1 else 'vars list'
EXPECT = sys.argv[2] if len(sys.argv) > 2 else 'Variables:'
MUST_NOT = 'Unknown command'

replies = []


def on_connect(client, userdata, flags, reason_code, properties=None):
    client.subscribe(f'{PREFIX}/shell/out')


def on_message(client, userdata, msg):
    replies.append(msg.payload.decode('utf-8', errors='replace'))


client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
client.on_connect = on_connect
client.on_message = on_message
client.connect(BROKER, PORT, 30)
client.loop_start()
time.sleep(2.0)
del replies[:]  # drain anything published before this test started
client.publish(f'{PREFIX}/shell/in', COMMAND, qos=0)

deadline = time.time() + 6
while time.time() < deadline and not replies:
    time.sleep(0.1)
client.loop_stop()
client.disconnect()

print(f'command : {COMMAND}')
for i, r in enumerate(replies):
    print(f'reply[{i}]: {r!r}')

if not replies:
    print('FAIL: no reply on shell/out')
    sys.exit(1)
if all(MUST_NOT in r for r in replies):
    print(f'FAIL: every reply contains {MUST_NOT!r}')
    sys.exit(1)
if not any(EXPECT in r for r in replies):
    print(f'FAIL: no reply contains {EXPECT!r}')
    sys.exit(1)
print('PASS')

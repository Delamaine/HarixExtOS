import serial, time, socket, struct, subprocess, sys

def ru(ser, n, t):
    b, e = '', time.time() + t
    while time.time() < e:
        d = ser.read(ser.in_waiting or 1)
        if d:
            b += d.decode(errors='replace')
            if n in b:
                return b
    return b

ser = serial.Serial('COM3', 115200, timeout=0.2)
ser.reset_input_buffer()
o = ru(ser, 'HarixOS> ', 90)
print('boot prompt:', 'HarixOS> ' in o, '| exception:', 'CUT HERE FOR EXCEPTION' in o)
time.sleep(10)

def a_probe(name):
    labels = name.split('.')
    q = b'\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00'
    for lab in labels:
        q += bytes([len(lab)]) + lab.encode()
    q += b'\x00\x00\x01\x00\x01'
    sk = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sk.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sk.bind(('', 5353))
    sk.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
                  struct.pack('=4s4s', socket.inet_aton('224.0.0.251'), socket.inet_aton('192.168.20.5')))
    sk.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton('192.168.20.5'))
    sk.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 255)
    sk.settimeout(3)
    sk.sendto(q, ('224.0.0.251', 5353))
    end = time.time() + 2.5
    hit = None
    while time.time() < end:
        try:
            d, a = sk.recvfrom(4096)
        except socket.timeout:
            break
        if a[0] == '192.168.20.26' and len(d) >= 12 and (d[2] & 0x80) and name.encode() in d:
            i = d.find(b'\x00\x04', 12)
            if i > 0:
                hit = '.'.join(str(x) for x in d[i + 2:i + 6])
                break
    sk.close()
    return hit

ip = a_probe('esp8266.rimu.local')
print('post-flash raw A esp8266.rimu.local ->', ip)
ok_flash = ip == '192.168.20.26'

ser.write(b'settings hostname test1\n')
print([l for l in ru(ser, 'HarixOS> ', 8).splitlines() if 'Hostname' in l])
time.sleep(6)

r = subprocess.run(['powershell.exe', '-NoProfile', '-Command',
                    'Resolve-DnsName -Name test1.local -Type A -ErrorAction Stop | ForEach-Object { $_.IPAddress }'],
                   capture_output=True, text=True, timeout=25)
out = (r.stdout or r.stderr).strip()
print('Resolve test1.local ->', out.replace('\n', ' '))
ok_t7 = r.returncode == 0 and '192.168.20.26' in out

ser.write(b'settings show\n')
o = ru(ser, 'HarixOS> ', 8)
print([l.strip() for l in o.splitlines() if 'ostname' in l])
ser.write(b'info\n')
o = ru(ser, 'HarixOS> ', 8)
print([l.strip() for l in o.splitlines() if 'mDNS' in l])
ser.close()
print('post-flash dotted probe:', 'PASS' if ok_flash else 'FAIL')
print('step7 test1.local resolve:', 'PASS' if ok_t7 else 'FAIL')
sys.exit(0 if (ok_flash and ok_t7) else 1)

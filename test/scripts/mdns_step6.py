import serial, time, socket, struct, sys

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
ser.write(b'serve /harixos/settings.cfg 8266\n')
o = ru(ser, 'HarixOS> ', 12)
print('serve:', 'Serving' in o)
if 'Serving' not in o:
    print(o[-300:]); sys.exit(1)
time.sleep(6)

def decode(data, p):
    labs = []
    while True:
        if p >= len(data):
            return '.'.join(labs), p
        l = data[p]
        if l == 0:
            return '.'.join(labs), p + 1
        if l & 0xC0 == 0xC0:
            ptr = ((l & 0x3F) << 8) | data[p + 1]
            n, _ = decode(data, ptr)
            labs.append(n)
            return '.'.join(labs), p + 2
        labs.append(data[p + 1:p + 1 + l].decode('latin1'))
        p += 1 + l

recs = []
for qname in (b'_services._dns-sd._udp.local', b'_http._tcp.local'):
    q = b'\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00'
    for lab in qname.split(b'.'):
        q += bytes([len(lab)]) + lab
    q += b'\x00\x00\x0c\x00\x01'
    sk = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sk.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sk.bind(('', 5353))
    sk.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
                  struct.pack('=4s4s', socket.inet_aton('224.0.0.251'), socket.inet_aton('192.168.20.5')))
    sk.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton('192.168.20.5'))
    sk.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 255)
    sk.settimeout(3)
    sk.sendto(q, ('224.0.0.251', 5353))
    end = time.time() + 3
    while time.time() < end:
        try:
            d, a = sk.recvfrom(4096)
        except socket.timeout:
            break
        if a[0] != '192.168.20.26' or len(d) < 12 or not (d[2] & 0x80):
            continue
        qd, an = (d[4] << 8 | d[5]), (d[6] << 8 | d[7])
        ns, ar = (d[8] << 8 | d[9]), (d[10] << 8 | d[11])
        p = 12
        for _ in range(qd):
            _, p = decode(d, p); p += 4
        for cnt in (an, ns, ar):
            for _ in range(cnt):
                n, p = decode(d, p)
                t = d[p] << 8 | d[p + 1]
                rl = d[p + 8] << 8 | d[p + 9]
                body = p + 10
                if t == 12:
                    tgt, _ = decode(d, body); desc = 'PTR ' + tgt
                elif t == 33:
                    tgt, _ = decode(d, body + 6)
                    desc = 'SRV %s:%d' % (tgt, d[body + 4] << 8 | d[body + 5])
                elif t == 1:
                    desc = 'A ' + '.'.join(str(x) for x in d[body:body + 4])
                elif t == 16:
                    desc = 'TXT'
                else:
                    desc = 'type%d' % t
                recs.append((n, desc))
                p = body + rl
    sk.close()

uniq = sorted(set(recs))
for r in uniq:
    print(' ', r[0], r[1])

checks = {
    'services PTR': ('_services._dns-sd._udp.local', 'PTR _http._tcp.local'),
    'instance PTR': ('_http._tcp.local', 'PTR esp8266.rimu._http._tcp.local'),
    'SRV target': (None, 'SRV esp8266.rimu.local:8266'),
    'A record': ('esp8266.rimu.local', 'A 192.168.20.26'),
}
ok = True
for k, (n, desc) in checks.items():
    hit = any(desc == d and (n is None or nn == n) for nn, d in recs)
    print(('PASS' if hit else 'FAIL'), '-', k)
    ok = ok and hit

import urllib.request
req = urllib.request.Request('http://esp8266.rimu.local:8266/',
                             headers={'Host': 'esp8266.rimu.local'})
# Host header alone won't route; use curl --resolve (bypasses Windows resolver)
r = None
import subprocess
p = subprocess.run(['curl', '-s', '-o', '-', '-w', '%{http_code}',
                    '--resolve', 'esp8266.rimu.local:8266:192.168.20.26',
                    'http://esp8266.rimu.local:8266/'],
                   capture_output=True, text=True, timeout=20)
body = p.stdout[:-3] if len(p.stdout) >= 3 else ''
code = p.stdout[-3:]
print('curl code:', code, 'body[:40]:', repr(body[:40]))
ok = ok and code == '200'

ser.write(b'serve stop\n'); ru(ser, 'HarixOS> ', 8)
ser.close()
print('OVERALL:', 'PASS' if ok else 'FAIL')
sys.exit(0 if ok else 1)

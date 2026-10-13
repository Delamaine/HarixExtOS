import serial, time, sys, subprocess, re

PORT, BAUD = "COM3", 115200
PROMPT = "HarixOS> "

def read_until(ser, needles, timeout):
    buf, end = "", time.time() + timeout
    while time.time() < end:
        n = ser.in_waiting
        data = ser.read(n if n else 1)
        if data:
            buf += data.decode(errors="replace")
            for x in needles:
                if x in buf:
                    return buf, True
    return buf, False

def send(ser, cmd):
    ser.write((cmd + "\r\n").encode()); ser.flush()

def collect(ser, seconds):
    buf, end = "", time.time() + seconds
    while time.time() < end:
        data = ser.read(256)
        if data:
            buf += data.decode(errors="replace")
    return buf

def pc(cmd, timeout=25):
    r = subprocess.run(["powershell.exe", "-NoProfile", "-Command", cmd],
                       capture_output=True, text=True, timeout=timeout)
    return r.returncode == 0, (r.stdout or "") + (r.stderr or "")

def resolve(name):
    return pc("Resolve-DnsName -Name %s -Type A -ErrorAction Stop | ForEach-Object { $_.IPAddress }" % name)

def req(url):
    return pc("Invoke-WebRequest -Uri '%s' -UseBasicParsing -TimeoutSec 10 | Select-Object -ExpandProperty StatusCode" % url)

res = {}
ser = serial.Serial(PORT, BAUD, timeout=0.2)
ser.rts = True; time.sleep(0.3); ser.rts = False; time.sleep(0.1)
ser.reset_input_buffer()

send(ser, "reboot")

# Baseline
out, ok = read_until(ser, [PROMPT], 75)
res["baseline: boot to prompt"] = ok
res["baseline: no exception"] = "CUT HERE FOR EXCEPTION" not in out

send(ser, "heap")
out, _ = read_until(ser, [PROMPT], 8)
m = re.search(r"Heap free: (\d+)", out)
B0 = int(m.group(1)) if m else 0
res["baseline: heap read"] = bool(m)
print("B0 =", B0)

send(ser, "settings show")
out, _ = read_until(ser, [PROMPT], 8)
m = re.search(r"Hostname: (\S+)", out)
H = m.group(1) if m else ""
res["baseline: hostname read"] = bool(H)
print("H =", H)

send(ser, "wifi ip")
out, _ = read_until(ser, [PROMPT], 8)
print("wifi ip ->", out.strip()[-200:])

# Check 1 (RF2): resolves with a real IP
ok, txt = resolve(H + ".local")
res["c1: resolves H.local"] = ok
ips = re.findall(r"\b\d{1,3}(?:\.\d{1,3}){3}\b", txt)
print("c1 resolve:", ok, "|", txt.strip()[:200])
res["c1: IP not 0.0.0.0"] = bool(ips) and ips[0] not in ("0.0.0.0",)

# Check 2: serve + HTTP via hostname
send(ser, "serve /harixos/settings.cfg")
out, _ = read_until(ser, [PROMPT], 10)
if "Serving" not in out:
    send(ser, "serve /www/index.html")
    out, _ = read_until(ser, [PROMPT], 10)
m = re.search(r"on [\d.]+:(\d+)", out)
port = m.group(1) if m else ""
res["c2: serve started"] = bool(port)
print("port =", port)
time.sleep(5)
ok, txt = req("http://%s.local:%s/" % (H, port))
res["c2: HTTP 200 via hostname"] = ok
print("c2 http:", ok, "|", txt.strip()[:200])

# Check 3 (RF3): rename while serving
send(ser, "settings hostname test1")
out, _ = read_until(ser, [PROMPT], 8)
res["c3: saved message"] = "Settings saved. Hostname: test1" in out
res["c3: no start failure"] = "mDNS: start failed." not in out
time.sleep(5)
ok, txt = resolve("test1.local")
res["c3: resolves test1.local"] = ok
print("c3 resolve:", ok, "|", txt.strip()[:200])
ok, txt = req("http://test1.local:%s/" % port)
res["c3: HTTP 200 via new name"] = ok
print("c3 http:", ok, "|", txt.strip()[:200])

# Check 4 (RF4): stop / restart lifecycle
send(ser, "serve stop")
out, _ = read_until(ser, [PROMPT], 8)
res["c4: stop clean"] = "HTTP server stopped." in out and "mDNS: start failed." not in out
send(ser, "serve /harixos/settings.cfg")
out, _ = read_until(ser, [PROMPT], 10)
res["c4: re-serve started"] = "Serving" in out
time.sleep(4)
ok, txt = req("http://test1.local:%s/" % port)
res["c4: HTTP 200 after re-serve"] = ok
print("c4 http:", ok, "|", txt.strip()[:200])
send(ser, "serve stop")
read_until(ser, [PROMPT], 8)

# Check 5 (RF1): Wi-Fi down stable, then reconnect
send(ser, "wifi disconnect")
out = collect(ser, 30)
res["c5: no crash during down"] = "CUT HERE FOR EXCEPTION" not in out
res["c5: no mDNS spam"] = out.count("mDNS") == 0
print("c5 down tail:", repr(out[-300:]))
send(ser, "heap")
out, ok = read_until(ser, [PROMPT], 8)
res["c5: shell responsive after down"] = ok and "Heap free:" in out

send(ser, "reboot")
out, ok = read_until(ser, [PROMPT], 75)
res["c5: reboot to prompt"] = ok
res["c5: no exception"] = "CUT HERE FOR EXCEPTION" not in out
res["c5: no abnormal"] = "ended abnormally" not in out.lower()
send(ser, "info")
out, _ = read_until(ser, [PROMPT], 8)
res["c5: info mDNS line"] = "mDNS: test1.local" in out
time.sleep(5)
ok, txt = resolve("test1.local")
res["c5: resolves after reconnect"] = ok
print("c5 resolve after reboot:", ok, "|", txt.strip()[:200])

# Check 6 (RF5): two more clean boots, info + heap each
for i in (2, 3):
    send(ser, "reboot")
    out, ok = read_until(ser, [PROMPT], 75)
    res["c6 boot%d: prompt" % i] = ok
    res["c6 boot%d: no exception" % i] = "CUT HERE FOR EXCEPTION" not in out
    res["c6 boot%d: no abnormal" % i] = "ended abnormally" not in out.lower()
    send(ser, "info")
    out, _ = read_until(ser, [PROMPT], 8)
    res["c6 boot%d: info mDNS line" % i] = "mDNS: test1.local" in out
    print("info boot%d tail:" % i, out.strip()[-350:])
    send(ser, "heap")
    out, _ = read_until(ser, [PROMPT], 8)
    m = re.search(r"Heap free: (\d+)", out)
    b = int(m.group(1)) if m else 0
    res["c6 boot%d: heap >= B0-4096" % i] = b >= B0 - 4096
    print("heap boot%d = %d (B0 = %d)" % (i, b, B0))

print("=== RESULTS ===")
allpass = all(v is True for v in res.values())
for k, v in res.items():
    print(("PASS" if v is True else "FAIL"), "-", k)
print("OVERALL:", "PASS" if allpass else "FAIL")
ser.close()
sys.exit(0 if allpass else 1)

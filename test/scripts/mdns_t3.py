import serial, time, sys, subprocess, re, base64

MODE = sys.argv[1] if len(sys.argv) > 1 else "red"  # "red" = pre-fix, "green" = post-fix
CFG = "/harixos/settings.cfg"

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

def drain(ser, secs=1.0):
    end = time.time() + secs
    while time.time() < end:
        n = ser.in_waiting
        if n: ser.read(n)
        else: time.sleep(0.05)

def send(ser, cmd):
    ser.write((cmd + "\n").encode()); ser.flush()

def pc(cmd, timeout=25):
    r = subprocess.run(["powershell.exe", "-NoProfile", "-Command", cmd],
                       capture_output=True, text=True, timeout=timeout)
    return r.returncode == 0, (r.stdout or "") + (r.stderr or "")

# PTR _http._tcp.local query; returns base64 of every QR packet received in 3.5s
P5 = ("$m=New-Object Net.Sockets.UdpClient;"
      "$m.Client.SetSocketOption([Net.Sockets.SocketOptionLevel]::Socket,"
      "[Net.Sockets.SocketOptionName]::ReuseAddress,$true);"
      "try{$m.Client.Bind((New-Object IPEndPoint([Net.IPAddress]::Any,5353)))}"
      "catch{Write-Output 'bind-fail';$m.Close();exit 2};"
      "$m.JoinMulticastGroup([Net.IPAddress]'224.0.0.251');"
      "$m.Client.ReceiveTimeout=1500;"
      "$q=[byte[]](0,0,0,0,0,1,0,0,0,0,0,0)+[byte[]]@(5)"
      "+[Text.Encoding]::ASCII.GetBytes('_http')+[byte[]]@(3)"
      "+[Text.Encoding]::ASCII.GetBytes('tcp')+[byte[]]@(5)"
      "+[Text.Encoding]::ASCII.GetBytes('local')+[byte[]]@(0)"
      "+[byte[]]@(0,12,0,1);"
      "$ep=New-Object IPEndPoint([Net.IPAddress]'224.0.0.251',5353);"
      "$m.Send($q,$q.Length,$ep)|Out-Null;"
      "$sw=[Diagnostics.Stopwatch]::StartNew();$out=@();"
      "while($sw.ElapsedMilliseconds -lt 3500){"
      "try{$r=$m.Receive([ref]$ep);"
      "if(($r[2] -band 128) -ne 0){$out+=[Convert]::ToBase64String($r)}}"
      "catch{break}};"
      "$m.Close();Write-Output ($out -join ';')")

def parse_dns(data):
    if len(data) < 12:
        return 0, [], []
    flags = (data[2] << 8) | data[3]
    qd = (data[4] << 8) | data[5]
    an = (data[6] << 8) | data[7]
    pos = 12

    def read_name(p):
        labels = []
        while True:
            if p >= len(data):
                return ".".join(labels), p
            l = data[p]
            if l == 0:
                return ".".join(labels), p + 1
            if l & 0xC0 == 0xC0:
                ptr = ((l & 0x3F) << 8) | data[p + 1]
                n, _ = read_name(ptr)
                labels.append(n)
                return ".".join(labels), p + 2
            labels.append(data[p + 1:p + 1 + l].decode("latin1"))
            p += 1 + l

    qs, ans = [], []
    for _ in range(qd):
        n, pos = read_name(pos)
        pos += 4
        qs.append(n)
    for _ in range(an):
        n, pos = read_name(pos)
        rtype = (data[pos] << 8) | data[pos + 1]
        pos += 6  # type, class, ttl(2 of 4)... careful: ttl is 4
        # pos now at ttl+2 -> fix: type(2)+class(2)+ttl(4)=8
        pos += 2  # remaining ttl bytes
        rdlen = (data[pos] << 8) | data[pos + 1]
        pos += 2
        rdata = data[pos:pos + rdlen]
        desc = ""
        if rtype == 12:
            target, _ = read_name(pos)
            desc = "PTR->" + target
        elif rtype == 33:
            port = (rdata[4] << 8) | rdata[5]
            tgt, _ = read_name(pos + 6)
            desc = "SRV %s:%d" % (tgt, port)
        elif rtype == 1:
            desc = "A " + ".".join(str(b) for b in rdata)
        elif rtype == 16:
            desc = "TXT"
        else:
            desc = "type%d" % rtype
        pos += rdlen
        ans.append((n, desc))
    return flags, qs, ans

def probe():
    """Returns (hit, packets_summary). hit = any _http._tcp record from any responder."""
    okp, txt = pc(P5, timeout=25)
    if not okp and "bind-fail" in txt:
        return False, "bind-fail"
    hits = [x for x in txt.strip().split(";") if x]
    hit = False
    summary = []
    for b64 in hits:
        try:
            data = base64.b64decode(b64)
        except Exception:
            continue
        flags, qs, ans = parse_dns(data)
        for n, d in ans:
            line = "%s %s" % (n, d)
            summary.append(line)
            if "_http._tcp" in line:
                hit = True
    return hit, " || ".join(summary) if summary else "no packets"

def a_check(name):
    return pc("Resolve-DnsName -Name %s -Type A -ErrorAction Stop | ForEach-Object { $_.IPAddress }" % name)

KEYPRE = ("banner", "timezone", "wifi", "auto_", "power", "cpufreq", "mqtt_", "hostname")

def parse_cfg(text):
    cfg, order = {}, []
    for l in (x.strip() for x in text.splitlines()):
        if "=" in l and l.startswith(KEYPRE):
            k, _, v = l.partition("=")
            if k not in cfg:
                cfg[k] = v; order.append(k)
    return cfg, order

res = {}
ser = serial.Serial("COM3", 115200, timeout=0.2)
ser.rts = True; time.sleep(0.3); ser.rts = False; time.sleep(0.1)
ser.reset_input_buffer()

send(ser, "reboot")
out, ok = read_until(ser, ["HarixOS> "], 75)
res["A1 boot to prompt"] = ok
res["A2 boot: no exception"] = "CUT HERE FOR EXCEPTION" not in out
time.sleep(12); drain(ser, 1.0)

cfg, order = {}, []
for attempt in range(2):
    send(ser, "cat " + CFG)
    out, ok = read_until(ser, ["HarixOS> "], 8)
    cfg, order = parse_cfg(out)
    if len(order) >= 16: break
    drain(ser, 1.0)
ssid = cfg.get("wifiSSID", "")
pw = cfg.get("wifiPassword", "")
res["A3 cfg captured"] = bool(ssid) and len(order) >= 16

send(ser, "wifi ip")
out, _ = read_until(ser, ["HarixOS> "], 8)
res["A4 wifi connected"] = "Station IP:" in out and "Station IP: (IP unset)" not in out

# Phase A: probe discriminates on the normal path (serve while connected)
send(ser, "serve " + CFG)
out, _ = read_until(ser, ["HarixOS> "], 10)
res["A5 serve started"] = "Serving" in out
time.sleep(6)
hitA, sumA = probe()
res["A6 probe HIT while serving (service registered)"] = hitA
print("A6:", sumA[:400])
send(ser, "serve stop")
read_until(ser, ["HarixOS> "], 8)
time.sleep(3)
hitA2, sumA2 = probe()
res["A7 probe NO-HIT after stop"] = not hitA2
print("A7:", sumA2[:400])

# Phase B: RED scenario (disconnected boot -> interactive serve)
send(ser, "cp %s %s.bak" % (CFG, CFG))
read_until(ser, ["HarixOS> "], 8)
first = True
for k in order:
    v = "" if k == "wifiSSID" else cfg[k]
    send(ser, ("%s %s %s=%s") % ("write" if first else "append", CFG, k, v))
    first = False
    read_until(ser, ["HarixOS> "], 8)
send(ser, "reboot")
out, ok = read_until(ser, ["HarixOS> "], 75)
res["B1 RED boot to prompt"] = ok
res["B2 RED boot: no exception"] = "CUT HERE FOR EXCEPTION" not in out
time.sleep(3); drain(ser, 1.0)

send(ser, "wifi ip")
out, _ = read_until(ser, ["HarixOS> "], 8)
res["B3 RED boot: wifi down"] = "Station IP: (IP unset)" in out

send(ser, "info")
out, _ = read_until(ser, ["HarixOS> "], 8)
m = re.search(r"mDNS:\s*(\S+)\.local", out)
H = m.group(1) if m else ""
res["B4 hostname known"] = bool(H)
print("live hostname:", H)

send(ser, "serve " + CFG)
out, ok = read_until(ser, ["Enter SSID"], 12)
res["B5 SSID prompt"] = ok
send(ser, ssid)
out, ok = read_until(ser, ["Enter password"], 12)
res["B6 password prompt"] = ok
send(ser, pw)
out, ok = read_until(ser, ["Serving"], 35)
res["B7 serve started after join"] = ok
print("join tail:", out[-160:].replace("\n", "|"))
time.sleep(6)

aB, txt = a_check(H + ".local")
res["B8 hostname resolves (responder began)"] = aB
hitB, sumB = probe()
print("B9:", sumB[:400])
expected = (MODE == "green")
res["B9 _http._tcp present after disconnected serve (expect %s)" % MODE] = (hitB == expected)
red_confirmed = (not hitB) and aB and res["B7 serve started after join"]

# Restore + cleanup
send(ser, "cp %s.bak %s" % (CFG, CFG))
read_until(ser, ["HarixOS> "], 8)
send(ser, "rm %s.bak" % CFG)
read_until(ser, ["HarixOS> "], 8)
send(ser, "cat " + CFG)
out, _ = read_until(ser, ["HarixOS> "], 8)
rcfg, _ = parse_cfg(out)
res["B10 config restored"] = rcfg.get("wifiSSID") == ssid and rcfg.get("hostname") == cfg.get("hostname")
send(ser, "reboot")
read_until(ser, ["HarixOS> "], 75)

print("=== RESULTS (%s) ===" % MODE)
for k, v in res.items():
    print(("PASS" if v is True else "FAIL"), "-", k)
allpass = all(v is True for v in res.values())
print("OVERALL:", "PASS" if allpass else "FAIL")
if red_confirmed:
    print("RED CONFIRMED: serving + responder up, _http._tcp ABSENT (Finding 1 reproduces)")
ser.close()
sys.exit(0 if allpass else 1)

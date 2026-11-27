#!/usr/bin/env python3
import struct, email, urllib.parse
"""Mock of the Desk Display HTTP API for working on the web UI without hardware.

    python3 tools/mock_device.py [port]      then open http://localhost:8080/
"""
import json, os, re, sys, time, random
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WWW = os.path.join(ROOT, "www")


def extract(path, marker):
    src = open(os.path.join(ROOT, "src", path)).read()
    m = re.search(marker + r'\[\] = R"JSON\((.*?)\)JSON"', src, re.S)
    return json.loads(m.group(1))


SETTINGS = extract("settings.cpp", "DEFAULTS")
SETTINGS["api"]["token"] = "0123456789abcdef"
SETTINGS["weather"].update(lat=40.6782, lon=-73.9442, place="Brooklyn")
LAYOUT = extract("ui.cpp", "DEFAULT_LAYOUT")
DEFAULT_LAYOUT = json.loads(json.dumps(LAYOUT))
STATE = {"page": 0, "sleep": False, "night": False, "last": "swipe_left", "last_t": time.time()}
now = int(time.time())
DATA = {
    "claude.five_hour.used_percentage": 37, "claude.five_hour.resets_at": now + 2 * 3600 + 13 * 60,
    "claude.seven_day.used_percentage": 64, "claude.seven_day.resets_at": now + 3 * 86400,
    "claude.context_window.used_percentage": 22, "claude.model.display_name": "Opus", "claude.updated": now - 95,
    "weather.temp": 47.3, "weather.feels": 44, "weather.desc": "Partly cloudy", "weather.icon": "", "weather.hi": 52, "weather.lo": 39,
    "weather.place": "Brooklyn", "weather.updated": now - 300, "weather.humidity": 61, "weather.pp": 20,
    "sys.name": "Desk Display", "sys.url": "http://192.168.1.42/", "sys.ip": "192.168.1.42", "sys.rssi": -58, "sys.battery": -1,
    "sys.battery_text": "USB", "sys.version": "1.0.0", "sys.uptime_text": "3h 12m",
}
icons = ["", "", "", "", "", ""]
for i in range(6):
    DATA[f"weather.d{i}.t"] = now + i * 86400
    DATA[f"weather.d{i}.hi"] = 50 + i
    DATA[f"weather.d{i}.lo"] = 38 + i
    DATA[f"weather.d{i}.icon"] = icons[i % len(icons)]
    DATA[f"weather.d{i}.wind"] = 6 + 3 * i
DATA["weather.wind_unit"] = "mph"
DATA.update({"bambu.connected": 1, "bambu.state": "RUNNING", "bambu.state_text": "Printing", "bambu.job": "Benchy_0.2mm_PLA",
             "bambu.percent": 63, "bambu.layer": 118, "bambu.total_layers": 187, "bambu.remaining_min": 42,
             "bambu.remaining_text": "42m", "bambu.eta": now + 42 * 60, "bambu.nozzle": 219.6, "bambu.bed": 65.1})
for i in range(24):
    DATA[f"weather.h{i}.t"] = now + i * 3600
    DATA[f"weather.h{i}.temp"] = 45 + (i % 6)
    DATA[f"weather.h{i}.icon"] = icons[i % len(icons)]
MIME = {".html": "text/html", ".js": "application/javascript", ".css": "text/css", ".woff2": "font/woff2"}


def masked():
    s = json.loads(json.dumps(SETTINGS))
    for a, b in (("wifi", "pass"), ("admin", "pass"), ("api", "token")):
        if s[a][b]:
            s[a][b] = "********"
    return s


def merge(dst, src):
    for k, v in src.items():
        if v == "********":
            continue
        if isinstance(v, dict) and isinstance(dst.get(k), dict):
            merge(dst[k], v)
        else:
            dst[k] = v


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def send(self, code, body, ctype="application/json"):
        b = body if isinstance(body, bytes) else (json.dumps(body) if not isinstance(body, str) else body).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def body(self):
        n = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(n) if n else b""
        try:
            return json.loads(raw or b"{}")
        except Exception:
            return raw

    def do_GET(self):
        p = self.path.split("?")[0]
        enabled = [pg for pg in LAYOUT["pages"] if pg.get("enabled", True)]
        if p == "/api/status":
            return self.send(200, {
                "name": SETTINGS["device"]["name"], "version": "1.0.0", "build": "Nov 21 2026 22:40:00", "uptime_s": int(time.time() - START),
                "heap": 180000, "heap_min": 150000, "psram": 7_500_000,
                "net": {"connected": True, "ap": False, "ip": "192.168.1.42", "ssid": "HomeNet", "rssi": -58, "host": "deskdisplay", "mac": "AA:BB:CC:DD:EE:FF"},
                "battery": {"present": False, "usb": True, "charging": False, "percent": -1, "mv": 0},
                "ui": {"page": STATE["page"], "pages": len(enabled), "page_name": enabled[STATE["page"]]["name"], "sleeping": STATE["sleep"], "night": STATE["night"]},
                "input": {"last": STATE["last"], "age_ms": int((time.time() - STATE["last_t"]) * 1000), "idle_ms": 5000, "roll": random.uniform(-3, 3),
                          "pitch": random.uniform(-3, 3), "angle": 2.1, "motion_g": 0.01, "imu": True},
                "time": int(time.time()), "time_valid": True, "display": {"fps": 31.5, "mbps": 13.2, "bus_busy_pct": 64}, "sound": "ready", "partition": "app0", "ota_state": "valid",
                "has_admin_pass": bool(SETTINGS["admin"]["pass"]), "api_token": SETTINGS["api"]["token"]})
        if p == "/api/govee/devices":
            return self.send(200, {"devices": [{"id": "AA:BB:CC:DD:EE:FF:00:11", "sku": "H6008", "ip": "192.168.1.61", "name": SETTINGS.get("govee", {}).get("names", {}).get("AA:BB:CC:DD:EE:FF:00:11", ""), "age_s": 12},
                                               {"id": "AA:BB:CC:DD:EE:FF:00:22", "sku": "H6008", "ip": "192.168.1.62", "name": "", "age_s": 12}]})
        if p == "/api/media":
            files = []
            for nm, data in MEDIA.items():
                w, hh, n = struct.unpack_from("<HHH", data, 4)
                delays = [struct.unpack_from("<IIH", data, 12 + i * 12)[2] for i in range(n)]
                files.append({"name": nm, "size": len(data), "w": w, "h": hh, "frames": n, "seconds": sum(delays) / 1000})
            return self.send(200, {"sd": {"mounted": True, "type": "SD (1-bit)", "total": 31_900_000_000, "used": 1_200_000_000 + sum(map(len, MEDIA.values()))}, "files": files})
        if p == "/api/media/frame":
            q = urllib.parse.parse_qs(self.path.split("?", 1)[1] if "?" in self.path else "")
            data = MEDIA.get(q.get("name", [""])[0])
            if not data:
                return self.send(404, {"ok": False, "error": "not found"})
            fr = int(q.get("frame", ["0"])[0])
            off, size = struct.unpack_from("<II", data, 12 + fr * 12)
            return self.send(200, data[off:off + size], "image/jpeg")
        if p == "/api/settings":
            return self.send(200, masked())
        if p == "/api/layout":
            return self.send(200, LAYOUT)
        if p == "/api/layout/default":
            return self.send(200, DEFAULT_LAYOUT)
        if p == "/api/data":
            return self.send(200, {"data": {k: {"v": v, "t": now} for k, v in DATA.items()}, "version": 1})
        if p == "/api/logs":
            return self.send(200, "+0s DeskDisplay 1.0.0 starting\n+1s wifi: connected\n", "text/plain")
        if p == "/api/wifi/scan":
            return self.send(200, {"networks": [{"ssid": "HomeNet", "rssi": -50, "open": False}, {"ssid": "Neighbor", "rssi": -80, "open": False}]})
        if p == "/api/sources/test":
            return self.send(200, {"ok": True, "keys": {"cpu.temp": 51.2, "disk.used_pct": 63}})
        if p == "/api/update/status":
            return self.send(200, {"status": "idle", "current": "1.1.0", "latest": "1.1.1", "available": True, "checked_at": int(time.time()) - 600,
                                   "notes": "Fixes and improvements", "check_error": "", "checking": False, "latest_url": "x"})
        if p == "/api/backup":
            return self.send(200, {"type": "deskdisplay-backup", "settings": masked(), "layout": LAYOUT})
        if p == "/":
            p = "/index.html"
        f = os.path.join(WWW, p.lstrip("/"))
        if os.path.isfile(f):
            return self.send(200, open(f, "rb").read(), MIME.get(os.path.splitext(f)[1], "application/octet-stream"))
        self.send(404, {"ok": False, "error": "not found"})

    def do_POST(self):
        p = self.path.split("?")[0]
        b = self.body()
        global LAYOUT
        if p == "/api/settings":
            merge(SETTINGS, b)
        elif p == "/api/layout":
            LAYOUT = b
        elif p == "/api/action":
            enabled = [pg for pg in LAYOUT["pages"] if pg.get("enabled", True)]
            a = b.get("action")
            if a == "next":
                STATE["page"] = (STATE["page"] + 1) % len(enabled)
            elif a == "prev":
                STATE["page"] = (STATE["page"] - 1) % len(enabled)
            elif a == "goto":
                STATE["page"] = b.get("page", 0) % len(enabled)
            elif a == "home":
                STATE["page"] = LAYOUT.get("home", 0)
            elif a == "sleep_toggle":
                STATE["sleep"] = not STATE["sleep"]
            elif a == "night_toggle":
                STATE["night"] = not STATE["night"]
        elif p == "/api/update/install-latest":
            STATE["upd"] = "up to date (v1.3.0 is the newest release)"
        elif p == "/api/media/upload":
            q = urllib.parse.parse_qs(self.path.split("?", 1)[1] if "?" in self.path else "")
            msg = email.message_from_bytes(b"Content-Type: " + self.headers["Content-Type"].encode() + b"\r\n\r\n" + b)
            for part in msg.walk():
                if part.get_filename():
                    MEDIA[q.get("name", ["x"])[0]] = part.get_payload(decode=True)
        elif p == "/api/media/delete":
            MEDIA.pop(b.get("name"), None)
        elif p == "/api/usage":
            rl = b.get("rate_limits", {})
            for w, v in rl.items():
                DATA[f"claude.{w}.used_percentage"] = v.get("used_percentage", 0)
                DATA[f"claude.{w}.resets_at"] = v.get("resets_at", 0)
            DATA["claude.updated"] = int(time.time())
        self.send(200, {"ok": True})


MEDIA = {}
START = time.time()
if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    print(f"mock device on http://localhost:{port}/")
    ThreadingHTTPServer(("0.0.0.0", port), H).serve_forever()

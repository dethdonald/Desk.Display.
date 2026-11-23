#!/usr/bin/env python3
"""Claude plan usage -> Desk Display, polled from an always-on machine.

Runs as a background service (e.g. on a home server). Every few minutes it asks Anthropic's
usage endpoint for your 5-hour and weekly limits (the same numbers /usage shows in Claude Code)
and pushes them to the display's /api/usage. Works whether or not Claude Code is open anywhere.

Sign-in: uses the Claude Code login on this machine (~/.claude/.credentials.json). Run `claude`
once and log in with your Claude account. When the access token expires the script refreshes it
and writes the new one back, so Claude Code on this machine stays signed in too.

NOTE: the usage endpoint is not officially documented; if Anthropic changes it this may stop
working (the display then shows the numbers going stale rather than wrong values).

Usage:
  claude-usage-poller.py --url http://deskdisplay.local/api/usage --token DISPLAY_TOKEN
  claude-usage-poller.py ... --once      # one poll, print the result, exit (for testing)

Options (or environment variables):
  --url       DESKDISPLAY_URL     display endpoint
  --token     DESKDISPLAY_TOKEN   API token from the display's Settings -> Security
  --interval  POLL_INTERVAL       seconds between polls (default 180, minimum 60)
  --creds     CLAUDE_CREDENTIALS  credentials file (default ~/.claude/.credentials.json)

Standard library only.
"""
import argparse, json, os, sys, tempfile, time, urllib.error, urllib.request

USAGE_URL = "https://api.anthropic.com/api/oauth/usage"
TOKEN_URL = "https://console.anthropic.com/v1/oauth/token"
CLIENT_ID = "9d1c250a-e61b-44d9-88ed-5944d1962f5e"   # Claude Code's public OAuth client id
BETA = "oauth-2025-04-20"
UA = "deskdisplay-usage-poller/1.0"


def log(msg):
    print(time.strftime("%Y-%m-%d %H:%M:%S ") + msg, flush=True)


def http_json(url, data=None, headers=None, timeout=15):
    body = json.dumps(data).encode() if data is not None else None
    h = {"User-Agent": UA, "Accept": "application/json"}
    if body is not None:
        h["Content-Type"] = "application/json"
    h.update(headers or {})
    req = urllib.request.Request(url, data=body, headers=h, method="POST" if body is not None else "GET")
    with urllib.request.urlopen(req, timeout=timeout) as r:
        raw = r.read().decode() or "{}"
        return json.loads(raw)


class Creds:
    def __init__(self, path):
        self.path = os.path.expanduser(path)

    def load(self):
        with open(self.path, encoding="utf-8") as f:
            self.doc = json.load(f)
        return self.doc.get("claudeAiOauth") or {}

    def save(self, oauth):
        self.doc["claudeAiOauth"] = oauth
        d = os.path.dirname(self.path)
        fd, tmp = tempfile.mkstemp(dir=d, prefix=".credentials.")
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            json.dump(self.doc, f)
        os.chmod(tmp, 0o600)
        os.replace(tmp, self.path)

    def access_token(self, force_refresh=False):
        o = self.load()
        if not o.get("accessToken"):
            raise RuntimeError(f"no Claude login in {self.path} - run `claude` and sign in")
        expires_ms = o.get("expiresAt") or 0
        if force_refresh or (expires_ms and expires_ms / 1000 < time.time() + 120):
            if not o.get("refreshToken"):
                raise RuntimeError("access token expired and no refresh token - run `claude` and sign in again")
            r = http_json(TOKEN_URL, {"grant_type": "refresh_token", "refresh_token": o["refreshToken"], "client_id": CLIENT_ID})
            o["accessToken"] = r["access_token"]
            if r.get("refresh_token"):
                o["refreshToken"] = r["refresh_token"]
            if r.get("expires_in"):
                o["expiresAt"] = int((time.time() + int(r["expires_in"])) * 1000)
            self.save(o)
            log("refreshed Claude sign-in token")
        return o["accessToken"]


def get_usage(creds):
    for attempt in (0, 1):
        tok = creds.access_token(force_refresh=attempt == 1)
        try:
            return http_json(USAGE_URL, headers={"Authorization": f"Bearer {tok}", "anthropic-beta": BETA})
        except urllib.error.HTTPError as e:
            if e.code == 401 and attempt == 0:
                continue
            raise


def push(url, token, usage):
    rl = {k: v for k, v in usage.items() if isinstance(v, dict) and ("utilization" in v)}
    if not rl:
        raise RuntimeError(f"usage response had no limits: {json.dumps(usage)[:200]}")
    payload = {"source": "usage-poller", "rate_limits": rl}
    http_json(url, payload, {"X-Api-Token": token}, timeout=10)
    h5 = (rl.get("five_hour") or {}).get("utilization")
    wk = (rl.get("seven_day") or {}).get("utilization")
    return h5, wk


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--url", default=os.environ.get("DESKDISPLAY_URL", "http://deskdisplay.local/api/usage"))
    ap.add_argument("--token", default=os.environ.get("DESKDISPLAY_TOKEN", ""))
    ap.add_argument("--interval", type=int, default=int(os.environ.get("POLL_INTERVAL", "180")))
    ap.add_argument("--creds", default=os.environ.get("CLAUDE_CREDENTIALS", "~/.claude/.credentials.json"))
    ap.add_argument("--once", action="store_true")
    a = ap.parse_args()
    if not a.url.rstrip("/").endswith("/api/usage"):
        a.url = a.url.rstrip("/") + "/api/usage"
    creds = Creds(a.creds)
    interval = max(60, a.interval)
    delay = interval
    while True:
        try:
            usage = get_usage(creds)
            try:
                h5, wk = push(a.url, a.token, usage)
                log(f"ok: 5h {h5}%, week {wk}% -> {a.url}")
            except urllib.error.HTTPError as e:
                log(f"display refused the update: HTTP {e.code} {e.read()[:120]!r} (check --token)")
            except (urllib.error.URLError, OSError) as e:
                log(f"display not reachable at {a.url}: {e}")
            delay = interval
        except urllib.error.HTTPError as e:
            if e.code == 429:
                delay = min(delay * 2, 1800)
                log(f"Anthropic rate-limited the usage check, next try in {delay}s")
            else:
                log(f"usage check failed: HTTP {e.code} {e.read()[:160]!r}")
                delay = interval
        except Exception as e:
            log(f"usage check failed: {e}")
            delay = interval
        if a.once:
            return
        time.sleep(delay)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass

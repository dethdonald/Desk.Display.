#!/usr/bin/env python3
"""Claude Code status line -> Desk Display.

Claude Code runs this as its status line command and pipes a JSON snapshot to stdin
(model, context window, and your plan's rate_limits.five_hour / seven_day windows).
This script prints a short status line for the terminal and forwards the numbers to the
display in the background, so it never slows Claude Code down.

Setup (~/.claude/settings.json):
  "statusLine": {
    "type": "command",
    "command": "python3 ~/.claude/deskdisplay-statusline.py --url http://deskdisplay.local/api/usage --token YOUR_TOKEN"
  }

Options:
  --url URL        display endpoint (or env DESKDISPLAY_URL)
  --token TOKEN    API token from the display's Settings -> Security (or env DESKDISPLAY_TOKEN)
  --chain "CMD"    run your existing status line command with the same input and print its output instead
  --min-interval S minimum seconds between pushes when nothing changed (default 30)
  --quiet          print nothing (use when chaining isn't needed and you want an empty status line)

Standard library only; works on macOS, Linux and Windows.
"""
import argparse, hashlib, json, os, subprocess, sys, tempfile, time, urllib.request

STATE = os.path.join(tempfile.gettempdir(), "deskdisplay-statusline.json")


def build_payload(data):
    out = {"source": "claude-code"}
    if isinstance(data.get("rate_limits"), dict):
        out["rate_limits"] = data["rate_limits"]
    cw = (data.get("context_window") or {}).get("used_percentage")
    if cw is not None:
        out["context_window"] = {"used_percentage": cw}
    name = (data.get("model") or {}).get("display_name")
    if name:
        out["model"] = {"display_name": name}
    cost = (data.get("cost") or {}).get("total_cost_usd")
    if cost is not None:
        out["cost"] = {"total_cost_usd": cost}
    return out


def should_send(payload, min_interval):
    digest = hashlib.sha1(json.dumps(payload, sort_keys=True).encode()).hexdigest()
    try:
        with open(STATE) as f:
            st = json.load(f)
    except Exception:
        st = {}
    now = time.time()
    if st.get("digest") == digest and now - st.get("t", 0) < min_interval:
        return False
    try:
        with open(STATE, "w") as f:
            json.dump({"digest": digest, "t": now}, f)
    except Exception:
        pass
    return True


def send(url, token, payload):
    req = urllib.request.Request(url, data=json.dumps(payload).encode(), method="POST",
                                 headers={"Content-Type": "application/json", "X-Api-Token": token or ""})
    try:
        urllib.request.urlopen(req, timeout=3).read()
    except Exception:
        pass  # display offline/asleep: never disturb Claude Code


def send_in_background(url, token, payload):
    if hasattr(os, "fork"):
        try:
            if os.fork() == 0:          # child: detach and send
                try:
                    os.setsid()
                    sys.stdout.close()
                    send(url, token, payload)
                finally:
                    os._exit(0)
            return
        except OSError:
            pass
    # Windows / no fork: start a detached copy of ourselves to do the request
    flags = getattr(subprocess, "DETACHED_PROCESS", 0) | getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)
    try:
        p = subprocess.Popen([sys.executable, os.path.abspath(__file__), "--_send", url, token or ""],
                             stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=flags)
        p.stdin.write(json.dumps(payload).encode())
        p.stdin.close()
    except Exception:
        send(url, token, payload)


def default_line(data):
    parts = []
    name = (data.get("model") or {}).get("display_name")
    if name:
        parts.append(name)
    cw = (data.get("context_window") or {}).get("used_percentage")
    if cw is not None:
        parts.append(f"ctx {round(cw)}%")
    rl = data.get("rate_limits") or {}
    if (rl.get("five_hour") or {}).get("used_percentage") is not None:
        parts.append(f"5h {round(rl['five_hour']['used_percentage'])}%")
    if (rl.get("seven_day") or {}).get("used_percentage") is not None:
        parts.append(f"wk {round(rl['seven_day']['used_percentage'])}%")
    return " · ".join(parts)


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "--_send":
        send(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else "", json.loads(sys.stdin.read() or "{}"))
        return
    ap = argparse.ArgumentParser(description="Forward Claude Code status line data to a Desk Display")
    ap.add_argument("--url", default=os.environ.get("DESKDISPLAY_URL", "http://deskdisplay.local/api/usage"))
    ap.add_argument("--token", default=os.environ.get("DESKDISPLAY_TOKEN", ""))
    ap.add_argument("--chain", default=os.environ.get("DESKDISPLAY_CHAIN", ""))
    ap.add_argument("--min-interval", type=float, default=30)
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    raw = sys.stdin.read()
    try:
        data = json.loads(raw) if raw.strip() else {}
    except json.JSONDecodeError:
        data = {}

    # 1) status line output first, so the terminal never waits on the network
    if args.chain:
        try:
            r = subprocess.run(args.chain, shell=True, input=raw, capture_output=True, text=True, timeout=5)
            sys.stdout.write(r.stdout)
        except Exception:
            pass
    elif not args.quiet:
        sys.stdout.write(default_line(data))
    sys.stdout.flush()

    # 2) forward to the display
    payload = build_payload(data)
    if ("rate_limits" in payload or "context_window" in payload) and should_send(payload, args.min_interval):
        send_in_background(args.url, args.token, payload)


if __name__ == "__main__":
    main()

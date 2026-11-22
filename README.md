# Desk Display

Firmware and web app for the **Waveshare ESP32-S3-Touch-AMOLED-2.16** (480×480 AMOLED, touch, motion sensor, battery support).
It shows the time, your Claude plan usage and the weather. Every screen is designed in a web page hosted by the display itself.

- **Layout designer:** drag and drop widgets on a 12×12 grid, with as many pages as you like (up to 16), day and night themes, live preview, undo, templates, and import/export.
- **Your choice of inputs:** bind touch gestures, tilt/shake/flip/pick-up, and the BOOT/PWR buttons to next page, previous page, home, sleep, night mode, QR and refresh.
- **Settings, maintenance and updates over your network:** Wi-Fi, time zone, weather location, night schedule, logs, backups, and firmware updates with automatic rollback.
- **Built to expand:** any JSON URL (or any script that pushes data) becomes `{keys}` you can put on screen.

---

## 1. First flash (USB, once)

Every later update can go over Wi-Fi.

**Option A: browser, no tools to install**
1. Connect the board over USB-C. If it isn't detected, hold **BOOT**, tap **RST** (or replug the cable), then release BOOT.
2. Open https://espressif.github.io/esptool-js/ in Chrome or Edge, click **Connect**, and pick the board.
3. Flash `firmware.factory.bin` at address **0x0**.

**Option B: PlatformIO (needed if you change the code)**
```bash
pip install platformio
cd deskdisplay
pio run -t upload            # build + flash over USB
pio device monitor           # serial log (115200)
```
Everything the build needs is vendored in `lib/`. The Arduino-ESP32 core version is pinned in `platformio.ini`.

## 2. First boot

1. The display can't find any Wi-Fi yet, so it opens a network called **`DeskDisplay-Setup-XXXX`**.
2. Join it from your phone. The settings page should open on its own; if it doesn't, go to `http://192.168.4.1`.
3. Go to **Settings → Wi-Fi**, scan, pick your network and save. The display joins your Wi-Fi and the setup network closes.
4. From then on, open **http://deskdisplay.local/**. You can also long-press the screen to show a QR code with its IP address.
5. Recommended next steps:
   - **Settings → Security:** set an admin password.
   - **Settings → Weather:** search for your location.
   - **Settings → Claude usage:** set up the push script (section 3).

## 3. Claude usage

Short version: open the display's web page → **Settings → Claude usage**. It shows the exact block to paste, with your display's address and token already filled in.

Claude Code passes your plan limits (`rate_limits.five_hour` and `rate_limits.seven_day`) to its status line command.
`tools/deskdisplay-statusline.py` forwards those numbers to the display. It runs in the background, so it never slows the terminal down.

1. Put `tools/deskdisplay-statusline.py` in your `.claude` folder:
   - macOS/Linux: `~/.claude/`
   - Windows: `C:\Users\YOU\.claude\`

   It's also attached to every GitHub Release.
2. Add a `statusLine` block to `.claude/settings.json`. Merge it in if the file already has other settings. Then restart Claude Code.

macOS / Linux:
```json
{
  "statusLine": {
    "type": "command",
    "command": "python3 ~/.claude/deskdisplay-statusline.py --url http://DISPLAY_IP/api/usage --token YOUR_TOKEN"
  }
}
```
Windows (use forward slashes, and `python` instead of `python3`):
```json
{
  "statusLine": {
    "type": "command",
    "command": "python C:/Users/YOU/.claude/deskdisplay-statusline.py --url http://DISPLAY_IP/api/usage --token YOUR_TOKEN"
  }
}
```
Reserve the display's IP address in your router so it doesn't change.
The Settings page shows this exact command with your token filled in, plus a **Send test data** button.

- **Already have a status line?** Add `--chain "your existing command"`. The script prints that command's output and still pushes to the display.
- **Several machines** (desktop, home server, laptop) can all push. The display keeps the newest numbers.
- **When numbers update:** only while Claude Code is running. Usage on claude.ai or in the apps counts toward the same limits, and it shows up with your next Claude Code message. Once a window's reset time passes, the display shows it as 0%.

## 4. Designing screens (Layout tab)

- **Pages:** add, duplicate, reorder, enable or disable, rename, choose the home page, and set each page's auto-rotate time.
- **Widgets:**

  | Widget | What it shows |
  |---|---|
  | `clock` | Time, with optional AM/PM and seconds |
  | `date` | Date in a strftime format, e.g. `%A, %b %e` |
  | `text` | Any text with `{templates}` |
  | `value` | A big number plus a label, optionally colored by threshold |
  | `bar` | Progress bar with label, value and sub-text |
  | `ring` | Arc gauge. Stack two for concentric rings |
  | `weather` | Icon plus current temperature |
  | `forecast` | Daily or hourly forecast columns |
  | `icon` | A weather glyph, fixed or taken from data |
  | `box` | A background panel |
  | `qr` | QR code linking to the settings page |
- **Grid:** 12×12 cells of 40 px each. Drag to move, use the orange corner to resize, arrow keys to nudge, Shift+arrows to resize, Delete to remove, Ctrl+D to duplicate, Ctrl+Z/Y to undo/redo.
- **Colors:** pick theme names (`fg`, `muted`, `accent`, `ok`, `warn`, `bad`, `track`, `bg`) or any hex color. The night theme has its own palette.
- **Preview on device:** shows your design on the display without saving it. **Save to device** keeps it.
- **Fonts:** Montserrat at 12–64 px, plus 96 and 128 px. The 96/128 px sizes contain only digits and `: . % - + ° A P M F C`, which is meant for clocks and big numbers.

### Templates
Text-type fields accept `{key}` placeholders. Format them with `{key:0}` for whole numbers, or one of these:

| Format | Shows |
|---|---|
| `{key\|pct}` | A percentage |
| `{key\|in}` | Time until ("2h 13m") |
| `{key\|ago}` | Time since |
| `{key\|time}` | Clock time |
| `{key\|day}` | Weekday |
| `{key\|date}` | Date |
| `{key\|hour}` | Hour |
| `{key\|upper}` | Upper-case text |

Icons: `{sym:wifi}` and `{sym:battery}` (the battery icon follows the battery level). Also `charge`, `bell`, `home`, `gear`, `power`, `clock`, `cloud`, `fire`, `warn`.

Built-in keys (all of them are listed live under **Maintenance → Live data**):
- **Claude:** `claude.five_hour.used_percentage`, `claude.five_hour.resets_at`, `claude.seven_day.*`, `claude.context_window.used_percentage`, `claude.model.display_name`, `claude.updated`, `claude.stale`
- **Weather now:** `weather.temp`, `feels`, `humidity`, `wind`, `desc`, `icon`, `hi`, `lo`, `pp` (rain %), `sunrise`, `sunset`, `place`
- **Daily forecast:** `weather.d0…d5.{t,hi,lo,icon,desc,pp}`
- **Hourly forecast:** `weather.h0…h23.{t,temp,icon,pp}`
- **System:** `sys.ip`, `sys.url`, `sys.rssi`, `sys.battery`, `sys.battery_text`, `sys.uptime_text`, `sys.page_name`

## 5. Controls tab

Each action gets any number of inputs:

| Touch | Motion | Buttons |
|---|---|---|
| tap, double tap, long press | tilt left/right/forward/back | BOOT press |
| swipe left/right/up/down | shake | BOOT hold |
| tap left edge, tap right edge | flip face-down, pick up | PWR press |

- **Live readout:** the page shows the last input the display detected and live tilt angles, so you can tune thresholds while holding it.
- **Calibrate resting angle:** put the display where it normally sits, then press this. Tilts are measured from that position.
- **Waking:** any touch, button or pick-up wakes a sleeping screen without triggering an action.
- **Guardrail:** if nothing is bound to next or previous page and auto-rotate is off, swipe left/right stays active so you can't get stuck.
- **Automation:** auto-rotate, return home after a period of no input, sleep after a period of no input, transition style, and page dots.

## 6. Extending it

- **Pull:** under **Settings → Extra data sources**, add a JSON URL (for example a stats endpoint on your CasaOS server). Every value becomes `{prefix.path.to.value}`. The **Test** button lists the keys it found.
- **Push:** send data from any script.
  ```bash
  curl -X POST http://deskdisplay.local/api/data -H "X-Api-Token: TOKEN" \
       -H "Content-Type: application/json" -d '{"server": {"cpu": 12, "disk": 63}}'
  ```
  The values become `{server.cpu}` and `{server.disk}`.
- **New widget types:** add a builder in `src/ui.cpp` (`buildWidget` / `updateWidget`) and its preview and properties in `www/app.js` (`renderWidget`, `WIDGETS`, `propSchema`).

### HTTP API

**Admin endpoints:** these use HTTP Basic auth once an admin password is set.

| Method | Path | Purpose |
|---|---|---|
| GET | `/api/status` | Device status |
| GET / POST | `/api/settings` | Read settings / apply a partial patch |
| GET / POST | `/api/layout` | Read / save the layout |
| POST | `/api/layout/preview` | Show a layout without saving it |
| POST | `/api/layout/revert` | Go back to the saved layout |
| GET | `/api/data` | Every data key |
| POST | `/api/action` | `{"action":"next"}`; also `goto`+`page`, `sleep`, `wake`, `night_on`, `night_off`, … |
| POST | `/api/calibrate` | Save the resting angle |
| GET | `/api/logs` | Log text |
| GET | `/api/backup` | Download a backup |
| POST | `/api/restore` | Restore a backup |
| POST | `/api/reboot` | Restart |
| POST | `/api/factory-reset` | Erase settings and layout |
| GET | `/api/wifi/scan` | Nearby networks |
| POST / GET | `/api/sources/test` | Test a data source URL |
| POST | `/api/update` | Upload a firmware `.bin` (multipart) |
| POST | `/api/update/url` | Install firmware from a URL |
| GET | `/api/update/status` | Progress of a URL update |

**Push endpoints:** these use the `X-Api-Token` header (or `?token=`).

| Method | Path | Purpose |
|---|---|---|
| POST | `/api/usage` | Claude Code status line JSON, or the `/usage` API shape |
| POST | `/api/data` | Any JSON, flattened into keys |

## 7. Updates and recovery

**Releases from GitHub (recommended)**
1. Change code, then commit and push.
2. Create a version tag and push it:
   ```bash
   git tag v1.2.0
   git push origin v1.2.0
   ```
   You can also go to **Actions → Release firmware → Run workflow** on GitHub and type a version.
3. GitHub Actions builds the firmware and publishes a Release with these files:
   - `deskdisplay-ota.bin`
   - `deskdisplay-factory.bin`
   - `version.json`
   - the status line script
4. The display picks up the new release in one of three ways:
   - it checks once a day on its own
   - **Update → Check now**
   - automatic install, if you enable **Install automatically**

The version comes from the tag, so `src/config.h` doesn't need editing for releases.

**Other ways to update**

- **Updates:**
  - **Update tab → Upload a file:** upload `.pio/build/amoled216/firmware.bin`.
  - **Update tab → Install from a URL:** the display downloads the `.bin` itself.
  - The web UI is built into the firmware, so one file updates everything. Settings and layout are kept.
- **Automatic rollback:** a new firmware has to run for 60 seconds without crashing before it's marked good. If it crashes or reboots before then, the display goes back to the previous version.
- **Crash report:** if the firmware crashes, the next boot shows a crash report with the step it reached and the crash addresses. After two crashes in a row it starts **safe mode**: only Wi-Fi and the web page run, so you can still upload a fixed firmware. To turn the addresses into code locations:
  ```bash
  xtensa-esp32s3-elf-addr2line -pfiaC -e .pio/build/amoled216/firmware.elf ADDR...
  ```
  This needs the `.elf` from the same build that crashed.
- **Forgot the admin password:** hold **BOOT** for 10 seconds while the display is running.
- **Factory reset:** Maintenance tab, either keeping Wi-Fi or erasing everything.
- **Bricked:** re-flash over USB (section 1).
- **Backups:** Maintenance → Download backup. Restore from the same page.

## 8. Troubleshooting

- **Taps land in the wrong place:** Controls → Touch tuning → Touch orientation (swap/mirror).
- **Tilt left/right is backwards:** Controls → Motion tuning → Swap tilt left/right.
- **Can't find `deskdisplay.local`:** some Android phones and Windows setups don't resolve `.local` names. Long-press the screen to see the IP address, or check your router.
- **Weather says "Set location":** Settings → Weather → Search.
- **Claude usage looks old:** it only updates while Claude Code is running. Check the command in `~/.claude/settings.json`, then use **Send test data** on the Settings page.
- **Battery:** the board takes a 3.7 V LiPo on its MX1.25 connector. Plan on staying plugged in most of the time, and use screen sleep (flip it over, or set an idle timer) to stretch battery life.

## Project layout
```
platformio.ini, partitions.csv   build config (16 MB flash: 2 × 4 MB app slots for OTA + 7.9 MB config FS)
src/main.cpp        setup/loop, command queue, night schedule, OTA verification
src/hw.*            display (CO5300 QSPI), LVGL, touch (CST9220), PMU (AXP2101), IMU (QMI8658), RTC (PCF85063)
src/ui.*            layout engine: builds LVGL pages from layout.json, widgets, transitions, overlays
src/input.*         gestures, motion and buttons -> events -> actions
src/datastore.*     key/value store + template engine
src/fetcher.*       weather, custom sources, Claude data intake, URL updates (background task)
src/net.*           Wi-Fi, setup network + captive portal, mDNS, NTP, RTC sync
src/web.*           HTTP server + REST API (UI files embedded from www/ at build time)
src/fonts/          generated fonts (Montserrat; Weather Icons by Erik Flowers, SIL OFL 1.1)
www/                the web app (vanilla JS, no build step)
tools/embed_www.py  gzips www/ into the firmware during the build
tools/mock_device.py   fake device API for working on the web UI without hardware
tools/deskdisplay-statusline.py   Claude Code status line -> display
```
To work on the web UI without hardware, run `python3 tools/mock_device.py` and open http://localhost:8080.

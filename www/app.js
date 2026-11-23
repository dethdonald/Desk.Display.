"use strict";
/* Desk Display web UI — no build step, no external dependencies (works on the setup network too). */

// ============================================================ basics
const $ = (s, el = document) => el.querySelector(s);
const $$ = (s, el = document) => [...el.querySelectorAll(s)];
const clone = o => JSON.parse(JSON.stringify(o));
const esc = s => String(s ?? "").replace(/[&<>"']/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
function h(tag, attrs = {}, ...kids) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v === undefined || v === null || v === false) continue;
    if (k === "class") el.className = v;
    else if (k === "style" && typeof v === "object") Object.assign(el.style, v);
    else if (k.startsWith("on")) el.addEventListener(k.slice(2), v);
    else if (k === "html") el.innerHTML = v;
    else if (k in el && k !== "list") { try { el[k] = v; } catch { el.setAttribute(k, v); } }
    else el.setAttribute(k, v === true ? "" : v);
  }
  for (const kid of kids.flat()) if (kid !== null && kid !== undefined && kid !== false) el.append(kid.nodeType ? kid : document.createTextNode(kid));
  return el;
}
let toastTimer;
function toast(msg, err = false) {
  const t = $("#toast");
  t.textContent = msg; t.className = "toast show" + (err ? " err" : "");
  clearTimeout(toastTimer); toastTimer = setTimeout(() => t.className = "toast", 2800);
}
async function api(path, opts = {}) {
  const o = { method: opts.method || (opts.body !== undefined ? "POST" : "GET"), headers: {} };
  if (opts.body !== undefined) { o.headers["Content-Type"] = "application/json"; o.body = JSON.stringify(opts.body); }
  const r = await fetch(path, o);
  const txt = await r.text();
  let data; try { data = txt ? JSON.parse(txt) : {}; } catch { data = txt; }
  if (!r.ok && r.status !== 202) { const e = new Error((data && data.error) || ("HTTP " + r.status)); e.status = r.status; throw e; }
  if (r.status === 202) data = Object.assign({ _pending: true }, data);
  return data;
}
const sleep = ms => new Promise(r => setTimeout(r, ms));
const get = (o, path, d) => path.split(".").reduce((a, k) => (a && a[k] !== undefined ? a[k] : undefined), o) ?? d;
function set(o, path, v) { const ks = path.split("."); let c = o; ks.slice(0, -1).forEach(k => { if (typeof c[k] !== "object" || c[k] === null) c[k] = {}; c = c[k]; }); c[ks.at(-1)] = v; }

// ============================================================ state
const S = { status: {}, settings: null, layout: null, data: {}, dataVer: -1, tab: "dash" };

// ============================================================ template engine (mirrors firmware datastore.cpp)
const SYMS = { wifi: "", bat_full: "", bat_3: "", bat_half: "", bat_1: "", bat_empty: "", charge: "",
  refresh: "", bell: "", home: "", gear: "", power: "", clock: "", cloud: "", fire: "", warn: "" };
const WX_GLYPHS = { sunny: "", "clear-night": "", "partly-cloudy": "", "partly-cloudy-night": "", cloudy: "", fog: "",
  drizzle: "", rain: "", snow: "", thunder: "", sleet: "", showers: "", "rain-mix": "",
  thermometer: "", raindrop: "", wind: "", sunrise: "", sunset: "", humidity: "", barometer: "" };
function fmtNum(v, d) {
  if (d === undefined || d < 0) return Math.abs(v - Math.round(v)) < 0.05 ? String(Math.round(v)) : v.toFixed(1);
  const s = v.toFixed(d); return s === "-0" ? "0" : s;
}
function fmtDur(s) {
  if (s <= 0) return "now";
  const d = Math.floor(s / 86400), hh = Math.floor((s % 86400) / 3600), m = Math.floor((s % 3600) / 60);
  return d > 0 ? `${d}d ${hh}h` : hh > 0 ? `${hh}h ${m}m` : m > 0 ? `${m}m` : `${Math.floor(s)}s`;
}
function h24() { return !!get(S.settings, "time.h24", false); }
function tzName() { return get(S.settings, "time.tz_name", undefined); }
function fmtTime(epoch, opts) {
  const o = Object.assign({}, opts);
  try { if (tzName()) o.timeZone = tzName(); return new Date(epoch * 1000).toLocaleString("en-US", o); }
  catch { delete o.timeZone; return new Date(epoch * 1000).toLocaleString("en-US", o); }
}
function renderTmpl(tmpl, data = S.data) {
  return String(tmpl ?? "").replace(/\{([^}]*)\}/g, (_, tok) => {
    if (tok.startsWith("sym:")) {
      let n = tok.slice(4);
      if (n === "battery") { const p = data["sys.battery"], c = data["sys.charging"]; n = c > 0 ? "charge" : p === undefined || p < 0 ? "bat_full" : p > 85 ? "bat_full" : p > 60 ? "bat_3" : p > 35 ? "bat_half" : p > 12 ? "bat_1" : "bat_empty"; }
      return SYMS[n] || "";
    }
    let key = tok, filter = "", dec;
    const bar = key.indexOf("|"); if (bar >= 0) { filter = key.slice(bar + 1).trim(); key = key.slice(0, bar); }
    const col = key.indexOf(":"); if (col >= 0) { dec = parseInt(key.slice(col + 1)); key = key.slice(0, col); }
    key = key.trim();
    if (!(key in data)) return "--";
    let v = data[key];
    if (typeof v !== "number" && filter) { const n = Number(v); if (v !== "" && !isNaN(n)) v = n; else { const t = Date.parse(v); if (!isNaN(t)) v = t / 1000; } }
    if (!filter) return typeof v === "number" ? fmtNum(v, dec) : String(v);
    if (filter === "upper") return (typeof v === "number" ? fmtNum(v, dec) : String(v)).toUpperCase();
    if (typeof v !== "number") return String(v);
    if (filter === "pct") return fmtNum(v, dec === undefined ? 0 : dec) + "%";
    let t = v > 1e11 ? v / 1000 : v;
    const now = Date.now() / 1000;
    if (filter === "in") return fmtDur(t - now);
    if (filter === "ago") { const d = now - t; return d < 60 ? "now" : fmtDur(d) + " ago"; }
    if (filter === "time") return fmtTime(t, { hour: h24() ? "2-digit" : "numeric", minute: "2-digit", hour12: !h24() });
    if (filter === "day") return fmtTime(t, { weekday: "short" });
    if (filter === "date") return fmtTime(t, { month: "short", day: "numeric" });
    if (filter === "hour") return h24() ? fmtTime(t, { hour: "2-digit", hour12: false }) : fmtTime(t, { hour: "numeric", hour12: true }).replace(" ", "");
    return fmtNum(v, dec);
  });
}

// ============================================================ preview renderer (mirrors firmware ui.cpp)
const FONT_SIZES = [12, 14, 16, 18, 20, 22, 24, 28, 32, 36, 40, 48, 64, 96, 128];
const nearestFont = px => FONT_SIZES.reduce((b, s) => Math.abs(s - px) < Math.abs(b - px) ? s : b, FONT_SIZES[0]);
const wxSize = px => px >= 96 ? 128 : px >= 48 ? 64 : 32;
const THEME_KEYS = ["bg", "fg", "muted", "accent", "ok", "warn", "bad", "track"];
const DEFAULT_THEME = { bg: "#000000", fg: "#F5F2EB", muted: "#8C8A85", accent: "#D97757", ok: "#5FB87A", warn: "#E8B04B", bad: "#E5534B", track: "#232323" };
function tcol(name, theme, def) {
  if (!name) return def;
  if (name[0] === "#") return name;
  return theme[name] || def;
}
function valueColor(p, v, has, theme) {
  if ((p.color_mode || "fixed") === "threshold" && has) {
    const warn = p.warn ?? 60, bad = p.bad ?? 85;
    return v >= bad ? theme.bad : v >= warn ? theme.warn : theme.ok;
  }
  return tcol(p.color || "accent", theme, theme.accent);
}
const numOf = (key, data) => { const v = data[key]; if (typeof v === "number") return v; const n = parseFloat(v); return isNaN(n) ? undefined : n; };
function lbl(text, size, color, extra = {}) {
  return h("div", { class: "lbl", style: Object.assign({ fontSize: nearestFont(size) + "px", color }, extra) }, text);
}
function nowParts() {
  const d = new Date();
  try {
    const tz = tzName();
    const parts = Object.fromEntries(new Intl.DateTimeFormat("en-US", { timeZone: tz, hour: "numeric", minute: "2-digit", second: "2-digit", hour12: false, weekday: "long", month: "long", day: "numeric", year: "numeric" }).formatToParts(d).map(p => [p.type, p.value]));
    return { H: parseInt(parts.hour) % 24, M: parseInt(parts.minute), s: parseInt(parts.second), wd: parts.weekday, mo: parts.month, day: parseInt(parts.day), year: parts.year };
  } catch { return { H: d.getHours(), M: d.getMinutes(), s: d.getSeconds(), wd: d.toLocaleString("en-US", { weekday: "long" }), mo: d.toLocaleString("en-US", { month: "long" }), day: d.getDate(), year: d.getFullYear() }; }
}
function strftime(fmt) {
  const n = nowParts(), p2 = x => String(x).padStart(2, "0");
  const h12 = n.H % 12 || 12;
  const map = { A: n.wd, a: n.wd.slice(0, 3), B: n.mo, b: n.mo.slice(0, 3), d: p2(n.day), e: String(n.day), Y: n.year, y: String(n.year).slice(2),
    H: p2(n.H), I: p2(h12), l: String(h12), M: p2(n.M), S: p2(n.s), p: n.H < 12 ? "AM" : "PM", "%": "%" };
  return fmt.replace(/%([a-zA-Z%])/g, (m, c) => map[c] ?? m);
}
function arcPath(cx, cy, r, a0, a1) {
  const rad = a => a * Math.PI / 180, pt = a => [cx + r * Math.cos(rad(a)), cy + r * Math.sin(rad(a))];
  if (a1 - a0 >= 359.99) a1 = a0 + 359.99;
  const [x0, y0] = pt(a0), [x1, y1] = pt(a1);
  return `M ${x0} ${y0} A ${r} ${r} 0 ${a1 - a0 > 180 ? 1 : 0} 1 ${x1} ${y1}`;
}
function renderWidget(p, theme, data) {
  const W = (p.w || 4) * 40, H = (p.h || 1) * 40, pad = p.pad || 0;
  const box = h("div", { class: "w" });
  Object.assign(box.style, { left: (p.x || 0) * 40 + "px", top: (p.y || 0) * 40 + "px", width: W + "px", height: H + "px", padding: pad + "px",
    borderRadius: (p.radius || 0) + "px", alignItems: "center", justifyContent: "center" });
  if (p.bg) { box.style.background = tcol(p.bg, theme, theme.track); box.style.opacity = ""; if (p.bg_opa !== undefined) box.style.background = hexA(tcol(p.bg, theme, theme.track), p.bg_opa); }
  if (p.border > 0) box.style.border = `${p.border}px solid ${tcol(p.border_color || "muted", theme, theme.muted)}`;
  const fg = tcol(p.color || "fg", theme, theme.fg);
  const size = p.size ?? 24;
  const ta = p.align || "center";
  const t = p.type || "text";

  if (t === "clock") {
    const n = nowParts();
    const mode = p.h24 || "auto";
    const is24 = mode === "24" || (mode === "auto" && h24());
    let txt;
    if (is24) txt = `${String(n.H).padStart(2, "0")}:${String(n.M).padStart(2, "0")}${p.seconds ? ":" + String(n.s).padStart(2, "0") : ""}`;
    else txt = `${n.H % 12 || 12}:${String(n.M).padStart(2, "0")}${p.seconds ? ":" + String(n.s).padStart(2, "0") : ""}`;
    // LVGL: flex row centered in the box, AM/PM aligned to the bottom of the digits
    const inner = h("div", { style: { display: "flex", alignItems: "flex-end" } }, lbl(txt, size, fg),
      !is24 && p.ampm !== false ? lbl(n.H < 12 ? "AM" : "PM", Math.max(14, Math.floor(size / 3)), tcol(p.ampm_color || "muted", theme, theme.muted), { paddingBottom: Math.floor(size / 7) + "px", paddingLeft: "6px" }) : null);
    box.append(inner);
  } else if (t === "date" || t === "text") {
    const txt = t === "date" ? strftime(p.fmt || "%A, %b %e").replace(/  /g, " ") : renderTmpl(p.text || "", data);
    const ov = p.overflow || "wrap";
    box.style.justifyContent = "flex-start";
    box.append(lbl(txt, size, fg, { width: "100%", textAlign: ta, whiteSpace: ov === "wrap" ? "pre-wrap" : "pre", overflow: "hidden", textOverflow: "clip" }));
  } else if (t === "value") {
    const v = numOf(p.key || "", data);
    const col = (p.color_mode || "fixed") === "threshold" ? valueColor(p, v, v !== undefined, theme) : fg;
    box.style.flexDirection = "column";
    box.append(lbl(renderTmpl(p.value || "", data), size, col, { width: "100%", textAlign: ta }),
      lbl(renderTmpl(p.label || "", data), p.label_size ?? 16, tcol(p.label_color || "muted", theme, theme.muted), { width: "100%", textAlign: ta }));
  } else if (t === "bar") {
    const v = numOf(p.key || "", data), has = v !== undefined;
    const mn = p.min ?? 0, mx = p.max ?? 100;
    const frac = has && mx > mn ? Math.min(1, Math.max(0, (v - mn) / (mx - mn))) : 0;
    const mode = p.value_pos || "right", vsz = p.value_size ?? size, bh = p.bar_h ?? 14;
    const vt = p.show_value === false ? "" : has ? fmtNum(v, 0) + (p.unit ?? "%") : "--";
    const col = valueColor(p, v, has, theme);
    const fgL = tcol(p.label_color || "fg", theme, theme.fg);   // text stays neutral; "color" is the bar fill
    box.style.flexDirection = "column"; box.style.alignItems = "stretch"; box.style.justifyContent = "center";
    const hasLabel = !!(p.label) || mode === "right";
    if (hasLabel) box.append(h("div", { style: { display: "flex", justifyContent: "space-between", marginBottom: "6px" } },
      lbl(renderTmpl(p.label || "", data), size, fgL), mode === "right" ? lbl(vt, vsz, fgL) : null));
    const cw = W - 2 * pad;
    const fillW = Math.round(frac * cw);
    if (mode === "follow") {
      const row = h("div", { style: { position: "relative", height: Math.round(nearestFont(vsz) * 1.22) + 2 + "px" } });
      const l = lbl(vt, vsz, fgL, { position: "absolute", top: 0, whiteSpace: "pre" });
      row.append(l); box.append(row);
      requestAnimationFrame(() => { const lw = l.offsetWidth; l.style.left = Math.max(0, Math.min(cw - lw, fillW - lw / 2)) + "px"; });
    }
    const bar = h("div", { style: { position: "relative", width: "100%", height: bh + "px", borderRadius: "99px", background: tcol(p.track || "track", theme, theme.track), overflow: "hidden", flex: "none" } },
      h("div", { style: { width: frac * 100 + "%", height: "100%", borderRadius: "99px", background: col } }));
    if (mode === "inside") {
      const l = lbl(vt, vsz, fgL, { position: "absolute", top: "50%", transform: "translateY(-50%)", whiteSpace: "pre" });
      bar.append(l);
      requestAnimationFrame(() => {
        const lw = l.offsetWidth, padIn = Math.max(8, Math.floor(bh / 3)), inside = fillW >= lw + 2 * padIn;
        let x = inside ? fillW - lw - padIn : fillW + padIn; if (x + lw > cw) x = cw - lw - padIn;
        l.style.left = x + "px"; l.style.color = inside ? tcol(p.value_color || "bg", theme, theme.bg) : theme.fg;
      });
    }
    box.append(bar);
    if (p.sub) box.append(lbl(has ? renderTmpl(p.sub || "", data) : (p.empty ?? "No data yet"), p.sub_size ?? 16, theme.muted, { marginTop: "6px" }));
  } else if (t === "ring") {
    const v = numOf(p.key || "", data), has = v !== undefined;
    const mn = p.min ?? 0, mx = p.max ?? 100;
    const frac = has && mx > mn ? Math.min(1, Math.max(0, (v - mn) / (mx - mn))) : 0;
    const side = Math.min(W, H) - 2 * pad, th = p.thickness ?? 12, r = side / 2 - th / 2;
    const start = p.start ?? 135, sweep = Math.min(360, Math.max(10, p.sweep ?? 270));
    const cap = p.rounded === false ? "butt" : "round";
    const svg = `<svg width="${side}" height="${side}" style="position:absolute;left:${(W - side) / 2}px;top:${(H - side) / 2}px">
      <path d="${arcPath(side / 2, side / 2, r, start, start + sweep)}" fill="none" stroke="${tcol(p.track || "track", theme, theme.track)}" stroke-width="${th}" stroke-linecap="${cap}"/>
      ${frac > 0 ? `<path d="${arcPath(side / 2, side / 2, r, start, start + sweep * frac)}" fill="none" stroke="${valueColor(p, v, has, theme)}" stroke-width="${th}" stroke-linecap="${cap}"/>` : ""}</svg>`;
    box.innerHTML = svg;
    const capSize = p.caption_size ?? 16;
    const mode = p.value_pos || "center";
    const textT = p.text !== undefined ? p.text : mode === "center" ? `{${p.key}|pct}` : "";
    const center = h("div", { style: { position: "absolute", inset: 0, display: "flex", flexDirection: "column", alignItems: "center", justifyContent: "center" } },
      lbl(renderTmpl(textT, data), size, fg), p.caption ? lbl(renderTmpl(p.caption, data), capSize, theme.muted) : null);
    box.append(center);
    if (mode === "tip" && has) {
      const a = (start + sweep * frac) * Math.PI / 180;
      const bx = W / 2 + r * Math.cos(a), by = H / 2 + r * Math.sin(a);
      const badge = lbl(renderTmpl(p.badge ?? `{${p.key}:0}%`, data), p.badge_size ?? 16, theme.bg,
        { position: "absolute", left: bx + "px", top: by + "px", transform: "translate(-50%,-50%)", background: valueColor(p, v, has, theme), borderRadius: "99px", padding: "2px 8px", whiteSpace: "pre" });
      box.append(badge);
    }
  } else if (t === "weather") {
    const row = (p.style || "row") === "row";
    box.style.flexDirection = row ? "row" : "column"; box.style.gap = "14px";
    box.append(h("div", { class: "wx", style: { fontSize: wxSize(p.icon_size ?? 64) + "px", color: tcol(p.icon_color || "fg", theme, theme.fg) } }, data["weather.icon"] || ""));
    const unit = get(S.settings, "weather.units", "imperial") === "metric" ? "C" : "F";
    const col = h("div", { style: { display: "flex", flexDirection: "column", alignItems: row ? "flex-start" : "center" } },
      lbl(renderTmpl("{weather.temp:0}°", data) + (p.show_unit ? unit : ""), size, fg),
      p.show_desc ? lbl(renderTmpl("{weather.desc}", data), p.desc_size ?? 18, theme.muted) : null);
    box.append(col);
  } else if (t === "forecast") {
    const hourly = (p.mode || "daily") === "hourly", step = p.step ?? 2, n = Math.min(8, Math.max(1, p.count ?? 5));
    box.style.justifyContent = "space-evenly";
    for (let i = 0; i < n; i++) {
      const b = hourly ? `weather.h${i * step}` : `weather.d${i}`;
      const l1 = hourly ? renderTmpl(`{${b}.t|hour}`, data) : i === 0 ? "Today" : renderTmpl(`{${b}.t|day}`, data);
      const l2 = hourly ? renderTmpl(`{${b}.temp:0}°`, data) : renderTmpl(`{${b}.hi:0}°/{${b}.lo:0}°`, data);
      const l3 = !hourly && p.show_wind ? renderTmpl(`{${b}.wind:0} `, data) + (data["weather.wind_unit"] || "mph") : null;
      box.append(h("div", { style: { display: "flex", flexDirection: "column", alignItems: "center", gap: "4px" } },
        lbl(l1, size, theme.muted), h("div", { class: "wx", style: { fontSize: wxSize(p.icon_size ?? 32) + "px", color: theme.fg } }, data[b + ".icon"] || ""), lbl(l2, size, fg),
        l3 !== null ? lbl(l3, p.wind_size ?? 14, theme.muted) : null));
    }
  } else if (t === "icon") {
    const g = p.key ? (data[p.key] ?? "") : (p.glyph || "");
    box.append(h("div", { class: "wx", style: { fontSize: wxSize(size) + "px", color: fg } }, g));
  } else if (t === "qr") {
    const side = Math.min(W, H) - 2 * pad - 8;
    box.append(h("div", { style: { width: side + "px", height: side + "px", background: "#fff", padding: "4px" } },
      h("div", { style: { width: "100%", height: "100%", background: "repeating-conic-gradient(#000 0 25%, #fff 0 50%) 0 0/16px 16px" } })));
  } else if (t === "media") {
    if (p.file) {
      const mf = ((Media.list && Media.list.files) || []).find(f => f.name === p.file);
      const im = h("img", { src: mediaThumbUrl(p.file, mf), alt: "" });
      Object.assign(im.style, { position: "absolute", left: "50%", top: "50%", transform: "translate(-50%,-50%)", maxWidth: "none" });
      im.onerror = () => im.replaceWith(h("div", { class: "small", style: { color: theme.muted, textAlign: "center" } }, "Not on the card: " + p.file));
      box.style.overflow = "hidden";
      box.append(im);
    } else box.append(h("div", { class: "small", style: { color: theme.muted, textAlign: "center", padding: "8px" } }, "Picture / video — choose a file"));
  } else if (t === "box") {
    // decoration
  } else {
    box.append(lbl("unknown: " + t, 14, theme.bad));
  }
  return box;
}
function hexA(hex, opa) { const a = Math.round(Math.max(0, Math.min(255, opa))).toString(16).padStart(2, "0"); return hex.length === 7 ? hex + a : hex; }
function renderScreen(page, theme, data, { edit = false, selected = -1, grid = false } = {}) {
  const scr = h("div", { class: "screen" + (edit ? " edit" : "") });
  scr.style.background = tcol(page.bg || "", theme, theme.bg);
  (page.widgets || []).forEach((w, i) => {
    const el = renderWidget(w, theme, data);
    el.dataset.idx = i;
    if (edit) { if (i === selected) el.classList.add("sel"); el.append(h("div", { class: "handle" })); }
    scr.append(el);
  });
  if (grid) scr.append(h("div", { class: "gridlines" }));
  return scr;
}
function fitScreen(wrap) {
  const scr = wrap.querySelector(".screen"); if (!scr) return;
  scr.style.transform = `scale(${wrap.clientWidth / 480})`;
}
const ro = new ResizeObserver(entries => entries.forEach(e => fitScreen(e.target)));
function mountScreen(wrap, scr) { wrap.replaceChildren(scr); fitScreen(wrap); ro.observe(wrap); }
function themeOf(layout, night) {
  const t = Object.assign({}, DEFAULT_THEME, layout?.theme || {});
  if (night && layout?.night_theme && get(S.settings, "display.night.use_night_theme", true)) Object.assign(t, layout.night_theme);
  return t;
}

// ============================================================ polling
async function loadData() {
  try {
    const d = await api("/api/data");
    const flat = {}; for (const [k, v] of Object.entries(d.data || {})) flat[k] = v.v;
    S.data = flat; S.dataTimes = Object.fromEntries(Object.entries(d.data || {}).map(([k, v]) => [k, v.t])); S.dataVer = d.version;
  } catch { }
}
async function pollStatus() {
  try {
    S.status = await api("/api/status");
    $("#connDot").className = "dot on";
    renderChips();
    onStatus.forEach(f => f());
  } catch { $("#connDot").className = "dot off"; }
}
const onStatus = [];
function renderChips() {
  const st = S.status, n = st.net || {}, b = st.battery || {};
  $("#devName").textContent = st.name || "Desk Display";
  const chips = [];
  chips.push(n.ap ? "Setup network" : n.connected ? `${n.ssid} ${n.rssi} dBm` : "Offline");
  if (b.present) chips.push((b.charging ? "⚡ " : "") + b.percent + "%"); else if (b.usb !== undefined) chips.push("USB power");
  if (st.ui) chips.push(`${st.ui.sleeping ? "Asleep" : st.ui.page_name || "-"}${st.ui.night ? " · night" : ""}`);
  chips.push("v" + st.version);
  $("#chips").replaceChildren(...chips.map(c => h("span", { class: "chip" }, c)));
}

// ============================================================ tabs
const TABS = {};
function showTab(name) {
  if (S.tab === "layout" && name !== "layout" && Editor.dirty && !confirm("You have unsaved layout changes. Leave anyway?")) return;
  S.tab = name;
  $$("#tabs button").forEach(b => b.classList.toggle("active", b.dataset.tab === name));
  $$(".tab").forEach(t => t.classList.toggle("active", t.id === "tab-" + name));
  TABS[name]?.();
  history.replaceState(null, "", "#" + name);
}
$$("#tabs button").forEach(b => b.onclick = () => showTab(b.dataset.tab));
window.addEventListener("beforeunload", e => { if (Editor.dirty) { e.preventDefault(); e.returnValue = ""; } });

// ============================================================ common form helpers
function field(label, input, hint) {
  return h("label", { class: "f" }, h("span", {}, label, hint ? h("span", { class: "small" }, hint) : null), input);
}
function inp(type, value, onchange, attrs = {}) {
  const el = h("input", Object.assign({ type, value: value ?? "" }, attrs));
  if (type === "checkbox") { el.checked = !!value; el.onchange = () => onchange(el.checked); }
  else el.oninput = () => onchange(type === "number" || type === "range" ? (el.value === "" ? null : Number(el.value)) : el.value);
  return el;
}
function sel(options, value, onchange) {
  const el = h("select", {}, ...options.map(o => { const [v, l] = Array.isArray(o) ? o : [o, o]; return h("option", { value: v, selected: String(v) === String(value) }, l); }));
  el.onchange = () => onchange(el.value);
  return el;
}
function check(label, value, onchange) { return h("label", { class: "check" }, inp("checkbox", value, onchange), label); }
async function saveSettings(patch, msg = "Saved") {
  try { await api("/api/settings", { body: patch }); toast(msg); await sleep(400); S.settings = await api("/api/settings"); }
  catch (e) { toast("Save failed: " + e.message, true); }
}

// ============================================================ MEDIA (microSD pictures, GIFs, video loops)
// Everything is converted in the browser into a "DDA" file: JPEG frames sized for the display plus
// a small index (see src/media.h). The display then only has to decode JPEG.
const Media = { list: null, busy: false };
async function mediaRefresh() {
  try { Media.list = await api("/api/media"); } catch (e) { Media.list = { sd: { mounted: false }, files: [], error: e.message }; }
  return Media.list;
}
function mediaFileField(value, onchange) {
  const files = (Media.list && Media.list.files) || [];
  const s = sel([["", files.length ? "Choose a file…" : "No files yet"], ...files.map(f => [f.name, `${f.name} (${f.w}×${f.h}${f.frames > 1 ? ", " + f.frames + " frames" : ""})`])], value, onchange);
  if (!Media.list) mediaRefresh().then(() => { if (S.tab === "layout") drawEditor(); });
  return h("div", { class: "stack" }, s, h("span", { class: "small muted" }, "Add files in the ", h("a", { href: "#media", onclick: e => { e.preventDefault(); showTab("media"); } }, "Media tab"), ". Tip: a full-screen file fits a 12 × 12 widget."));
}
const mediaThumbUrl = (name, f) => "/api/media/frame?name=" + encodeURIComponent(name) + (f ? "&v=" + f.size : "");
const fmtBytes = n => n >= 1 << 30 ? (n / (1 << 30)).toFixed(1) + " GB" : n >= 1 << 20 ? (n / (1 << 20)).toFixed(1) + " MB" : Math.max(1, Math.round(n / 1024)) + " KB";

function ddaDims(srcW, srcH, size, fit) {
  if (fit === "cover") return [size, size];
  const k = Math.min(size / srcW, size / srcH);
  const r16 = v => Math.max(16, Math.min(size, Math.round(v * k / 16) * 16));
  return [r16(srcW), r16(srcH)];
}
function drawFit(ctx, src, sw, sh, W, H, fit) {
  ctx.fillStyle = "#000"; ctx.fillRect(0, 0, W, H);
  const k = fit === "cover" ? Math.max(W / sw, H / sh) : Math.min(W / sw, H / sh);
  const dw = sw * k, dh = sh * k;
  ctx.imageSmoothingQuality = "high";
  ctx.drawImage(src, (W - dw) / 2, (H - dh) / 2, dw, dh);
}
async function encodeJpeg(canvas, q) {
  if (canvas.convertToBlob) return new Uint8Array(await (await canvas.convertToBlob({ type: "image/jpeg", quality: q })).arrayBuffer());
  return new Uint8Array(await (await new Promise(r => canvas.toBlob(r, "image/jpeg", q))).arrayBuffer());
}
function buildDda(W, H, frames) {           // frames: [{jpg: Uint8Array, delay}]
  const head = 12 + frames.length * 12;
  const total = head + frames.reduce((a, f) => a + f.jpg.length, 0);
  const out = new Uint8Array(total), dv = new DataView(out.buffer);
  out.set([68, 68, 65, 49], 0);              // "DDA1"
  dv.setUint16(4, W, true); dv.setUint16(6, H, true); dv.setUint16(8, frames.length, true); dv.setUint16(10, 1, true);
  let off = head;
  frames.forEach((f, i) => {
    dv.setUint32(12 + i * 12, off, true); dv.setUint32(16 + i * 12, f.jpg.length, true); dv.setUint16(20 + i * 12, f.delay, true);
    out.set(f.jpg, off); off += f.jpg.length;
  });
  return new Blob([out], { type: "application/octet-stream" });
}
async function convertMedia(file, o, progress) {
  const mkCanvas = (W, H) => { const c = document.createElement("canvas"); c.width = W; c.height = H; return c; };
  const frames = [];
  let W, H;
  const isVideo = file.type.startsWith("video/") || /\.(mp4|webm|mov|m4v|mkv)$/i.test(file.name);
  const animated = /gif|webp|png|apng/i.test(file.type) && "ImageDecoder" in window;
  if (isVideo) {
    const v = h("video", { muted: true, playsInline: true, preload: "auto" });
    v.src = URL.createObjectURL(file);
    await new Promise((res, rej) => { v.onloadeddata = res; v.onerror = () => rej(new Error("This browser can't play that video. Try an MP4 (H.264) file.")); });
    W = ddaDims(v.videoWidth, v.videoHeight, o.size, o.fit)[0]; H = ddaDims(v.videoWidth, v.videoHeight, o.size, o.fit)[1];
    const c = mkCanvas(W, H), ctx = c.getContext("2d");
    const dur = Math.min(v.duration || 0, o.maxSec), step = 1 / o.fps, n = Math.max(1, Math.floor(dur * o.fps));
    for (let i = 0; i < n; i++) {
      v.currentTime = i * step;
      await new Promise(r => { v.onseeked = r; });
      drawFit(ctx, v, v.videoWidth, v.videoHeight, W, H, o.fit);
      frames.push({ jpg: await encodeJpeg(c, o.q), delay: Math.round(1000 / o.fps) });
      progress(`Converting video: frame ${i + 1} of ${n}`, (i + 1) / n);
    }
    URL.revokeObjectURL(v.src);
  } else if (animated) {
    const dec = new ImageDecoder({ data: await file.arrayBuffer(), type: file.type });
    await dec.tracks.ready;
    const n = Math.min(dec.tracks.selectedTrack.frameCount || 1, 1500);
    for (let i = 0; i < n; i++) {
      const { image } = await dec.decode({ frameIndex: i });
      if (!W) [W, H] = ddaDims(image.displayWidth, image.displayHeight, o.size, o.fit);
      const c = mkCanvas(W, H);
      drawFit(c.getContext("2d"), image, image.displayWidth, image.displayHeight, W, H, o.fit);
      const d = image.duration ? Math.round(image.duration / 1000) : 100;
      image.close();
      frames.push({ jpg: await encodeJpeg(c, o.q), delay: d >= 20 ? d : 100 });
      progress(`Converting animation: frame ${i + 1} of ${n}`, (i + 1) / n);
    }
  } else {
    const bmp = await createImageBitmap(file);
    [W, H] = ddaDims(bmp.width, bmp.height, o.size, o.fit);
    const c = mkCanvas(W, H);
    drawFit(c.getContext("2d"), bmp, bmp.width, bmp.height, W, H, o.fit);
    frames.push({ jpg: await encodeJpeg(c, o.q), delay: 1000 });
    progress("Converted", 1);
  }
  return { blob: buildDda(W, H, frames), W, H, n: frames.length };
}
function uploadMedia(blob, name, progress) {
  return new Promise((res, rej) => {
    const x = new XMLHttpRequest();
    x.open("POST", "/api/media/upload?name=" + encodeURIComponent(name));
    x.upload.onprogress = e => e.lengthComputable && progress(`Uploading ${fmtBytes(e.loaded)} of ${fmtBytes(e.total)}`, e.loaded / e.total);
    x.onload = () => { let d = {}; try { d = JSON.parse(x.responseText); } catch { } x.status < 300 ? res(d) : rej(new Error(d.error || "HTTP " + x.status)); };
    x.onerror = () => rej(new Error("network error"));
    const fd = new FormData(); fd.append("file", blob, name + ".dda"); x.send(fd);
  });
}
async function mediaNewPage(f) {
  const cells = Math.max(1, Math.min(12, Math.round(Math.max(f.w, f.h) / 40)));
  const pg = { name: f.name.slice(0, 24), enabled: true, duration: 15, bg: "#000000",
    widgets: [{ type: "media", x: Math.floor((12 - cells) / 2), y: Math.floor((12 - cells) / 2), w: cells, h: cells, file: f.name }] };
  if (Editor.layout && Editor.dirty) {
    Editor.snapshot(); Editor.layout.pages.push(pg); Editor.page = Editor.layout.pages.length - 1; Editor.sel = -1;
    showTab("layout"); toast("Page added — press Save to display"); return;
  }
  try {
    const L = await api("/api/layout"); L.pages.push(pg);
    await api("/api/layout", { body: L });
    S.layout = L; if (Editor.layout) Editor.load(L);
    toast(`Added the page "${pg.name}" to the display`);
  } catch (e) { toast("Couldn't add the page: " + e.message, true); }
}
TABS.media = async () => {
  const el = $("#tab-media");
  if (!Media.list) el.replaceChildren(h("div", { class: "card" }, "Loading…"));
  const L = await mediaRefresh();
  const sd = L.sd || {};
  const card = h("div", { class: "card stack" }, h("h2", {}, "microSD card"),
    sd.mounted
      ? h("div", { class: "stack" },
          h("div", {}, `${sd.type} · ${fmtBytes(sd.used || 0)} used of ${fmtBytes(sd.total || 0)}`),
          h("div", { class: "mmeter" }, h("div", { style: { width: Math.min(100, 100 * (sd.used || 0) / (sd.total || 1)) + "%" } })))
      : h("div", { class: "stack" },
          h("p", { style: { margin: 0 } }, "No card found. Insert a microSD card formatted as FAT32 (most cards up to 32 GB already are), then press Check again."),
          h("div", { class: "row" }, h("button", { class: "btn sm", onclick: async () => { try { await api("/api/media/mount", { method: "POST" }); toast("Card found"); } catch (e) { toast(e.message, true); } TABS.media(); } }, "Check again"))));

  // upload
  const o = Object.assign({ size: 480, fit: "cover", fps: 12, maxSec: 20, q: 0.8 }, Media.opts || {});
  Media.opts = o;
  const fileIn = h("input", { type: "file", accept: "image/*,video/*,.gif,.mp4,.webm,.mov", style: { display: "none" } });
  const nameIn = inp("text", "", () => { }, { placeholder: "name on the card" });
  const info = h("div", { class: "small muted" }, "JPG, PNG, GIF, WebP or a video (MP4/WebM/MOV). It's converted here in your browser, so big files are fine.");
  const bar = h("div", { class: "mmeter", style: { display: "none" } }, h("div", { style: { width: "0%" } }));
  const progress = (msg, frac) => { info.textContent = msg; bar.style.display = ""; bar.firstChild.style.width = Math.round(frac * 100) + "%"; };
  let picked = null;
  const drop = h("div", { class: "dropzone", onclick: () => fileIn.click() }, h("b", {}, "Drop a file here"), h("span", { class: "small muted" }, "or click to choose"));
  const pick = f => {
    picked = f; drop.querySelector("b").textContent = f.name;
    nameIn.value = f.name.replace(/\.[^.]+$/, "").replace(/[^A-Za-z0-9 _.-]/g, "").slice(0, 40);
    const vid = f.type.startsWith("video/"); vidOpts.style.display = vid ? "" : "none";
    go.disabled = !sd.mounted;
  };
  fileIn.onchange = () => fileIn.files[0] && pick(fileIn.files[0]);
  drop.ondragover = e => { e.preventDefault(); drop.classList.add("over"); };
  drop.ondragleave = () => drop.classList.remove("over");
  drop.ondrop = e => { e.preventDefault(); drop.classList.remove("over"); e.dataTransfer.files[0] && pick(e.dataTransfer.files[0]); };
  const vidOpts = h("div", { class: "two", style: { display: "none" } },
    field("Frames per second", sel([[8, "8"], [10, "10"], [12, "12 (recommended)"], [15, "15"], [20, "20 (may stutter)"]], o.fps, v => o.fps = +v)),
    field("Use the first", sel([[5, "5 seconds"], [10, "10 seconds"], [20, "20 seconds"], [30, "30 seconds"], [60, "1 minute"], [120, "2 minutes"]], o.maxSec, v => o.maxSec = +v)));
  const go = h("button", { class: "btn primary", disabled: true, onclick: async () => {
    if (!picked || Media.busy) return;
    const name = nameIn.value.trim();
    if (!name) return toast("Give it a name", true);
    Media.busy = true; go.disabled = true;
    try {
      const r = await convertMedia(picked, o, progress);
      await uploadMedia(r.blob, name, progress);
      info.textContent = `Saved "${name}" (${r.W}×${r.H}, ${r.n} frame${r.n > 1 ? "s" : ""}, ${fmtBytes(r.blob.size)}).`;
      toast("Saved to the card");
      Media.busy = false; TABS.media();
    } catch (e) { info.textContent = "Failed: " + e.message; toast(e.message, true); Media.busy = false; go.disabled = false; }
  } }, "Convert & upload");
  const up = h("div", { class: "card stack" }, h("h2", {}, "Add a picture, GIF or video"), fileIn, drop,
    h("div", { class: "two" },
      field("Size", sel([[480, "Full screen (480 × 480)"], [320, "Medium (320 × 320)"], [240, "Small (240 × 240)"], [160, "Tiny (160 × 160)"]], o.size, v => o.size = +v)),
      field("Fit", sel([["cover", "Fill the square (crop edges)"], ["contain", "Show the whole picture"]], o.fit, v => o.fit = v))),
    vidOpts,
    field("Quality", sel([[0.65, "Smaller file"], [0.8, "Good (recommended)"], [0.92, "Best (slower video)"]], o.q, v => o.q = +v)),
    field("Name", nameIn), h("div", { class: "row" }, go), bar, info);

  // files
  const files = L.files || [];
  const list = h("div", { class: "card stack" }, h("h2", {}, `On the card (${files.length})`),
    files.length ? h("div", { class: "mgrid" }, ...files.map(f => h("div", { class: "mtile" },
      h("div", { class: "mthumb" }, f.bad ? h("span", { class: "small muted" }, "unreadable") : h("img", { src: mediaThumbUrl(f.name, f), loading: "lazy", alt: "" })),
      h("b", {}, f.name),
      h("span", { class: "small muted" }, `${f.w}×${f.h} · ${f.frames > 1 ? f.frames + " frames, " + (f.seconds || 0).toFixed(1) + " s" : "picture"} · ${fmtBytes(f.size)}`),
      h("div", { class: "row" },
        h("button", { class: "btn sm", onclick: () => mediaNewPage(f), title: "Add a page that shows this" }, "New page"),
        h("button", { class: "btn sm danger", onclick: async () => { if (!confirm(`Delete "${f.name}" from the card?`)) return; try { await api("/api/media/delete", { body: { name: f.name } }); TABS.media(); } catch (e) { toast(e.message, true); } } }, "Delete")))))
    : h("p", { class: "muted", style: { margin: 0 } }, sd.mounted ? "Nothing here yet." : "—"),
    h("p", { class: "small muted", style: { margin: 0 } }, "To show a file: press New page, or add a “Picture / video” widget to any page in Layout."));
  el.replaceChildren(h("div", { class: "grid2" }, h("div", { class: "stack" }, card, up), list));
};

// ============================================================ DASHBOARD
TABS.dash = () => {
  const el = $("#tab-dash");
  const st = S.status;
  const cards = [];
  if (st.net?.ap) cards.push(h("div", { class: "card warnbox" }, h("h2", {}, "Finish setup"),
    h("p", {}, "You're on the display's setup network. Enter your home Wi-Fi in Settings → Wi-Fi; the display reconnects and this network closes."),
    h("button", { class: "btn primary", onclick: () => showTab("settings") }, "Open Wi-Fi settings")));
  if (S.settings && !st.has_admin_pass) cards.push(h("div", { class: "card warnbox" }, h("h2", {}, "Set an admin password"),
    h("p", { class: "muted" }, "Anyone on your network can change settings until you set one (Settings → Security)."),
    h("button", { class: "btn", onclick: () => { showTab("settings"); setTimeout(() => $("#sec-security")?.scrollIntoView(), 50); } }, "Set password")));
  if (S.settings && get(S.settings, "weather.lat") == null) cards.push(h("div", { class: "card warnbox" }, h("h2", {}, "Weather location"),
    h("p", { class: "muted" }, "Pick a location so the weather widgets have data."),
    h("button", { class: "btn", onclick: () => { showTab("settings"); setTimeout(() => $("#sec-weather")?.scrollIntoView(), 50); } }, "Set location")));
  if (S.data["claude.updated"] === undefined) cards.push(h("div", { class: "card warnbox" }, h("h2", {}, "Connect Claude usage"),
    h("p", { class: "muted" }, "No usage data yet. Set up the Claude Code status line script (Settings → Claude usage) and it appears after your next Claude Code message."),
    h("button", { class: "btn", onclick: () => { showTab("settings"); setTimeout(() => $("#sec-claude")?.scrollIntoView(), 50); } }, "Show setup")));

  const wrap = h("div", { class: "screen-wrap" });
  const remote = h("div", { class: "row", style: { justifyContent: "center", marginTop: "12px" } },
    ...[["◀ Prev", "prev"], ["Home", "home"], ["Next ▶", "next"]].map(([l, a]) => h("button", { class: "btn", onclick: () => act(a) }, l)));
  const remote2 = h("div", { class: "row", style: { justifyContent: "center", marginTop: "8px" } },
    ...[["Sleep / wake", "sleep_toggle"], ["Night", "night_toggle"], ["Show QR", "show_qr"], ["Refresh data", "refresh"], ["Say hi", "identify"]].map(([l, a]) => h("button", { class: "btn sm", onclick: () => act(a) }, l)));
  const pageSel = h("div", { class: "row", style: { justifyContent: "center", marginTop: "8px" }, id: "dashPages" });

  const usage = h("div", { class: "card", id: "dashUsage" });
  const wx = h("div", { class: "card", id: "dashWx" });
  el.replaceChildren(...cards, h("div", { class: "grid" },
    h("div", { class: "card" }, h("h2", {}, "On the display now"), h("div", { class: "bezel" }, wrap), remote, remote2, pageSel), usage, wx));
  const draw = () => {
    if (!S.layout) return;
    const pages = (S.layout.pages || []).filter(p => p.enabled !== false);
    const idx = Math.min(st.ui?.page ?? 0, pages.length - 1);
    const pg = pages[Math.max(0, S.status.ui?.page ?? 0)] || pages[idx] || { widgets: [] };
    mountScreen(wrap, renderScreen(pg, themeOf(S.layout, S.status.ui?.night), S.data));
    if (S.status.ui?.sleeping) wrap.querySelector(".screen").style.opacity = .25;
    $("#dashPages")?.replaceChildren(...pages.map((p, i) => h("button", { class: "btn sm" + (i === S.status.ui?.page ? " primary" : ""), onclick: () => act("goto", i) }, p.name || "Page " + (i + 1))));
    const u = (k) => S.data[k];
    const meter = (pct, warn, bad) => h("div", { class: "meter" }, h("i", { style: { width: Math.min(100, pct || 0) + "%", background: pct >= bad ? "var(--bad)" : pct >= warn ? "var(--warn)" : "var(--ok)" } }));
    $("#dashUsage")?.replaceChildren(h("h2", {}, "Claude usage"),
      u("claude.updated") === undefined ? h("p", { class: "muted" }, "Waiting for the first push from Claude Code.") :
        h("div", { class: "stack" },
          h("div", { class: "row between" }, h("span", {}, "Current session (5 h)"), h("b", {}, fmtNum(u("claude.five_hour.used_percentage") ?? 0, 0) + "%")),
          meter(u("claude.five_hour.used_percentage"), 60, 85),
          h("div", { class: "small muted" }, "Resets in " + renderTmpl("{claude.five_hour.resets_at|in}")),
          h("div", { class: "row between" }, h("span", {}, "This week"), h("b", {}, fmtNum(u("claude.seven_day.used_percentage") ?? 0, 0) + "%")),
          meter(u("claude.seven_day.used_percentage"), 70, 90),
          h("div", { class: "small muted" }, "Resets " + renderTmpl("{claude.seven_day.resets_at|day} {claude.seven_day.resets_at|time}")),
          h("div", { class: "small muted" }, "Last update " + renderTmpl("{claude.updated|ago}") + (u("claude.stale") ? " (stale)" : ""))));
    $("#dashWx")?.replaceChildren(h("div", { class: "row between" }, h("h2", { style: { margin: 0 } }, "Weather"),
        h("button", { class: "btn sm", onclick: async () => { await act("refresh_weather"); toast("Updating weather…"); setTimeout(loadData, 4000); setTimeout(() => TABS.dash.draw?.(), 4200); } }, "Update now")),
      u("weather.error") ? h("p", { class: "small", style: { color: "var(--bad)" } }, "Last update failed: " + u("weather.error")) : null,
      u("weather.temp") === undefined ? h("p", { class: "muted" }, u("weather.desc") === "Set location" ? "Set a location in Settings." : "Waiting for the first forecast.") :
        h("div", { class: "row" }, h("div", { class: "wx", style: { fontSize: "64px" } }, u("weather.icon")),
          h("div", {}, h("div", { style: { fontSize: "28px" } }, renderTmpl("{weather.temp:0}°")), h("div", { class: "muted" }, renderTmpl("{weather.desc} · H {weather.hi:0}° L {weather.lo:0}°")),
            h("div", { class: "small muted" }, renderTmpl("{weather.place} · updated {weather.updated|ago}")))));
  };
  draw();
  TABS.dash.draw = draw;
};
async function act(action, page) {
  try { await api("/api/action", { body: { action, page } }); setTimeout(pollStatus, 400); } catch (e) { toast(e.message, true); }
}

// ============================================================ LAYOUT EDITOR
const WIDGETS = {
  clock:    { label: "Clock", def: { w: 8, h: 3, size: 96, ampm: true, color: "fg" } },
  date:     { label: "Date", def: { w: 8, h: 1, size: 22, fmt: "%A, %b %e", color: "muted" } },
  text:     { label: "Text / template", def: { w: 8, h: 1, size: 22, text: "Hello {sys.name}", color: "fg", align: "center" } },
  value:    { label: "Big value", def: { w: 4, h: 3, size: 48, value: "{claude.five_hour.used_percentage:0}%", label: "Session", key: "claude.five_hour.used_percentage", color_mode: "threshold", warn: 60, bad: 85 } },
  bar:      { label: "Progress bar", def: { w: 10, h: 3, key: "claude.five_hour.used_percentage", label: "Current session", sub: "Resets in {claude.five_hour.resets_at|in}", size: 22, bar_h: 34, value_pos: "inside", value_size: 20, color_mode: "fixed", color: "#D97757", track: "#2A1A14" } },
  ring:     { label: "Ring gauge", def: { w: 6, h: 6, key: "claude.five_hour.used_percentage", thickness: 14, start: 135, sweep: 270, size: 40, caption: "Session", color_mode: "fixed", color: "#D97757", track: "#2A1A14" } },
  weather:  { label: "Weather now", def: { w: 6, h: 2, icon_size: 48, size: 32, style: "row", show_desc: false } },
  forecast: { label: "Forecast", def: { w: 12, h: 4, mode: "daily", count: 5, icon_size: 32, size: 18, show_wind: true } },
  icon:     { label: "Icon", def: { w: 2, h: 2, glyph: "", size: 64, color: "fg" } },
  box:      { label: "Box / panel", def: { w: 6, h: 3, bg: "track", radius: 16 } },
  qr:       { label: "Settings QR", def: { w: 4, h: 4 } },
  media:    { label: "Picture / video", def: { x: 0, y: 0, w: 12, h: 12, file: "" } },
};
const COLOR_OPTS = [["fg", "Text"], ["muted", "Muted"], ["accent", "Accent"], ["ok", "OK"], ["warn", "Warning"], ["bad", "Alert"], ["track", "Track"], ["bg", "Background"], ["custom", "Custom…"]];
const KEY_HINTS = {
  "claude.five_hour.used_percentage": "5-hour session usage %", "claude.five_hour.resets_at": "5-hour reset time",
  "claude.seven_day.used_percentage": "Weekly usage %", "claude.seven_day.resets_at": "Weekly reset time",
  "claude.context_window.used_percentage": "Context window %", "claude.model.display_name": "Model", "claude.cost.total_cost_usd": "Session cost $",
  "claude.updated": "Last push time", "weather.temp": "Temperature", "weather.feels": "Feels like", "weather.desc": "Conditions", "weather.hi": "Today's high",
  "weather.lo": "Today's low", "weather.humidity": "Humidity %", "weather.wind": "Wind", "weather.pp": "Rain chance %", "weather.wind_unit": "mph or km/h", "weather.d0.wind": "Today's max wind", "weather.sunrise": "Sunrise", "weather.sunset": "Sunset",
  "weather.icon": "Weather glyph", "weather.place": "Location name", "sys.ip": "IP address", "sys.url": "Settings URL", "sys.battery": "Battery %", "sys.battery_text": "Battery text",
  "sys.rssi": "Wi-Fi signal", "sys.uptime_text": "Uptime", "sys.name": "Device name", "sys.page_name": "Current page", "bambu.percent": "Print % complete", "bambu.layer": "Current layer", "bambu.total_layers": "Total layers",
  "bambu.eta": "Print finish time", "bambu.remaining_text": "Print time left", "bambu.state_text": "Printer state", "bambu.job": "Print job name",
  "bambu.nozzle": "Nozzle temp", "bambu.bed": "Bed temp", "bambu.connected": "Printer connected (1/0)" };
const DATE_PRESETS = [["%A, %b %e", "Friday, Nov 21"], ["%a %b %e", "Fri Nov 21"], ["%B %e, %Y", "November 21, 2026"], ["%m/%d", "11/21"], ["%A", "Friday"], ["%b %e", "Nov 21"]];

function propSchema(t) {
  const size = ["size", "Text size", "font"], color = ["color", "Color", "color"], align = ["align", "Align", ["left", "center", "right"]];
  const thr = [["color_mode", "Color mode", [["threshold", "By threshold"], ["fixed", "Fixed"]]], ["warn", "Warning at", "num"], ["bad", "Alert at", "num"]];
  const S_ = {
    clock: [size, color, ["h24", "Format", [["auto", "Follow settings"], ["12", "12-hour"], ["24", "24-hour"]]], ["ampm", "Show AM/PM", "bool"], ["seconds", "Show seconds", "bool"], ["ampm_color", "AM/PM color", "color"]],
    date: [["fmt", "Format", "datefmt"], size, color, align],
    text: [["text", "Text (templates allowed)", "tmpl"], size, color, align, ["overflow", "Overflow", ["wrap", "clip", "scroll"]]],
    value: [["value", "Value (template)", "tmpl1"], ["label", "Label (template)", "tmpl1"], ["key", "Threshold key", "key"], size, ["label_size", "Label size", "font"], color, align, ...thr],
    bar: [["key", "Data key", "key"], ["value_pos", "Value position", [["inside", "On the bar (moves with fill)"], ["follow", "Above the bar (moves with fill)"], ["right", "Right of label"]]],
      ["value_size", "Value size", "font"], ["value_color", "Value color on fill", "color"], ["label_color", "Text color", "color"], ["label", "Label", "tmpl1"], ["sub", "Sub text (template)", "tmpl1"], ["empty", "Text when no data", "text"], ["min", "Min", "num"], ["max", "Max", "num"], ["unit", "Unit", "text"],
      size, ["sub_size", "Sub text size", "font"], ["bar_h", "Bar height px", "num"], ["show_value", "Show value", "bool"], ...thr, color, ["track", "Track color", "color"]],
    ring: [["key", "Data key", "key"], ["value_pos", "Value", [["center", "Center text"], ["tip", "Badge at arc tip (moves)"], ["none", "Hidden"]]],
      ["badge", "Badge text (template)", "tmpl1"], ["badge_size", "Badge size", "font"], ["text", "Center text (template)", "tmpl1"], ["caption", "Caption (template)", "tmpl1"], ["min", "Min", "num"], ["max", "Max", "num"],
      ["start", "Start angle°", "num"], ["sweep", "Sweep°", "num"], ["thickness", "Thickness px", "num"], ["rounded", "Rounded ends", "bool"], size, ["caption_size", "Caption size", "font"], ...thr, color, ["track", "Track color", "color"]],
    weather: [["icon_size", "Icon size", [[32, "Small"], [64, "Medium"], [128, "Large"]]], size, ["style", "Arrangement", [["row", "Side by side"], ["stack", "Stacked"]]], ["show_desc", "Show conditions", "bool"], ["desc_size", "Conditions size", "font"], ["show_unit", "Show °F/°C letter", "bool"], color, ["icon_color", "Icon color", "color"]],
    forecast: [["mode", "Mode", [["daily", "Daily"], ["hourly", "Hourly"]]], ["count", "Columns", "num"], ["step", "Hours between (hourly)", "num"], ["icon_size", "Icon size", [[32, "Small"], [64, "Medium"]]], size, color, ["show_wind", "Show wind speed (daily)", "bool"], ["wind_size", "Wind text size", "font"]],
    icon: [["glyph", "Icon", "glyph"], ["key", "…or from data key", "key"], ["size", "Size", [[32, "Small"], [64, "Medium"], [128, "Large"]]], color],
    box: [], qr: [], media: [["file", "File on the microSD card", "mediafile"]],
  };
  return S_[t] || [];
}
const COMMON_PROPS = [["bg", "Background", "color"], ["radius", "Corner radius", "num"], ["border", "Border px", "num"], ["border_color", "Border color", "color"], ["pad", "Padding px", "num"]];

const Editor = {
  layout: null, page: 0, sel: -1, dirty: false, previewing: false, undo: [], redo: [], grid: true, nightPreview: false,
  load(l) { this.layout = clone(l); this.page = Math.min(this.page, this.layout.pages.length - 1); this.sel = -1; this.dirty = false; this.undo = []; this.redo = []; },
  snapshot() { this.undo.push(JSON.stringify(this.layout)); if (this.undo.length > 80) this.undo.shift(); this.redo = []; this.dirty = true; },
  doUndo() { if (!this.undo.length) return; this.redo.push(JSON.stringify(this.layout)); this.layout = JSON.parse(this.undo.pop()); this.fix(); this.dirty = true; drawEditor(); },
  doRedo() { if (!this.redo.length) return; this.undo.push(JSON.stringify(this.layout)); this.layout = JSON.parse(this.redo.pop()); this.fix(); this.dirty = true; drawEditor(); },
  fix() { this.page = Math.max(0, Math.min(this.page, this.layout.pages.length - 1)); const n = this.pg().widgets.length; if (this.sel >= n) this.sel = -1; },
  pg() { return this.layout.pages[this.page]; },
  w() { return this.sel >= 0 ? this.pg().widgets[this.sel] : null; },
};

window.addEventListener("beforeunload", e => { if (Editor.dirty) { e.preventDefault(); e.returnValue = ""; } });
TABS.layout = async () => {
  if (!Editor.layout) Editor.load(S.layout || await api("/api/layout"));
  drawEditor();
};

function switchEl(on, onchange, title) {
  const c = h("input", { type: "checkbox", class: "sw", title: title || "" }); c.checked = !!on;
  c.onclick = e => e.stopPropagation(); c.onchange = () => onchange(c.checked);
  return c;
}
function addPageMenu(L) {
  return sel([["", "+ Add page…"], ["blank", "Blank page"], ["dup", "Copy of this page"], ["Overview", "Overview (printer + Claude bars)"], ["Printer", "Bambu printer"]], "", async v => {
    if (!v) return;
    let pg;
    if (v === "blank") pg = { name: "Page " + (L.pages.length + 1), enabled: true, duration: 15, widgets: [] };
    else if (v === "dup") { pg = clone(Editor.pg()); pg.name += " copy"; }
    else { pg = await presetPage(v); if (!pg) { toast("Preset not found", true); return drawEditor(); } }
    Editor.snapshot(); pg.enabled = true; L.pages.push(pg); Editor.page = L.pages.length - 1; Editor.sel = -1; drawEditor();
    toast(`Added "${pg.name}" — press Save to display to send it`);
  });
}
function drawEditor() {
  const el = $("#tab-layout");
  const L = Editor.layout;
  const shown = L.pages.filter(p => p.enabled !== false).length;
  const more = h("details", { class: "menu" }, h("summary", { class: "btn sm" }, "More ▾"),
    h("div", { class: "menu-body" },
      h("div", { class: "small muted" }, "Start from a template (replaces everything, you can undo):"),
      sel([["", "Choose template…"], ["default", "Default (4 pages)"], ["bigclock", "Big clock"], ["usage", "Usage focus"], ["minimal", "Minimal"]], "", applyTemplate),
      h("div", { class: "row" }, h("button", { class: "btn sm", onclick: exportLayout, title: "Download this design as a file" }, "Export to file"),
        h("button", { class: "btn sm", onclick: importLayout, title: "Load a design file (then Save to display)" }, "Import file")),
      check("Show grid", Editor.grid, v => { Editor.grid = v; drawEditor(); }),
      check("Edit night colors", Editor.nightPreview, v => { Editor.nightPreview = v; drawEditor(); })));
  const status = Editor.dirty
    ? h("div", { class: "savebar dirty" }, h("b", {}, "● Unsaved changes"), h("span", { class: "small" }, "The display still shows the old design until you save."))
    : h("div", { class: "savebar ok" }, h("b", {}, "✓ Saved"), h("span", { class: "small muted" }, "The display matches this design."));
  const tb = h("div", { class: "toolbar" },
    h("button", { class: "btn" + (Editor.dirty ? " primary pulse" : ""), onclick: saveLayout, disabled: !Editor.dirty, title: "Send this design to the display and keep it" }, "Save to display"),
    h("button", { class: "btn sm", disabled: !Editor.dirty, onclick: () => { if (confirm("Throw away your unsaved changes?")) { Editor.load(S.layout); drawEditor(); } } }, "Discard changes"),
    h("span", { class: "sep" }),
    h("button", { class: "btn sm icon", onclick: () => Editor.doUndo(), disabled: !Editor.undo.length, title: "Undo (Ctrl+Z)" }, "↶ Undo"),
    h("button", { class: "btn sm icon", onclick: () => Editor.doRedo(), disabled: !Editor.redo.length, title: "Redo (Ctrl+Y)" }, "↷ Redo"),
    h("span", { class: "sep" }),
    Editor.previewing
      ? h("button", { class: "btn sm live", onclick: stopPreview, title: "Put the saved design back on the display" }, "■ Stop trying on display")
      : h("button", { class: "btn sm", onclick: previewOnDevice, title: "Show this design on the display for 10 minutes without saving it" }, "▶ Try on display"),
    more, h("span", { class: "grow" }), status);

  // left: pages
  const pages = h("div", { class: "card stack" },
    h("div", { class: "row between" }, h("h2", { style: { margin: 0 } }, "Pages"), h("span", { class: "small muted" }, `${shown} of ${L.pages.length} on the display`)),
    h("p", { class: "small muted", style: { margin: 0 } }, "Click a page to edit it. The switch shows or hides it on the display; ★ is the page it starts on."),
    ...L.pages.map((p, i) => {
      const on = p.enabled !== false;
      return h("div", { class: "list-item" + (i === Editor.page ? " sel" : "") + (on ? "" : " off"), onclick: () => { Editor.page = i; Editor.sel = -1; drawEditor(); } },
        switchEl(on, v => { Editor.snapshot(); p.enabled = v; if (!v && shown <= 1) { p.enabled = true; toast("At least one page has to stay on", true); } drawEditor(); }, on ? "Shown on the display — click to hide" : "Hidden — click to show"),
        h("span", { class: "grow" }, h("span", { class: "pnum" }, i + 1 + "."), " ", p.name || "Page " + (i + 1)),
        (L.home ?? 0) === i ? h("span", { class: "star", title: "Home page" }, "★") : null,
        h("button", { class: "btn sm icon", title: "Move up", disabled: i === 0, onclick: e => { e.stopPropagation(); movePage(i, -1); } }, "↑"),
        h("button", { class: "btn sm icon", title: "Move down", disabled: i === L.pages.length - 1, onclick: e => { e.stopPropagation(); movePage(i, 1); } }, "↓"));
    }),
    addPageMenu(L));
  const pg = Editor.pg();
  const autoRotate = get(S.settings, "controls.auto_rotate.enabled", false);
  const opt = (title, hint, control) => h("label", { class: "opt" }, h("span", {}, h("b", {}, title), h("span", { class: "small muted" }, hint)), control);
  const pageProps = h("div", { class: "card stack" }, h("h2", {}, "Page settings"),
    field("Name", inp("text", pg.name, v => { pg.name = v; Editor.dirty = true; }, { onchange: () => { Editor.snapshot(); drawEditor(); } })),
    opt("Show on display", "Off = kept here but skipped on the device", switchEl(pg.enabled !== false, v => { Editor.snapshot(); pg.enabled = v; drawEditor(); })),
    opt("Home page", "Shown at start-up and after being idle", switchEl((L.home ?? 0) === Editor.page, v => { Editor.snapshot(); L.home = v ? Editor.page : 0; drawEditor(); })),
    opt("Stay on this page", "Don't jump back to the home page when idle", switchEl(pg.stay ?? (pg.name === "Printer" || pg.name === "Overview"), v => { Editor.snapshot(); pg.stay = v; drawEditor(); })),
    autoRotate ? field("Seconds on this page", inp("number", pg.duration ?? 15, v => { pg.duration = v || 0; Editor.dirty = true; }, { min: 0, max: 3600 }), " (auto-rotate)") : null,
    field("Background", colorField(pg.bg || "", v => { Editor.snapshot(); if (v) pg.bg = v; else delete pg.bg; drawEditor(); }, true)),
    h("button", { class: "btn sm danger", disabled: L.pages.length < 2, onclick: () => { if (!confirm(`Delete the page "${pg.name}"?`)) return; Editor.snapshot(); L.pages.splice(Editor.page, 1); if ((L.home ?? 0) >= L.pages.length) L.home = 0; Editor.fix(); drawEditor(); } }, "Delete this page"));
  const themeKey = Editor.nightPreview ? "night_theme" : "theme";
  if (!L[themeKey]) L[themeKey] = clone(themeKey === "theme" ? DEFAULT_THEME : themeOf(L, false));
  const theme = h("details", { class: "card" }, h("summary", { class: "h2like" }, Editor.nightPreview ? "Night colors" : "Colors (all pages)"),
    h("div", { class: "swatches", style: { marginTop: "12px" } }, ...THEME_KEYS.map(k => h("label", {}, k, inp("color", L[themeKey][k] || DEFAULT_THEME[k], v => { L[themeKey][k] = v.toUpperCase(); Editor.dirty = true; redrawCanvas(); }, { onchange: () => Editor.snapshot() })))),
    h("p", { class: "small muted" }, "Widgets use these by name, so one change updates every page. Night colors: More ▾ → Edit night colors."));
  const left = h("div", { class: "side" }, pages, pageProps, theme);

  // center: canvas
  const wrap = h("div", { class: "screen-wrap", id: "edWrap" });
  const center = h("div", { class: "canvas-col" }, h("div", { class: "card stack" },
    h("div", { class: "row between" }, h("h2", { style: { margin: 0 } }, `${Editor.page + 1}. ${pg.name || "Page"}`),
      h("div", { class: "row" }, h("button", { class: "btn sm icon", disabled: Editor.page === 0, onclick: () => { Editor.page--; Editor.sel = -1; drawEditor(); }, title: "Previous page" }, "‹"),
        h("button", { class: "btn sm icon", disabled: Editor.page >= L.pages.length - 1, onclick: () => { Editor.page++; Editor.sel = -1; drawEditor(); }, title: "Next page" }, "›"))),
    pg.enabled === false ? h("div", { class: "warnbar" }, "This page is hidden, so the display skips it. ", h("button", { class: "btn sm", onclick: () => { Editor.snapshot(); pg.enabled = true; drawEditor(); } }, "Show it")) : null,
    h("div", { class: "bezel" }, wrap),
    h("details", { class: "small muted" }, h("summary", { style: { cursor: "pointer" } }, "How to edit"),
      h("ul", { style: { margin: "6px 0 0", paddingLeft: "18px" } },
        h("li", {}, "Click a widget to select it, drag to move, drag the orange corner to resize."),
        h("li", {}, "Add widgets from the list on the right; change the selected one under it."),
        h("li", {}, "Keyboard: arrows move · Shift+arrows resize · Delete removes · Ctrl+D duplicates · Ctrl+Z undo."),
        h("li", {}, "Nothing reaches the display until you press ", h("b", {}, "Save to display"), ".")))));

  // right: palette + props
  const palette = h("div", { class: "card stack" }, h("h2", {}, "Add to this page"),
    h("div", { class: "palette" }, ...Object.entries(WIDGETS).map(([t, d]) => h("button", { class: "btn sm", onclick: () => addWidget(t) }, d.label))));
  const right = h("div", { class: "side" }, palette, propsPanel());
  el.replaceChildren(tb, h("div", { class: "editor" }, left, center, right));
  redrawCanvas();
}

function redrawCanvas() {
  const wrap = $("#edWrap"); if (!wrap) return;
  const scr = renderScreen(Editor.pg(), themeOf(Editor.layout, Editor.nightPreview), S.data, { edit: true, selected: Editor.sel, grid: Editor.grid });
  mountScreen(wrap, scr);
  scr.addEventListener("pointerdown", onCanvasDown);
}

function onCanvasDown(e) {
  const scr = e.currentTarget;
  const wEl = e.target.closest(".w");
  if (!wEl) { Editor.sel = -1; drawEditor(); return; }
  const idx = +wEl.dataset.idx;
  const resizing = e.target.classList.contains("handle");
  if (Editor.sel !== idx) { Editor.sel = idx; drawEditor(); }
  const w = Editor.pg().widgets[idx];
  const target = $("#edWrap .screen");                  // may have been re-rendered by the selection change
  const scale = target.getBoundingClientRect().width / 480 || 1;
  const sx = e.clientX, sy = e.clientY, ox = w.x || 0, oy = w.y || 0, ow = w.w || 1, oh = w.h || 1;
  let moved = false;
  const before = JSON.stringify(Editor.layout);
  target.setPointerCapture?.(e.pointerId);
  const move = ev => {
    const dx = Math.round((ev.clientX - sx) / scale / 40), dy = Math.round((ev.clientY - sy) / scale / 40);
    if (resizing) { w.w = Math.max(1, Math.min(12 - ox, ow + dx)); w.h = Math.max(1, Math.min(12 - oy, oh + dy)); }
    else { w.x = Math.max(0, Math.min(12 - ow, ox + dx)); w.y = Math.max(0, Math.min(12 - oh, oy + dy)); }
    if (w.x !== ox || w.y !== oy || w.w !== ow || w.h !== oh) { moved = true; redrawCanvasKeepCapture(); }
  };
  const up = () => {
    target.removeEventListener("pointermove", move); target.removeEventListener("pointerup", up); target.removeEventListener("pointercancel", up);
    if (moved) { Editor.undo.push(before); Editor.redo = []; Editor.dirty = true; drawEditor(); }
  };
  target.addEventListener("pointermove", move); target.addEventListener("pointerup", up); target.addEventListener("pointercancel", up);
  e.preventDefault();
}
function redrawCanvasKeepCapture() {
  // Re-render widgets inside the same .screen element so the pointer capture survives.
  const scr = $("#edWrap .screen");
  const fresh = renderScreen(Editor.pg(), themeOf(Editor.layout, Editor.nightPreview), S.data, { edit: true, selected: Editor.sel, grid: Editor.grid });
  scr.replaceChildren(...fresh.childNodes);
}

document.addEventListener("keydown", e => {
  if (S.tab !== "layout" || !Editor.layout) return;
  const tag = document.activeElement?.tagName;
  if (tag === "INPUT" || tag === "TEXTAREA" || tag === "SELECT") return;
  const k = e.key, mod = e.ctrlKey || e.metaKey;
  if (mod && k.toLowerCase() === "z") { e.preventDefault(); e.shiftKey ? Editor.doRedo() : Editor.doUndo(); return; }
  if (mod && k.toLowerCase() === "y") { e.preventDefault(); Editor.doRedo(); return; }
  const w = Editor.w(); if (!w) return;
  if (mod && k.toLowerCase() === "d") { e.preventDefault(); dupWidget(); return; }
  if (k === "Delete" || k === "Backspace") { e.preventDefault(); delWidget(); return; }
  const d = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] }[k];
  if (!d) return;
  e.preventDefault(); Editor.snapshot();
  if (e.shiftKey) { w.w = Math.max(1, Math.min(12 - w.x, w.w + d[0])); w.h = Math.max(1, Math.min(12 - w.y, w.h + d[1])); }
  else { w.x = Math.max(0, Math.min(12 - w.w, w.x + d[0])); w.y = Math.max(0, Math.min(12 - w.h, w.y + d[1])); }
  drawEditor();
});

function addWidget(t) {
  Editor.snapshot();
  const d = clone(WIDGETS[t].def);
  const w = Object.assign({ type: t, x: Math.max(0, Math.floor((12 - d.w) / 2)), y: Math.max(0, Math.floor((12 - d.h) / 2)) }, d);
  Editor.pg().widgets.push(w);
  Editor.sel = Editor.pg().widgets.length - 1;
  drawEditor();
}
function dupWidget() { const w = Editor.w(); if (!w) return; Editor.snapshot(); const c = clone(w); c.x = Math.min(12 - c.w, c.x + 1); c.y = Math.min(12 - c.h, c.y + 1); Editor.pg().widgets.push(c); Editor.sel = Editor.pg().widgets.length - 1; drawEditor(); }
function delWidget() { if (!Editor.w()) return; Editor.snapshot(); Editor.pg().widgets.splice(Editor.sel, 1); Editor.sel = -1; drawEditor(); }
function orderWidget(dir) {
  const ws = Editor.pg().widgets, i = Editor.sel, j = dir > 0 ? ws.length - 1 : 0;
  if (i < 0 || i === j) return; Editor.snapshot(); const [w] = ws.splice(i, 1); if (dir > 0) ws.push(w); else ws.unshift(w); Editor.sel = dir > 0 ? ws.length - 1 : 0; drawEditor();
}
function movePage(i, d) {
  const P = Editor.layout.pages, j = i + d; if (j < 0 || j >= P.length) return;
  Editor.snapshot(); [P[i], P[j]] = [P[j], P[i]];
  const home = Editor.layout.home ?? 0; if (home === i) Editor.layout.home = j; else if (home === j) Editor.layout.home = i;
  Editor.page = j; drawEditor();
}

function colorField(value, onchange, allowNone = false) {
  const isCustom = value && value[0] === "#";
  const opts = (allowNone ? [["", "None"]] : []).concat(COLOR_OPTS);
  const picker = inp("color", isCustom ? value : "#ffffff", v => onchange(v.toUpperCase()), { style: { display: isCustom ? "" : "none" } });
  const s = sel(opts, isCustom ? "custom" : (value || ""), v => {
    if (v === "custom") { picker.style.display = ""; onchange(picker.value.toUpperCase()); } else { picker.style.display = "none"; onchange(v); }
  });
  return h("div", { class: "colorfield" }, s, picker);
}
function keyField(value, onchange) {
  const id = "keys-dl";
  if (!$("#" + id)) document.body.append(h("datalist", { id }));
  const all = [...new Set([...Object.keys(KEY_HINTS), ...Object.keys(S.data)])].sort();
  $("#" + id).replaceChildren(...all.map(k => h("option", { value: k }, KEY_HINTS[k] || "")));
  const el = inp("text", value, onchange, { list: id, placeholder: "e.g. claude.five_hour.used_percentage" });
  el.setAttribute("list", id);
  return el;
}
function tmplHelp(target) {
  const insert = txt => { const s = target.selectionStart ?? target.value.length; target.value = target.value.slice(0, s) + txt + target.value.slice(target.selectionEnd ?? s); target.dispatchEvent(new Event("input")); target.focus(); };
  return h("div", { class: "row" },
    sel([["", "Insert data…"], ...[...new Set([...Object.keys(KEY_HINTS), ...Object.keys(S.data)])].sort().map(k => [k, k + (KEY_HINTS[k] ? " — " + KEY_HINTS[k] : "")])], "", v => { if (v) insert(`{${v}}`); }),
    sel([["", "Format…"], [":0", "No decimals"], [":1", "1 decimal"], ["|pct", "Percent"], ["|in", "Time until"], ["|ago", "Time since"], ["|time", "Clock time"], ["|day", "Weekday"], ["|date", "Date"], ["|hour", "Hour"], ["|upper", "UPPERCASE"]], "", v => {
      if (!v) return; const m = target.value.slice(0, target.selectionStart ?? target.value.length).lastIndexOf("}");
      if (m >= 0) { target.value = target.value.slice(0, m) + v + target.value.slice(m); target.dispatchEvent(new Event("input")); } else toast("Put the cursor after a {key} first");
    }),
    sel([["", "Symbol…"], ...Object.keys(SYMS).concat(["battery"]).map(k => [k, k])], "", v => { if (v) insert(`{sym:${v}}`); }));
}

function propsPanel() {
  const w = Editor.w();
  const card = h("div", { class: "card props" });
  if (!w) { card.append(h("h2", {}, "Selected widget"), h("p", { class: "muted" }, "Nothing selected. Click a widget on the screen to change its text, data, colors and size.")); return card; }
  const change = (k, v, redraw = false) => {
    if (v === null || v === "" || v === undefined) delete w[k]; else w[k] = v;
    Editor.dirty = true; redraw ? drawEditor() : redrawCanvas();
  };
  const snap = () => Editor.snapshot();
  const mk = ([k, label, kind]) => {
    let input;
    if (Array.isArray(kind)) input = sel(kind, w[k] ?? (kind[0][0] ?? kind[0]), v => { snap(); change(k, isNaN(+v) || v === "" ? v : (kind.some(o => typeof (o[0] ?? o) === "number") ? +v : v)); });
    else if (kind === "num") input = inp("number", w[k], v => change(k, v), { onfocus: snap });
    else if (kind === "bool") input = h("label", { class: "check" }, inp("checkbox", w[k] ?? (k === "ampm" || k === "show_value" || k === "rounded"), v => { snap(); change(k, v); }), "");
    else if (kind === "font") input = sel(FONT_SIZES.map(s => [s, s + " px" + (s >= 96 ? " (digits only)" : "")]), nearestFont(w[k] ?? (k === "size" ? 24 : 16)), v => { snap(); change(k, +v); });
    else if (kind === "color") input = colorField(w[k] || "", v => { snap(); change(k, v); }, k === "bg" || k === "ampm_color" || k === "icon_color");
    else if (kind === "key") input = keyField(w[k] || "", v => change(k, v));
    else if (kind === "mediafile") input = mediaFileField(w[k] || "", v => { snap(); change(k, v); });
    else if (kind === "glyph") input = sel(Object.entries(WX_GLYPHS).map(([n, g]) => [g, n]), w[k] || "", v => { snap(); change(k, v); });
    else if (kind === "datefmt") { const t = inp("text", w[k] || "%A, %b %e", v => change(k, v), { onfocus: snap }); input = h("div", { class: "stack" }, t, sel([["", "Presets…"], ...DATE_PRESETS], "", v => { if (v) { snap(); t.value = v; change(k, v); } }), h("span", { class: "small muted" }, "%A weekday · %a short · %B month · %b short · %e day · %Y year · %H:%M time")); }
    else if (kind === "tmpl" || kind === "tmpl1") { const t = kind === "tmpl" ? h("textarea", { onfocus: snap }) : h("input", { type: "text", onfocus: snap }); t.value = w[k] ?? ""; t.oninput = () => change(k, t.value); input = h("div", { class: "stack" }, t, tmplHelp(t)); }
    else input = inp("text", w[k] ?? "", v => change(k, v), { onfocus: snap });
    return field(label, input);
  };
  const pos = h("div", { class: "three" }, ...["x", "y", "w"].map(k => field(k.toUpperCase(), inp("number", w[k], v => { snap(); change(k, Math.max(k === "w" ? 1 : 0, Math.min(12, v || 0))); }, { min: 0, max: 12 }))));
  card.append(h("div", { class: "row between" }, h("h2", { style: { margin: 0 } }, WIDGETS[w.type]?.label || w.type), h("span", { class: "badge" }, w.type)),
    h("div", { class: "row" },
      h("button", { class: "btn sm", onclick: dupWidget }, "Duplicate"),
      h("button", { class: "btn sm", onclick: () => orderWidget(1), title: "Draw on top" }, "To front"),
      h("button", { class: "btn sm", onclick: () => orderWidget(-1), title: "Draw underneath" }, "To back"),
      h("button", { class: "btn sm danger", onclick: delWidget }, "Delete")),
    h("div", { class: "grp" }, pos, field("Height", inp("number", w.h, v => { snap(); change("h", Math.max(1, Math.min(12, v || 1))); }, { min: 1, max: 12 }))),
    h("div", { class: "grp" }, ...propSchema(w.type).map(mk)),
    h("details", {}, h("summary", { class: "muted small", style: { cursor: "pointer" } }, "Box style"), h("div", { class: "grp", style: { marginTop: "10px" } }, ...COMMON_PROPS.map(mk))));
  return card;
}

async function previewOnDevice() {
  try { await api("/api/layout/preview", { body: Editor.layout }); Editor.previewing = true; drawEditor(); toast("Trying it on the display (not saved). Goes back after 10 min."); }
  catch (e) { toast(e.message, true); }
}
async function stopPreview() {
  try { await api("/api/layout/revert", { method: "POST" }); } catch { }
  Editor.previewing = false; drawEditor(); toast("Display is back to the saved design");
}
const layoutSig = l => JSON.stringify(l, (k, v) => typeof v === "number" ? Math.round(v * 1000) / 1000 : v);
async function saveLayout() {
  try {
    await api("/api/layout", { body: Editor.layout });
    // The display applies it a moment later; confirm it actually took.
    let okSaved = false;
    for (let i = 0; i < 6 && !okSaved; i++) { await sleep(500); try { okSaved = layoutSig(await api("/api/layout")) === layoutSig(Editor.layout); } catch { } }
    if (!okSaved) throw new Error("the display didn't apply it - see Maintenance → Logs");
    S.layout = clone(Editor.layout); Editor.dirty = false; Editor.previewing = false; toast("Saved — the display now shows this design"); drawEditor();
  } catch (e) { toast("Save failed: " + e.message, true); }
}
function exportLayout() {
  const blob = new Blob([JSON.stringify(Editor.layout, null, 2)], { type: "application/json" });
  const a = h("a", { href: URL.createObjectURL(blob), download: "deskdisplay-layout.json" }); a.click();
}
function importLayout() {
  const f = h("input", { type: "file", accept: ".json,application/json" });
  f.onchange = async () => {
    try { const l = JSON.parse(await f.files[0].text()); if (!Array.isArray(l.pages)) throw new Error("not a layout file"); Editor.snapshot(); Editor.layout = l; Editor.fix(); drawEditor(); toast("Imported — Save to keep it"); }
    catch (e) { toast("Import failed: " + e.message, true); }
  };
  f.click();
}
async function applyTemplate(name) {
  if (!name) return;
  if (!confirm("Replace the current design with this template? (You can undo.)")) { drawEditor(); return; }
  Editor.snapshot();
  const base = await api("/api/layout/default");
  const T = {
    default: base,
    bigclock: Object.assign(clone(base), { home: 0, pages: [{ name: "Clock", enabled: true, duration: 20, widgets: [
      { type: "clock", x: 0, y: 3, w: 12, h: 4, size: 128, ampm: false },
      { type: "date", x: 1, y: 7, w: 10, h: 1, size: 28, fmt: "%A, %b %e", color: "muted" },
      { type: "weather", x: 3, y: 9, w: 6, h: 2, icon_size: 48, size: 32 }] }, ...base.pages.slice(1)] }),
    usage: Object.assign(clone(base), { home: 0, pages: [{ name: "Usage", enabled: true, duration: 20, widgets: [
      { type: "ring", x: 0, y: 0, w: 12, h: 12, key: "claude.five_hour.used_percentage", thickness: 22, start: 135, sweep: 270, size: 96, text: "{claude.five_hour.used_percentage:0}", caption: "", color_mode: "fixed", color: "#D97757", track: "#2A1A14" },
      { type: "text", x: 2, y: 7, w: 8, h: 1, size: 20, color: "muted", align: "center", text: "session · resets in {claude.five_hour.resets_at|in}" },
      { type: "bar", x: 2, y: 8, w: 8, h: 2, key: "claude.seven_day.used_percentage", label: "", sub: "", size: 18, bar_h: 26, value_pos: "inside", value_size: 16, color_mode: "fixed", color: "#D97757", track: "#2A1A14" },
      { type: "clock", x: 3, y: 1, w: 6, h: 2, size: 40, ampm: true, color: "muted" }] }, ...base.pages.slice(1)] }),
    minimal: Object.assign(clone(base), { home: 0, pages: [{ name: "Minimal", enabled: true, duration: 20, widgets: [
      { type: "clock", x: 1, y: 4, w: 10, h: 3, size: 96, ampm: false },
      { type: "text", x: 1, y: 7, w: 10, h: 1, size: 20, color: "muted", align: "center", text: "{weather.temp:0}°  ·  {claude.five_hour.used_percentage:0}%" }] }] }),
  };
  Editor.layout = clone(T[name]); Editor.page = 0; Editor.sel = -1; Editor.dirty = true; drawEditor();
}

async function presetPage(name) {
  const def = await api("/api/layout/default");
  const pg = (def.pages || []).find(p => p.name === name);
  return pg ? clone(pg) : null;
}
async function addPrinterPageToDevice() {
  try {
    const lay = await api("/api/layout");
    let pg = lay.pages.find(p => p.name === "Printer");
    if (pg) pg.enabled = true;
    else { pg = await presetPage("Printer"); if (!pg) throw new Error("preset missing"); pg.enabled = true; lay.pages.push(pg); }
    await api("/api/layout", { body: lay });
    S.layout = lay; Editor.layout = null;
    toast("Printer page added to the display");
  } catch (e) { toast("Couldn't add the page: " + e.message, true); }
}

// ============================================================ CONTROLS
const ACTIONS = [["next", "Next page"], ["prev", "Previous page"], ["home", "Go to home page"], ["sleep_toggle", "Sleep / wake screen"], ["night_toggle", "Night mode on/off"], ["show_qr", "Show settings QR"], ["refresh", "Refresh data"]];
const EVENTS = {
  Touch: [["tap", "Tap"], ["double_tap", "Double tap"], ["long_press", "Long press"], ["swipe_left", "Swipe left"], ["swipe_right", "Swipe right"], ["swipe_up", "Swipe up"], ["swipe_down", "Swipe down"], ["edge_tap_left", "Tap left edge"], ["edge_tap_right", "Tap right edge"]],
  Motion: [["tilt_left", "Tilt left"], ["tilt_right", "Tilt right"], ["tilt_forward", "Tilt forward"], ["tilt_back", "Tilt back"], ["shake", "Shake"], ["flip", "Flip face-down"], ["pickup", "Pick up"]],
  Buttons: [["boot_short", "BOOT press"], ["boot_long", "BOOT hold"], ["pwr_short", "PWR press"]],
};
const EV_LABEL = Object.fromEntries(Object.values(EVENTS).flat());
let Ctl = null;
TABS.controls = () => {
  Ctl = clone(get(S.settings, "controls", {}));
  drawControls();
};
function drawControls() {
  const el = $("#tab-controls");
  const c = Ctl; c.bindings = c.bindings || {};
  const usedBy = ev => Object.entries(c.bindings).filter(([, l]) => l.includes(ev)).map(([a]) => a);
  const nav = ["next", "prev"].some(a => (c.bindings[a] || []).length) || get(c, "auto_rotate.enabled", false);
  const rows = ACTIONS.map(([a, label]) => {
    const list = c.bindings[a] = c.bindings[a] || [];
    const add = h("select", {}, h("option", { value: "" }, "+ add input"),
      ...Object.entries(EVENTS).map(([g, evs]) => h("optgroup", { label: g }, ...evs.filter(([e]) => !list.includes(e)).map(([e, l]) => h("option", { value: e }, l + (usedBy(e).length ? " (also used)" : ""))))));
    add.onchange = () => { if (add.value) { list.push(add.value); drawControls(); } };
    return h("div", { class: "bind-row" }, h("b", {}, label),
      h("div", { class: "pills" }, ...list.map(e => h("span", { class: "pill", "data-ev": e }, EV_LABEL[e] || e, h("button", { title: "Remove", onclick: () => { c.bindings[a] = list.filter(x => x !== e); drawControls(); } }, "×"))), add));
  });
  const num = (path, label, attrs = {}, hint) => field(label, inp("number", get(c, path), v => set(c, path, v), attrs), hint);
  const imuBox = h("div", { class: "imu", id: "imuBox" });
  const lastBox = h("div", { id: "lastInput", class: "pill live", style: { padding: "6px 12px" } }, "Waiting for input…");
  el.replaceChildren(...[
    h("div", { class: "toolbar" }, h("button", { class: "btn primary", onclick: () => saveSettings({ controls: Ctl }, "Controls saved") }, "Save controls"),
      h("button", { class: "btn", onclick: () => { Ctl = clone(get(S.settings, "controls", {})); drawControls(); } }, "Discard changes"),
      h("span", { class: "muted small" }, "Changes apply as soon as you save.")),
    !nav ? h("div", { class: "card warnbox", style: { marginBottom: "16px" } }, "No input is bound to Next or Previous page and auto-rotate is off. The display falls back to swipe left/right so you can't get stuck.") : null,
    h("div", { class: "grid" },
      h("div", { class: "card", style: { gridColumn: "1 / -1" } }, h("h2", {}, "What each input does"),
        h("p", { class: "muted small" }, "Add any mix of touch, motion and button inputs to each action. Pressing anything wakes a sleeping screen."),
        ...rows,
        h("div", { class: "row", style: { marginTop: "12px" } }, h("span", { class: "muted" }, "Try it on the display:"), lastBox)),
      h("div", { class: "card stack" }, h("h2", {}, "Page automation"),
        check("Auto-rotate pages", get(c, "auto_rotate.enabled", false), v => { set(c, "auto_rotate.enabled", v); drawControls(); }),
        num("auto_rotate.default_s", "Default time per page (s)", { min: 3 }, "per-page times are set in Layout"),
        num("idle_home_s", "Return to home page after idle (s)", { min: 0 }, "0 = never"),
        h("p", { class: "small muted", style: { margin: 0 } }, "Dimming and turning the screen off when idle: Settings → Display."),
        field("Page transition", sel([["slide", "Slide (eased)"], ["dim", "Dim (smoothest)"], ["fade", "Cross-fade"], ["none", "None"]], c.transition || "slide", v => c.transition = v)),
        num("transition_ms", "Transition time (ms)", { min: 0, max: 1000 }),
        field("Page dots", sel([["on_change", "Show briefly on change"], ["always", "Always"], ["never", "Never"]], c.page_dots || "on_change", v => c.page_dots = v))),
      h("div", { class: "card stack" }, h("h2", {}, "Touch tuning"),
        num("swipe_min_px", "Swipe distance (px)", { min: 20, max: 300 }), num("edge_width", "Edge tap zone width (px)", { min: 30, max: 200 }),
        num("long_press_ms", "Long press time (ms)", { min: 300, max: 3000 }), num("double_tap_ms", "Double tap window (ms)", { min: 150, max: 800 }),
        num("cooldown_ms", "Cooldown between repeats (ms)", { min: 0, max: 3000 }),
        h("details", {}, h("summary", { class: "muted small", style: { cursor: "pointer" } }, "Touch orientation (if taps land in the wrong place)"),
          h("div", { class: "stack", style: { marginTop: "8px" } },
            check("Swap X/Y", get(S.settings, "touch.swap_xy", true), v => saveSettings({ touch: { swap_xy: v } })),
            check("Mirror X", get(S.settings, "touch.mirror_x", true), v => saveSettings({ touch: { mirror_x: v } })),
            check("Mirror Y", get(S.settings, "touch.mirror_y", false), v => saveSettings({ touch: { mirror_y: v } }))))),
      h("div", { class: "card stack" }, h("h2", {}, "Motion tuning"),
        h("p", { class: "muted small" }, "Tilts are measured from the display's resting angle. Put it where it normally sits, then calibrate."),
        h("button", { class: "btn", onclick: async () => { await api("/api/calibrate", { method: "POST" }); toast("Resting angle saved"); setTimeout(async () => { S.settings = await api("/api/settings"); Ctl.tilt = get(S.settings, "controls.tilt"); }, 600); } }, "Calibrate resting angle"),
        imuBox,
        num("tilt.threshold_deg", "Tilt angle (°)", { min: 8, max: 70 }), num("tilt.hold_ms", "Hold tilt for (ms)", { min: 0, max: 2000 }),
        check("Swap tilt left/right", get(c, "tilt.invert_lr", false), v => set(c, "tilt.invert_lr", v)),
        check("Swap tilt forward/back", get(c, "tilt.invert_fb", false), v => set(c, "tilt.invert_fb", v)),
        num("shake.threshold_g", "Shake strength (g)", { min: 0.3, max: 3, step: 0.1 }),
        num("flip.hold_ms", "Face-down time before flip (ms)", { min: 200, max: 5000 }),
        num("pickup.threshold_g", "Pick-up sensitivity (g)", { min: 0.05, max: 1, step: 0.01 }, "lower = more sensitive"),
        num("pickup.still_s", "Must be still for (s) first", { min: 1, max: 60 })))].filter(Boolean));
  updateLiveInputs();
}
function updateLiveInputs() {
  const st = S.status.input; if (!st) return;
  const box = $("#imuBox");
  if (box) box.replaceChildren(...(st.imu ? [["Left/right", st.roll, "°"], ["Fwd/back", st.pitch, "°"], ["From rest", st.angle, "°"], ["Motion", st.motion_g, "g"]] : [])
    .map(([l, v, u]) => h("div", {}, h("b", {}, (v ?? 0).toFixed(u === "g" ? 2 : 0) + u), h("span", { class: "small muted" }, l))));
  const last = $("#lastInput");
  if (last && st.last) last.textContent = `${EV_LABEL[st.last] || st.last} · ${st.age_ms < 1500 ? "just now" : Math.round(st.age_ms / 1000) + "s ago"}`;
  $$(".pill[data-ev]").forEach(p => p.classList.toggle("live", p.dataset.ev === st.last && st.age_ms < 1500));
}
onStatus.push(() => { if (S.tab === "controls") updateLiveInputs(); if (S.tab === "dash") TABS.dash.draw?.(); });

// ============================================================ SETTINGS
const TZ = [["America/New_York", "EST5EDT,M3.2.0,M11.1.0"], ["America/Chicago", "CST6CDT,M3.2.0,M11.1.0"], ["America/Denver", "MST7MDT,M3.2.0,M11.1.0"], ["America/Phoenix", "MST7"],
  ["America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"], ["America/Anchorage", "AKST9AKDT,M3.2.0,M11.1.0"], ["Pacific/Honolulu", "HST10"], ["America/Halifax", "AST4ADT,M3.2.0,M11.1.0"],
  ["America/St_Johns", "NST3:30NDT,M3.2.0,M11.1.0"], ["America/Toronto", "EST5EDT,M3.2.0,M11.1.0"], ["America/Vancouver", "PST8PDT,M3.2.0,M11.1.0"], ["America/Mexico_City", "CST6"],
  ["America/Bogota", "<-05>5"], ["America/Lima", "<-05>5"], ["America/Sao_Paulo", "<-03>3"], ["America/Argentina/Buenos_Aires", "<-03>3"], ["America/Santiago", "<-04>4<-03>,M9.1.6/24,M4.1.6/24"],
  ["Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"], ["Europe/Dublin", "IST-1GMT0,M10.5.0,M3.5.0/1"], ["Europe/Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"], ["Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0/3"],
  ["Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"], ["Europe/Madrid", "CET-1CEST,M3.5.0,M10.5.0/3"], ["Europe/Rome", "CET-1CEST,M3.5.0,M10.5.0/3"], ["Europe/Amsterdam", "CET-1CEST,M3.5.0,M10.5.0/3"],
  ["Europe/Stockholm", "CET-1CEST,M3.5.0,M10.5.0/3"], ["Europe/Warsaw", "CET-1CEST,M3.5.0,M10.5.0/3"], ["Europe/Zurich", "CET-1CEST,M3.5.0,M10.5.0/3"], ["Europe/Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4"],
  ["Europe/Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4"], ["Europe/Kyiv", "EET-2EEST,M3.5.0/3,M10.5.0/4"], ["Europe/Istanbul", "<+03>-3"], ["Europe/Moscow", "MSK-3"],
  ["Africa/Cairo", "EET-2EEST,M4.5.5/0,M10.5.4/24"], ["Africa/Johannesburg", "SAST-2"], ["Africa/Lagos", "WAT-1"], ["Africa/Nairobi", "EAT-3"], ["Asia/Dubai", "<+04>-4"],
  ["Asia/Karachi", "PKT-5"], ["Asia/Kolkata", "IST-5:30"], ["Asia/Dhaka", "<+06>-6"], ["Asia/Bangkok", "<+07>-7"], ["Asia/Jakarta", "WIB-7"], ["Asia/Singapore", "<+08>-8"],
  ["Asia/Hong_Kong", "HKT-8"], ["Asia/Shanghai", "CST-8"], ["Asia/Taipei", "CST-8"], ["Asia/Manila", "PST-8"], ["Asia/Seoul", "KST-9"], ["Asia/Tokyo", "JST-9"],
  ["Australia/Perth", "AWST-8"], ["Australia/Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"], ["Australia/Darwin", "ACST-9:30"], ["Australia/Brisbane", "AEST-10"],
  ["Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"], ["Australia/Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3"], ["Pacific/Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"], ["UTC", "UTC0"]];

const SCRIPT_URL = () => `https://raw.githubusercontent.com/${get(S.settings, "update.repo", "dethdonald/Desk.Display.")}/main/tools/deskdisplay-statusline.py`;
function statusCmd(url, token) {
  return (TABS.settings.os || (navigator.userAgent.includes("Windows") ? "win" : "unix")) === "win"
    ? `python C:/Users/${localStorage.getItem("dd-win-user") || "YOUR_NAME"}/.claude/deskdisplay-statusline.py --url ${url} --token ${token}`
    : `python3 ~/.claude/deskdisplay-statusline.py --url ${url} --token ${token}`;
}
TABS.settings = () => {
  const el = $("#tab-settings");
  const s = clone(S.settings || {});
  const sec = (id, title, ...kids) => h("div", { class: "card stack", id }, h("h2", {}, title), ...kids);
  const saveBtn = (label, fn) => h("div", { class: "row" }, h("button", { class: "btn primary", onclick: fn }, label));

  // Wi-Fi
  const ssid = inp("text", get(s, "wifi.ssid"), v => set(s, "wifi.ssid", v), { list: "ssids", autocomplete: "off" });
  const ssidList = h("datalist", { id: "ssids" });
  const scan = h("button", { class: "btn sm", onclick: async () => {
    scan.disabled = true; scan.textContent = "Scanning…";
    for (let i = 0; i < 12; i++) { const r = await api("/api/wifi/scan").catch(() => ({})); if (r.networks) { ssidList.replaceChildren(...r.networks.map(n => h("option", { value: n.ssid }, `${n.rssi} dBm${n.open ? " (open)" : ""}`))); toast(`Found ${r.networks.length} networks`); break; } await sleep(1000); }
    scan.disabled = false; scan.textContent = "Scan";
  } }, "Scan");

  // Time
  const tzSel = sel([...TZ.map(([n]) => [n, n.replace(/_/g, " ")]), ["custom", "Custom POSIX string…"]], TZ.some(([n]) => n === get(s, "time.tz_name")) ? get(s, "time.tz_name") : "custom", v => {
    if (v === "custom") { custom.style.display = ""; set(s, "time.tz_name", "custom"); }
    else { custom.style.display = "none"; set(s, "time.tz_name", v); set(s, "time.tz", TZ.find(([n]) => n === v)[1]); custom.querySelector("input").value = get(s, "time.tz"); }
  });
  const custom = field("POSIX TZ", inp("text", get(s, "time.tz"), v => set(s, "time.tz", v)), "e.g. EST5EDT,M3.2.0,M11.1.0");
  if (TZ.some(([n]) => n === get(s, "time.tz_name"))) custom.style.display = "none";
  const browserTz = Intl.DateTimeFormat().resolvedOptions().timeZone;

  // Weather location search (runs in the browser against Open-Meteo's geocoder)
  const results = h("div", { class: "stack" });
  const q = inp("text", "", () => { }, { placeholder: "City or ZIP, e.g. Brooklyn" });
  const doSearch = async () => {
    results.replaceChildren(h("span", { class: "muted small" }, "Searching…"));
    try {
      const r = await (await fetch("https://geocoding-api.open-meteo.com/v1/search?count=6&language=en&format=json&name=" + encodeURIComponent(q.value))).json();
      results.replaceChildren(...(r.results || []).map(p => h("button", { class: "btn sm", style: { textAlign: "left" }, onclick: () => {
        set(s, "weather.lat", +p.latitude.toFixed(4)); set(s, "weather.lon", +p.longitude.toFixed(4)); set(s, "weather.place", p.name);
        $("#wxPlace").textContent = `${p.name}, ${p.admin1 || ""} ${p.country_code || ""} (${p.latitude.toFixed(2)}, ${p.longitude.toFixed(2)})`; results.replaceChildren();
      } }, `${p.name}, ${p.admin1 || ""} · ${p.country}`)));
      if (!r.results?.length) results.replaceChildren(h("span", { class: "muted small" }, "No matches."));
    } catch { results.replaceChildren(h("span", { class: "muted small" }, "Search needs internet in your browser. You can type coordinates below instead.")); }
  };
  q.onkeydown = e => { if (e.key === "Enter") doSearch(); };

  // Sources
  const sources = s.sources = s.sources || [];
  const srcList = h("div", { class: "stack" });
  const drawSources = () => srcList.replaceChildren(...sources.map((src, i) => h("div", { class: "card stack", style: { background: "var(--panel2)" } },
    h("div", { class: "two" }, field("Name / key prefix", inp("text", src.prefix || src.name, v => { src.name = v; src.prefix = v; })), field("Every (s)", inp("number", src.interval_s ?? 60, v => src.interval_s = v, { min: 10 }))),
    field("JSON URL", inp("url", src.url, v => src.url = v, { placeholder: "http://casaos.local:8080/stats.json" })),
    h("div", { class: "row" }, check("Enabled", src.enabled !== false, v => src.enabled = v),
      h("button", { class: "btn sm", onclick: async e => {
        const out = e.target.closest(".card").querySelector("pre") || e.target.closest(".card").appendChild(h("pre"));
        out.textContent = "Fetching from the display…";
        try { await api("/api/sources/test", { body: { url: src.url } });
          for (let k = 0; k < 30; k++) { await sleep(500); const r = await api("/api/sources/test"); if (!r._pending) { out.textContent = r.ok ? Object.entries(r.keys).map(([k2, v]) => `{${src.prefix || src.name}.${k2}} = ${v}`).join("\n") : "Error: " + r.error; break; } }
        } catch (er) { out.textContent = er.message; }
      } }, "Test"),
      h("button", { class: "btn sm danger", onclick: () => { sources.splice(i, 1); drawSources(); } }, "Remove")))));
  drawSources();

  const token = get(S.status, "api_token", "");
  const host = S.status.net?.ip || location.host;
  const pushUrl = `http://${host}/api/usage`;
  el.replaceChildren(h("div", { class: "grid" },
    sec("sec-device", "Device",
      field("Name", inp("text", get(s, "device.name"), v => set(s, "device.name", v))),
      field("Hostname", inp("text", get(s, "device.hostname"), v => set(s, "device.hostname", v.toLowerCase().replace(/[^a-z0-9-]/g, ""))), ".local address"),
      saveBtn("Save device", () => saveSettings({ device: s.device }))),
    sec("sec-wifi", "Wi-Fi",
      h("div", { class: "row" }, h("div", { style: { flex: 1 } }, field("Network", ssid)), scan), ssidList,
      field("Password", inp("password", get(s, "wifi.pass"), v => set(s, "wifi.pass", v), { autocomplete: "new-password" })),
      h("p", { class: "small muted" }, "If the display can't join within 20 s it opens its own setup network again."),
      saveBtn("Save & connect", () => saveSettings({ wifi: s.wifi }, "Saved. Reconnecting…"))),
    sec("sec-time", "Time",
      field("Time zone", tzSel), custom,
      browserTz && browserTz !== get(s, "time.tz_name") && TZ.some(([n]) => n === browserTz) ? h("button", { class: "btn sm", onclick: () => { tzSel.value = browserTz; tzSel.onchange(); } }, "Use this browser's zone (" + browserTz + ")") : null,
      check("24-hour clock", get(s, "time.h24"), v => set(s, "time.h24", v)),
      field("NTP server", inp("text", get(s, "time.ntp"), v => set(s, "time.ntp", v))),
      saveBtn("Save time", () => saveSettings({ time: s.time }))),
    sec("sec-weather", "Weather",
      check("Enabled", get(s, "weather.enabled", true), v => set(s, "weather.enabled", v)),
      h("div", { class: "muted small", id: "wxPlace" }, get(s, "weather.place") ? `${get(s, "weather.place")} (${get(s, "weather.lat")}, ${get(s, "weather.lon")})` : "No location set"),
      h("div", { class: "row" }, h("div", { style: { flex: 1 } }, q), h("button", { class: "btn", onclick: doSearch }, "Search")), results,
      h("div", { class: "two" }, field("Latitude", inp("number", get(s, "weather.lat"), v => set(s, "weather.lat", v), { step: "0.0001" })), field("Longitude", inp("number", get(s, "weather.lon"), v => set(s, "weather.lon", v), { step: "0.0001" }))),
      h("div", { class: "two" }, field("Units", sel([["imperial", "°F, mph"], ["metric", "°C, km/h"]], get(s, "weather.units"), v => set(s, "weather.units", v))),
        field("Refresh every (min)", inp("number", get(s, "weather.interval_min", 15), v => set(s, "weather.interval_min", v), { min: 5 }))),
      h("p", { class: "small muted" }, "Forecasts from Open-Meteo (free, no key)."),
      h("button", { class: "btn sm", onclick: () => act("refresh_weather").then(() => toast("Updating weather…")) }, "Update weather now"),
      saveBtn("Save weather", () => saveSettings({ weather: s.weather }))),
    sec("sec-display", "Display",
      field("Brightness", inp("range", get(s, "display.brightness", 180), v => { set(s, "display.brightness", v); clearTimeout(TABS.settings.bt); TABS.settings.bt = setTimeout(() => saveSettings({ display: { brightness: v } }, "Brightness set"), 350); }, { min: 5, max: 255 })),
      check("Pixel shift (burn-in protection)", get(s, "display.pixel_shift", true), v => set(s, "display.pixel_shift", v)),
      h("h3", {}, "Rotation"),
      field("Screen rotation", sel([["auto", "Auto: turn with the device"], ["0", "0°"], ["90", "90°"], ["180", "180°"], ["270", "270°"]], get(s, "display.rotation.mode", "auto"), v => set(s, "display.rotation.mode", v))),
      h("div", { class: "row" }, h("button", { class: "btn sm", onclick: async () => { await act("rotation_here"); setTimeout(async () => { S.settings = await api("/api/settings"); }, 800); } }, "This way is up"),
        h("span", { class: "small muted" }, "Hold the display the way you want it upright, then press.")),
      check("Reverse auto-rotate direction", get(s, "display.rotation.reverse", false), v => set(s, "display.rotation.reverse", v)),
      h("div", { class: "two" },
        field("Turn effect", sel([["dip", "Fade out & in"], ["none", "Instant"]], get(s, "display.rotation.effect", "dip"), v => set(s, "display.rotation.effect", v))),
        field("Effect time (ms)", inp("number", get(s, "display.rotation.effect_ms", 360), v => set(s, "display.rotation.effect_ms", v), { min: 100, max: 1500 }))),
      h("h3", {}, "When nobody's using it"),
      h("p", { class: "small muted", style: { margin: 0 } }, "Touch, picking it up or a button press counts as use and brings it straight back."),
      h("div", { class: "two" },
        field("1. Dim after", sel([[0, "Never"], [15, "15 seconds"], [30, "30 seconds"], [60, "1 minute"], [120, "2 minutes"], [300, "5 minutes"], [600, "10 minutes"], [1800, "30 minutes"]], String(get(s, "display.dim_after_s", 0)), v => set(s, "display.dim_after_s", +v))),
        field("Dim to", sel([[5, "5% of normal"], [10, "10%"], [15, "15%"], [25, "25%"], [40, "40%"]], String(get(s, "display.dim_percent", 15)), v => set(s, "display.dim_percent", +v)))),
      field("2. Turn screen off after", sel([[0, "Never"], [1, "1 minute"], [2, "2 minutes"], [5, "5 minutes"], [10, "10 minutes"], [15, "15 minutes"], [30, "30 minutes"], [60, "1 hour"], [120, "2 hours"]], String(get(s, "display.sleep_after_min", 0)), v => set(s, "display.sleep_after_min", +v)), " (longer than the dim time)"),
      check("Stay on while the printer is printing", get(s, "display.awake_while_printing", true), v => set(s, "display.awake_while_printing", v)),
      h("h3", {}, "Night mode"),
      check("Enabled", get(s, "display.night.enabled", true), v => set(s, "display.night.enabled", v)),
      h("div", { class: "two" }, field("Starts", inp("time", get(s, "display.night.start", "22:30"), v => set(s, "display.night.start", v))), field("Ends", inp("time", get(s, "display.night.end", "07:00"), v => set(s, "display.night.end", v)))),
      field("Night brightness", inp("range", get(s, "display.night.brightness", 25), v => set(s, "display.night.brightness", v), { min: 1, max: 255 })),
      check("Use night theme colors", get(s, "display.night.use_night_theme", true), v => set(s, "display.night.use_night_theme", v)),
      check("Turn the screen off at night (touch to wake)", get(s, "display.night.screen_off", false), v => set(s, "display.night.screen_off", v)),
      saveBtn("Save display", () => saveSettings({ display: s.display }))),
    sec("sec-bambu", "Bambu Lab printer",
      h("p", { class: "small muted" }, "Shows print progress from your printer over your home network (read-only, works with cloud mode too). On the printer's screen: the network settings show the IP address and access code; the device info shows the serial number."),
      check("Enabled", get(s, "bambu.enabled", false), v => set(s, "bambu.enabled", v)),
      field("Printer IP address", inp("text", get(s, "bambu.ip"), v => set(s, "bambu.ip", v.trim()), { placeholder: "192.168.1.50" })),
      field("Serial number", inp("text", get(s, "bambu.serial"), v => set(s, "bambu.serial", v.trim().toUpperCase()), { placeholder: "e.g. 03919C4xxxxxxxx" })),
      field("Access code", inp("password", get(s, "bambu.access_code"), v => set(s, "bambu.access_code", v.trim()), { autocomplete: "off", placeholder: "8 characters" })),
      h("div", { class: "small" }, "Status: ", S.data["bambu.connected"] === 1
        ? h("b", { style: { color: "var(--ok)" } }, `connected · ${S.data["bambu.state_text"] || "?"}${S.data["bambu.percent"] !== undefined ? " · " + S.data["bambu.percent"] + "%" : ""}`)
        : h("b", { style: { color: "var(--warn)" } }, S.data["bambu.error"] || "not connected")),
      h("div", { class: "row" }, h("button", { class: "btn primary", onclick: () => saveSettings({ bambu: s.bambu }) }, "Save printer"),
        h("button", { class: "btn", onclick: addPrinterPageToDevice }, "Add printer page to my layout"))),
    sec("sec-battery", "Battery",
      h("p", { class: "small muted" }, "Set this to the capacity printed on your battery. The display charges at half that rate (0.5C), which is gentle on small cells."),
      field("Battery capacity (mAh)", inp("number", get(s, "battery.capacity_mah", 1000), v => set(s, "battery.capacity_mah", v), { min: 100, max: 5000 }),
        "charges at ~" + Math.max(100, Math.min(1000, Math.floor((get(s, "battery.capacity_mah", 1000)) / 2))) + " mA"),
      h("div", { class: "small muted" }, S.status.battery?.present ? `Now: ${S.status.battery.percent}% · ${S.status.battery.mv} mV${S.status.battery.charging ? " · charging" : ""}` : "No battery detected"),
      h("div", { class: "row" }, h("button", { class: "btn primary", onclick: () => saveSettings({ battery: s.battery }) }, "Save battery"))),
    sec("sec-sound", "Sound",
      h("div", { class: "small" }, "Audio chip: ", h("b", { style: { color: S.status.sound === "ready" ? "var(--ok)" : "var(--warn)" } }, S.status.sound || "unknown (update firmware)")),
      check("Sounds on", get(s, "sound.enabled", true), v => set(s, "sound.enabled", v)),
      field("Volume", inp("range", get(s, "sound.volume", 60), v => set(s, "sound.volume", v), { min: 0, max: 100 })),
      check("Tick when the page changes", get(s, "sound.page_change", true), v => set(s, "sound.page_change", v)),
      check("Chirp when the screen rotates", get(s, "sound.rotation", true), v => set(s, "sound.rotation", v)),
      check("Quiet during night mode", get(s, "sound.quiet_at_night", true), v => set(s, "sound.quiet_at_night", v)),
      h("div", { class: "row" }, h("button", { class: "btn primary", onclick: () => saveSettings({ sound: s.sound }) }, "Save sound"),
        h("button", { class: "btn", onclick: async () => { await saveSettings({ sound: s.sound }, "Saved"); act("test_sound"); } }, "Save & test"))),
    sec("sec-claude", "Claude usage",
      h("div", { class: "card stack", style: { margin: "0 0 10px" } }, h("b", {}, "Always-on (recommended): run the poller on a home server"),
        h("p", { class: "small muted", style: { margin: 0 } }, "Updates every 3 minutes whether or not Claude Code is open. On a Linux server signed in to Claude Code (run ", h("code", {}, "claude"), " once), run:"),
        h("pre", { class: "mono small" }, `curl -fsSLO https://github.com/${get(S.settings, "update.repo", "") || "dethdonald/Desk.Display."}/releases/latest/download/install-usage-poller.sh\nbash install-usage-poller.sh ${get(S.status, "net.ip", "") || location.host} ${get(S.status, "api_token", "") || "YOUR_API_TOKEN"}`),
        h("p", { class: "small muted", style: { margin: 0 } }, "The status-line method below still works alongside it.")),
      h("p", { class: "muted small" }, "Claude Code's status line already receives your 5-hour and weekly plan limits. A tiny script forwards them here whenever Claude Code refreshes its status line, so the numbers update while you use Claude Code (usage from claude.ai shows up with your next Claude Code message)."),
      h("ol", { class: "small", style: { paddingLeft: "18px", margin: 0 } },
        h("li", {}, "Download ", h("a", { href: SCRIPT_URL(), target: "_blank" }, "deskdisplay-statusline.py"), " (right-click → Save link as) into your ", h("code", {}, ".claude"), " folder: ", h("code", {}, "~/.claude/"), " on Mac/Linux, ", h("code", {}, "C:\\Users\\YOU\\.claude\\"), " on Windows."),
        h("li", {}, "Add the matching block to ", h("code", {}, ".claude/settings.json"), " (merge it in if the file already has settings), then restart Claude Code.")),
      h("div", { class: "row" }, sel([["unix", "Mac / Linux"], ["win", "Windows"]], TABS.settings.os || (navigator.userAgent.includes("Windows") ? "win" : "unix"), v => { TABS.settings.os = v; TABS.settings(); setTimeout(() => $("#sec-claude")?.scrollIntoView(), 30); })),
      (TABS.settings.os || (navigator.userAgent.includes("Windows") ? "win" : "unix")) === "win"
        ? field("Your Windows user name", inp("text", localStorage.getItem("dd-win-user") || "", v => localStorage.setItem("dd-win-user", v.trim()), { placeholder: "the folder name in C:\\Users\\", onchange: () => { TABS.settings(); setTimeout(() => $("#sec-claude")?.scrollIntoView(), 30); } }), "fills the paths below")
        : null,
      h("pre", {}, JSON.stringify({ statusLine: { type: "command", command: statusCmd(pushUrl, token) } }, null, 2)),
      h("div", { class: "row" }, h("button", { class: "btn sm", onclick: () => navigator.clipboard?.writeText(JSON.stringify({ statusLine: { type: "command", command: statusCmd(pushUrl, token) } }, null, 2)).then(() => toast("Copied")) }, "Copy settings block"),
        h("button", { class: "btn sm", onclick: async () => { try { await api("/api/usage", { body: { rate_limits: { five_hour: { used_percentage: 42, resets_at: Math.floor(Date.now() / 1000) + 7200 }, seven_day: { used_percentage: 18, resets_at: Math.floor(Date.now() / 1000) + 4 * 86400 } }, source: "test" } }); toast("Sent sample data"); } catch (e) { toast(e.message, true); } } }, "Send test data")),
      h("div", { class: "small" }, S.data["claude.updated"] !== undefined
        ? h("span", {}, "Last push received: ", h("b", {}, renderTmpl("{claude.updated|ago}")))
        : h("span", { style: { color: "var(--warn)" } }, "No push received yet.")),
      h("h3", {}, "Not syncing? Test from that computer"),
      h("p", { class: "small muted" }, "Run this in a terminal (PowerShell on Windows). It sends sample numbers right away and prints exactly what went wrong if it fails."),
      h("pre", {}, statusCmd(pushUrl, token) + " --test"),
      h("p", { class: "small muted" }, "Background sends are logged to deskdisplay-statusline.log in your temp folder (type %TEMP% in Explorer's address bar on Windows)."),
      h("p", { class: "small muted" }, "Uses the display's IP address because some Windows setups don't resolve .local names. If your router gives the display a new IP, update the command (or reserve the IP in your router)."),
      field("Mark data stale after (min)", inp("number", get(s, "claude.stale_after_min", 60), v => set(s, "claude.stale_after_min", v), { min: 5 })),
      saveBtn("Save", () => saveSettings({ claude: s.claude }))),
    sec("sec-sources", "Extra data sources",
      h("p", { class: "muted small" }, "Any JSON URL becomes data you can show. Every value is flattened to a key like {server.cpu.temp}. Scripts can also push data with POST /api/data (X-Api-Token header)."),
      srcList,
      h("div", { class: "row" }, h("button", { class: "btn sm", onclick: () => { sources.push({ name: "server", prefix: "server", url: "", interval_s: 60, enabled: true }); drawSources(); } }, "+ Add source"),
        h("button", { class: "btn primary", onclick: () => saveSettings({ sources }) }, "Save sources"))),
    sec("sec-security", "Security",
      field("Admin user", inp("text", get(s, "admin.user", "admin"), v => set(s, "admin.user", v))),
      field("Admin password", inp("password", get(s, "admin.pass"), v => set(s, "admin.pass", v), { autocomplete: "new-password", placeholder: "Not set" }), "blank = no login"),
      h("p", { class: "small muted" }, "Your browser will ask for these from now on. Forgot it? Hold the BOOT button for 10 seconds while the display is running to clear the password."),
      saveBtn("Save security", () => saveSettings({ admin: s.admin }, "Saved. Log in with the new password.")),
      h("h3", {}, "API token"),
      h("div", { class: "row" }, h("code", {}, token || "(none)"),
        h("button", { class: "btn sm", onclick: () => { if (confirm("Create a new token? Scripts using the old one stop working.")) { const t = [...crypto.getRandomValues(new Uint8Array(8))].map(b => b.toString(16).padStart(2, "0")).join(""); saveSettings({ api: { token: t } }, "New token saved"); setTimeout(() => TABS.settings(), 900); } } }, "Regenerate")),
      h("p", { class: "small muted" }, "Used by the status line script and anything pushing to /api/usage or /api/data."))));
};

// ============================================================ MAINTENANCE
TABS.maint = async () => {
  const el = $("#tab-maint");
  const st = S.status, n = st.net || {}, b = st.battery || {};
  const rows = [["Firmware", `v${st.version} (${st.build})`], ["Running from", `${st.partition} · ${st.ota_state || ""}`], ["Uptime", fmtDur(st.uptime_s || 0)],
    ["Free memory", `${Math.round(st.heap / 1024)} KB (min ${Math.round(st.heap_min / 1024)} KB) · PSRAM ${Math.round(st.psram / 1024)} KB`],
    ["Internal RAM", st.ram_internal != null ? `${Math.round(st.ram_internal / 1024)} KB (min ${Math.round(st.ram_internal_min / 1024)} KB)` : "-"],
    ["Network", n.ap ? "Setup network" : `${n.ssid} · ${n.rssi} dBm`], ["Address", `${n.ip} · ${n.host}.local`], ["MAC", n.mac],
    ["Display", st.display ? `${(st.display.fps || 0).toFixed(1)} fps · ${(st.display.mbps || 0).toFixed(1)} MB/s · bus ${Math.round(st.display.bus_busy_pct || 0)}% busy${st.display.draw_buffers ? " · buffers " + st.display.draw_buffers : ""}` : "-"],
    ["Power", b.present ? `${b.percent}% · ${b.mv} mV${b.charging ? " · charging" : ""}` : "USB (no battery)"], ["Clock", st.time_valid ? new Date(st.time * 1000).toLocaleString() : "not set yet"]];
  const logPre = h("pre", { id: "logs" }, "Loading…");
  const dataBox = h("div", {});
  const drawData = () => {
    const filt = (dataFilter.value || "").toLowerCase();
    const keys = Object.keys(S.data).sort().filter(k => k.toLowerCase().includes(filt));
    dataBox.replaceChildren(h("table", { class: "kv" }, ...keys.slice(0, 400).map(k => h("tr", {}, h("td", { class: "mono" }, k), h("td", {}, h("span", { class: /^weather.*icon$/.test(k) ? "wx" : "" }, String(S.data[k])), S.dataTimes?.[k] ? h("span", { class: "small muted" }, "  " + renderTmpl(`{_t|ago}`, { _t: S.dataTimes[k] })) : null)))));
  };
  const dataFilter = inp("text", "", drawData, { placeholder: "Filter keys…" });
  el.replaceChildren(h("div", { class: "grid" },
    h("div", { class: "card" }, h("h2", {}, "Status"), h("table", { class: "kv" }, ...rows.map(([k, v]) => h("tr", {}, h("td", {}, k), h("td", {}, v ?? "-"))))),
    st.last_crash ? h("div", { class: "card stack" }, h("h2", {}, "Last crash"), h("pre", { class: "mono small" }, st.last_crash),
      h("p", { class: "small muted" }, "Copy this into the chat so the cause can be looked up."),
      h("div", { class: "row" }, h("button", { class: "btn sm", onclick: () => { navigator.clipboard?.writeText(st.last_crash); toast("Copied"); } }, "Copy"))) : null,
    h("div", { class: "card stack" }, h("h2", {}, "Actions"),
      h("div", { class: "row" }, h("button", { class: "btn", onclick: async () => { if (confirm("Restart the display?")) { await api("/api/reboot", { method: "POST" }); toast("Restarting…"); } } }, "Restart"),
        h("button", { class: "btn", onclick: () => act("refresh") }, "Refresh all data")),
      h("h3", {}, "Backup"),
      h("div", { class: "row" }, h("a", { class: "btn", href: "/api/backup", download: "deskdisplay-backup.json" }, "Download backup"),
        h("a", { class: "btn", href: "/api/backup?secrets=1", download: "deskdisplay-backup-secrets.json" }, "…with passwords"),
        h("button", { class: "btn", onclick: () => {
          const f = h("input", { type: "file", accept: ".json" });
          f.onchange = async () => { try { const j = JSON.parse(await f.files[0].text()); if (j.type !== "deskdisplay-backup") throw new Error("not a Desk Display backup"); await api("/api/restore", { body: j }); toast("Restored"); setTimeout(() => location.reload(), 1500); } catch (e) { toast("Restore failed: " + e.message, true); } };
          f.click();
        } }, "Restore…")),
      h("h3", {}, "Factory reset"),
      h("p", { class: "small muted" }, "Erases settings and your layout."),
      h("div", { class: "row" }, h("button", { class: "btn danger", onclick: async () => { if (confirm("Reset everything but keep Wi-Fi?")) { await api("/api/factory-reset", { body: { keep_wifi: true } }); toast("Resetting…"); } } }, "Reset (keep Wi-Fi)"),
        h("button", { class: "btn danger", onclick: async () => { if (confirm("Erase EVERYTHING including Wi-Fi? The display will open its setup network.")) { await api("/api/factory-reset", { body: { keep_wifi: false } }); toast("Resetting…"); } } }, "Erase all"))),
    h("div", { class: "card", style: { gridColumn: "1 / -1" } }, h("div", { class: "row between" }, h("h2", {}, "Log"), h("label", { class: "check" }, inp("checkbox", true, v => TABS.maint.follow = v), "Live")), logPre),
    h("div", { class: "card", style: { gridColumn: "1 / -1" } }, h("div", { class: "row between" }, h("h2", {}, "Live data"), h("div", { style: { width: "260px" } }, dataFilter)),
      h("p", { class: "small muted" }, "Everything widgets can show. Use these keys in templates like {weather.temp:0}."), dataBox)));
  TABS.maint.follow = true;
  drawData();
  TABS.maint.drawData = drawData;
  refreshLogs();
};
async function refreshLogs() {
  if (S.tab !== "maint" || !TABS.maint.follow) return;
  try { const t = await (await fetch("/api/logs")).text(); const p = $("#logs"); if (p) { const atBottom = p.scrollTop + p.clientHeight >= p.scrollHeight - 30; p.textContent = t; if (atBottom) p.scrollTop = p.scrollHeight; } } catch { }
}

// ============================================================ UPDATE
TABS.update = () => {
  const el = $("#tab-update");
  const st = S.status;
  const prog = h("div", { class: "meter", style: { display: "none" } }, h("i", { style: { width: "0%", background: "var(--accent)" } }));
  const msg = h("div", { class: "muted small" });
  const file = h("input", { type: "file", accept: ".bin" });
  const go = h("button", { class: "btn primary", onclick: () => {
    if (!file.files[0]) return toast("Choose a firmware .bin first", true);
    const f = file.files[0];
    if (f.size < 100000 || f.size > 4 * 1024 * 1024) return toast("That doesn't look like a firmware image", true);
    const fd = new FormData(); fd.append("firmware", f, f.name);
    const xhr = new XMLHttpRequest();
    prog.style.display = ""; go.disabled = true;
    xhr.upload.onprogress = e => { if (e.lengthComputable) { prog.firstChild.style.width = (e.loaded / e.total * 100) + "%"; msg.textContent = `Uploading ${Math.round(e.loaded / 1024)} / ${Math.round(e.total / 1024)} KB`; } };
    xhr.onload = () => { if (xhr.status === 200) { msg.textContent = "Installed. Restarting…"; waitForReboot(msg); } else { msg.textContent = "Failed: " + xhr.responseText; go.disabled = false; } };
    xhr.onerror = () => { msg.textContent = "Upload failed (connection lost)"; go.disabled = false; };
    xhr.open("POST", "/api/update"); xhr.send(fd);
  } }, "Upload & install");
  el.replaceChildren(h("div", { class: "grid" },
    h("div", { class: "card stack" }, h("h2", {}, "Firmware"),
      h("table", { class: "kv" }, h("tr", {}, h("td", {}, "Installed"), h("td", {}, `v${st.version} · built ${st.build}`)), h("tr", {}, h("td", {}, "Running from"), h("td", {}, `${st.partition} (${st.ota_state === "pending_verify" ? "verifying — rolls back if it crashes in the first minute" : "verified"})`))),
      h("p", { class: "small muted" }, "The web UI is built into the firmware, so one file updates everything. Settings and layout are kept. A new version must run for a minute without crashing, otherwise the display automatically goes back to the previous one.")),
    ghCard(),
    h("div", { class: "card stack" }, h("h2", {}, "Upload a file"), file, go, prog, msg,
      h("p", { class: "small muted" }, "Use ", h("code", {}, ".pio/build/amoled216/firmware.bin"), " from the project (not the merged/factory image)."))));
};
const normRepo = r => (r || "").trim().replace(/^https?:\/\//, "").replace(/^www\./, "").replace(/^github\.com\//, "").replace(/\.git$/, "").split("/").slice(0, 2).join("/");
function ghCard() {
  const u = clone(get(S.settings, "update", {}));
  const info = h("div", { class: "stack" }, h("span", { class: "muted small" }, "Loading…"));
  const out = h("div", { class: "muted small" });
  const draw = d => info.replaceChildren(
    h("table", { class: "kv" },
      h("tr", {}, h("td", {}, "Running"), h("td", {}, "v" + d.current)),
      h("tr", {}, h("td", {}, "Newest release"), h("td", {}, d.latest ? "v" + d.latest + (d.available ? "  (update available)" : "  (up to date)") : (d.check_error || "not checked yet"))),
      h("tr", {}, h("td", {}, "Last checked"), h("td", {}, d.checked_at ? renderTmpl("{_t|ago}", { _t: d.checked_at }) : "never"))),
    d.notes && d.available ? h("pre", {}, d.notes) : null);
  const refresh = async () => { try { draw(await api("/api/update/status")); } catch { } };
  refresh();
  const repoIn = inp("text", u.repo, v => u.repo = v, { placeholder: "owner/repo or https://github.com/owner/repo" });
  const installBtn = h("button", { class: "btn primary", onclick: async () => {
    const repo = normRepo(repoIn.value);
    if (!repo.includes("/")) return toast("Enter a repository like owner/repo", true);
    installBtn.disabled = true;
    try {
      await api("/api/update/install-latest", { body: { repo } });
      for (let i = 0; i < 240; i++) {
        await sleep(1000);
        const r = await api("/api/update/status").catch(() => null);
        if (!r) { out.textContent = "Installed. Restarting…"; waitForReboot(out); return; }
        out.textContent = r.status;
        if (/^(error|up to date|no update)/.test(r.status)) { draw(r); break; }
      }
    } catch (e) { toast(e.message, true); }
    installBtn.disabled = false;
  } }, "Install latest release");
  return h("div", { class: "card stack" }, h("h2", {}, "Updates from GitHub"),
    h("p", { class: "small muted" }, "Enter the repository and the display finds its newest release by itself. Releases are built automatically when you run the Release firmware workflow (or push a version tag)."),
    field("Repository", repoIn),
    h("div", { class: "row" }, installBtn,
      h("button", { class: "btn", onclick: async () => { u.repo = normRepo(repoIn.value); repoIn.value = u.repo; await api("/api/settings", { body: { update: u } }); await api("/api/update/check", { method: "POST" }); info.replaceChildren(h("span", { class: "muted small" }, "Checking…")); for (let i = 0; i < 20; i++) { await sleep(1000); const d = await api("/api/update/status"); if (!d.checking) { draw(d); return; } } } }, "Check only")),
    out, info,
    h("h3", {}, "Automatic updates"),
    h("div", { class: "two" }, field("Check every (hours)", inp("number", u.check_hours ?? 24, v => u.check_hours = v, { min: 1 })),
      h("div", { style: { display: "flex", alignItems: "flex-end" } }, check("Install automatically", u.auto, v => u.auto = v))),
    h("div", { class: "row" }, h("button", { class: "btn", onclick: () => { u.repo = normRepo(repoIn.value); repoIn.value = u.repo; saveSettings({ update: u }); } }, "Save")));
}
async function waitForReboot(msgEl) {
  await sleep(4000);
  for (let i = 0; i < 60; i++) {
    try { const s = await api("/api/status"); msgEl.textContent = `Back online: v${s.version}`; toast("Update complete"); S.status = s; return; } catch { }
    msgEl.textContent = "Waiting for the display to restart…"; await sleep(2000);
  }
  msgEl.textContent = "Couldn't reach the display. Check it's powered and on Wi-Fi.";
}

// ============================================================ boot
(async function init() {
  try { [S.settings, S.layout] = await Promise.all([api("/api/settings"), api("/api/layout")]); } catch (e) { toast("Can't reach the display: " + e.message, true); }
  await Promise.all([pollStatus(), loadData()]);
  const start = (location.hash || "#dash").slice(1);
  showTab(TABS[start] ? start : "dash");
  setInterval(pollStatus, 2000);
  setInterval(async () => {
    await loadData();
    if (S.tab === "dash") TABS.dash.draw?.();
    if (S.tab === "layout" && Editor.layout && !document.querySelector("#edWrap .screen:active")) redrawCanvasLight();
    if (S.tab === "maint") { TABS.maint.drawData?.(); refreshLogs(); }
  }, 5000);
  setInterval(() => { if (S.tab === "dash") TABS.dash.draw?.(); }, 1000);
})();
function redrawCanvasLight() {
  // refresh live values without disturbing an in-progress drag
  if (!$("#edWrap .screen")) return;
  redrawCanvasKeepCapture();
}
window.addEventListener("hashchange", () => { const t = location.hash.slice(1); if (TABS[t] && t !== S.tab) showTab(t); });

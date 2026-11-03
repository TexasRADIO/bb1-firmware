// SPDX-License-Identifier: GPL-3.0-only
// BB1 sound library over the FM-1's USB serial console (Web Serial).
// The firmware writes the whole 240 KB region itself and checks every 32-byte chunk
// (bb_target_asset_console.c: bainit / baw / bacommit / barestart). This page then reads
// the region back and compares its SHA-256 before it asks the FM-1 to restart.
// Errors carry a code the page translates, as fm1ota.js does.

export const SOUND_BASE = 0xA0000, SOUND_BYTES = 245760;
export const SERIAL_FILTER = { usbVendorId: 0x1209, usbProductId: 0x0001 };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
function fail(code, msg) { const e = new Error(msg); e.code = code; return e; }

const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; }
  return t;
})();
export function crc32(bytes) {
  let c = 0xFFFFFFFF;
  for (const b of bytes) c = CRC_TABLE[(c ^ b) & 0xFF] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
}
const hex8 = (n) => n.toString(16).toUpperCase().padStart(8, "0");
export async function sha256hex(bytes) {
  const d = new Uint8Array(await crypto.subtle.digest("SHA-256", bytes));
  return Array.from(d, (b) => b.toString(16).padStart(2, "0")).join("");
}

// One console: send a line, collect the reply up to the "> " prompt.
export class SoundConsole {
  constructor(port) { this.port = port; this.buf = ""; this.waiter = null; this.dead = null; }
  async open() {
    await this.port.open({ baudRate: 115200 });
    this.reader = this.port.readable.getReader();
    this.writer = this.port.writable.getWriter();
    this.pump();
    await this.port.setSignals({ dataTerminalReady: true });
    // The console prints a prompt when the port opens; ask again if it was missed.
    try { await this.reply(1500); } catch (_) { await this.send(""); }
  }
  async pump() {
    const dec = new TextDecoder();
    try {
      for (;;) {
        const { value, done } = await this.reader.read();
        if (done) throw new Error("closed");
        this.buf += dec.decode(value, { stream: true });
        if (this.buf.length > 65536) throw new Error("reply too long");
        if (this.waiter && this.buf.endsWith("> ")) { const w = this.waiter; this.waiter = null; w(); }
      }
    } catch (e) {
      this.dead = e;
      if (this.waiter) { const w = this.waiter; this.waiter = null; w(); }
    }
  }
  async reply(ms) {
    if (!this.buf.endsWith("> ") && !this.dead) {
      let tm;
      await Promise.race([new Promise((r) => { this.waiter = r; }), new Promise((r) => { tm = setTimeout(r, ms); })]);
      clearTimeout(tm); this.waiter = null;
    }
    if (this.dead) throw fail("lost", "The USB connection was lost.");
    if (!this.buf.endsWith("> ")) throw fail("timeout", "The FM-1 did not answer.");
    const text = this.buf; this.buf = ""; return text;
  }
  async send(line, ms = 3000) {
    if (this.dead) throw fail("lost", "The USB connection was lost.");
    this.buf = "";
    await this.writer.write(new TextEncoder().encode(line + "\r\n"));
    return this.reply(ms);
  }
  async close() {
    try { await this.reader?.cancel(); } catch (_) {}
    try { this.reader?.releaseLock(); } catch (_) {}
    try { this.writer?.releaseLock(); } catch (_) {}
    try { await this.port.close(); } catch (_) {}
  }
}

// The same console lines over USB-MIDI: Felucca's editor SysEx framing (F0 7D 46 4C cmd ... F7), command 80
// in BB1 r9.13+ (editor.c ED_ASSET -> console.c con_tunnel). Replies carry the console's text.
export const ED_ASSET = 80;
export class MidiConsole {
  constructor(input, output) {
    this.input = input; this.output = output; this.pending = null; this.chunk = 128; this.closed = false;
    input.onmidimessage = (e) => {
      const d = e.data;
      if (d.length < 6 || d[0] !== 0xF0 || d[1] !== 0x7D || d[2] !== 0x46 || d[3] !== 0x4C || d[4] !== ED_ASSET || d.at(-1) !== 0xF7) return;
      if (this.pending) { const p = this.pending; this.pending = null; clearTimeout(p.timer); p.resolve(String.fromCharCode(...d.slice(5, -1))); }
    };
  }
  async send(line, ms = 3000) {
    if (this.closed || this.input.state === "disconnected" || this.output.state === "disconnected") throw fail("lost", "The USB connection was lost.");
    if (this.pending) throw fail("busy", "Another request is still open.");
    const bytes = Array.from(line, (c) => c.charCodeAt(0));
    if (!bytes.length || bytes.length > 127 || bytes.some((b) => b < 32 || b > 126)) throw fail("badreq", "Bad console line.");
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => { this.pending = null; reject(fail("timeout", "The FM-1 did not answer.")); }, ms);
      this.pending = { resolve, reject, timer };
      try { this.output.send([0xF0, 0x7D, 0x46, 0x4C, ED_ASSET, ...bytes, 0xF7]); }
      catch (e) { clearTimeout(timer); this.pending = null; reject(fail("lost", "The USB connection was lost.")); }
    });
  }
  async close() {
    this.closed = true; this.input.onmidimessage = null;
    if (this.pending) { const p = this.pending; this.pending = null; clearTimeout(p.timer); p.reject(fail("lost", "Closed.")); }
  }
}

export function parseFields(text) {
  const f = {};
  for (const line of text.replaceAll("\r", "").split("\n")) {
    const m = /^\s*([a-z][a-z0-9_]*)\s+(-?\d+)\s*$/.exec(line);
    if (m) f[m[1]] = Number(m[2]);
  }
  return f;
}

// flr OFFSET COUNT: rows "0A0000: 42 42 53 4B ..." (6 hex digits, up to 16 bytes each)
export function parseFlr(text, offset, count) {
  const out = [];
  for (const raw of text.replaceAll("\r", "").split("\n")) {
    const m = /^\s*([0-9A-Fa-f]{6}):((?: [0-9A-Fa-f]{2}){1,16})\s*$/.exec(raw);
    if (!m) continue;
    if (parseInt(m[1], 16) !== offset + out.length) throw fail("badread", "Flash read out of order.");
    out.push(...m[2].trim().split(" ").map((x) => parseInt(x, 16)));
  }
  if (out.length !== count) throw fail("badread", "Short flash read.");
  return Uint8Array.from(out);
}

export async function readRegion(con, base, bytes, onProgress = () => {}, chunk = con.chunk ?? 256) {
  const out = new Uint8Array(bytes);
  for (let off = 0; off < bytes; off += chunk) {
    const n = Math.min(chunk, bytes - off), at = base + off;
    out.set(parseFlr(await con.send(`flr 0x${at.toString(16)} ${n}`), at, n), off);
    onProgress(off + n, bytes);
  }
  return out;
}

function assetReply(text, phase) {
  const f = parseFields(text);
  if (!Number.isInteger(f.asset_rc)) throw fail("refused", `The FM-1 did not accept the ${phase} step.`);
  return f;
}

// Status the page shows before writing: playing must be stopped.
export async function deviceState(con) {
  const f = parseFields(await con.send("status") + await con.send("bbdiag"));
  return { playing: f.bb_playing ?? f.playing, master: f.bb_master_q12, faults: f.bb_faults ?? 0, boots: f.boots, uptime: f.uptime_ms };
}

// Is this exact library already installed? The header carries its length and both CRCs,
// and the firmware checks the whole-pack CRC at every start.
export async function soundsInstalled(con, sounds) {
  const head = parseFlr(await con.send(`flr 0x${SOUND_BASE.toString(16)} 32`), SOUND_BASE, 32);
  return head.every((b, i) => b === sounds[i]);
}

// port: a Web Serial port (the console). con: an already-open console, e.g. MidiConsole for the one-click
// install. Over MIDI the firmware's own gate (bainit refuses unless stopped, MASTER down, USB audio idle)
// stands in for the status read, which the MIDI tunnel does not carry.
export async function installSounds({ port, con: given, sounds, sha256, onStep = () => {}, force = false }) {
  if (!(sounds instanceof Uint8Array) || sounds.length !== SOUND_BYTES) throw fail("badpkg", "Sound library has the wrong size.");
  if (await sha256hex(sounds) !== sha256) throw fail("badpkg", "Sound library checksum mismatch.");
  const con = given ?? new SoundConsole(port);
  try {
    onStep("connect");
    if (!given) {
      await con.open();
      const st = await deviceState(con);
      if (st.playing !== 0) throw fail("playing", "Stop playback first.");
      if (st.master !== 0) throw fail("master", "Turn MASTER fully down first.");
    }
    if (!force && await soundsInstalled(con, sounds)) {
      onStep("check", 0);
      const back = await readRegion(con, SOUND_BASE, SOUND_BYTES, (d, t) => onStep("check", d / t));
      if (await sha256hex(back) === sha256) { onStep("already"); return { written: false }; }
    }
    // 1. begin: the firmware pauses audio first; rc 1 means "still pausing, ask again"
    let r = assetReply(await con.send("bastat"), "status");
    const begin = `bainit 1 ${hex8(crc32(sounds))} ${hex8(crc32(sounds.subarray(0, 256)))}`;
    for (let i = 0; ; i++) {
      r = assetReply(await con.send(begin, 10000), "start");
      if (r.asset_rc === 0) break;
      if (r.asset_rc !== 1 || i >= 20) throw fail("refused", "The FM-1 could not pause its audio. Stop playback, turn MASTER down, and close any app using the FM-1 as an audio input.");
      await sleep(50);
    }
    // 2. every 32-byte chunk with its CRC; the firmware checks each one as it writes
    for (let off = 0; off < SOUND_BYTES; off += 32) {
      const p = sounds.subarray(off, off + 32);
      const line = `baw ${off.toString(16).toUpperCase()} ${hex8(crc32(p))} ${Array.from(p, (b) => b.toString(16).padStart(2, "0")).join("")}`;
      r = assetReply(await con.send(line, 5000), "write");
      if (r.asset_rc !== 0 || r.asset_next !== off + 32) throw fail("writefail", `Writing stopped at ${off}.`);
      if ((off & 0x7FF) === 0) onStep("write", (off + 32) / SOUND_BYTES);
    }
    r = assetReply(await con.send("bacommit", 20000), "finish");
    if (r.asset_rc !== 0) throw fail("writefail", "The FM-1 did not confirm the new sounds.");
    // 3. independent read-back of the whole region
    const back = await readRegion(con, SOUND_BASE, SOUND_BYTES, (d, t) => onStep("check", d / t));
    if (await sha256hex(back) !== sha256) throw fail("mismatch", "The sounds read back differently.");
    // 4. restart so the new library loads. The firmware answers asset_rc 0 and then restarts;
    // a refusal is reported as one, and no answer at all is "not confirmed", never "done".
    onStep("restart");
    let restart = "unconfirmed";
    try {
      const f = parseFields(await con.send("barestart", 2000));
      if (Number.isInteger(f.asset_rc)) restart = f.asset_rc === 0 ? "confirmed" : "refused";
    } catch (_) { /* the port dropped: the FM-1 may have restarted, but that is not proof */ }
    if (restart === "refused") {
      const e = fail("restartrefused", "The sounds are written and checked, but the FM-1 refused to restart.");
      e.verified = true; throw e;
    }
    onStep(restart === "confirmed" ? "done" : "unconfirmed");
    return { written: true, verified: true, restart };
  } finally { await con.close(); }
}

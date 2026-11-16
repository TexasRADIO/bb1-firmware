#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Install a Felucca package (.fwsc) on an FM-1 over USB-MIDI.

The same update as the web installer (web/fm1ota.js): step 1, the
running firmware reads parts of the package and starts the update loader;
step 2, the loader reads the whole package and writes it. In both steps the
device asks (SysEx read requests on the logical image) and we answer. Then
the FM-1 restarts and the installed identity is checked.

  fm1_install.py PACKAGE.fwsc --rom-preflight-confirmed [--port NAME] [--yes] [--force]
  fm1_install.py FM-1.fwsc --rom-preflight-confirmed  (exact official V16/V15)
  fm1_install.py --info [--port NAME]

Before any write, prove that the recovery tool can communicate with this
unit's chip ROM at USB 4C4A:8057 through a route that survives the proposed
write and works without the running firmware. Keep two independent matching
full-flash reads and bind a verified return image and procedure to the unit
and target package. The flag records operator confirmation of that evidence.
After writing, repeat ROM entry and USB recognition, then verify normal boot.
The CLI only verifies MIDI transfer and identity. Needs mido with python-rtmidi.

Exit codes: 0 done, 1 cancelled or other error, 2 bad arguments or package,
3 FM-1 not found, 4 connection lost or the device stopped, 5 timeout (no
loader / no restart), 6 wrong model, or another identity after the install.
"""
import argparse
import hashlib
import queue
import re
import sys
import time

HS_QUERY = bytes([0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7])
# 00 59 11, length 1, body 01, checksum FE.  New BB1 builds answer the
# BB1 answers the plain M-UPGRADE query with FM-1_922 and this query with the
# full package identity, so exact post-install verification remains possible.
HS_FULL = bytes([0xF0, 0x00, 0x32, 0x45, 0x08, 0x00, 0x00, 0x40, 0x00, 0x7E, 0x01, 0xF7])
BB1_PLAIN_ID = "FM-1_922"
UPGRADE = bytes([0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7])
FINISH_CHECK, FINISH_WRITE = 0xE0000000, 0xF0000000
MAXDATA = 512
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F
LOADER_MARK = b"FELUCCA-LOADER-1"
# Exact official FM-1 packages allowed as return-to-stock targets without
# --force. V16 is current; V15 remains an independently verified legacy route.
STOCK_V15_SHA256 = "db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a"
STOCK_V16_SHA256 = "d017fb65e70aca97ee29d944470c3515a74581df5c0576bf70de917c7b2b61d5"
STOCK_RELEASES = {
    STOCK_V16_SHA256: ("V16", "FM-1_016"),
    STOCK_V15_SHA256: ("V15", "FM-1_015"),
}
PORT_RE = re.compile(r"fm-1|big beat|felucca|ota|composite|sinco|usb-midi", re.I)

# seconds; the tests shorten them
DELAY = {"open": 0.3, "start": 2.0, "reply": 0.01, "loader": 3.0, "reboot": 3.0, "retry": 1.0,
         "poll": 1.0, "hs": 1.0, "hs_full": 0.6, "idle_check": 8.0, "idle_write": 180.0,
         "wait_loader": 30.0, "wait_reboot": 40.0}

EXIT = {"usage": 2, "badpkg": 2, "notfound": 3, "model": 6, "lost": 4, "stopped": 4, "badreq": 4,
        "noloader": 5, "noreturn": 5, "mismatch": 6, "ambiguous": 3, "multiplefm1": 3}


class InstallError(Exception):
    def __init__(self, code, msg):
        super().__init__(msg)
        self.code = code


# ------------------------------------------------------------ wire format ---

def pack7(data):
    out, acc, nb = [], 0, 0
    for b in data:
        acc |= b << nb
        nb += 8
        while nb >= 7:
            out.append(acc & 0x7F)
            acc >>= 7
            nb -= 7
    if nb:
        out.append(acc & 0x7F)
    return bytes(out)


def unpack7(s):
    out, acc, nb = [], 0, 0
    for b in s:
        acc |= (b & 0x7F) << nb
        nb += 7
        while nb >= 8:
            out.append(acc & 0xFF)
            acc >>= 8
            nb -= 8
    return bytes(out)


def response(addr, data, fl=0):
    n = len(data)
    body = bytearray([0x00, 0x59, 0x30, (n + 8) & 0xFF, ((n + 8) >> 8) & 0xFF, 0, fl])
    body += addr.to_bytes(4, "little") + bytes([n & 0xFF, (n >> 8) & 0xFF, 0]) + bytes(data)
    body.append(~sum(body[6:]) & 0xFF)
    return b"\xF0" + pack7(body) + b"\xF7"


def parse_request(pkt):
    """-> (flags, addr, len) or None"""
    if len(pkt) < 2 or pkt[0] != 0xF0 or pkt[-1] != 0xF7:
        return None
    u = unpack7(pkt[1:-1])
    if len(u) != 15 or u[:3] != b"\x00\x59\x30" or ~sum(u[6:14]) & 0xFF != u[14]:
        return None
    return u[6], int.from_bytes(u[7:11], "little"), int.from_bytes(u[11:14], "little")


class Identity:
    def __init__(self, text, model, version):
        self.text, self.model, self.version = text, model, version

    @property
    def loader(self):
        return self.model.lower().startswith("ota-")


def printable(b):
    """bytes from the device as text for the terminal: printable ASCII only (no escape sequences)"""
    return "".join(chr(c) if 32 <= c < 127 else "?" for c in b)


def parse_identity(pkt):
    if len(pkt) < 2 or pkt[0] != 0xF0 or pkt[-1] != 0xF7:
        return None
    d = unpack7(pkt[1:-1])
    if len(d) != 34 or d[:3] != b"\x00\x59\x11":
        return None
    txt = printable(d[6:33].split(b"\0", 1)[0])
    if txt == BB1_PLAIN_ID:
        return Identity(txt, "FM-1", None)
    m = re.fullmatch(r"([^_]+)_(\d+)", txt)
    return Identity(txt, m[1], int(m[2])) if m else None


# --------------------------------------------------------------- package ---

def product_of(raw):
    """the package identity ("FM-1_906"): one marker byte after each of the first 20 blocks"""
    if len(raw) < BLOCKS * BLK:
        raise InstallError("badpkg", "not an FM-1 package (too short)")
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def logical_image(raw):
    """the image the device reads during an update: the package without the 20 marker bytes"""
    out = bytearray()
    for i in range(BLOCKS):
        out += raw[i * BLK:i * BLK + KEEP]
    return bytes(out + raw[BLOCKS * BLK:])


def model_of(text):
    return text.split("_")[0].removeprefix("ota-")


# ------------------------------------------------------------------ MIDI ---

class MidoLink:
    """one MIDI in/out pair with a SysEx queue"""

    def __init__(self, backend, in_name, out_name):
        import mido
        self.backend, self.name, self.mido = backend, in_name, mido
        self.q = queue.Queue()
        self.inp = mido.open_input(in_name, callback=self._rx)
        try:
            self.out = mido.open_output(out_name)
        except Exception:
            self.inp.close()
            raise
        time.sleep(DELAY["open"])   # CoreMIDI may drop a message sent just after opening

    def _rx(self, msg):
        if msg.type == "sysex":
            self.q.put(bytes([0xF0, *msg.data, 0xF7]))

    @property
    def lost(self):
        return self.name not in self.backend.input_names()

    def send(self, pkt):
        try:
            self.out.send(self.mido.Message("sysex", data=pkt[1:-1]))
            return True
        except Exception:
            return False

    def read(self, timeout):
        try:
            return self.q.get(timeout=max(timeout, 0))
        except queue.Empty:
            return None

    def drain(self):
        while not self.q.empty():
            self.q.get_nowait()

    def close(self):
        for p in (self.inp, self.out):
            try:
                p.close()
            except Exception:
                pass


class MidoBackend:
    def __init__(self):
        import mido
        self.mido = mido

    def input_names(self):
        return self.mido.get_input_names()

    def output_names(self):
        return self.mido.get_output_names()

    def open(self, in_name, out_name):
        return MidoLink(self, in_name, out_name)


def ambiguous_port(name):
    return InstallError("ambiguous", f"MIDI port {name!r} is ambiguous. Connect one FM-1 and retry.")


def pair_output(name, outs):
    exact = outs.count(name)
    if exact > 1:
        raise ambiguous_port(name)
    if exact:
        return name
    base = re.sub(r"\s+\d+$", "", name)     # Windows numbers each port
    same = [o for o in outs if re.sub(r"\s+\d+$", "", o) == base]
    if len(same) > 1:
        raise ambiguous_port(name)
    if len(same) == 1:
        return same[0]
    return outs[0] if len(outs) == 1 else None


# --------------------------------------------------------------- updater ---

class Device:
    def __init__(self, link, ident, name):
        self.link, self.id, self.name = link, ident, name


def ask(link, query, secs):
    """Send one identity query: Identity, None on timeout, False on send failure."""
    if not link.send(query):
        return False
    end = time.monotonic() + secs
    while (left := end - time.monotonic()) > 0:
        p = link.read(left)
        if p is None:
            break
        ident = parse_identity(p)
        if ident:
            return ident
    return None


def handshake(link, tries=3):
    """Read the full identity when a compatible firmware abbreviates its
    abbreviated M-UPGRADE reply; retain the plain reply for stock/legacy firmware."""
    link.drain()
    for _ in range(tries):
        ident = ask(link, HS_QUERY, DELAY["hs"])
        if ident is False:
            return None
        if ident is None:
            continue
        abbreviated = ident.text == BB1_PLAIN_ID or re.fullmatch(r"[^_]+_\d{3}", ident.text)
        if ident.loader or not abbreviated:
            return ident
        full = ask(link, HS_FULL, DELAY["hs_full"])
        agrees = full and (re.fullmatch(r"FM-1_922\d+", full.text) if ident.text == BB1_PLAIN_ID
                           else full.text.startswith(ident.text))
        return full if full and full.model == ident.model and agrees else ident
    return None


class Updater:
    def __init__(self, backend, port=None):
        self.backend = backend
        self.port = port

    def find(self, want=None, only_port=True):
        """Find a matching responder after checking all plausible FM-1 ports."""
        names = self.backend.input_names()
        preferred = [n for n in names if self.port and self.port.lower() in n.lower()]
        if only_port and self.port and not preferred:
            return None
        candidates = preferred + [n for n in names if n not in preferred and PORT_RE.search(n)]
        outs = self.backend.output_names()
        pairs = {}
        for name in candidates:
            out = pair_output(name, outs)
            if out:
                if name in pairs or out in pairs.values():
                    raise ambiguous_port(name)
                pairs[name] = out

        selected = None
        fm1_port = None
        try:
            for name, out in pairs.items():
                try:
                    link = self.backend.open(name, out)
                except Exception:
                    continue
                try:
                    ident = handshake(link, 2)
                    if ident and re.fullmatch(r"(?:ota-)?FM-1", ident.model, re.I):
                        if fm1_port is not None:
                            raise InstallError("multiplefm1", f"More than one FM-1 answered ({fm1_port}, {name}). "
                                               "Leave only the intended FM-1 connected.")
                        fm1_port = name
                    allowed = not only_port or not self.port or name in preferred
                    if selected is None and ident and allowed and (want is None or want(ident)):
                        selected = Device(link, ident, name)
                finally:
                    if selected is None or selected.link is not link:
                        link.close()
        except BaseException:
            if selected:
                selected.link.close()
            raise
        return selected

    def wait_for(self, want, secs):
        end = time.monotonic() + secs
        while time.monotonic() < end:
            dev = self.find(want, only_port=False)
            if dev:
                return dev
            time.sleep(DELAY["retry"])
        return None

    def serve(self, link, image, finish, idle, progress):
        """answer read requests until the device asks for `finish`; -> (served, finished, lost)"""
        served, last = 0, time.monotonic()
        while True:
            pkt = link.read(DELAY["poll"])
            if pkt is None:
                if link.lost:
                    return served, False, True
                if time.monotonic() - last > idle:
                    return served, False, False
                continue
            r = parse_request(pkt)
            if not r:
                continue
            fl, addr, n = r
            last = time.monotonic()
            if addr in (FINISH_CHECK, FINISH_WRITE):
                if addr != finish:
                    raise InstallError("badreq", "the FM-1 reported completion for the wrong update stage")
                if not link.send(response(addr, b"success\0")):
                    return served, False, True
                return served, True, False
            if n > MAXDATA or addr + n > len(image):
                raise InstallError("badreq", f"the device asked for {addr:#x}+{n}, outside the package")
            time.sleep(DELAY["reply"])
            if not link.send(response(addr, image[addr:addr + n], fl)):
                return served, False, True
            served += 1
            progress(served, addr + n)

    def check(self, dev, image, step):
        """step 1: the running firmware checks the package and starts the loader"""
        try:
            step("start", dev.id.text)
            if not dev.link.send(UPGRADE):
                raise InstallError("lost", "could not start the firmware check")
            time.sleep(DELAY["start"])
            served, finished, lost = self.serve(dev.link, image, FINISH_CHECK, DELAY["idle_check"],
                                                lambda k, end: step("check", k))
        finally:
            dev.link.close()
        if not finished:
            raise InstallError("lost" if lost else "stopped",
                               f"the FM-1 {'was disconnected' if lost else 'stopped answering'} after "
                               f"{served} requests; nothing was written")
        step("loader")
        time.sleep(DELAY["loader"])
        model = model_of(dev.id.text)
        ota = self.wait_for(lambda i: i.loader and model_of(i.text) == model, DELAY["wait_loader"])
        if not ota:
            raise InstallError("noloader", "the update loader did not appear. Replug the USB cable and "
                                           "run the install again: the FM-1 stays in update mode until it is done")
        return ota

    def write(self, ota, image, step):
        """step 2: the loader reads and writes the whole package, then restarts"""
        try:
            step("write", 0)
            if not ota.link.send(UPGRADE):
                raise InstallError("lost", "could not start the firmware transfer")
            time.sleep(DELAY["start"])
            served, finished, lost = self.serve(ota.link, image, FINISH_WRITE, DELAY["idle_write"],
                                                lambda k, end: step("write", min(99, end * 100 // len(image))))
        finally:
            ota.link.close()
        if not finished:
            raise InstallError("lost" if lost else "stopped",
                               f"the loader {'was disconnected' if lost else 'stopped answering'} after "
                               f"{served} requests. Replug the USB cable and run the install again to finish")
        step("write", 100)

    def verify(self, product, step):
        step("reboot")
        time.sleep(DELAY["reboot"])
        model = model_of(product)
        back = self.wait_for(lambda i: not i.loader and model_of(i.text) == model, DELAY["wait_reboot"])
        if not back:
            raise InstallError("noreturn", "the FM-1 did not come back: power-cycle it")
        back.link.close()
        if back.id.text != product:
            raise InstallError("mismatch", f"written, but the FM-1 reports {back.id.text} (expected {product})")
        step("done", back.id.text)
        return back.id.text

    def install(self, dev, image, product, step=lambda *a: None):
        """running firmware -> loader -> new firmware; dev from find(). A device already in
        update mode only needs the write."""
        if model_of(dev.id.text) != model_of(product):
            dev.link.close()
            raise InstallError("model", f"the device is {dev.id.text}, the package is for {product}")
        ota = dev if dev.id.loader else self.check(dev, image, step)
        self.write(ota, image, step)
        return self.verify(product, step)


# ------------------------------------------------------------------- CLI ---

def load_package(path, force):
    try:
        raw = open(path, "rb").read()
    except OSError as e:
        raise InstallError("badpkg", f"cannot read {path}: {e.strerror}")
    product = product_of(raw)
    if not re.fullmatch(r"[^_]+_\d+", product):
        raise InstallError("badpkg", f"{path}: not an FM-1 package (identity {product!r})")
    official = STOCK_RELEASES.get(hashlib.sha256(raw).hexdigest())
    if official and product != official[1]:
        raise InstallError("badpkg", f"{path}: official {official[0]} identity mismatch ({product!r})")
    if LOADER_MARK not in raw and not official and not force:
        raise InstallError("badpkg", f"{path}: no Felucca update loader in this package; only Felucca's own "
                                     "packages and exact official V16/V15 files (FM-1.fwsc) are installed "
                                     "(--force overrides)")
    return product, logical_image(raw)


def not_found(up):
    names = up.backend.input_names()
    where = f"no MIDI port matching {up.port!r} answered" if up.port else "FM-1 not found"
    return InstallError("notfound", f"{where} (USB data cable? another app using it?)"
                        f"\n  MIDI inputs: {', '.join(names) if names else '(none)'}")


class Progress:
    def __init__(self, out):
        self.out, self.line = out, False

    def __call__(self, k, a=None):
        if k == "check":
            self.put(f"checking the package ({a} reads)")
        elif k == "write":
            self.put(f"writing {a:3d}%")
        else:
            self.end()
            msg = {"start": f"starting the update on {a}", "loader": "switching to update mode",
                   "reboot": "restarting", "done": f"MIDI identity confirmed: {a}"}[k]
            print(msg, file=self.out, flush=True)

    def put(self, s):
        print(f"\r{s}\033[K", end="", file=self.out, flush=True)
        self.line = True

    def end(self):
        if self.line:
            print(file=self.out, flush=True)
            self.line = False


def run(a, backend, out, ask):
    up = Updater(backend, a.port)
    if a.info:
        dev = up.find()
        if not dev:
            raise not_found(up)
        dev.link.close()
        mode = "update loader (update not finished)" if dev.id.loader else "running"
        print(f"{dev.id.text}  [{mode}]  port: {dev.name}", file=out)
        return 0
    product, image = load_package(a.package, a.force)
    print(f"package: {product}  ({a.package})", file=out)
    dev = up.find()
    if not dev:
        raise not_found(up)
    print(f"device:  {dev.id.text}  ({dev.name})" + ("  in update mode: the write will be finished" if dev.id.loader else ""),
          file=out)
    if not a.yes and not ask("Install? Do not unplug the FM-1 while writing. [y/N] "):
        dev.link.close()
        print("cancelled", file=out)
        return 1
    prog = Progress(out)
    try:
        up.install(dev, image, product, prog)
    finally:
        prog.end()
    print("Transfer complete. Repeat ROM entry and USB recognition, then verify normal boot.", file=out)
    return 0


def ask_tty(prompt):
    try:
        return input(prompt).strip().lower() in ("y", "yes")
    except EOFError:
        return False


def main(argv=None, backend=None, out=sys.stdout, ask=ask_tty):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("package", nargs="?", help="Felucca package (.fwsc)")
    ap.add_argument("--info", action="store_true", help="print the identity of the connected FM-1")
    ap.add_argument("--port", metavar="NAME", help="MIDI port to use (part of its name)")
    ap.add_argument("--rom-preflight-confirmed", action="store_true",
                    help="confirm unit-specific ROM access, matching backups and verified return path")
    ap.add_argument("--yes", action="store_true", help="skip the install prompt after ROM preflight")
    ap.add_argument("--force", action="store_true", help="install a package without the Felucca loader marker")
    a = ap.parse_args(argv)
    if bool(a.info) == bool(a.package):
        ap.error("give a PACKAGE.fwsc or --info")
    if not a.info and not a.rom_preflight_confirmed:
        ap.error("write requires --rom-preflight-confirmed after proving ROM USB 4C4A:8057 access, "
                 "two independent matching full-flash reads, and a verified return path")
    try:
        if backend is None:
            try:
                backend = MidoBackend()
            except ImportError:
                raise InstallError("usage", "needs mido and python-rtmidi (pip install mido python-rtmidi)")
        return run(a, backend, out, ask)
    except InstallError as e:
        print(f"error: {e}", file=sys.stderr)
        return EXIT.get(e.code, 1)
    except KeyboardInterrupt:
        print("\ninterrupted: identify the FM-1 state and revalidate ROM recovery before resuming", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())

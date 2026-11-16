#!/usr/bin/env python3
"""Capture diagnostic data from a running FM-1 over USB-MIDI.
Listens for all MIDI messages and prints them with timestamps.
Also queries the FM-1's identity and any SysEx responses.

  python3 fm1_diag.py [--port NAME] [--duration SECONDS]
"""
import sys, time, re
try:
    import mido
except ImportError:
    sys.exit("needs mido: pip install mido python-rtmidi")

def find_ports(hint=None):
    ins = mido.get_input_names()
    outs = mido.get_output_names()
    pat = re.compile(r'fm-1|big beat|felucca|sinco|usb-midi|midi', re.I)
    def pick(names):
        for n in names:
            if hint and hint.lower() in n.lower(): return n
            if pat.search(n): return n
        return names[0] if names else None
    return pick(ins), pick(outs)

# Identity query
HS_QUERY = bytes([0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7])

duration = 30
port_hint = None
for i, a in enumerate(sys.argv[1:]):
    if a == '--duration' and i + 2 <= len(sys.argv[1:]): duration = int(sys.argv[i + 2])
    if a == '--port' and i + 2 <= len(sys.argv[1:]): port_hint = sys.argv[i + 2]

inp_name, out_name = find_ports(port_hint)
if not inp_name:
    print("No MIDI input ports found. Available:")
    for n in mido.get_input_names(): print(f"  {n}")
    sys.exit(1)

print(f"input:  {inp_name}")
print(f"output: {out_name}")
print(f"listening for {duration}s... (Ctrl-C to stop)\n")

inp = mido.open_input(inp_name)
if out_name:
    out = mido.open_output(out_name)
    # Send identity query
    out.send(mido.Message('sysex', data=list(HS_QUERY[1:-1])))
    print("sent identity query")

t0 = time.time()
count = 0
try:
    while time.time() - t0 < duration:
        msg = inp.poll()
        if msg:
            elapsed = time.time() - t0
            if msg.type == 'sysex':
                hexdata = ' '.join(f'{b:02X}' for b in msg.data[:64])
                print(f"[{elapsed:7.3f}] SysEx ({len(msg.data)} bytes): F0 {hexdata}{' ...' if len(msg.data) > 64 else ''} F7")
                # Try to decode identity response
                if len(msg.data) >= 8 and msg.data[0] == 0x00 and msg.data[1] == 0x32 and msg.data[2] == 0x45:
                    # Unpack 7-bit data
                    packed = msg.data[5:]
                    raw, acc, nb = [], 0, 0
                    for b in packed:
                        acc |= b << nb; nb += 7
                        while nb >= 8: raw.append(acc & 0xFF); acc >>= 8; nb -= 8
                    ident = bytes(raw).split(b'\x00')[0].decode('ascii', errors='replace')
                    print(f"         identity: {ident}")
            elif msg.type == 'note_on':
                print(f"[{elapsed:7.3f}] NoteOn  ch={msg.channel} note={msg.note} vel={msg.velocity}")
            elif msg.type == 'note_off':
                print(f"[{elapsed:7.3f}] NoteOff ch={msg.channel} note={msg.note}")
            elif msg.type == 'control_change':
                print(f"[{elapsed:7.3f}] CC      ch={msg.channel} cc={msg.control} val={msg.value}")
            elif msg.type == 'clock':
                pass  # suppress clock spam
            elif msg.type in ('start', 'stop', 'continue'):
                print(f"[{elapsed:7.3f}] {msg.type.upper()}")
            else:
                print(f"[{elapsed:7.3f}] {msg}")
            count += 1
        else:
            time.sleep(0.001)
except KeyboardInterrupt:
    pass

print(f"\n{count} messages captured in {time.time() - t0:.1f}s")
inp.close()
if out_name: out.close()

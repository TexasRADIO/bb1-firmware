import sys, html, os
sys.path.insert(0, os.path.dirname(__file__))
from lib import md
SITE = sys.argv[1]
items = [
 ('songs first', 'BB1 opens on a welcome card. Turn {ALGO} to choose a song and press {PLAY}, or press {EDIT} to open the full song list. In the list, pick a song with {ALGO} or keys {1}–{16} and press {PLAY}: KIT A, KIT B and Dillo load together, and RHYTHM CONDUCTOR opens. {B5} {B6} {B7} switch between SONGS, ERAS and PRESETS, the four Roland rhythm machines.', '9.1'),
 ('three musical destinations', 'Tap {HOME}, then turn {SELECT}: RHYTHM CONDUCTOR is the whole band, BIG BEAT is the two-layer drum machine, and DILLO is the acid bass. The screen changes immediately to the selected instrument.', '7.1'),
 ('rhythm conductor', 'The bandleader screen shows the song, its sections as big coloured buttons on white keys {1}–{16}, and KIT A, KIT B and Dillo. {B1} {B2} {B3} focus and mute a player; with {REC}, solo.', '9.1'),
 ('the rhythm conductor', 'One white key moves the whole band to a section on the next bar, all together or not at all. Players on another song keep playing. Press another key to change your mind, the pulsing key again to cancel.', '11.1'),
 ('combine rhythms', 'Press section keys together on RHYTHM CONDUCTOR to combine their rhythms. Hold a key and tap others to add or remove sections, then release. A single section key returns to that rhythm alone.', '11.1'),
 ('every section has a key', '{OCT−} {OCT+} on RHYTHM CONDUCTOR turn pages of 16 sections, up to 64, so the longest Roland machines play to the end.', '9.1'),
 ('lists that read', 'KIT, PATTERN, the song list and Dillo\'s PATCH page are full-screen lists of ten, the playing one marked, with one line at the bottom saying what happens next.', '9.4'),
 ('one lighting rule', 'Half-lit: works here. Bright: on or playing. Pulse: waiting for the bar. Off: does nothing here. The screen, the legend and the help always agree with the lights.', '5.5'),
 ('help that explains', 'Hold {HOME}: the lights stay on and every lit key, button and knob is listed, on every page. Press a key while holding {HOME} to read what it does without triggering it.', '4.3'),
 ('dillo\'s sounds while you play', 'Tap {HOME}, turn {SELECT} to DILLO, then turn {PRESETS} and keep playing: the keys stay notes and each sound arrives when you stop turning. {EDIT} keeps it; {HOME} puts your old sound back.', '9.4'),
 ('modes, tabs and SONG', '{LFO} steps EVOLVE, EUCLID and RANDOM. FX shows ECHO, TAPE DRIVE, SPRING, MOTION and SENDS on white keys {1}–{5}. The SONG arranger is on {GLO} then white key {4}.', '9.9'),
 ('focused drum library', 'SLOOP kits have been removed to make room for the new playing interface. Older projects that depend on those kits are unsupported. The 808, seven sampled machines and USER kit builder remain.', '15.1'),
 ('one install', 'The help texts are new, so the sound library is too. The installer puts both on in one click.', '3.3'),
]
o = ['<!doctype html>\n<html lang="en-GB">\n<head>\n<meta charset="utf-8">\n<meta name="robots" content="noindex,nofollow">\n<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n<meta name="description" content="What is new in Big Beat Studio BB1 2.1 for the FM-1, compared with 2.0.">\n<title>Big Beat Studio BB1 2.1 · What\'s new</title>\n<link rel="stylesheet" href="/fm1/brand.css">\n<link rel="stylesheet" href="/fm1/guide/manual.css">\n<style>.wn{list-style:none;margin:0;padding:0;counter-reset:n}.wn li{display:grid;grid-template-columns:56px 1fr;gap:12px;padding:14px 0;border-bottom:1px solid var(--line);text-transform:uppercase}.wn li::before{counter-increment:n;content:counter(n);display:inline-grid;place-items:center;width:40px;height:40px;border:2px solid var(--ink);border-radius:50%;font:400 18px/1 var(--f-cap)}.wn h2{margin:0 0 4px;font:400 20px/1.1 var(--f-disp)}.wn p{margin:0 0 4px}.wn a{font-size:12px}</style>\n</head>\n<body>\n<nav class="top" aria-label="Big Beat navigation"><a href="/">THE BIG BEAT</a><a href="/fm1/">BB1</a><a href="/fm1/guide/">Guide</a><a href="/fm1/emulator/2.1/">Play in your browser</a><a href="/fm1/install/">Install</a></nav>\n<div class="wrap" style="max-width:900px">',
     '<div class="chbar"><h1><span class="num">2.1</span>what\'s new</h1><a href="/fm1/guide/">the guide</a></div>',
     '<div class="intro"><p>Big Beat Studio BB1 2.1, compared with 2.0.</p><p>Install it from <a href="/fm1/install/">the installer page</a>.</p></div><ol class="wn">']
for t, d, s in items:
    o.append(f'<li><div><h2>{html.escape(t)}</h2><p>{md(d)}</p><a href="/fm1/guide/{s.split(".")[0]}/#{s}">guide {s} →</a></div></li>')
o.append('</ol><footer class="foot"><span>Big Beat Studio BB1 for the FM-1</span><a href="/fm1/guide/">user guide →</a></footer></div>\n</body>\n</html>\n')
d = os.path.join(SITE, 'public/fm1/whats-new'); os.makedirs(d, exist_ok=True)
open(os.path.join(d, 'index.html'), 'w').write('\n'.join(o))
print('whats-new', len(items))

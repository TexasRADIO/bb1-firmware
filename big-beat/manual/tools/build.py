#!/usr/bin/env python3
"""Lane M: build the BB1 manual (chapter pages, index, print edition), the What's New page and the source trace.
usage: build.py D4_TREE SR_TREE SITE_ROOT SHOTS_PNG_DIR CATALOGUE_TXT KEYMAP_JSON TRACE_OUT"""
import html, os, re, shutil, sys
sys.path.insert(0, os.path.dirname(__file__))
import lib, content
from lib import md
from PIL import Image

D4, SR, SITE, SHOTS, CATF, KMF, TRACE_OUT = sys.argv[1:8]
lib.TREES.update(d4=D4, sr=SR, site=SITE)
content.CATFILE = CATF
content.keymap(KMF)
OUT = os.path.join(SITE, 'public/fm1/guide')
SKIP = set()   # 2.1: frames whose status label the renderer cuts are not drawn at all (manual_capture.c: FX TAPE DRIVE)

CSS = r"""
:root{--paper:#e9eae8;--paper2:#f6f6f4;--band:#d1d3cf;--ink:#1b1c1b;--mute:#5f625d;--line:#a6a9a3;--accent:#ffae42;
 --cap:#c9cbc6;--capw:#ffffff;--dev:#d6d8d4;--devc:#c3c5c0;--devk:#ecedeb;--devb:#5a5c5f;
 --f-disp:"BB Rodeo","Inter","Helvetica Neue",Arial,sans-serif;--f-cap:"BB Wanted","Inter","Helvetica Neue",Arial,sans-serif;
 --f-body:"BB Plumber","Inter","Helvetica Neue",Arial,sans-serif;--f-mono:ui-monospace,Menlo,Consolas,monospace;color-scheme:light}
@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){--paper:#121312;--paper2:#1b1c1b;--band:#2d2f2c;--ink:#e9eae6;--mute:#a3a69f;--line:#4a4d48;
 --cap:#4a4c4f;--capw:#2a2b2d;--dev:#262826;--devc:#363836;--devk:#3a3c3a;--devb:#0e0f0e;color-scheme:dark}}
:root[data-theme="dark"]{--paper:#121312;--paper2:#1b1c1b;--band:#2d2f2c;--ink:#e9eae6;--mute:#a3a69f;--line:#4a4d48;--cap:#4a4c4f;--capw:#2a2b2d;--dev:#262826;--devc:#363836;--devk:#3a3c3a;--devb:#0e0f0e;color-scheme:dark}
*{box-sizing:border-box}html{-webkit-text-size-adjust:100%}
body{margin:0;background:var(--paper);color:var(--ink);font:400 15px/1.45 var(--f-body);font-variant-ligatures:none;font-feature-settings:"liga" 0,"dlig" 0,"clig" 0}
a{color:inherit}img{max-width:100%;height:auto}
.top{display:flex;gap:18px;flex-wrap:wrap;align-items:baseline;max-width:1200px;margin:0 auto;padding:18px 16px 0;text-transform:uppercase;font-size:14px}
.top a{text-decoration:none}.top a:first-child{font:400 24px/1 var(--f-disp)}
.wrap{max-width:1200px;margin:0 auto;padding:16px 16px 72px}
main{min-width:0}
.layout{display:grid;grid-template-columns:220px minmax(0,1fr);gap:28px;align-items:start}
.side{position:sticky;top:12px;max-height:calc(100vh - 24px);overflow:auto;font-size:13px;text-transform:uppercase;border:1px solid var(--line);background:var(--paper2);padding:10px 12px}
.side a{display:block;text-decoration:none;padding:3px 0}.side a span{font-family:var(--f-mono);color:var(--mute);margin-right:6px}
.side a.cur{font-weight:700}.side .sub{padding-left:16px;font-size:12px}
/* cover */
.cover{display:grid;grid-template-columns:auto 1fr auto auto;gap:24px;align-items:end;padding:20px 0 14px;border-bottom:2px solid var(--ink)}
.cover .model{font:400 64px/0.9 var(--f-disp)}.cover .doc,.cover .ver{text-transform:uppercase;font-size:14px}.cover .ver{color:var(--mute)}
.quick{display:flex;justify-content:space-between;align-items:center;gap:16px;flex-wrap:wrap;margin:22px 0;padding:14px 16px;background:var(--ink);color:var(--paper)}
.quick b{font:400 24px/1 var(--f-disp);text-transform:uppercase}.quick a{color:var(--paper);text-transform:uppercase}
.toc{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:0 24px;margin:8px 0 0}
.toc a{display:grid;grid-template-columns:40px 1fr;padding:9px 0;border-bottom:1px solid var(--line);text-decoration:none;text-transform:uppercase}
.toc a span:first-child{font-family:var(--f-mono);color:var(--mute)}.toc .pg{display:none}
/* chapter */
.chbar{display:flex;justify-content:space-between;align-items:center;gap:12px;background:var(--ink);color:var(--paper);padding:12px 16px;margin:0 0 20px}
.chbar h1{margin:0;font:400 28px/1 var(--f-disp);text-transform:uppercase;display:flex;gap:22px;align-items:center}
.chbar h1 .num{display:inline-grid;place-items:center;min-width:40px;height:40px;border:2px solid var(--paper);border-radius:50%;font:400 20px/1 var(--f-cap)}
.chbar a{color:var(--paper);font-size:12px;text-transform:uppercase;text-decoration:none;white-space:nowrap}
.intro{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:24px;font-size:19px;line-height:1.35;text-transform:uppercase;margin:0 0 30px}
.intro p{margin:0}
.secbar{display:flex;justify-content:space-between;align-items:center;background:var(--band);border:1px solid var(--line);padding:9px 14px;margin:34px 0 14px}
.secbar h2{margin:0;font:400 22px/1 var(--f-disp);text-transform:uppercase;display:flex;gap:22px}.secbar h2 span{white-space:nowrap}
.secbar a{font-size:11px;text-transform:uppercase;text-decoration:none}
.secintro{text-transform:uppercase;max-width:760px;margin:0 0 14px;font-size:16px}
.grid{display:grid;grid-template-columns:repeat(12,minmax(0,1fr));gap:14px 12px}
.card{grid-column:span 4;min-width:0;display:flex;flex-direction:column}
.card.s3{grid-column:span 3}.card.s6{grid-column:span 6}.card.s8{grid-column:span 8}.card.s12{grid-column:span 12}
.frame{border:1px solid var(--line);background:var(--paper2)}
.frame h3{margin:0;padding:8px 10px;background:var(--band);border-bottom:1px solid var(--line);font:400 15px/1.1 var(--f-disp);text-transform:uppercase;display:flex;gap:10px;align-items:center}
.frame h3 .n{display:inline-grid;place-items:center;width:24px;height:24px;border:1.5px solid var(--ink);border-radius:50%;font:400 13px/1 var(--f-cap)}
.strip{display:block;width:100%;height:auto;background:#000;image-rendering:pixelated}
.art{padding:14px 10px;display:flex;flex-wrap:wrap;gap:10px;justify-content:center;align-items:center;min-height:130px}
.card.dark .frame{background:#000;color:#f2f2ee}.card.dark .frame h3{background:#000;color:#f2f2ee;border-color:#333}
.cap{text-transform:uppercase;font-size:14px;padding:8px 0 0}.cap p{margin:0 0 7px}
.shot{width:100%;max-width:300px;aspect-ratio:1;background:#000;image-rendering:auto;border:1px solid var(--ink)}
.shot.sm{max-width:170px}
svg.panel{width:100%;height:auto;display:block}
svg.gest{max-width:100%;height:auto}
svg .capt,svg .plab,svg .knum,svg .krole{font-family:var(--f-cap);fill:var(--ink)}svg .capt.onb{fill:var(--paper2)}svg .flag{font-family:var(--f-cap)}svg .op2{font-family:var(--f-body)}
svg .plab{fill:var(--mute)}svg .plab.on{fill:var(--ink)}svg .knum{fill:var(--mute)}svg .knum.on{fill:var(--ink)}svg .onb{fill:var(--paper2)}

.knobrow{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;padding:10px;border-top:1px solid var(--line);text-transform:uppercase;font-size:13px}
.knobrow div{display:flex;gap:6px;align-items:center}
.jump{display:inline-block;font:400 20px/1 var(--f-disp);text-transform:uppercase;text-decoration:none;border:2px solid var(--ink);padding:12px 16px;box-shadow:4px 4px 0 var(--ink);background:var(--capw)}
/* inline chips */
.kc{display:inline-block;font:400 11.5px/1 var(--f-cap);text-transform:uppercase;letter-spacing:.03em;padding:3px 5px 2px;margin:0 1px;border:1.5px solid var(--ink);box-shadow:2px 2px 0 var(--ink);background:var(--cap);color:var(--ink);vertical-align:2px;white-space:nowrap}
.kc-wht{background:var(--capw)}.kc-blk{background:var(--ink);color:var(--paper2)}.kc-enc,.kc-knob{border-radius:10px;background:var(--capw)}
.op{font-family:var(--f-body);margin:0 3px}.scr{font-family:var(--f-mono);font-size:.86em;background:#000;color:#f6f6f2;padding:1px 4px;white-space:nowrap}
/* tables */
.tbl{overflow-x:auto;margin:14px 0 0;border:1px solid var(--line);background:var(--paper2)}
table{border-collapse:collapse;width:100%;font-size:14px;text-transform:uppercase}
th,td{text-align:left;padding:8px 10px;border-bottom:1px solid var(--line);vertical-align:top}
th{background:var(--band);font:400 13px/1.2 var(--f-disp)}td:first-child{white-space:nowrap}
.tbl.narrow{max-width:420px}
/* screen map */
.smap{display:grid;grid-template-columns:minmax(0,360px) 1fr;gap:20px;padding:16px;align-items:start}
.smap-img{position:relative}.smap-img .shot{max-width:none}
.mk{position:absolute;display:grid;place-items:center;width:22px;height:22px;border-radius:50%;background:var(--accent);color:#000;font:400 12px/1 var(--f-cap);border:1.5px solid #000;transform:translate(-2px,-2px)}
.smap-list{margin:0;padding:0;list-style:none;text-transform:uppercase;font-size:14px}
.smap-list li{padding:6px 0;border-bottom:1px solid #333;display:grid;grid-template-columns:30px 1fr}
.smap-list b{display:inline-grid;place-items:center;width:22px;height:22px;border-radius:50%;background:var(--accent);color:#000;font:400 12px/1 var(--f-cap)}
ul.cols4{columns:4 180px;margin:0;padding:0;list-style:none}ul.cols4 li{break-inside:avoid;padding:2px 0}
ol.steps{margin:0;padding-left:20px}ol.steps li{margin:0 0 6px}
.pager{display:flex;justify-content:space-between;margin:40px 0 0;padding-top:14px;border-top:2px solid var(--ink);text-transform:uppercase}
.pager a{text-decoration:none}
.idx{columns:3 220px;text-transform:uppercase;font-size:14px}.idx a{display:block;text-decoration:none;padding:2px 0}.idx a span{font-family:var(--f-mono);color:var(--mute);float:right}
.idx h4{font:400 18px/1 var(--f-disp);margin:12px 0 4px;break-after:avoid}
footer.foot{margin-top:46px;padding-top:12px;border-top:1px solid var(--line);font-size:12px;text-transform:uppercase;color:var(--mute);display:flex;justify-content:space-between;gap:10px;flex-wrap:wrap}
@media (max-width:900px){.layout{grid-template-columns:minmax(0,1fr)}.side{position:static;max-height:none}.toc{grid-template-columns:repeat(2,minmax(0,1fr))}
 .card,.card.s3{grid-column:span 6}.card.s8{grid-column:span 12}.smap{grid-template-columns:1fr}}
@media (max-width:600px){.top{gap:10px 14px;font-size:12px}.top a:first-child{flex-basis:100%;font-size:22px}.intro{grid-template-columns:1fr;font-size:16px}.toc{grid-template-columns:1fr}.cover{grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}.cover>*{min-width:0;overflow-wrap:anywhere}.cover .model{font-size:48px}.cover .doc,.cover .ver{font-size:12px}.quick{grid-template-columns:1fr;gap:8px}.quick b{font-size:20px}
 .card,.card.s3,.card.s6,.card.s8{grid-column:span 12}.chbar h1{font-size:20px;gap:12px}.chbar h1 .num{min-width:32px;height:32px;font-size:16px}
 .knobrow{grid-template-columns:repeat(2,1fr)}.secbar,.chbar{flex-wrap:wrap}.secbar h2{font-size:16px;gap:12px;overflow-wrap:anywhere}.chbar h1{overflow-wrap:anywhere}.secbar>a,.chbar>a{flex-basis:100%;font-size:10px;white-space:normal}.frame h3{font-size:13px}.side{display:none}.shot,.smap-img .shot{max-width:100%;width:100%;height:auto}td:first-child{white-space:normal}}
@media (prefers-reduced-motion:reduce){*{scroll-behavior:auto}}
/* print: A4, each chapter starts a page, nothing cut */
@page{size:A4;margin:12mm 11mm 14mm}
@media print{
 :root{--paper:#fff;--paper2:#fff}
 body{font-size:10.5pt;background:#fff}.top,.side,.pager,.noprint{display:none!important}
 .layout{display:block}.wrap{max-width:none;padding:0}
 section.chapter{break-before:page}section.chapter:first-of-type{break-before:auto}
 .secbar{break-after:avoid}.card,.tbl tr,.smap,figure{break-inside:avoid}
 .grid{gap:8px}.card{grid-column:span 4}.card.s3{grid-column:span 3}.card.s6{grid-column:span 6}.card.s8{grid-column:span 8}.card.s12{grid-column:span 12}
 .smap{grid-template-columns:240px 1fr;padding:10px}.smap-img .shot{max-width:240px}.smap-list li{padding:3px 0}
 .shot{max-width:200px}.shot.sm{max-width:120px}.toc .pg{display:block;text-align:right;font-family:var(--f-mono)}.toc a{grid-template-columns:34px 1fr 40px}
 .intro{font-size:12pt}a{text-decoration:none}.tbl{overflow:visible}
}
"""

FONTS = '<link rel="stylesheet" href="/fm1/brand.css">'

def head(title, desc, extra=''):
    return f"""<!doctype html>
<html lang="en-GB">
<head>
<meta charset="utf-8">
<meta name="robots" content="noindex,nofollow">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<meta name="description" content="{html.escape(desc)}">
<title>{html.escape(title)}</title>
{FONTS}
<link rel="stylesheet" href="/fm1/guide/manual.css">{extra}
</head>
<body>
<nav class="top" aria-label="Big Beat navigation"><a href="/">THE BIG BEAT</a><a href="/fm1/">BB1</a><a href="/fm1/guide/">Guide</a><a href="/fm1/emulator/2.1/">Play in your browser</a><a href="/fm1/install/">Install</a></nav>
"""

def render_card(c):
    span = {3: 's3', 4: '', 6: 's6', 8: 's8', 12: 's12'}[c['span']]
    body = ''.join(f'<p>{md(b)}</p>' if not b.startswith('<ol') and not b.startswith('<ul') else md(b) for b in c['body'])
    strip = content.strip(c['strip']) if c['strip'] else ''
    art = f'<div class="art">{c["art"]}</div>' if c['art'] else ''
    return (f'<div class="card {span} {c["cls"]}"><div class="frame"><h3>{c["title"]}</h3>{strip}{art}</div>'
            f'{"<div class=cap>" + body + "</div>" if body else ""}</div>')

def render_section(s, chap):
    cards = ''.join(render_card(c) for c in s['cards'])
    intro = ''.join(f'<p class="secintro">{md(x)}</p>' for x in s['intro'])
    return (f'<div class="sec" id="{s["num"]}"><div class="secbar"><h2><span>{s["num"]}</span>{html.escape(s["title"])}</h2>'
            f'<a class="noprint" href="#top">return to index</a></div>{intro}'
            f'{"<div class=grid>" + cards + "</div>" if cards else ""}{s["extra"]}</div>')

def render_chapter(c, link_index='/fm1/guide/'):
    intro = ''.join(f'<p>{md(x)}</p>' for x in c['intro'])
    secs = ''.join(render_section(s, c) for s in c['sections'])
    return (f'<section class="chapter" id="{c["num"]}"><div class="chbar"><h1><span class="num">{c["num"]}</span>{html.escape(c["title"])}</h1>'
            f'<a href="{link_index}">return to index</a></div><div class="intro">{intro}</div>{secs}</section>')

def side(chapters, cur):
    o = ['<nav class="side" aria-label="Chapters"><a href="/fm1/guide/"><span>·</span>index</a>']
    for c in chapters:
        o.append(f'<a href="/fm1/guide/{c["num"]}/" class="{"cur" if c["num"] == cur else ""}"><span>{c["num"]}</span>{html.escape(c["title"])}</a>')
        if c['num'] == cur:
            o += [f'<a class="sub" href="#{s["num"]}"><span>{s["num"]}</span>{html.escape(s["title"])}</a>' for s in c['sections']]
    o.append('</nav>')
    return ''.join(o)

def toc(chapters, pages=None):
    return '<nav class="toc" aria-label="Contents">' + ''.join(
        f'<a href="{("#" + str(c["num"])) if pages is not None else "/fm1/guide/%d/" % c["num"]}"><span>{c["num"]}</span><span>{html.escape(c["title"])}</span>'
        f'<span class="pg">{(pages or {}).get(c["num"], "")}</span></a>' for c in chapters) + '</nav>'

def cover():
    return ('<header class="cover" id="top"><div class="model">FM-1</div><div class="doc">Big Beat Studio BB1<br>user guide</div>'
            '<div class="doc">two drum machines<br>and dillo</div><div class="ver">firmware 2.1</div></header>')

FOOT = '<footer class="foot"><span>Big Beat Studio BB1 for the FM-1 · user guide</span><span>screens drawn by bb1\'s own display code</span></footer>'

def index_terms(chapters):
    terms = {}
    for c in chapters:
        for s in c['sections']:
            terms.setdefault(s['title'], (c['num'], s['num']))
    extra = {'ACID': '9.1', 'APPLIED': '4.2', 'QUEUED': '4.2', 'BUILD': '11.6', 'USER kits': '11.6', 'ratchet': '11.5', 'undo': '7.4', 'swing': '11.9',
             'tempo': '11.9', 'tap tempo': '5.3', 'dub throw': '9.7', 'mute': '9.1', 'solo': '5.3', 'kill': '9.2', 'accent': '10.1', 'help': '4.3',
             'HELP UNAVAILABLE': '15.8', 'MIDI clock': '13.1', 'program change': '13.1', 'pitch bend': '13.1', 'EUCLID': '11.4', 'RANDOM': '11.4',
             'drum choices': '11.7', 'scene': '11.8', 'other data': '11.8', 'count-in': '10.2', 'take': '10.2', 'sections': '9.1', 'SUBST': '9.5',
             'SONGS': '9.1', 'ERAS': '9.1', 'PRESETS (Roland)': '9.1', 'song list': '9.1', 'Rhythm Conductor': '11.1', 'chooser lists': '9.4', 'cue': '11.1', 'HOLD (mixed songs)': '7.2', 'lights': '5.5',
             'half-lit': '5.5', 'pulse': '5.5', 'press to explain': '4.3', 'KEEP': '9.4', 'SOUND RESTORED': '9.4', 'Dillo sounds': '9.4',
             'TAPE DRIVE': '9.7', 'tabs (FX)': '9.7', 'modes (EVOLVE)': '9.9', 'CAPTURE': '9.9', 'APPLY': '9.9', 'SONG arranger': '9.11',
             'Roland machines': '15.6', 'sections past 16': '9.1',
             'key legend': '4.2', 'knob pop-up': '4.2', 'armadillo': '4.4', 'install': '3.2', 'recovery': '13.5', 'transpose': '9.10', 'A/B balance': '9.10',
             'favourite colours': None}
    for k, v in extra.items():
        if v: terms.setdefault(k, (int(v.split('.')[0]), v))
    by = {}
    for t, (cn, sn) in terms.items():
        by.setdefault(t[0].upper(), []).append((t, cn, sn))
    o = ['<div class="idx">']
    for L in sorted(by):
        o.append(f'<h4>{L}</h4>')
        for t, cn, sn in sorted(by[L], key=lambda x: x[0].lower()):
            o.append(f'<a href="/fm1/guide/{cn}/#{sn}">{html.escape(t)}<span>{sn}</span></a>')
    o.append('</div>')
    return ''.join(o)

def live_aside():
    """the release team's install note on the live chapter 3 (added at release, not by the guide builder): carried over unchanged"""
    f = os.path.join(OUT, '3', 'index.html')
    if not os.path.exists(f): return ''
    m = re.search(r'<aside class="release-install-note">.*?</aside>', open(f).read(), re.S)
    return m.group(0) if m else ''

def main():
    aside = live_aside()
    chapters = content.build()
    lib.where('16')
    idx = dict(num=16, title='index', intro=[lib.S('Every topic, A to Z, with its section number.'), lib.S('Section numbers match the headings exactly.')], sections=[])
    idx_html = index_terms(chapters)
    chapters_all = chapters + [idx]
    os.makedirs(OUT, exist_ok=True)
    open(os.path.join(OUT, 'manual.css'), 'w').write(CSS.strip() + '\n')
    # screens and strips
    sd = os.path.join(OUT, 'screens'); shutil.rmtree(sd, ignore_errors=True); os.makedirs(sd)
    used = set()
    for root, _, files in os.walk(OUT):
        pass
    for f in sorted(os.listdir(SHOTS)):
        if not f.endswith('.png') or f in SKIP: continue
        im = Image.open(os.path.join(SHOTS, f)).convert('RGB')
        im.save(os.path.join(sd, f), optimize=True)
        im.crop((0, 0, 480, 48)).save(os.path.join(sd, 'strip-' + f), optimize=True)
    for f in sorted(os.listdir(SHOTS)):          # RC2: the HOME key maps drawn by the firmware lane (perform-keymap.svg)
        if f.endswith('.svg'): shutil.copy(os.path.join(SHOTS, f), os.path.join(sd, f))
    # chapter pages
    desc = "Big Beat Studio BB1 user guide for the FM-1"
    for i, c in enumerate(chapters_all):
        d = os.path.join(OUT, str(c['num'])); os.makedirs(d, exist_ok=True)
        prev = chapters_all[i - 1] if i else None; nxt = chapters_all[i + 1] if i + 1 < len(chapters_all) else None
        pager = ('<div class="pager">' + (f'<a href="/fm1/guide/{prev["num"]}/">← {prev["num"]} {html.escape(prev["title"])}</a>' if prev else '<a href="/fm1/guide/">← index</a>') +
                 (f'<a href="/fm1/guide/{nxt["num"]}/">{nxt["num"]} {html.escape(nxt["title"])} →</a>' if nxt else '<a href="/fm1/guide/">index →</a>') + '</div>')
        body = render_chapter(c) if c['num'] != 16 else render_chapter(c).replace('</section>', idx_html + '</section>')
        page = (head(f'Big Beat Studio BB1 guide · {c["num"]} {c["title"]}', f'{desc}: chapter {c["num"]}, {c["title"]}.') +
                f'<div class="wrap" id="top"><div class="layout">{side(chapters_all, c["num"])}<main>{body}{pager}{FOOT}</main></div></div>\n</body>\n</html>\n')
        if c['num'] == 3 and aside: page = page.replace('<section class="chapter" id="3">', '<section class="chapter" id="3">' + aside, 1)
        open(os.path.join(d, 'index.html'), 'w').write(page)
    # index page
    ip = (head('Big Beat Studio BB1 user guide', 'Big Beat Studio BB1 user guide for the FM-1: quick start, every page, keys and combos, recording, songs, kits, MIDI and recovery.') +
          f'<div class="wrap">{cover()}<div class="quick"><b>new here? jump to get started</b><a href="/fm1/guide/8/">8 · play a song with the band →</a></div>'
          f'{toc(chapters_all)}<p class="noprint" style="margin-top:22px;text-transform:uppercase"><a href="/fm1/guide/print/">the whole guide on one page, for printing →</a> · <a href="/fm1/whats-new/">what\'s new in 2.1 →</a></p>{FOOT}</div>\n</body>\n</html>\n')
    open(os.path.join(OUT, 'index.html'), 'w').write(ip)
    # print edition (one page; contents page numbers filled in by the PDF pass via PAGES env)
    pages = {}
    pf = os.environ.get('MANUAL_PAGES')
    if pf and os.path.exists(pf):
        for line in open(pf):
            n, p = line.split(); pages[int(n)] = p
    body = ''.join(render_chapter(c, '#top') if c['num'] != 16 else render_chapter(c, '#top').replace('</section>', idx_html + '</section>') for c in chapters_all)
    pp = (head('Big Beat Studio BB1 user guide · print', 'Big Beat Studio BB1 user guide, all chapters on one page for printing to A4.') +
          f'<div class="wrap"><section class="chapter" id="0">{cover()}<div class="quick"><b>jump to get started</b><a href="#8">8 · play a song with the band</a></div>{toc(chapters_all, pages)}</section>{body}{FOOT}</div>\n</body>\n</html>\n')
    if aside: pp = pp.replace('<section class="chapter" id="3">', '<section class="chapter" id="3">' + aside, 1)
    os.makedirs(os.path.join(OUT, 'print'), exist_ok=True)
    open(os.path.join(OUT, 'print', 'index.html'), 'w').write(pp)
    # publish only the screens the pages use
    used = set()
    for root, _, files in os.walk(OUT):
        for f in files:
            if f == 'index.html': used |= set(re.findall(r'/fm1/guide/screens/([\w.-]+\.png)', open(os.path.join(root, f)).read()))
    for f in os.listdir(sd):
        if f.endswith('.png') and f not in used: os.remove(os.path.join(sd, f))
    # trace
    with open(TRACE_OUT, 'w') as f:
        f.write('# Big Beat Studio BB1 2.1 manual: source trace (review artifact, not published)\n\n')
        f.write('Source trees inspected for this build:\n\n')
        for name in lib.TREES:
            f.write(f'- {name}: {lib.tree_identity(name)}\n')
        f.write('\nLine numbers are resolved from these inputs; this trace is not device acceptance.\n\n'
                '| § | sentence | source |\n|---|---|---|\n')
        for w, t, s in lib.TRACE:
            f.write(f'| {w} | {t.replace("|", "/")} | {s} |\n')
    print('chapters', len(chapters_all), 'trace rows', len(lib.TRACE))
    for m in sorted(set(lib.MISSES)): print('MISS', m)
    if lib.MISSES:
        raise SystemExit('Guide verification failed: unresolved source references; output is review-only.')

main()

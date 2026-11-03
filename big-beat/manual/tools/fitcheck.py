"""Lane M: measure every keycap label against its cap, and every SVG label against the others, in Chromium.
Fails (exit 1) on any overflow or overlap. Runs each page at 1440 px, 390 px and in print media."""
import sys
from playwright.sync_api import sync_playwright
BASE = 'http://127.0.0.1:8765'
pages = ['/fm1/guide/'] + [f'/fm1/guide/{n}/' for n in range(1, 17)] + ['/fm1/guide/print/', '/fm1/whats-new/']
JS = r'''() => {
  const bad = []; let caps = 0, svgs = 0;
  for (const svg of document.querySelectorAll('svg')) {
    svgs++;
    for (const g of svg.querySelectorAll('g.fit')) {
      const r = g.querySelector('rect').getBBox();
      for (const t of g.querySelectorAll('text')) {
        caps++; const b = t.getBBox();
        if (b.x < r.x + 1 || b.x + b.width > r.x + r.width - 1) bad.push('overflow "' + t.textContent + '" ' + b.width.toFixed(1) + ' in ' + r.width.toFixed(1));
      }
    }
    const ts = [...svg.querySelectorAll('text')].map(t => ({t, b: t.getBBox(), g: t.closest('g.fit')}));
    for (let i = 0; i < ts.length; i++) for (let j = i + 1; j < ts.length; j++) {
      const a = ts[i], c = ts[j]; if (a.g && a.g === c.g) continue;
      if (a.b.x < c.b.x + c.b.width - 0.5 && c.b.x < a.b.x + a.b.width - 0.5 && a.b.y < c.b.y + c.b.height - 0.5 && c.b.y < a.b.y + a.b.height - 0.5)
        bad.push('overlap "' + a.t.textContent + '" / "' + c.t.textContent + '"');
    }
  }
  return {bad, caps, svgs};
}'''
fail = 0
with sync_playwright() as p:
    b = p.chromium.launch()
    for mode, w in (('1440', 1440), ('390', 390), ('print', 1000)):
        pg = b.new_page(viewport={'width': w, 'height': 900})
        if mode == 'print': pg.emulate_media(media='print')
        tot = 0
        for u in pages:
            pg.goto(BASE + u, wait_until='networkidle'); pg.evaluate('document.fonts.ready')
            r = pg.evaluate(JS); tot += r['caps']
            sw = pg.evaluate('document.documentElement.scrollWidth')
            if mode == '390' and sw > 390: r['bad'].append(f'horizontal scroll {sw}')
            for x in r['bad']: print(mode, u, x); fail += 1
        print(mode, 'labels checked', tot)
        pg.close()
    b.close()
print('FAIL' if fail else 'PASS', fail)
sys.exit(1 if fail else 0)

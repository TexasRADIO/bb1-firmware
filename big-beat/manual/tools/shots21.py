"""the two review screenshots: chapter 9.1 (STUDIO, both views) at 1440 px and chapter 8 (get started) at 390 px; plus What's New at both."""
import sys
from playwright.sync_api import sync_playwright
out = sys.argv[1]
with sync_playwright() as p:
    b = p.chromium.launch()
    for w, u, name, h in ((1440, '/fm1/guide/9/', 'guide-1440-studio', 2600), (390, '/fm1/guide/8/', 'guide-390-get-started', 3200),
                          (1440, '/fm1/whats-new/', 'whats-new-1440', 0), (390, '/fm1/whats-new/', 'whats-new-390', 0)):
        pg = b.new_page(viewport={'width': w, 'height': 900}); pg.goto('http://127.0.0.1:8765' + u, wait_until='networkidle')
        pg.evaluate('document.fonts.ready'); pg.wait_for_timeout(400)
        full = pg.evaluate('document.documentElement.scrollHeight')
        pg.screenshot(path=f'{out}/{name}.png', full_page=True, clip={'x': 0, 'y': 0, 'width': w, 'height': min(full, h) if h else full})
        print(name, w, 'scrollWidth', pg.evaluate('document.documentElement.scrollWidth'), 'height', full); pg.close()
    b.close()

"""print the manual to A4 and find each chapter's first page (for the contents page numbers)."""
import sys, re
from playwright.sync_api import sync_playwright
from pypdf import PdfReader
out, pages_out = sys.argv[1], sys.argv[2]
titles = sys.argv[3:]
with sync_playwright() as p:
    b = p.chromium.launch(); pg = b.new_page()
    pg.emulate_media(media='print')
    pg.goto('http://127.0.0.1:8765/fm1/guide/print/', wait_until='networkidle'); pg.wait_for_timeout(800)
    pg.pdf(path=out, format='A4', prefer_css_page_size=True, print_background=True)
    b.close()
r = PdfReader(out)
texts = [re.sub(r'\s+', ' ', (pg.extract_text() or '').lower()) for pg in r.pages]
res, start = [], 1
for i, t in enumerate(titles):
    n = i + 1
    for k in range(start, len(texts)):
        head = texts[k]   # whole page: chapter 3 carries the release team's install note above its bar
        if re.search(r'(^|\s)' + str(n) + r'\s*' + re.escape(t.lower()) + r' return to index', head):
            res.append((n, k + 1)); start = k + 1; break
open(pages_out, 'w').write(''.join(f'{n} {p}\n' for n, p in res))
print(len(r.pages), 'pages;', res)

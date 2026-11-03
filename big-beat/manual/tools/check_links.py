"""Check generated guide assets and chapter anchors; external routes are separate gates."""
import json
import sys
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit


class Document(HTMLParser):
    def __init__(self, path):
        super().__init__()
        self.ids, self.refs = set(), []
        self.feed(path.read_text())

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if 'id' in attrs:
            self.ids.add(attrs['id'])
        self.refs.extend(attrs[k] for k in ('src', 'href') if k in attrs)


def check(root):
    root = Path(root).resolve()
    docs = {p: Document(p) for p in (root / 'fm1/guide').rglob('*.html')}
    failures, checked = [], 0
    if not docs:
        failures.append({'error': 'No guide HTML found'})
    for path, doc in docs.items():
        for ref in doc.refs:
            url = urlsplit(ref)
            if url.scheme or url.netloc:
                continue
            if url.path and not url.path.startswith('/fm1/guide/'):
                continue
            target = root / unquote(url.path).lstrip('/') if url.path else path
            if target.is_dir():
                target /= 'index.html'
            checked += 1
            error = None
            if not target.exists():
                error = 'Missing file'
            elif url.fragment and target in docs and unquote(url.fragment) not in docs[target].ids:
                error = 'Missing anchor'
            if error:
                failures.append({'page': str(path.relative_to(root)), 'reference': ref, 'error': error})
    return {'html_files': len(docs), 'checked': checked, 'failures': failures,
            'scope': 'Guide-local assets and anchors only; excludes installer, player and external routes'}


if __name__ == '__main__':
    result = check(sys.argv[1])
    print(json.dumps(result, indent=2))
    sys.exit(bool(result['failures']))

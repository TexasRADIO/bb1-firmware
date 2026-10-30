"""BB1's package/runtime identity contract; no transport or device access."""
import ast
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
IDENTITY = json.loads((ROOT / 'big-beat/identity.json').read_text())
PRODUCT = IDENTITY['product']
DISPLAY_NAME = IDENTITY['display_name']

def validate_identity(product):
    if not re.fullmatch(r'FM-1_[0-9]+', product) or len(product) > 20:
        raise ValueError('expected an FM-1 numeric identity fitting 20 package markers')
    if any((ord(c) + i + 1) & 255 == 0x7d for i, c in enumerate(product)):
        raise ValueError('identity collides with the unused marker')
    return product

validate_identity(PRODUCT)
header = (ROOT / 'firmware/src/bb_identity.h').read_text()
for macro, value in [('BB1_PRODUCT', PRODUCT), ('BB1_DISPLAY_NAME', DISPLAY_NAME)]:
    found = re.search(r'^#define ' + macro + r' "([^"\n]+)"$', header, re.M)
    if not found or found[1] != value:
        raise ValueError('identity metadata/header mismatch: ' + macro)

def require_compiled_identity(app, product=PRODUCT):
    """Reject relabelling a previously built FM-1_900 image as the new product."""
    validate_identity(product)
    if product.encode('ascii') + b'\0' not in app:
        raise ValueError('application does not contain the requested runtime identity')

def verifier_for_product(path, product=PRODUCT):
    """Parameterise only the identity predicate of the hash-pinned verifier.

    Its complete independent geometry, CRC, loader, SDK and exact app checks
    remain unchanged. An unexpected verifier shape fails closed.
    """
    validate_identity(product)
    tree = ast.parse(Path(path).read_text())
    found = []
    for node in ast.walk(tree):
        if not isinstance(node, ast.Compare) or len(node.ops) != 1 or not isinstance(node.ops[0], ast.Eq):
            continue
        call = node.left
        if (isinstance(call, ast.Call) and isinstance(call.func, ast.Attribute) and
            isinstance(call.func.value, ast.Name) and call.func.value.id == 'installer' and
            call.func.attr == 'product_of' and len(call.args) == 1 and
            isinstance(call.args[0], ast.Name) and call.args[0].id == 'raw' and
            len(node.comparators) == 1 and isinstance(node.comparators[0], ast.Constant) and
            node.comparators[0].value == 'FM-1_900'):
            found.append(node.comparators[0])
    if len(found) != 1:
        raise ValueError('pinned verifier identity predicate changed')
    found[0].value = product
    namespace = {'__file__': str(path), '__name__': 'bb_identity_bound_verifier'}
    exec(compile(tree, str(path), 'exec'), namespace)
    return namespace['verify']

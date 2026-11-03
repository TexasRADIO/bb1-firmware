"""Resolve guide capture flags from the current 2.1 release recipe."""
import json
import sys
from pathlib import Path


def flags(root):
    build = Path(root) / 'big-beat/integration/build'
    front = json.loads((build / 'cmds-r4-onepass/main-front-command.json').read_text())
    recipe = json.loads((build / 'recipes/composition-21/recipe.json').read_text())
    definitions = {}
    for arg in front + recipe['extra_flags']:
        if arg.startswith('-D') and not arg.startswith('-D__DATE__'):
            definitions[arg.split('=', 1)[0]] = arg
    assert definitions['-DBB_WITH_SLOOP_KITS'] == '-DBB_WITH_SLOOP_KITS=0'
    assert definitions['-DBB_WITH_USER_KITS'] == '-DBB_WITH_USER_KITS=1'
    return list(definitions.values())


if __name__ == '__main__':
    print(' '.join(flags(sys.argv[1])))

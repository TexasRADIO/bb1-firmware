#!/usr/bin/env python3
"""Exercise the actual builder guard against real effective compile arguments.

Extract only the pure function definitions so the target-only builder's mount
and compilation side effects cannot run on the host.
"""
import ast
import copy
import json
from pathlib import Path

source = Path(__file__).resolve().parents[3]
builder = source / 'big-beat/integration/build/build_from_source.py'
recipe = json.loads((source / 'big-beat/integration/build/recipes/composition-20-lean/recipe.json').read_text())
cmd_dir = source / 'big-beat/integration/build/cmds-r4-onepass'
nodes = [n for n in ast.parse(builder.read_text()).body
         if isinstance(n, ast.FunctionDef) and n.name in ('rewrite', 'validate_profile')]
assert len(nodes) == 2
space = {'recipe': recipe, 'SRC': '/src', 'OUT': '/out'}
exec(compile(ast.Module(body=nodes, type_ignores=[]), str(builder), 'exec'), space)
commands = {}
for unit in recipe['profile_guard_units']:
    name = 'main-front-command.json' if unit == 'main' else unit + '-o-command.json'
    commands[unit] = space['rewrite'](json.loads((cmd_dir / name).read_text()))
validate = space['validate_profile']
validate(recipe, commands)
checks = 1
required = {**recipe['required_disabled_flags'], **recipe['required_profile_flags']}
# Every guard unit must refuse every changed or undefined required flag,
# including units that the original main/audio-only guard never examined.
for unit in recipe['profile_guard_units']:
    for flag, expected in required.items():
        for bad in ('-D' + flag + '=' + ('0' if expected == '1' else '1'), '-U' + flag):
            changed = dict(commands)
            changed[unit] = commands[unit] + [bad]
            try:
                validate(recipe, changed)
            except ValueError as error:
                assert unit in str(error) and flag in str(error)
            else:
                raise AssertionError((unit, flag, bad))
            checks += 1
# Refuse silent removal of a guarded unit; the four assembly exclusions
# are an explicit reviewed part of this recipe.
for bad_units in ([], recipe['profile_guard_units'][1:], recipe['profile_guard_units'] + ['main'], recipe['profile_guard_units'] + ['missing']):
    changed = copy.deepcopy(recipe)
    changed['profile_guard_units'] = bad_units
    try:
        validate(changed, commands)
    except ValueError:
        checks += 1
    else:
        raise AssertionError('invalid unit list accepted')
changed = copy.deepcopy(recipe)
changed['required_profile_flags']['BB_WITH_TELEGRAM'] = '1'
try:
    validate(changed, commands)
except ValueError:
    checks += 1
else:
    raise AssertionError('conflicting requirements accepted')
# Existing recipes keep their previous two-unit policy unless explicitly
# migrating to the complete-unit profile. No changed target flags required.
legacy = copy.deepcopy(recipe)
for key in ('profile_guard_units', 'profile_guard_exclusions', 'required_profile_flags'):
    legacy.pop(key)
validate(legacy, commands)
checks += 1
print(json.dumps({'passed': True, 'checks': checks, 'guarded_units': len(commands),
                  'required_flags': len(required), 'explicit_assembly_exclusions': list(recipe['profile_guard_exclusions']),
                  'target_compiler_or_device_called': False}, indent=2))

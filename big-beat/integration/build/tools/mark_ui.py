"""Size attributes only on explicit main-loop drawing/input helpers."""
from pathlib import Path
import importlib.util,re,sys
p=Path(sys.argv[1]);ir=p.read_text()
spec=importlib.util.spec_from_file_location('size_fns',Path('/work/target/tools/size_fns.py'))
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
names={'bb_target_draw','bb_backdrop','bb_mascot_ink','bb_scope_backdrop',
       'bb_home_result','bb_home_poll','bb_home_input','bb_home_label','bb_home_draw','bb_home_leds',
       'bb_kit_shelf_draw',
       'bb_c1_family_scene_restore','bb_c1_909_restore_suspended','bb_session_poll',
       'bb_sparks_draw','bb_library_draw','bb_help_draw','bb_evo_draw',
       'bb_number','bb_level_number','bb_rotary','bb_synth_draw',
       'bb_library_text','bb_library_label','bb_pitch_name',
       'bb_ui_kit_name','bb_ui_voice_name','bb_ui_kit_choose','bb_ui_909_parameter','bb_ui_909_knob'}
isr=module.isr_reach(ir)
assert not names&isr,'UI helper reachable from ISR'
names.update(set(module.definitions(Path('/work/target/firmware/src/bb_target_ui.c').read_text()))-isr)
for file in ('bb_target_session.c','bb_target_asset_console.c','bb_target_909_selector.c'):
    names.update(set(module.definitions((Path('/work/target/firmware/src')/file).read_text()))-isr)
hit=set()
def mark(m):
    if m[2] not in names:return m[0]
    hit.add(m[2]);return m[1]+' minsize'+m[3]
p.write_text(module.IR_DEF.sub(mark,ir))
# Conditional and inlined-away UI definitions need not appear in this profile.
assert hit, 'No eligible main-loop UI functions'
print('Marked',len(hit),'non-ISR UI functions for size')

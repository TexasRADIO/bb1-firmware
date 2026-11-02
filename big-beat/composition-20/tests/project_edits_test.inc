/* SPDX-License-Identifier: GPL-3.0-only */
static void command_edit(bb_command c,int changed){
 uint32_t before=audio.commands.project_edits;CK(bb_command_push(&audio.commands,&c));drain();
 if(changed)CK(audio.commands.project_edits>before);else CK(audio.commands.project_edits==before);
}
static void scalar_edit(bb_command_type type,unsigned target,unsigned voice,unsigned step,unsigned value,int changed){
 bb_command c={0};c.type=type;c.target=(uint8_t)target;c.voice=(uint8_t)voice;c.step=(uint8_t)step;c.value=(uint16_t)value;command_edit(c,changed);
}
int main(void){
 collect();out=fopen("/dev/null","wb");CK(out);setup(0);drain();
 scalar_edit(BB_CMD_TEMPO,0,0,0,audio.core.bpm,0);scalar_edit(BB_CMD_TEMPO,0,0,0,121,1);
 scalar_edit(BB_CMD_SWING,0,0,0,audio.core.swing,0);scalar_edit(BB_CMD_SWING,0,0,0,60,1);
 scalar_edit(BB_CMD_SWING_X2,0,0,0,120,0);scalar_edit(BB_CMD_SWING_X2,0,0,0,121,1);
 scalar_edit(BB_CMD_DRUM_LENGTH,0,0,0,audio.core.drums[0].length,0);scalar_edit(BB_CMD_DRUM_LENGTH,0,0,0,17,1);
 scalar_edit(BB_CMD_DRUM_MUTE,0,0,0,0,0);scalar_edit(BB_CMD_DRUM_MUTE,0,0,0,1,1);scalar_edit(BB_CMD_DRUM_MUTE,0,0,0,0,1);
 scalar_edit(BB_CMD_DRUM_LEVEL,0,0,0,audio.core.drums[0].level,0);scalar_edit(BB_CMD_DRUM_LEVEL,0,0,0,99,1);
 scalar_edit(BB_CMD_DRUM_ENABLE,1,0,0,1,0);scalar_edit(BB_CMD_DRUM_ENABLE,1,0,0,0,1);scalar_edit(BB_CMD_DRUM_ENABLE,1,0,0,1,1);
 scalar_edit(BB_CMD_SYNTH_LENGTH,0,0,0,audio.core.synths[0].length,0);scalar_edit(BB_CMD_SYNTH_LENGTH,0,0,0,19,1);
 scalar_edit(BB_CMD_SYNTH_MUTE,0,0,0,0,0);scalar_edit(BB_CMD_SYNTH_MUTE,0,0,0,1,1);scalar_edit(BB_CMD_SYNTH_MUTE,0,0,0,0,1);
 scalar_edit(BB_CMD_BAND_LEVEL,0,0,0,audio.core.band_level[0],0);scalar_edit(BB_CMD_BAND_LEVEL,0,0,0,97,1);
 unsigned cut=bb_synth_parameter_get(&audio.core,0,BB_PARAM_CUTOFF);scalar_edit(BB_CMD_SYNTH_PARAMETER,0,BB_PARAM_CUTOFF,0,cut,0);scalar_edit(BB_CMD_SYNTH_PARAMETER,0,BB_PARAM_CUTOFF,0,cut==900?1000:900,1);
 unsigned wet=bb_fx_parameter_get(&audio.core,BB_FX_ECHO_WET);scalar_edit(BB_CMD_FX_PARAMETER,0,BB_FX_ECHO_WET,0,wet,0);scalar_edit(BB_CMD_FX_PARAMETER,0,BB_FX_ECHO_WET,0,wet==40?41:40,1);
 unsigned tune=bb_drum_parameter_get(&audio.core.drums[0].sound,0,BB_DRUM_TUNE);scalar_edit(BB_CMD_DRUM_PARAMETER,0,0,BB_DRUM_TUNE,tune,0);scalar_edit(BB_CMD_DRUM_PARAMETER,0,0,BB_DRUM_TUNE,tune==65?64:65,1);
 tune=bb_drum_source_parameter_get(&audio.core.drums[0].sound,0,BB_DRUM_TUNE);scalar_edit(BB_CMD_DRUM_SOURCE_PARAMETER,0,0,BB_DRUM_TUNE,tune,0);scalar_edit(BB_CMD_DRUM_SOURCE_PARAMETER,0,0,BB_DRUM_TUNE,tune==66?65:66,1);
 scalar_edit(BB_CMD_DRUM_KIT,0,0,0,audio.core.drums[0].kit,1);scalar_edit(BB_CMD_DRUM_KIT,0,0,0,audio.core.drums[0].kit,0);
 scalar_edit(BB_CMD_DILLO_PATCH,0,0,0,1,memcmp(&audio.core.dillo,&bb_dillo_bank[1].sound,sizeof audio.core.dillo)!=0);scalar_edit(BB_CMD_DILLO_PATCH,0,0,0,1,0);
 scalar_edit(BB_CMD_DRUM_STEP,0,0,0,0,!!(audio.core.drums[0].steps[0].hits|audio.core.drums[0].steps[0].accents));scalar_edit(BB_CMD_DRUM_TOGGLE,0,0,0,1,0);scalar_edit(BB_CMD_DRUM_TOGGLE,0,0,0,0,1);scalar_edit(BB_CMD_DRUM_TOGGLE,0,0,0,1,1);
 scalar_edit(BB_CMD_NOTE_PITCH,0,0,0,audio.core.synths[0].notes[0].note,0);scalar_edit(BB_CMD_NOTE_SHIFT,0,0,0,127,0);scalar_edit(BB_CMD_NOTE_PITCH,0,0,0,59,1);
 scalar_edit(BB_CMD_GROUP_LEVEL,0,0,0,groups[0].fader[0].level,0);scalar_edit(BB_CMD_GROUP_LEVEL,0,0,0,61,1);scalar_edit(BB_CMD_GROUP_MUTE,0,0,0,groups[0].fader[0].muted,0);scalar_edit(BB_CMD_GROUP_MUTE,0,0,0,1,1);
 // Serialized note-expression changes count even if legacy note bytes match.
 CK(bb_note_expression_make(1,70,1,60,&audio.patterns.expression[0][0]));bb_command note={0};note.type=BB_CMD_NOTE;note.note=audio.core.synths[0].notes[0];command_edit(note,1);command_edit(note,0);
 // Performance-only actions never claim unsaved project edits.
 scalar_edit(BB_CMD_TRANSPORT,0,0,0,1,0);scalar_edit(BB_CMD_TRANSPORT,0,0,0,0,0);scalar_edit(BB_CMD_TRANSPORT_RESTART,0,0,0,0,0);scalar_edit(BB_CMD_TRANSPORT,0,0,0,0,0);
 scalar_edit(BB_CMD_BAND_TOGGLE,0,0,0,0,0);scalar_edit(BB_CMD_DRUM_TRIGGER,0,0,0,100,0);scalar_edit(BB_CMD_SYNTH_KEY,0,60,BB_KEY_PHYSICAL,100,0);scalar_edit(BB_CMD_SYNTH_KEY,0,60,BB_KEY_PHYSICAL,0,0);scalar_edit(BB_CMD_SYNTH_TRANSPOSE,0,0,0,49,0);
 uint32_t before=audio.commands.project_edits;bb_command invalid={0};invalid.type=BB_CMD_TEMPO;invalid.value=1;CK(!bb_command_push(&audio.commands,&invalid));CK(audio.commands.project_edits==before);
 invalid.type=BB_CMD_DRUM_PHRASE;invalid.value=1;command_edit(invalid,0);
 audio.commands.project_edits=UINT32_MAX-1;scalar_edit(BB_CMD_TEMPO,0,0,0,130,1);CK(audio.commands.project_edits==UINT32_MAX);scalar_edit(BB_CMD_TEMPO,0,0,0,131,0);CK(audio.commands.project_edits==UINT32_MAX);
 fclose(out);printf("PASS %u real-engine project edit checks; no-op/rejected/transient commands and saturation\n",checks);return 0;
}

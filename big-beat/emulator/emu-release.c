/* Big Beat FM-1 web emulator driver. Appended to the real firmware unity build (felucca.c) by host_prep.py,
 * exactly as the review journeys harness is: the firmware's own code runs unchanged; this file only sets the
 * debounced panel state the TIMER5 scan would publish, runs the main loop's UI half, and runs the audio
 * callback (audio_block) to fill a sample buffer. No device code path is replaced except the panel SPI. */
#define EMU_EXPORT(n) __attribute__((export_name(#n)))
static int32_t emu_ab[2u * 32u];
static float emu_out[2u * 4096u];
static unsigned emu_out_n;
static unsigned emu_acc;
#if !defined(BB_UI_V3) || BB_UI_V3
/* The browser can draw between LCD refreshes. Keep the extra mix labels
 * on the same frame as the current rows and NEXT flags. */
static struct {
    bb_factory_choice playing, queued;
    uint64_t playing_mask, queued_mask;
    uint32_t ticket;
} emu_conductor_frame;
#endif
static void emu_audio(unsigned quanta)
{
    unsigned q, b, i;
    for (q = 0; q < quanta; q++) {
        bb_target_half_begin();
        for (b = 0; b < 4u; b++) {
            audio_block(emu_ab, 32u);
            for (i = 0; i < 64u && emu_out_n < 2u * 4096u; i++)
                emu_out[emu_out_n++] = (float)emu_ab[i] * (1.0f / 2147483648.0f);
        }
    }
}
EMU_EXPORT(emu_init) void emu_init(void)
{
    fm1_ms = 1000u;
    *(volatile uint32_t *)0x1EEE004u = 2u;     /* CPU1 held: CPU0 renders serially (as the review harness) */
    panel_init();
    felucca_init();
    bb_target_init();
    bb_target_trio_init();
    bb_factory_init();
    bb_sample_boot();   /* the firmware's own validated pack loader: the page may have placed the library at 0xA0000 */
    bb_ui_init();
}
/* where the page writes the 240 KB sound library before emu_init (the FM-1 flash window at 0xA0000) */
EMU_EXPORT(emu_sound_window) uint32_t emu_sound_window(void) { return (uint32_t)(uintptr_t)fm1_xip_ptr(0xA0000u); }
/* which boot check refuses the pack (diagnostics for the page and tests): 1 magic, 2 length, 3 resident prepare,
 * 4 directory count, 5 cold rebind; 0 = the firmware loaded it */
EMU_EXPORT(emu_sound_check) uint32_t emu_sound_check(void)
{   /* (emu.c is appended after preprocessing: no build macros here, so the firmware's limit is spelled out) */
    const uint8_t *pack = fm1_xip_ptr(0xA0000u);
    unsigned bytes;
    if (bb_sample_boot_ready) return 0u;
    if (bb_fsmp_u32(pack) != 0x4b534242u) return 1u;
    bytes = bb_fsmp_u32(pack + 8);
    if (bytes < 256 || bytes > 245760u) return 2u;
    if (!bb_sample_resident_prepare(&bb_sample_store, pack, bytes)) return 3u;
    if (bb_sample_store.count < 3 || bb_sample_store.count > 8) return 4u;
    return 5u;
}
EMU_EXPORT(emu_sound_kits) uint32_t emu_sound_kits(void)
{
    return bb_sample_boot_ready ? bb_sample_musical_count : 0u;
}
/* one main-loop pass: 16 ms of firmware time */
EMU_EXPORT(emu_frame) void emu_frame(void)
{
    emu_acc += 11u;
    emu_audio(emu_acc / 2u); emu_acc %= 2u;
    fm1_ms += 16u;
    bb_target_input();
    bb_target_bank_service();
    bb_target_leds();
#if !defined(BB_UI_V3) || BB_UI_V3
    uint32_t draw_at = bb_v3_next;
#endif
    bb_target_draw();
#if !defined(BB_UI_V3) || BB_UI_V3
    if (draw_at != bb_v3_next && bb_v3_in.page == BB_UI_STUDIO) {
        emu_conductor_frame.ticket = bb_cd.ticket;
        if (bb_v3_in.perf_combo_playing) {
            emu_conductor_frame.playing = bb_cd.group_choice;
            emu_conductor_frame.playing_mask = bb_cd.pset;
        }
        if (bb_v3_in.perf_combo_queued) {
            emu_conductor_frame.queued = bb_cd.group_pending;
            emu_conductor_frame.queued_mask = bb_cd.qset;
        }
    }
#endif
    fm1_in.pressed = fm1_in.released = 0;
    fm1_in.notes_pressed = 0;
}
EMU_EXPORT(emu_button) void emu_button(unsigned b, int down)
{
    uint32_t m;
    if (b >= NB) return;
    m = 1u << panel.btn[b];
    if (down) { fm1_in.buttons |= m; fm1_in.pressed |= m; }
    else { fm1_in.buttons &= ~m; fm1_in.released |= m; }
}
EMU_EXPORT(emu_turn) void emu_turn(unsigned role, int n)
{
    if (role >= NE) return;
    fm1_in.enc_steps[panel.enc[role]] += (int16_t)(n * panel.dir[role]);
}
/* logical key: 0..15 white 1-16, 16..26 black 1-11 */
EMU_EXPORT(emu_key) void emu_key(unsigned logical, int down)
{
    uint32_t m;
    if (logical >= 27u) return;
    m = 1u << bb_nav_phys[logical];
    if (down) { fm1_in.notes |= m; fm1_in.notes_pressed |= m; }
    else fm1_in.notes &= ~m;
}
const uint16_t *host_panel_fb(void);
EMU_EXPORT(emu_fb) const uint16_t *emu_fb(void) { return host_panel_fb(); }
EMU_EXPORT(emu_audio_ptr) const float *emu_audio_ptr(void) { return emu_out; }
EMU_EXPORT(emu_audio_take) unsigned emu_audio_take(void) { unsigned n = emu_out_n / 2u; emu_out_n = 0; return n; }
EMU_EXPORT(emu_running) unsigned emu_running(void) { return bb_face.requested.running; }
#if BB_WITH_FX_ANCHOR
EMU_EXPORT(emu_fx_setting) unsigned emu_fx_setting(unsigned parameter)
{ return bb_fx_parameter_get(&bb_sound.core, parameter); }
#endif
EMU_EXPORT(emu_led_ptr) const uint8_t *emu_led_ptr(void) { return fm1_led; }

/* Semantic LED intent, not a simulation of electrical PWM brightness.
 * 0..13 follow emu_button; 14..40 follow emu_key's logical white/black order.
 * Flags: full=1, dim=2, mid=4, breath=8. Read-only, including PLAY green. */
EMU_EXPORT(emu_led_state) unsigned emu_led_state(unsigned logical)
{
    if (logical >= 41u) return 0u;
    unsigned id = logical < 14u ? panel.btn[logical] : 14u + bb_nav_phys[logical - 14u];
    unsigned state = logical == 10u && (fm1_led[8] & 2u) ? 1u : 0u;
    for (unsigned col = 0; col < sizeof fm1_led; col++)
        for (unsigned row = 1; row < 5u; row++)
            if (FM1_KEYMAP[row][col] == (int)id) {
                unsigned bit = 1u << row;
                state |= (fm1_led[col] & bit ? 1u : 0u)
                       | (fm1_led_dim[col] & bit ? 2u : 0u)
                       | (fm1_led_mid[col] & bit ? 4u : 0u)
                       | (fm1_led_breath[col] & bit ? 8u : 0u);
            }
    return state;
}

/* Host-only observation exports; no replacement engine, route or catalogue. */
EMU_EXPORT(emu_catalogue_count) unsigned emu_catalogue_count(unsigned group,unsigned parts) {
 bb_browser b; if(!bb_browser_init(&b,&bb_factory_catalogue)||!bb_browser_group(&b,group))return 0;
 unsigned n=bb_browser_bank_count(&b);if(!parts)return n;unsigned sum=0;
 for(unsigned k=0;k<n;k++){b.bank_index=k;b.part_index=0;sum+=bb_browser_part_count(&b);}return sum;
}
EMU_EXPORT(emu_patch_name) const char *emu_patch_name(unsigned i){return i<9?bb_dillo_bank[i].name:0;}
EMU_EXPORT(emu_kit_name) const char *emu_kit_name(unsigned i){return i==0?"808":i<=bb_sample_musical_count?bb_sample_store.names[i-1]:0;}
EMU_EXPORT(emu_selected_kit) unsigned emu_selected_kit(unsigned layer){return bb_audio_sample_layer_id(&bb_sound,layer);}
EMU_EXPORT(emu_kit_cursor) unsigned emu_kit_cursor(void){return bb_sound_pick.cursor;}
EMU_EXPORT(emu_kit_page) unsigned emu_kit_page(void){return bb_kit_page;}
EMU_EXPORT(emu_bank_name) const char *emu_bank_name(unsigned group,unsigned index){
 static char name[160];bb_browser b;if(!bb_browser_init(&b,&bb_factory_catalogue)||!bb_browser_group(&b,group)||index>=bb_browser_bank_count(&b))return 0;
 b.bank_page=index/16;return bb_browser_bank_title(&b,index%16,name,sizeof name)?name:0;
}
EMU_EXPORT(emu_bank_parts) unsigned emu_bank_parts(unsigned group,unsigned index){
 bb_browser b;if(!bb_browser_init(&b,&bb_factory_catalogue)||!bb_browser_group(&b,group)||index>=bb_browser_bank_count(&b))return 0;
 b.bank_index=index;b.part_index=0;return bb_browser_part_count(&b);
}
EMU_EXPORT(emu_channel_state) unsigned emu_channel_state(void){return (unsigned)bb_sound.core.drums[1].enabled|((unsigned)bb_sound.core.drums[1].muted<<1)|((unsigned)bb_sound.core.synths[0].muted<<2);}

/* Browser mixer uses the same group controls as the panel. A discrete
 * click has an explicit deck, unlike the guarded physical encoder. */
static int emu_drum_mix_deck(unsigned deck)
{
    return deck < 2u && bb_face.machine == deck && bb_nav_top_mode == BB_TOP_BIG_BEAT &&
           bb_nav.page == BB_NAV_PLAY && bb_nav.landing && !bb_nav.help;
}
EMU_EXPORT(emu_ui_drum_mix) unsigned emu_ui_drum_mix(unsigned deck, unsigned group)
{
    if (!emu_drum_mix_deck(deck) || group >= GG_COUNT) return 0;
    bb_group_view view;
    if (bb_group_input(&bb_sound.commands, bb_sound.groups, deck, BB_GROUP_READ, 0, 0, &view) == BB_GROUP_REFUSED)
        return 0;
    unsigned bit = 1u << group;
    unsigned solo = bb_nav_gsolo[deck];
    return 0x8000u | view.level[group] | ((view.muted & bit) ? 128u : 0u) |
           ((solo & bit) ? 256u : 0u) | ((solo && !(solo & bit)) ? 512u : 0u);
}
static int emu_drum_controls_idle(void)
{
    if (bb_project_owner_song_pending() || bb_asset_hold_request || bb_session_hold_request || bb_kit_hold_request ||
        bb_session_busy() || bb_count_busy() ||
        fm1_in.buttons || fm1_in.pressed || fm1_in.released || fm1_in.notes || fm1_in.notes_pressed) return 0;
    for (unsigned e = 0; e < NE; e++) if (fm1_in.enc_steps[panel.enc[e]]) return 0;
    return 1;
}
EMU_EXPORT(emu_ui_drum_mix_press) unsigned emu_ui_drum_mix_press(unsigned deck, unsigned group, int delta)
{
    if (!emu_ui_drum_mix(deck, group) || !emu_drum_controls_idle() ||
        (delta != 0 && delta != 8 && delta != -8)) return 0;
    if (delta) {
        bb_nav_knob(BB_NAV_R_GROUP_KICK + group, group, delta);
        return bb_face.rejected == 0;
    }
    if (bb_nav_gsolo[deck]) return 0;
    emu_key(16u + group, 1);
    emu_key(16u + group, 0);
    return 1;
}

/* Return the current editing deck and the first blocker in the panel's
 * silence order. Browser-only output volume is handled by the player. */
enum { EMU_DRUM_OFF=1, EMU_DRUM_MUTED, EMU_DRUM_ZERO, EMU_DRUM_OTHER_SOLO };
EMU_EXPORT(emu_ui_drum_recovery_state) unsigned emu_ui_drum_recovery_state(void)
{
    unsigned deck = bb_nav_row;
    if (deck >= 2u || bb_face.machine != deck || bb_nav_top_mode != BB_TOP_BIG_BEAT ||
        bb_nav.page != BB_NAV_PLAY || bb_nav.help ||
        (!bb_nav.landing && bb_nav.play_keys != BB_NAV_PLAY_STEPS)) return 0;
    const bb_machine *kit = &bb_face.requested.drums[deck];
    unsigned reason = bb_nav_isolo && !(bb_nav_isolo & (1u << deck))
        ? EMU_DRUM_OTHER_SOLO + (unsigned)__builtin_ctz(bb_nav_isolo)
        : !kit->enabled ? EMU_DRUM_OFF : kit->muted ? EMU_DRUM_MUTED : !kit->level ? EMU_DRUM_ZERO : 0u;
    return ((deck + 1u) << 8) | reason;
}
EMU_EXPORT(emu_ui_drum_recover) unsigned emu_ui_drum_recover(unsigned state)
{
    if (!state || state != emu_ui_drum_recovery_state() || !emu_drum_controls_idle()) return 0;
    unsigned deck = (state >> 8) - 1u;
    switch (state & 255u) {
    case EMU_DRUM_OFF: return bb_send_machine(deck, BB_CMD_DRUM_ENABLE, 1);
    case EMU_DRUM_MUTED: return bb_send_machine(deck, BB_CMD_DRUM_MUTE, 0);
    case EMU_DRUM_ZERO: return bb_send_machine(deck, BB_CMD_DRUM_LEVEL, 32);
    case EMU_DRUM_OTHER_SOLO: case EMU_DRUM_OTHER_SOLO + 1u: case EMU_DRUM_OTHER_SOLO + 2u:
        bb_nav_instr_solo((state & 255u) - EMU_DRUM_OTHER_SOLO);
        return !bb_nav_isolo;
    default: return 0;
    }
}

/* Browser shortcut for the existing layer command, scoped to Drum B's pads. */
EMU_EXPORT(emu_enable_drum_b) unsigned emu_enable_drum_b(void)
{
    if(bb_nav_top_mode!=BB_TOP_BIG_BEAT||bb_nav.page!=BB_NAV_PLAY||!bb_nav.landing||
       bb_nav_row!=1u||bb_nav.help||bb_project_owner_song_pending()||
       bb_asset_hold_request||bb_session_hold_request||bb_kit_hold_request)return 0;
    return bb_face.requested.drums[1].enabled||bb_send_machine(1,BB_CMD_DRUM_ENABLE,1);
}

/* Read-only private test observations of the real arranger owner. */
EMU_EXPORT(emu_arr_revision) unsigned emu_arr_revision(void){return bb_arr_workspace.model.revision;}
EMU_EXPORT(emu_arr_cursor) unsigned emu_arr_cursor(void){return bb_arr_workspace.model.cursor;}
EMU_EXPORT(emu_arr_history) unsigned emu_arr_history(void){return bb_arr_workspace.model.history_count;}
EMU_EXPORT(emu_arr_repeats) unsigned emu_arr_repeats(void){return bb_arr_workspace.model.row_count?bb_arr_workspace.model.rows[bb_arr_workspace.controller.selected].repeats:0;}
EMU_EXPORT(emu_arr_duration) unsigned emu_arr_duration(void){const bb_arr_section *s=bb_arr_workspace.model.row_count?bb_arr_find(&bb_arr_workspace.model,bb_arr_workspace.model.rows[bb_arr_workspace.controller.selected].section):0;return s?s->duration:0;}

/* Private capture tests: use actual owner APIs and inspect copied events. */
static bb_capture_clock emu_capture_clock_value;
static bb_capture_event emu_capture_event_value;
EMU_EXPORT(emu_capture_begin) unsigned emu_capture_begin(unsigned m){return bb_audio_capture_begin(&bb_sound,m,&emu_capture_clock_value);}
EMU_EXPORT(emu_capture_read) unsigned emu_capture_read(void){return bb_audio_capture_read(&bb_sound,&emu_capture_event_value);}
EMU_EXPORT(emu_capture_end) unsigned emu_capture_end(void){return bb_audio_capture_end(&bb_sound);}
EMU_EXPORT(emu_capture_discard) unsigned emu_capture_discard(void){return bb_audio_capture_discard(&bb_sound);}
EMU_EXPORT(emu_capture_voice) unsigned emu_capture_voice(void){return emu_capture_event_value.voice;}
EMU_EXPORT(emu_capture_velocity) unsigned emu_capture_velocity(void){return emu_capture_event_value.velocity;}
EMU_EXPORT(emu_capture_target) unsigned emu_capture_target(void){return emu_capture_event_value.target;}
EMU_EXPORT(emu_capture_kit) unsigned emu_capture_kit(void){return emu_capture_event_value.kit;}
EMU_EXPORT(emu_capture_frame) unsigned emu_capture_frame(void){return (unsigned)emu_capture_event_value.frame;}
EMU_EXPORT(emu_capture_origin) unsigned emu_capture_origin(void){return (unsigned)emu_capture_clock_value.frame;}

/* Read-only recording/working-part test observations. */
EMU_EXPORT(emu_record_phase) unsigned emu_record_phase(void){return bb_record.phase;}
EMU_EXPORT(emu_record_hits) unsigned emu_record_hits(void){return bb_record.take.take.count-bb_record.take.before.count;}
EMU_EXPORT(emu_record_error) const char *emu_record_error(void){return bb_record.error;}
EMU_EXPORT(emu_record_target) unsigned emu_record_target(void){return bb_record.target;}
EMU_EXPORT(emu_record_gate) unsigned emu_record_gate(void){return (unsigned)bb_record.gate;}
EMU_EXPORT(emu_current_frame) unsigned emu_current_frame(void){return (unsigned)bb_sound.core.frame;}
EMU_EXPORT(emu_work_count) unsigned emu_work_count(void){return bb_arr_workspace.sources.work_count;}
EMU_EXPORT(emu_row_work) unsigned emu_row_work(unsigned row,unsigned target){const bb_arr_section *s=row<bb_arr_workspace.model.row_count?bb_arr_find(&bb_arr_workspace.model,bb_arr_workspace.model.rows[row].section):0;return s&&target<2&&s->part[target].source==BB_ARR_WORKING?s->part[target].id:0;}

EMU_EXPORT(emu_record_clear_undo) unsigned emu_record_clear_undo(void){return bb_record.clear_undo;}

/* Read-only rich-phrase observations for emulator journeys. They expose the
 * acknowledged owner state, not bb_pe's draft, so a queued TAKE cannot be
 * mistaken for an applied one. ~0u means no valid rich phrase. */
EMU_EXPORT(emu_phrase_meta) unsigned emu_phrase_meta(unsigned m){
 bb_phrase_view v={0};
 if(m>=2u||!bb_audio_phrase_view_read(&bb_sound,m,&v)||!v.valid)return ~0u;
 return (unsigned)v.phrase.count|((unsigned)v.phrase.length<<8)|((unsigned)v.phrase.lattice<<16);
}
EMU_EXPORT(emu_phrase_event) unsigned emu_phrase_event(unsigned m,unsigned i){
 bb_phrase_view v={0};
 if(m>=2u||!bb_audio_phrase_view_read(&bb_sound,m,&v)||!v.valid||i>=v.phrase.count)return ~0u;
 return v.phrase.events[i];
}
EMU_EXPORT(emu_phrase_edits) unsigned emu_phrase_edits(unsigned m){
 bb_phrase_view v={0};return m<2u&&bb_audio_phrase_view_read(&bb_sound,m,&v)&&v.valid?v.edits:~0u;
}
EMU_EXPORT(emu_phrase_loop) unsigned emu_phrase_loop(unsigned m){return bb_audio_retro_loop_serial(&bb_sound,m);}
EMU_EXPORT(emu_step_condition) unsigned emu_step_condition(unsigned m,unsigned voice,unsigned step){
#if BB_WITH_STEP_CONDITIONS
 bb_phrase_view v={0};unsigned condition=0;
 int state=m<2u&&bb_audio_phrase_view_read(&bb_sound,m,&v)&&v.valid?
     bb_phrase_view_condition(&v,voice,step,&condition):0;
 return state==1?condition:state==2?BB_PC_COUNT:~0u;
#else
 (void)m;(void)voice;(void)step;return ~0u;
#endif
}
EMU_EXPORT(emu_phrase_fill) unsigned emu_phrase_fill(unsigned m){
#if BB_WITH_STEP_CONDITIONS
 unsigned fill=0;return bb_audio_phrase_fill_read(&bb_sound,m,&fill)?fill:~0u;
#else
 (void)m;return ~0u;
#endif
}

/* Read-only generator observations for the real-control emulator journey. */
EMU_EXPORT(emu_gen_mode) unsigned emu_gen_mode(void){return bb_gen_state.mode;}
EMU_EXPORT(emu_gen_value) unsigned emu_gen_value(unsigned k){
 static const uint8_t *const value[]={&bb_gen_state.density,&bb_gen_state.accent,&bb_gen_state.anchor,&bb_gen_state.slide};
 return k<4u?*value[k]:k==4u?bb_gen_state.seed:~0u;
}
EMU_EXPORT(emu_dillo_applied) unsigned emu_dillo_applied(void){return bb_sound.patterns.status[0].applied;}
EMU_EXPORT(emu_dillo_meta) unsigned emu_dillo_meta(void){return bb_sound.core.synths[0].length;}
EMU_EXPORT(emu_dillo_note) unsigned emu_dillo_note(unsigned i){
 if(i>=64u)return ~0u; /* emu-release.c is appended after preprocessing */
 const bb_note *n=&bb_sound.core.synths[0].notes[i];
 return n->note|((unsigned)n->gate<<8)|((unsigned)n->accent<<9)|((unsigned)n->slide<<10);
}

EMU_EXPORT(emu_dillo_expression) unsigned emu_dillo_expression(unsigned i){
 return i<64u?bb_sound.patterns.expression[0][i]:~0u;
}

/* Private browser file bridge. Whole-file host buffer is WASM-only and is NOT
 * a target allocation. All project/music operations use the common C owner. */
static uint8_t emu_project_wire[32768];
static unsigned emu_project_size,emu_project_error_value;
static int emu_project_write(void *ctx,size_t at,const void *data,size_t n){
 (void)ctx;if(at>sizeof emu_project_wire||n>sizeof emu_project_wire-at)return 0;
 memcpy(emu_project_wire+at,data,n);return 1;
}
static int emu_project_read(void *ctx,size_t at,void *data,size_t n){
 (void)ctx;if(at>emu_project_size||n>emu_project_size-at)return 0;
 memcpy(data,emu_project_wire+at,n);return 1;
}
EMU_EXPORT(emu_project_ptr) void *emu_project_ptr(void){return emu_project_wire;}
EMU_EXPORT(emu_project_capacity) unsigned emu_project_capacity(void){return sizeof emu_project_wire;}
EMU_EXPORT(emu_project_can_begin) unsigned emu_project_can_begin(void){
 return !bb_project_owner.phase&&!bb_project_owner.song_action&&!bb_asset_hold_request&&!bb_session_hold_request&&
  !bb_kit_hold_request&&bb_project_owner_conditions();
}
EMU_EXPORT(emu_project_begin) unsigned emu_project_begin(void){return bb_project_owner_acquire();}
EMU_EXPORT(emu_project_state) unsigned emu_project_state(void){
 if(bb_project_owner.song_action)return BB_PROJECT_OWNER_BUSY;
 return bb_project_owner.phase==2?0:bb_project_owner.phase==3?BB_PROJECT_OWNER_HELD:
 bb_project_owner.phase==1?BB_PROJECT_OWNER_PENDING:bb_project_owner.error?bb_project_owner.error:BB_PROJECT_OWNER_BUSY;
}
EMU_EXPORT(emu_project_cancel) void emu_project_cancel(void){if(!bb_project_owner.song_action)bb_project_owner_release();}
EMU_EXPORT(emu_project_error) unsigned emu_project_error(void){return emu_project_error_value;}
EMU_EXPORT(emu_project_export) unsigned emu_project_export(void){
 if(bb_project_owner.song_action){emu_project_error_value=BB_PROJECT_OWNER_BUSY;return 0;}
 size_t n=0,written=0;emu_project_size=0;
 emu_project_error_value=bb_project_owner_snapshot(&n);
 if(!emu_project_error_value)emu_project_error_value=bb_project_write_stream(&bb_arr_workspace.model,&bb_arr_workspace.sources,
  bb_project_owner_assets(),bb_session_memory.wire,n,bb_project_owner_check,0,emu_project_write,0,&written);
 if(!emu_project_error_value)emu_project_size=(unsigned)written;
 bb_project_owner_release();return emu_project_size;
}
EMU_EXPORT(emu_project_import) unsigned emu_project_import(unsigned size){
 if(bb_project_owner.song_action)return BB_PROJECT_OWNER_BUSY;
 if(size>sizeof emu_project_wire){bb_project_owner_release();return BB_PROJECT_CAPACITY;}
 emu_project_size=size;emu_project_error_value=bb_project_owner_stage(emu_project_read,0,size);
 if(!emu_project_error_value)emu_project_error_value=bb_project_owner_commit();
 if(emu_project_error_value!=BB_PROJECT_OWNER_HELD)bb_project_owner_release();return emu_project_error_value;
}
EMU_EXPORT(emu_arr_dirty) unsigned emu_arr_dirty(void){return bb_arr_dirty(&bb_arr_workspace.model);}
EMU_EXPORT(emu_arr_rows) unsigned emu_arr_rows(void){return bb_arr_workspace.model.row_count;}
EMU_EXPORT(emu_project_phase) unsigned emu_project_phase(void){return bb_project_owner.phase;}

/* Read-only project-state observations; no new audio/UI command path. */
EMU_EXPORT(emu_project_dirty) unsigned emu_project_dirty(void){return bb_project_owner_dirty();}
EMU_EXPORT(emu_project_has_file) unsigned emu_project_has_file(void){return bb_project_has_file;}
EMU_EXPORT(emu_project_epoch) unsigned emu_project_epoch(void){return bb_sound.commands.project_edits;}
EMU_EXPORT(emu_project_arrangement_revision) unsigned emu_project_arrangement_revision(void){return bb_arr_workspace.model.revision;}

EMU_EXPORT(emu_focus) unsigned emu_focus(void){return bb_face.machine;}
/* Read-only navigation observations for browser journeys.  These expose the
 * real firmware state after input; they do not replace routing or mutate it. */
EMU_EXPORT(emu_nav_page) unsigned emu_nav_page(void){return bb_nav.page;}
EMU_EXPORT(emu_nav_bank) unsigned emu_nav_bank(void){return bb_nav.bank;}
EMU_EXPORT(emu_nav_landing) unsigned emu_nav_landing(void){return bb_nav.landing;}
EMU_EXPORT(emu_top_mode) unsigned emu_top_mode(void){return bb_nav_top_mode;}
EMU_EXPORT(emu_conduct_scope) unsigned emu_conduct_scope(void){return bb_nav.conduct_scope;}

/* Panel labels use the same bank and valid controls that the LCD just drew. */
EMU_EXPORT(emu_ui_step_bank) unsigned emu_ui_step_bank(void)
{
#if (!defined(BB_UI_V3) || BB_UI_V3) && (!defined(BB_UI_PLAY) || BB_UI_PLAY)
    if (bb_nav.page != BB_NAV_PLAY || bb_nav.play_keys != BB_NAV_PLAY_STEPS || !bb_v3_in.step_length) return 0;
    return (unsigned)bb_v3_in.bank | (unsigned)bb_v3_in.step_length << 8 |
           (unsigned)!bb_nav_is_drum() << 16;
#else
    return 0;
#endif
}
/* Stable browser wording for the same owner transaction the LCD reports.
 * 0 = no edit status, 1 = accepted but not yet sounding, 2 = an edit from
 * this session is in the acknowledged playing pattern.  Observation only. */
EMU_EXPORT(emu_ui_step_edit_state) unsigned emu_ui_step_edit_state(void)
{
#if BB_WITH_RICH_DRUM_BROWSER && BB_WITH_RICH_DRUM_RENDER && BB_WITH_EVOLVE
    unsigned bank = emu_ui_step_bank(), m = bb_face.machine;
    if (!bank || (bank >> 16) || m >= BB_RICH_DRUMS) return 0;
    if (bb_pe.pending[m]) return 1;
#if BB_WITH_PHRASE_NUDGE
    if (bb_pe.nudge_ticket && bb_pe.scratch.nudge_tx.machine == m) return 1;
#endif
    for (unsigned i = 0; i < bb_pe.native_input_count; i++)
        if (bb_pe.native_inputs[i].machine == m) return 1;
    if (bb_pe.count.ticket) {
        bb_section_ratchet status;
        if (bb_audio_section_ratchet_read(&bb_sound, &status) &&
            status.ticket == bb_pe.count.ticket && status.target == m &&
            status.phase == BB_SECTION_EDIT_QUEUED) return 1;
    }
    return bb_pe_entries(&bb_pe.edit, m) ? 2u : 0u;
#else
    return 0;
#endif
}
EMU_EXPORT(emu_ui_step_pitch) unsigned emu_ui_step_pitch(unsigned key)
{
#if (!defined(BB_UI_V3) || BB_UI_V3) && (!defined(BB_UI_PLAY) || BB_UI_PLAY)
    if (key >= 16 && key < sizeof bb_v3_in.key_led && (emu_ui_step_bank() >> 16) &&
        bb_v3_in.key_led[key] != BB_UI_LED_DARK) return 1u + bb_pl.key_note[key];
#else
    (void)key;
#endif
    return 0;
}

/* The browser can select any voice from the exact source order used by
 * B1-B8 and REC+PRESETS. A folded rich lane is not its source identity. */
EMU_EXPORT(emu_ui_step_voice_context) unsigned emu_ui_step_voice_context(void)
{
    unsigned bank = emu_ui_step_bank();
    if (!bank || (bank >> 16) || bb_nav.help || bb_face.machine >= 2u) return 0;
#if BB_WITH_RICH_DRUM_BROWSER
    if (bb_rich_active()) return ((unsigned)(sizeof bb_rich_sources) << 8) | (bb_face.machine + 1u) | 4u;
#endif
    return ((unsigned)(sizeof bb_face.requested.drums[0].sound.mode) << 8) | (bb_face.machine + 1u) |
           (bb_nav.instrument == BB_NAV_INSTR_DRUM_909 ? 8u : 0u);
}
EMU_EXPORT(emu_ui_step_voice_selected) unsigned emu_ui_step_voice_selected(void)
{
    unsigned context = emu_ui_step_voice_context();
    if (!context) return 0;
#if BB_WITH_RICH_DRUM_BROWSER
    if (context & 4u) return bb_rich_voices[bb_face.machine];
#endif
    return bb_face.voice;
}
EMU_EXPORT(emu_ui_step_voice_name) const char *emu_ui_step_voice_name(unsigned voice)
{
    unsigned context = emu_ui_step_voice_context();
    if (voice >= (context >> 8)) return 0;
#if BB_WITH_RICH_DRUM_BROWSER
    if (context & 4u) return bb_rich_names[voice];
#endif
    return context & 8u ? bb909_sequence_voice_name(voice) :
        bb_drum_voice_name(&bb_face.requested.drums[bb_face.machine].sound, voice);
}
EMU_EXPORT(emu_ui_step_voice_choose) unsigned emu_ui_step_voice_choose(unsigned context, unsigned voice)
{
    if (context != emu_ui_step_voice_context() || voice >= (context >> 8) ||
        bb_nav.step_hold || !emu_drum_controls_idle()) return 0;
#if BB_WITH_RICH_DRUM_BROWSER
    if (context & 4u) { bb_rich_select(voice); return 1; }
#endif
    bb_face.voice = (uint8_t)voice;
    bb_dirty = 1;
    return 1;
}

extern int bb_audio_browser_audition(bb_audio *,unsigned,unsigned,int);
EMU_EXPORT(emu_ui_step_voice_audition) unsigned emu_ui_step_voice_audition(unsigned context,unsigned voice)
{
    if (!context || context != emu_ui_step_voice_context() || voice >= (context >> 8) ||
        voice != emu_ui_step_voice_selected() || bb_nav.step_hold || bb_record.phase ||
        (emu_ui_drum_recovery_state() & 255u) || !emu_drum_controls_idle()) return 0;
#if BB_WITH_RICH_DRUM_BROWSER
    if (context & 4u) return bb_audio_browser_audition(&bb_sound,bb_face.machine,bb_rich_sources[voice],1);
#endif
    return bb_audio_browser_audition(&bb_sound,bb_face.machine,voice,0);
}

EMU_EXPORT(emu_ui_hint) const char *emu_ui_hint(void){return bb_hint;}
EMU_EXPORT(emu_pattern_result) unsigned emu_pattern_result(unsigned m){return m<2?(unsigned)bb_sound.rich.status[m].result|((unsigned)bb_sound.rich.error[m]<<8)|((unsigned)bb_sound.core.drums[m].enabled<<16):0;}
EMU_EXPORT(emu_library_error) unsigned emu_library_error(unsigned m){return m<2?bb_drum_library[m].error:0;}

EMU_EXPORT(emu_focus_row) unsigned emu_focus_row(void){return bb_nav_row;}
/* Exact rich/native source selected by the real STEPS navigation.  The
 * visible lane is not an owner identity: rich patterns can fold several
 * original sources onto one row. */
EMU_EXPORT(emu_step_source) unsigned emu_step_source(void){return bb_ratchet_voice();}

EMU_EXPORT(emu_aux_capabilities) unsigned emu_aux_capabilities(void){return bb_audio_aux_capabilities(&bb_sound);}

/* Read-only UI-label observations, distinct from stored pack metadata. */
EMU_EXPORT(emu_ui_loaded_kit_name) const char *emu_ui_loaded_kit_name(unsigned m){return m<2?bb_ui_kit_name(m):0;}
EMU_EXPORT(emu_ui_picker_kit_name) const char *emu_ui_picker_kit_name(unsigned i){return bb_sound_picker_item_name(&bb_sound_pick,i);}
EMU_EXPORT(emu_ui_drum_pad_name) const char *emu_ui_drum_pad_name(unsigned pad)
{
#if !defined(BB_UI_V3) || BB_UI_V3
    /* The labels come from the most recent LCD frame. */
    if (bb_nav_top_mode == BB_TOP_BIG_BEAT && bb_nav.landing &&
        pad < sizeof bb_pl.pad_name / sizeof bb_pl.pad_name[0])
        return bb_pl.pad_name[pad];
#endif
    return 0;
}
static int emu_conductor_visible(void)
{
    return bb_nav_top_mode == BB_TOP_CONDUCTOR && bb_nav.page == BB_NAV_STUDIO &&
        bb_nav.bank == BB_NAV_HOME_CONDUCT && bb_v3_in.page == BB_UI_STUDIO && !bb_v3_in.welcome;
}

EMU_EXPORT(emu_ui_conductor_song_row) unsigned emu_ui_conductor_song_row(void)
{
    return emu_conductor_visible() ? bb_v3_song_row : 0;
}
EMU_EXPORT(emu_ui_section_label_set) uint32_t emu_ui_section_label_set(void)
{
#if !defined(BB_UI_V3) || BB_UI_V3
    if (emu_conductor_visible() && bb_v3_in.tile[0])
        return 1u + ((uint32_t)bb_v3_grid_page << 24 |
                     (uint32_t)bb_v3_grid_group << 16 | bb_v3_grid_bank);
#endif
    return 0;
}
EMU_EXPORT(emu_ui_section_label) const char *emu_ui_section_label(unsigned key)
{
#if !defined(BB_UI_V3) || BB_UI_V3
    if (key < 16u && emu_ui_section_label_set() && bb_v3_in.tile[key])
        return pf_section_short(bb_v3_in.tile[key]);
#endif
    return 0;
}
EMU_EXPORT(emu_ui_perform_status_width) unsigned emu_ui_perform_status_width(void){
    return emu_ui_section_label_set()?(unsigned)text_w(&AF_S,bb_v3_home_status):0;
}
EMU_EXPORT(emu_ui_perform_status) const char *emu_ui_perform_status(void)
{
#if !defined(BB_UI_V3) || BB_UI_V3
    if (emu_ui_section_label_set()) return bb_v3_home_status;
#endif
    return 0;
}
EMU_EXPORT(emu_ui_combo_players) unsigned emu_ui_combo_players(void)
{
#if !defined(BB_UI_V3) || BB_UI_V3
    if (emu_ui_section_label_set()) return bb_v3_in.perf_combo_playing;
#endif
    return 0;
}
EMU_EXPORT(emu_ui_chooser_label) const char *emu_ui_chooser_label(void)
{
#if !defined(BB_UI_V3) || BB_UI_V3
    if (bb_nav.page == BB_NAV_STUDIO && bb_nav.bank == BB_NAV_HOME_SONGS &&
        bb_v3_in.page == BB_UI_PATTERN && bb_v3_in.cursor < bb_v3_in.items)
        return bb_v3_in.item[bb_v3_in.cursor];
#endif
    return 0;
}
EMU_EXPORT(emu_conductor_playing_source) uint32_t emu_conductor_playing_source(unsigned lane)
{
    if (lane >= 3u) return 0;
    const bb_player_selection *selection = bb_cd_applied(bb_cd_player(lane),lane);
    if(!selection)return 0;
    const bb_factory_choice *choice = &selection->choice;
    return 1u + ((uint32_t)choice->group << 24 |
                                    (uint32_t)choice->bank_index << 8 | choice->part_index);
}
EMU_EXPORT(emu_conductor_playing_ticket) uint32_t emu_conductor_playing_ticket(unsigned lane)
{
    if (lane >= 3u) return 0;
    const bb_player_selection *selection = bb_cd_applied(bb_cd_player(lane),lane);
    return selection ? selection->ticket : 0;
}

EMU_EXPORT(emu_ui_conductor_lane_part) const char *emu_ui_conductor_lane_part(unsigned lane)
{
#if !defined(BB_UI_V3) || BB_UI_V3
    if(lane<3u&&(emu_ui_section_label_set() || emu_ui_conductor_song_row()))return bb_v3_in.lane[lane].part;
#endif
    return 0;
}

EMU_EXPORT(emu_ui_conductor_song) const char *emu_ui_conductor_song(unsigned lane)
{
#if !defined(BB_UI_V3) || BB_UI_V3
    if (lane < 3u && (emu_ui_section_label_set() || emu_ui_conductor_song_row())) return bb_v3_in.lane[lane].pattern;
#endif
    return 0;
}

#if !defined(BB_UI_V3) || BB_UI_V3
/* The LCD can fall back to section numbers. The browser has room for all four
 * names and keeps separate caches so a new cue cannot relabel the playing mix. */
typedef struct {
    uint64_t mask;
    bb_catalogue_id bank, lead;
    char text[256];
} emu_conductor_mix;
static emu_conductor_mix emu_playing_mix, emu_queued_mix;

static const char *emu_conductor_mix_names(emu_conductor_mix *mix, const bb_factory_choice *choice,
                                           uint64_t mask, int pending)
{
    if (mix->mask == mask && mix->bank == choice->bank && mix->lead == choice->part) return mix->text;
    mix->mask = mask;
    mix->bank = choice->bank;
    mix->lead = choice->part;
    const unsigned cap = sizeof mix->text;
    unsigned used = pf_append(mix->text, 0, cap, pending ? "Drums: " : "Mix: ");
    bb_catalogue_id parts = bb_catalogue_find(&bb_factory_catalogue, choice->bank, "parts");
    char label[40];
    for (uint64_t set = mask; set; set &= set - 1u) {
        unsigned index = (unsigned)__builtin_ctzll(set);
        if (set != mask) used = pf_append(mix->text, used, cap, " + ");
        bb_v3_conductor_section(bb_catalogue_at(&bb_factory_catalogue, parts, index), label, sizeof label);
        if (!label[0]) label[nav_leg_num(label, index + 1u)] = 0;
        used = pf_append(mix->text, used, cap, pf_section_short(label));
    }
    if (pending) {
        used = pf_append(mix->text, used, cap, " | Dillo: ");
        bb_v3_conductor_section(choice->part, label, sizeof label);
        used = pf_append(mix->text, used, cap, pf_section_short(label));
    }
    mix->text[used] = 0;
    return mix->text;
}
#endif

EMU_EXPORT(emu_ui_conductor_section) const char *emu_ui_conductor_section(unsigned lane)
{
#if !defined(BB_UI_V3) || BB_UI_V3
    if (lane < 3u && (emu_ui_section_label_set() || emu_ui_conductor_song_row())) {
        if (bb_v3_in.perf_combo_playing & (1u << lane))
            return emu_conductor_mix_names(&emu_playing_mix, &emu_conductor_frame.playing,
                                           emu_conductor_frame.playing_mask, 0);
        return bb_v3_in.lane[lane].name;
    }
#endif
    return 0;
}

EMU_EXPORT(emu_ui_conductor_flags) unsigned emu_ui_conductor_flags(unsigned lane)
{
#if !defined(BB_UI_V3) || BB_UI_V3
    if (lane < 3u && (emu_ui_section_label_set() || emu_ui_conductor_song_row())) return bb_v3_in.lane[lane].flags;
#endif
    return 0;
}

EMU_EXPORT(emu_ui_conductor_next) const char *emu_ui_conductor_next(void)
{
#if !defined(BB_UI_V3) || BB_UI_V3
    if ((emu_ui_section_label_set() || emu_ui_conductor_song_row()) && emu_conductor_frame.ticket) {
        if (bb_v3_in.perf_combo_queued)
            return emu_conductor_mix_names(&emu_queued_mix, &emu_conductor_frame.queued,
                                           emu_conductor_frame.queued_mask, 1);
        return bb_v3_in.perf_song ? bb_v3_in.perf_song : bb_v3_in.perf_cue_name;
    }
#endif
    return 0;
}

#if !defined(BB_UI_V3) || BB_UI_V3
static int emu_conductor_can_submit(void)
{
    return bb_nav.landing && !bb_nav.help && !bb_project_owner.phase && !bb_project_owner_song_pending() &&
           !bb_asset_hold_request && !bb_session_hold_request && !bb_kit_hold_request;
}

static uint32_t emu_conductor_bank_key(void)
{
    uint32_t set = emu_ui_section_label_set();
    return set ? ((set - 1u) & 0x00ffffffu) + 1u : 0;
}
static unsigned emu_submit_redirect(unsigned scope, unsigned section)
{
    if (scope < BB_NAV_CONDUCT_A || scope > BB_NAV_CONDUCT_DILLO || !emu_conductor_can_submit()) return 0;
    unsigned saved = bb_nav.conduct_scope;
    uint32_t before = bb_cd.ticket;
    bb_nav.conduct_scope = (uint8_t)scope;
    bb_cd_redirect(section);
    bb_nav.conduct_scope = (uint8_t)saved;
    return bb_cd.ticket && bb_cd.ticket != before ? 1u : 2u;
}
#endif

/* Cancel only the cue represented by the current readout. The section owner
 * applies the same ticket check and queue-full retry as a panel cancellation. */
EMU_EXPORT(emu_ui_conductor_cancel_ticket) uint32_t emu_ui_conductor_cancel_ticket(void)
{
#if !defined(BB_UI_V3) || BB_UI_V3
    if (emu_ui_section_label_set() && emu_conductor_can_submit() &&
        bb_cd.ticket == emu_conductor_frame.ticket &&
        !bb_cd.cancel && !bb_cd.want && !bb_cd.chord.active)
        return bb_cd.ticket;
#endif
    return 0;
}
EMU_EXPORT(emu_conductor_cancel) unsigned emu_conductor_cancel(uint32_t ticket)
{
#if !defined(BB_UI_V3) || BB_UI_V3
    if (ticket && ticket == emu_ui_conductor_cancel_ticket()) {
        bb_cd.cancel = bb_audio_section_cancel_submit(&bb_sound, ticket) ? 1u : 2u;
        return 1;
    }
#endif
    return 0;
}

/* Browser shortcuts report the current SONG page, then inject its normal
 * panel buttons. Recheck the page at click time before accepting a press. */
enum { EMU_SONG_ROWS=1, EMU_SONG_ADD=2, EMU_SONG_READY=4, EMU_SONG_PLAYING=8, EMU_SONG_CHOOSE=16 };
enum { EMU_SONG_PRIMARY, EMU_SONG_BACK, EMU_SONG_PICK };
EMU_EXPORT(emu_ui_song_page) unsigned emu_ui_song_page(void)
{
    if (bb_nav.page != BB_NAV_SONG || bb_nav.help || !bb_arr_ready || bb_record.phase || bb_arr_remove) return 0;
    if (bb_nav.bank == BB_NAV_SONG_ADD) {
        unsigned ready = bb_arr_workspace.model.row_count < BB_ARR_ROWS &&
                         bb_arr_controller_can_edit(&bb_arr_workspace.controller, &bb_sound);
        return EMU_SONG_ADD | (ready ? EMU_SONG_READY : 0);
    }
    unsigned rows = bb_arr_workspace.model.row_count;
    unsigned state = EMU_SONG_ROWS | (rows ? EMU_SONG_READY : 0);
    if (bb_face.requested.running) return state | EMU_SONG_PLAYING;
    return state | (rows < BB_ARR_ROWS ? EMU_SONG_CHOOSE : 0);
}
/* Read the same draft that EDIT would append. Only the browser needs
 * room to spell out all three sources while the LCD edits one target. */
EMU_EXPORT(emu_ui_song_preview) unsigned emu_ui_song_preview(void)
{
    if (!bb_arr_page || !bb_arr_source_page || bb_nav.help || bb_record.phase) return 0;
    return (unsigned)bb_arr_ticks | (unsigned)bb_arr_source_target << 16;
}
EMU_EXPORT(emu_ui_song_preview_part) const char *emu_ui_song_preview_part(unsigned lane)
{
    if (lane >= 3u || !emu_ui_song_preview()) return 0;
    if (lane == 2u && bb_arr_no_dillo) return "No part";
    bb_factory_choice choice;
    if (!bb_browser_current(&bb_arr_workspace.browse[lane], &choice)) return "Source unavailable";
    char bank[96], part[96];
    static char text[224];
    bb_v3_bank_title(&choice, bank, sizeof bank);
    bb_v3_conductor_section(choice.part, part, sizeof part);
    unsigned mask = 1u << lane;
    const char *prefix = bb_arr_mix_lanes & mask ? "Combined from " :
                         bb_arr_live_lanes & mask ? "Working from " : "";
    unsigned used = pf_append(text, 0, sizeof text, prefix);
    used = pf_append(text, used, sizeof text, bank);
    used = pf_append(text, used, sizeof text, " \xB7 ");
    used = pf_append(text, used, sizeof text, part);
    text[used] = 0;
    return text;
}

EMU_EXPORT(emu_ui_song_press) unsigned emu_ui_song_press(unsigned page, unsigned action)
{
    if (!page || page != emu_ui_song_page() || bb_project_owner_song_pending() ||
        bb_asset_hold_request || bb_session_hold_request || bb_kit_hold_request ||
        fm1_in.buttons || fm1_in.pressed || fm1_in.released || fm1_in.notes || fm1_in.notes_pressed) return 0;
    if (action == EMU_SONG_PICK) {
        if (!(page & EMU_SONG_CHOOSE)) return 0;
        emu_key(0, 1); /* White 1 opens the existing source chooser. */
        emu_key(0, 0);
        return 1;
    }
    if (action != EMU_SONG_BACK &&
        (action != EMU_SONG_PRIMARY || !(page & (EMU_SONG_READY | EMU_SONG_PLAYING)))) return 0;
    unsigned button = action == EMU_SONG_BACK ? BB_NAV_B_HOME : page & EMU_SONG_ADD ? BB_NAV_B_EDIT : BB_NAV_B_PLAY;
    emu_button(button, 1);
    emu_button(button, 0);
    return 1;
}

/* Stage the same snapshot as SONG ADD's BAND key. The player still
 * confirms the row with EDIT, so this never changes the arrangement. */
EMU_EXPORT(emu_conductor_stage_song_state) unsigned emu_conductor_stage_song_state(void)
{
#if !defined(BB_UI_V3) || BB_UI_V3
    if (!emu_ui_section_label_set() || !emu_conductor_can_submit()) return 0;
    if (fm1_in.buttons || fm1_in.pressed || fm1_in.released || fm1_in.notes || fm1_in.notes_pressed) return 0;
    return bb_arr_stage_conductor_state();
#else
    return 0;
#endif
}
EMU_EXPORT(emu_conductor_stage_song) unsigned emu_conductor_stage_song(void)
{
#if !defined(BB_UI_V3) || BB_UI_V3
    unsigned state = emu_conductor_stage_song_state();
    if (state != 1u) return state;
    return bb_arr_stage_conductor() ? 1u : 3u;
#else
    return 0;
#endif
}

/* The physical grid still uses page-local keys. The labelled browser form
 * can address the entire bank without changing the visible panel page. */
EMU_EXPORT(emu_conductor_redirect) unsigned emu_conductor_redirect(unsigned scope, unsigned key, uint32_t section_set)
{
#if !defined(BB_UI_V3) || BB_UI_V3
    const unsigned keys = sizeof bb_v3_in.tile / sizeof bb_v3_in.tile[0];
    if (key >= keys || !section_set || section_set != emu_ui_section_label_set() || !bb_v3_in.tile[key]) return 0;
    return emu_submit_redirect(scope, bb_nav_section_page(&bb_nav) * keys + key);
#else
    return 0;
#endif
}

EMU_EXPORT(emu_conductor_bank_redirect) unsigned emu_conductor_bank_redirect(unsigned scope, unsigned section, uint32_t bank_key)
{
#if !defined(BB_UI_V3) || BB_UI_V3
    if (!bank_key || bank_key != emu_conductor_bank_key() || section >= bb_nav_f.section_count) return 0;
    const bb_factory_choice *group = bb_cd_group_ref();
    if (!group || group->bank != bb_v3_grid_bank || group->group != bb_v3_grid_group) return 0;
    return emu_submit_redirect(scope, section);
#else
    return 0;
#endif
}

EMU_EXPORT(emu_ui_conductor_bank_section) const char *emu_ui_conductor_bank_section(unsigned index)
{
#if !defined(BB_UI_V3) || BB_UI_V3
    static char label[40];
    if (!emu_conductor_bank_key() || index >= bb_nav_f.section_count) return 0;
    bb_catalogue_id parts = bb_catalogue_find(&bb_factory_catalogue, bb_v3_grid_bank, "parts");
    bb_v3_conductor_section(bb_catalogue_at(&bb_factory_catalogue, parts, index), label, sizeof label);
    return label;
#else
    return 0;
#endif
}

/* A labelled Queue mix button may submit a new mix only. The repeat-chord
 * cancellation gesture remains on the section keys. */
EMU_EXPORT(emu_conductor_combine) unsigned emu_conductor_combine(uint64_t mask, uint32_t bank_key)
{
#if !defined(BB_UI_V3) || BB_UI_V3
    const unsigned count = bb_nav_f.section_count;
    const unsigned members = (unsigned)__builtin_popcountll(mask);
    if (!bank_key || bank_key != emu_conductor_bank_key() || !emu_conductor_can_submit() ||
        members < 2u || members > 4u || (count < 64u && (mask >> count))) return 0;
    if (bb_cd.ticket || bb_cd.want || bb_cd.chord.active) return 2;
    const bb_factory_choice *group = bb_cd_group_ref();
    if (!group || group->bank != bb_v3_grid_bank) return 0;
    if (bb_cd_group_linked() && mask == bb_cd_cur()) return 3;
    bb_cd_set(mask);
    return bb_cd.ticket ? 1u : 4u;
#else
    return 0;
#endif
}

/* Release identity exports; public label from the explicit release manifest. */
EMU_EXPORT(emu_product_id) const char *emu_product_id(void){return "@PRODUCT@";}
EMU_EXPORT(emu_display_name) const char *emu_display_name(void){return "@DISPLAY_NAME@";}

EMU_EXPORT(emu_arr_add_subtitle) const char *emu_arr_add_subtitle(void){
    return bb_arr_page&&bb_arr_source_page?bb_v3_in.subtitle:0;
}
EMU_EXPORT(emu_prefill_source) unsigned emu_prefill_source(unsigned lane){
 if(lane>=3)return 0;
 const bb_browser *b=&bb_arr_workspace.browse[lane];
 return (b->group<<24)|(b->bank_index<<8)|b->part_index;
}
EMU_EXPORT(emu_prefill_group) unsigned emu_prefill_group(void){
 const bb_factory_choice *c=&bb_cd.group_choice;
 return (c->group<<24)|(c->bank_index<<8)|c->part_index;
}

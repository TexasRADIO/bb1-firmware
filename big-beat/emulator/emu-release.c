/* Big Beat FM-1 web emulator driver. Appended to the real firmware unity build (felucca.c) by host_prep.py,
 * exactly as the review journeys harness is: the firmware's own code runs unchanged; this file only sets the
 * debounced panel state the TIMER5 scan would publish, runs the main loop's UI half, and runs the audio
 * callback (audio_block) to fill a sample buffer. No device code path is replaced except the panel SPI. */
#define EMU_EXPORT(n) __attribute__((export_name(#n)))
static int32_t emu_ab[2u * 32u];
static float emu_out[2u * 4096u];
static unsigned emu_out_n;
static unsigned emu_acc;
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
    bb_target_draw();
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
EMU_EXPORT(emu_project_begin) unsigned emu_project_begin(void){return bb_project_owner_acquire();}
EMU_EXPORT(emu_project_state) unsigned emu_project_state(void){
 return bb_project_owner.phase==2?0:bb_project_owner.phase==3?BB_PROJECT_OWNER_HELD:
 bb_project_owner.phase==1?BB_PROJECT_OWNER_PENDING:bb_project_owner.error?bb_project_owner.error:BB_PROJECT_OWNER_BUSY;
}
EMU_EXPORT(emu_project_cancel) void emu_project_cancel(void){bb_project_owner_release();}
EMU_EXPORT(emu_project_error) unsigned emu_project_error(void){return emu_project_error_value;}
EMU_EXPORT(emu_project_export) unsigned emu_project_export(void){
 size_t n=0,written=0;emu_project_size=0;
 emu_project_error_value=bb_project_owner_snapshot(&n);
 if(!emu_project_error_value)emu_project_error_value=bb_project_write_stream(&bb_arr_workspace.model,&bb_arr_workspace.sources,
  bb_project_owner_assets(),bb_session_memory.wire,n,bb_project_owner_check,0,emu_project_write,0,&written);
 if(!emu_project_error_value)emu_project_size=(unsigned)written;
 bb_project_owner_release();return emu_project_size;
}
EMU_EXPORT(emu_project_import) unsigned emu_project_import(unsigned size){
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

EMU_EXPORT(emu_focus) unsigned emu_focus(void){return bb_face.machine;}

EMU_EXPORT(emu_ui_hint) const char *emu_ui_hint(void){return bb_hint;}
EMU_EXPORT(emu_pattern_result) unsigned emu_pattern_result(unsigned m){return m<2?(unsigned)bb_sound.rich.status[m].result|((unsigned)bb_sound.rich.error[m]<<8)|((unsigned)bb_sound.core.drums[m].enabled<<16):0;}
EMU_EXPORT(emu_library_error) unsigned emu_library_error(unsigned m){return m<2?bb_drum_library[m].error:0;}

EMU_EXPORT(emu_focus_row) unsigned emu_focus_row(void){return bb_nav_row;}

EMU_EXPORT(emu_aux_capabilities) unsigned emu_aux_capabilities(void){return bb_audio_aux_capabilities(&bb_sound);}

/* Read-only UI-label observations, distinct from stored pack metadata. */
EMU_EXPORT(emu_ui_loaded_kit_name) const char *emu_ui_loaded_kit_name(unsigned m){return m<2?bb_ui_kit_name(m):0;}
EMU_EXPORT(emu_ui_picker_kit_name) const char *emu_ui_picker_kit_name(unsigned i){return bb_sound_picker_item_name(&bb_sound_pick,i);}

/* Release identity exports; public label from the explicit release manifest. */
EMU_EXPORT(emu_product_id) const char *emu_product_id(void){return "@PRODUCT@";}
EMU_EXPORT(emu_display_name) const char *emu_display_name(void){return "@DISPLAY_NAME@";}

/* Include within console.c after con_kv/con_hex, before con_exec.
 * Read-only main-loop command: copy a bounded snapshot with IRQs saved, then
 * format AFTER restoring interrupts. Never wait on USB while blocking audio.
 * Fields describe Big Beat, not unused stock track/preset state. */
static void bb_diag_counter(const char *name,uint32_t value) {
    con_puts(name);con_puts(" 0x");con_hex(value,8);con_puts("\r\n");
}
#if BB_SAMPLE_HOST_INTEGRATION
/* Short read-only kit acknowledgement snapshot. Copy only scalars under the
 * existing audio IRQ guard; format over USB after releasing it. In particular,
 * queue acceptance is not reported as a completed or audible kit change. */
static void bb_con_kitdiag(void) {
    struct {
        uint32_t frame,serial_a,failed,faults;
        uint8_t queue,stop_pending,running;
        uint8_t selected_a,requested_a,result_a;
#if defined(BB_SAMPLE_DUAL_TRIAL)
        uint32_t serial_b;
        uint8_t selected_b,requested_b,result_b;
#endif
    } snap;
    uintptr_t token=bb_target_enter(0);
    snap.frame=(uint32_t)bb_sound.core.frame;
    snap.serial_a=bb_sound.sample_select_serial;
#if defined(BB_SAMPLE_DUAL_TRIAL)
    snap.serial_b=bb_sound.sample_b_select_serial;
#endif
    snap.failed=bb_c1_live.failed;
    snap.faults=bb_audio_faults;
    snap.queue=bb_sound.commands.count;
    snap.stop_pending=bb_sound.commands.stop_pending;
    snap.running=bb_sound.core.running;
    snap.selected_a=bb_sound.sample_selected;
    snap.requested_a=bb_sound.sample_requested;
    snap.result_a=bb_sound.sample_select_result;
#if defined(BB_SAMPLE_DUAL_TRIAL)
    snap.selected_b=bb_sound.sample_b_selected;
    snap.requested_b=bb_sound.sample_b_requested;
    snap.result_b=bb_sound.sample_b_select_result;
#endif
    bb_target_leave(0,token);
    bb_diag_counter("kitdiag_version",1u);
    bb_diag_counter("kit_frame_low",snap.frame);
    bb_diag_counter("kit_audio_failed",snap.failed);
    bb_diag_counter("kit_audio_faults",snap.faults);
    bb_diag_counter("kit_queue",snap.queue);
    bb_diag_counter("kit_stop_pending",snap.stop_pending);
    bb_diag_counter("kit_playing",snap.running);
    bb_diag_counter("kit_a_serial",snap.serial_a);
    bb_diag_counter("kit_a_selected",snap.selected_a);
    bb_diag_counter("kit_a_requested",snap.requested_a);
    bb_diag_counter("kit_a_result",snap.result_a);
#if defined(BB_SAMPLE_DUAL_TRIAL)
    bb_diag_counter("kit_b_serial",snap.serial_b);
    bb_diag_counter("kit_b_selected",snap.selected_b);
    bb_diag_counter("kit_b_requested",snap.requested_b);
    bb_diag_counter("kit_b_result",snap.result_b);
#endif
}
#endif
#if BB_OUTPUT_GUARD
/* Format on the main loop, not the IRQ snapshot. -1 means invalid/unknown;
 * ordered bounds reject NaN/infinity before the float-to-integer conversion.
 * Q16 values are truncated, not measurements of analogue/perceived loudness. */
static void bb_diag_q16(const char *name,float value,float low,float high) {
    con_kv(name,value>=low && value<=high?(int32_t)(value*65536.0f):-1);
}
#endif
static void bb_con_diagnostics(void) {
    con_kv("bb_profile_enabled",BB_AUDIO_PROFILE);
#if BB_RICH_BOOT_QUALIFY && BB_WITH_EVOLVE && BB_SNAPSHOT_STARTUP_DIAGNOSTIC
    con_kv("snapshot_probe_version",bb_snapshot_probe.version);
    con_kv("snapshot_probe_status",bb_snapshot_probe.status);
    con_kv("snapshot_probe_polls",bb_snapshot_probe.polls);
    con_kv("snapshot_probe_completed",bb_snapshot_probe.completed);
    con_kv("snapshot_probe_failures",bb_snapshot_probe.failures);
    con_kv("snapshot_probe_restored",bb_snapshot_probe.restored);
    {
        static const char *const tick_names[]={"snapshot_probe_ticks0","snapshot_probe_ticks1","snapshot_probe_ticks2","snapshot_probe_ticks3","snapshot_probe_ticks4"};
        static const char *const guard_names[]={"snapshot_probe_guards0","snapshot_probe_guards1","snapshot_probe_guards2","snapshot_probe_guards3","snapshot_probe_guards4"};
        static const char *const max_names[]={"snapshot_probe_guard_max0","snapshot_probe_guard_max1","snapshot_probe_guard_max2","snapshot_probe_guard_max3","snapshot_probe_guard_max4"};
        for(unsigned i=0;i<BB_SNAPSHOT_PHASES;i++){
            bb_diag_counter(tick_names[i],bb_snapshot_probe.ticks[i]);
            bb_diag_counter(guard_names[i],bb_snapshot_probe.guards[i]);
            bb_diag_counter(max_names[i],bb_snapshot_probe.guard_max[i]);
        }
    }
#endif
#if BB_RICH_BOOT_QUALIFY
    /* Separate immutable report: never present legacy sample timing as rich. */
    con_kv("rich_probe_version",bb_rich_probe.version);
#if BB_QUIET_STAGE_PROBE
    con_kv("stage_probe_version",bb_stage_probe.version);
    con_kv("stage_probe_complete",bb_stage_probe.complete);
    con_kv("stage_probe_errors",bb_stage_probe.errors);
    con_kv("stage_probe_route",bb_stage_probe.route);
    con_kv("stage_probe_block",bb_stage_probe.block);
    con_kv("stage_probe_total",bb_stage_probe.total_ticks);
    for(unsigned s=0;s<BB_STAGE_COUNT;s++){
        char name[]="stage_0_ticks";name[6]=(char)('0'+s);
        con_kv(name,bb_stage_probe.cost[s].ticks);
        char calls[]="stage_0_calls";calls[6]=(char)('0'+s);
        con_kv(calls,bb_stage_probe.cost[s].calls);
        char frames[]="stage_0_frames";frames[6]=(char)('0'+s);
        con_kv(frames,bb_stage_probe.cost[s].frames);
    }
#endif
    con_kv("rich_probe_status",bb_rich_probe.status);
    con_kv("rich_probe_routes",bb_rich_probe.routes);
    con_kv("rich_probe_completed",bb_rich_probe.completed);
    con_kv("rich_probe_blocks",bb_rich_probe.blocks);
    con_kv("rich_probe_min_ticks",bb_rich_probe.min_ticks);
    con_kv("rich_probe_max_ticks",bb_rich_probe.max_ticks);
    con_kv("rich_probe_command_max_ticks",bb_rich_probe.command_max_ticks);
    con_kv("rich_probe_source_commands",bb_rich_probe.source_commands);
    con_kv("rich_probe_source_refreshes",bb_rich_probe.source_refreshes);
    con_kv("rich_probe_source_pending_reads",bb_rich_probe.source_pending_reads);
    con_kv("rich_probe_source_snapshot_max_ticks",bb_rich_probe.source_snapshot_max_ticks);
    con_kv("rich_probe_source_queue_max_ticks",bb_rich_probe.source_queue_max_ticks);
    con_kv("mix_probe_version",bb_rich_probe.mix_version);
    con_kv("ride_probe_version",bb_rich_probe.ride_version);
    con_kv("ride_probe_requests",bb_rich_probe.ride_requests);
    con_kv("ride_probe_pending_reads",bb_rich_probe.ride_pending_reads);
    con_kv("ride_probe_applied",bb_rich_probe.ride_applied);
    con_kv("ride_probe_active_mask",bb_rich_probe.ride_active_mask);
    con_kv("ride_probe_slot_mask",bb_rich_probe.ride_slot_mask);
    con_kv("ride_probe_snapshot_max_ticks",bb_rich_probe.ride_snapshot_max_ticks);
    con_kv("ride_probe_failures",bb_rich_probe.ride_failures);
    con_kv("ride_probe_restored",bb_rich_probe.ride_restored);
    con_kv("mix_probe_commands",bb_rich_probe.mix_commands);
    con_kv("mix_probe_pending_reads",bb_rich_probe.mix_pending_reads);
    con_kv("mix_probe_refreshes",bb_rich_probe.mix_refreshes);
    con_kv("mix_probe_snapshot_max_ticks",bb_rich_probe.mix_snapshot_max_ticks);
    con_kv("mix_probe_queue_max_ticks",bb_rich_probe.mix_queue_max_ticks);
    con_kv("mix_probe_failures",bb_rich_probe.mix_failures);
    con_kv("mix_probe_restored",bb_rich_probe.mix_restored);
    con_kv("rich_probe_hits_a",bb_rich_probe.hits_a);
    con_kv("rich_probe_hits_b",bb_rich_probe.hits_b);
    con_kv("rich_probe_dillo_frames",bb_rich_probe.dillo_frames);
    con_kv("rich_probe_wet_blocks",bb_rich_probe.wet_blocks);
    con_kv("rich_probe_faults",bb_rich_probe.faults);
    con_kv("rich_probe_render_errors",bb_rich_probe.render_errors);
    con_kv("rich_probe_transitions",bb_rich_probe.transitions);
    con_kv("rich_probe_restored",bb_rich_probe.restored);
    con_kv("rich_probe_peak_0",bb_rich_probe.peaks[0]);
    con_kv("rich_probe_peak_1",bb_rich_probe.peaks[1]);
    con_kv("rich_probe_peak_2",bb_rich_probe.peaks[2]);
    con_kv("rich_probe_peak_3",bb_rich_probe.peaks[3]);
    con_kv("rich_probe_peak_4",bb_rich_probe.peaks[4]);
    con_kv("rich_probe_peak_5",bb_rich_probe.peaks[5]);
    con_kv("rich_probe_peak_6",bb_rich_probe.peaks[6]);
    con_kv("rich_probe_sampled_mask",bb_rich_probe.sampled_rich_mask);
    con_kv("rich_probe_sampled_hits",bb_rich_probe.sampled_rich_hits);
    con_kv("rich_probe_sampled_acks",bb_rich_probe.sampled_rich_acks);
    con_kv("rich_trace_version",bb_rich_probe.trace_version);
    con_kv("rich_trace_valid",bb_rich_probe.trace_valid);
    con_kv("rich_trace_route",bb_rich_probe.trace_route);
    con_kv("rich_trace_block",bb_rich_probe.trace_block);
    con_kv("rich_trace_flags",bb_rich_probe.trace_flags);
    bb_diag_counter("rich_trace_source",bb_rich_probe.trace_source);
    bb_diag_counter("rich_trace_frame_low",bb_rich_probe.trace_frame_low);
    bb_diag_counter("rich_trace_frame_high",bb_rich_probe.trace_frame_high);
    con_kv("rich_trace_hits_a",bb_rich_probe.trace_hits_a);
    con_kv("rich_trace_hits_b",bb_rich_probe.trace_hits_b);
#endif
#if BB_SAMPLE_HOST_INTEGRATION
    /* Boot-only report is immutable after audio/USB start. */
    con_kv("sample_probe_version",bb_sample_probe.version);
    con_kv("sample_probe_status",bb_sample_probe.status);
    con_kv("sample_probe_kits",bb_sample_probe.kits);
    con_kv("sample_probe_completed",bb_sample_probe.completed_kits);
    con_kv("sample_probe_blocks",bb_sample_probe.blocks);
    con_kv("sample_probe_max_ticks",bb_sample_probe.max_ticks);
    con_kv("sample_probe_hits",bb_sample_probe.sample_hits);
    con_kv("sample_probe_rejected",bb_sample_probe.sample_rejections);
    con_kv("sample_probe_dillo_frames",bb_sample_probe.dillo_frames);
    con_kv("sample_probe_wet_blocks",bb_sample_probe.wet_blocks);
    con_kv("sample_probe_faults",bb_sample_probe.callback_faults);
    con_kv("sample_probe_restored",bb_sample_probe.restored);
    con_kv("sample_probe_transitions",bb_sample_probe.transitions);
    con_kv("sample_probe_overlap_blocks",bb_sample_probe.overlap_blocks);
#endif
    struct {
        uint32_t ticks,frames_lo,frames_hi,rate,hits[BB_MACHINES],faults;
        unsigned queued;
        uint8_t stop_pending,stop_inflight,commands_idle;
        uint16_t bpm;
        uint8_t running,swing,length[BB_MACHINES],next[BB_MACHINES];
        uint8_t enabled[BB_MACHINES],muted[BB_MACHINES],level[BB_MACHINES];
        uint8_t band_level[BB_MIX_CHANNELS],synth_muted[BB_SYNTHS];
        uint16_t synth_send[BB_SYNTHS][2];
#if BB_OUTPUT_GUARD
        float boost,gain,required,window_peak;
        uint32_t guard_frames,guard_limited,guard_faults;
#endif
#if BB_AUDIO_PROFILE
        bb_profile_report profile;
        bb_profile_window_report windows;
#endif
#ifdef BB_TARGET_TIMING_H
        bb_target_timing_stats timing;
#endif
#ifdef BB_T5_SERVICE_TIMING_H
        bb_t5_service_stats services;
#endif
        bb_p1b_report p1b;
    } snap;
    unsigned i;
    uintptr_t token=bb_target_enter(0);
    snap.ticks=bb_sound.core.ticks;
    snap.frames_lo=(uint32_t)bb_sound.core.frame;
    snap.frames_hi=(uint32_t)(bb_sound.core.frame>>32);
    snap.rate=bb_sound.core.sample_rate;snap.bpm=bb_sound.core.bpm;
    snap.running=bb_sound.core.running;snap.swing=bb_sound.core.active_swing;
#if BB_AUDIO_PROFILE
    snap.profile=bb_profile;
    snap.windows=bb_profile_windows;
#endif
    snap.queued=bb_sound.commands.count;snap.faults=bb_audio_faults;
    snap.stop_pending=bb_sound.commands.stop_pending;
    snap.stop_inflight=bb_sound.commands.stop_inflight;
    snap.commands_idle=(uint8_t)bb_commands_idle(&bb_sound.commands);
#if BB_OUTPUT_GUARD
    snap.boost=bb_sound.output.boost;snap.gain=bb_sound.output.gain;
    snap.required=bb_sound.output.required;snap.window_peak=bb_sound.output.cached_peak;
    snap.guard_frames=bb_sound.output.frame;snap.guard_limited=bb_sound.output.limited;
    snap.guard_faults=bb_sound.output.faults;
#endif
    for(i=0;i<BB_MIX_CHANNELS;i++)snap.band_level[i]=bb_sound.core.band_level[i];
    for(i=0;i<BB_SYNTHS;i++) {
        unsigned base=i?BB_FX_TELEGRAM_ECHO:BB_FX_DILLO_ECHO;
        snap.synth_muted[i]=bb_sound.core.synths[i].muted;
        snap.synth_send[i][0]=bb_sound.core.effects.value[base];
        snap.synth_send[i][1]=bb_sound.core.effects.value[base+1];
    }
#ifdef BB_TARGET_TIMING_H
    snap.timing=bb_target_timing;
#endif
    bb_audio_p1b_report(&snap.p1b);
#ifdef BB_T5_SERVICE_TIMING_H
    snap.services=bb_t5_services;
#endif
    for(i=0;i<BB_MACHINES;i++) {
        const bb_machine *d=&bb_sound.core.drums[i];
        snap.length[i]=d->length;snap.next[i]=d->next;
        snap.enabled[i]=d->enabled;snap.muted[i]=d->muted;snap.level[i]=d->level;
        snap.hits[i]=bb_sound.hits[i];
    }
    bb_target_leave(0,token);
#ifdef BB_T5_SERVICE_TIMING_H
    con_kv("t5_probe_version",1);
    {
        static const char *const calls[]={"t5_scan_calls","t5_usb_calls","t5_uart_calls","t5_uac_calls","t5_outside_calls"};
        static const char *const last[]={"t5_scan_last_ticks","t5_usb_last_ticks","t5_uart_last_ticks","t5_uac_last_ticks","t5_outside_last_ticks"};
        static const char *const max[]={"t5_scan_max_ticks","t5_usb_max_ticks","t5_uart_max_ticks","t5_uac_max_ticks","t5_outside_max_ticks"};
        static const char *const at[]={"t5_scan_max_at_ms","t5_usb_max_at_ms","t5_uart_max_at_ms","t5_uac_max_at_ms","t5_outside_max_at_ms"};
        for(unsigned service=0;service<BB_T5_SERVICE_KINDS;service++){
            bb_diag_counter(calls[service],snap.services.calls[service]);
            bb_diag_counter(last[service],snap.services.last_ticks[service]);
            bb_diag_counter(max[service],snap.services.max_ticks[service]);
            bb_diag_counter(at[service],snap.services.max_at_ms[service]);
        }
    }
#endif
    con_kv("bb_dillo",BB_WITH_DILLO);con_kv("bb_telegram",BB_WITH_TELEGRAM);
    con_kv("bb_mix_drums",snap.band_level[BB_MIX_DRUMS]);
    con_kv("bb_mix_dillo",snap.band_level[BB_MIX_DILLO]);
    con_kv("bb_mix_telegram",snap.band_level[BB_MIX_TELEGRAM]);
    con_kv("bb_dillo_muted",snap.synth_muted[0]);
    con_kv("bb_telegram_muted",snap.synth_muted[1]);
    con_kv("bb_dillo_echo",snap.synth_send[0][0]);
    con_kv("bb_dillo_spring",snap.synth_send[0][1]);
    con_kv("bb_telegram_echo",snap.synth_send[1][0]);
    con_kv("bb_telegram_spring",snap.synth_send[1][1]);
    con_kv("bb_playing",snap.running);con_kv("bb_bpm",snap.bpm);
    con_kv("bb_swing",(int32_t)(bb_swing_x2(snap.swing)/2u));con_kv("bb_swing_x2",(int32_t)bb_swing_x2(snap.swing));con_kv("bb_rate",(int32_t)snap.rate);
    con_kv("bb_queue",(int32_t)snap.queued);
    con_kv("bb_stop_pending",snap.stop_pending);
    con_kv("bb_stop_inflight",snap.stop_inflight);
    con_kv("bb_commands_idle",snap.commands_idle);
    bb_diag_counter("bb_ticks",snap.ticks);
    bb_diag_counter("bb_frames_low",snap.frames_lo);bb_diag_counter("bb_frames_high",snap.frames_hi);
    bb_diag_counter("bb_faults",snap.faults);
#if BB_AUDIO_PROFILE
    con_kv("profile_wall_includes_nested",1);
    bb_diag_counter("profile_blocks",snap.profile.blocks);
    bb_diag_counter("profile_errors",snap.profile.errors);
    /* Stage IDs: Dillo, Telegram, all drums, echo, spring, output guard.
     * Synth/drum stages include their faders/sends. Other control, scheduling,
     * return mixing and conversion work remains in the whole-deadline meter. */
    for(i=0;i<BB_PROF_COUNT;i++) {
        con_kv("profile_stage",(int32_t)i);
        bb_diag_counter("profile_ticks",snap.profile.stage[i].ticks);
        bb_diag_counter("profile_max_ticks",snap.profile.stage[i].max_ticks);
        bb_diag_counter("profile_calls",snap.profile.stage[i].calls);
        bb_diag_counter("profile_frames",snap.profile.stage[i].frames);
    }
    bb_diag_counter("profile_half_completed",snap.windows.completed);
    bb_diag_counter("profile_half_errors",snap.windows.errors);
    for(unsigned w=0;w<2;w++) {
        const bb_profile_window *h=w?&snap.windows.peak:&snap.windows.last;
        con_kv("profile_half",(int32_t)w); /* 0 latest, 1 peak-total correlated interval */
        bb_diag_counter("half_sequence",h->sequence);bb_diag_counter("half_valid",h->valid);
        bb_diag_counter("half_total_ticks",h->total_ticks);bb_diag_counter("half_nested_ticks",h->nested_ticks);
        bb_diag_counter("half_budget_ticks",h->budget_ticks);bb_diag_counter("half_frames",h->frames);
        bb_diag_counter("half_blocks",h->blocks);
        bb_diag_counter("half_worker_wait_ticks",h->worker_wait_ticks);
        bb_diag_counter("half_worker_joins",h->worker_joins);
        bb_diag_counter("half_worker_run_ticks",h->worker_run_ticks);
        bb_diag_counter("half_worker_runs",h->worker_runs);
        for(i=0;i<BB_PROF_COUNT;i++) {
            con_kv("half_stage",(int32_t)i);
            bb_diag_counter("half_stage_ticks",h->stage[i].ticks);
            bb_diag_counter("half_stage_calls",h->stage[i].calls);
            bb_diag_counter("half_stage_frames",h->stage[i].frames);
        }
    }
#endif
#ifdef BB_TARGET_TIMING_H
    /* These share one IRQ-protected snapshot. Maxima may refer to different
     * halves; do not add/subtract the independent maximum values. */
    con_kv("audio_total_us",(int32_t)(snap.timing.last_total_ticks/FM1_TICKS_PER_US));
    con_kv("audio_total_max_us",(int32_t)(snap.timing.max_total_ticks/FM1_TICKS_PER_US));
    con_kv("audio_nested_us",(int32_t)(snap.timing.last_nested_ticks/FM1_TICKS_PER_US));
    con_kv("audio_nested_max_us",(int32_t)(snap.timing.max_nested_ticks/FM1_TICKS_PER_US));
    con_kv("audio_render_us",(int32_t)(snap.timing.last_render_ticks/FM1_TICKS_PER_US));
    con_kv("audio_render_max_us",(int32_t)(snap.timing.max_render_ticks/FM1_TICKS_PER_US));
    con_kv("audio_budget_us",(int32_t)(snap.timing.budget_ticks/FM1_TICKS_PER_US));
    bb_diag_counter("audio_total_ticks",snap.timing.last_total_ticks);
    bb_diag_counter("audio_total_max_ticks",snap.timing.max_total_ticks);
    bb_diag_counter("audio_nested_ticks",snap.timing.last_nested_ticks);
    bb_diag_counter("audio_render_ticks",snap.timing.last_render_ticks);
    bb_diag_counter("audio_budget_ticks",snap.timing.budget_ticks);
    bb_diag_counter("audio_budget_exceeds",snap.timing.budget_exceeds);
    bb_diag_counter("audio_dma_misses",snap.timing.dma_misses);
    bb_diag_counter("audio_timing_anomalies",snap.timing.nested_gt_total);
    con_kv("audio_entry_spacing_valid",(int32_t)snap.timing.has_entry_spacing);
    con_kv("audio_entry_spacing_us",(int32_t)(snap.timing.last_entry_spacing_ticks/FM1_TICKS_PER_US));
    con_kv("audio_entry_spacing_max_us",(int32_t)(snap.timing.max_entry_spacing_ticks/FM1_TICKS_PER_US));
    /* Timing P1b: is the worst block a B-launch boundary (P1b cannot help)
     * or a quiet one (P1b applies)? Compare both with audio_total_max. */
    con_kv("audio_b_launch_max_us",(int32_t)(snap.timing.max_total_b_launch_ticks/FM1_TICKS_PER_US));
    con_kv("audio_b_quiet_max_us",(int32_t)(snap.timing.max_total_b_quiet_ticks/FM1_TICKS_PER_US));
    bb_diag_counter("audio_b_launch_max_ticks",snap.timing.max_total_b_launch_ticks);
    bb_diag_counter("audio_b_quiet_max_ticks",snap.timing.max_total_b_quiet_ticks);
    bb_diag_counter("audio_b_launch_halves",snap.timing.b_launch_halves);
    bb_diag_counter("audio_b_quiet_halves",snap.timing.b_quiet_halves);
#endif
    con_kv("p1b_compiled",(int32_t)snap.p1b.compiled);
    con_kv("p1b_enable",(int32_t)snap.p1b.enable);
    bb_diag_counter("p1b_call_jobs",snap.p1b.call_jobs);
    bb_diag_counter("p1b_joins_saved",snap.p1b.joins_saved);
    bb_diag_counter("p1b_guard_trips",snap.p1b.guard_trips);
    bb_diag_counter("p1b_fallbacks",snap.p1b.fallbacks);
    bb_diag_counter("p1b_guard_first_mask",snap.p1b.guard_first_mask);
    bb_diag_counter("p1b_guard_discards",snap.p1b.guard_discards);
    bb_diag_counter("p1b_guard_cpu1_faults",snap.p1b.guard_cpu1_faults);
    for(i=0;i<BB_MACHINES;i++) {
        /* A part header precedes each six-field group, avoiding dynamic keys. */
        con_kv("bb_part",(int32_t)i);
        con_kv("length",snap.length[i]);con_kv("next",snap.next[i]);
        con_kv("enabled",snap.enabled[i]);con_kv("muted",snap.muted[i]);
        con_kv("level",snap.level[i]);bb_diag_counter("hits",snap.hits[i]);
    }
    /* Main-loop-owned master and USB policy cannot change during this call. */
    con_kv("bb_master_q12",(int32_t)song.master_q12);
    con_kv("bb_usb_fixed",fx_usb_fixed);
    con_kv("bb_output_guard",BB_OUTPUT_GUARD);
#ifdef BB_DAC_SCALE
    con_kv("bb_dac_scale",BB_DAC_SCALE);
    con_kv("bb_dac_shift",BB_DAC_SCALE==128?7:BB_DAC_SCALE==256?8:-1);
#elif defined(OUT_SHIFT)
    con_kv("bb_dac_scale",1<<OUT_SHIFT);
    con_kv("bb_dac_shift",OUT_SHIFT);
#else
    con_kv("bb_dac_scale",-1);
    con_kv("bb_dac_shift",-1); /* Unknown in an adapter-only host fixture. */
#endif
#if BB_OUTPUT_GUARD
    /* Existing limiter state only: no new per-sample analysis. This peak is
     * the latest 33-frame INPUT window, not a held, output or true peak/RMS.
     * Frames wrap uint32; limited/fault counters saturate. Not a percentage. */
    con_kv("bb_guard_window_frames",BB_OUTPUT_WINDOW);
    bb_diag_q16("bb_guard_boost_q16",snap.boost,1,2);
    bb_diag_q16("bb_guard_gain_q16",snap.gain,0,1);
    bb_diag_q16("bb_guard_required_q16",snap.required,0,1);
    bb_diag_q16("bb_guard_input_window_peak_q16",snap.window_peak,0,64);
    bb_diag_counter("bb_guard_frames",snap.guard_frames);
    bb_diag_counter("bb_guard_limited",snap.guard_limited);
    bb_diag_counter("bb_guard_faults",snap.guard_faults);
#endif
}

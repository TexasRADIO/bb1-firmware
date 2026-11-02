/* SPDX-License-Identifier: GPL-3.0-only */
#define main oracle_main
#include ORACLE_C
#undef main
#include "bb_wire.h"
#include "single-drum-platform-r1.inc"
#include "dual-sample-platform-r1.inc"
static bb_pair_snapshot snap;
static bb_single_pause paused={1,1,1,0,0};
static unsigned checks,seen[256],restores;
#define CK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL line%d %s\n",__LINE__,#x);exit(1);}}while(0)
static int emit_test(void *ctx,unsigned status){CK(ctx==seen);seen[status&255u]++;return 1;}
static void initialize(void){
 memset(&bb_c1_live,0,sizeof bb_c1_live);bb_c1_live.ready=1;
 memset(&bb_c1_mail,0,sizeof bb_c1_mail);bb_c1_rich_active=0;
 memset(groups,0,sizeof groups);fail_wait=fail_publish=run_failed=inject_next=0;
 CK(bb_audio_init(&audio,enter_fn,leave_fn,NULL));CK(bb_audio_groups_bind(&audio,groups,BB_MACHINES));
 for(unsigned m=0;m<BB_MACHINES;m++)audio.core.drums[m].enabled=m<2;
 CK(bb_single_paused(&audio,&paused));
 memset(&snap,0,sizeof snap);snap.scene.core=audio.core;
 snap.identities[0].kind=snap.identities[1].kind=1;CK(gg_scene_capture(groups,snap.mix));
}
static void case_restore(unsigned paired,unsigned mode){
 initialize();memset(seen,0,sizeof seen);
 bb_midi_emit_fn endpoint=mode==2?NULL:emit_test;unsigned enabled=mode!=1;
 for(unsigned repeat=0;repeat<3;repeat++){
  audio.midi_output.emit=endpoint;audio.midi_output.context=seen;audio.midi_output.enabled=(uint8_t)enabled;
  audio.midi_output.phase=123;audio.midi_output.clocks_sent=17;audio.midi_output.clock_drops=3;
  audio.midi_output.transport_sent=9;audio.midi_output.transport_drops=2;
  // Refused scene/ownership must not reset or replace the current endpoint.
  bb_midi_output before=audio.midi_output;bb_single_pause no_ack=paused;no_ack.acknowledged=0;
  CK(bb_single_restore_base(&audio,&snap.scene,snap.mix,&no_ack)==BB_SINGLE_REJECTED);
  CK(!memcmp(&before,&audio.midi_output,sizeof before));
  snap.scene.core.running=1;CK(bb_single_restore_base(&audio,&snap.scene,snap.mix,&paused)==BB_SINGLE_REJECTED);
  snap.scene.core.running=0;CK(!memcmp(&before,&audio.midi_output,sizeof before));
  unsigned emitted_before=seen[0xfa]+seen[0xfb]+seen[0xfc]+seen[0xf8];
  int result=paired?bb_pair_restore_native(&audio,&snap,&paused):bb_single_restore(&audio,&snap.scene,snap.mix,&paused);
  CK(result==BB_SINGLE_OK);restores++;
  CK(audio.midi_output.emit==endpoint&&audio.midi_output.context==seen&&audio.midi_output.enabled==enabled);
  CK(!audio.midi_output.phase&&!audio.midi_output.running&&!audio.midi_output.clocks_sent&&!audio.midi_output.clock_drops&&!audio.midi_output.transport_sent&&!audio.midi_output.transport_drops);
  CK(!audio.core.running&&bb_commands_idle(&audio.commands));
  CK(bb_single_paused(&audio,&paused));CK(audio.groups==groups);
  for(unsigned i=0;i<4;i++)CK(render());
  CK(seen[0xfa]+seen[0xfb]+seen[0xfc]+seen[0xf8]==emitted_before); // no recall-triggered transport/clock
  memset(seen,0,sizeof seen);unsigned bpm=audio.core.bpm;
  push((bb_command){.type=BB_CMD_TRANSPORT,.value=1});CK(render());
  for(unsigned i=0;i<50;i++)CK(render());
  unsigned expected=(unsigned)((UINT64_C(51)*128*bpm*24)/(44100u*60u));CK(expected>0);
  if(enabled&&endpoint){CK(seen[0xfa]==1);CK(seen[0xf8]==expected);CK(audio.midi_output.clocks_sent==expected);}
  else {CK(!seen[0xfa]&&!seen[0xf8]);if(enabled){CK(audio.midi_output.transport_drops==1);CK(audio.midi_output.clock_drops==expected);}}
  push((bb_command){.type=BB_CMD_TRANSPORT,.value=0});CK(render());
  if(enabled&&endpoint)CK(seen[0xfc]==1);else CK(!seen[0xfc]);
  CK(!audio.core.running&&bb_commands_idle(&audio.commands));
  // A subsequent real Continue still uses the retained platform route.
  push((bb_command){.type=BB_CMD_TRANSPORT_RESUME});CK(render());
  CK(seen[0xfb]==(enabled&&endpoint?1u:0u));
  push((bb_command){.type=BB_CMD_TRANSPORT,.value=0});CK(render());
 }
}
int main(void){
 out=fopen("/dev/null","wb");CK(out);
 for(unsigned paired=0;paired<2;paired++)for(unsigned mode=0;mode<3;mode++)case_restore(paired,mode);
 fclose(out);printf("PASS %u checks / %u real legacy/native-pair restores; ON/OFF/null endpoints; rendered Start/clock/Stop/Continue; refusal/no unsolicited output\n",checks,restores);return 0;
}

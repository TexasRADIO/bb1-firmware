/* SPDX-License-Identifier: GPL-3.0-only
 * Compile-only native candidate. Exclusive cold boot, before physical audio.
 * No flash writes. Old 909/stock slot contents do not pass BBSK validation.
 */
#if BB_SAMPLE_HOST_INTEGRATION && defined(BB_SAMPLE_TARGET_CANDIDATE)
#include "bb/bb_sample_resident_r24.h"
#include "bb/bb_aux_voices_r1.h"
#include "../hal/fm1_xip.h"
static bb_sample_resident bb_sample_store __attribute__((section(".pool")));
/* A fourth BBSK directory entry is a shared auxiliary sound layer, not a
 * selectable kit. Keep all three musical counts separate from pack count. */
static bb_aux_voices bb_aux_owner __attribute__((section(".pool")));
static unsigned bb_sample_musical_count;
#if defined(BB_SAMPLE_DUAL_TRIAL)
static bb_sample_audio_binding bb_sample_b_binding;
static bb_sample_slot bb_sample_b_slots[BB_SAMPLE_OWNER_SLOTS] __attribute__((section(".pool")));
#endif
static unsigned bb_sample_boot_ready;
/* BB1 1.0/1.1 sound library f4bbf1a4... (header CRC at +16, payload CRC at +12) */
#define BB_SAMPLE_LIB11_HEADER_CRC 0xaab68e96u
#define BB_SAMPLE_LIB11_PAYLOAD_CRC 0xf5f132ffu
static uint32_t bb_sample_library_header_crc,bb_sample_library_payload_crc;
static int bb_sample_rebind_cold(void){
 bb_sample_audio_binding entries[8];
 if(!bb_sample_musical_count)return 0;
 memset(bb_sample_store.slots,0,sizeof bb_sample_store.slots);
 bb_aux_voices_stop(&bb_aux_owner);
 for(unsigned i=0;i<bb_sample_musical_count;i++)
  if(!bb_sample_resident_binding(&bb_sample_store,i,&entries[i]))return 0;
 bb_sample_store.binding=entries[0];
 unsigned selected=0;
#if defined(BB_SAMPLE_HYBRID_TRIAL)
 selected=BB_SAMPLE_808_ID;
#endif
 if(!bb_audio_sample_startup(&bb_sound,&bb_sample_store.binding,entries,bb_sample_musical_count,selected))return 0;
#if defined(BB_SAMPLE_DUAL_TRIAL)
 memset(bb_sample_b_slots,0,sizeof bb_sample_b_slots);
 bb_sample_b_binding=entries[0];bb_sample_b_binding.slots=bb_sample_b_slots;
 if(!bb_audio_sample_bind_b(&bb_sound,&bb_sample_b_binding,BB_SAMPLE_808_ID))return 0;
#endif
 return 1;
}
static void bb_sample_boot(void) {
 const uint8_t *pack=fm1_xip_ptr(0xA0000u);
 bb_sample_boot_ready=0;bb_sample_musical_count=0;
 bb_sample_ui_names=NULL;bb_sample_ui_count=0;
 if(bb_fsmp_u32(pack)!=0x4b534242u)return;
 unsigned bytes=bb_fsmp_u32(pack+8);
 if(bytes<256||bytes>BB_SAMPLE_PACK_LIMIT)return;
 if(!bb_sample_resident_prepare(&bb_sample_store,pack,bytes))return;
 /* 1.2: 3..8 directory entries. An auxiliary layer ("BB AUX") may only be the LAST entry and is never a
  * selectable kit, so the musical IDs are 0..musical-1 and 1.1's IDs 0..2 keep their meaning (a 1.1 library is
  * three kits, or three kits + AUX at index 3). "BB AUX" anywhere else is refused rather than exposed as a kit.
  * Whole-pack CRC and zone structure passed resident_prepare already. */
 unsigned entries=bb_sample_store.count,aux=0;
 if(entries<3||entries>8)return;
 for(unsigned k=0;k<entries;k++){
  const char *n=bb_sample_store.names[k];
  if(!n)return;
  if(memcmp(n,"BB AUX",6)==0&&n[6]==0){if(k!=entries-1)return;aux=1;}
 }
 if(aux&&(bb_sample_store.counts[entries-1]!=BB_AUX_ZONES||
    !bb_aux_voices_bind(&bb_aux_owner,bb_sample_store.zones[entries-1],BB_AUX_ZONES)))return;
 bb_sample_musical_count=entries-aux;
 if(bb_sample_musical_count<3){bb_sample_musical_count=0;if(aux)bb_aux_voices_stop(&bb_aux_owner);return;}
 if(!bb_sample_rebind_cold())return;
 if(aux&&!bb_audio_aux_bind(&bb_sound,&bb_aux_owner))return;
 bb_sample_ui_names=bb_sample_store.names;bb_sample_ui_count=bb_sample_musical_count;
 bb_sample_library_header_crc=bb_fsmp_u32(pack+16);bb_sample_library_payload_crc=bb_fsmp_u32(pack+12);
 /* Scenes saved with BB1 1.0/1.1's library (LinnDrum, DrumTraks, DMX + AUX) stay loadable when this library keeps
  * those three machines in slots 0..2 (#64 6492165998 §2). */
 {
  static const char kits11[3][12]={"LinnDrum","DrumTraks","DMX"};   /* names are NUL-terminated in 12-byte fields */
  static const uint8_t len11[3]={9,10,4};                              /* with the terminator; firmware has no strcmp */
  unsigned same=bb_sample_musical_count>=3;
  for(unsigned k=0;same&&k<3;k++)same=memcmp(bb_sample_store.names[k],kits11[k],len11[k])==0;
  bb_sample_scene_set_predecessor(BB_SAMPLE_LIB11_HEADER_CRC,BB_SAMPLE_LIB11_PAYLOAD_CRC,same?3u:0u);
 }
 bb_sample_boot_ready=1;
}
#if BB_WITH_SESSION
#if defined(BB_SAMPLE_DUAL_TRIAL)
static int bb_pair_session_capture(void){
 if(!bb_sample_boot_ready&&(bb_sound.sample||bb_sound.sample_b))return 0;
 bb_single_pause p=bb_single_target_pause();
 return bb_pair_capture(&bb_sound,&bb_session_memory.pair.snapshot,&p,
   bb_sample_library_header_crc,bb_sample_library_payload_crc)==BB_SINGLE_OK;
}
static size_t bb_pair_session_encode(void){
 return bb_pair_encode_mode(&bb_session_memory.pair.snapshot,bb_session_memory.wire,
   sizeof bb_session_memory.wire,!bb_sample_boot_ready);
}
static int bb_pair_session_decode(const void *data,size_t bytes){
 return bb_pair_decode(data,bytes,&bb_session_memory.pair,
   bb_sample_library_header_crc,bb_sample_library_payload_crc,bb_sample_boot_ready?bb_sample_musical_count:0);
}
static int bb_pair_session_restore(void){
 if(!bb_sample_boot_ready){bb_single_pause p=bb_single_target_pause();return bb_pair_restore_native(&bb_sound,&bb_session_memory.pair.snapshot,&p);}
 if(bb_sound.sample!=&bb_sample_store.binding||bb_sound.sample_b!=&bb_sample_b_binding)return BB_SINGLE_REJECTED;
 for(unsigned k=0;k<bb_sample_musical_count;k++){
  if(!bb_sample_resident_binding(&bb_sample_store,k,&bb_session_memory.sample_entries[0][k]))return BB_SINGLE_REJECTED;
  bb_session_memory.sample_entries[1][k]=bb_session_memory.sample_entries[0][k];
  bb_session_memory.sample_entries[1][k].slots=bb_sample_b_slots;
 }
 bb_single_pause p=bb_single_target_pause();
 return bb_pair_restore(&bb_sound,&bb_session_memory.pair.snapshot,&p,
  bb_session_memory.sample_entries[0],bb_session_memory.sample_entries[1],bb_sample_musical_count,
  bb_sample_library_header_crc,bb_sample_library_payload_crc);
}
#else
static int bb_sample_session_capture(bb_sample_scene_identity *id){
 unsigned synth=0;
#if defined(BB_SAMPLE_HYBRID_TRIAL)
 synth=bb_sound.sample_selected==BB_SAMPLE_808_ID;
#endif
 if(!id||!bb_sample_boot_ready||bb_sound.sample!=&bb_sample_store.binding||
    (!synth&&bb_sound.sample_selected>=bb_sample_musical_count))return 0;
 bb_sample_scene_identity value={0};
 value.pack_header_crc=bb_sample_library_header_crc;value.pack_payload_crc=bb_sample_library_payload_crc;
 value.kind=(uint8_t)synth;value.kit=synth?0:bb_sound.sample_selected;value.gain=bb_sound.sample->gain;
 value.echo=(uint8_t)bb_sound.sample->echo;value.spring=(uint8_t)bb_sound.sample->spring;
 *id=value;return 1;
}
static int bb_sample_session_decode(const void *data,size_t bytes,bb_family_scene_workspace *work,
                                    uint8_t *mix,bb_sample_scene_identity *id){
 return bb_sample_boot_ready&&bb_sample_scene_decode(data,bytes,44100,
     bb_sample_library_header_crc,bb_sample_library_payload_crc,bb_sample_musical_count,work,mix,id);
}
static int bb_sample_session_restore(const bb_family_scene *scene,const uint8_t *mix,
                                     const bb_sample_scene_identity *id,bb_sample_audio_binding *entries){
 if(!id||!entries||!bb_sample_boot_ready||id->kit>=bb_sample_musical_count||
    id->pack_header_crc!=bb_sample_library_header_crc||id->pack_payload_crc!=bb_sample_library_payload_crc)
     return BB_SINGLE_REJECTED;
 unsigned selected=id->kit;
#if defined(BB_SAMPLE_HYBRID_TRIAL)
 if(id->kind>1||(id->kind&&id->kit))return BB_SINGLE_REJECTED;
 if(id->kind)selected=BB_SAMPLE_808_ID;
#else
 if(id->kind)return BB_SINGLE_REJECTED;
#endif
 for(unsigned i=0;i<bb_sample_musical_count;i++){
     if(!bb_sample_resident_binding(&bb_sample_store,i,&entries[i]))return BB_SINGLE_REJECTED;
     entries[i].gain=id->gain;entries[i].echo=id->echo;entries[i].spring=id->spring;
 }
 bb_single_pause pause=bb_single_target_pause();
#if BB_WITH_RICH_DRUM_RENDER
 /* The decoder validated every rich part against the saved kit before reset.
  * Pass A's actual replacement through BOTH restore checks; do not erase its
  * dormant legacy notes merely to satisfy the older lane validator. */
 return bb_single_restore_sample_impl(&bb_sound,scene,mix,&pause,&bb_sample_store.binding,
                                 entries,bb_sample_musical_count,selected,
                                 (bb_session_memory.rich_mask&1u)?&bb_session_memory.rich_phrases[0]:NULL);
#else
 return bb_single_restore_sample(&bb_sound,scene,mix,&pause,&bb_sample_store.binding,
                                 entries,bb_sample_musical_count,selected);
#endif
}
#endif
#endif
#else
static void bb_sample_boot(void) {}
#endif

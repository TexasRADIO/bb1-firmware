/* SPDX-License-Identifier: GPL-3.0-only
 * BB1 2.0 pre-launch checks (Lane Q). Test code only: no firmware source is changed.
 * Appended to the oracle driver's CPU1 stand-in by build.sh (the pattern_edit build: integration-480.c up to its
 * "integrated bb_audio comparison" marker, the oracle's own compile.json, ASan/UBSan on), so it drives the real
 * 2.0 audio owner: bb_audio_render, the rich phrase players, the core clock, the arranger section handoff, the
 * capture ring and the live recorder.
 *
 * N02  live-swing regression matrix. Swing (GLOBAL K2, BB_CMD_SWING_X2) changes WHILE PLAYING, at exact step
 *      starts, mid-step, one frame either side of a pair start, with every block size the device could use.
 *      Every strike of both drum layers (BB_RICH_TRACE hook) and every core tick (Dillo's clock) is compared with
 *      an independent reference written from the documented rules, not from the engine's code:
 *        R1 (bb_swing.h, GLOBAL K2): swing is 50..75 % in half steps, x2 = 100..150. A pair of steps lasts two
 *           straight steps; its first step takes x2/100 of a straight step, the second (200-x2)/100.
 *        R2 (bb_core.h "latch each pair"): the swing used by a pair is the value in force when the pair starts.
 *           A change during a pair's first step therefore waits for the next pair.
 *        R3 lattices: sixteenths 4, eighths 2, triplets 6 steps per beat; one beat = 60/BPM s at every swing.
 *        R4 a command takes effect at the start of the audio block in which it is drained.
 *        R5 a step starts on the first sample at or after its exact musical position (no early strikes).
 *        R6 every pattern hit sounds exactly once per pass of its step; a ratchet xN adds N-1 strikes at
 *           ceil(k*D/N) frames into the step, D = the step's length in frames (R5 rounding).
 *        R7 (bb_core.h: "A phrase launch may ... update active_swing at an even tick"; drums and Dillo share
 *           one pair grid): every layer's pairs start on the same frames as the core clock's pairs.
 *      The reference counts position in exact integer units (sample_rate*60*x2 per step, BPM*steps_per_beat*100
 *      per sample), frame by frame: no spans, no phase residuals, no shared code with bb_core.c.
 *      On 732646dd the SONG part FAILS by design with 6 R7 failures: a SONG section whose length is an odd number of
 *      sixteenths hands the next section over on the second step of a pair, the restarted drum layers start a new
 *      pair there while the core clock (Dillo) does not, so at swing > 50 % drums and Dillo disagree on every
 *      other step (125 ms at 75 %, 120 BPM). `live_swing repro` prints the minimal case.
 * TAP  N12 first step: what a tempo change of the size REC+GLO tap produces (BB_CMD_TEMPO) does to the step
 *      grid, to a sounding Dillo step note, to a held Dillo key, to a queued SONG section and to a take.
 */
#include "bb_pattern_edit.h"
#include "bb_arr_controller.h"
#include "bb_arr_sources.h"
#include <inttypes.h>
static uintptr_t enter_fn(void *x){(void)x;return 0;}
static void leave_fn(void *x,uintptr_t t){(void)x;(void)t;}
static bb_audio audio;static gg_state groups[BB_MACHINES];
static unsigned long checks,failures;
#define CHECK(c,...) do{checks++;if(!(c)){failures++;if(failures<=60){printf("FAIL %s:%d: ",__func__,__LINE__);printf(__VA_ARGS__);printf("\n");}}}while(0)
static void push(bb_command c){assert(bb_command_push(&audio.commands,&c));}
typedef unsigned __int128 u128;

/* ------------------------------------------------------------------ strike trace (BB_RICH_TRACE=1) */
#define TMAX 400000u
typedef struct {uint64_t frame;uint32_t payload;uint8_t m;} strike_t;
static strike_t tr[TMAX];static unsigned trn,tr_lost;
void bb_rich_trace(const bb_audio *a,unsigned m,uint64_t frame,uint32_t payload){
 (void)a;if(trn<TMAX){tr[trn].frame=frame;tr[trn].payload=payload;tr[trn].m=(uint8_t)m;trn++;}else tr_lost++;
}
/* ------------------------------------------------------------------ owner calls, core tick log */
#define KMAX 16384u
static uint64_t core_tick[KMAX];static unsigned core_n,core_restarts;
static unsigned serial_mode=1,render_refused;
static uint64_t state_digest;
static void digest(const void *p,size_t n){const unsigned char *b=p;for(size_t i=0;i<n;i++)state_digest=(state_digest^b[i])*1099511628211ull;}
static void call(unsigned n){
 uint32_t t0=audio.core.ticks;uint8_t was=audio.core.running;
 if(!bb_audio_render(&audio,n))render_refused++;
 if(!audio.core.running)return;
 if(audio.core.ticks<t0||!was){core_restarts+=was;core_n=0;} /* transport start/restart: frame 0 again */
 uint32_t t1=audio.core.ticks;
 if(t1&&(t1!=t0||!was)){
  /* the owner's clock after the call: phase=(frames since the tick)*increment+residual, residual<increment */
  /* The residual is below the increment that produced it: the new one for a tick inside this call, the old one
   * for a tick on the call's first frame (a tempo command drained at that frame). Hence the max(). */
  uint64_t f=audio.core.frame-audio.core.phase/((uint32_t)audio.core.bpm*400u),start=audio.core.frame-n;
  if(was&&f<start)f=start;
  if(t1-1u<KMAX){core_tick[t1-1u]=f;if(t1>core_n)core_n=t1;}
 }
 digest(&audio.core.ticks,4);digest(&audio.core.phase,4);digest(&audio.core.frame,8);
 for(unsigned m=0;m<2;m++){digest(&audio.rich.player[m].step,1);digest(&audio.rich.player[m].odd,1);
  digest(&audio.rich.player[m].phase,4);digest(&audio.rich.player[m].swing,1);digest(&audio.hits[m],4);}
 digest(audio.left,n*sizeof(float));digest(audio.right,n*sizeof(float));
}
static uint32_t rng=1;
static unsigned rnd(unsigned n){rng=rng*1103515245u+12345u;return (rng>>8)%n;}
static unsigned block_mode;
static unsigned next_block(void){return block_mode==0?128u:block_mode==1?64u:block_mode==2?1u+rnd(128):1u+rnd(7);}
static void render_to(uint64_t target){
 while(audio.core.running&&audio.core.frame<target){
  uint64_t left=target-audio.core.frame;unsigned b=next_block();call(left<b?(unsigned)left:b);
 }
}
static void fresh(unsigned bpm,unsigned x2){
 memset(&bb_c1_live,0,sizeof bb_c1_live);bb_c1_live.ready=serial_mode?0u:1u;bb_c1_retired=BB_C1_CON=0;
 memset(&bb_c1_mail,0,sizeof bb_c1_mail);bb_c1_rich_active=0;fail_wait=fail_publish=run_failed=inject_next=0;
 memset(&bb_coefficient_trace,0,sizeof bb_coefficient_trace);memset(groups,0,sizeof groups);
 assert(bb_audio_init(&audio,enter_fn,leave_fn,0));assert(bb_audio_groups_bind(&audio,groups,BB_MACHINES));
 assert(bb_set_tempo(&audio.core,bpm));assert(bb_set_swing_x2(&audio.core,x2));
 for(unsigned m=0;m<2;m++){audio.core.drums[m].enabled=1;audio.core.drums[m].muted=0;audio.core.drums[m].level=110;}
 trn=tr_lost=0;core_n=core_restarts=0;render_refused=0;state_digest=1469598103934665603ull;
}
/* ------------------------------------------------------------------ independent reference (R1-R5) */
typedef struct {uint64_t frame;unsigned x2,bpm;} sched_t;
static sched_t sch[64];static unsigned nsch;
static void sched_add(uint64_t f,unsigned x2,unsigned bpm){assert(nsch<64);sch[nsch++]=(sched_t){f,x2,bpm};}
static unsigned at_x2(uint64_t f){unsigned v=sch[0].x2;for(unsigned i=1;i<nsch;i++)if(sch[i].frame<=f)v=sch[i].x2;return v;}
static unsigned at_bpm(uint64_t f){unsigned v=sch[0].bpm;for(unsigned i=1;i<nsch;i++)if(sch[i].frame<=f)v=sch[i].bpm;return v;}
static unsigned per_beat(unsigned lattice){return lattice==BB_FDL_EIGHTHS?2u:lattice==BB_FDL_TRIPLETS?6u:4u;}
/* Step start frames of a player that (re)starts at frame origin with step 0 the first of a pair. Also returns the
 * exact unit boundaries B[k] (units: per sample BPM*per_beat*100, per step 44100*60*x2/100 *100). */
static uint64_t ref_frame[KMAX];static u128 ref_units[KMAX];static unsigned ref_x2[KMAX];
static unsigned ref_steps(uint64_t origin,unsigned lattice,unsigned count){
 u128 pos=0,boundary=0;unsigned k=0,x2=100,rate=audio.core.sample_rate;
 if(count>KMAX)count=KMAX;
 for(uint64_t f=origin;k<count;f++){
  while(k<count&&pos>=boundary){
   ref_frame[k]=f;ref_units[k]=boundary;if(!(k&1u))x2=at_x2(f);   /* R2: latch at the pair's first step */
   ref_x2[k]=x2;boundary+=(u128)rate*60u*((k&1u)?200u-x2:x2);k++;
  }
  pos+=(u128)at_bpm(f)*per_beat(lattice)*100u;
 }
 return k;
}
/* ------------------------------------------------------------------ test phrases */
static unsigned phrase_add(bb_phrase *p,unsigned voice,unsigned step){
 p->events[p->count++]=(uint16_t)(voice|(step<<5)|((unsigned)BB_FDI_UNACCENTED<<11));return p->count;
}
static void make_phrase(bb_phrase *p,unsigned lattice,unsigned len,unsigned ratchets){
 memset(p,0,sizeof *p);p->lattice=(uint8_t)lattice;p->length=(uint8_t)len;p->bars=0;
 for(unsigned s=0;s<len;s++){
  phrase_add(p,BB_FDV_HH,s);                       /* every step sounds: every step start is observed */
  if(s%4u==0)phrase_add(p,BB_FDV_BD,s);
  if(s%8u==4u)phrase_add(p,BB_FDV_SD,s);
 }
 if(ratchets){
  assert(bb_phrase_ratchet_set(p,BB_FDV_BD,0,2));
  if(len>4)assert(bb_phrase_ratchet_set(p,BB_FDV_SD,4,3));
  if(len>6)assert(bb_phrase_ratchet_set(p,BB_FDV_HH,6,4));
  assert(bb_phrase_ratchet_set(p,BB_FDV_HH,len-1u,2)); /* last step: its strikes reach the loop wrap */
 }
}
static int submit_phrase(unsigned m,const bb_phrase *p){
 static bb_phrase_index index;uint32_t ticket,playable=bb_audio_phrase_source_voices(&audio,m);
 if(!bb_phrase_index_prepare(p,playable,&index))return 0;
 return bb_audio_phrase_submit(&audio,m,&index,16,&ticket);
}
/* ------------------------------------------------------------------ strike comparison (R5, R6) */
typedef struct {uint64_t frame;uint16_t voice,cell,kind;} hit_t;
#define HMAX 200000u
static hit_t want[HMAX],got[HMAX];static unsigned nwant,ngot;
static int hit_cmp(const void *x,const void *y){
 const hit_t *a=x,*b=y;
 if(a->frame!=b->frame)return a->frame<b->frame?-1:1;
 if(a->kind!=b->kind)return a->kind<b->kind?-1:1;
 if(a->cell!=b->cell)return a->cell<b->cell?-1:1;
 return a->voice<b->voice?-1:a->voice>b->voice;
}
/* Expected strikes of phrase p from reference step first to last-1, cell = (start_cell+k)%len. */
static void want_steps(const bb_phrase *p,unsigned first,unsigned last,unsigned start_cell){
 for(unsigned k=first;k<last;k++){
  unsigned cell=(start_cell+k)%p->length;uint64_t D=ref_frame[k+1]-ref_frame[k];
  for(unsigned i=0;i<p->count;i++){
   unsigned w=p->events[i];if(((w>>5)&63u)!=cell)continue;
   assert(nwant<HMAX);want[nwant++]=(hit_t){ref_frame[k],(uint16_t)(w&31u),(uint16_t)cell,0};
   unsigned n=bb_phrase_ratchet_count(p,i);
   for(unsigned j=1;j<n;j++){assert(nwant<HMAX);want[nwant++]=(hit_t){ref_frame[k]+(D*j+n-1u)/n,(uint16_t)(w&31u),(uint16_t)cell,1};}
  }
 }
}
static void got_strikes(unsigned m,uint64_t from,uint64_t to){
 ngot=0;
 for(unsigned i=0;i<trn;i++)if(tr[i].m==m&&tr[i].frame>=from&&tr[i].frame<to){
  uint32_t p=tr[i].payload;assert(ngot<HMAX);
  got[ngot++]=(hit_t){tr[i].frame,(uint16_t)(p&31u),(uint16_t)((p>>5)&63u),(uint16_t)(((p>>23)&3u)?1u:0u)};
 }
}
/* Returns the number of differing strikes (missing + extra + moved), 0 when identical. */
static unsigned compare_hits(unsigned *dup,unsigned *lost){
 qsort(want,nwant,sizeof *want,hit_cmp);qsort(got,ngot,sizeof *got,hit_cmp);
 unsigned i=0,j=0,diff=0;*dup=*lost=0;
 while(i<nwant||j<ngot){
  int c=i==nwant?1:j==ngot?-1:hit_cmp(&want[i],&got[j]);
  if(i<nwant&&j<ngot&&want[i].voice==got[j].voice&&want[i].cell==got[j].cell&&want[i].kind==got[j].kind&&want[i].frame<=got[j].frame+1u&&got[j].frame<=want[i].frame+1u)c=0;
  if(!c){i++;j++;continue;}
  diff++;if(c<0){(*lost)++;i++;}else{(*dup)++;j++;}
 }
 return diff;
}
/* ------------------------------------------------------------------ N02 matrix */
typedef struct {unsigned lattice,len,lenB,bpm,x2a,x2b,x2c,where,ratchet,block;} scn_t;
static unsigned long m_runs,m_changes,m_strikes,m_ratchet_strikes,m_ticks,m_worker_runs,m_worker_same,m_pair_checks;
static unsigned long m_where[6],m_lattice[3],m_odd,m_even,m_x2seen[51];
static uint64_t change_frame(unsigned where,unsigned k){
 switch(where){
  case 0:return ref_frame[k];                              /* exactly at a pair's first step */
  case 1:return ref_frame[k+1];                            /* exactly at a pair's second step */
  case 2:return (ref_frame[k]+ref_frame[k+1])/2u;          /* mid first step */
  case 3:return (ref_frame[k+1]+ref_frame[k+2])/2u;        /* mid second step */
  case 4:return ref_frame[k+2]-1u;                         /* one frame before the next pair */
  default:return ref_frame[k]+1u;                          /* one frame after the pair start */
 }
}
static uint64_t run_case(const scn_t *c,int check){
 bb_phrase A,B;fresh(c->bpm,c->x2a);nsch=0;sched_add(0,c->x2a,c->bpm);block_mode=c->block;rng=c->len*7919u+c->x2b;
 make_phrase(&A,c->lattice,c->len,c->ratchet);make_phrase(&B,BB_FDL_SIXTEENTHS,c->lenB,!c->ratchet);
 CHECK(submit_phrase(0,&A)&&submit_phrase(1,&B),"submit lattice %u len %u",c->lattice,c->len);
 call(128);push((bb_command){.type=BB_CMD_TRANSPORT,.value=1});call(1);
 unsigned k0=(c->len+3u)&~1u;if(k0<8)k0=8;                   /* a pair start after the first pass */
 ref_steps(0,c->lattice,k0+4);uint64_t f1=change_frame(c->where,k0);
 render_to(f1);push((bb_command){.type=BB_CMD_SWING_X2,.value=(uint16_t)c->x2b});sched_add(f1,c->x2b,c->bpm);
 unsigned k1=k0+c->len+5u;k1&=~1u;
 ref_steps(0,c->lattice,k1+4);uint64_t f2=change_frame((c->where+3u)%6u,k1);
 render_to(f2);push((bb_command){.type=BB_CMD_SWING_X2,.value=(uint16_t)c->x2c});sched_add(f2,c->x2c,c->bpm);
 unsigned K=k1+2u*c->len+12u;
 unsigned n=ref_steps(0,c->lattice,K+1);assert(n==K+1);
 render_to(ref_frame[K]+1u);
 if(!check)return state_digest;
 /* layer A against the reference */
 nwant=0;want_steps(&A,0,K,0);got_strikes(0,0,ref_frame[K]);
 unsigned dup,lost,d=compare_hits(&dup,&lost);
 CHECK(!d,"A lattice %u len %u bpm %u x2 %u->%u(%u)->%u where %u ratchet %u block %u: %u differ (%u extra, %u missing)",
  c->lattice,c->len,c->bpm,c->x2a,c->x2b,f1>=ref_frame[k0]?1:0,c->x2c,c->where,c->ratchet,c->block,d,dup,lost);
 for(unsigned i=0;i<nwant;i++){if(want[i].kind)m_ratchet_strikes++;else m_strikes++;}
 /* layer B (sixteenths) and the core clock (Dillo) on their own reference: R7 means these frames are equal */
 uint64_t a_frames[KMAX];unsigned na=n;memcpy(a_frames,ref_frame,na*sizeof *ref_frame);
 unsigned KB=(unsigned)(K*4u/per_beat(c->lattice));
 n=ref_steps(0,BB_FDL_SIXTEENTHS,KB+1);assert(n==KB+1);
 nwant=0;want_steps(&B,0,KB,0);got_strikes(1,0,ref_frame[KB]);d=compare_hits(&dup,&lost);
 CHECK(!d,"B len %u bpm %u x2 %u->%u->%u where %u: %u differ (%u extra, %u missing)",c->lenB,c->bpm,c->x2a,c->x2b,c->x2c,c->where,d,dup,lost);
 for(unsigned i=0;i<nwant;i++){if(want[i].kind)m_ratchet_strikes++;else m_strikes++;}
 unsigned ticks=core_n<KB?core_n:KB,bad=0;
 for(unsigned k=0;k<ticks;k++)if(core_tick[k]!=ref_frame[k])bad++;
 CHECK(ticks+2u>=KB&&!bad,"core ticks %u/%u, %u off the reference (bpm %u x2 %u->%u->%u where %u)",ticks,KB,bad,c->bpm,c->x2a,c->x2b,c->x2c,c->where);
 m_ticks+=ticks;
 /* R7 across lattices: every beat line of A coincides with a core beat line (4 sixteenths) */
 unsigned per=per_beat(c->lattice),beats=K/per;
 for(unsigned b=0;b<beats&&4u*b<ticks;b++){m_pair_checks++;CHECK(a_frames[b*per]==core_tick[4u*b],"beat %u: A %" PRIu64 " core %" PRIu64,b,a_frames[b*per],core_tick[4u*b]);}
 CHECK(!render_refused&&!audio.rich.render_errors&&!tr_lost,"owner refused %u calls, %u render errors",render_refused,audio.rich.render_errors);
 m_runs++;m_changes+=2;m_where[c->where]++;m_where[(c->where+3u)%6u]++;m_lattice[c->lattice]++;
 if(c->len&1u)m_odd++;else m_even++;m_x2seen[c->x2b-100u]++;m_x2seen[c->x2c-100u]++;
 return state_digest;
}
static void n02_matrix(void){
 static const struct {unsigned lattice,len;} shapes[]={
  {BB_FDL_SIXTEENTHS,16},{BB_FDL_SIXTEENTHS,15},{BB_FDL_SIXTEENTHS,7},{BB_FDL_SIXTEENTHS,64},
  {BB_FDL_EIGHTHS,8},{BB_FDL_EIGHTHS,5},{BB_FDL_TRIPLETS,24},{BB_FDL_TRIPLETS,13}};
 static const unsigned from[5]={100,150,117,133,101},tempo[5]={120,97,174,300,61};
 unsigned idx=0;
 for(unsigned s=0;s<sizeof shapes/sizeof shapes[0];s++)for(unsigned x2=100;x2<=150;x2++,idx++){
  scn_t c={shapes[s].lattice,shapes[s].len,idx%2?13u:16u,tempo[idx%5],from[idx%5],x2,150u-(x2-100u)/2u,idx%6,idx%3==0,idx%4};
  if(c.x2c==c.x2b)c.x2c=c.x2b==150?149:c.x2b+1;
  serial_mode=1;uint64_t serial=run_case(&c,1);
  if(idx%5==0){serial_mode=0;uint64_t worker=run_case(&c,0);m_worker_runs++;
   CHECK(worker==serial,"worker (CPU1 plan) and serial owner differ: shape %u x2 %u",s,x2);if(worker==serial)m_worker_same++;serial_mode=1;}
 }
 unsigned all=1;for(unsigned i=0;i<51;i++)if(!m_x2seen[i])all=0;
 CHECK(all,"every swing value 50..75 in half steps used as a live change");
 printf("N02 matrix: %lu runs, %lu live swing changes (where: %lu/%lu/%lu/%lu/%lu/%lu; lattices 16th/8th/triplet %lu/%lu/%lu; "
  "lengths odd %lu even %lu; all 51 swing values), %lu primary strikes + %lu ratchet strikes on the reference, %lu core ticks, "
  "%lu beat-line checks, worker=serial %lu/%lu\n",m_runs,m_changes,m_where[0],m_where[1],m_where[2],m_where[3],m_where[4],m_where[5],
  m_lattice[0],m_lattice[1],m_lattice[2],m_odd,m_even,m_strikes,m_ratchet_strikes,m_ticks,m_pair_checks,m_worker_same,m_worker_runs);
}
/* ------------------------------------------------------------------ N02: a sub-sample anchor check that
 * documents R7 for a launch: a phrase launched while playing lands on an even core tick. */
/* ------------------------------------------------------------------ N02: queued SONG sections */
static bb_phrase_index catalogue[1024];static bb_factory_choice choices[1024];static unsigned sections;
static void collect(void){
 if(sections)return;assert(bb_factory_init());
 for(unsigned family=0;family<2;family++){
  bb_browser b;assert(bb_browser_init(&b,&bb_factory_catalogue));assert(bb_browser_group(&b,family));
  for(unsigned bank=0;bank<bb_browser_bank_count(&b);bank++){
   b.bank_index=bank;b.part_index=0;
   for(unsigned part=0;part<bb_browser_part_count(&b);part++){
    b.part_index=part;assert(bb_browser_current(&b,&choices[sections]));
    assert(bb_factory_prepare_rich_drum(&bb_factory_catalogue,&choices[sections],&catalogue[sections])==BB_FDRUM_OK);sections++;
   }
  }
 }
}
static bb_arrangement model;static bb_arr_sources sources;static bb_arr_controller controller;
/* Pick a catalogue section whose A and B drum parts are sixteenths with at least one hit on most steps. */
static unsigned pick_sixteenths(unsigned from){
 for(unsigned i=from;i<sections;i++){
  const bb_phrase *p=&catalogue[i].phrase,*q=&catalogue[(i+17)%sections].phrase;
  if(p->lattice!=BB_FDL_SIXTEENTHS||q->lattice!=BB_FDL_SIXTEENTHS||p->length!=16||q->length!=16)continue;
  unsigned steps=0;for(unsigned s=0;s<16;s++){for(unsigned e=0;e<p->count;e++)if(((p->events[e]>>5)&63u)==s&&(p->events[e]&31u)<BB_FDV_ACCENT){steps++;break;}}
  if(steps>=12)return i;
 }
 return UINT32_MAX;
}
typedef struct {unsigned applied,odd_ticks,misaligned_steps,max_offset,checked_pairs,subsample_steps;} song_result;
/* One SONG: rows with the given section durations (sixteenths), swing x2, optional live swing change while a
 * section is QUEUED. Returns per-section results: for every section applied while playing, its A layer's step
 * starts are compared with the core clock's step starts (R7). */
static song_result play_song(const unsigned *dur,unsigned rows,unsigned bpm,unsigned x2,int change_queued,unsigned *strike_diff){
 song_result r={0};collect();fresh(bpm,x2);nsch=0;sched_add(0,x2,bpm);block_mode=0;
 assert(!bb_arr_sources_init(&sources,&bb_factory_catalogue));bb_arr_init(&model);
 unsigned base=pick_sixteenths(0);assert(base!=UINT32_MAX);
 uint32_t id[8];
 for(unsigned i=0;i<rows;i++){
  bb_arr_section part={0};snprintf(part.name,sizeof part.name,"S%u",i);part.duration=(uint16_t)dur[i];
  unsigned sec=pick_sixteenths(base+i);assert(sec!=UINT32_MAX);
  assert(!bb_arr_source_bind(&sources,&choices[sec],BB_SOURCE_DRUM,&part.part[0]));
  assert(!bb_arr_source_bind(&sources,&choices[(sec+17)%sections],BB_SOURCE_DRUM,&part.part[1]));
  assert(!bb_arr_source_bind(&sources,&choices[sec],BB_SOURCE_DILLO,&part.part[2]));
  assert(!bb_arr_section_put(&model,&part,&id[i]));assert(!bb_arr_row_insert(&model,i,(bb_arr_row){id[i],1}));
 }
 call(128);assert(!bb_arr_controller_init(&controller,&model,&sources));assert(!bb_arr_controller_start(&controller,&audio));
 uint32_t seen=0;uint64_t apply_frame[16];uint32_t apply_tick[16];unsigned applies=0,changed=0;
 for(unsigned blocks=0;blocks<40000;blocks++){
  call(128);assert(!bb_arr_controller_poll(&controller,&audio));
  if(change_queued&&!changed&&audio.section.phase==BB_SECTION_PENDING&&applies==2){
   uint64_t f=audio.core.frame;push((bb_command){.type=BB_CMD_SWING_X2,.value=(uint16_t)(x2==150?117:150)});
   sched_add(f,x2==150?117:150,bpm);changed=1;if(getenv("LQ_DEBUG"))printf("  dbg swing change at %" PRIu64 " tick %u due %u\n",f,audio.core.ticks,audio.section.due_tick);
  }
  if(audio.section.status.state.applied!=seen){seen=audio.section.status.state.applied;
   if(applies<16){apply_frame[applies]=audio.section.status.state.applied_frame;apply_tick[applies]=audio.section.status.applied_tick;applies++;}}
  if(controller.view.ended&&!audio.core.running)break;
 }
 CHECK(applies==rows,"song: %u of %u sections applied",applies,rows);
 unsigned total=0;for(unsigned i=0;i<rows;i++)total+=dur[i];
 CHECK(audio.core.ticks==total,"song length %u ticks, rows sum %u",audio.core.ticks,total);
 /* core clock against the reference from transport start */
 unsigned n=ref_steps(0,BB_FDL_SIXTEENTHS,total+1);assert(n==total+1);
 unsigned bad=0;for(unsigned k=0;k<total&&k<core_n;k++)if(core_tick[k]!=ref_frame[k])bad++;
 CHECK(!bad,"song core ticks: %u off the reference",bad);
 uint64_t core_ref[KMAX];memcpy(core_ref,ref_frame,(total+1)*sizeof *ref_frame);
 *strike_diff=0;
 for(unsigned s=0;s<applies;s++){
  CHECK(apply_frame[s]==core_ref[apply_tick[s]],"section %u applied at frame %" PRIu64 ", its tick %u starts at %" PRIu64,s,apply_frame[s],apply_tick[s],core_ref[apply_tick[s]]);
  if(apply_tick[s]&1u)r.odd_ticks++;
  r.applied++;
  /* the reference for the restarted layer: origin = the apply frame, step 0 = a pair's first step */
  unsigned steps=dur[s];
  if(getenv("LQ_NEGATIVE_OLD_GRID")){n=ref_steps(apply_frame[s],BB_FDL_SIXTEENTHS,steps+1);assert(n==steps+1);}
  else for(unsigned k=0;k<=steps;k++)ref_frame[k]=core_ref[apply_tick[s]+k];
  /* Grid identity is established by comparing actual strikes below; do not compare two references. */
  /* the layer's own strikes follow its own reference (R5, R6): no hit doubled or lost at the hand-off */
  for(unsigned layer=0;layer<2;layer++){
  unsigned source_index=pick_sixteenths(base+s);if(layer)source_index=(source_index+17)%sections;
  bb_phrase p=catalogue[source_index].phrase;
  CHECK(p.lattice==BB_FDL_SIXTEENTHS,"shared-grid test requires sixteenths for layer %u",layer);
  uint64_t end=s+1<applies?apply_frame[s+1]:ref_frame[steps];if(end>ref_frame[steps])end=ref_frame[steps];
  unsigned last=0;while(last<steps&&ref_frame[last]<end)last++;
  nwant=0;want_steps(&p,0,last,0);
  for(unsigned i=0;i<nwant;)if(want[i].voice>=BB_FDV_ACCENT)want[i]=want[--nwant];else i++;
  got_strikes(layer,ref_frame[0],end);unsigned dup,lost;unsigned dd=compare_hits(&dup,&lost);*strike_diff+=dd;
  if(dd&&getenv("LQ_DEBUG")){for(unsigned i=0,j=0;(i<nwant||j<ngot);){int c=i==nwant?1:j==ngot?-1:hit_cmp(&want[i],&got[j]);
    if(!c){i++;j++;continue;}const hit_t *h=c<0?&want[i++]:&got[j++];printf("  dbg strike %s frame %" PRIu64 " voice %u cell %u kind %u (section %u applied %" PRIu64 ")\n",c<0?"missing":"extra",h->frame,h->voice,h->cell,h->kind,s,apply_frame[s]);}}
  } /* each layer */
 }
 return r;
}
static void n02_song(void){
 static const unsigned even[4]={16,32,16,48},odd[4]={16,15,16,13},odd2[4]={15,16,16,16};
 static const unsigned swings[4]={100,116,133,150};
 unsigned long songs=0,odd_apps=0,mis=0,maxoff=0,clean_even=0,diffs=0,sub=0;
 for(unsigned i=0;i<4;i++)for(unsigned v=0;v<3;v++){
  const unsigned *d=v==0?even:v==1?odd:odd2;unsigned sd;
  song_result r=play_song(d,4,120,swings[i],0,&sd);songs++;diffs+=sd;sub+=r.subsample_steps;
  CHECK(!sd,"song v%u swing %u: %u strikes differ from the restarted layer's reference",v,swings[i],sd);
  if(v==0){CHECK(!r.misaligned_steps&&!r.odd_ticks,"even durations: %u steps off the core grid",r.misaligned_steps);if(!r.misaligned_steps)clean_even++;}
  else{odd_apps+=r.odd_ticks;mis+=r.misaligned_steps;if(r.max_offset>maxoff)maxoff=r.max_offset;
   /* R7: drums and Dillo share one pair grid. With swing off a restart on an odd tick is harmless. */
   if(swings[i]==100)CHECK(!r.misaligned_steps,"swing 50: %u steps off by more than one frame",r.misaligned_steps);
   else{CHECK(!r.misaligned_steps,"R7 defect candidate: odd section length at swing %.1f%%: %u sections applied on an odd tick, "
     "%u drum steps off the Dillo/core grid, up to %u frames (%.1f ms)",swings[i]/2.0,r.odd_ticks,r.misaligned_steps,r.max_offset,r.max_offset*1000.0/44100.0);}
  }
 }
 /* QUEUED -> APPLIED with a live swing change while the next section is queued */
 for(unsigned i=0;i<4;i++){unsigned sd;song_result r=play_song(even,4,174,swings[i],1,&sd);songs++;diffs+=sd;
  sub+=r.subsample_steps;CHECK(!sd&&!r.misaligned_steps,"queued + swing change at %u: %u strikes differ, %u steps off",swings[i],sd,r.misaligned_steps);}
 {unsigned odd=0,lat[3]={0,0,0},bars0=0;collect();for(unsigned i=0;i<sections;i++){const bb_phrase *p=&catalogue[i].phrase;if(p->length&1u)odd++;if(p->lattice<3)lat[p->lattice]++;if(!p->bars)bars0++;}
  printf("N02 catalogue: %u drum parts, %u of odd length, %u without declared bars; lattices 16th/8th/triplet %u/%u/%u\n",sections,odd,bars0,lat[0],lat[1],lat[2]);}
 printf("N02 SONG: %lu songs (%lu with even section lengths clean), queued->applied with a live swing change 4/4; "
  "odd section lengths: %lu hand-offs on odd ticks, %lu drum steps off the Dillo/core grid by more than one frame, max %lu frames; "
  "%lu steps 1 frame off (sub-sample restart anchor, all lengths); strike differences %lu\n",
  songs,clean_even,odd_apps,mis,maxoff,sub,diffs);
}
/* Minimal reproducer of the R7 finding: a two-row SONG, 15 then 16 sixteenths, swing 75 %, 120 BPM. Prints the
 * first four step starts of row 2 for drum layer A (its strikes) and for the core clock that plays Dillo. */
static void n02_repro(void){
 static const unsigned d[2]={15,16};unsigned sd;
 for(unsigned v=0;v<2;v++){
  unsigned x2=v?150:100;song_result r=play_song(d,2,120,x2,0,&sd);(void)r;
  uint64_t apply=audio.section.status.state.applied_frame;
  printf("REPRO swing %.1f%%: row 2 applied on core tick %u (%s of a pair) at frame %" PRIu64 "\n",x2/2.0,
   audio.section.status.applied_tick,audio.section.status.applied_tick&1u?"second step":"first step",apply);
  unsigned shown=0;
  for(unsigned cell=0;cell<6&&shown<4;cell++){
   uint64_t f=0;for(unsigned i=0;i<trn&&!f;i++)if(tr[i].m==0&&tr[i].frame>=apply&&!((tr[i].payload>>23)&3u)&&((tr[i].payload>>5)&63u)==cell)f=tr[i].frame;
   if(!f)continue;unsigned k=audio.section.status.applied_tick+cell;shown++;
   printf("  step %u: drums A %" PRIu64 "  Dillo/core %" PRIu64 "  difference %+lld frames (%+.1f ms)\n",cell,f,core_tick[k],
    (long long)f-(long long)core_tick[k],((double)f-(double)core_tick[k])*1000.0/44100.0);
  }
 }
}
/* ------------------------------------------------------------------ N02: count-in, swung recording */
static unsigned long r_takes,r_hits,r_countins,r_bar,r_offbar,r_refused_swing,r_refused_tempo,r_latch_refusals,r_replayed;
static int record_case(unsigned lattice,unsigned len,unsigned bpm,unsigned x2,unsigned skip,unsigned new_x2){
 bb_phrase A;fresh(bpm,x2);nsch=0;sched_add(0,x2,bpm);block_mode=0;
 make_phrase(&A,lattice,len,0);
 /* leave only every other step sounding so recorded cells are new */
 unsigned w=0;for(unsigned i=0;i<A.count;i++){unsigned cell=(A.events[i]>>5)&63u;if((A.events[i]&31u)==BB_FDV_HH&&(cell&1u))continue;A.events[w++]=A.events[i];}A.count=(uint8_t)w;
 CHECK(submit_phrase(0,&A),"record submit");call(128);push((bb_command){.type=BB_CMD_TRANSPORT,.value=1});
 for(unsigned i=0;i<skip;i++)call(128);
 bb_capture_clock clock;bb_live_record take;memset(&take,0,sizeof take);uint64_t gate=0;
 int r=bb_audio_capture_begin(&audio,0,&clock);CHECK(r==BB_CAPTURE_OK,"capture begin %d (swing %u)",r,x2);if(r)return 0;
 bb_phrase_view view;memset(&view,0,sizeof view);
 CHECK(bb_audio_phrase_view_read(&audio,0,&view)&&view.valid,"view");
 CHECK(bb_pe_take_begin(&take,&view.phrase,&clock,1,&gate)==BB_PE_OK,"take begin");
 unsigned sub=per_beat(lattice),bar=4u*sub,K=4000;unsigned n=ref_steps(0,lattice,K);assert(n==K);
 unsigned e=0;while(e+1<K&&ref_frame[e+1]<=clock.frame)e++;
 CHECK(e%len==clock.source_step,"capture step %u, reference %u",clock.source_step,e%len);
 unsigned want_n=bar-clock.source_step%bar;r_countins++;
 CHECK(gate==ref_frame[e+want_n],"count-in gate %" PRIu64 ", reference step %u starts %" PRIu64,gate,e+want_n,ref_frame[e+want_n]);
 if((e+want_n)%bar==0)r_bar++;else r_offbar++;
 /* five pad hits at awkward places after the gate, one before it (dropped as the firmware does) */
 uint64_t at[6];unsigned cells[6],accepted=0;
 at[0]=gate>clock.frame+200u?gate-150u:0;
 for(unsigned h=1;h<6;h++){unsigned k=e+want_n+h*3u+(h&1u);at[h]=ref_frame[k]+(ref_frame[k+1]-ref_frame[k])*(h*2u+1u)/11u;}
 for(unsigned h=0;h<6;h++){
  if(!at[h]||at[h]<audio.core.frame)continue;
  render_to(at[h]);push((bb_command){.type=BB_CMD_DRUM_TRIGGER,.target=0,.voice=(uint8_t)(h%2?1:0),.value=88});call(1);
  bb_capture_event ev;
  while((r=bb_audio_capture_read(&audio,&ev))==BB_CAPTURE_OK){
   if(ev.frame<gate)continue;
   CHECK(ev.frame==at[h],"pad frame %" PRIu64 " want %" PRIu64,ev.frame,at[h]);
   unsigned before=take.take.count;int hr=bb_live_record_hit(&take,ev.frame,ev.voice,0);
   CHECK(hr==BB_REC_OK||hr==BB_REC_DUPLICATE,"hit %d",hr);
   if(take.take.count>before){
    /* reference nearest cell: exact units, ties forward (R5 positions, recorder "nearest step") */
    u128 pos=0;for(uint64_t f=0;f<ev.frame;f++)pos+=(u128)at_bpm(f)*sub*100u;
    unsigned k=0;while(k+1<K&&ref_units[k+1]<=pos)k++;
    u128 into=pos-ref_units[k],dur=ref_units[k+1]-ref_units[k];unsigned cell=(2u*into>=dur?k+1u:k)%len;
    unsigned got_cell=(take.take.events[before]>>5)&63u;
    CHECK(got_cell==cell,"swing %u lattice %u len %u: hit at %" PRIu64 " recorded on cell %u, reference %u",x2,lattice,len,ev.frame,got_cell,cell);
    cells[accepted++]=cell;r_hits++;
   }
  }
 }
 (void)cells;
 if(new_x2){
  /* swung recording, then the swing changes during the take: the pass must be refused, never re-gridded */
  push((bb_command){.type=BB_CMD_SWING_X2,.value=(uint16_t)new_x2});call(128);
  bb_capture_event ev;r=bb_audio_capture_read(&audio,&ev);
  CHECK(r==BB_CAPTURE_CHANGED,"swing change during a take: read %d",r);if(r==BB_CAPTURE_CHANGED)r_refused_swing++;
  (void)bb_audio_capture_discard(&audio);return 1;
 }
 CHECK(bb_audio_capture_end(&audio)==BB_CAPTURE_OK,"end");CHECK(bb_live_record_finish(&take)==BB_REC_OK,"finish");
 bb_phrase out;CHECK(bb_pe_take(&take,clock.applied,&audio.rich.active[0].phrase,&out)==BB_PE_OK,"keep");
 (void)bb_audio_capture_discard(&audio);r_takes++;
 /* KEEP, then change swing: the kept take replays on the new swung grid like any pattern (R1-R6) */
 static bb_phrase_index index;uint32_t ticket;
 CHECK(bb_phrase_index_prepare(&out,bb_audio_phrase_source_voices(&audio,0),&index)&&bb_audio_phrase_submit_seamless(&audio,0,&index,&ticket),"publish take");
 unsigned other=x2==150?100:150;uint64_t fc=audio.core.frame;push((bb_command){.type=BB_CMD_SWING_X2,.value=(uint16_t)other});sched_add(fc,other,bpm);
 for(unsigned i=0;i<6000&&audio.rich.status[0].applied!=ticket;i++)call(128);
 CHECK(audio.rich.status[0].applied==ticket,"take applied");
 uint64_t from=audio.rich.status[0].applied_frame;
 n=ref_steps(0,lattice,K);unsigned ka=0;while(ref_frame[ka]<from)ka++;
 unsigned kend=ka+2u*len+2u;render_to(ref_frame[kend]+1u);
 nwant=0;want_steps(&out,ka,kend,0);got_strikes(0,ref_frame[ka],ref_frame[kend]);unsigned dup,lost;
 CHECK(!compare_hits(&dup,&lost),"kept take replay at the new swing: %u extra %u missing",dup,lost);r_replayed++;
 return 1;
}
static void n02_record(void){
 static const unsigned xs[6]={100,101,116,125,133,150};
 static const struct {unsigned lattice,len;} shapes[]={{BB_FDL_SIXTEENTHS,16},{BB_FDL_SIXTEENTHS,15},{BB_FDL_EIGHTHS,8},{BB_FDL_TRIPLETS,24},{BB_FDL_SIXTEENTHS,12}};
 unsigned i=0;
 static const unsigned skips[5]={13,58,401,777,1201};   /* blocks of 128 before REC+SEQ: first pass and later passes */
 for(unsigned s=0;s<5;s++)for(unsigned x=0;x<6;x++)for(unsigned j=0;j<5;j++,i++)
  record_case(shapes[s].lattice,shapes[s].len,i%2?174:120,xs[x],skips[j],i%3==0?(xs[x]==150?117:150):0);
 /* a REC+SEQ within a pair after a swing change is refused until the new swing is latched */
 for(unsigned x=1;x<6;x++){
  bb_phrase A;fresh(120,100);make_phrase(&A,BB_FDL_SIXTEENTHS,16,0);(void)submit_phrase(0,&A);call(128);
  push((bb_command){.type=BB_CMD_TRANSPORT,.value=1});for(unsigned k=0;k<40;k++)call(128);
  while(audio.rich.player[0].odd)call(64);                 /* now in a pair's first step */
  push((bb_command){.type=BB_CMD_SWING_X2,.value=(uint16_t)xs[x]});call(1);
  bb_capture_clock clock;int r=bb_audio_capture_begin(&audio,0,&clock);
  CHECK(r==BB_CAPTURE_INVALID,"capture inside the changed pair: %d",r);if(r==BB_CAPTURE_INVALID)r_latch_refusals++;
  if(!r)(void)bb_audio_capture_discard(&audio);
  for(unsigned k=0;k<200&&(audio.rich.player[0].swing!=audio.core.swing||audio.core.active_swing!=audio.core.swing);k++)call(64);
  r=bb_audio_capture_begin(&audio,0,&clock);CHECK(r==BB_CAPTURE_OK,"capture after the latch: %d",r);(void)bb_audio_capture_discard(&audio);
 }
 printf("N02 record: %lu count-ins on the reference (%lu end on a bar line, %lu on a pattern-step line of an odd-length pattern), "
  "%lu swung takes kept, %lu hits on the reference's nearest cell, %lu takes refused on a live swing change, %lu kept takes replayed "
  "at a new swing, %lu captures refused inside a just-changed pair\n",r_countins,r_bar,r_offbar,r_takes,r_hits,r_refused_swing,r_replayed,r_latch_refusals);
}
/* ------------------------------------------------------------------ TAP: what a REC+GLO tempo change does */
static void tap_tests(void){
 unsigned long grid_runs=0,grid_strikes=0,lost_ratchets=0,late_ratchets=0,dillo_cases=0,held_cases=0,queued_cases=0,take_refusals=0;
 /* 1. the step grid: tempo jumps at every change point, both directions, with ratchets */
 static const unsigned pairs[6][2]={{120,180},{180,120},{97,98},{300,30},{30,300},{120,240}};
 for(unsigned t=0;t<6;t++)for(unsigned where=0;where<6;where++)for(unsigned x=0;x<2;x++){
  bb_phrase A;unsigned x2=x?133:100;fresh(pairs[t][0],x2);nsch=0;sched_add(0,x2,pairs[t][0]);block_mode=where%4;
  make_phrase(&A,BB_FDL_SIXTEENTHS,16,1);CHECK(submit_phrase(0,&A),"tap submit");call(128);
  push((bb_command){.type=BB_CMD_TRANSPORT,.value=1});call(1);
  ref_steps(0,BB_FDL_SIXTEENTHS,40);uint64_t fc=change_frame(where,16);
  render_to(fc);uint32_t step_before=audio.rich.player[0].step,tick_before=audio.core.ticks;
  push((bb_command){.type=BB_CMD_TEMPO,.value=(uint16_t)pairs[t][1]});sched_add(fc,x2,pairs[t][1]);call(1);
  CHECK(audio.core.bpm==pairs[t][1],"tempo applied");
  CHECK(audio.core.ticks-tick_before<=1u&&(audio.rich.player[0].step==step_before||audio.rich.player[0].step==(step_before+1u)%16u),"tap repositioned the playhead");
  unsigned K=56;unsigned n=ref_steps(0,BB_FDL_SIXTEENTHS,K+1);assert(n==K+1);render_to(ref_frame[K]+1u);
  nwant=0;want_steps(&A,0,K,0);got_strikes(0,0,ref_frame[K]);
  /* ratchet strikes of the one step that was sounding when the tempo changed were scheduled for the old step
   * length: count them separately (expected behaviour, not a hit-loss on any other step) */
  unsigned kc=0;while(kc+1<=K&&ref_frame[kc+1]<=fc)kc++;
  unsigned dup,lost;compare_hits(&dup,&lost);
  unsigned other=0;
  for(unsigned i=0,j=0;i<nwant||j<ngot;){
   int c=i==nwant?1:j==ngot?-1:hit_cmp(&want[i],&got[j]);
   if(!c){i++;j++;continue;}
   const hit_t *h=c<0?&want[i]:&got[j];
   int in_step=h->kind&&h->frame>=ref_frame[kc]&&h->frame<ref_frame[kc]+(ref_frame[kc+1]-ref_frame[kc])*4u;
   if(!in_step)other++;
   else if(c<0)lost_ratchets++;else late_ratchets++;
   if(c<0)i++;else j++;
  }
  CHECK(!other,"tempo %u->%u where %u swing %u: %u strikes outside the changed step differ",pairs[t][0],pairs[t][1],where,x2,other);
  unsigned bad=0,ticks=core_n<K?core_n:K,first_bad=0;for(unsigned k=0;k<ticks;k++)if(core_tick[k]!=ref_frame[k]){if(!bad)first_bad=k;bad++;}
  CHECK(!bad,"core ticks off after a tempo change %u->%u where %u swing %u block %u: %u, first tick %u at %" PRIu64 " ref %" PRIu64 " change at %" PRIu64,
   pairs[t][0],pairs[t][1],where,x2,block_mode,bad,first_bad,core_tick[first_bad],ref_frame[first_bad],fc);
  grid_runs++;grid_strikes+=nwant;
 }
 /* 2. a Dillo step note sounding when the tempo jumps: it keeps the gate it was given (65 % of the OLD step)
  *    unless the next step comes first; it never hangs past the next step */
 for(unsigned t=0;t<6;t++){
  fresh(pairs[t][0],100);bb_sequence *s=&audio.core.synths[0];s->length=16;s->muted=0;
  for(unsigned k=0;k<16;k++)s->notes[k]=(bb_note){(uint8_t)(36+k),(uint8_t)(k%4==0),0,0};
  push((bb_command){.type=BB_CMD_TRANSPORT,.value=1});
  while(audio.core.ticks<17)call(16);                           /* step 16 (a gated note) has just started */
  uint64_t on=core_tick[16];uint32_t old_step=(uint32_t)(core_tick[16]-core_tick[15]);
  CHECK(audio.dillo.voice.gate,"Dillo note sounding");
  push((bb_command){.type=BB_CMD_TEMPO,.value=(uint16_t)pairs[t][1]});
  uint64_t off=0;while(audio.core.ticks<19){call(1);if(!off&&!audio.dillo.voice.gate)off=audio.core.frame-1u;}
  uint64_t next_tick=core_tick[17];
  uint64_t old_release=on+(old_step/100u)*65u+(old_step%100u)*65u/100u;
  CHECK(off&&off<=next_tick,"Dillo note hangs past the next step: off %" PRIu64 " next %" PRIu64,off,next_tick);
  uint64_t expect_off=old_release<next_tick?old_release:next_tick;
  CHECK(off+1u>=expect_off&&off<=expect_off+1u,
   "Dillo gate ends %" PRIu64 ", old-tempo release %" PRIu64 ", next step %" PRIu64,off,old_release,next_tick);
  dillo_cases++;
 }
 /* 3. a key held on Dillo (keyboard/MIDI) is untouched by a tempo change */
 for(unsigned t=0;t<6;t++){
  fresh(pairs[t][0],100);audio.core.synths[0].muted=0;push((bb_command){.type=BB_CMD_TRANSPORT,.value=1});
  push((bb_command){.type=BB_CMD_SYNTH_KEY,.target=0,.voice=48,.step=0,.value=100});for(unsigned i=0;i<50;i++)call(128);
  CHECK(audio.keys[0].count==1&&audio.dillo.voice.gate,"key held");
  push((bb_command){.type=BB_CMD_TEMPO,.value=(uint16_t)pairs[t][1]});for(unsigned i=0;i<400;i++)call(128);
  CHECK(audio.keys[0].count==1&&audio.dillo.voice.gate,"held key survives a tempo change");
  push((bb_command){.type=BB_CMD_SYNTH_KEY,.target=0,.voice=48,.step=0,.value=0});call(128);
  CHECK(!audio.keys[0].count,"key released");held_cases++;
 }
 /* 4. a queued SONG section keeps its bar: the tempo moves when (in time) it arrives, not where */
 {static const unsigned d[3]={16,16,16};
  for(unsigned t=0;t<6;t++){
   collect();fresh(pairs[t][0],116);nsch=0;sched_add(0,116,pairs[t][0]);block_mode=0;
   assert(!bb_arr_sources_init(&sources,&bb_factory_catalogue));bb_arr_init(&model);
   unsigned base=pick_sixteenths(0);
   for(unsigned i=0;i<3;i++){bb_arr_section part={0};snprintf(part.name,sizeof part.name,"T%u",i);part.duration=(uint16_t)d[i];uint32_t id;
    unsigned sec=pick_sixteenths(base+i);
    assert(!bb_arr_source_bind(&sources,&choices[sec],BB_SOURCE_DRUM,&part.part[0]));
    assert(!bb_arr_source_bind(&sources,&choices[(sec+17)%sections],BB_SOURCE_DRUM,&part.part[1]));
    assert(!bb_arr_section_put(&model,&part,&id));assert(!bb_arr_row_insert(&model,i,(bb_arr_row){id,1}));}
   call(128);assert(!bb_arr_controller_init(&controller,&model,&sources));assert(!bb_arr_controller_start(&controller,&audio));
   int tapped=0;uint32_t due=0,seen=0;unsigned applies=0;uint32_t tick_at[4]={0};uint64_t frame_at[4]={0};
   for(unsigned b=0;b<40000;b++){
    call(128);assert(!bb_arr_controller_poll(&controller,&audio));
    if(!tapped&&applies==1&&audio.section.phase==BB_SECTION_PENDING&&audio.core.ticks>=6){
     due=audio.section.due_tick;uint64_t f=audio.core.frame;push((bb_command){.type=BB_CMD_TEMPO,.value=(uint16_t)pairs[t][1]});
     sched_add(f,116,pairs[t][1]);tapped=1;}
    if(audio.section.status.state.applied!=seen){seen=audio.section.status.state.applied;if(applies<4){tick_at[applies]=audio.section.status.applied_tick;frame_at[applies]=audio.section.status.state.applied_frame;}applies++;}
    if(controller.view.ended&&!audio.core.running)break;
   }
   unsigned n=ref_steps(0,BB_FDL_SIXTEENTHS,49);assert(n==49);
   CHECK(tapped&&applies==3&&tick_at[1]==due&&tick_at[1]==16&&frame_at[1]==ref_frame[16],"queued section: due %u applied at tick %u frame %" PRIu64 " ref %" PRIu64,due,tick_at[1],frame_at[1],ref_frame[16]);
   queued_cases++;
  }}
 /* 5. a take in progress: any tempo change refuses the pass (the engine side; on the unit REC/GLO first
  *    ends the take, bb_record_firmware.inc) */
 for(unsigned x=0;x<4;x++){
  bb_phrase A;static const unsigned xs[4]={100,117,133,150};fresh(120,xs[x]);make_phrase(&A,BB_FDL_SIXTEENTHS,16,0);(void)submit_phrase(0,&A);call(128);
  push((bb_command){.type=BB_CMD_TRANSPORT,.value=1});for(unsigned k=0;k<70;k++)call(128);
  bb_capture_clock clock;int r=bb_audio_capture_begin(&audio,0,&clock);CHECK(r==BB_CAPTURE_OK,"capture %d",r);
  push((bb_command){.type=BB_CMD_TEMPO,.value=125});call(128);bb_capture_event ev;r=bb_audio_capture_read(&audio,&ev);
  CHECK(r==BB_CAPTURE_CHANGED,"tempo change during a take: %d",r);if(r==BB_CAPTURE_CHANGED)take_refusals++;(void)bb_audio_capture_discard(&audio);
 }
 printf("TAP: %lu tempo-jump runs (6 jumps x 6 change points x swing 50/66.5), %lu strikes on the reference, playhead never repositioned; "
  "ratchet strikes of the step sounding at the change: %lu dropped, %lu at the old spacing; %lu Dillo step notes keep the old gate or end "
  "at the next step; %lu held Dillo keys untouched; %lu queued sections apply on their own bar; %lu swung takes refused\n",
  grid_runs,grid_strikes,lost_ratchets,late_ratchets,dillo_cases,held_cases,queued_cases,take_refusals);
}
int main(int argc,char **argv){
 const char *only=argc>1?argv[1]:"";
 if(!*only||!strcmp(only,"matrix"))n02_matrix();
 if(!*only||!strcmp(only,"song"))n02_song();
 if(!*only||!strcmp(only,"record"))n02_record();
 if(!*only||!strcmp(only,"tap"))tap_tests();
 if(!strcmp(only,"repro"))n02_repro();
 printf("%s %lu checks, %lu failures\n",failures?"FAIL":"PASS",checks,failures);
 return failures?1:0;
}

/* BB1 2.0 kit builder: host tests (prepended with cpu1_mock.inc by run.sh). Carried over from
 * claude-code/kit-builder-20 (engine gates 1-10) and extended for the 2.0 firmware paths (gates 11-15):
 * the live BUILD swap, the kit+phrase USER load, swaps while playing, and the paired scene / project round trip.
 * The real bb_audio engine, the candidate's CPU1 rich-job stand-in and the
 * classic-three BBSK pack bound as bb_sample_boot.inc does.
 *   usage: test_user_kit PACK.bbsk [sections]
 * stdout: one RESULT line per gate; "ROUTE kit,requested,played" lines for the
 * substitution comparison against routes.csv (d097e15); exit 0 only if all pass. */
#include "bb_sample_resident_r24.h"
#include "bb_aux_voices_r1.h"
#include "bb_user_kit.h"
#include "sample_scene.h"
#include "bb_voice_fallback.h"
#include "bb_wire.h"
#include "bb_crc32.h"
#include <math.h>

static unsigned fails;
#define CHECK(c,...) do{if(!(c)){fails++;fprintf(stderr,"FAIL %s:%d ",__FILE__,__LINE__);fprintf(stderr,__VA_ARGS__);fputc('\n',stderr);}}while(0)
static uint64_t rng=88172645463325252ull;
static unsigned rnd(unsigned n){rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return (unsigned)(rng%n);}
static const char *const VN[17]={"Bass drum","Snare","Low tom","Middle tom","High tom","Low conga","Middle conga",
 "High conga","Rimshot","Claves","Clap","Maracas","Cowbell","Cymbal","Open hi-hat","Closed hi-hat","Ride"};

/* ---------------- installed machines ---------------- */
static bb_sample_resident store;static bb_aux_voices aux;
static bb_uk_machine inst[9];static unsigned ninst;static char names[8][13];
static uint32_t voices_of(const bb_sample_zone *z,unsigned nz){
 static const uint8_t note[17]={36,38,45,47,50,64,63,62,37,75,39,70,56,49,46,42,51};
 uint32_t v=0;for(unsigned i=0;i<17;i++)for(unsigned j=0;j<nz;j++)if(z[j].note==note[i])v|=1u<<i;return v;
}
static void machines_init(void){
 ninst=0;inst[ninst++]=(bb_uk_machine){bb_uk_key('N',"808",3),BB_UK_ENGINE_808,BB_SAMPLE_808_ID,65535u,0,0};
 for(unsigned k=0;k<3;k++){
  memcpy(names[k],store.names[k],12);names[k][12]=0;
  inst[ninst++]=(bb_uk_machine){bb_uk_key('S',names[k],12),BB_UK_ENGINE_SAMPLE,(uint8_t)k,
   voices_of(store.zones[k],store.counts[k]),store.zones[k],store.counts[k]};
 }
}
static const bb_uk_machine *mach(unsigned i){return &inst[i];} /* 0=808, 1..3 sampled */

/* ---------------- 1. record codec ---------------- */
static void random_kit(bb_user_kit *k){
 static bb_uk_machine pool[12];
 for(unsigned i=0;i<12;i++){pool[i]=(bb_uk_machine){0};pool[i].key=0x1000u+i*7919u;pool[i].engine=i?BB_UK_ENGINE_SAMPLE:BB_UK_ENGINE_808;}
 assert(bb_uk_from_machine(k,&pool[rnd(12)],rnd(2)?"MY KIT":""));
 for(unsigned n=rnd(40);n--;){
  unsigned v=rnd(17);bb_uk_set_source(k,v,&pool[rnd(12)],rnd(17));
  k->v[v].level=(uint8_t)rnd(128);k->v[v].tune=(uint8_t)rnd(128);k->v[v].decay=(uint8_t)rnd(128);
 }
}
static int recrc(uint8_t *p){bb_wire_put32(p+8,~bb_crc32_add(bb_crc32_add(UINT32_MAX,p,8),p+12,108));return 1;}
static void test_codec(void){
 unsigned trips=0,flips=0,flip_refused=0,mut=0,mut_refused=0;
 for(unsigned i=0;i<20000;i++){
  bb_user_kit k,d;uint8_t a[120],b[120];random_kit(&k);
  CHECK(bb_uk_valid(&k),"random kit valid");
  CHECK(bb_uk_encode(&k,a)==120,"encode");memset(&d,0x5a,sizeof d);
  CHECK(bb_uk_decode(a,&d),"decode");CHECK(bb_uk_encode(&d,b)==120&&!memcmp(a,b,120),"re-encode identical");
  CHECK(!memcmp(d.name,k.name,8)&&d.machines==k.machines&&!memcmp(d.key,k.key,sizeof k.key)&&!memcmp(d.v,k.v,sizeof k.v),"fields");
  trips++;
  if(i<300)for(unsigned bit=0;bit<960;bit++){
   uint8_t c[120];memcpy(c,a,120);c[bit>>3]^=(uint8_t)(1u<<(bit&7));bb_user_kit x=d;flips++;
   if(!bb_uk_decode(c,&x)&&!memcmp(&x,&d,sizeof d))flip_refused++;
  }
 }
 /* field mutations with a correct CRC: every one must still refuse */
 bb_user_kit k;assert(bb_uk_from_machine(&k,mach(1),"LINN"));bb_uk_set_source(&k,1,mach(3),1);
 uint8_t a[120];assert(bb_uk_encode(&k,a));
 for(unsigned m=0;m<12;m++){
  uint8_t c[120];memcpy(c,a,120);
  switch(m){
  case 0:c[4]=2;break;                 /* future version */
  case 1:c[6]=1;break;                 /* unknown flag */
  case 2:c[7]=1;break;                 /* reserved */
  case 3:c[5]=0;break;                 /* no machines */
  case 4:c[5]=9;break;                 /* too many */
  case 5:memcpy(c+24,c+20,4);break;    /* duplicate key */
  case 6:bb_wire_put32(c+20,0);break;  /* zero key */
  case 7:c[52]=(uint8_t)(c[52]&7u)|(17u<<3);break; /* voice 17 */
  case 8:c[53]=128;break;              /* level out of range */
  case 9:c[12]=7;break;                /* control character in name */
  case 10:bb_wire_put32(c+28,0x1234);break; /* key beyond count */
  case 11:c[52]=1;c[52+4]=0;break;     /* machine order not canonical */
  }
  recrc(c);bb_user_kit x;mut++;if(!bb_uk_decode(c,&x))mut_refused++;else fprintf(stderr,"mutation %u accepted\n",m);
 }
 /* bank object */
 bb_user_kit s0,s2,out[4];assert(bb_uk_from_machine(&s0,mach(0),"BOOM"));random_kit(&s2);
 const bb_user_kit *slots[4]={&s0,0,&s2,0};uint8_t bank[BB_UK_BANK_BYTES];
 CHECK(bb_uk_bank_encode(slots,bank)==496,"bank encode");
 CHECK(bb_uk_bank_decode(bank,out)==5u,"bank used mask");
 bank[16+2*120+60]^=1;CHECK(bb_uk_bank_decode(bank,out)==1u,"damaged slot 3 alone reads empty");
 bank[6]^=2;CHECK(bb_uk_bank_decode(bank,out)==0u,"damaged header refuses");
 CHECK(flips==flip_refused&&mut==mut_refused,"codec refusals");
 printf("RESULT codec round_trips=%u bit_flips=%u refused=%u field_mutations=%u refused=%u bank=ok\n",trips,flips,flip_refused,mut,mut_refused);
}

/* ---------------- 2. resolution and fallback ---------------- */
static void test_resolve(void){
 bb_uk_resolved r;bb_user_kit k;
 uint32_t auxc=(1u<<17)|(1u<<18);
 /* a. one sampled machine: the substitutions are exactly the installed routes */
 for(unsigned m=1;m<=3;m++){
  CHECK(bb_uk_from_machine(&k,mach(m),""),"from machine");
  CHECK(bb_uk_resolve(&k,inst,ninst,auxc,&r)==BB_UK_OK,"resolve");
  CHECK(!r.missing&&!r.uses808&&r.uses_sample,"all sampled");
  for(unsigned v=0;v<17;v++){
   unsigned played=bb_voice_fallback(v,mach(m)->voices|auxc);
   CHECK(!!(r.substituted&(1u<<v))==(played!=v),"substituted flag");
   if(played!=v)printf("ROUTE %s,%s,%s\n",names[m-1],VN[v],VN[played]);
  }
 }
 /* b. the 808 machine: Ride -> Cymbal, the only 808 kit-voice route */
 CHECK(bb_uk_from_machine(&k,mach(0),""),"808");CHECK(bb_uk_resolve(&k,inst,ninst,auxc,&r)==BB_UK_OK,"808 resolve");
 CHECK(r.substituted==(1u<<16)&&r.arg[16]==13&&!r.uses_sample,"808 RD->CY");
 printf("ROUTE 808 layer A,%s,%s\n",VN[16],VN[13]);
 /* c. a machine that is not installed (Simmons): every voice plays the 808, shown missing */
 bb_uk_machine simmons={bb_uk_key('S',"Simmons",12),BB_UK_ENGINE_SAMPLE,9,0x1f3ffu,0,0};
 CHECK(bb_uk_from_machine(&k,mach(1),"MIXED"),"mixed");
 CHECK(bb_uk_set_source(&k,1,&simmons,1),"SD from Simmons");CHECK(bb_uk_set_source(&k,16,&simmons,16),"RD from Simmons");
 CHECK(k.machines==2,"two machines");
 CHECK(bb_uk_resolve(&k,inst,ninst,auxc,&r)==BB_UK_OK,"missing resolves");
 CHECK(r.missing==((1u<<1)|(1u<<16))&&r.machines_missing==2u,"missing mask %x",r.missing);
 CHECK(r.engine[1]==BB_UK_ENGINE_808&&r.arg[1]==1&&r.engine[16]==BB_UK_ENGINE_808&&r.arg[16]==13,"808 floor");
 CHECK((r.substituted&(1u<<16))&&!(r.substituted&2u),"RD substituted, SD identity");
 CHECK(r.pot_mask==((1u<<1)|(1u<<15))&&r.pot[1][2]==108,"808 Reference pots for missing voices");
 printf("RESULT missing_machine SD,RD from Simmons -> 808 SD, 808 CY (missing=%05x substituted=%05x)\n",r.missing,r.substituted);
 /* d. two voices on one 808 sound with different settings: the later one is flagged shared */
 CHECK(bb_uk_from_machine(&k,mach(0),""),"808");CHECK(bb_uk_set_source(&k,2,mach(0),0),"LT<-808 BD");
 k.v[2].tune=90;CHECK(bb_uk_resolve(&k,inst,ninst,auxc,&r)==BB_UK_OK,"shared");
 CHECK(r.shared808==(1u<<2)&&r.pot[0][1]==64,"BD keeps its own tune, LT flagged shared");
 /* e. tune and decay arithmetic on a sampled voice */
 CHECK(bb_uk_from_machine(&k,mach(1),""),"linn");
 bb_uk_resolved r0;CHECK(bb_uk_resolve(&k,inst,ninst,auxc,&r0)==BB_UK_OK,"r0");
 k.v[0].tune=64+48;k.v[1].tune=64-48;k.v[15].decay=32;
 CHECK(bb_uk_resolve(&k,inst,ninst,auxc,&r)==BB_UK_OK,"tuned");
 const bb_sample_zone *bd=0,*bd0=0,*sd=0,*sd0=0,*hh=0,*hh0=0;
 for(unsigned i=0;i<r.nz;i++){if(r.zones[i].note==r.arg[0])bd=&r.zones[i];if(r.zones[i].note==r.arg[1])sd=&r.zones[i];if(r.zones[i].note==r.arg[15])hh=&r.zones[i];}
 for(unsigned i=0;i<r0.nz;i++){if(r0.zones[i].note==r0.arg[0])bd0=&r0.zones[i];if(r0.zones[i].note==r0.arg[1])sd0=&r0.zones[i];if(r0.zones[i].note==r0.arg[15])hh0=&r0.zones[i];}
 CHECK(bd&&bd0&&bd->rate==bd0->rate*2u,"+12 st doubles rate");CHECK(sd&&sd0&&sd->rate*2u==sd0->rate,"-12 st halves rate");
 CHECK(hh&&hh0&&hh->frames<hh0->frames&&hh->frames>=hh0->frames*32u/127u,"decay shortens to a zero crossing");
 printf("RESULT tune_decay BD rate %u->%u, SD %u->%u, CH frames %u->%u\n",bd0->rate,bd->rate,sd0->rate,sd->rate,hh0->frames,hh->frames);
 /* f. zone cap: 17 distinct sampled sounds refuse, the kit is unchanged */
 CHECK(bb_uk_from_machine(&k,mach(1),""),"linn");for(unsigned v=0;v<17;v++){k.v[v].voice=0;k.v[v].tune=(uint8_t)(40+v);}
 CHECK(bb_uk_resolve(&k,inst,ninst,auxc,&r)==BB_UK_FULL_ZONES,"17 distinct sounds refuse");
 /* g. a 9th machine is refused by the editor */
 CHECK(bb_uk_from_machine(&k,mach(0),""),"808");bb_uk_machine many[9];
 for(unsigned i=0;i<9;i++){many[i]=*mach(1);many[i].key=0x9000u+i;}
 unsigned ok=0;for(unsigned i=0;i<9;i++)ok+=bb_uk_set_source(&k,i,&many[i],0);
 CHECK(ok==7&&k.machines==8,"8 machines max (808 + 7)");
 /* h. placement guard */
 {bb_user_kit a,b,c,d;bb_uk_resolved ra,rb,rc,rd;
  assert(bb_uk_from_machine(&a,mach(0),"")&&bb_uk_from_machine(&b,mach(1),"")&&bb_uk_from_machine(&c,mach(0),"")&&bb_uk_from_machine(&d,mach(0),""));
  a.v[0].tune=90;                                   /* 808 with edited pots: native */
  assert(bb_uk_set_source(&c,0,mach(1),0));          /* 808 + LinnDrum BD: mixed */
  assert(bb_uk_set_source(&d,2,mach(0),0));          /* 808 with BD on the LT lane: remapped */
  assert(!bb_uk_resolve(&a,inst,ninst,auxc,&ra)&&!bb_uk_resolve(&b,inst,ninst,auxc,&rb)&&
         !bb_uk_resolve(&c,inst,ninst,auxc,&rc)&&!bb_uk_resolve(&d,inst,ninst,auxc,&rd));
  CHECK(bb_uk_native808(&ra)&&bb_uk_placement(&ra)==BB_UK_PLACE_ANY,"808 pots: anywhere, CPU1 kept");
  CHECK(!bb_uk_native808(&rb)&&bb_uk_placement(&rb)==BB_UK_PLACE_ANY,"sampled only: anywhere");
  CHECK(bb_uk_placement(&rc)==BB_UK_PLACE_A_ONLY,"mixed: A only");
  CHECK(bb_uk_placement(&rd)==BB_UK_PLACE_A_ONLY,"remapped 808: A only");
  printf("RESULT placement 808-pots=ANY sampled-only=ANY mixed=A_ONLY remapped-808=A_ONLY\n");}
 printf("RESULT resolve gates done\n");
}

/* ---------------- 2b. scenes: v3 untouched, v4 carries the kit ---------------- */
static bb_family_scene_workspace ws;
static void test_scene(bb_audio *a){
 static bb_family_scene sc;memset(&sc,0,sizeof sc);sc.core=a->core;
 CHECK(bb_family_scene_valid(&sc),"scene valid");
 uint8_t mix[16],mix2[16];memset(mix,127,16);mix[3]=100;
 const uint32_t H=0x1234abcdu,P=0x55aa0f0fu;
 bb_sample_scene_identity id[2]={{H,P,0.5f,1,64,48,0},{H,P,0.5f,0,64,48,1}},got[2],old[2];
 static uint8_t v3[4096],v3b[4096],v4[4096];bb_user_kit uk[2],k;unsigned mask=9;
 size_t n3=bb_sample_pair_scene_encode(&sc,mix,id,v3,sizeof v3);
 const bb_user_kit *none[2]={0,0};
 size_t n3b=bb_sample_user_scene_encode(&sc,mix,id,none,v3b,sizeof v3b);
 CHECK(n3&&n3==n3b&&!memcmp(v3,v3b,n3),"no user kit: byte-identical v3");
 CHECK(bb_sample_user_scene_decode(v3,n3,44100,H,P,3,&ws,mix2,got,uk,&mask)&&mask==0,"v3 through the new decoder");
 CHECK(bb_sample_pair_scene_decode(v3,n3,44100,H,P,3,&ws,mix2,old)&&!memcmp(old,got,sizeof old),"same identities as v3 decoder");
 assert(bb_uk_from_machine(&k,mach(0),"MYKIT")&&bb_uk_set_source(&k,0,mach(1),0)&&bb_uk_set_source(&k,1,mach(3),1));
 bb_sample_scene_identity id4[2]={{H,P,0.5f,1,64,48,2},{H,P,0.5f,0,64,48,1}};
 const bb_user_kit *u[2]={&k,0};
 size_t n4=bb_sample_user_scene_encode(&sc,mix,id4,u,v4,sizeof v4);
 CHECK(n4==n3+120&&n4<=BB_SAMPLE_USER_SCENE_BYTES,"v4 size %zu",n4);
 memset(uk,0,sizeof uk);
 CHECK(bb_sample_user_scene_decode(v4,n4,44100,H,P,3,&ws,mix2,got,uk,&mask)&&mask==1&&got[0].kind==2&&
       !memcmp(uk[0].key,k.key,sizeof k.key)&&!memcmp(uk[0].v,k.v,sizeof k.v)&&!memcmp(mix,mix2,16),"v4 round trip");
 CHECK(!bb_sample_pair_scene_decode(v4,n4,44100,H,P,3,&ws,mix2,old),"a 1.5 decoder refuses v4");
 unsigned refused=0;
 for(unsigned i=0;i<n4;i+=7){uint8_t save=v4[i];v4[i]^=0x40;refused+=!bb_sample_user_scene_decode(v4,n4,44100,H,P,3,&ws,mix2,got,uk,&mask);v4[i]=save;}
 CHECK(refused==(n4+6)/7,"every corrupted v4 byte refuses");
 printf("RESULT scenes v3_bytes=%zu identical_without_user_kit=yes v4_bytes=%zu (+120 per USER layer, bound %u <= 3840) 1.5_decoder_refuses_v4=yes corrupt_refused=%u/%zu\n",
        n3,n4,(unsigned)BB_SAMPLE_USER_SCENE_BYTES,refused,(n4+6)/7);
}
/* ---------------- 3. rendering ---------------- */
static uintptr_t enter_fn(void *x){(void)x;return 0;}
static void leave_fn(void *x,uintptr_t t){(void)x;(void)t;}
static bb_audio audio;static gg_state groups[BB_MACHINES];
#define MAXSEC 1024
static bb_phrase_index catalogue[MAXSEC];static unsigned nsec;
static bb_factory_choice choices[MAXSEC];
static bb_sample_audio_binding current,entries[3],bbind;static bb_sample_slot bslots[BB_SAMPLE_OWNER_SLOTS];
static bb_uk_resolved res[2];
static void push(bb_command c){assert(bb_command_push(&audio.commands,&c));}
/* How a user kit reaches drum A: 0 = the prototype's cold install, 1 = the firmware's BUILD swap
 * (bb_audio_user_kit_swap_owned, always the USER id), 2 = the firmware's USER load (staged table +
 * kit+phrase request with BB_SAMPLE_USER_ID, installed at the boundary). */
static unsigned uk_path;
enum{K808=BB_SAMPLE_808_ID,KOFF=255};
/* kit < 3: catalogue kit; 808; 100+n: user kit n (resolved in res[layer]) */
static int setup(unsigned s,unsigned ka,unsigned kb,const bb_user_kit *ua,const bb_user_kit *ub){
 memset(&bb_c1_live,0,sizeof bb_c1_live);bb_c1_live.ready=1;bb_c1_retired=BB_C1_CON=0;
 memset(&bb_c1_mail,0,sizeof bb_c1_mail);bb_c1_rich_active=0;fail_wait=fail_publish=run_failed=inject_next=0;
 memset(&bb_coefficient_trace,0,sizeof bb_coefficient_trace);memset(groups,0,sizeof groups);
 assert(bb_audio_init(&audio,enter_fn,leave_fn,0));assert(bb_audio_groups_bind(&audio,groups,BB_MACHINES));
 memset(store.slots,0,sizeof store.slots);aux.ready=0;memset(aux.slots,0,sizeof aux.slots);
 assert(bb_aux_voices_bind(&aux,store.zones[3],BB_AUX_ZONES));
 for(unsigned i=0;i<3;i++)assert(bb_sample_resident_binding(&store,i,&entries[i]));
 unsigned a0=ka<3?ka:K808,b0=kb<3?kb:K808;
 current=entries[a0<3?a0:0];
 assert(bb_audio_sample_startup(&audio,&current,entries,3,a0));
 memset(bslots,0,sizeof bslots);bbind=entries[b0<3?b0:0];bbind.slots=bslots;
 assert(bb_audio_sample_bind_b(&audio,&bbind,b0));
 assert(bb_audio_aux_bind(&audio,&aux));
 static const unsigned bpm[4]={120,96,174,140},swing[4]={50,63,75,58};
 assert(bb_set_tempo(&audio.core,bpm[s%4]));assert(bb_set_swing(&audio.core,swing[s%4]));
 for(unsigned m=0;m<2;m++){audio.core.drums[m].enabled=1;audio.core.drums[m].muted=0;audio.core.drums[m].level=(uint8_t)(100+m*20);}
 if(kb==KOFF)audio.core.drums[1].enabled=0;
 audio.core.synths[0].muted=1;
 const bb_user_kit *u[2]={ua,ub};
 for(unsigned m=0;m<2;m++)if(u[m]){
  if(bb_uk_resolve(u[m],inst,ninst,bb_audio_aux_capabilities(&audio),&res[m])!=BB_UK_OK)return 0;
  if(uk_path==2&&!m){audio.user_staged[0]=&res[0];continue;}
  if(!(uk_path==1?bb_audio_user_kit_swap_owned(&audio,m,&res[m]):bb_audio_user_kit_install_owned(&audio,m,&res[m])))return 0;
 }
 return 1;
}
static uint64_t fnv(uint64_t h,const void *p,size_t n){const unsigned char *b=p;for(size_t i=0;i<n;i++)h=(h^b[i])*1099511628211ull;return h;}
typedef struct{uint64_t digest;double peak,energy;unsigned errors,refused;uint32_t rejected,hits[2],worker_b;}run_t;
static unsigned long worker_jobs_seen;
static void render_into(run_t *r){
 unsigned jobs=jobs_run;
 if(!bb_audio_render(&audio,128))r->errors++;
 if(jobs_run!=jobs)r->worker_b++;
 r->digest=fnv(r->digest,audio.left,sizeof audio.left);r->digest=fnv(r->digest,audio.right,sizeof audio.right);
 r->digest=fnv(r->digest,audio.echo_send,sizeof audio.echo_send);r->digest=fnv(r->digest,audio.spring_send,sizeof audio.spring_send);
 for(unsigned i=0;i<128;i++){double l=fabs(audio.left[i]),q=fabs(audio.right[i]);if(l>r->peak)r->peak=l;if(q>r->peak)r->peak=q;
  r->energy+=(double)audio.left[i]*audio.left[i]+(double)audio.right[i]*audio.right[i];}
}
static unsigned ref_kitphrase;   /* the reference also loads its kit with its phrase (kit+phrase, its own ID) */
static int run_section(unsigned s,unsigned ka,unsigned kb,const bb_user_kit *ua,const bb_user_kit *ub,unsigned calls,run_t *r){
 memset(r,0,sizeof *r);r->digest=1469598103934665603ull;
 if(!setup(s,ka,kb,ua,ub))return 0;
 uint32_t t;
 if(uk_path==2&&ua){if(!bb_audio_phrase_submit_launch(&audio,0,BB_SAMPLE_USER_ID,&catalogue[s],16,0,&t))r->refused++;}
 else if(ref_kitphrase&&!ua){if(!bb_audio_phrase_submit_launch(&audio,0,ka<3?ka:K808,&catalogue[s],16,0,&t))r->refused++;}
 else if(!bb_audio_phrase_submit(&audio,0,&catalogue[s],16,&t))r->refused++;
 if(kb!=KOFF&&!bb_audio_phrase_submit(&audio,1,&catalogue[(s*7u+3u)%nsec],16,&t))r->refused++;
 render_into(r);
 push((bb_command){.type=BB_CMD_TRANSPORT,.value=1});
 for(unsigned i=0;i<calls;i++){
  if(i==90)push((bb_command){.type=BB_CMD_DRUM_LEVEL,.target=1,.value=60});
  if(i==140)push((bb_command){.type=BB_CMD_TEMPO,.value=(uint16_t)(audio.core.bpm+7u)});
  render_into(r);
 }
 push((bb_command){.type=BB_CMD_TRANSPORT,.value=0});
 for(unsigned k=0;k<120;k++)render_into(r);
 r->rejected=audio.sample_rejected;r->hits[0]=audio.hits[0];r->hits[1]=audio.hits[1];
 worker_jobs_seen+=r->worker_b;
 return 1;
}
/* Compare a user kit against a reference kit over every catalogue section. */
static void compare(const char *label,unsigned ka,unsigned kb,const bb_user_kit *ua,const bb_user_kit *ub,
                    unsigned ra,unsigned rb,unsigned calls,unsigned sections){
 unsigned same=0,n=0,err=0,refused=0,hitdiff=0;unsigned long wu=0,wr=0;
 for(unsigned s=0;s<sections;s++){
  run_t u,ref;
  CHECK(run_section(s,ka,kb,ua,ub,calls,&u),"%s user setup",label);
  CHECK(run_section(s,ra,rb,0,0,calls,&ref),"%s ref setup",label);
  n++;same+=u.digest==ref.digest;err+=u.errors+ref.errors;refused+=u.refused+ref.refused;
  hitdiff+=u.hits[0]!=ref.hits[0]||u.hits[1]!=ref.hits[1];wu+=u.worker_b;wr+=ref.worker_b;
  if(u.digest!=ref.digest&&n-same<=3)fprintf(stderr,"%s: section %u differs (peak %.6g vs %.6g)\n",label,s,u.peak,ref.peak);
 }
 CHECK(same==n&&!err&&!refused&&!hitdiff,"%s identical",label);
 printf("RESULT identity %-34s sections=%u bit_identical=%u render_errors=%u refused=%u hit_count_diffs=%u cpu1_B_jobs user=%lu ref=%lu\n",
        label,n,same,err,refused,hitdiff,wu,wr);
}
/* One pad (legacy lane trigger), transport stopped: user kit lane vs source kit lane. */
static uint64_t pad(unsigned ka,const bb_user_kit *ua,unsigned lane,double *peak){
 run_t r;memset(&r,0,sizeof r);r.digest=1469598103934665603ull;
 assert(setup(0,ka,KOFF,ua,0));
 push((bb_command){.type=BB_CMD_DRUM_TRIGGER,.target=0,.voice=(uint8_t)lane,.value=110});
 for(unsigned i=0;i<400;i++)render_into(&r);
 *peak=r.peak;return r.digest;
}
static void test_pads(void){
 /* Mixed kit: BD LinnDrum, SD DMX, LT DrumTraks (its MT, a substitute), CH/OH DrumTraks,
  * CP 808, CB 808, CY LinnDrum, the rest from the 808. */
 bb_user_kit k;assert(bb_uk_from_machine(&k,mach(0),"MIXED"));
 assert(bb_uk_set_source(&k,0,mach(1),0)&&bb_uk_set_source(&k,1,mach(3),1)&&bb_uk_set_source(&k,2,mach(2),2)&&
        bb_uk_set_source(&k,15,mach(2),15)&&bb_uk_set_source(&k,14,mach(2),14)&&bb_uk_set_source(&k,13,mach(1),13));
 static const uint8_t lane_voice[11]={0,1,2,3,4,8,10,12,13,14,15};
 static const char *const LN[11]={"BD","SD","LT","MT","HT","RS","CP","CB","CY","OH","CH"};
 unsigned same=0;
 for(unsigned lane=0;lane<11;lane++){
  unsigned v=lane_voice[lane],src=k.v[v].machine;uint32_t key=k.key[src];unsigned ref=K808;
  for(unsigned m=1;m<=3;m++)if(mach(m)->key==key)ref=mach(m)->id;
  /* the reference plays the SOURCE voice on its own kit: use the lane that carries it */
  /* A native sampled kit refuses a pad for a lane it lacks; the user kit plays the
   * resolved substitute, so the reference is the substitute's own lane. */
  unsigned sv=k.v[v].voice;if(ref!=K808)sv=bb_voice_fallback(sv,mach(ref+1)->voices);
  unsigned rlane=lane;for(unsigned l=0;l<11;l++)if(lane_voice[l]==sv)rlane=l;
  double pu,pr;uint64_t du=pad(K808,&k,lane,&pu),dr=pad(ref,0,rlane,&pr);
  int eq=du==dr;same+=eq;
  printf("PAD lane %-2s from %-9s plays %-12s user_peak=%.6f ref_peak=%.6f %s\n",LN[lane],
         ref==K808?"808":names[ref],VN[sv],pu,pr,eq?"IDENTICAL":"DIFFERS");
 }
 printf("RESULT pads mixed_kit lanes=11 identical_to_source_lane=%u\n",same);
}
/* ---------------- 4. cost bench (run under callgrind --toggle-collect=bb_audio_render) ----------------
 * One pad at velocity 110 on layer A, transport stopped, layer B off, Dillo muted; then CALLS
 * render calls of 128 frames. Scenario 0/8 = idle layers (subtract as the base). */
static void bench(unsigned sc,unsigned calls){
 bb_user_kit k;unsigned ka=K808,lane=0;const bb_user_kit *u=0;
 switch(sc){
 case 0:break;                                   /* idle native 808 layer */
 case 1:lane=0;break;                            /* 808 BD */
 case 2:ka=0;lane=0;break;                       /* LinnDrum BD, native sampled kit (half-rate fast path) */
 case 3:assert(bb_uk_from_machine(&k,mach(0),"")&&bb_uk_set_source(&k,0,mach(1),0));u=&k;break; /* USER: BD<-LinnDrum */
 case 4:assert(bb_uk_from_machine(&k,mach(1),"")&&bb_uk_set_source(&k,0,mach(0),0));u=&k;break; /* USER: BD<-808 */
 case 5:assert(bb_uk_from_machine(&k,mach(0),"")&&bb_uk_set_source(&k,0,mach(1),0));k.v[0].tune=76;u=&k;break; /* tuned +3 st */
 case 6:lane=8;break;                            /* 808 CY */
 case 7:assert(bb_uk_from_machine(&k,mach(1),"")&&bb_uk_set_source(&k,13,mach(0),13));lane=8;u=&k;break; /* USER: CY<-808 */
 case 8:assert(bb_uk_from_machine(&k,mach(0),"")&&bb_uk_set_source(&k,0,mach(1),0));u=&k;sc=99;break; /* idle USER layer */
 case 9:ka=0;sc=99;break;                        /* idle sampled layer */
 case 10:ka=0;break;                             /* LinnDrum BD SD OH CH (4 sampled voices) */
 case 11:break;                                  /* 808 BD SD OH CH */
 case 12:assert(bb_uk_from_machine(&k,mach(1),"")&&bb_uk_set_source(&k,1,mach(0),1)&&bb_uk_set_source(&k,14,mach(0),14));u=&k;break;
                                                 /* USER: BD,CH LinnDrum + SD,OH 808 */
 }
 static const uint8_t four[4]={0,1,9,10};
 assert(setup(0,ka,KOFF,u,0));
 for(unsigned i=0;i<4;i++)assert(bb_audio_render(&audio,128));   /* settle */
 if(sc>=10)for(unsigned i=0;i<4;i++)push((bb_command){.type=BB_CMD_DRUM_TRIGGER,.target=0,.voice=four[i],.value=110});
 else if(sc&&sc!=99)push((bb_command){.type=BB_CMD_DRUM_TRIGGER,.target=0,.voice=(uint8_t)lane,.value=110});
 for(unsigned i=0;i<calls;i++)assert(bb_audio_render(&audio,128));
 printf("bench scenario %u calls %u hits %u\n",sc,calls,audio.hits[0]);
}
/* ---------------- 13. BUILD edits while playing: swaps every 37 blocks between two different tables ---------------- */
static void test_live_swaps(const bb_user_kit *mixed,unsigned sections){
 static bb_uk_resolved t[2];bb_user_kit other=*mixed;other.v[0].tune=90;other.v[1].decay=40;other.v[13].level=60;
 unsigned err=0,refused=0,swaps=0,n=sections<60?sections:60;
 assert(!bb_uk_resolve(mixed,inst,ninst,(1u<<17)|(1u<<18),&t[0])&&!bb_uk_resolve(&other,inst,ninst,(1u<<17)|(1u<<18),&t[1]));
 for(unsigned s=0;s<n;s++){
  run_t r;memset(&r,0,sizeof r);r.digest=1;
  assert(setup(s,K808,K808,0,0));uint32_t tk;
  if(!bb_audio_phrase_submit(&audio,0,&catalogue[s],16,&tk))refused++;
  push((bb_command){.type=BB_CMD_TRANSPORT,.value=1});
  for(unsigned i=0;i<300;i++){
   if(i%37==5){audio.commands.enter(0);err+=!bb_audio_user_kit_swap_owned(&audio,0,&t[(i/37)&1u]);audio.commands.leave(0,0);swaps++;}
   render_into(&r);
  }
  err+=r.errors;
 }
 CHECK(!err&&!refused,"live swaps");
 printf("RESULT live_swaps sections=%u swaps=%u render_errors=%u refused=%u\n",n,swaps,err,refused);
}
/* ---------------- 14/15. paired scenes and projects through the firmware's own codec (dual-sample-platform-r1.inc) */
#define bb_sound audio
#include "single-drum-platform-r1.inc"
#include "dual-sample-platform-r1.inc"
#include "bb_project.h"
static bb_pair_stage stage;static uint8_t wire[BB_PAIR_WIRE_BYTES],wire2[BB_PAIR_WIRE_BYTES];
static bb_sample_audio_binding ea[3],eb[3];
static const bb_single_pause paused={1,1,1,0,0};
static const uint32_t PH=0x1234abcdu,PP=0x55aa0f0fu;
/* capture drum A's state (a USER kit if u) with the section's phrases, then encode as the session would */
static size_t scene_of(unsigned s,unsigned ka,const bb_user_kit *u,unsigned origin,uint8_t *out){
 uk_path=1;assert(setup(s,ka,K808,u,0));uk_path=0;
 uint32_t t;assert(bb_audio_phrase_submit(&audio,0,&catalogue[s],16,&t));
 assert(bb_audio_phrase_submit(&audio,1,&catalogue[(s*7u+3u)%nsec],16,&t));
 run_t r;memset(&r,0,sizeof r);render_into(&r);
 if(u){bb_uk.draft=*u;bb_uk.origin=(uint8_t)origin;}
 if(bb_pair_capture(&audio,&stage.snapshot,&paused,PH,PP)!=BB_SINGLE_OK)return 0;
 if(stage.snapshot.identities[0].kind==2)stage.snapshot.identities[0].kit=(uint8_t)origin;
 return bb_pair_encode(&stage.snapshot,out,BB_PAIR_WIRE_BYTES);
}
/* decode and restore into a fresh engine (drum A on the 808), as bb_pair_session_restore does, then play */
static int restore_play(const uint8_t *w,size_t n,unsigned s,run_t *r){
 memset(r,0,sizeof *r);r->digest=1469598103934665603ull;
 assert(setup(s,K808,K808,0,0));
 if(!bb_pair_decode(w,n,&stage,PH,PP,3))return 0;
 for(unsigned i=0;i<3;i++){ea[i]=entries[i];eb[i]=entries[i];eb[i].slots=bslots;}
 int user=bb_uk_scene_prepare();if(user<0)return 0;
 if(bb_pair_restore(&audio,&stage.snapshot,&paused,ea,eb,3,PH,PP)!=BB_SINGLE_OK)return 0;
 if(!bb_uk_scene_installed(user,stage.snapshot.identities[0].kit))return 0;
 push((bb_command){.type=BB_CMD_TRANSPORT,.value=1});
 for(unsigned i=0;i<300;i++)render_into(r);
 return 1;
}
static int project_check(void *c,const void *w,size_t n){(void)c;return bb_pair_decode(w,n,&stage,PH,PP,3);}
static void test_pair_scenes(const bb_user_kit *linn,const bb_user_kit *mixed,unsigned sections){
 unsigned n=sections<40?sections:40,same=0,refused_v4_by_15=0,proj=0;size_t b3=0,b4=0;
 for(unsigned i=0;i<ninst;i++)bb_uk.m[i]=inst[i];   /* the firmware's machine table (bb_uk_boot) */
 bb_uk.nm=(uint8_t)ninst;
 for(unsigned s=0;s<n;s++){
  size_t n3=scene_of(s,0,0,0,wire),n4=scene_of(s,K808,linn,2,wire2);
  CHECK(n3&&n4,"scene encode %u",s);if(!n3||!n4)continue;
  b3=n3;b4=n4;
  run_t r3,r4;
  CHECK(restore_play(wire,n3,s,&r3),"restore v3 %u",s);CHECK(restore_play(wire2,n4,s,&r4),"restore v4 %u",s);
  CHECK(bb_uk.scene_mask==1&&bb_uk.live&&bb_uk.origin==2&&!memcmp(&bb_uk.draft,linn,sizeof *linn),"v4 restores the copy");
  same+=r3.digest==r4.digest&&!r3.errors&&!r4.errors;
  /* a 1.5 build (no USER decoder) refuses the v4 scene rather than misreading it */
  {const void *in,*base;size_t ib,bb;uint8_t mx[16];bb_sample_scene_identity ids[2];
   assert(bb_rich_scene_open(wire2,n4,stage.snapshot.phrases,&stage.snapshot.rich_mask,&in,&ib)&&
          bb_expression_scene_open(in,ib,stage.snapshot.expression,&base,&bb));
   refused_v4_by_15+=!bb_sample_pair_scene_decode(base,bb,44100,PH,PP,3,&stage.decode,mx,ids);}
 }
 CHECK(same==n,"USER(LinnDrum) scene restores as the LinnDrum scene");
 /* the mixed kit: its copy, origin "draft" (7), round-trips byte for byte and plays */
 size_t nm=scene_of(0,K808,mixed,BB_UK_ORIGIN_DRAFT,wire2);run_t rm;
 CHECK(nm&&restore_play(wire2,nm,0,&rm)&&!rm.errors&&bb_uk.origin==BB_UK_ORIGIN_DRAFT&&!memcmp(&bb_uk.draft,mixed,sizeof *mixed),"mixed v4");
 /* a scene naming a machine the library lacks still loads (the 808 stands in); a scene whose drum B is USER refuses */
 bb_uk_machine simmons={bb_uk_key('S',"Simmons",12),BB_UK_ENGINE_SAMPLE,9,0x1f3ffu,0,0};
 bb_user_kit gone;assert(bb_uk_from_machine(&gone,&simmons,"GONE"));
 size_t ng=scene_of(1,K808,linn,1,wire2);
 /* swap the record for the missing-machine kit: re-encode with the draft replaced */
 bb_uk.draft=gone;ng=bb_pair_encode(&stage.snapshot,wire2,BB_PAIR_WIRE_BYTES);
 run_t rg;CHECK(ng&&restore_play(wire2,ng,1,&rg)&&!rg.errors&&bb_uk.miss==0x1ffffu,"missing machine scene loads, 808 stands in");
 /* project (BBP2): the v4 scene nests opaquely and comes back byte for byte, then restores */
 {
  static bb_project pr,op;static uint8_t file[BB_PROJECT_MAX_BYTES];size_t fb=0;const void *nested;size_t nn;
  size_t n4=scene_of(3,K808,mixed,1,wire2);
  memset(&pr,0,sizeof pr);bb_arr_init(&pr.arrangement);assert(!bb_arr_sources_init(&pr.sources,&bb_factory_catalogue));
  bb_project_assets as={PH,PP};
  int e=bb_project_encode(&pr,as,wire2,n4,project_check,0,file,sizeof file,&fb);
  int o=e?e:bb_project_open_staged(file,fb,&bb_factory_catalogue,as,project_check,0,&op,&nested,&nn);
  run_t rp;
  proj=!e&&!o&&nn==n4&&!memcmp(nested,wire2,n4)&&restore_play((const uint8_t *)nested,nn,3,&rp)&&!rp.errors&&
       !memcmp(&bb_uk.draft,mixed,sizeof *mixed)&&bb_uk.origin==1;
  CHECK(proj,"project round trip (encode %d open %d)",e,o);
 }
 printf("RESULT scenes_v4 sections=%u v3_bytes=%zu v4_bytes=%zu user(LinnDrum)_restores_as_LinnDrum=%u/%u v4_refused_by_v3_decoder=%u/%u mixed_round_trip=%s missing_machine_scene=%s project_round_trip=%s\n",
        n,b3,b4,same,n,refused_v4_by_15,n,nm?"ok":"FAIL",ng?"ok":"FAIL",proj?"ok":"FAIL");
}
int main(int argc,char **argv){
 static uint8_t pack[1<<20];FILE *f=fopen(argv[1],"rb");assert(f);size_t bytes=fread(pack,1,sizeof pack,f);fclose(f);
 bytes=bb_fsmp_u32(pack+8);
 assert(bb_sample_resident_prepare(&store,pack,bytes)&&store.count==4);
 machines_init();
 if(argc>3&&!strcmp(argv[2],"bench")){bench((unsigned)atoi(argv[3]),argc>4?(unsigned)atoi(argv[4]):1u);return 0;}
 for(unsigned m=1;m<=3;m++)fprintf(stderr,"machine %u %s voices %05x zones %u\n",m,names[m-1],mach(m)->voices,mach(m)->nz);
 test_codec();test_resolve();
 assert(bb_factory_init());
 for(unsigned family=0;family<2;family++){
  bb_browser browser;assert(bb_browser_init(&browser,&bb_factory_catalogue));assert(bb_browser_group(&browser,family));
  for(unsigned bank=0;bank<bb_browser_bank_count(&browser);bank++){
   browser.bank_index=bank;browser.part_index=0;
   for(unsigned part=0;part<bb_browser_part_count(&browser);part++){
    browser.part_index=part;bb_factory_choice choice;assert(bb_browser_current(&browser,&choice));
    choices[nsec]=choice;
    assert(bb_factory_prepare_rich_drum(&bb_factory_catalogue,&choice,&catalogue[nsec])==BB_FDRUM_OK);nsec++;
   }}}
 unsigned sections=argc>2?(unsigned)atoi(argv[2]):nsec;if(sections>nsec)sections=nsec;
 fprintf(stderr,"sections %u of %u\n",sections,nsec);
 assert(setup(0,K808,K808,0,0));test_scene(&audio);
 test_pads();
 bb_user_kit uk[4];for(unsigned m=0;m<4;m++)assert(bb_uk_from_machine(&uk[m],mach(m),""));
 /* every lane from one machine: the user kit must render exactly as that machine's kit */
 for(unsigned m=1;m<=3;m++){
  char l[64];snprintf(l,sizeof l,"A=user(%s) B=808",names[m-1]);compare(l,K808,K808,&uk[m],0,m-1,K808,300,sections);
  snprintf(l,sizeof l,"A=808 B=user(%s)",names[m-1]);compare(l,K808,K808,0,&uk[m],K808,m-1,300,sections);
 }
 compare("A=user(808) B=user(808)",K808,K808,&uk[0],&uk[0],K808,K808,300,sections);
 /* a kit that references an uninstalled machine plays the 808 floor */
 bb_uk_machine simmons={bb_uk_key('S',"Simmons",12),BB_UK_ENGINE_SAMPLE,9,0x1f3ffu,0,0};
 bb_user_kit gone;assert(bb_uk_from_machine(&gone,&simmons,"GONE"));
 compare("A=user(Simmons, missing) B=808",K808,K808,&gone,0,K808,K808,300,sections);
 /* mixed kit on both layers: plays every section with no error/refusal */
 bb_user_kit mix;assert(bb_uk_from_machine(&mix,mach(0),"MIXED"));
 assert(bb_uk_set_source(&mix,0,mach(1),0)&&bb_uk_set_source(&mix,1,mach(3),1)&&bb_uk_set_source(&mix,15,mach(2),15)&&
        bb_uk_set_source(&mix,14,mach(2),14)&&bb_uk_set_source(&mix,13,mach(1),13));
 mix.v[1].tune=72;mix.v[14].decay=60;mix.v[0].level=100;
 unsigned err=0,ref=0;double peak=0;unsigned long rej=0;
 bb_user_kit bsamp;assert(bb_uk_from_machine(&bsamp,mach(1),"BSAMP")&&bb_uk_set_source(&bsamp,1,mach(3),1)&&
                         bb_uk_set_source(&bsamp,15,mach(2),15)&&bb_uk_set_source(&bsamp,14,mach(2),14));
 for(unsigned s=0;s<sections;s++){run_t r;CHECK(run_section(s,K808,K808,&mix,&bsamp,300,&r),"mixed setup");
  err+=r.errors;ref+=r.refused;rej+=r.rejected;if(r.peak>peak)peak=r.peak;}
 CHECK(!err&&!ref,"mixed kit plays");
 printf("RESULT mixed A=user(808+3 sampled kits, tuned/decayed) B=user(3 sampled kits) sections=%u render_errors=%u refused=%u omitted_hits=%lu peak=%.4f\n",sections,err,ref,rej,peak);
 /* 11. the firmware's BUILD swap (always the USER id on drum A): one-machine kits identical to that machine */
 uk_path=1;
 for(unsigned m=0;m<=3;m++){
  char l[64];snprintf(l,sizeof l,"swap A=user(%s) B=808",m?names[m-1]:"808");compare(l,K808,K808,&uk[m],0,m?m-1:K808,K808,300,sections);
 }
 /* 12. the firmware's USER load: staged table + kit+phrase(BB_SAMPLE_USER_ID) vs the machine's own kit+phrase */
 uk_path=2;ref_kitphrase=1;
 for(unsigned m=0;m<=3;m++){
  char l[64];snprintf(l,sizeof l,"kit+phrase A=USER(%s) B=808",m?names[m-1]:"808");compare(l,K808,K808,&uk[m],0,m?m-1:K808,K808,300,sections);
 }
 uk_path=0;ref_kitphrase=0;
 test_live_swaps(&mix,sections);
 test_pair_scenes(&uk[1],&mix,sections);
 printf("RESULT %s failures=%u\n",fails?"FAIL":"PASS",fails);
 return fails?1:0;
}

#include "bb_project.h"
#include "bb_crc32.h"
#include "bb_wire.h"
#include "group_scene.h"
static bb_project project,opened,project_before;
static uint8_t file[BB_PROJECT_MAX_BYTES],again[BB_PROJECT_MAX_BYTES],broken[BB_PROJECT_MAX_BYTES],scene_wire[BB_PROJECT_SCENE_MAX];
static bb_family_scene scene_state;
static bb_family_scene_workspace scene_stage;
static uint8_t scene_mix[GG_SCENE_MIX_BYTES];
static bb_project_assets assets={0x1234abcd,0xef987601};
static size_t file_bytes,scene_length;
static int scene_check(void *ctx,const void *wire,size_t n){
 (void)ctx;return gg_scene_decode_staged(wire,n,44100,&scene_stage,scene_mix);
}
static void fix_crc(uint8_t *wire,size_t n){uint32_t c=bb_crc32_add(UINT32_MAX,wire,12);c=bb_crc32_add(c,wire+16,n-16);bb_wire_put32(wire+12,~c);}
static void make_project(void){
 build_song();memset(&project,0,sizeof project);project.arrangement=model;project.sources=sources;
 bb_arr_part ref;CK(!bb_arr_source_fork(&project.sources,model.sections[0].part[0],BB_SOURCE_DRUM,&ref));project.arrangement.sections[0].part[0]=ref;
 project.sources.work[0].protected_steps=1;
 CK(!bb_arr_source_fork(&project.sources,model.sections[0].part[2],BB_SOURCE_DILLO,&ref));project.arrangement.sections[0].part[2]=ref;
 CK(bb_note_expression_make(1,79,1,67,&project.sources.work[1].data.dillo.expression[0]));
 memset(&scene_state,0,sizeof scene_state);scene_state.core=audio.core;scene_state.core.frame=0;scene_state.core.running=0;
 uint8_t mix[16]={0};mix[3]=83;mix[7]=0x80|100;scene_length=gg_scene_encode(&scene_state,mix,scene_wire,sizeof scene_wire);CK(scene_length>0);CK(scene_check(NULL,scene_wire,scene_length));CK(!bb_arr_work_rebuild(&project.arrangement));
}
static int open_file(const uint8_t *wire,size_t n){const void *nested=(void*)1;size_t len=13;
 int r=bb_project_open_staged(wire,n,&bb_factory_catalogue,assets,scene_check,NULL,&opened,&nested,&len);
 if(!r){CK(len==scene_length);CK(!memcmp(nested,scene_wire,len));}
 else{CK(nested==(void*)1);CK(len==13);}return r;
}
static uint32_t playback(const bb_project *p){
 model=p->arrangement;sources=p->sources;setup(0);drain();CK(!bb_arr_controller_init(&controller,&model,&sources));CK(!bb_arr_controller_start(&controller,&audio));uint32_t crc=UINT32_MAX;unsigned n=0;
 while(n++<20000){drain();CK(!bb_arr_controller_poll(&controller,&audio));crc=bb_crc32_add(crc,(uint8_t*)audio.left,sizeof audio.left);crc=bb_crc32_add(crc,(uint8_t*)audio.right,sizeof audio.right);if(controller.view.ended)break;}
 CK(n<20000&&!audio.core.running);return ~crc;
}
static void project_roundtrip(void){
 make_project();project_before=project;file_bytes=0;CK(!bb_project_encode(&project,assets,scene_wire,scene_length,scene_check,NULL,file,sizeof file,&file_bytes));CK(!memcmp(&project,&project_before,sizeof project));CK(!open_file(file,file_bytes));
 CK(opened.arrangement.row_count==project.arrangement.row_count);CK(!opened.arrangement.history_count&&!opened.arrangement.cursor);CK(!bb_arr_dirty(&opened.arrangement));
 CK(opened.sources.count==project.sources.count&&opened.sources.work_count==2);
 CK(!memcmp(opened.sources.work,project.sources.work,sizeof project.sources.work));
 for(unsigned i=0;i<5;i++){CK(!bb_arr_prepare_section(&opened.arrangement,&opened.sources,opened.arrangement.sections[i].id,1,&preparation));CK(!bb_arr_prepare_section(&project.arrangement,&project.sources,project.arrangement.sections[i].id,1,&preparation));}
 size_t n;CK(!bb_project_encode(&opened,assets,scene_wire,scene_length,scene_check,NULL,again,sizeof again,&n));CK(n==file_bytes&&!memcmp(file,again,n));
 CK(playback(&project)==playback(&opened));
 /* Real source caches are reconstructed from names; bad cache bytes are not durable. */
 opened.sources.entry[0].bank_index=UINT16_MAX;opened.sources.entry[0].part_index=UINT16_MAX;CK(!bb_arr_sources_reindex_staged(&opened.sources));bb_factory_choice choice;CK(!bb_arr_source_choice(&opened.sources,opened.arrangement.sections[1].part[0],BB_SOURCE_DRUM,&choice));
}
static void refusal_cases(void){
 make_project();CK(!bb_project_encode(&project,assets,scene_wire,scene_length,scene_check,NULL,file,sizeof file,&file_bytes));
 for(size_t n=0;n<file_bytes;n++)CK(open_file(file,n)!=BB_PROJECT_OK);
 for(size_t i=0;i<file_bytes;i++){memcpy(broken,file,file_bytes);broken[i]^=1;CK(open_file(broken,file_bytes)!=BB_PROJECT_OK);}
 const unsigned invalid_header[]={16,20,24,28,32,40,41,42,43,44,48,52,56};
 for(unsigned i=0;i<sizeof invalid_header/sizeof invalid_header[0];i++){
  memcpy(broken,file,file_bytes);unsigned off=invalid_header[i];if(off>=40&&off<=43)broken[off]=255;else bb_wire_put32(broken+off,0);if(off==56)broken[off]=1;fix_crc(broken,file_bytes);CK(open_file(broken,file_bytes)!=BB_PROJECT_OK);
 }
 /* Valid checksum does not turn an invalid sound scene into a project. */
 memcpy(broken,file,file_bytes);broken[file_bytes-scene_length]^=1;fix_crc(broken,file_bytes);CK(open_file(broken,file_bytes)==BB_PROJECT_SCENE);
 const void *nested=NULL;size_t nested_n=0;bb_project_assets wrong=assets;wrong.library_payload_crc++;
 CK(bb_project_open_staged(file,file_bytes,&bb_factory_catalogue,wrong,scene_check,NULL,&opened,&nested,&nested_n)==BB_PROJECT_ASSETS);
 memset(again,0xa5,sizeof again);size_t before=123;CK(bb_project_encode(&project,assets,scene_wire,scene_length,scene_check,NULL,again,file_bytes-1,&before)==BB_PROJECT_CAPACITY);CK(before==123);for(unsigned i=0;i<sizeof again;i++)CK(again[i]==0xa5);
 CK(bb_project_encode(&project,assets,scene_wire,scene_length,NULL,NULL,again,sizeof again,&before)==BB_PROJECT_INVALID);
 project.sources.work[0].origin=(bb_arr_part){1,project.sources.work[0].revision,BB_ARR_WORKING};CK(bb_project_encode(&project,assets,scene_wire,scene_length,scene_check,NULL,again,sizeof again,&before)==BB_PROJECT_SOURCE);CK(before==123);
 CK(bb_project_open_staged(file,file_bytes,&bb_factory_catalogue,assets,scene_check,NULL,(bb_project*)file,&nested,&nested_n)==BB_PROJECT_ALIAS);
 CK(bb_project_open_staged(file,file_bytes,&bb_factory_catalogue,assets,scene_check,NULL,&opened,(const void**)&opened,&nested_n)==BB_PROJECT_ALIAS);
}
static void edge_cases(void){
 make_project();size_t n;
 CK(bb_project_encode(&project,assets,scene_wire,scene_length,scene_check,NULL,again,sizeof again,(size_t*)&bb_factory_catalogue)==BB_PROJECT_ALIAS);
 project.arrangement.row_count=0;CK(!bb_project_encode(&project,assets,scene_wire,scene_length,scene_check,NULL,file,sizeof file,&file_bytes));
 memcpy(broken,file,file_bytes);bb_wire_put32(broken+BB_PROJECT_HEADER,0);fix_crc(broken,file_bytes);CK(open_file(broken,file_bytes)==BB_PROJECT_INVALID);
 memset(&project,0,sizeof project);bb_arr_init(&project.arrangement);CK(!bb_arr_sources_init(&project.sources,&bb_factory_catalogue));
 CK(!bb_project_encode(&project,assets,scene_wire,scene_length,scene_check,NULL,file,sizeof file,&file_bytes));CK(!open_file(file,file_bytes));CK(!opened.arrangement.row_count&&!opened.sources.count&&!opened.sources.work_count);
 make_project();project.sources.work[0].protected_steps=UINT64_C(1)<<63;CK(bb_project_encode(&project,assets,scene_wire,scene_length,scene_check,NULL,again,sizeof again,&n)==BB_PROJECT_SOURCE);
 make_project();project.sources.work[0].data.drum.ordinal[0]=255;CK(bb_project_encode(&project,assets,scene_wire,scene_length,scene_check,NULL,again,sizeof again,&n)==BB_PROJECT_SOURCE);
 make_project();project.sources.entry[0].part[0]^=1;CK(bb_project_encode(&project,assets,scene_wire,scene_length,scene_check,NULL,again,sizeof again,&n)==BB_PROJECT_SOURCE);
}
static void maximum_project(void){
 memset(&project,0,sizeof project);bb_arr_init(&project.arrangement);CK(!bb_arr_sources_init(&project.sources,&bb_factory_catalogue));
 for(unsigned i=0;i<BB_ARR_SECTIONS;i++){
  if(project.arrangement.cursor==BB_ARR_HISTORY)CK(!bb_arr_history_discard(&project.arrangement,project.arrangement.revision));
  bb_arr_section q={0};strcpy(q.name,"MAXIMUM SECTION");q.duration=BB_ARR_DURATION_MAX;
  for(unsigned m=0;m<3;m++)CK(!bb_arr_source_bind(&project.sources,&choices[i*3+m],m==2?BB_SOURCE_DILLO:BB_SOURCE_DRUM,&q.part[m]));
  CK(!bb_arr_section_append(&project.arrangement,&q,BB_ARR_REPEAT_MAX));
 }
 while(project.arrangement.row_count<BB_ARR_ROWS){
  if(project.arrangement.cursor==BB_ARR_HISTORY)CK(!bb_arr_history_discard(&project.arrangement,project.arrangement.revision));
  CK(!bb_arr_row_insert(&project.arrangement,project.arrangement.row_count,project.arrangement.rows[0]));
 }
 for(unsigned i=0;i<BB_ARR_WORK_PARTS;i++){
  bb_arr_part ref;unsigned target=i%2?2:0;CK(!bb_arr_source_fork(&project.sources,project.arrangement.sections[i].part[target],target==2?BB_SOURCE_DILLO:BB_SOURCE_DRUM,&ref));
  project.arrangement.sections[i].part[target]=ref;bb_arr_work_part *q=&project.sources.work[i];
  if(target==0){
   bb_phrase phrase={0};phrase.length=64;phrase.count=128;phrase.bars=4;phrase.lattice=BB_FDL_SIXTEENTHS;
   for(unsigned n=0;n<128;n++)phrase.events[n]=(uint16_t)(BB_FDV_BD|((n%64)<<5)|((n%3)<<11));
   CK(bb_phrase_index_prepare(&phrase,BB_RICH_REPRESENTABLE_VOICES,&q->data.drum));
  }else{
   q->data.dillo.length=64;for(unsigned n=0;n<64;n++){q->data.dillo.notes[n]=(bb_note){(uint8_t)(36+n),1,(uint8_t)(n%2),(uint8_t)((n+1)%2)};CK(bb_note_expression_make(1,63+n,1,n+1,&q->data.dillo.expression[n]));}
  }
  q->protected_steps=UINT64_MAX;
 }
 CK(!bb_arr_work_rebuild(&project.arrangement));
 CK(project.sources.count==96&&project.sources.work_count==16&&project.arrangement.row_count==64);
 CK(!bb_project_encode(&project,assets,scene_wire,scene_length,scene_check,NULL,file,sizeof file,&file_bytes));CK(!open_file(file,file_bytes));CK(!memcmp(project.sources.work,opened.sources.work,sizeof project.sources.work));
 size_t n;CK(!bb_project_encode(&opened,assets,scene_wire,scene_length,scene_check,NULL,again,sizeof again,&n));CK(n==file_bytes&&!memcmp(file,again,n));
 printf("MAXIMUM project %zu bytes; workspace %zu bytes; schema worst case31504 <=32768\n",file_bytes,sizeof(bb_project));
}
static void file_readback(void){
 FILE *f=fopen("project-roundtrip.bbp2","wb");CK(f);CK(fwrite(file,1,file_bytes,f)==file_bytes);CK(!fclose(f));
 memset(again,0,sizeof again);f=fopen("project-roundtrip.bbp2","rb");CK(f);CK(fread(again,1,sizeof again,f)==file_bytes);CK(!ferror(f));CK(fgetc(f)==EOF);CK(!fclose(f));CK(!memcmp(file,again,file_bytes));CK(!open_file(again,file_bytes));
 printf("HOST FILE closed/reopened %zu bytes and decoded; not power-loss or device persistence\n",file_bytes);
}
static void acid_roundtrip(void){
 bb_browser b;CK(bb_browser_init(&b,&bb_factory_catalogue));CK(bb_browser_group(&b,2));unsigned count=0;
 for(unsigned bank=0;bank<bb_browser_bank_count(&b);bank++){
  b.bank_index=(uint16_t)bank;b.part_index=0;unsigned parts=bb_browser_part_count(&b);
  for(unsigned i=0;i<parts;i++){
   b.part_index=(uint16_t)i;bb_factory_choice c;CK(bb_browser_current(&b,&c));
   memset(&project,0,sizeof project);bb_arr_init(&project.arrangement);CK(!bb_arr_sources_init(&project.sources,&bb_factory_catalogue));
   bb_arr_section q={0};strcpy(q.name,"ACID ROUNDTRIP");q.duration=16;
   CK(!bb_arr_source_bind(&project.sources,&choices[0],BB_SOURCE_DRUM,&q.part[0]));q.part[1]=q.part[0];CK(!bb_arr_source_bind(&project.sources,&c,BB_SOURCE_DILLO,&q.part[2]));
   bb_arr_part ref;CK(!bb_arr_source_fork(&project.sources,q.part[2],BB_SOURCE_DILLO,&ref));q.part[2]=ref;CK(!bb_arr_section_append(&project.arrangement,&q,1));
   CK(!bb_project_encode(&project,assets,scene_wire,scene_length,scene_check,NULL,file,sizeof file,&file_bytes));CK(!open_file(file,file_bytes));CK(!memcmp(project.sources.work,opened.sources.work,sizeof project.sources.work));
   CK(!bb_arr_prepare_section(&opened.arrangement,&opened.sources,opened.arrangement.rows[0].section,1,&preparation));CK(!memcmp(&preparation.dillo,&project.sources.work[0].data.dillo,sizeof synth));count++;
  }
 }
 CK(count==205);printf("ACID205 stable source names and complete immutable Dillo expression roundtrip\n");
}
static void catalogue_roundtrip(void){
 for(unsigned i=0;i<sections;i++){
  memset(&project,0,sizeof project);bb_arr_init(&project.arrangement);CK(!bb_arr_sources_init(&project.sources,&bb_factory_catalogue));
  bb_arr_section q={0};strcpy(q.name,"CATALOGUE ROUNDTRIP");q.duration=16;
  CK(!bb_arr_source_bind(&project.sources,&choices[i],BB_SOURCE_DRUM,&q.part[0]));q.part[1]=q.part[0];CK(!bb_arr_source_bind(&project.sources,&choices[i],BB_SOURCE_DILLO,&q.part[2]));
  CK(!bb_arr_section_append(&project.arrangement,&q,2));
  bb_arr_part ref;CK(!bb_arr_source_fork(&project.sources,q.part[0],BB_SOURCE_DRUM,&ref));project.arrangement.sections[0].part[0]=ref;
  CK(!bb_arr_source_fork(&project.sources,q.part[2],BB_SOURCE_DILLO,&ref));project.arrangement.sections[0].part[2]=ref;CK(!bb_arr_work_rebuild(&project.arrangement));
  CK(!bb_project_encode(&project,assets,scene_wire,scene_length,scene_check,NULL,file,sizeof file,&file_bytes));CK(!open_file(file,file_bytes));CK(!memcmp(project.sources.work,opened.sources.work,sizeof project.sources.work));
  CK(!bb_arr_prepare_section(&opened.arrangement,&opened.sources,opened.arrangement.rows[0].section,2,&preparation));CK(!memcmp(&preparation.drum[0],&catalogue[i],sizeof rich));
 }
}
int main(void){collect();out=fopen("/dev/null","wb");CK(out);if(getenv("BB_PROJECT_IO_ACID_ONLY")){make_project();maximum_project();file_readback();acid_roundtrip();printf("PASS %u additional host-file/205ACID project checks; cold codec only\n",checks);fclose(out);return 0;}project_roundtrip();refusal_cases();edge_cases();maximum_project();catalogue_roundtrip();
 printf("PASS %u project checks: actual539-source rich/Dillo roundtrip, lineage/protected/expression, stopped finite playback PCM, canonical wire, checksum/truncation/admission/alias/refusal; cold codec only\n",checks);fclose(out);return 0;}

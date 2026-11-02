static struct {uint8_t bytes[BB_PROJECT_MAX_BYTES];size_t size,calls,max_chunk,next,fail_call,header_calls;unsigned corrupt_tail,mutate;} transfer;
static struct {bb_arrangement model;uint8_t gap[37];bb_arr_sources sources;} split_stage;
static uint8_t nested_scene[BB_PROJECT_SCENE_MAX];
static unsigned checked_scene;
static int count_scene(void *ctx,const void *p,size_t n){checked_scene++;return scene_check(ctx,p,n);}
static int file_write(void *ctx,size_t off,const void *p,size_t n){
 (void)ctx;CK(n&&n<=256);CK(off<=sizeof transfer.bytes&&n<=sizeof transfer.bytes-off);
 transfer.calls++;if(n>transfer.max_chunk)transfer.max_chunk=n;
 if(transfer.calls==transfer.fail_call)return 0;
 if(!off){CK(n==64);CK(!transfer.header_calls);transfer.header_calls++;}
 else{CK(off==transfer.next);transfer.next=off+n;}
 memcpy(transfer.bytes+off,p,n);
 if(transfer.mutate&&transfer.calls==2)project.arrangement.revision++;
 return 1;
}
static int file_read(void *ctx,size_t off,void *p,size_t n){
 (void)ctx;CK(n&&n<=256);CK(off<=transfer.size&&n<=transfer.size-off);CK(off==transfer.next);
 transfer.calls++;if(n>transfer.max_chunk)transfer.max_chunk=n;
 if(transfer.calls==transfer.fail_call)return 0;
 memcpy(p,transfer.bytes+off,n);transfer.next=off+n;
 if(transfer.corrupt_tail&&off+n==transfer.size)((uint8_t*)p)[n-1]^=1;
 return 1;
}
static void transfer_init(void){memset(&transfer,0,sizeof transfer);transfer.next=64;}
static void input_init(void){transfer_init();memcpy(transfer.bytes,file,file_bytes);transfer.size=file_bytes;transfer.next=0;checked_scene=0;}
static int streamed_open(void){
 const void *scene=(void*)1;size_t n=17;
 int result=bb_project_read_stream_staged(file_read,NULL,file_bytes,&bb_factory_catalogue,assets,count_scene,NULL,
  &split_stage.model,&split_stage.sources,nested_scene,sizeof nested_scene,&scene,&n);
 if(!result){CK(scene==nested_scene&&n==scene_length);CK(!memcmp(scene,scene_wire,n));}
 else{CK(scene==(void*)1&&n==17);}
 return result;
}
static void stream_cases(void){
 make_project();maximum_project();project_before=project;
 size_t written=11;transfer_init();CK(!bb_project_write_stream(&project.arrangement,&project.sources,assets,scene_wire,scene_length,scene_check,NULL,file_write,NULL,&written));
 CK(written==file_bytes&&!memcmp(transfer.bytes,file,file_bytes));CK(transfer.header_calls==1);CK(transfer.max_chunk<=256);size_t write_calls=transfer.calls;CK(write_calls<=129);
 CK(!memcmp(&project,&project_before,sizeof project));
 const char *reference=getenv("BB_PROJECT_REFERENCE_FILE");if(reference){FILE *f=fopen(reference,"rb");CK(f);size_t n=fread(again,1,sizeof again,f);CK(!ferror(f)&&!fclose(f));CK(n==file_bytes&&!memcmp(again,file,file_bytes));printf("WIRE matches sealed pre-stream maximum-count project byte for byte\n");}
 memset(split_stage.gap,0xa5,sizeof split_stage.gap);input_init();CK(!streamed_open());size_t read_calls=transfer.calls;CK(transfer.max_chunk<=256&&read_calls<=129);CK(checked_scene==1);
 for(unsigned i=0;i<sizeof split_stage.gap;i++)CK(split_stage.gap[i]==0xa5);
 CK(!memcmp(&project.sources,&split_stage.sources,sizeof project.sources));CK(split_stage.model.row_count==64&&!bb_arr_dirty(&split_stage.model));
 model_copy=project.arrangement;memset(model_copy.history,0,sizeof model_copy.history);model_copy.history_count=model_copy.cursor=0;model_copy.saved_revision=model_copy.revision;CK(!memcmp(&model_copy,&split_stage.model,sizeof model_copy));
 CK(!memcmp(&project,&project_before,sizeof project));
 for(size_t fail=1;fail<=write_calls;fail++){
  transfer_init();transfer.fail_call=fail;written=11;CK(bb_project_write_stream(&project.arrangement,&project.sources,assets,scene_wire,scene_length,scene_check,NULL,file_write,NULL,&written)==BB_PROJECT_IO);CK(written==11&&!transfer.header_calls);CK(!memcmp(&project,&project_before,sizeof project));
 }
 for(size_t fail=1;fail<=read_calls;fail++){
  input_init();transfer.fail_call=fail;int error=streamed_open();if(error!=BB_PROJECT_IO)fprintf(stderr,"READ FAIL callback%zu error%d calls%zu\n",fail,error,transfer.calls);CK(error==BB_PROJECT_IO);CK(!checked_scene);CK(!memcmp(&project,&project_before,sizeof project));
 }
 input_init();transfer.corrupt_tail=1;CK(streamed_open()==BB_PROJECT_CHECKSUM);CK(!checked_scene);CK(!memcmp(&project,&project_before,sizeof project));
 transfer_init();transfer.mutate=1;written=11;CK(bb_project_write_stream(&project.arrangement,&project.sources,assets,scene_wire,scene_length,scene_check,NULL,file_write,NULL,&written)==BB_PROJECT_CONFLICT);CK(written==11&&!transfer.header_calls);project=project_before;
 input_init();const void *scene=(void*)1;size_t n=17;
 CK(bb_project_read_stream_staged(file_read,NULL,file_bytes,&bb_factory_catalogue,assets,count_scene,NULL,&split_stage.model,&split_stage.sources,nested_scene,scene_length-1,&scene,&n)==BB_PROJECT_CAPACITY);CK(scene==(void*)1&&n==17);
 input_init();CK(bb_project_read_stream_staged(file_read,NULL,file_bytes,&bb_factory_catalogue,assets,count_scene,NULL,&split_stage.model,&split_stage.sources,&split_stage.model,scene_length,&scene,&n)==BB_PROJECT_ALIAS);
 printf("STREAM callbacks write%zu/read%zu; every read/write failure refused; <=256B chunks; split staging gap intact\n",write_calls,read_calls);
}
int main(void){if(!getenv("BB_PROJECT_STREAM_ONLY"))return project_regression_main();collect();out=fopen("/dev/null","wb");CK(out);stream_cases();printf("PASS %u streaming project checks; no live restore or device persistence claim\n",checks);fclose(out);return 0;}

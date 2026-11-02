#include <unistd.h>
#include "bb_project.h"
static struct {FILE *f;char temp[256],dest[256];unsigned mode,begins,seals,publishes,aborts,writes,reads,fail_write,fail_read,scene_checks;} disk;
static int disk_begin(void *ctx){(void)ctx;disk.begins++;if(disk.mode==1)return 0;disk.f=fopen(disk.temp,"w+b");return disk.f!=NULL;}
static int disk_write(void *ctx,size_t off,const void *p,size_t n){(void)ctx;disk.writes++;if(disk.writes==disk.fail_write)return 0;return !fseek(disk.f,(long)off,SEEK_SET)&&fwrite(p,1,n,disk.f)==n;}
static int disk_seal(void *ctx,size_t expected,size_t *actual){
 (void)ctx;disk.seals++;if(disk.mode==2)return 0;
 CK(!fflush(disk.f));CK(!fclose(disk.f));disk.f=NULL;
 if(disk.mode==3){FILE *f=fopen(disk.temp,"r+b");CK(f);CK(!fseek(f,(long)expected-1,SEEK_SET));int c=fgetc(f);CK(c!=EOF);CK(!fseek(f,-1,SEEK_CUR));CK(fputc(c^1,f)!=EOF);CK(!fclose(f));}
 if(disk.mode==4){ /* Another structurally valid BBP2 file must not pass. */
  FILE *f=fopen(disk.temp,"r+b");CK(f);CK(fread(broken,1,expected,f)==expected);broken[16]^=1;fix_crc(broken,expected);rewind(f);CK(fwrite(broken,1,expected,f)==expected);CK(!fclose(f));
 }
 disk.f=fopen(disk.temp,"rb");if(!disk.f)return 0;
 CK(!fseek(disk.f,0,SEEK_END));long n=ftell(disk.f);CK(n>=0);*actual=(size_t)n;
 if(disk.mode==5)(*actual)--;
 if(disk.mode==6)project.arrangement.revision++;
 return 1;
}
static int disk_read(void *ctx,size_t off,void *p,size_t n){(void)ctx;disk.reads++;if(disk.reads==disk.fail_read)return 0;return !fseek(disk.f,(long)off,SEEK_SET)&&fread(p,1,n,disk.f)==n;}
static int disk_publish(void *ctx){(void)ctx;disk.publishes++;if(disk.mode==7)return 0;if(disk.f){CK(!fclose(disk.f));disk.f=NULL;}int result=!rename(disk.temp,disk.dest);if(result&&disk.mode==9)project.arrangement.revision++;return result;}
static void disk_abort(void *ctx){(void)ctx;disk.aborts++;if(disk.f){CK(!fclose(disk.f));disk.f=NULL;}CK(!remove(disk.temp));}
static const bb_project_file_ops disk_ops={disk_begin,disk_write,disk_seal,disk_read,disk_publish,disk_abort};
static void disk_reset(const char *dir){
 memset(&disk,0,sizeof disk);snprintf(disk.temp,sizeof disk.temp,"%s/staged",dir);snprintf(disk.dest,sizeof disk.dest,"%s/song.bbp2",dir);
 FILE *f=fopen(disk.dest,"wb");CK(f);CK(fwrite("OLD",1,3,f)==3);CK(!fclose(f));project=project_before;
 CK(project.arrangement.revision>1);project.arrangement.saved_revision=project.arrangement.revision-1;
}
static int disk_check(void *context,const void *p,size_t n){disk.scene_checks++;if(disk.mode==8&&disk.scene_checks==2)return 0;return scene_check(context,p,n);}
static int disk_save(size_t *n){return bb_project_save_verified(&project.arrangement,&project.sources,assets,scene_wire,scene_length,
 disk_check,NULL,&disk_ops,NULL,&split_stage.model,&split_stage.sources,nested_scene,sizeof nested_scene,n);}
static void disk_old(void){char text[4]={0};FILE *f=fopen(disk.dest,"rb");CK(f);CK(fread(text,1,sizeof text,f)==3);CK(!fclose(f));CK(!strcmp(text,"OLD"));}
int main(void){
 collect();out=fopen("/dev/null","wb");CK(out);make_project();maximum_project();project_before=project;
 uint32_t audio_before=bb_crc32_add(UINT32_MAX,(const uint8_t *)&audio,sizeof audio);
 char directory[]="/private/tmp/bb1-project-file-XXXXXX";char *dir=mkdtemp(directory);CK(dir);
 disk_reset(dir);size_t n=19;CK(!disk_save(&n));CK(n==file_bytes&&!bb_arr_dirty(&project.arrangement));CK(disk.begins==1&&disk.seals==1&&disk.publishes==1&&!disk.aborts);
 unsigned writes=disk.writes,reads=disk.reads;FILE *f=fopen(disk.dest,"rb");CK(f);CK(fread(again,1,sizeof again,f)==file_bytes);CK(!fclose(f));CK(!memcmp(file,again,file_bytes));
 for(unsigned mode=1;mode<=8;mode++){
  disk_reset(dir);disk.mode=mode;opened=project;n=19;int r=disk_save(&n);if(mode==6)opened.arrangement.revision++;CK(!memcmp(&opened,&project,sizeof project));CK(r!=BB_PROJECT_OK);CK(n==19&&bb_arr_dirty(&project.arrangement));disk_old();CK(disk.aborts==(mode!=1));
  CK(disk.publishes==(mode==7));
  if(mode==3)CK(r==BB_PROJECT_CHECKSUM);if(mode==4||mode==5)CK(r==BB_PROJECT_FILE_VERIFY);if(mode==6)CK(r==BB_PROJECT_CONFLICT);if(mode==7)CK(r==BB_PROJECT_FILE_PUBLISH);if(mode==8)CK(r==BB_PROJECT_SCENE);
 }
 for(unsigned w=0;w<2;w++)for(unsigned fail=1;fail<=(w?writes:reads);fail++){
  disk_reset(dir);if(w)disk.fail_write=fail;else disk.fail_read=fail;opened=project;n=19;CK(disk_save(&n)==BB_PROJECT_IO);CK(!memcmp(&opened,&project,sizeof project));CK(n==19&&bb_arr_dirty(&project.arrangement));CK(disk.aborts==1&&!disk.publishes);disk_old();
 }
 disk_reset(dir);n=19;CK(bb_project_save_verified(&project.arrangement,&project.sources,assets,scene_wire,scene_length,scene_check,NULL,&disk_ops,NULL,
  &project.arrangement,&split_stage.sources,nested_scene,sizeof nested_scene,&n)==BB_PROJECT_ALIAS);CK(!disk.begins&&n==19);disk_old();
 disk_reset(dir);disk.mode=9;n=19;CK(disk_save(&n)==BB_PROJECT_CONFLICT);CK(n==19&&bb_arr_dirty(&project.arrangement));CK(disk.publishes==1&&!disk.aborts);
 CK(bb_crc32_add(UINT32_MAX,(const uint8_t *)&audio,sizeof audio)==audio_before);
 CK(!remove(disk.dest));CK(!rmdir(dir));fclose(out);
 printf("PASS %u verified file transaction checks; actual close/reopen/rename, all %u write/%u read failures; no device/UI claim\n",checks,writes,reads);return 0;
}

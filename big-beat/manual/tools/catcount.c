/* Lane M: walk the firmware catalogue as the player's browser does; print groups, banks and parts. Host only. */
#include <stdio.h>
#include "bb_factory.h"
int main(void){
  if(!bb_factory_init()){puts("no catalogue");return 2;}
  static const char *G[]={"ERAS","SONGS","ACID"};
  for(unsigned g=0;g<BB_BROWSER_GROUPS;g++){
    bb_browser v=bb_factory_browser; if(!bb_browser_group(&v,g)){printf("%s: no\n",G[g]);continue;}
    size_t banks=bb_browser_bank_count(&v); unsigned parts=0;
    printf("== %s banks %zu\n",G[g],banks);
    for(size_t b=0;b<banks;b++){ char t[64]="";
      bb_browser_bank_title(&v,(unsigned)(b%16),t,sizeof t);
      size_t p=bb_browser_part_count(&v); parts+=(unsigned)p;
      printf("  %zu\t%s\t%zu\n",b+1,t,p);
      if(b+1<banks) bb_browser_move_bank(&v,1);
      if((b+1)%16==0){}
    }
    printf("== %s parts %u\n",G[g],parts);
  }
  return 0;
}

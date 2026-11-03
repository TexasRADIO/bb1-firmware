#include <stdio.h>
#include "bb_factory.h"
int main(void){ if(!bb_factory_init())return 2;
 for(unsigned g=0;g<3;g++){ bb_browser v=bb_factory_browser; bb_browser_group(&v,g);
  size_t n=bb_browser_part_count(&v); printf("G%u:",g);
  for(size_t i=0;i<n&&i<8;i++){char t[64]="";bb_browser_part_title(&v,(unsigned)i,t,sizeof t);printf(" [%s]",t);} puts("");}
 return 0;}

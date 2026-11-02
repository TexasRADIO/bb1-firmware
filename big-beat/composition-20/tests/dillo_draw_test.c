/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_dillo_kit_draw.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned n;
static void line(void *ctx,int x,int y,int xx,int yy){
 (void)ctx;if(x<0||xx<0||x>34||xx>34||y<0||yy<0||y>24||yy>24)exit(1);n++;
 printf("<path d='M%d %dL%d %d'/>\n",x,y,xx,yy);
}
int main(void){
 puts("<!doctype html><meta charset='utf-8'><meta name='viewport' content='width=device-width'><title>BB1 Dillo kit motion study</title><style>body{background:#182320;color:#f4ebd6;font:16px system-ui;margin:24px}main{max-width:620px}svg{width:100%;background:#21352d;stroke:#f2bc68;fill:none;stroke-width:.7}.frame{animation:show 1s steps(1) infinite;opacity:0}.f0{animation-delay:0s}.f1{animation-delay:.2s}.f2{animation-delay:.4s}.f3{animation-delay:.6s}.f4{animation-delay:.8s}@keyframes show{0%,19%{opacity:1}20%,100%{opacity:0}}text{fill:#f4ebd6;stroke:none;font:4px system-ui}@media(prefers-reduced-motion:reduce){.frame{animation:none;opacity:0}.f0{opacity:1}}</style><main><h1>The Dillo kit</h1><p>C geometry motion study: synthetic activity, not a device capture. Curled kick, head-on snare, sideways tom, opening shell hats. Existing 16-step editing stays available.</p><svg viewBox='0 0 176 46' role='img' aria-label='Four Dillo instrument poses'>");
 const char *names[]={"KICK","SNARE","TOM","HATS"};
 for(unsigned p=0;p<4;p++){
  printf("<g transform='translate(%u 4)'>\n",p*44+4);
  for(unsigned f=0;f<5;f++){printf("<g class='frame f%u'>\n",f);bb_dillo_kit_draw(p,f,0,0,line,0);puts("</g>");}
  printf("<text x='6' y='35'>%s</text></g>\n",names[p]);
 }
 puts("</svg><p>Hold to overdub · finish pass · keep or Undo. Actual control binding, audio event snapshots, full target fit and device timing remain integration gates.</p></main>");
 fprintf(stderr,"PASS %u bounded C geometry segments across 20 pose/motion cases\n",n);return 0;
}

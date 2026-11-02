/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_dillo_kit_draw.h"
void bb_dillo_kit_draw(unsigned pose,unsigned level,int x,int y,bb_dillo_line line,void *ctx){
 if(!line||pose>BB_DILLO_SHELL)return;if(level>4)level=4;
 int lift=(int)level;
 #define L(a,b,c,d) line(ctx,x+(a),y+(b),x+(c),y+(d))
 if(pose==BB_DILLO_CURLED){
  L(9,3,23,3);L(23,3,29,9);L(29,9,29,17);L(29,17,23,22);
  L(23,22,9,22);L(9,22,4,17);L(4,17,4,9);L(4,9,9,3);
  L(10,7,21,7);L(21,7,25,12);L(25,12,21,18);L(21,18,12,18);
  L(12,18,9,14);L(9,14,12,11);L(12,11,18+lift,11);L(14,3,14,7);L(20,3,20,7);
 }else if(pose==BB_DILLO_FRONT){
  L(8,21,5,11-lift);L(5,11-lift,10,5);L(10,5,24,5);L(24,5,29,11-lift);L(29,11-lift,26,21);
  L(10,5,8,0);L(8,0,14,5);L(20,5,26,0);L(26,0,24,5);
  L(10,11-lift,13,11-lift);L(21,11-lift,24,11-lift);L(14,14-lift,20,14-lift);
  L(14,14-lift,17,19-lift);L(17,19-lift,20,14-lift);L(8,21,26,21);
 }else if(pose==BB_DILLO_SIDE){
  L(3,17-lift,6,8-lift);L(6,8-lift,12,4);L(12,4,22,4);L(22,4,26,10-lift);
  L(26,10-lift,32,13-lift);L(32,13-lift,27,17-lift);L(27,17-lift,3,17-lift);
  L(3,17-lift,0,20);L(9,17-lift,7,23);L(21,17-lift,23,23);
  L(10,7,10,16-lift);L(15,5,15,16-lift);L(20,5,20,16-lift);L(26,10-lift,26,6);L(29,12-lift,30,12-lift);
 }else{
  L(3,12-lift,9,6-lift);L(9,6-lift,24,6-lift);L(24,6-lift,31,12-lift);L(31,12-lift,3,12-lift);
  L(5,15+lift,29,15+lift);L(5,15+lift,10,20+lift);L(10,20+lift,24,20+lift);L(24,20+lift,29,15+lift);
  L(12,6-lift,12,12-lift);L(21,6-lift,21,12-lift);
 }
 #undef L
}

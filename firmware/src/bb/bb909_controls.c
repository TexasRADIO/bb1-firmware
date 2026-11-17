/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb909_controls.h"
#include "drum909.h"
#include <string.h>
unsigned bb909_control_count(unsigned v){return v<12?(unsigned)drum909_nparams((int)v):0;}
int bb909_control_max(unsigned v,unsigned p){
    if(v>=12||p>=bb909_control_count(v))return -1;
    const x0x_param_t *s=drum909_param((int)v,(int)p);return s?s->max:-1;
}
const char *bb909_control_name(unsigned v,unsigned p){
    if(bb909_control_max(v,p)<0)return "--";return drum909_param((int)v,(int)p)->name;
}
int bb909_control_valid(unsigned v,unsigned p,unsigned value){
    int max=bb909_control_max(v,p);return max>=0&&value<=(unsigned)max;
}
const char *bb909_control_value_name(unsigned v,unsigned p,unsigned value){
    if(!bb909_control_valid(v,p,value))return "--";
    const x0x_param_t *s=drum909_param((int)v,(int)p);return s->names?s->names[value]:0;
}
void bb909_controls_default(bb909_controls *s){
    if(!s)return;memset(s,0,sizeof *s);
    for(unsigned v=0;v<12;v++)for(unsigned p=0;p<bb909_control_count(v);p++)
        s->value[v][p]=drum909_param((int)v,(int)p)->def;
}
int bb909_controls_applied(bb909_controls *s,unsigned v,unsigned p,unsigned value){
    if(!s||!bb909_control_valid(v,p,value))return 0;
    s->value[v][p]=(uint8_t)value;s->touched[v]|=(uint16_t)(1u<<p);s->revision++;return 1;
}

/* SPDX-License-Identifier: GPL-3.0-only */
#include "bb_catalogue.h"
int bb_catalogue_open(bb_catalogue*c,const uint8_t*b,size_t n,bb_pc_cache*w){return bb_pr_open(c,b,n,w);}
int bb_catalogue_kind(const bb_catalogue*c,bb_catalogue_id id){return bb_pr_kind(c,id);}
size_t bb_catalogue_count(const bb_catalogue*c,bb_catalogue_id id){return bb_pr_count(c,id);}
bb_catalogue_id bb_catalogue_at(const bb_catalogue*c,bb_catalogue_id id,size_t i){return bb_pr_at(c,id,i);}
bb_catalogue_id bb_catalogue_key_at(const bb_catalogue*c,bb_catalogue_id id,size_t i){return bb_pr_key_at(c,id,i);}
bb_catalogue_id bb_catalogue_find(const bb_catalogue*c,bb_catalogue_id id,const char*s){return bb_pr_find(c,id,s);}
int bb_catalogue_number(const bb_catalogue*c,bb_catalogue_id id,double*out){return bb_pr_number(c,id,out);}
int bb_catalogue_unsigned(const bb_catalogue*c,bb_catalogue_id id,uint32_t m,uint32_t*out){return bb_pr_unsigned(c,id,m,out);}
int bb_catalogue_scaled(const bb_catalogue*c,bb_catalogue_id id,unsigned n,unsigned d,uint32_t m,uint32_t*out){return bb_pr_scaled(c,id,n,d,m,out);}
int bb_catalogue_text(const bb_catalogue*c,bb_catalogue_id id,const uint8_t**p,size_t*n){return bb_pr_text(c,id,p,n);}
int bb_catalogue_copy_text(const bb_catalogue*c,bb_catalogue_id id,char*out,size_t n){return bb_pr_copy_text(c,id,out,n);}
static int separated(const void*a,size_t an,const void*b,size_t bn){
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return a&&b&&an<=UINTPTR_MAX-x&&bn<=UINTPTR_MAX-y&&(x<=y?an<=y-x:bn<=x-y);
}
int bb_catalogue_private(const bb_catalogue*c,const void*p,size_t n){
 return c && separated(p,n,c,sizeof *c) &&
  separated(p,n,c->container.bytes,c->container.length)&&
  separated(p,n,c->container.cache,sizeof *c->container.cache);
}

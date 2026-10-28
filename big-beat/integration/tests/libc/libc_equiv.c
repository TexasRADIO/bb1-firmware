/* Equivalence of firmware libc.c memset/memcpy against reference byte loops.
 * Every length 0..300, every dst/src alignment 0..7, every fill byte class, and
 * every overlap offset -40..40 for memcpy (out of contract, must still match the
 * original forward byte loop). Exit 0 only when every case matches. */
#include <stdio.h>
#include <stdint.h>
#define memset fw_memset
#define memcpy fw_memcpy
#define memmove fw_memmove
#define memcmp fw_memcmp
#define strlen fw_strlen
#include LIBC_C
#undef memset
#undef memcpy
#undef memcmp
static int differ(const unsigned char *a,const unsigned char *b,unsigned n){ while(n--) if(*a++!=*b++) return 1; return 0; }
static void ref_set(unsigned char *p, int c, unsigned n){ while(n--) *p++=(unsigned char)c; }
static void ref_cpy(unsigned char *p, const unsigned char *q, unsigned n){ while(n--) *p++=*q++; }
static unsigned char A[1024] __attribute__((aligned(16))), B[1024] __attribute__((aligned(16)));
static void fill(unsigned char *x, unsigned s){ for(unsigned i=0;i<1024;i++) x[i]=(unsigned char)(i*131u+s*17u+7u); }
int main(void){
    unsigned long cases=0, bad=0; const int cs[]={0,1,0x7F,0x80,0xFF,-1,0x1A5};
    for(unsigned n=0;n<=300;n++) for(unsigned a=0;a<8;a++) for(unsigned k=0;k<7;k++){
        fill(A,n+a); fill(B,n+a);
        void *r=fw_memset(A+64+a,cs[k],n); ref_set(B+64+a,cs[k],n);
        cases++; if(r!=A+64+a||differ(A,B,1024)) bad++;
    }
    for(unsigned n=0;n<=300;n++) for(unsigned da=0;da<8;da++) for(unsigned sa=0;sa<8;sa++){
        static unsigned char S[1024]; fill(S,n^da^sa); fill(A,n); fill(B,n);
        void *r=fw_memcpy(A+64+da,S+64+sa,n); ref_cpy(B+64+da,S+64+sa,n);
        cases++; if(r!=A+64+da||differ(A,B,1024)) bad++;
    }
    for(unsigned n=0;n<=200;n++) for(int off=-40;off<=40;off++) for(unsigned al=0;al<4;al++){
        fill(A,n+off); fill(B,n+off); unsigned base=400+al;
        fw_memcpy(A+base+off,A+base,n); ref_cpy(B+base+off,B+base,n);
        cases++; if(differ(A,B,1024)) bad++;
    }
    printf("libc equivalence: %lu cases, %lu mismatches\n",cases,bad);
    return bad!=0;
}

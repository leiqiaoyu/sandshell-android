/* fakedroid PROG ARGS: run PROG under an Android-app-like seccomp filter (TRAP + ENOSYS) for sandbox tests */
typedef unsigned long u64; typedef long i64; typedef unsigned int u32; typedef unsigned short u16; typedef unsigned char u8;
static i64 sc(i64 n,i64 a,i64 b,i64 c,...){register i64 x8 __asm__("x8")=n;register i64 x0 __asm__("x0")=a;register i64 x1 __asm__("x1")=b;register i64 x2 __asm__("x2")=c;register i64 x3 __asm__("x3")=0;register i64 x4 __asm__("x4")=0;__asm__ volatile("svc 0":"+r"(x0):"r"(x8),"r"(x1),"r"(x2),"r"(x3),"r"(x4):"memory");return x0;}
struct sf{u16 code;u8 jt,jf;u32 k;};struct sp{u16 len;struct sf*f;};
static const int KILL[]={293,99,202,146,144,147,149,40,194,425,449,-1};
static const int NOSYS[]={291,439,276,452,437,279,434,441,283,285,-1};
void __attribute__((noreturn)) cmain(i64*s){int argc=s[0];char**argv=(char**)(s+1);char**envp=argv+argc+1;
 static struct sf f[64];int n=0;f[n++]=(struct sf){0x20,0,0,0};
 for(int i=0;KILL[i]>=0;i++){f[n++]=(struct sf){0x15,0,1,(u32)KILL[i]};f[n++]=(struct sf){6,0,0,0x00030000};}
 for(int i=0;NOSYS[i]>=0;i++){f[n++]=(struct sf){0x15,0,1,(u32)NOSYS[i]};f[n++]=(struct sf){6,0,0,0x00050000|38};}
 f[n++]=(struct sf){6,0,0,0x7fff0000};struct sp p={n,f};
 sc(167,38,1,0,0); i64 rr=sc(277,1,0,(i64)&p,0); if(rr<0){char eb[4]={(char)(48+(-rr)/10),(char)(48+(-rr)%10),10,0};sc(64,2,(i64)eb,3,0);sc(64,2,(i64)"fakedroid: seccomp failed\n",26,0);sc(93,1,0,0);}
 sc(221,(i64)argv[1],(i64)(argv+1),(i64)envp,0); sc(64,2,(i64)"fakedroid: exec failed\n",23,0); sc(93,127,0,0);for(;;);}
__asm__(".global _start\n_start:\n mov x0, sp\n and sp, x0, #~15\n bl cmain\n");

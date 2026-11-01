/* sstrap — in-process syscall interception loader for Sandshell (PoC, aarch64, no libc).
 *
 * Problem: static / non-glibc programs (Go, static Rust, Bun, gcc -static) bypass the
 * LD_PRELOAD shim, so (a) path syscalls the supervisor does not mediate see physical
 * paths, (b) Android's app seccomp filter SIGSYS-kills rseq/set_robust_list/accept/
 * setuid/shmget/..., (c) newer syscalls return ENOSYS.
 *
 * Idea (like gVisor systrap): we exec the program ourselves (userland ELF loader),
 * keep our code mapped in its address space, install a SIGSYS handler, and add a
 * seccomp filter that TRAPs selected syscalls unless they come from our own code
 * (seccomp_data.instruction_pointer). Android's own TRAPs also land in our handler.
 * The handler emulates/translates in-process: no ptrace, no extra process.
 *
 * Usage:  sstrap PROGRAM [ARGS...]
 * Env:    SSTRAP_ROOT=<physical rootfs>  override auto-detected prefix
 *         SSTRAP_DEBUG=1                 log each trap to stderr
 * Build:  see build.sh (static, -nostdlib, linked at 0x6000000000)
 */
typedef unsigned long u64; typedef long i64; typedef unsigned int u32; typedef int i32;
typedef unsigned short u16; typedef unsigned char u8;

#define REGION_HI 0x60u          /* loader text lives at 0x60_0000_0000 .. +16MB */
#define REGION_LOW_LIMIT 0x01000000u

/* ---------------- raw syscalls (only ever issued from this binary's text) ---------- */
static inline i64 sc6(i64 n,i64 a,i64 b,i64 c,i64 d,i64 e,i64 f){
  register i64 x8 __asm__("x8")=n; register i64 x0 __asm__("x0")=a; register i64 x1 __asm__("x1")=b;
  register i64 x2 __asm__("x2")=c; register i64 x3 __asm__("x3")=d; register i64 x4 __asm__("x4")=e; register i64 x5 __asm__("x5")=f;
  __asm__ volatile("svc 0":"+r"(x0):"r"(x8),"r"(x1),"r"(x2),"r"(x3),"r"(x4),"r"(x5):"memory");
  return x0;
}
#define S0(n) sc6(n,0,0,0,0,0,0)
#define S1(n,a) sc6(n,(i64)(a),0,0,0,0,0)
#define S2(n,a,b) sc6(n,(i64)(a),(i64)(b),0,0,0,0)
#define S3(n,a,b,c) sc6(n,(i64)(a),(i64)(b),(i64)(c),0,0,0)
#define S4(n,a,b,c,d) sc6(n,(i64)(a),(i64)(b),(i64)(c),(i64)(d),0,0)
#define S5(n,a,b,c,d,e) sc6(n,(i64)(a),(i64)(b),(i64)(c),(i64)(d),(i64)(e),0)
#define S6(n,a,b,c,d,e,f) sc6(n,(i64)(a),(i64)(b),(i64)(c),(i64)(d),(i64)(e),(i64)(f))

enum { NR_getxattr=8,NR_lgetxattr=9,NR_setxattr=5,NR_lsetxattr=6,NR_listxattr=11,NR_llistxattr=12,
  NR_removexattr=14,NR_lremovexattr=15,NR_getcwd=17,NR_inotify_add_watch=27,NR_mknodat=33,NR_mkdirat=34,
  NR_unlinkat=35,NR_symlinkat=36,NR_linkat=37,NR_renameat=38,NR_mount=40,NR_truncate=45,NR_faccessat=48,
  NR_chdir=49,NR_fchmod=52,NR_fchmodat=53,NR_openat=56,NR_close=57,NR_read=63,NR_write=64,NR_pread64=67,
  NR_readlinkat=78,NR_newfstatat=79,NR_fstat=80,NR_getuid=174,NR_getgid=176,NR_exit=93,NR_exit_group=94,NR_set_robust_list=99,NR_rt_sigaction=134,
  NR_rt_sigprocmask=135,NR_setgid=144,NR_setreuid=145,NR_setuid=146,NR_setresuid=147,NR_setresgid=149,
  NR_setfsuid=151,NR_setfsgid=152,NR_setgroups=159,NR_setregid=143,NR_prctl=167,NR_getpid=172,
  NR_shmget=194,NR_bind=200,NR_accept=202,NR_connect=203,NR_munmap=215,NR_execve=221,NR_mmap=222,
  NR_mprotect=226,NR_accept4=242,NR_renameat2=276,NR_seccomp=277,NR_memfd_create=279,NR_execveat=281,
  NR_statx=291,NR_rseq=293,NR_io_uring_setup=425,NR_openat2=437,NR_faccessat2=439,NR_futex_waitv=449,
  NR_fchmodat2=452 };

#define AT_FDCWD (-100)
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_EMPTY_PATH 0x1000
#define AT_NO_AUTOMOUNT 0x800
#define EPERM 1
#define ENOENT 2
#define ENOEXEC 8
#define EFAULT 14
#define EEXIST 17
#define EINVAL 22
#define ERANGE 34
#define ENOSYS 38
#define ENAMETOOLONG 36
#define EACCES 13
#define ENOTDIR 20
#define EOPNOTSUPP 95
#define SIGSYS 31

/* ---------------- tiny libc ---------------- */
static u64 slen(const char*s){u64 n=0;while(s[n])n++;return n;}
static int spre(const char*a,const char*p){while(*p){if(*a!=*p)return 0;a++;p++;}return 1;}
static int seq(const char*a,const char*b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
static void scpy(char*d,const char*s){while((*d++=*s++));}
void *memset(void*p,int c,u64 n){u8*q=p;while(n--)*q++=(u8)c;return p;}
void *memcpy(void*d,const void*s,u64 n){u8*a=d;const u8*b=s;while(n--)*a++=*b++;return d;}
void *memmove(void*d,const void*s,u64 n){u8*a=d;const u8*b=s;if(a<b){while(n--)*a++=*b++;}else{a+=n;b+=n;while(n--)*--a=*--b;}return d;}
static void wr(const char*s){S3(NR_write,2,s,slen(s));}
static void wrn(i64 v){char b[24];int i=23;b[i]=0;int neg=v<0;u64 u=neg?-v:v;do{b[--i]='0'+u%10;u/=10;}while(u);if(neg)b[--i]='-';wr(b+i);}
static __attribute__((noreturn)) void die(const char*m,i64 e){wr("sstrap: ");wr(m);if(e){wr(" (err ");wrn(e);wr(")");}wr("\n");S1(NR_exit_group,127);for(;;);}

/* ---------------- global state (read-mostly after startup) ---------------- */
static char G_prefix[512];   /* physical rootfs, "" = no translation */
static u64  G_plen;
static char G_self[512];     /* our own path as the program namespace sees it */
static char G_exe[512];      /* virtual path of the loaded program (for /proc/self/exe) */
static int  G_debug;
static u64  G_prog_sigsys[4];/* program's view of its SIGSYS action (never installed) */

/* ---------------- path translation ---------------- */
static const char *PASS[]={"/proc","/dev","/sys","/system","/apex","/vendor","/data","/storage","/sdcard","/odm","/product","/linkerconfig",0};
static int passthrough(const char*p){
  for(int i=0;PASS[i];i++){const char*q=PASS[i];u64 n=slen(q);if(spre(p,q)&&(p[n]==0||p[n]=='/'))return 1;}
  return 0;
}
/* virtual -> physical for syscalls the supervisor does NOT mediate. returns ptr to use. */
static const char *v2p(const char*p,char*buf,u64 cap,i64*err){
  *err=0;
  if(!p||!G_plen||p[0]!='/')return p;
  if(spre(p,G_prefix)&&(p[G_plen]==0||p[G_plen]=='/'))return p;
  if(passthrough(p))return p;
  u64 n=slen(p);if(G_plen+n+1>cap){*err=-ENAMETOOLONG;return p;}
  memcpy(buf,G_prefix,G_plen);memcpy(buf+G_plen,p,n+1);
  if(n==1){buf[G_plen]=0;} /* "/" -> prefix */
  return buf;
}
/* physical -> virtual in place; s has length n (no NUL needed). returns new length */
static i64 p2v(char*s,i64 n){
  if(!G_plen||n<(i64)G_plen)return n;
  for(u64 i=0;i<G_plen;i++)if(s[i]!=G_prefix[i])return n;
  if(n>(i64)G_plen&&s[G_plen]!='/')return n;
  if(n==(i64)G_plen){s[0]='/';return 1;}
  memmove(s,s+G_plen,n-G_plen);return n-G_plen;
}


/* ---------------- slow path: same syscall issued from OUTSIDE region 0x60 ----------------
 * A patched Sandshell supervisor (t18+) ALLOWs syscalls whose IP high word is 0x60 (ours), so
 * our direct calls skip the ~90us USER_NOTIF round trip. For anything we do not want to emulate
 * (special paths, errors) we re-issue through a second, file-backed mapping of our own text at a
 * kernel-chosen address: there the supervisor sees it exactly as before. */
extern i64 slow_stub(i64,i64,i64,i64,i64,i64,i64 nr);
__asm__(".text\n.global slow_stub\nslow_stub:\n mov x8, x6\n svc #0\n ret\n");
typedef i64 (*slowfn)(i64,i64,i64,i64,i64,i64,i64);
static slowfn G_slow;
static u32 G_appuid=~0u, G_appgid=~0u;
static i64 SL(i64 nr,i64 a,i64 b,i64 c,i64 d,i64 e){ return G_slow? G_slow(a,b,c,d,e,0,nr) : sc6(nr,a,b,c,d,e,0); }
static void init_slow(void){
  i64 fd=S4(NR_openat,AT_FDCWD,"/proc/self/exe",02000000,0); if(fd<0)return;
  i64 m=S6(NR_mmap,0x5f00000000ul,0x4000+(((u64)slow_stub-0x6000000000ul)&~4095ul),5/*R|X*/,0x02|0x100000/*PRIVATE|FIXED_NOREPLACE*/,fd,0);
  S1(NR_close,fd); if(m<0)return;
  if((u64)m!=0x5f00000000ul)return; /* fixed so exec'd images (inherited filter) agree */
  G_slow=(slowfn)((u64)m+((u64)slow_stub-0x6000000000ul));
}
/* paths whose meaning the supervisor owns (bridges, injected files): always slow */
static int special(const char*p){
  static const char*SP[]={"/mnt","/tmp","/etc/ld.so.cache","/etc/nsswitch.conf","/usr/lib/python3",0};
  for(int i=0;SP[i];i++){u64 n=slen(SP[i]); if(spre(p,SP[i])&&(p[n]==0||p[n]=='/'||SP[i][n-1]=='3'))return 1;}
  return 0;
}
struct kstat;
static void fixids(void*stp);
/* path syscall with (dfd,path) at args 0/1: fast direct, else / on failure via supervisor */
static i64 pathcall(i64 nr,i64 dfd,const char*path,i64 c,i64 d,i64 e,int fixst,void*st){
  char b[4096]; i64 err=0; const char*p=path; int slow=0;
  if(!path)return -EFAULT;
  if(path[0]=='/'){ if(special(path))slow=1; else { p=v2p(path,b,sizeof b,&err); if(err)return err; } }
  i64 r=-ENOENT;
  if(!slow){ r=sc6(nr,dfd,(i64)p,c,d,e,0); if(r>=0){ if(fixst)fixids(st); return r; }
    if(r!=-ENOENT&&r!=-EACCES&&r!=-EPERM&&r!=-ENOTDIR)return r; if(!G_slow)return r; }
  u64 n=slen(path); if(n>=sizeof b)return -ENAMETOOLONG; memcpy(b,path,n+1); /* private copy: supervisor rewrites in place */
  r=SL(nr,dfd,(i64)b,c,d,e);
  return r;
}

/* ---------------- signal frame layout (arm64) ---------------- */
struct uctx { u64 uc_flags,uc_link,ss_sp; i32 ss_flags,ss_pad; u64 ss_size,uc_sigmask; u8 unused[120]; u64 align_pad;
  u64 fault_address; u64 regs[31]; u64 sp,pc,pstate; };
_Static_assert(__builtin_offsetof(struct uctx,regs)==184,"ucontext layout");
struct sinfo { i32 si_signo,si_errno,si_code,pad; u64 call_addr; i32 syscall; u32 arch; };

struct ksigaction { u64 handler; u64 flags; u64 restorer; u64 mask; };
#define SA_SIGINFO 4
#define SA_RESTORER 0x04000000
#define SA_ONSTACK 0x08000000
#define SA_NODEFER 0x40000000
#define SIGSYS_BIT (1ul<<(SIGSYS-1))
extern void sstrap_restorer(void);
__asm__(".text\n.global sstrap_restorer\nsstrap_restorer:\n mov x8, #139\n svc #0\n");

/* ---------------- emulations ---------------- */
struct kstat { u64 dev,ino; u32 mode,nlink,uid,gid; u64 rdev,pad1; i64 size; i32 blksize,pad2; i64 blocks;
  i64 atime,atime_ns,mtime,mtime_ns,ctime,ctime_ns; u32 unused[2]; };
struct sxts { i64 sec; u32 nsec; i32 r; };
struct kstatx { u32 mask,blksize; u64 attributes; u32 nlink,uid,gid; u16 mode,pad1; u64 ino,size,blocks,attributes_mask;
  struct sxts atime,btime,ctime,mtime; u32 rdev_major,rdev_minor,dev_major,dev_minor; u64 spare[14]; };
_Static_assert(sizeof(struct kstatx)==256,"statx");
static void fixids(void*p){ struct kstat*st=p; if(!st)return; if(st->uid==G_appuid)st->uid=0; if(st->gid==G_appgid)st->gid=0; }
static i64 x_fstatat(i64 dfd,const char*path,struct kstat*st,i64 fl){
  if(path&&path[0]==0&&(fl&AT_EMPTY_PATH)){ i64 r=S2(NR_fstat,dfd,st); if(r>=0)fixids(st); return r; }
  return pathcall(NR_newfstatat,dfd,path,(i64)st,fl,0,1,st); }
static i64 x_fstat(i64 fd,struct kstat*st){ i64 r=S2(NR_fstat,fd,st); if(r>=0)fixids(st); return r; }
static i64 x_faccessat(i64 dfd,const char*path,i64 mode,i64 fl){ return pathcall(NR_faccessat,dfd,path,mode,fl,0,0,0); }
static i64 x_openat(i64 dfd,const char*path,i64 fl,i64 mode){ return pathcall(NR_openat,dfd,path,fl,mode,0,0,0); }
static u32 dmaj(u64 d){return (u32)(((d>>8)&0xfff)|((d>>32)&~0xfffull));}
static u32 dmin(u64 d){return (u32)((d&0xff)|((d>>12)&~0xffull));}

static i64 emu_statx(i64 dfd,const char*path,i64 flags,u64 mask,struct kstatx*sx){
  (void)mask; struct kstat st;
  i64 r;
  r=x_fstatat(dfd,path,&st,flags&(AT_SYMLINK_NOFOLLOW|AT_EMPTY_PATH|AT_NO_AUTOMOUNT));
  if(r<0)return r;
  memset(sx,0,sizeof *sx);
  sx->mask=0x7ff; sx->blksize=st.blksize; sx->nlink=st.nlink; sx->uid=st.uid; sx->gid=st.gid; sx->mode=(u16)st.mode;
  sx->ino=st.ino; sx->size=st.size; sx->blocks=st.blocks;
  sx->atime.sec=st.atime;sx->atime.nsec=st.atime_ns; sx->mtime.sec=st.mtime;sx->mtime.nsec=st.mtime_ns;
  sx->ctime.sec=st.ctime;sx->ctime.nsec=st.ctime_ns;
  sx->rdev_major=dmaj(st.rdev);sx->rdev_minor=dmin(st.rdev);sx->dev_major=dmaj(st.dev);sx->dev_minor=dmin(st.dev);
  return 0;
}

static i64 emu_memfd(const char*name,u64 flags){
  (void)name; char p[64]="/tmp/.sstrap-memfd-"; u64 x; S3(278,&x,8,0);
  char*q=p+slen(p); for(int i=0;i<12;i++){*q++="0123456789abcdef"[x&15];x>>=4;} *q=0;
  i64 fd=S4(NR_openat,AT_FDCWD,p,02|0100|0200|((flags&1)?02000000:0),0600); /* RDWR|CREAT|EXCL[|CLOEXEC] */
  if(fd<0)return fd; S3(NR_unlinkat,AT_FDCWD,p,0); return fd;
}

static int is_proc_self(const char*p,const char*leaf){
  /* /proc/self/<leaf> or /proc/<ourpid>/<leaf> or /proc/thread-self/<leaf> */
  if(!spre(p,"/proc/"))return 0; p+=6;
  if(spre(p,"self/"))p+=5; else if(spre(p,"thread-self/"))p+=12;
  else { char b[24];int i=23;b[i]=0;u64 u=S0(NR_getpid);do{b[--i]='0'+u%10;u/=10;}while(u);
         if(!spre(p,b+i))return 0; p+=slen(b+i); if(*p!='/')return 0; p++; }
  return seq(p,leaf);
}

static i64 emu_readlinkat(i64 dfd,const char*path,char*buf,i64 sz){
  if(sz<=0)return -EINVAL;
  if(dfd==AT_FDCWD&&path&&is_proc_self(path,"exe")){i64 n=slen(G_exe);if(n>sz)n=sz;memcpy(buf,G_exe,n);return n;}
  if(dfd==AT_FDCWD&&path&&is_proc_self(path,"cwd")){
    char t[4096]; i64 n=S2(NR_getcwd,t,sizeof t); if(n<0)return n; n=p2v(t,n-1); if(n>sz)n=sz; memcpy(buf,t,n); return n; }
  if(dfd==AT_FDCWD&&path&&is_proc_self(path,"root")){buf[0]='/';return 1;}
  i64 n=pathcall(NR_readlinkat,dfd,path,(i64)buf,sz,0,0,0); if(n<=0)return n; return p2v(buf,n);
}

static i64 emu_getcwd(char*buf,u64 sz){
  char t[4096]; i64 n=S2(NR_getcwd,t,sizeof t); if(n<0)return n;
  i64 m=p2v(t,n-1); if((u64)m+1>sz)return -ERANGE; memcpy(buf,t,m); buf[m]=0; return m+1;
}

static i64 emu_sockaddr(i64 nr,i64 fd,const u8*sa,i64 len){
  if(!sa||len<3||sa[0]!=1||sa[1]!=0||sa[2]!='/'||!G_plen) return S3(nr,fd,sa,len);
  char path[110]; i64 pl=len-2; if(pl>108)pl=108; memcpy(path,sa+2,pl); path[pl]=0;
  char b[4096]; i64 e; const char*p=v2p(path,b,sizeof b,&e); if(e)return e;
  if(p==path)return S3(nr,fd,sa,len);
  u64 n=slen(p); if(n>107)return -ENAMETOOLONG;
  u8 ns[110]; memset(ns,0,sizeof ns); ns[0]=1; memcpy(ns+2,p,n+1);
  return S3(nr,fd,ns,2+n+1);
}

static i64 emu_rt_sigaction(i64 sig,const struct ksigaction*act,struct ksigaction*old,i64 ssz){
  if(sig==SIGSYS){
    if(old){memcpy(old,G_prog_sigsys,sizeof(struct ksigaction));}
    if(act){memcpy(G_prog_sigsys,act,sizeof(struct ksigaction));}
    return 0;
  }
  if(act&&(act->mask&SIGSYS_BIT)){struct ksigaction a=*act; a.mask&=~SIGSYS_BIT; return S4(NR_rt_sigaction,sig,&a,old,ssz);}
  return S4(NR_rt_sigaction,sig,act,old,ssz);
}
static i64 emu_rt_sigprocmask(i64 how,const u64*set,u64*old,i64 ssz){
  if(set&&how!=1/*SIG_UNBLOCK*/&&(*set&SIGSYS_BIT)){u64 s=*set&~SIGSYS_BIT; return S4(NR_rt_sigprocmask,how,&s,old,ssz);}
  return S4(NR_rt_sigprocmask,how,set,old,ssz);
}

/* execve: re-enter the loader so the new image also gets the handler (filter is inherited) */
static i64 emu_execve(i64 dfd,const char*upath,char*const*argv,char*const*envp,i64 flags){
  if(!upath)return -EFAULT;
  if(dfd!=AT_FDCWD||(flags&AT_EMPTY_PATH)){ /* PoC: only plain paths */
    if(dfd!=AT_FDCWD&&upath[0]!='/')return -ENOSYS;
  }
  /* work on private copies: the Sandshell supervisor may rewrite path strings in place,
     which must never touch the program's own memory */
  static char path[4096], self[600]; u64 pl=slen(upath); if(pl>=sizeof path)return -36; memcpy(path,upath,pl+1);
  char pathv[4096]; memcpy(pathv,path,pl+1); /* virtual name for argv */
  i64 r=x_faccessat(AT_FDCWD,path,1/*X_OK*/,0);
  if(G_debug){wr("[sstrap] exec '");wr(pathv);wr("' access=");wrn(r);wr(" now '");wr(path);wr("'\n");}
  if(r<0)return r;
  memcpy(path,pathv,pl+1);
  struct kstat st; r=x_fstatat(AT_FDCWD,path,&st,0); if(r<0)return r;
  if((st.mode&0170000)!=0100000)return -13; /* EACCES on non-regular */
  u64 argc=0; if(argv)while(argv[argc])argc++;
  u64 need=(argc+4)*sizeof(char*);
  char*stackv[512]; char**nv=stackv; int mapped=0;
  if(argc+4>512){ i64 m=S6(NR_mmap,0,(need+4095)&~4095ul,3,0x22,-1,0); if(m<0)return m; nv=(char**)m; mapped=1; }
  /* try: virtual self, then physical self */
  for(int attempt=0;attempt<2;attempt++){
    /* physical self first: inside Sandshell the kernel needs the real path for execve */
    i64 e2=0; const char*sp=G_self; char pb[600];
    if(attempt==0){ sp=v2p(G_self,pb,sizeof pb,&e2); if(e2)sp=G_self; }
    else { if(sp==G_self&&!G_plen)break; }
    scpy(self,sp); memcpy(path,pathv,pl+1);
    u64 k=0; nv[k++]=self; nv[k++]=(char*)"--sstrap-exec"; nv[k++]=path;
    if(argc==0)nv[k++]=pathv; else for(u64 i=0;i<argc;i++)nv[k++]=argv[i];
    nv[k]=0;
    r=S3(NR_execve,self,nv,envp);
    if(G_debug){wr("[sstrap] execve self '");wr(sp);wr("' -> ");wrn(r);wr(" (self now '");wr(self);wr("')\n");}
  }
  if(mapped)S2(NR_munmap,nv,(need+4095)&~4095ul);
  return r;
}

/* ---------------- the SIGSYS handler ---------------- */
static void dbg(i64 nr,i64 ret){ if(!G_debug)return; wr("[sstrap] nr=");wrn(nr);wr(" -> ");wrn(ret);wr("\n"); }

static void handler(int sig,struct sinfo*si,struct uctx*uc){
  (void)sig;
  u64*R=uc->regs; i64 nr=si->syscall; i64 ret=-ENOSYS;
  i64 a0=R[0],a1=R[1],a2=R[2],a3=R[3],a4=R[4];
  char b1[4096]; i64 e; const char*p;
  switch(nr){
  /* ---- Android-killed syscalls ---- */
  case NR_rseq: case NR_set_robust_list: case NR_io_uring_setup: case NR_futex_waitv: ret=-ENOSYS; break;
  case NR_accept: ret=S4(NR_accept4,a0,a1,a2,0); break;
  case NR_setuid: case NR_setgid: case NR_setreuid: case NR_setregid: case NR_setresuid: case NR_setresgid:
  case NR_setgroups: ret=0; break;
  case NR_setfsuid: case NR_setfsgid: ret=0; break;
  case NR_mount: ret=-EPERM; break;
  case NR_shmget: ret=-ENOSYS; break; /* TODO: emulate SysV shm on memfd */
  /* ---- ENOSYS on Android kernel/supervisor ---- */
  case NR_statx: ret=emu_statx(a0,(const char*)a1,a2,a3,(struct kstatx*)a4); break;
  case NR_faccessat2: ret=x_faccessat(a0,(const char*)a1,a2,0); break;
  case NR_renameat2:
    if(a4==0)ret=S4(NR_renameat,a0,a1,a2,a3);
    else if(a4==1){ struct kstat st; i64 r=x_fstatat(a2,(const char*)a3,&st,AT_SYMLINK_NOFOLLOW);
      ret= r==0? -EEXIST : (r!=-ENOENT? r : S4(NR_renameat,a0,a1,a2,a3)); }
    else ret=-EINVAL;
    break;
  case NR_fchmodat2:
    if(a3==0)ret=S3(NR_fchmodat,a0,a1,a2);
    else if((a3&AT_EMPTY_PATH)&&a1&&((const char*)a1)[0]==0)ret=S2(NR_fchmod,a0,a2);
    else if(a3&AT_SYMLINK_NOFOLLOW)ret=-EOPNOTSUPP; else ret=-EINVAL;
    break;
  case NR_openat2: { const u64*how=(const u64*)a2; if(!how||a3<16){ret=-EINVAL;break;}
    ret=S4(NR_openat,a0,a1,how[0],how[1]); break; }
  case NR_memfd_create: ret=emu_memfd((const char*)a0,a1); break;
  /* ---- unmediated path syscalls ---- */
  case NR_chdir: p=v2p((const char*)a0,b1,sizeof b1,&e); ret=e?e:S1(NR_chdir,p); break;
  case NR_truncate: p=v2p((const char*)a0,b1,sizeof b1,&e); ret=e?e:S2(NR_truncate,p,a1); break;
  case NR_getxattr: case NR_lgetxattr: case NR_setxattr: case NR_lsetxattr:
  case NR_listxattr: case NR_llistxattr: case NR_removexattr: case NR_lremovexattr:
    p=v2p((const char*)a0,b1,sizeof b1,&e); ret=e?e:S5(nr,p,a1,a2,a3,a4); break;
  case NR_inotify_add_watch: p=v2p((const char*)a1,b1,sizeof b1,&e); ret=e?e:S3(nr,a0,p,a2); break;
  case NR_mknodat: if(a0==AT_FDCWD){p=v2p((const char*)a1,b1,sizeof b1,&e); ret=e?e:S4(nr,a0,p,a2,a3);} else ret=S4(nr,a0,a1,a2,a3); break;
  case NR_symlinkat: /* target text stays virtual; translate link location */
    if(a1==AT_FDCWD){p=v2p((const char*)a2,b1,sizeof b1,&e); ret=e?e:S3(nr,a0,a1,p);} else ret=S3(nr,a0,a1,a2); break;
  case NR_linkat: { char b2[4096]; const char*p1=(const char*)a1,*p2=(const char*)a3; i64 e2=0; e=0;
    if(a0==AT_FDCWD)p1=v2p(p1,b1,sizeof b1,&e); if(a2==AT_FDCWD)p2=v2p(p2,b2,sizeof b2,&e2);
    ret=e?e:e2?e2:S5(nr,a0,p1,a2,p2,a4); break; }
  case NR_bind: case NR_connect: ret=emu_sockaddr(nr,a0,(const u8*)a1,a2); break;
  case NR_getcwd: ret=emu_getcwd((char*)a0,a1); break;
  case NR_fstat: ret=x_fstat(a0,(struct kstat*)a1); break;
  case NR_newfstatat: ret=x_fstatat(a0,(const char*)a1,(struct kstat*)a2,a3); break;
  case NR_faccessat: ret=x_faccessat(a0,(const char*)a1,a2,a3); break;
  case NR_openat: ret=x_openat(a0,(const char*)a1,a2,a3); break;
  case NR_getuid: case 175: case NR_getgid: case 177: ret=0; break;
  case 158/*getgroups*/: if(a0==0)ret=1; else if(a0<0)ret=-EINVAL; else { ((u32*)a1)[0]=0; ret=1; } break;
  case NR_readlinkat: ret=emu_readlinkat(a0,(const char*)a1,(char*)a2,a3); break;
  case NR_execve: ret=emu_execve(AT_FDCWD,(const char*)a0,(char*const*)a1,(char*const*)a2,0); break;
  case NR_execveat: ret=emu_execve(a0,(const char*)a1,(char*const*)a2,(char*const*)a3,a4); break;
  /* ---- keep SIGSYS deliverable ---- */
  case NR_rt_sigaction: ret=emu_rt_sigaction(a0,(const struct ksigaction*)a1,(struct ksigaction*)a2,a3); break;
  case NR_rt_sigprocmask: ret=emu_rt_sigprocmask(a0,(const u64*)a1,(u64*)a2,a3); break;
  default: ret=-ENOSYS; break;
  }
  dbg(nr,ret);
  R[0]=(u64)ret;
  uc->uc_sigmask&=~SIGSYS_BIT;
}
extern void sstrap_handler_entry(void);
/* plain C function pointer is fine: SA_SIGINFO handler(int, siginfo*, void*) */

/* ---------------- seccomp filter ---------------- */
struct sock_filter { u16 code; u8 jt,jf; u32 k; };
struct sock_fprog { u16 len; struct sock_filter*filter; };
#define BPF_LD 0x00
#define BPF_W 0x00
#define BPF_ABS 0x20
#define BPF_JMP 0x05
#define BPF_JEQ 0x10
#define BPF_JGE 0x30
#define BPF_K 0x00
#define BPF_RET 0x06
#define STMT(c,k) ((struct sock_filter){(u16)(c),0,0,(u32)(k)})
#define JUMP(c,k,t,f) ((struct sock_filter){(u16)(c),(u8)(t),(u8)(f),(u32)(k)})
#define SECCOMP_RET_ALLOW 0x7fff0000u
#define SECCOMP_RET_TRAP 0x00030000u
#define AUDIT_ARCH_AARCH64 0xc00000b7u

static const int TRAPPED[]={158,NR_fstat,NR_newfstatat,NR_faccessat,NR_getuid,175,NR_getgid,177,NR_statx,NR_faccessat2,NR_renameat2,NR_fchmodat2,NR_openat2,NR_memfd_create,
  NR_chdir,NR_truncate,NR_getxattr,NR_lgetxattr,NR_setxattr,NR_lsetxattr,NR_listxattr,NR_llistxattr,
  NR_removexattr,NR_lremovexattr,NR_inotify_add_watch,NR_mknodat,NR_symlinkat,NR_linkat,NR_bind,NR_connect,
  NR_getcwd,NR_readlinkat,NR_execve,NR_execveat,NR_rt_sigaction,NR_rt_sigprocmask,
  /* also catch Android-killed ones ourselves so behaviour is identical everywhere */
  NR_rseq,NR_set_robust_list,NR_accept,NR_setuid,NR_setgid,NR_setreuid,NR_setregid,NR_setresuid,NR_setresgid,
  NR_setgroups,NR_setfsuid,NR_setfsgid,NR_mount,NR_shmget,NR_io_uring_setup,NR_futex_waitv,-1};

static void install_filter(void){
  static struct sock_filter f[128]; int n=0;
  f[n++]=STMT(BPF_LD|BPF_W|BPF_ABS,4);                         /* arch */
  f[n++]=JUMP(BPF_JMP|BPF_JEQ|BPF_K,AUDIT_ARCH_AARCH64,1,0);
  f[n++]=STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW);
  if(G_slow){ /* our slow stub mapping: let it through to the supervisor untouched */
    u64 a=(u64)G_slow;
    f[n++]=STMT(BPF_LD|BPF_W|BPF_ABS,12);
    f[n++]=JUMP(BPF_JMP|BPF_JEQ|BPF_K,(u32)(a>>32),0,4);
    f[n++]=STMT(BPF_LD|BPF_W|BPF_ABS,8);
    f[n++]=JUMP(BPF_JMP|BPF_JGE|BPF_K,(u32)a,0,2);
    f[n++]=JUMP(BPF_JMP|BPF_JGE|BPF_K,(u32)a+16,1,0);
    f[n++]=STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW);
  }
  f[n++]=STMT(BPF_LD|BPF_W|BPF_ABS,12);                        /* IP high word */
  f[n++]=JUMP(BPF_JMP|BPF_JEQ|BPF_K,REGION_HI,0,3);            /* not ours -> nr check */
  f[n++]=STMT(BPF_LD|BPF_W|BPF_ABS,8);                         /* IP low word */
  f[n++]=JUMP(BPF_JMP|BPF_JGE|BPF_K,REGION_LOW_LIMIT,1,0);
  f[n++]=STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW);                /* issued by sstrap itself */
  f[n++]=STMT(BPF_LD|BPF_W|BPF_ABS,0);                         /* nr */
  /* openat: only read-only opens are fast-pathed; writes/creates keep the supervisor's policy */
  f[n++]=JUMP(BPF_JMP|BPF_JEQ|BPF_K,NR_openat,0,4);
  f[n++]=STMT(BPF_LD|BPF_W|BPF_ABS,32);                        /* args[2] low word = flags */
  f[n++]=JUMP(BPF_JMP|0x40/*JSET*/|BPF_K,0x3|0x40|0x200|0x200000|0x400000,1,0);
  f[n++]=STMT(BPF_RET|BPF_K,SECCOMP_RET_TRAP);
  f[n++]=STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW);
  f[n++]=STMT(BPF_LD|BPF_W|BPF_ABS,0);
  int cnt=0; while(TRAPPED[cnt]>=0)cnt++;
  for(int i=0;i<cnt;i++) f[n++]=JUMP(BPF_JMP|BPF_JEQ|BPF_K,TRAPPED[i],(u8)(cnt-i),0);
  f[n++]=STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW);
  f[n++]=STMT(BPF_RET|BPF_K,SECCOMP_RET_TRAP);
  struct sock_fprog prog={(u16)n,f};
  i64 r=S5(NR_prctl,38/*PR_SET_NO_NEW_PRIVS*/,1,0,0,0); if(r<0)die("no_new_privs",r);
  r=S3(NR_seccomp,1/*SET_MODE_FILTER*/,0,&prog);
  if(r<0){ i64 r2=S5(NR_prctl,22/*PR_SET_SECCOMP*/,2/*MODE_FILTER*/,(i64)&prog,0,0);
    if(G_debug){wr("[sstrap] seccomp() ");wrn(r);wr(", prctl fallback ");wrn(r2);wr("\n");}
    if(r2<0)die("seccomp filter (seccomp+prctl)",r2); }
}

static void install_handler(void){
  struct ksigaction a; memset(&a,0,sizeof a);
  a.handler=(u64)handler; a.flags=SA_SIGINFO|SA_RESTORER|SA_ONSTACK|SA_NODEFER; a.restorer=(u64)sstrap_restorer; a.mask=0;
  i64 r=S4(NR_rt_sigaction,SIGSYS,&a,0,8); if(r<0)die("sigaction",r);
  u64 s=SIGSYS_BIT; S4(NR_rt_sigprocmask,1,&s,0,8);
}

/* ---------------- ELF loader ---------------- */
typedef struct { u8 ident[16]; u16 type,machine; u32 version; u64 entry,phoff,shoff; u32 flags; u16 ehsize,phentsize,phnum,shentsize,shnum,shstrndx; } Ehdr;
typedef struct { u32 type,flags; u64 offset,vaddr,paddr,filesz,memsz,align; } Phdr;
#define PT_LOAD 1
#define PT_INTERP 3
#define PT_PHDR 6
#define PAGE 4096ul
#define PDOWN(x) ((x)&~(PAGE-1))
#define PUP(x) (((x)+PAGE-1)&~(PAGE-1))

struct loaded { u64 base,entry,phdr; u16 phnum; char interp[256]; };

static void load_elf(const char*path,struct loaded*L,int want_interp){
  i64 fd=x_openat(AT_FDCWD,path,02000000,0); if(fd<0)die("open program",fd);
  Ehdr eh; if(S4(NR_pread64,fd,&eh,sizeof eh,0)!=sizeof eh)die("read ELF header",0);
  if(eh.ident[0]!=0x7f||eh.ident[1]!='E'||eh.ident[2]!='L'||eh.ident[3]!='F')die("not ELF",0);
  if(eh.machine!=183)die("not aarch64",0);
  if(eh.phnum>64)die("too many phdrs",0);
  Phdr ph[64]; if(S4(NR_pread64,fd,ph,eh.phnum*sizeof(Phdr),eh.phoff)!=(i64)(eh.phnum*sizeof(Phdr)))die("read phdrs",0);
  u64 lo=~0ul,hi=0; L->interp[0]=0; u64 phdr_va=0;
  for(int i=0;i<eh.phnum;i++){
    if(ph[i].type==PT_LOAD){ if(PDOWN(ph[i].vaddr)<lo)lo=PDOWN(ph[i].vaddr); if(ph[i].vaddr+ph[i].memsz>hi)hi=ph[i].vaddr+ph[i].memsz; }
    if(ph[i].type==PT_INTERP&&want_interp){ u64 n=ph[i].filesz<255?ph[i].filesz:255; S4(NR_pread64,fd,L->interp,n,ph[i].offset); L->interp[n]=0; }
    if(ph[i].type==PT_PHDR)phdr_va=ph[i].vaddr;
  }
  if(lo==~0ul)die("no PT_LOAD",0);
  u64 base=0;
  if(eh.type==3){ /* ET_DYN: reserve span */
    i64 m=S6(NR_mmap,0,PUP(hi)-lo,0,0x22,-1,0); if(m<0)die("reserve",m); base=(u64)m-lo;
  } else if(eh.type!=2) die("bad ELF type",0);
  for(int i=0;i<eh.phnum;i++){ if(ph[i].type!=PT_LOAD)continue;
    Phdr*p=&ph[i]; int prot=((p->flags&4)?1:0)|((p->flags&2)?2:0)|((p->flags&1)?4:0);
    u64 va=base+p->vaddr, start=PDOWN(va), off=PDOWN(p->offset), fend=va+p->filesz, mend=va+p->memsz;
    if(p->filesz){ i64 m=S6(NR_mmap,start,PUP(fend)-start,prot|2,0x12/*PRIVATE|FIXED*/,fd,off); if(m<0)die("map segment",m); }
    if(p->memsz>p->filesz){
      u64 zs=fend, ze=PUP(fend); if(ze>mend)ze=mend;
      if(p->filesz&&ze>zs) memset((void*)zs,0,ze-zs);
      u64 as=p->filesz?PUP(fend):start; if(mend>as){ i64 m=S6(NR_mmap,as,PUP(mend)-as,prot|2,0x32/*PRIVATE|FIXED|ANON*/,-1,0); if(m<0)die("map bss",m); }
    }
    if(!(prot&2)) S3(NR_mprotect,start,PUP(mend)-start,prot);
    if(!phdr_va&&p->offset<=eh.phoff&&eh.phoff<p->offset+p->filesz) phdr_va=p->vaddr+(eh.phoff-p->offset);
  }
  S1(NR_close,fd);
  L->base=base; L->entry=base+eh.entry; L->phdr=base+phdr_va; L->phnum=eh.phnum;
}

/* resolve program by PATH if it has no slash */
static const char*find_prog(const char*name,char**envp,char*buf){
  for(const char*c=name;*c;c++)if(*c=='/')return name;
  const char*path="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
  for(char**e=envp;*e;e++)if(spre(*e,"PATH=")){path=*e+5;break;}
  while(*path){ const char*s=path; while(*path&&*path!=':')path++; u64 n=path-s; if(*path)path++;
    if(n==0||n+slen(name)+2>4096)continue; memcpy(buf,s,n); buf[n]='/'; scpy(buf+n+1,name);
    if(x_faccessat(AT_FDCWD,buf,1,0)==0)return buf; }
  return name;
}

#define AT_NULL 0
#define AT_PHDR 3
#define AT_PHENT 4
#define AT_PHNUM 5
#define AT_BASE 7
#define AT_ENTRY 9
#define AT_EXECFN 31

extern void jump_to(u64 sp,u64 entry) __attribute__((noreturn));
__asm__(".text\n.global jump_to\njump_to:\n mov sp, x0\n mov x16, x1\n mov x0, #0\n mov x1, #0\n mov x2, #0\n mov x3, #0\n"
        " mov x29, #0\n mov x30, #0\n br x16\n");

void __attribute__((noreturn)) cmain(u64*sp){
  i64 argc=(i64)sp[0]; char**argv=(char**)(sp+1); char**envp=argv+argc+1;
  char**ep=envp; while(*ep)ep++; u64*auxv=(u64*)(ep+1);
  for(char**e=envp;*e;e++){ if(spre(*e,"SSTRAP_DEBUG=1"))G_debug=1; }
  int reexec = argc>=3 && seq(argv[1],"--sstrap-exec");
  if(argc<2){ wr("usage: sstrap PROGRAM [ARGS...]\n"); S1(NR_exit_group,2); }

  init_slow();
  /* prefix: SSTRAP_ROOT or detect via readlink of an fd for "/" (supervisor-translated open) */
  int have=0;
  for(char**e=envp;*e;e++) if(spre(*e,"SSTRAP_ROOT=")){ scpy(G_prefix,*e+12); have=1; }
  if(!have){ static char root[2]="/"; i64 fd=SL(NR_openat,AT_FDCWD,(i64)root,040000|02000000,0,0);
    if(fd>=0){ char pp[64]="/proc/self/fd/"; char d[8]; int i=7; d[i]=0; u64 u=fd; do{d[--i]='0'+u%10;u/=10;}while(u); scpy(pp+14,d+i);
      i64 n=S4(NR_readlinkat,AT_FDCWD,pp,G_prefix,sizeof G_prefix-1); if(n>0)G_prefix[n]=0; S1(NR_close,fd); } }
  { struct kstat st; if(S4(NR_newfstatat,AT_FDCWD,"/proc/self",&st,0)==0){G_appuid=st.uid;G_appgid=st.gid;} }
  G_plen=slen(G_prefix); if(G_plen==1&&G_prefix[0]=='/'){G_plen=0;G_prefix[0]=0;}
  while(G_plen>1&&G_prefix[G_plen-1]=='/')G_prefix[--G_plen]=0;

  if(G_debug&&!reexec){ wr("[sstrap] prefix='");wr(G_prefix);wr("' slow=");wrn(G_slow!=0);wr(" appuid=");wrn(G_appuid);wr("\n"); }
  /* own virtual path for re-exec. /proc/self/exe is NOT usable inside Sandshell (it can name the
     supervisor's exec launcher), so: SSTRAP_SELF, else argv[0] made absolute (re-exec passes it on). */
  { int got=0; for(char**e=envp;*e;e++) if(spre(*e,"SSTRAP_SELF=")){ scpy(G_self,*e+12); got=1; }
    if(!got){ const char*a0=argv[0]; char tmp[4096];
      if(a0[0]!='/'){ int slash=0; for(const char*c=a0;*c;c++)if(*c=='/')slash=1;
        if(!slash) a0=find_prog(a0,envp,tmp);
        if(a0[0]!='/'){ char cwd[512]; i64 n=emu_getcwd(cwd,sizeof cwd);
          if(n>0&&slen(cwd)+slen(a0)+2<sizeof G_self){ scpy(G_self,cwd); u64 l=slen(G_self); G_self[l]='/'; scpy(G_self+l+1,a0[0]=='.'&&a0[1]=='/'?a0+2:a0); got=1; } } }
      if(!got) scpy(G_self,a0); }
    if(G_debug){ char x[512]; i64 n=S4(NR_readlinkat,AT_FDCWD,"/proc/self/exe",x,sizeof x-1); if(n<0)n=0; x[n]=0;
      wr("[sstrap] self='");wr(G_self);wr("' /proc/self/exe='");wr(x);wr("'\n"); } }

  const char*path; char**pargv; char pbuf[4096];
  if(reexec){ path=argv[2]; pargv=argv+3; }
  else { path=find_prog(argv[1],envp,pbuf); pargv=argv+1; }
  i64 pargc=0; while(pargv[pargc])pargc++;

  /* shebang (up to 4 levels) */
  static char *sbv[1024]; static char sbl[4][256];
  for(int lvl=0;lvl<4;lvl++){
    i64 fd=x_openat(AT_FDCWD,path,02000000,0); if(fd<0)die("open program",fd);
    char h[256]; i64 n=S4(NR_pread64,fd,h,255,0); S1(NR_close,fd); if(n<2)die("short file",n); h[n]=0;
    if(!(h[0]=='#'&&h[1]=='!'))break;
    char*s=h+2; while(*s==' '||*s=='\t')s++; char*interp=s; while(*s&&*s!=' '&&*s!='\t'&&*s!='\n')s++;
    char*arg=0; if(*s==' '||*s=='\t'){ *s++=0; while(*s==' '||*s=='\t')s++; if(*s&&*s!='\n'){arg=s; while(*s&&*s!='\n')s++; while(s>arg&&(s[-1]==' '||s[-1]=='\t'))s--; } }
    *s=0;
    char*L=sbl[lvl]; scpy(L,interp); char*A=0; if(arg){ A=L+slen(L)+1; scpy(A,arg); }
    if(pargc+3>1023)die("too many args",0);
    int k=0; sbv[k++]=L; if(A)sbv[k++]=A; sbv[k++]=(char*)path; for(i64 i=1;i<pargc;i++)sbv[k++]=pargv[i]; sbv[k]=0;
    /* copy back since sbv is reused per level */
    static char*sbv2[1024]; for(int i=0;i<=k;i++)sbv2[i]=sbv[i];
    pargv=sbv2; pargc=k; path=L;
  }

  /* exe name shown via /proc/self/exe */
  { scpy(G_exe,path); if(path[0]!='/'){ char cwd[512]; i64 n=emu_getcwd(cwd,sizeof cwd);
      if(n>0&&slen(cwd)+slen(path)+2<sizeof G_exe){ scpy(G_exe,cwd); u64 l=slen(G_exe); G_exe[l]='/'; scpy(G_exe+l+1,path[0]=='.'&&path[1]=='/'?path+2:path);} } }

  struct loaded P,I; load_elf(path,&P,1);
  u64 entry=P.entry, ibase=0;
  if(P.interp[0]){ load_elf(P.interp,&I,0); entry=I.entry; ibase=I.base; }

  install_handler();
  if(!reexec) install_filter();   /* re-exec: filter already inherited */

  /* new stack: argc, argv, envp, auxv (strings stay in the old stack) */
  u64 ssz=8ul<<20; i64 m=S6(NR_mmap,0,ssz,3,0x22|0x20000/*GROWSDOWN off*/&~0x20000,-1,0); if(m<0)die("stack",m);
  u64 naux=0; while(auxv[naux*2]!=AT_NULL)naux++;
  i64 envc=ep-envp;
  u64 words=1+pargc+1+envc+1+(naux+1)*2;
  u64*nsp=(u64*)(((u64)m+ssz-256-words*8)&~15ul);
  u64*w=nsp; *w++=pargc; for(i64 i=0;i<pargc;i++)*w++=(u64)pargv[i]; *w++=0;
  for(i64 i=0;i<envc;i++)*w++=(u64)envp[i]; *w++=0;
  for(u64 i=0;i<naux;i++){ u64 t=auxv[i*2],v=auxv[i*2+1];
    switch(t){ case AT_PHDR:v=P.phdr;break; case AT_PHNUM:v=P.phnum;break; case AT_PHENT:v=sizeof(Phdr);break;
      case AT_ENTRY:v=P.entry;break; case AT_BASE:v=ibase;break; case AT_EXECFN:v=(u64)G_exe;break; }
    *w++=t; *w++=v; }
  *w++=AT_NULL; *w++=0;
  jump_to((u64)nsp,entry);
}
__asm__(".text\n.global _start\n_start:\n mov x0, sp\n and sp, x0, #~15\n bl cmain\n");

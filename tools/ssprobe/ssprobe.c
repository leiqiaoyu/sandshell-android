/* ssprobe — Sandshell syscall conformance probe (static, no libc, aarch64).
 * Each test runs in a forked child so a SIGSYS kill is reported, not fatal.
 * Programs like Go / Rust-static / Bun bypass LD_PRELOAD shims, so this binary
 * sees exactly what the supervisor + Android seccomp give them.
 * Build: gcc -O1 -static -nostdlib -fno-stack-protector -ffreestanding -o ssprobe ssprobe.c
 * Output: one line per test:  RESULT  name  detail
 *   OK      = behaved like Linux
 *   ERR     = returned an error a normal Linux would not (detail = errno name)
 *   KILL    = child killed by a signal (detail = signal number; 31 = SIGSYS)
 *   LEAK    = worked but leaked/behaved wrong (detail says what)
 */
typedef unsigned long u64; typedef long i64;
#define AT_FDCWD (-100)
static i64 sc6(i64 n,i64 a,i64 b,i64 c,i64 d,i64 e,i64 f){
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

static u64 slen(const char*s){u64 n=0;while(s[n])n++;return n;}
static void out(const char*s){S3(64,1,s,slen(s));}
static void outn(i64 v){char b[24];int i=23;b[i]=0;int neg=v<0;u64 u=neg?-v:v;do{b[--i]='0'+u%10;u/=10;}while(u);if(neg)b[--i]='-';out(b+i);}
static int seq(const char*a,const char*b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
static int spre(const char*a,const char*p){while(*p){if(*a!=*p)return 0;a++;p++;}return 1;}
static void scpy(char*d,const char*s){while((*d++=*s++));}
static void scat(char*d,const char*s){while(*d)d++;scpy(d,s);}
static void mset(void*p,int c,u64 n){unsigned char*q=p;while(n--)*q++=c;}
void *memset(void*p,int c,u64 n){mset(p,c,n);return p;}
void *memcpy(void*d,const void*s,u64 n){unsigned char*a=d;const unsigned char*b=s;while(n--)*a++=*b++;return d;}

static const char*en(i64 e){switch(e){case 1:return"EPERM";case 2:return"ENOENT";case 9:return"EBADF";case 13:return"EACCES";case 14:return"EFAULT";
 case 17:return"EEXIST";case 18:return"EXDEV";case 20:return"ENOTDIR";case 21:return"EISDIR";case 22:return"EINVAL";case 30:return"EROFS";case 38:return"ENOSYS";
 case 40:return"ELOOP";case 95:return"EOPNOTSUPP";case 61:return"ENODATA";case 111:return"ECONNREFUSED";case 98:return"EADDRINUSE";default:return 0;}}

/* result reporting from the child: write to pipe fd 3-ish via global */
static int RP; static char RB[512];
static void rep(const char*res,const char*detail){scpy(RB,res);scat(RB,"\t");scat(RB,detail?detail:"");S3(64,RP,RB,slen(RB));}
static void repe(i64 r){ /* r<0 -> ERR errno */ char d[64];const char*n=en(-r);if(n){scpy(d,n);}else{d[0]='e';d[1]=0;char b[24];int i=23;b[i]=0;u64 u=-r;do{b[--i]='0'+u%10;u/=10;}while(u);scat(d,b+i);} rep("ERR",d);}
#define CHK(expr) do{ i64 _r=(expr); if(_r<0){repe(_r);return;} }while(0)

static char TD[256]; /* test dir */
static void P(char*b,const char*name){scpy(b,TD);scat(b,"/");scat(b,name);}

/* ---------- tests ---------- */
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0100
#define O_TRUNC 01000
#define O_DIRECTORY 040000
#define O_NOFOLLOW 0100000
#define O_CLOEXEC 02000000
#define O_PATH 010000000
#define O_TMPFILE (020000000|O_DIRECTORY)

static void t_open_root(void){i64 fd=S4(56,AT_FDCWD,"/root",O_RDONLY|O_DIRECTORY,0);CHK(fd);rep("OK",0);}
static void t_open_etc(void){i64 fd=S4(56,AT_FDCWD,"/etc/os-release",O_RDONLY,0);CHK(fd);char b[64];i64 n=S3(63,fd,b,63);CHK(n);rep(n>0?"OK":"LEAK",n>0?0:"empty read");}
static void t_open_path(void){i64 fd=S4(56,AT_FDCWD,"/root",O_PATH|O_DIRECTORY,0);CHK(fd);char st[128];CHK(S4(79,fd,"",st,0x1000/*AT_EMPTY_PATH*/));rep("OK",0);}
static void t_open_path_nofollow_link(void){char l[300];P(l,"pl");S3(36,"target-x",AT_FDCWD,l);i64 fd=S4(56,AT_FDCWD,l,O_PATH|O_NOFOLLOW,0);CHK(fd);rep("OK",0);}
static void t_cloexec(void){i64 fd=S4(56,AT_FDCWD,"/etc/os-release",O_RDONLY|O_CLOEXEC,0);CHK(fd);i64 f=S3(25,fd,1/*F_GETFD*/,0);CHK(f);rep((f&1)?"OK":"LEAK",(f&1)?0:"O_CLOEXEC lost");}
static void t_cloexec_translated(void){i64 fd=S4(56,AT_FDCWD,"/root",O_RDONLY|O_DIRECTORY|O_CLOEXEC,0);CHK(fd);i64 f=S3(25,fd,1,0);CHK(f);rep((f&1)?"OK":"LEAK",(f&1)?0:"O_CLOEXEC lost");}
static void t_tmpfile(void){i64 fd=S4(56,AT_FDCWD,TD,O_TMPFILE|O_RDWR,0600);CHK(fd);rep("OK",0);}
static void t_creat_write(void){char p[300];P(p,"f1");i64 fd=S4(56,AT_FDCWD,p,O_CREAT|O_WRONLY|O_TRUNC,0644);CHK(fd);CHK(S3(64,fd,"hello\n",6));rep("OK",0);}
static void t_fstatat(void){char st[128];CHK(S4(79,AT_FDCWD,"/root",st,0));rep("OK",0);}
static void t_statx(void){char st[256];CHK(S5(291,AT_FDCWD,"/root",0,0x7ff,st));rep("OK",0);}
static void t_statx_empty(void){i64 fd=S4(56,AT_FDCWD,"/etc/os-release",0,0);CHK(fd);char st[256];CHK(S5(291,fd,"",0x1000,0x7ff,st));rep("OK",0);}
static void t_faccessat(void){CHK(S3(48,AT_FDCWD,"/root",0));rep("OK",0);}
static void t_faccessat2(void){CHK(S4(439,AT_FDCWD,"/root",0,0));rep("OK",0);}
static void t_readlinkat(void){char l[300],b[300];P(l,"rl");S3(36,"/etc/os-release",AT_FDCWD,l);i64 n=S4(78,AT_FDCWD,l,b,299);CHK(n);b[n]=0;rep(seq(b,"/etc/os-release")?"OK":"LEAK",b);}
static void t_symlink_abs(void){char l[300];P(l,"sa");i64 r=S3(36,"/root/zz",AT_FDCWD,l);CHK(r);char st[128];rep("OK",0);(void)st;}
static void t_getdents(void){i64 fd=S4(56,AT_FDCWD,"/",O_RDONLY|O_DIRECTORY,0);CHK(fd);char b[4096];i64 n=S3(61,fd,b,4096);CHK(n);rep(n>0?"OK":"LEAK","");}
static void t_mkdir_unlink(void){char p[300];P(p,"d1");CHK(S3(34,AT_FDCWD,p,0755));CHK(S3(35,AT_FDCWD,p,0x200));rep("OK",0);}
static void t_renameat(void){char a[300],b[300];P(a,"ra");P(b,"rb");i64 fd=S4(56,AT_FDCWD,a,O_CREAT|O_WRONLY,0644);CHK(fd);CHK(S4(38,AT_FDCWD,a,AT_FDCWD,b));rep("OK",0);}
static void t_renameat2(void){char a[300],b[300];P(a,"r2a");P(b,"r2b");i64 fd=S4(56,AT_FDCWD,a,O_CREAT|O_WRONLY,0644);CHK(fd);CHK(S5(276,AT_FDCWD,a,AT_FDCWD,b,1/*NOREPLACE*/));rep("OK",0);}
static void t_linkat(void){char a[300],b[300];P(a,"la");P(b,"lb");i64 fd=S4(56,AT_FDCWD,a,O_CREAT|O_WRONLY,0644);CHK(fd);CHK(S5(37,AT_FDCWD,a,AT_FDCWD,b,0));rep("OK",0);}
static void t_fchmodat(void){char a[300];P(a,"f1");CHK(S3(53,AT_FDCWD,a,0600));rep("OK",0);}
static void t_fchmodat2(void){char a[300];P(a,"f1");CHK(S4(452,AT_FDCWD,a,0644,0));rep("OK",0);}
static void t_fchownat(void){char a[300];P(a,"f1");CHK(S5(54,AT_FDCWD,a,0,0,0));rep("OK",0);}
static void t_utimensat(void){char a[300];P(a,"f1");CHK(S4(88,AT_FDCWD,a,0,0));rep("OK",0);}
static void t_truncate(void){char a[300];P(a,"f1");CHK(S2(45,a,2));rep("OK",0);}
static void t_mknod_fifo(void){char a[300];P(a,"fifo");CHK(S4(33,AT_FDCWD,a,0010644,0));rep("OK",0);}
static void t_statfs(void){char b[256];CHK(S2(43,"/root",b));rep("OK",0);}
static void t_xattr(void){char a[300],b[64];P(a,"f1");i64 r=S4(8,a,"user.x",b,64);if(r==-61||r==-95){rep("OK",0);return;}CHK(r);rep("OK",0);}
static void t_inotify(void){i64 fd=S1(26,0);CHK(fd);CHK(S3(27,fd,"/root",0x100));rep("OK",0);}
static void t_chdir_getcwd(void){CHK(S1(49,"/root"));char b[512];i64 n=S2(17,b,512);CHK(n);rep(seq(b,"/root")?"OK":"LEAK",b);}
static void t_fchdir_getcwd(void){i64 fd=S4(56,AT_FDCWD,"/tmp",O_RDONLY|O_DIRECTORY,0);CHK(fd);CHK(S1(50,fd));char b[512];CHK(S2(17,b,512));rep(seq(b,"/tmp")?"OK":"LEAK",b);}
static void t_proc_cwd(void){CHK(S1(49,"/root"));char b[512];i64 n=S4(78,AT_FDCWD,"/proc/self/cwd",b,511);CHK(n);b[n]=0;rep(seq(b,"/root")?"OK":"LEAK",b);}
static void t_proc_fd(void){i64 fd=S4(56,AT_FDCWD,"/etc/os-release",0,0);CHK(fd);char p[64]="/proc/self/fd/";char d[4];d[0]='0'+fd/10;d[1]='0'+fd%10;d[2]=0;if(fd<10){d[0]='0'+fd;d[1]=0;}scat(p,d);char b[512];i64 n=S4(78,AT_FDCWD,p,b,511);CHK(n);b[n]=0;rep(seq(b,"/etc/os-release")||spre(b,"/usr/lib/os-release")?"OK":"LEAK",b);}
static void t_openat2(void){u64 how[3]={O_RDONLY,0,0};i64 fd=S4(437,AT_FDCWD,"/etc/os-release",how,24);CHK(fd);rep("OK",0);}
static void t_execve(void){i64 pid=S5(220,17,0,0,0,0);if(pid==0){char*av[]={"/bin/true",0};char*ev[]={0};S3(221,"/bin/true",av,ev);S1(93,127);}CHK(pid);int st=0;S4(260,pid,&st,0,0);if((st&0x7f)==0&&((st>>8)&0xff)==0)rep("OK",0);else{char d[32]="status ";char b[16];int i=15;b[i]=0;u64 u=st;do{b[--i]='0'+u%10;u/=10;}while(u);scat(d,b+i);rep("ERR",d);}}
static void t_execveat(void){i64 pid=S5(220,17,0,0,0,0);if(pid==0){char*av[]={"/bin/true",0};char*ev[]={0};S5(281,AT_FDCWD,"/bin/true",av,ev,0);S1(93,127);}CHK(pid);int st=0;S4(260,pid,&st,0,0);rep(((st&0xffff)==0)?"OK":"ERR",((st&0xffff)==0)?0:"exit!=0");}
static void t_unix_bind(void){i64 s=S3(198,1,1,0);CHK(s);char sa[110];mset(sa,0,110);sa[0]=1;char p[300];P(p,"sock");scpy(sa+2,p);CHK(S3(200,s,sa,2+slen(p)+1));CHK(S2(201,s,4));i64 c=S3(198,1,1,0);CHK(c);CHK(S3(203,c,sa,2+slen(p)+1));rep("OK",0);}
static void t_unix_bind_root(void){i64 s=S3(198,1,1,0);CHK(s);char sa[110];mset(sa,0,110);sa[0]=1;scpy(sa+2,"/root/.ssprobe.sock");S3(35,AT_FDCWD,"/root/.ssprobe.sock",0);CHK(S3(200,s,sa,2+20));S3(35,AT_FDCWD,"/root/.ssprobe.sock",0);rep("OK",0);}
static void t_accept(void){i64 s=S3(198,2,1,0);CHK(s);unsigned char sa[16];mset(sa,0,16);sa[0]=2;sa[4]=127;sa[7]=1;CHK(S3(200,s,sa,16));CHK(S2(201,s,1));u64 len=16;CHK(S3(204,s,sa,&len));CHK(S4(25,s,4/*F_SETFL*/,04000,0));i64 r=S3(202,s,0,0);if(r==-11){rep("OK",0);return;}CHK(r);rep("OK",0);}
static void t_accept4(void){i64 s=S3(198,2,1|04000,0);CHK(s);unsigned char sa[16];mset(sa,0,16);sa[0]=2;sa[4]=127;sa[7]=1;CHK(S3(200,s,sa,16));CHK(S2(201,s,1));i64 r=S4(242,s,0,0,0);if(r==-11){rep("OK",0);return;}CHK(r);rep("OK",0);}
static void t_clone3(void){u64 a[11];mset(a,0,sizeof a);a[4]=17;i64 r=S2(435,a,88);if(r==0)S1(93,0);if(r==-38){rep("OK","ENOSYS (libc falls back to clone)");return;}CHK(r);int st;S4(260,r,&st,0,0);rep("OK",0);}
static void t_pidfd(void){i64 r=S2(434,S0(172),0);CHK(r);rep("OK",0);}
static void t_epoll_pwait2(void){i64 ep=S1(20,0);CHK(ep);u64 ts[2]={0,0};char ev[16];i64 r=S6(441,ep,ev,1,ts,0,8);CHK(r);rep("OK",0);}
static void t_close_range(void){CHK(S3(436,100,200,0));rep("OK",0);}
static void t_getrandom(void){char b[16];CHK(S3(278,b,16,0));rep("OK",0);}
static void t_prlimit(void){u64 l[2];CHK(S4(261,0,7,0,l));rep("OK",0);}
static void t_memfd(void){CHK(S2(279,"x",1));rep("OK",0);}
static void t_membarrier(void){CHK(S3(283,0,0,0));rep("OK",0);}
static void t_rseq(void){static char rs[32] __attribute__((aligned(32)));i64 r=S4(293,rs,32,0,0x53053053);if(r==-38||r==-1||r==-22||r==-16){rep("OK","refused (libc copes)");return;}CHK(r);rep("OK",0);}
static void t_robust(void){static u64 h[3];CHK(S2(99,h,24));rep("OK",0);}
static void t_copy_file_range(void){char a[300],b[300];P(a,"f1");P(b,"cfr");i64 i=S4(56,AT_FDCWD,a,0,0);CHK(i);i64 o=S4(56,AT_FDCWD,b,O_CREAT|O_WRONLY,0644);CHK(o);CHK(S6(285,i,0,o,0,4,0));rep("OK",0);}
static void t_io_uring(void){char p[120];mset(p,0,120);i64 r=S2(425,4,p);if(r==-38||r==-1){rep("OK","refused (expected on Android)");return;}CHK(r);rep("OK",0);}
static void t_futex_waitv(void){i64 r=S5(449,0,0,0,0,0);if(r==-22){rep("OK",0);return;}CHK(r);rep("OK",0);}
static void t_landlock(void){i64 r=S3(444,0,0,1);if(r>=0||r==-95||r==-38){rep("OK",0);return;}CHK(r);rep("OK",0);}
static void t_ptrace_traceme(void){i64 pid=S5(220,17,0,0,0,0);if(pid==0){i64 r=S4(117,0,0,0,0);S1(93,r==0?0:1);}CHK(pid);int st=0;S4(260,pid,&st,0,0);rep((st&0xffff)==0?"OK":"ERR",(st&0xffff)==0?0:"PTRACE_TRACEME failed");}
static void t_setuid0(void){i64 r=S1(146,0);if(r==0){rep("OK",0);return;}CHK(r);}
static void t_getuid(void){i64 u=S0(174);char d[32]="uid=";char b[16];int i=15;b[i]=0;u64 x=u;do{b[--i]='0'+x%10;x/=10;}while(x);scat(d,b+i);rep(u==0?"OK":"LEAK",d);}
static void t_sched_aff(void){char m[128];CHK(S3(123,0,128,m));rep("OK",0);}
static void t_mount(void){i64 r=S5(40,"none","/tmp","tmpfs",0,0);if(r==-1||r==-13){rep("OK","EPERM (expected, no root)");return;}CHK(r);rep("OK",0);}
static void t_sysinfo(void){char b[128];CHK(S1(179,b));rep("OK",0);}
static void t_uname(void){char b[390];CHK(S1(160,b));rep("OK",0);}
static void t_shm(void){i64 r=S3(194,0,4096,01000|0600);CHK(r);rep("OK",0);}
static void t_sendmmsg(void){i64 s=S3(198,2,2,0);CHK(s);i64 r=S4(269,s,0,0,0);if(r==0||r==-9||r==-14||r==-89){rep("OK",0);return;}CHK(r);rep("OK",0);}

struct T{const char*n;void(*f)(void);};
static struct T TS[]={
 {"openat /root",t_open_root},{"openat+read /etc/os-release",t_open_etc},{"openat O_PATH",t_open_path},{"openat O_PATH|O_NOFOLLOW symlink",t_open_path_nofollow_link},
 {"O_CLOEXEC kept (plain file)",t_cloexec},{"O_CLOEXEC kept (translated dir)",t_cloexec_translated},{"O_TMPFILE",t_tmpfile},{"create+write",t_creat_write},
 {"newfstatat /root",t_fstatat},{"statx /root",t_statx},{"statx AT_EMPTY_PATH",t_statx_empty},{"faccessat",t_faccessat},{"faccessat2",t_faccessat2},
 {"symlink abs + readlinkat",t_readlinkat},{"symlinkat abs /root target",t_symlink_abs},{"getdents64 /",t_getdents},{"mkdirat+unlinkat",t_mkdir_unlink},
 {"renameat",t_renameat},{"renameat2 NOREPLACE",t_renameat2},{"linkat (hard link)",t_linkat},{"fchmodat",t_fchmodat},{"fchmodat2",t_fchmodat2},
 {"fchownat 0:0",t_fchownat},{"utimensat",t_utimensat},{"truncate",t_truncate},{"mknodat FIFO",t_mknod_fifo},{"statfs /root",t_statfs},{"getxattr",t_xattr},
 {"inotify on /root",t_inotify},{"chdir /root + getcwd",t_chdir_getcwd},{"fchdir /tmp + getcwd",t_fchdir_getcwd},{"readlink /proc/self/cwd",t_proc_cwd},
 {"readlink /proc/self/fd/N",t_proc_fd},{"openat2",t_openat2},{"execve /bin/true",t_execve},{"execveat /bin/true",t_execveat},
 {"AF_UNIX bind/connect in test dir",t_unix_bind},{"AF_UNIX bind /root path",t_unix_bind_root},{"accept (202)",t_accept},{"accept4",t_accept4},
 {"clone3",t_clone3},{"pidfd_open",t_pidfd},{"epoll_pwait2",t_epoll_pwait2},{"close_range",t_close_range},{"getrandom",t_getrandom},{"prlimit64",t_prlimit},
 {"memfd_create",t_memfd},{"membarrier",t_membarrier},{"rseq",t_rseq},{"set_robust_list",t_robust},{"copy_file_range",t_copy_file_range},{"io_uring_setup",t_io_uring},
 {"futex_waitv",t_futex_waitv},{"landlock_create_ruleset",t_landlock},{"ptrace TRACEME",t_ptrace_traceme},{"setuid(0)",t_setuid0},{"getuid==0",t_getuid},
 {"sched_getaffinity",t_sched_aff},{"mount tmpfs",t_mount},{"sysinfo",t_sysinfo},{"uname",t_uname},{"shmget",t_shm},{"sendmmsg",t_sendmmsg},
 {0,0}};

static void run(struct T*t){
  int p[2];if(S2(59,p,0)<0){out("pipe fail\n");return;}
  i64 pid=S5(220,17,0,0,0,0);
  if(pid==0){S1(57,p[0]);RP=p[1];t->f();S1(93,0);}
  S1(57,p[1]);
  char b[512];i64 tot=0,n;while((n=S3(63,p[0],b+tot,511-tot))>0)tot+=n;b[tot<0?0:tot]=0;S1(57,p[0]);
  int st=0;S4(260,pid,&st,0,0);
  if(tot>0){ /* RESULT\tdetail */ char*d=b;while(*d&&*d!='\t')d++;if(*d)*d++=0;out(b);out("\t");out(t->n);if(*d){out("\t");out(d);}out("\n");}
  else if((st&0x7f)){out("KILL\t");out(t->n);out("\tsignal ");outn(st&0x7f);out((st&0x7f)==31?" (SIGSYS)":"");out("\n");}
  else {out("ERR\t");out(t->n);out("\tno result, exit ");outn((st>>8)&0xff);out("\n");}
}
static void rmrf_simple(void){const char*n[]={"f1","pl","rl","sa","ra","rb","r2a","r2b","la","lb","fifo","cfr","sock",0};char p[300];for(int i=0;n[i];i++){P(p,n[i]);S3(35,AT_FDCWD,p,0);}S3(35,AT_FDCWD,TD,0x200);}
void __attribute__((noreturn)) cmain(i64*sp){
  int argc=(int)sp[0];char**argv=(char**)(sp+1);
  scpy(TD,argc>1?argv[1]:"/tmp/ssprobe.d");
  rmrf_simple();S3(34,AT_FDCWD,TD,0755);
  out("# ssprobe 1 — test dir ");out(TD);out("\n");
  int ok=0,bad=0;(void)ok;(void)bad;
  for(struct T*t=TS;t->n;t++)run(t);
  rmrf_simple();S1(94,0);for(;;);
}
__asm__(".global _start\n_start:\n mov x0, sp\n and sp, x0, #~15\n bl cmain\n");

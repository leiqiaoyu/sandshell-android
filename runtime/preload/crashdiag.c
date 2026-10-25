// t13d diagnostic: report fatal signals of Debian processes on the PTY (Android keeps no record for glibc children).
#define _GNU_SOURCE
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>
#include <sys/syscall.h>

static const int FATAL[] = {SIGSEGV, SIGSYS, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP};
static int is_fatal(int s){ for (unsigned i=0;i<sizeof FATAL/sizeof *FATAL;i++) if (FATAL[i]==s) return 1; return 0; }
static char altstk[65536];
static volatile int in_handler;

static void out(int fd, const char *s){ size_t n=strlen(s); while(n){ ssize_t w=write(fd,s,n); if(w<=0)break; s+=w; n-=w; } }

static void both(int lf, const char *s){ out(2,s); if(lf>=0) out(lf,s); }

static void maps_for(int lf, unsigned long a, const char *tag){
    int fd=open("/proc/self/maps",O_RDONLY); if(fd<0) return;
    static char buf[1<<16]; static char line[512]; size_t ln=0; ssize_t r;
    while((r=read(fd,buf,sizeof buf))>0){
        for(ssize_t i=0;i<r;i++){
            if(buf[i]!='\n'){ if(ln<sizeof line-1) line[ln++]=buf[i]; continue; }
            line[ln]=0; ln=0;
            unsigned long lo=strtoul(line,NULL,16); char *d=strchr(line,'-'); unsigned long hi=d?strtoul(d+1,NULL,16):0;
            if(a>=lo && a<hi){ char m[700]; snprintf(m,sizeof m,"[t13diag] %s in: %s (+0x%lx)\r\n",tag,line,a-lo); both(lf,m); close(fd); return; }
        }
    }
    close(fd);
}

static void handler(int sig, siginfo_t *si, void *ucv){
    if(in_handler++) _exit(128+sig);
    ucontext_t *uc=ucv; unsigned long pc=uc->uc_mcontext.pc, lr=uc->uc_mcontext.regs[30], x8=uc->uc_mcontext.regs[8];
    char exe[256]={0}; ssize_t n=readlink("/proc/self/exe",exe,sizeof exe-1); if(n<0) strcpy(exe,"?");
    char cmd[256]={0}; int cf=open("/proc/self/cmdline",O_RDONLY); if(cf>=0){ ssize_t k=read(cf,cmd,sizeof cmd-1); close(cf); for(ssize_t i=0;i<k-1;i++) if(!cmd[i]) cmd[i]=' '; }
    int lf=-1; const char *sh=getenv("DEBIAN_SHARED_STORAGE");
    if(sh){ char p[512]; snprintf(p,sizeof p,"%s/sandshell-crash.log",sh); lf=open(p,O_WRONLY|O_CREAT|O_APPEND,0644); }
    char m[1024];
    snprintf(m,sizeof m,"\r\n[t13diag] FATAL pid=%d sig=%d(%s) code=%d addr=%p syscall=%d arch=0x%x\r\n[t13diag] pc=0x%lx lr=0x%lx x8=%lu exe=%s\r\n[t13diag] cmd=%s\r\n",
        getpid(),sig,strsignal(sig),si->si_code,si->si_addr,sig==SIGSYS?si->si_syscall:-1,sig==SIGSYS?si->si_arch:0,pc,lr,x8,exe,cmd);
    both(lf,m);
    maps_for(lf,pc,"pc"); maps_for(lf,lr,"lr");
    if(lf>=0) close(lf);
    sleep(2);
    signal(sig,SIG_DFL); raise(sig); _exit(128+sig);
}


#include <link.h>
#include <elf.h>
#include <sys/mman.h>
static int my_sigaction(int,const struct sigaction*,struct sigaction*);
static sighandler_t my_signal(int,sighandler_t);
static int patch_cb(struct dl_phdr_info *info, size_t sz, void *data){
    const char *nm=info->dlpi_name?info->dlpi_name:"";
    if(strstr(nm,"libc.so")||strstr(nm,"ld-linux")||strstr(nm,"libdebian-ld")||strstr(nm,"crashdiag")||strstr(nm,"linux-vdso")) return 0;
    ElfW(Addr) base=info->dlpi_addr; ElfW(Dyn) *dyn=NULL;
    for(int i=0;i<info->dlpi_phnum;i++) if(info->dlpi_phdr[i].p_type==PT_DYNAMIC) dyn=(ElfW(Dyn)*)(base+info->dlpi_phdr[i].p_vaddr);
    if(!dyn) return 0;
    ElfW(Sym)*sym=NULL; const char*str=NULL; ElfW(Rela)*jr=NULL,*ra=NULL; size_t jrs=0,ras=0;
    for(ElfW(Dyn)*d=dyn;d->d_tag!=DT_NULL;d++){
        switch(d->d_tag){ case DT_SYMTAB: sym=(void*)d->d_un.d_ptr; break; case DT_STRTAB: str=(void*)d->d_un.d_ptr; break;
        case DT_JMPREL: jr=(void*)d->d_un.d_ptr; break; case DT_PLTRELSZ: jrs=d->d_un.d_val; break;
        case DT_RELA: ra=(void*)d->d_un.d_ptr; break; case DT_RELASZ: ras=d->d_un.d_val; break; }
    }
    if(!sym||!str) return 0;
    ElfW(Rela)*tabs[2]={jr,ra}; size_t szs[2]={jrs,ras}; long pg=sysconf(_SC_PAGESIZE);
    for(int t=0;t<2;t++){ if(!tabs[t]) continue;
        for(size_t i=0;i<szs[t]/sizeof(ElfW(Rela));i++){
            ElfW(Rela)*r=&tabs[t][i]; unsigned ty=ELF64_R_TYPE(r->r_info), si=ELF64_R_SYM(r->r_info);
            if(!si || (ty!=R_AARCH64_JUMP_SLOT && ty!=R_AARCH64_GLOB_DAT)) continue;
            const char*s=str+sym[si].st_name; void*rep=NULL;
            if(!strcmp(s,"sigaction")||!strcmp(s,"__sigaction")) rep=(void*)my_sigaction; else if(!strcmp(s,"signal")||!strcmp(s,"bsd_signal")||!strcmp(s,"sysv_signal")) rep=(void*)my_signal;
            if(!rep) continue;
            void**slot=(void**)(base+r->r_offset); uintptr_t p=(uintptr_t)slot&~(uintptr_t)(pg-1);
            mprotect((void*)p,pg,PROT_READ|PROT_WRITE); *slot=rep;
        }
    }
    return 0;
}

typedef int (*sa_t)(int,const struct sigaction*,struct sigaction*);
static sa_t real_sa;
static struct sigaction saved[65];

static int my_sigaction(int sig,const struct sigaction *act,struct sigaction *old){
    if(!real_sa) real_sa=(sa_t)dlsym(RTLD_NEXT,"sigaction");
    if(sig>0 && sig<65 && is_fatal(sig) && !getenv("T13DIAG_OFF")){
        if(old) *old=saved[sig];
        if(act) saved[sig]=*act;
        return 0;              // keep our reporter installed; the program believes its handler is set
    }
    return real_sa(sig,act,old);
}
static sighandler_t my_signal(int sig, sighandler_t h){
    struct sigaction a,o; memset(&a,0,sizeof a); a.sa_handler=h; a.sa_flags=SA_RESTART; sigemptyset(&a.sa_mask);
    if(my_sigaction(sig,&a,&o)<0) return SIG_ERR; return o.sa_handler;
}

__attribute__((constructor)) static void init(void){
    if(getenv("T13DIAG_OFF")) return;
    if(!real_sa) real_sa=(sa_t)dlsym(RTLD_NEXT,"sigaction");
    stack_t st={.ss_sp=altstk,.ss_size=sizeof altstk,.ss_flags=0}; sigaltstack(&st,NULL);
    struct sigaction a; memset(&a,0,sizeof a); a.sa_sigaction=handler; a.sa_flags=SA_SIGINFO|SA_ONSTACK; sigemptyset(&a.sa_mask);
    for(unsigned i=0;i<sizeof FATAL/sizeof *FATAL;i++) real_sa(FATAL[i],&a,NULL);
    dl_iterate_phdr(patch_cb,NULL);
    if(getenv("T13DIAG_VERBOSE") && !getenv("T13DIAG_SEEN")){
        char exe[256]={0}; if(readlink("/proc/self/exe",exe,sizeof exe-1)<0) strcpy(exe,"?");
        char m[400]; snprintf(m,sizeof m,"[t13diag] reporter armed pid=%d exe=%s\r\n",getpid(),exe); out(2,m);
        setenv("T13DIAG_SEEN","1",1);
    }
}

// forwarders: this object is shipped as libdebian_exec.so (PTY LD_PRELOAD); real interposer = libss_exec_real.so beside it
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
static void *H;
static void *R(const char *n){
    if(!H){ Dl_info di; char p[4096];
        if(dladdr((void*)R,&di) && di.dli_fname){ strncpy(p,di.dli_fname,sizeof p-40); p[sizeof p-40]=0; char *s=strrchr(p,'/'); if(s) strcpy(s+1,"libss_exec_real.so"); else strcpy(p,"libss_exec_real.so"); H=dlopen(p,RTLD_NOW|RTLD_GLOBAL); }
    }
    void *f = H ? dlsym(H,n) : NULL;
    return f ? f : dlsym(RTLD_NEXT,n);
}

/* t13f: top-level Debian dirs the original interposer passes through to Android (ENOENT there).
   Translate them into the rootfs before handing off. */
static const char *XT[]={"/root","/home","/opt","/srv","/run","/media","/boot","/sbin",0};
static const char *xlate(const char *p, char *buf, size_t n){
    if(!p || p[0]!='/') return p;
    const char *rf=getenv("DEBIAN_ROOTFS"); if(!rf || !*rf) return p;
    for(int i=0;XT[i];i++){ size_t l=strlen(XT[i]);
        if(!strncmp(p,XT[i],l) && (p[l]==0 || p[l]=='/')){ if((size_t)snprintf(buf,n,"%s%s",rf,p)>=n) return p; return buf; } }
    return p;
}
#include <stdio.h>
#define XP(p) char _xb[4096]; p = xlate(p,_xb,sizeof _xb)

__attribute__((constructor(101))) static void fwd_init(void){ R("execve"); }
int execve(const char*p,char*const a[],char*const e[]){ XP(p); return ((int(*)(const char*,char*const*,char*const*))R("execve"))(p,a,e); }
int execveat(int d,const char*p,char*const a[],char*const e[],int f){ XP(p); return ((int(*)(int,const char*,char*const*,char*const*,int))R("execveat"))(d,p,a,e,f); }
int execv(const char*p,char*const a[]){ XP(p); return ((int(*)(const char*,char*const*))R("execv"))(p,a); }
int execvp(const char*p,char*const a[]){ XP(p); return ((int(*)(const char*,char*const*))R("execvp"))(p,a); }
int execvpe(const char*p,char*const a[],char*const e[]){ XP(p); return ((int(*)(const char*,char*const*,char*const*))R("execvpe"))(p,a,e); }
int execlp(const char*f,const char*arg,...){
    va_list ap; int n=1; va_start(ap,arg); while(va_arg(ap,char*)) n++; va_end(ap);
    char **v=alloca((n+1)*sizeof *v); v[0]=(char*)arg; va_start(ap,arg); for(int i=1;i<=n;i++) v[i]=va_arg(ap,char*); va_end(ap);
    return execvp(f,v);
}
int chdir(const char*p){ XP(p); return ((int(*)(const char*))R("chdir"))(p); }
int futimens(int fd,const struct timespec t[2]){ return ((int(*)(int,const struct timespec*))R("futimens"))(fd,t); }
int utimensat(int d,const char*p,const struct timespec t[2],int f){ XP(p); return ((int(*)(int,const char*,const struct timespec*,int))R("utimensat"))(d,p,t,f); }
/* t13e: Android's app seccomp filter traps setfsuid(152)/setfsgid(151) with SIGSYS. ncurses 6.5 (trixie libtinfo)
   calls them while opening terminfo, killing bash at startup. Emulate: no fs-id change, report the current id. */
#include <sys/types.h>
int setfsuid(uid_t u){ (void)u; return (int)getuid(); }
int setfsgid(gid_t g){ (void)g; return (int)getgid(); }
/* t13g: glibc 2.41 fchmodat(AT_SYMLINK_NOFOLLOW) tries fchmodat2 (452, ENOSYS here) and then chmods
   "/proc/self/fd/N" of an O_PATH fd. The supervisor resolves that path in its own process ("self" = the
   supervisor) -> EPERM; tar 1.35 then fails every directory mode. Do the check here and use plain fchmodat. */
#include <sys/syscall.h>
int fchmodat(int d,const char*p,mode_t m,int f){
    if(f & AT_SYMLINK_NOFOLLOW){
        struct stat st;
        if(fstatat(d,p,&st,AT_SYMLINK_NOFOLLOW)==0 && S_ISLNK(st.st_mode)){ errno=EOPNOTSUPP; return -1; }
        f &= ~AT_SYMLINK_NOFOLLOW;
    }
    if(f==0){ char _xb[4096]; p=xlate(p,_xb,sizeof _xb); return (int)syscall(SYS_fchmodat,d,p,m); }
    return ((int(*)(int,const char*,mode_t,int))dlsym(RTLD_NEXT,"fchmodat"))(d,p,m,f);
}
/* t13h: Android's app seccomp kills accept(202); glibc's accept() issues it directly. accept4(242) is allowed. */
#include <sys/socket.h>
int accept(int s, struct sockaddr *a, socklen_t *l){ return accept4(s,a,l,0); }

/* t13j: symlinks straight to the kernel (BPF ALLOWs 36); see symrel.h */
#include "symrel.h"
int symlinkat(const char*t,int d,const char*p){ return ss_symlinkat(t,d,p); }
int symlink(const char*t,const char*p){ return ss_symlinkat(t,AT_FDCWD,p); }
/* t13k: hard links straight to the kernel with physical paths; see symrel.h */
int linkat(int od,const char*op,int nd,const char*np,int f){ return ss_linkat(od,op,nd,np,f); }
int link(const char*op,const char*np){ return ss_linkat(AT_FDCWD,op,AT_FDCWD,np,0); }

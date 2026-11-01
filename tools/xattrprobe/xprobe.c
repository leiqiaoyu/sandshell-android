#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/xattr.h>
#include <sys/sysmacros.h>
static const char *D;
static char P[512];
static const char*pth(const char*n){snprintf(P,sizeof P,"%s/%s",D,n);return P;}
static void rep(const char*what,int r){ if(r<0) printf("ERR\t%s\t%s\n",what,strerror(errno)); else printf("OK\t%s\n",what); }
static double now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e9+t.tv_nsec;}
int main(int c,char**v){
  D=c>1?v[1]:"/root/.xprobe";
  if(c>2&&!strcmp(v[2],"check")){ char b[64]; ssize_t n=getxattr(pth("persist"),"user.sandshell.probe",b,sizeof b-1);
    if(n<0)printf("ERR\tpersist after reboot\t%s\n",strerror(errno)); else {b[n]=0;printf("OK\tpersist after reboot\t%s\n",b);} return 0; }
  mkdir(D,0755);
  struct statfs sf; if(statfs(D,&sf)==0) printf("INFO\tf_type=0x%lx (f2fs=0xf2f52010 ext4=0xef53)\n",(long)sf.f_type);
  int fd=open(pth("f"),O_CREAT|O_RDWR|O_TRUNC,0644); if(fd>=0){write(fd,"x",1);close(fd);}
  rep("setxattr user.* on file",setxattr(P,"user.sandshell.probe","v1",2,0));
  char b[256]; ssize_t n=getxattr(P,"user.sandshell.probe",b,sizeof b); printf("%s\tgetxattr file\t%s\n",n==2&&!memcmp(b,"v1",2)?"OK":"ERR",n<0?strerror(errno):"");
  n=listxattr(P,b,sizeof b); printf("INFO\tlistxattr len=%zd\n",n);
  { char big[3000]; memset(big,'a',sizeof big); rep("setxattr 3000-byte value",setxattr(P,"user.sandshell.big",big,sizeof big,0)); }
  rep("removexattr",removexattr(P,"user.sandshell.probe"));
  rep("re-set for chmod test",setxattr(P,"user.sandshell.probe","v2",2,0));
  chmod(P,0000); n=getxattr(P,"user.sandshell.probe",b,sizeof b); printf("%s\tgetxattr after chmod 000 (DAC applies)\t%s\n",n==2?"OK":"ERR",n<0?strerror(errno):"");
  rep("setxattr after chmod 000",setxattr(P,"user.sandshell.probe","v3",2,0));
  chmod(P,0644);
  { int fd2=open(P,O_RDONLY); rep("fsetxattr via O_RDONLY fd",fsetxattr(fd2,"user.sandshell.fd","1",1,0)); n=fgetxattr(fd2,"user.sandshell.fd",b,sizeof b); printf("%s\tfgetxattr\t%s\n",n==1?"OK":"ERR",n<0?strerror(errno):""); close(fd2);}
  mkdir(pth("d"),0755); rep("setxattr user.* on dir",setxattr(P,"user.sandshell.probe","d",1,0));
  unlink(pth("l")); symlink("f",P); rep("lsetxattr user.* on symlink (expect EPERM)",lsetxattr(P,"user.sandshell.probe","l",1,0));
  rep("setxattr trusted.* (expect EPERM)",setxattr(pth("f"),"trusted.sandshell","t",1,0));
  rep("setxattr security.* (expect fail)",setxattr(pth("f"),"security.sandshell","t",1,0));
  unlink(pth("fifo")); mkfifo(P,0644); rep("setxattr user.* on fifo (expect EPERM)",setxattr(P,"user.sandshell.probe","p",1,0));
  { char a[512]; snprintf(a,sizeof a,"%s/f",D); rename(a,pth("g")); } n=getxattr(pth("g"),"user.sandshell.probe",b,sizeof b); printf("%s\txattr survives rename\t%s\n",n>0?"OK":"ERR",n<0?strerror(errno):"");
  fd=open(pth("persist"),O_CREAT|O_WRONLY,0644); close(fd); { char t[64]; snprintf(t,sizeof t,"set-%ld",(long)time(0)); rep("persist marker set",setxattr(pth("persist"),"user.sandshell.probe",t,strlen(t),0)); }
  /* timing */
  struct stat st; const int N=200000; double t0,t1; char *f=strdup(pth("g")), *h=strdup(pth("persist"));
  t0=now(); for(int i=0;i<N;i++) stat(f,&st); t1=now(); printf("TIME\tstat\t%.0f ns\n",(t1-t0)/N);
  t0=now(); for(int i=0;i<N;i++) getxattr(f,"user.sandshell.probe",b,sizeof b); t1=now(); printf("TIME\tgetxattr hit\t%.0f ns\n",(t1-t0)/N);
  t0=now(); for(int i=0;i<N;i++) getxattr(f,"user.sandshell.none",b,sizeof b); t1=now(); printf("TIME\tgetxattr miss\t%.0f ns\n",(t1-t0)/N);
  fd=open(f,O_RDONLY);
  t0=now(); for(int i=0;i<N;i++) fstat(fd,&st); t1=now(); printf("TIME\tfstat\t%.0f ns\n",(t1-t0)/N);
  t0=now(); for(int i=0;i<N;i++) fgetxattr(fd,"user.sandshell.probe",b,sizeof b); t1=now(); printf("TIME\tfgetxattr hit\t%.0f ns\n",(t1-t0)/N);
  (void)h; printf("# after reboot run: xprobe %s check\n",D);
  return 0;
}

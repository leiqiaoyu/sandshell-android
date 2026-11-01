#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
static void*t(void*a){return a;}
int main(int c,char**v){pthread_t th;pthread_create(&th,0,t,0);pthread_join(th,0);
 char b[PATH_MAX];char e[PATH_MAX];ssize_t n=readlink("/proc/self/exe",e,sizeof e-1);e[n>0?n:0]=0;
 struct stat st; int sr=stat("/etc/os-release",&st);
 printf("hello static: threads ok, cwd=%s exe=%s stat=%d uid=%d\n",getcwd(b,sizeof b),e,sr,getuid());
 if(c>1){execl("/bin/echo","echo","exec from static ok",(char*)0);perror("exec");}
 return 0;}

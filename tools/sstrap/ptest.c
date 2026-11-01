#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
int main(){char b[4096];mkdir("/root/.sst",0755);
 printf("chdir /root/.sst: %d\n",chdir("/root/.sst"));
 printf("getcwd: %s\n",getcwd(b,sizeof b));
 ssize_t n=readlink("/proc/self/cwd",b,sizeof b-1);b[n>0?n:0]=0;printf("/proc/self/cwd: %s\n",b);
 int fd=open("f",O_CREAT|O_WRONLY,0644);write(fd,"hello",5);close(fd);
 printf("truncate /root/.sst/f: %d\n",truncate("/root/.sst/f",2));
 printf("mkfifo /root/p: %d\n",mkfifo("/root/.sst/p",0644));
 printf("symlink -> /root/l: %d\n",symlink("/root/.sst/f","/root/.sst/l"));
 int s=socket(AF_UNIX,SOCK_STREAM,0);struct sockaddr_un a={AF_UNIX};strcpy(a.sun_path,"/root/.sst/sock");
 printf("bind /root/sock: %d\n",bind(s,(void*)&a,sizeof a));
 printf("chdir /tmp: %d -> ",chdir("/tmp")); printf("%s\n",getcwd(b,sizeof b));
 return 0;}

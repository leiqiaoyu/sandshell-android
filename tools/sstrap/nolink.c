/* test helper: linkat -> EACCES like Android, then exec */
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <stddef.h>
int main(int c,char**v){struct sock_filter f[]={BPF_STMT(BPF_LD|BPF_W|BPF_ABS,offsetof(struct seccomp_data,nr)),
BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,37,0,1),BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ERRNO|13),BPF_STMT(BPF_RET|BPF_K,SECCOMP_RET_ALLOW)};
struct sock_fprog p={4,f};prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0);prctl(PR_SET_SECCOMP,2,&p);execv(v[1],v+1);return 1;}

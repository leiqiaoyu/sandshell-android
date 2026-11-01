#include <unistd.h>
#include <stdio.h>
#include <time.h>
int main(){char b[256];struct timespec a,c;clock_gettime(CLOCK_MONOTONIC,&a);for(int i=0;i<200000;i++)getcwd(b,256);clock_gettime(CLOCK_MONOTONIC,&c);printf("getcwd: %.0f ns/call\n",((c.tv_sec-a.tv_sec)*1e9+(c.tv_nsec-a.tv_nsec))/200000);return 0;}

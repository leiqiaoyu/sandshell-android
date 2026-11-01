/* ---------------- hard-link emulation (link2symlink) ----------------
 * Android's SELinux policy forbids link(2) for apps. Like proot's link2symlink, a hard-linked file
 * moves to a hidden store ".l2s.<name>.<ino>" in its directory and every name becomes a RELATIVE
 * symlink to it; the link count lives in xattr user.sandshell.l2s on the store. lstat of such a name
 * reports the store (same ino for all names, real nlink), readlink says EINVAL, unlink/rename keep
 * the count. Nothing changes on the hot path: only symlinks whose target is a store are touched.
 * Big buffers come from a private mmap (signal stacks can be small, e.g. Go's 32 KB). */
#define PB 4096
static char*l2s_mem(void){ i64 m=S6(NR_mmap,0,16*PB,3,0x22,-1,0); return m<0?0:(char*)m; }
static void l2s_free(char*m){ if(m)S2(NR_munmap,m,16*PB); }
static int l2s_name(const char*t,i64 n){ /* basename of link text starts with ".l2s." */
  i64 b=0; for(i64 i=0;i<n;i++) if(t[i]=='/') b=i+1;
  return n-b>5&&t[b]=='.'&&t[b+1]=='l'&&t[b+2]=='2'&&t[b+3]=='s'&&t[b+4]=='.';
}
static i64 fd_path(i64 fd,char*out){
  char pp[40]="/proc/self/fd/"; char d[12]; int i=11; d[i]=0; u64 u=fd; do{d[--i]='0'+u%10;u/=10;}while(u); scpy(pp+14,d+i);
  i64 n=S4(NR_readlinkat,AT_FDCWD,pp,out,PB-1); if(n<0)return n; out[n]=0; return n;
}
/* (virtual, dfd-relative) path -> physical realpath of its directory + last component. tmp: PB scratch */
static i64 l2s_split(i64 dfd,const char*path,char*dir,char*base,char*tmp){
  u64 n=slen(path); while(n>1&&path[n-1]=='/')n--; if(!n)return -ENOENT;
  i64 s=-1; for(u64 i=0;i<n;i++) if(path[i]=='/') s=i;
  u64 bl=n-(s+1); if(bl>=256)return -ENAMETOOLONG; memcpy(base,path+s+1,bl); base[bl]=0;
  if(s<0){ tmp[0]='.'; tmp[1]=0; } else if(s==0){ tmp[0]='/'; tmp[1]=0; } else { if(s>=PB)return -ENAMETOOLONG; memcpy(tmp,path,s); tmp[s]=0; }
  i64 fd=x_openat(dfd,tmp,040000|02000000,0); if(fd<0)return fd;
  i64 r=fd_path(fd,dir); S1(NR_close,fd); return r;
}
static void l2s_join(char*o,const char*a,const char*b){ u64 l=slen(a); memcpy(o,a,l); if(!(l==1&&a[0]=='/'))o[l++]='/'; scpy(o+l,b); }
/* normalize physical "<dir path>/<name>" (dir may hold ..) -> out. tmp: PB scratch */
static i64 l2s_norm(const char*phys,char*out,char*tmp){
  u64 n=slen(phys); if(n>=PB)return -ENAMETOOLONG; memcpy(tmp,phys,n+1);
  i64 s=-1; for(u64 i=0;i<n;i++) if(tmp[i]=='/') s=i; if(s<=0)return -EINVAL; tmp[s]=0;
  i64 fd=S4(NR_openat,AT_FDCWD,tmp,040000|02000000,0); if(fd<0)return fd;
  i64 r=fd_path(fd,out); S1(NR_close,fd); if(r<0)return r;
  if(r+1+slen(tmp+s+1)+1>PB)return -ENAMETOOLONG; out[r]='/'; scpy(out+r+1,tmp+s+1); return 0;
}
/* relative path from directory `from` to file `to` (both normalized physical) */
static void l2s_rel(const char*from,const char*to,char*out){
  u64 i=0,last=0; while(from[i]&&from[i]==to[i]){ if(from[i]=='/')last=i; i++; }
  if(from[i]==0&&to[i]=='/')last=i;
  u64 k=0; for(const char*f=from+last;*f;f++) if(*f=='/'&&f[1]){ out[k++]='.';out[k++]='.';out[k++]='/'; }
  const char*t=to+last; if(*t=='/')t++; scpy(out+k,t);
}
/* user.* xattrs need write permission even for the owner: read-only files (git objects are 0444)
   get u+w for the moment of the update */
static i64 l2s_xop(i64 nr,const char*p,const char*name,const char*v,i64 n){
  i64 r= nr==NR_removexattr? S2(nr,p,name) : S5(nr,p,name,v,n,0);
  if(r!=-EACCES&&r!=-EPERM)return r;
  struct kstat st; if(S4(NR_newfstatat,AT_FDCWD,p,&st,0)<0)return r;
  if(S3(NR_fchmodat,AT_FDCWD,p,(st.mode&07777)|0600)<0)return r;
  r= nr==NR_removexattr? S2(nr,p,name) : S5(nr,p,name,v,n,0);
  S3(NR_fchmodat,AT_FDCWD,p,st.mode&07777); return r;
}
static i64 l2s_cnt(const char*store){ char v[24]; i64 n=S4(NR_getxattr,store,L2S_X,v,sizeof v-1); if(n<=0)return 1;
  i64 x=0; for(i64 i=0;i<n;i++) if(v[i]>='0'&&v[i]<='9') x=x*10+(v[i]-'0'); return x; }
static i64 l2s_setcnt(const char*store,i64 x){ char v[24]; int i=23; v[i]=0; if(x<0)x=0; do{v[--i]='0'+x%10;x/=10;}while(x);
  return l2s_xop(NR_setxattr,store,L2S_X,v+i,23-i); }
/* names of a store: xattr user.sandshell.l2sn = NUL-separated physical paths (best effort) */
#define L2S_N "user.sandshell.l2sn"
static i64 l2s_names(const char*store,char*buf){ i64 n=S4(NR_getxattr,store,L2S_N,buf,PB-1); return n<0?0:n; }
static void l2s_setnames(const char*store,const char*buf,i64 n){
  if(n<=0) l2s_xop(NR_removexattr,store,L2S_N,0,0); else l2s_xop(NR_setxattr,store,L2S_N,buf,n); }
static void l2s_addname(const char*store,const char*p,char*buf){
  i64 n=l2s_names(store,buf); u64 l=slen(p); if(n+l+1>PB-1)return; memcpy(buf+n,p,l+1); l2s_setnames(store,buf,n+l+1); }
static i64 l2s_delname(char*buf,i64 n,const char*p){ /* remove p from list in place; returns new length */
  i64 i=0; while(i<n){ i64 l=slen(buf+i); if(seq(buf+i,p)){ memmove(buf+i,buf+i+l+1,n-(i+l+1)); return n-(l+1); } i+=l+1; } return n; }
/* is (dfd,path) an l2s name? -> store's physical path (PB). m: scratch of >= 4*PB. dir (PB) receives the link's dir */
static int l2s_store_of2(i64 dfd,const char*path,char*store,char*m,char*lp);
static int l2s_store_of(i64 dfd,const char*path,char*store,char*m){ return l2s_store_of2(dfd,path,store,m,0); }
static int l2s_store_of2(i64 dfd,const char*path,char*store,char*m,char*lp){
  struct kstat st; if(pathcall(NR_newfstatat,dfd,path,(i64)&st,AT_SYMLINK_NOFOLLOW,0,0,0)<0)return 0;
  if((st.mode&0170000)!=0120000)return 0;
  char*t=m,*dir=m+PB,*tmp=m+2*PB,*sp=m+3*PB; char base[256];
  i64 n=pathcall(NR_readlinkat,dfd,path,(i64)t,PB-1,0,0,0); if(n<=0||!l2s_name(t,n))return 0; t[n]=0;
  if(l2s_split(dfd,path,dir,base,tmp)<0)return 0;
  if(lp){ if(slen(dir)+slen(base)+2>PB)return 0; l2s_join(lp,dir,base); }
  if(t[0]=='/'){ scpy(sp,t); } else { if(slen(dir)+slen(t)+2>PB)return 0; l2s_join(sp,dir,t); }
  return l2s_norm(sp,store,tmp)==0;
}
static i64 emu_link(i64 odfd,const char*opath,i64 ndfd,const char*npath){
  char*m=l2s_mem(); if(!m)return -12;
  char *od=m,*nd=m+PB,*op=m+2*PB,*np=m+3*PB,*store=m+4*PB,*rel=m+5*PB,*tmp=m+6*PB,*t=m+7*PB,*sp=m+8*PB;
  char ob[256],nb[256]; i64 r; int made=0; struct kstat st,s2;
  if((r=l2s_split(odfd,opath,od,ob,tmp))<0)goto out;
  if((r=l2s_split(ndfd,npath,nd,nb,tmp))<0)goto out;
  r=-ENAMETOOLONG; if(slen(od)+slen(ob)+2>PB||slen(nd)+slen(nb)+2>PB)goto out;
  l2s_join(op,od,ob); l2s_join(np,nd,nb);
  if((r=S4(NR_newfstatat,AT_FDCWD,op,&st,AT_SYMLINK_NOFOLLOW))<0)goto out;
  r=-EPERM; if((st.mode&0170000)==0040000)goto out;
  r=-EEXIST; if(S4(NR_newfstatat,AT_FDCWD,np,&s2,AT_SYMLINK_NOFOLLOW)==0)goto out;
  if((st.mode&0170000)==0120000){
    i64 n=S4(NR_readlinkat,AT_FDCWD,op,t,PB-1); if(n<0){r=n;goto out;} t[n]=0;
    if(!l2s_name(t,n)){ r=S3(NR_symlinkat,t,AT_FDCWD,np); goto out; }   /* link to a plain symlink = copy of it */
    if(t[0]=='/')scpy(sp,t); else l2s_join(sp,od,t);
    if((r=l2s_norm(sp,store,tmp))<0)goto out;
  } else {
    char nm[300]=".l2s."; u64 l=5; u64 bl=slen(ob); if(bl>200)bl=200; memcpy(nm+l,ob,bl); l+=bl; nm[l++]='.';
    char hx[20]; int i=19; hx[i]=0; u64 u=st.ino; do{hx[--i]="0123456789abcdef"[u&15];u>>=4;}while(u); scpy(nm+l,hx+i);
    l2s_join(store,od,nm);
    if((r=S4(NR_renameat,AT_FDCWD,op,AT_FDCWD,store))<0)goto out;
    if((r=S3(NR_symlinkat,nm,AT_FDCWD,op))<0){ S4(NR_renameat,AT_FDCWD,store,AT_FDCWD,op); goto out; }
    if(l2s_setcnt(store,1)<0){ /* no user xattrs here: no safe emulation, keep the kernel's answer */
      S3(NR_unlinkat,AT_FDCWD,op,0); S4(NR_renameat,AT_FDCWD,store,AT_FDCWD,op); r=-EACCES; goto out; }
    made=1; l2s_setnames(store,0,0); l2s_addname(store,op,t);
  }
  l2s_rel(nd,store,rel);
  if((r=S3(NR_symlinkat,rel,AT_FDCWD,np))<0){
    if(made){ S3(NR_unlinkat,AT_FDCWD,op,0); S4(NR_renameat,AT_FDCWD,store,AT_FDCWD,op); }
    goto out; }
  l2s_setcnt(store,l2s_cnt(store)+1); l2s_addname(store,np,t); r=0;
  if(S4(NR_newfstatat,AT_FDCWD,store,&s2,0)==0&&!(s2.mode&01000)) S3(NR_fchmodat,AT_FDCWD,store,(s2.mode&07777)|01000);
out: l2s_free(m); return r;
}
/* lstat of an l2s name: report the stored file */
static void l2s_fixstat(i64 dfd,const char*path,struct kstat*st,i64 fl){
  char t[256]; i64 n=pathcall(NR_readlinkat,dfd,path,(i64)t,sizeof t,0,0,0);   /* cheap pre-check: most symlinks are not ours */
  if(n<=0||!l2s_name(t,n<(i64)sizeof t?n:(i64)sizeof t))return;
  char*m=l2s_mem(); if(!m)return; char*store=m+4*PB;
  if(l2s_store_of(dfd,path,store,m)){ struct kstat s2;
    if(S4(NR_newfstatat,AT_FDCWD,store,&s2,fl&~AT_SYMLINK_NOFOLLOW)==0){ s2.nlink=(u32)l2s_cnt(store); s2.mode&=~01000; fixids(&s2); *st=s2; } }
  l2s_free(m);
}
/* one name of a store went away (lp: its physical path). With one name left the store moves back
   there as a plain file: "link(tmp,final); unlink(tmp); rm -rf tmpdir" (git, dpkg, ...) stays safe. */
static void l2s_unlinked(const char*store,const char*lp){
  i64 c=l2s_cnt(store)-1; if(c<=0){ S3(NR_unlinkat,AT_FDCWD,store,0); return; }
  l2s_setcnt(store,c);
  char*m=l2s_mem(); if(!m)return; char*buf=m,*t=m+PB,*sp=m+2*PB,*nm=m+3*PB,*tmp=m+4*PB;
  i64 n=l2s_names(store,buf); if(lp) n=l2s_delname(buf,n,lp); l2s_setnames(store,buf,n);
  if(c==1&&n>0&&(i64)slen(buf)+1==n){ /* exactly one known name left: verify it still points here */
    i64 k=S4(NR_readlinkat,AT_FDCWD,buf,t,PB-1);
    if(k>0&&l2s_name(t,k)){ t[k]=0; i64 s=-1; for(i64 i=0;buf[i];i++) if(buf[i]=='/') s=i;
      if(t[0]=='/')scpy(sp,t); else { memcpy(sp,buf,s+1); scpy(sp+s+1,t); }
      if(l2s_norm(sp,nm,tmp)==0&&seq(nm,store)&&S4(NR_renameat,AT_FDCWD,store,AT_FDCWD,buf)==0){
        struct kstat st; l2s_xop(NR_removexattr,buf,L2S_X,0,0); l2s_xop(NR_removexattr,buf,L2S_N,0,0);
        if(S4(NR_newfstatat,AT_FDCWD,buf,&st,AT_SYMLINK_NOFOLLOW)==0) S3(NR_fchmodat,AT_FDCWD,buf,st.mode&06777); } } }
  l2s_free(m);
}
/* after renaming an l2s name to (dfd,path): repoint its relative target */
static void l2s_repoint(i64 dfd,const char*path,const char*store,char*m,const char*oldp){
  char*dir=m,*tmp=m+PB,*rel=m+2*PB,*np=m+3*PB,*cur=m+5*PB,*nb=m+6*PB; char base[256];
  if(l2s_split(dfd,path,dir,base,tmp)<0)return;
  l2s_rel(dir,store,rel); l2s_join(np,dir,base);
  { i64 n=l2s_names(store,nb); n=l2s_delname(nb,n,oldp); u64 l=slen(np); if(n+l+1<=PB-1){ memcpy(nb+n,np,l+1); n+=l+1; } l2s_setnames(store,nb,n); }
  i64 n=S4(NR_readlinkat,AT_FDCWD,np,cur,PB-1); if(n>=0){ cur[n]=0; if(seq(cur,rel))return; }
  u64 l=slen(np); memcpy(tmp,np,l); scpy(tmp+l,".l2s-tmp");
  if(S3(NR_symlinkat,rel,AT_FDCWD,tmp)==0 && S4(NR_renameat,AT_FDCWD,tmp,AT_FDCWD,np)!=0) S3(NR_unlinkat,AT_FDCWD,tmp,0);
}

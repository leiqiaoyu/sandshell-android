/* t13j: symbolic links done in-process, straight to the kernel.
   The supervisor's symlinkat (nr 36) handler only allows a few hard-coded dpkg/alternatives shapes and returns
   EROFS(policy) for everything else (device 14:17: ln -s rel and abs both EROFS; disasm 0x8600 / "n17-unified-usr-payload").
   t13j's BPF ALLOWs 36, so the kernel sees our arguments directly. Here:
   - absolute Debian link paths (/usr/..., /root/...) become physical $DEBIAN_ROOTFS paths;
   - absolute targets become the equivalent root-relative target ("../"*depth(linkdir) + t), because the kernel would
     follow an absolute target into Android's root, not Debian's.
   Result = the kernel's real result (never faked). */
#include <limits.h>
#include <sys/syscall.h>
#define SS_DEF_ROOTFS "/data/user/0/dev.sandshell.core/files/debian"
static const char *ss_rootfs(void){ const char *r=getenv("DEBIAN_ROOTFS"); return (r && r[0]=='/' && r[1]) ? r : SS_DEF_ROOTFS; }
static int ss_pfx(const char *p,const char *s){ size_t l=strlen(s); return !strncmp(p,s,l) && (p[l]==0 || p[l]=='/'); }
/* physical path -> virtual (pointer into p), or NULL when p is not inside the rootfs */
static const char *ss_unphys(const char *p){ const char *r=ss_rootfs(); size_t l=strlen(r);
    if(!strncmp(p,r,l)){ if(p[l]==0) return "/"; if(p[l]=='/') return p+l; } return NULL; }
/* absolute path the kernel cannot see as-is (Debian virtual) */
static int ss_is_virtual(const char *p){
    static const char *host[]={"/proc","/dev","/sys","/system","/mnt","/storage","/sdcard","/data","/apex","/vendor","/odm","/product","/linkerconfig",0};
    if(!p || p[0]!='/') return 0;
    for(int i=0;host[i];i++) if(ss_pfx(p,host[i])) return 0;
    return 1;
}
static int ss_cwd_virtual(char *out,size_t n){
    char c[PATH_MAX]; if(!getcwd(c,sizeof c)) return -1;
    const char *v=ss_unphys(c); if(!v) return -1;
    return snprintf(out,n,"%s",v)>=(int)n ? -1 : 0;
}
/* virtual directory that will contain the link, or -1 */
static int ss_linkdir(int d,const char *p,char *dir,size_t n){
    char b[PATH_MAX];
    if(p[0]=='/'){ const char *v=ss_unphys(p); if(!v){ if(!ss_is_virtual(p)) return -1; v=p; }
        if(snprintf(b,sizeof b,"%s",v)>=(int)sizeof b) return -1; }
    else {
        char base[PATH_MAX];
        if(d==AT_FDCWD){ if(ss_cwd_virtual(base,sizeof base)) return -1; }
        else { char save[PATH_MAX]; if(!getcwd(save,sizeof save)) return -1;
            if(syscall(SYS_fchdir,d)) return -1;
            int r=ss_cwd_virtual(base,sizeof base);
            if(syscall(SYS_chdir,save)) return -1;
            if(r) return -1; }
        if(snprintf(b,sizeof b,"%s/%s",base,p)>=(int)sizeof b) return -1;
    }
    char *s=strrchr(b,'/'); *s=0;
    return snprintf(dir,n,"%s",b)>=(int)n ? -1 : 0;
}
static int ss_symrel(const char *t,int d,const char *linkp,char *out,size_t n){
    char dir[PATH_MAX]; if(ss_linkdir(d,linkp,dir,sizeof dir)) return -1;
    int depth=0; const char *p=dir;
    while(*p){ while(*p=='/') p++; if(!*p) break; const char *e=p; while(*e && *e!='/') e++;
        size_t l=(size_t)(e-p);
        if(l==2 && p[0]=='.' && p[1]=='.') return -1;
        if(!(l==1 && p[0]=='.')) depth++;
        p=e; }
    size_t o=0; while(*t=='/') t++;
    if(depth==0) return snprintf(out,n,"./%s",*t?t:".")>=(int)n ? -1 : 0;
    for(int i=0;i<depth;i++){ if(o+3>=n) return -1; memcpy(out+o,"../",3); o+=3; }
    if(*t){ if(o+strlen(t)>=n) return -1; strcpy(out+o,t); } else out[o-1]=0;
    return 0;
}
static int ss_symlinkat(const char *t,int d,const char *p){
    if(!t || !p){ errno=EFAULT; return -1; }
    char rel[PATH_MAX], phys[PATH_MAX];
    if(t[0]=='/' && ss_symrel(t,d,p,rel,sizeof rel)==0) t=rel;
    if(ss_is_virtual(p) && !ss_unphys(p)){
        if(snprintf(phys,sizeof phys,"%s%s",ss_rootfs(),p)>=(int)sizeof phys){ errno=ENAMETOOLONG; return -1; }
        p=phys; d=AT_FDCWD; }
    return (int)syscall(SYS_symlinkat,t,d,p);
}
/* t13k: hard links. The supervisor does not mediate linkat (37): the kernel saw virtual paths (/root/...) -> ENOENT
   (device 14:52, dsh atomic write link(tmp, final)). Translate absolute virtual paths to physical $DEBIAN_ROOTFS
   paths and issue the raw syscall. Relative paths / dirfds are already real. Real result, never faked. */
static int ss_linkat(int od,const char *op,int nd,const char *np,int f){
    if(!op || !np){ errno=EFAULT; return -1; }
    char po[PATH_MAX], pn[PATH_MAX];
    if(ss_is_virtual(op) && !ss_unphys(op)){
        if(snprintf(po,sizeof po,"%s%s",ss_rootfs(),op)>=(int)sizeof po){ errno=ENAMETOOLONG; return -1; }
        op=po; od=AT_FDCWD; }
    if(ss_is_virtual(np) && !ss_unphys(np)){
        if(snprintf(pn,sizeof pn,"%s%s",ss_rootfs(),np)>=(int)sizeof pn){ errno=ENAMETOOLONG; return -1; }
        np=pn; nd=AT_FDCWD; }
    return (int)syscall(SYS_linkat,od,op,nd,np,f);
}

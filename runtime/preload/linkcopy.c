// libsandshell_linkcopy.so — 只给 dpkg 用的兜底（sw3/sw4）
// 1) 硬链接：supervisor 不中介 linkat，真机内核看到的是虚拟路径（/usr/...），返回 ENOENT；物理路径下 Android 可能再拒绝。
//    规则：真 link → 用 rootfs 物理路径再试真 link → 源是普通文件才复制成新文件（O_EXCL，保留权限与时间）。
// 2) sw4：supervisor 写策略只放行 /usr /etc /tmp /var/lib/dpkg 等，Debian 包里正当的 /var/lib/git 之类目录会 EROFS。
//    对 dpkg 的 open(写)/mkdir/rmdir/unlink/rename/chmod/chown/utimensat/symlink 等，若虚拟绝对路径得到 EROFS，
//    改用 rootfs 物理路径再试一次（落在同一个真实位置，只是绕开 supervisor 的路径策略；返回重试的真实结果）。
//    依据：libdebian_runtime.so 的路径分类函数（0xd234）对 "/data/user/0/" 前缀返回 0 = 不中介，直达内核。
// 硬链接复制兜底失败则恢复原 errno，绝不伪造成功。只在进程名为 dpkg 时生效，其它程序看到的仍是真实错误。
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <stdarg.h>
#include <sys/time.h>

#define DEF_ROOTFS "/data/user/0/dev.sandshell.core/files/debian"
static const char *rootfs(void) {
    const char *r = getenv("DEBIAN_ROOTFS");
    return (r && r[0] == '/' && r[1]) ? r : DEF_ROOTFS;
}
// 虚拟绝对路径 -> 物理路径；相对路径、已是物理路径、过长则返回 NULL
static const char *phys(const char *p, char *buf) {
    if (!p || p[0] != '/') return NULL;
    const char *r = rootfs();
    size_t rl = strlen(r);
    if (strncmp(p, r, rl) == 0 && (p[rl] == '/' || p[rl] == 0)) return NULL;
    if (strncmp(p, "/proc/", 6) == 0 || strncmp(p, "/dev/", 5) == 0 || strncmp(p, "/system/", 8) == 0) return NULL;
    if (snprintf(buf, PATH_MAX, "%s%s", r, p) >= PATH_MAX) return NULL;
    return buf;
}

static int copy_as_link(int ofd, const char *op, int nfd, const char *np, int flags) {
    struct stat st;
    int sf = (flags & AT_SYMLINK_FOLLOW) ? 0 : AT_SYMLINK_NOFOLLOW;
    if (fstatat(ofd, op, &st, sf) != 0 || !S_ISREG(st.st_mode)) return -1;
    int in = openat(ofd, op, O_RDONLY | O_CLOEXEC | ((flags & AT_SYMLINK_FOLLOW) ? 0 : O_NOFOLLOW));
    if (in < 0) return -1;
    int out = openat(nfd, np, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (out < 0) { int e = errno; close(in); errno = e; return -1; }
    char buf[65536];
    for (;;) {
        ssize_t r = read(in, buf, sizeof buf);
        if (r == 0) break;
        if (r < 0) { if (errno == EINTR) continue; goto fail; }
        for (ssize_t o = 0; o < r;) {
            ssize_t w = write(out, buf + o, r - o);
            if (w < 0) { if (errno == EINTR) continue; goto fail; }
            o += w;
        }
    }
    (void)fchown(out, st.st_uid, st.st_gid);  // 沙箱里多半无效，忽略
    if (fchmod(out, st.st_mode & 07777) != 0) goto fail;
    struct timespec ts[2] = { st.st_atim, st.st_mtim };
    (void)futimens(out, ts);
    if (close(out) != 0) { out = -1; goto fail; }
    close(in);
    return 0;
fail:;
    int e = errno;
    if (out >= 0) close(out);
    close(in);
    unlinkat(nfd, np, 0);
    errno = e;
    return -1;
}

static int fallback_ok(int e) { return e == EPERM || e == EACCES || e == EROFS || e == ENOSYS || e == EXDEV; }

// 只在 dpkg 进程里生效（维护脚本等子进程继承了预载也不受影响）；SANDSHELL_LINKCOPY_ANY=1 可放开（测试用）
extern char *program_invocation_short_name;
static int active(void) {
    if (getenv("SANDSHELL_NO_LINKCOPY")) return 0;
    if (getenv("SANDSHELL_LINKCOPY_ANY")) return 1;
    const char *n = program_invocation_short_name;
    return n && n[0] == 'd' && n[1] == 'p' && n[2] == 'k' && n[3] == 'g' && n[4] == 0;
}

int linkat(int ofd, const char *op, int nfd, const char *np, int flags) {
    long r = syscall(SYS_linkat, ofd, op, nfd, np, flags);
    if (r == 0) return 0;
    int e = errno;
    if (!(fallback_ok(e) || e == ENOENT) || !active()) { errno = e; return -1; }
    char b1[PATH_MAX], b2[PATH_MAX];
    const char *po = (ofd == AT_FDCWD || (op && op[0] == '/')) ? phys(op, b1) : NULL;
    const char *pn = (nfd == AT_FDCWD || (np && np[0] == '/')) ? phys(np, b2) : NULL;
    if (po || pn) {
        if (syscall(SYS_linkat, po ? AT_FDCWD : ofd, po ? po : op, pn ? AT_FDCWD : nfd, pn ? pn : np, flags) == 0) return 0;
    }
    if (copy_as_link(ofd, op, nfd, np, flags) == 0) return 0;
    errno = e;  // 报告真实的 link 错误
    return -1;
}

int link(const char *op, const char *np) { return linkat(AT_FDCWD, op, AT_FDCWD, np, 0); }

// ---- sw4: EROFS(supervisor 路径策略) -> 物理路径重试 ----
#define RETRY1(call_v, call_p, path) do { \
    long _r = (call_v); if (_r == 0 || errno != EROFS || !active()) return _r; \
    int _e = errno; char _b[PATH_MAX]; const char *_pp = phys((path), _b); \
    if (!_pp) { errno = _e; return -1; } \
    { const char *path##_p = _pp; (void)path##_p; return (call_p); } } while (0)
// 物理路径就是同一个真实位置，所以重试的结果（含 ENOENT/EEXIST 等 errno）就是真实结果，原样返回

int mkdir(const char *p, mode_t m) { RETRY1(syscall(SYS_mkdirat, AT_FDCWD, p, m), syscall(SYS_mkdirat, AT_FDCWD, p_p, m), p); }
int mkdirat(int d, const char *p, mode_t m) {
    if (d != AT_FDCWD && !(p && p[0] == '/')) return syscall(SYS_mkdirat, d, p, m);
    RETRY1(syscall(SYS_mkdirat, d, p, m), syscall(SYS_mkdirat, AT_FDCWD, p_p, m), p);
}
int rmdir(const char *p) { RETRY1(syscall(SYS_unlinkat, AT_FDCWD, p, AT_REMOVEDIR), syscall(SYS_unlinkat, AT_FDCWD, p_p, AT_REMOVEDIR), p); }
int unlink(const char *p) { RETRY1(syscall(SYS_unlinkat, AT_FDCWD, p, 0), syscall(SYS_unlinkat, AT_FDCWD, p_p, 0), p); }
int unlinkat(int d, const char *p, int f) {
    if (d != AT_FDCWD && !(p && p[0] == '/')) return syscall(SYS_unlinkat, d, p, f);
    RETRY1(syscall(SYS_unlinkat, d, p, f), syscall(SYS_unlinkat, AT_FDCWD, p_p, f), p);
}
int chmod(const char *p, mode_t m) { RETRY1(syscall(SYS_fchmodat, AT_FDCWD, p, m), syscall(SYS_fchmodat, AT_FDCWD, p_p, m), p); }
int chown(const char *p, uid_t u, gid_t g) { RETRY1(syscall(SYS_fchownat, AT_FDCWD, p, u, g, 0), syscall(SYS_fchownat, AT_FDCWD, p_p, u, g, 0), p); }
int lchown(const char *p, uid_t u, gid_t g) { RETRY1(syscall(SYS_fchownat, AT_FDCWD, p, u, g, AT_SYMLINK_NOFOLLOW), syscall(SYS_fchownat, AT_FDCWD, p_p, u, g, AT_SYMLINK_NOFOLLOW), p); }
int fchownat(int d, const char *p, uid_t u, gid_t g, int f) {
    if (d != AT_FDCWD && !(p && p[0] == '/')) return syscall(SYS_fchownat, d, p, u, g, f);
    RETRY1(syscall(SYS_fchownat, d, p, u, g, f), syscall(SYS_fchownat, AT_FDCWD, p_p, u, g, f), p);
}
int utimensat(int d, const char *p, const struct timespec t[2], int f) {
    if (!p || (d != AT_FDCWD && p[0] != '/')) return syscall(SYS_utimensat, d, p, t, f);
    RETRY1(syscall(SYS_utimensat, d, p, t, f), syscall(SYS_utimensat, AT_FDCWD, p_p, t, f), p);
}
int lutimes(const char *p, const struct timeval tv[2]) {
    struct timespec ts[2], *tp = NULL;
    if (tv) { ts[0].tv_sec = tv[0].tv_sec; ts[0].tv_nsec = tv[0].tv_usec * 1000; ts[1].tv_sec = tv[1].tv_sec; ts[1].tv_nsec = tv[1].tv_usec * 1000; tp = ts; }
    return utimensat(AT_FDCWD, p, tp, AT_SYMLINK_NOFOLLOW);
}
int utimes(const char *p, const struct timeval tv[2]) {
    struct timespec ts[2], *tp = NULL;
    if (tv) { ts[0].tv_sec = tv[0].tv_sec; ts[0].tv_nsec = tv[0].tv_usec * 1000; ts[1].tv_sec = tv[1].tv_sec; ts[1].tv_nsec = tv[1].tv_usec * 1000; tp = ts; }
    return utimensat(AT_FDCWD, p, tp, 0);
}
#include "symrel.h"
// t13j: symlinks go straight to the kernel (BPF ALLOW 36) with physical link path + root-relative target
int symlinkat(const char *t, int d, const char *p) { return ss_symlinkat(t, d, p); }
int symlink(const char *t, const char *p) { return symlinkat(t, AT_FDCWD, p); }
// rename: 两个路径都换成物理路径（同一文件系统内才能 rename）
int renameat(int od, const char *o, int nd, const char *n) {
    long r = syscall(SYS_renameat, od, o, nd, n);
    if (r == 0 || errno != EROFS || !active()) return r;
    int e = errno; char b1[PATH_MAX], b2[PATH_MAX];
    const char *po = (od == AT_FDCWD || (o && o[0] == '/')) ? phys(o, b1) : NULL;
    const char *pn = (nd == AT_FDCWD || (n && n[0] == '/')) ? phys(n, b2) : NULL;
    if (po && pn) return syscall(SYS_renameat, AT_FDCWD, po, AT_FDCWD, pn);
    errno = e; return -1;
}
int rename(const char *o, const char *n) { return renameat(AT_FDCWD, o, AT_FDCWD, n); }

// 写方式打开（O_CREAT/O_WRONLY/O_RDWR/O_TRUNC）被路径策略 EROFS 时，用物理路径再开
static int open_retry(int d, const char *p, int fl, mode_t m) {
    long r = syscall(SYS_openat, d, p, fl, m);
    if (r >= 0 || errno != EROFS || !(fl & (O_CREAT | O_WRONLY | O_RDWR | O_TRUNC)) || !active()) return r;
    if (d != AT_FDCWD && !(p && p[0] == '/')) { errno = EROFS; return -1; }
    char b[PATH_MAX]; const char *pp = phys(p, b);
    if (!pp) { errno = EROFS; return -1; }
    return syscall(SYS_openat, AT_FDCWD, pp, fl, m);
}
static mode_t getmode(int fl, va_list ap) { return (fl & O_CREAT) || (fl & O_TMPFILE) == O_TMPFILE ? va_arg(ap, int) : 0; }
int open(const char *p, int fl, ...) { va_list ap; va_start(ap, fl); mode_t m = getmode(fl, ap); va_end(ap); return open_retry(AT_FDCWD, p, fl, m); }
int open64(const char *p, int fl, ...) { va_list ap; va_start(ap, fl); mode_t m = getmode(fl, ap); va_end(ap); return open_retry(AT_FDCWD, p, fl, m); }
int openat(int d, const char *p, int fl, ...) { va_list ap; va_start(ap, fl); mode_t m = getmode(fl, ap); va_end(ap); return open_retry(d, p, fl, m); }
int openat64(int d, const char *p, int fl, ...) { va_list ap; va_start(ap, fl); mode_t m = getmode(fl, ap); va_end(ap); return open_retry(d, p, fl, m); }
int creat(const char *p, mode_t m) { return open_retry(AT_FDCWD, p, O_CREAT | O_WRONLY | O_TRUNC, m); }
int creat64(const char *p, mode_t m) { return open_retry(AT_FDCWD, p, O_CREAT | O_WRONLY | O_TRUNC, m); }

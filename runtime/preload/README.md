# runtime/preload — t13 PTY 预载层源码

`fwd.c + crashdiag.c` 编译为 `libdebian_exec.so`（PTY 会话的 LD_PRELOAD），把原始 exec 拦截器改名为 `libss_exec_real.so` 并转发给它；两者必须同目录部署。

```sh
gcc -shared -fPIC -O2 -Wl,-soname,libss_crashdiag.so crashdiag.c fwd.c -ldl -o libdebian_exec.so
gcc -shared -fPIC -O2 linkcopy.c -ldl -o libsandshell_linkcopy.so   # 仅 dpkg 进程启用
```

| 符号 | 原因（真机证据） |
|---|---|
| `setfsuid/setfsgid` | trixie ncurses 6.5 打开 terminfo 时调用，Android app seccomp 直接 SIGSYS 杀进程；返回当前 id，不做系统调用 |
| `exec*/chdir/utimensat` 路径翻译 | `/root /home /opt /srv /run /media /boot /sbin` 原拦截器不认识，透传给 Android 得 ENOENT |
| `fchmodat` | glibc 2.41 NOFOLLOW 回退到 `/proc/self/fd/N`，supervisor 在自身进程解析 → EPERM，tar/npm 解压失败 |
| `accept` → `accept4` | Android 杀 `accept(202)`，`accept4(242)` 允许 |
| `symlink/symlinkat` | supervisor 只放行少数 dpkg 形态，其余 EROFS；t13j 起 BPF 放行 36，链接路径换成物理路径、绝对目标换成等价相对目标，返回内核真实结果 |
| `link/linkat` | supervisor 不中介 37，内核看到虚拟路径 → ENOENT；现改为物理路径。注意 Android SELinux 禁止 App 建硬链接（EACCES），这是平台墙，不伪造成功 |

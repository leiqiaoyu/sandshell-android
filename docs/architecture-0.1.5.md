# Sandshell 0.1.5 架构总览（rc12，2026-11-08）

目的：给代码审查用的一张"地图"。标出每层是什么、谁写的（开源/闭源/二进制补丁）、哪里重复、哪里是历史包袱。

## 1. 总图

```
┌──────────────────────────── Android App（dev.sandshell.core，签名证书 77077a32…）────────────────────────────┐
│                                                                                                              │
│  [Java/Kotlin UI，Termux 派生，dex 二进制补丁]                                                                │
│   TermuxActivity ─ TerminalView(classes14, p14 补丁: 鼠标拖拽/fling) ─ TerminalEmulator/TerminalBuffer        │
│   TermuxActivityRootView(classes21 补丁: 小窗抖动)   DebianRootfs/RootfsMigrator(classes24 补丁: 解包/迁移)      │
│        │ JNI                                                                                                  │
│  [libtermux.so]  建 PTY，fork+exec 第一个进程                                                                 │
│        │                                                                                                      │
│  [libdebian_runtime.so  = supervisor，闭源 + 机器码"洞"补丁 t13…t18]                                          │
│     · 装 seccomp 过滤器 #1：路径类 syscall → USER_NOTIF（由 supervisor 线程代办，虚拟路径→物理路径，写策略白名单）  │
│       get*uid → 0；nr≥260 大多 ENOSYS；IP 落在 0x60 区段（sstrap）→ 放行                                       │
│     · ADDFD 回填文件描述符；fakeroot 注册表；symlink 只放行固定形状                                              │
│        │ execve(loader)                                                                                       │
│  [libdebian-ld.so] = Debian 13 的 ld-linux-aarch64（补丁: set_robust_list/rseq → ENOSYS）                     │
│     安卓不允许直接 exec 应用数据目录里的文件 → 所有 Debian 程序都"借" lib 目录里的 loader 启动                    │
│                                                                                                              │
│  [libdebian_exec.so]  PTY 的 LD_PRELOAD：崩溃报告器 + 把 exec*/chdir/utimensat/fchmodat 转给 ↓                 │
│  [libss_exec_real.so] 闭源 exec 改写器：虚拟路径 → "loader --library-path … 程序"；#! 脚本；                     │
│                       历史遗留: node-musl 分支；BusyBox 换名（rc12 已摘除）                                     │
│  [libnode_glibc / libnpm_glibc / libopencode_glibc.so]  打包成 .so 才能执行的捆绑程序                           │
│                                                                                                              │
│  assets/debian-mobile-bashrc  (9.4k 行；31 个 __ss_deploy 载荷，base64+gzip 内嵌)                              │
│  assets/debian-*-rootfs.tar   Debian 13 trixie arm64（rc11 起带手册页）                                        │
└──────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
                 │ 首次启动解包到 /data/user/0/dev.sandshell.core/files/debian（= 进程眼里的 "/"）
                 ▼
┌──────────────────────────── Debian 用户态（每个终端）────────────────────────────────────────────────────────┐
│  bash ── source bashrc：                                                                                    │
│     __ss_gexec   部署/校验载荷（按 sha 幂等）、写 sandshell-exec.conf、glibc-g 包装、gitconfig、gdbinit、fakeroot-tcp │
│     hostenv 重进  LD_PRELOAD 加 libsandshell_spawn + libsandshell_hostenv 再 exec 自己一次                       │
│     __ss_fast     exec 到 sstrap 下（可 touch /root/.sandshell-no-sstrap 关闭）                                  │
│     PATH/TZ/apt.conf/sandshell-fix-man 等函数                                                               │
│                                                                                                              │
│  [sstrap]（开源，tools/sstrap，~1k 行 C，静态，放在 0x60 区段）                                                │
│     · 装 seccomp 过滤器 #2：热路径 syscall → SIGSYS → 进程内处理（不走 supervisor，快 25-50×）                    │
│       stat/open/access/readlink 路径翻译；l2s 硬链接仿真（.l2s 仓 + 相对符号链接 + xattr 计数）；                  │
│       exec：自带 loader 装载程序，保持过滤器与信号掩码；伪造 /proc/self/exe、/proc/<pid>/auxv；                    │
│       vfork / PTRACE_TRACEME（gdb）、clone3→ENOSYS、sendmmsg、NETLINK_AUDIT→EPROTONOSUPPORT                       │
│                                                                                                              │
│  LD_PRELOAD 垫片（rootfs 内，开源 runtime/preload）                                                           │
│     libsandshell_spawn   posix_spawn 用 fork+execve 重做；setfsuid 不发会被杀的调用                              │
│     libsandshell_hostenv exec /system/bin/* 前剥掉 LD_*                                                       │
│     libsandshell_linkcopy 只在 dpkg 里生效：硬链接失败 → 复制；EROFS → 物理路径重试                              │
│     noshm3 / stackfix / sysfix / enosys（glibc-g 那套 node/opencode 用）                                       │
│  /usr/local/bin/dpkg 包装 → 预载 linkcopy 后 exec 真 dpkg                                                      │
└──────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

## 2. 一次系统调用走哪条路

```
Debian 程序 ──syscall──► 过滤器 #2（sstrap）
                          ├─ 热路径 → SIGSYS → sstrap 进程内翻译路径 → 从 0x60 区段重发 → 过滤器 #1 放行 → 内核
                          ├─ 写类/少见路径类 → 交给 supervisor ──► 过滤器 #1 USER_NOTIF → supervisor 线程代办 → 内核
                          └─ 其它 → 过滤器 #1：放行 / ENOSYS / KILL
没有 sstrap 时（opt-out 或旧终端）：只有过滤器 #1，全部路径 syscall 都排队给 supervisor（慢，硬链接不可用）
```

## 3. 一次 exec 走哪条路（复杂度最高的地方）

```
bash 执行 "mv"
  PATH 查找：交互终端 PATH 里物理路径在前（/data/user/0/…/usr/bin/mv）
             dpkg/fakeroot 会把 PATH 重置成虚拟路径（/usr/bin/mv）  ← rc11 前这里被换成 BusyBox
  libc execve
   └► libsandshell_hostenv（若目标是 /system/bin 剥 LD_*）
       └► libdebian_exec → libss_exec_real（虚拟路径 → loader 命令行；#! 处理）
           └► execve syscall ─► sstrap 捕获，自带 loader 装载（保持 seccomp/信号/auxv）
                               └ 无 sstrap 时 → supervisor 的 exec 处理
posix_spawn 走 libsandshell_spawn；node 子进程走 libuv → 同上
```

exec 一共被 4 层碰过：hostenv、libdebian_exec/libss_exec_real、spawn、sstrap。行为不一致的 bug（BusyBox 换名、LD_* 继承、argv0）几乎都出在这里。

## 4. 路径翻译有 3 份实现

| 位置 | 管什么 | 开源？ |
|---|---|---|
| supervisor（libdebian_runtime） | USER_NOTIF 的所有路径 syscall；写策略白名单；symlink 形状白名单 | 闭源 + 补丁 |
| sstrap | 热路径 + 硬链接仿真 + /proc 伪造 | 开源 |
| libdebian_exec / linkcopy | exec/chdir；dpkg 的 EROFS 物理路径重试 | 开源（fwd.c/linkcopy.c）+ 闭源 libss_exec_real |

## 5. 史山清单（审查优先级）

1. **闭源 supervisor + 机器码洞补丁**（t13f 分类器、t16 O_PATH、t17 CLOEXEC、t18 BPF）。每个补丁都只能反汇编验证，模拟器跑不了它。长期：用开源实现替换（sstrap 已承担热路径，可逐步把 USER_NOTIF 部分也搬过来）。
2. **闭源 libss_exec_real**：仍带 node-musl 分支、DEBIAN_LD_PATH/python3.9 特判。rc12 摘了 BusyBox 换名；其余遗留应整体换成开源 exec 层（sstrap 已有自己的 loader，可直接接管）。
3. **bashrc 9.4k 行**：部署器 + 每一代迁移清理（musl、bullseye、node-wall-probe…）+ 31 个内嵌载荷。建议拆成：小 bashrc + 版本化的载荷包（一个 tar，按 manifest 部署），迁移代码按版本号过期删除。
4. **exec 四层叠加**（见第 3 节）：目标是只剩 sstrap 一层 + spawn。
5. **路径翻译三份**（第 4 节）：目标是 sstrap 一份为准，supervisor 只做 sstrap 做不了的（受 Android 限制的调用）。
6. **dex 补丁按文件堆叠**（classes14/21/24 各自 smali 补丁）：建议把 TerminalView 等改动改成源码级 fork 的 terminal-view 模块重新编译，而不是逐版二进制打补丁。
7. **glibc-g 平行世界**（/usr/local/glibc-g、opencode-g/node-g…）：sw 时代的兼容层，现在系统 glibc 可用后，可评估合并。

## 6. 版本/交付物对照

- 测试版：0.1.5-rcN（versionCode 1020+N），构建脚本 build/surgery-015-rcN.py（对上一版 APK 做"手术"替换资产/库/dex）。
- sstrap：tools/sstrap（dev-0.1.5 分支），bashrc 内嵌其 sha。
- 回归：tools/ssprobe/parity.sh（47 项，对照 Termux+proot）；dpkg 维护脚本上下文探针 tools/ssprobe/ss-ctx。

# t13：升级到 Debian 13 trixie（开发记录，2026-10-25）

状态：候选版，未正式发布。真机基线 `0.1.3-trixie-t13j`（versionCode 1013），`t13k`（1014）待验证。

## 真机已验证
- Debian 13 trixie rootless 启动；旧 Debian 11 自动迁移（旧系统改名备份可回滚，`/root`、`/home` 迁入新系统）
- `apt update`、`apt install jq`、`apt install python3`（Python 3.13.5）
- Node 22 / npm 10.9.9，`npm i -g`、目录 chmod
- 普通符号链接（相对/绝对目标）、`/root` 可见、PATH 清理
- `python3 -m http.server` 本地服务可被 curl 访问
- `npx @deepseek-ai/dsh web` 可安装并启动（会话保存受下述硬链接限制影响）

## 修掉的墙
1. glibc ≥ 2.34 的 `set_robust_list`/`rseq` 被 Android 杀：ld.so/libc 中对应 `svc` 改为返回 ENOSYS；Java 侧每次启动自愈。
2. supervisor BPF 放行 `prlimit64`、`sendmmsg`（glibc 2.41 DNS 无回退）、`getrandom`（OpenSSL 3.5 回退到 shmget 被杀）、`close_range`；t13j 再放行 `symlinkat`。
3. ncurses 6.5 调 `setfsuid/setfsgid` 被杀 → 预载 shim。
4. glibc `posix_spawn` 内部 exec 绕过路径翻译 → 预载重实现。
5. supervisor 路径分类只认 `/ /bin /lib /usr /etc /tmp /var` → 补上 `/root /home /opt ...`。
6. `fchmodat` NOFOLLOW 经 `/proc/self/fd` 回退 → EPERM，预载修正。
7. 符号链接被 supervisor 一律 EROFS → 直通内核，路径换算。
详见 `runtime/preload/README.md`。

## 平台限制：硬链接
Android SELinux（`neverallow all_untrusted_apps file_type:file link`）禁止任何普通 App 创建硬链接，`link()` 返回 EACCES，Termux 同样如此。Sandshell 不在系统层伪造硬链接。
需要 `link(tmp, final)` 做“原子且不覆盖”发布的 Node 程序（如 dsh 会话保存），可加载 `scripts/sandshell-node-linkfix.cjs`：

```sh
mkdir -p /usr/local/lib/sandshell
curl -fsSL https://raw.githubusercontent.com/leiqiaoyu/sandshell-android/main/scripts/sandshell-node-linkfix.cjs -o /usr/local/lib/sandshell/node-linkfix.cjs
echo 'export NODE_OPTIONS="--require /usr/local/lib/sandshell/node-linkfix.cjs${NODE_OPTIONS:+ $NODE_OPTIONS}"' >> ~/.bashrc
```
行为：`link` 遇 EACCES/EPERM 且源为普通文件时，目标已存在则报 EEXIST，否则复制到临时文件再 `rename` 就位。结果是独立副本（nlink=1），不是共享 inode。

## 待办
- 非 glibc 程序的高号 syscall（≥260）仍拿到 ENOSYS
- 迁移备份移出 `/root`
- TUI 长按拖动未测
- README 与 Release 在正式发布时更新

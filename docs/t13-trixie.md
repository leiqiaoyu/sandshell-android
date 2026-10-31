# t13：升级到 Debian 13 trixie（开发记录，2026-10-25）

状态：**已正式发布为 0.1.3**（versionCode 1015，2026-10-25，GitHub Release `v0.1.3`）。下文的 t13j（1013）/t13k（1014）是开发过程中的真机基线，保留作记录。

> **版本线说明：0.1.2-gexec 已废弃。** 曾批过的 versionCode 1005 / `0.1.2-gexec`（musl exec 垫片 `libexec_shim_musl.so`）从未发布。glibc 路线走通后不再需要 musl 垫片，0.1.3 直接由 v0.1.1（code 1003）手术构建，所以 `scripts/t13/surgery-*.py` 断言 `cur_name == '0.1.1'`。版本号从 0.1.1 跳到 0.1.3 不是丢了一个版本。

> **asset 命名债：** 0.1.3 的 `assets/debian-bookworm-arm64-minbase.tar` 里装的是 **Debian 13 trixie**（0.1.2 及以前装的是 bullseye 11）。dex 按文件名查找，0.1.3 未改名；0.1.4 起改为 `debian-trixie-arm64-rootfs.tar`。

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
- 迁移备份移出 `/root`（0.1.4 候选已做，未发布）
- TUI 长按拖动未测

## opencode 在 0.1.3 上的状态
- 0.1.3 APK 内含 `lib/arm64-v8a/libopencode_glibc.so`（官方 glibc 构建 opencode 1.18.21，命令 `opencode-g`）。**这是失误**：违反“不预装第三方业务工具”的规则，且未写进 release note 和验证清单。
- 证据：glibc opencode 曾在维护者测试机上跑通完整真实 API 会话（bullseye 时期）；trixie 上 1.19.4 于 2026-10-31 在测试机确认 `--version` 与正常对话。0.1.3 发布包内的 1.18.21 本身没有单独的真机记录。
- 0.1.3 内置的 1.18.21（Bun 1.3.14）受 CVE-2026-111497（Brotli 越界写）影响。
- 处理：0.1.4 公开版移除内置 opencode，需要的用户自行 `npm i -g` 安装；带 opencode 的构建只用于维护者测试机。
- `scripts/install-opencode.sh`（musl + noshm3 路线）随 0.1.3 删除 musl 已失效，仅作历史保留。

## 0.1.3 (versionCode 1015)

- Base: t13k preload (link/linkat translated to physical paths; on Android the kernel then returns the real SELinux EACCES).
- dsh hard-link fallback is built into the default bashrc: `scripts/sandshell-node-linkfix.cjs` is deployed to
  `/usr/local/lib/sandshell/android-link-fallback.cjs`, and the `dsh()` / `npx()` shell functions (npx only when the
  arguments contain `@deepseek-ai/dsh`) load it via `NODE_OPTIONS=--require` in a subshell. Other Node programs are untouched.
- Fallback: on EACCES/EPERM, EEXIST if the target exists, else copy to a hidden temp beside it and `rename()`. The result
  is an independent copy (nlink 1), not a fake hard link. Verified on device with `npx @deepseek-ai/dsh web` (0.2.1-rc.2).

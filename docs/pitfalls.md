# Debian Mobile 避坑指南

这份清单由 Part 1–90 的真实代价换来的：每个雷都以"症状 → 真相 → 规避"三行给出。README 的[那些硬骨头](../README.zh-CN.md#那些硬骨头)案卷讲完整的故事，本文是按症状快速检索的雷区地图——两份文件覆盖同一批事故的两种读法。

适用三类读者：想在无 Root Android 上跑 Linux 用户态的人、本项目贡献者、以及准备开源发布 Android 应用的人（最后一节的雷与运行时无关，任何 Android 项目都会踩）。

## 内核与 seccomp 层

### 继承的 KILL 无法被你的 BPF 削弱

**症状。** 程序启动即死，`Bad system call`，退出码 159，无任何输出；你自己新增 ALLOW/ERRNO 规则毫无作用，装 SIGSYS handler 也拦不住。

**真相。** Android 应用从 zygote 继承 seccomp 过滤器；内核对多层过滤器全部评估后取最高优先级动作，`KILL` 高于 `ERRNO` 和 `USER_NOTIF`，且 KILL 触发时 handler 被忽略。本项目的 Part31 探针实测：`set_robust_list(99)` 连本项目 BPF 默认 ALLOW 的低号调用都被继承规则杀死；`io_uring` 三件、`openat2(437)`、`faccessat2(439)` 同样死于继承 KILL。这是 OEM 策略层，用户态不可推翻。

**规避。** 在承诺任何二进制能跑之前，先用静态探针逐号枚举目标设备真实 syscall 面（本项目把 Part31 探针矩阵留在了证据链里）。继承 KILL 毙掉的调用，别试图加白名单"修"——承认边界并写进文档。深读：[案卷 1](../README.zh-CN.md#案卷-1三只耗子与一个垫片)、[案卷 3](../README.zh-CN.md#案卷-3node-之墙与-musl-弯路)。

### 自己的高号守护会自伤

**症状。** 明明没人毙它，程序还是死：`getrandom` 返回 `ENOSYS`，加密与 UUID 初始化路径随之崩坏。

**真相。** 本项目自己的 BPF 对 `>=260` 一刀切返回 `ENOSYS`（除 `wait4(260)` 唯一 ALLOW），而 `getrandom` 是 278。继承过滤器的子弹没到，先被自己的规则打中。

**规避。** 高号守护规则必须逐号审计例外表；"一刀切 + 白名单"的白名单本身就是攻击面与事故面。

### 内核加载解释器不走用户态 openat

**症状。** 动态链接二进制的 ELF 解释器写成了逻辑 Debian 路径，翻译层"应该"能翻译它，却什么也没翻译。

**真相。** 内核加载 `PT_INTERP` 是内核内部的 open，从不经过用户态 `openat`，seccomp `USER_NOTIF` 调解永远看不见这一步，也就无法翻译。

**规避。** 解释器写成逻辑路径的动态链接二进制，必须包一层 wrapper 显式调用物理加载器。本项目对 musl Node 的精确分流就是这个模式。深读：[案卷 7](../README.zh-CN.md#案卷-7冻结线opencode)。

### 静态二进制的裸 svc 够不着

**症状。** Bun/Zig 风格直接发 `svc` 指令的静态二进制，`LD_PRELOAD` 垫片完全无效。

**真相。** `LD_PRELOAD` 只能拦 libc 函数调用（PLT 路径），裸 `svc` 根本不经过 libc。

**规避。** 唯一已知解法是二进制补丁（本项目有先例：把 `io_uring` 调用改写为返回 `ENOSYS`）。规划功能时先确认目标二进制是不是静态直发型。

## 双 libc 世界

### ABI 就是生死

**症状。** 一次例行重构建之后，所有 Debian 会话启动即退，无一幸免。

**真相。** `libdebian_exec.so` 预加载钩子被用 Android NDK 重编，产出 bionic ABI，被预载进 Debian glibc 进程；两个 ABI 世界在同一地址空间相遇，会话集体阵亡。"能编译"不能证明任何事，你链接的 ABI 就是你必须活下去的 ABI。

**规避。** 跑在 Debian 进程里的钩子只用 Debian Bullseye 的 glibc 工具链构建，`NEEDED` 条目仅允许 `libc.so.6` 与 `ld-linux-aarch64.so.1`；NDK 只构建 Android 侧代码。这条现在是本项目的贡献门槛。深读：[案卷 2](../README.zh-CN.md#案卷-2abi-惨案)。

### musl 与 glibc 的隔离要显式声明

**症状。** musl Node 单独跑没问题，一旦从 Debian 会话里派生就死，症状千奇百怪。

**真相。** musl 进程不能预加载 glibc 钩子，也不能继承带 glibc 味道的 `LD_PRELOAD`/`LD_LIBRARY_PATH`。继承来的环境是状态，而状态是 bug 的载体。

**规避。** 两个 libc 世界共存时，隔离在每个进程边界上显式声明：命中 musl 树的精确路径就剥掉全部继承的加载器变量，再注入 musl 加载器与配套垫片。深读：[案卷 3](../README.zh-CN.md#案卷-3node-之墙与-musl-弯路)。

### AArch64 的 glibc 加载器文件名

**症状。** wrapper 里猜了个 `ld-linux-aarch64.so.2`，执行报 exit 127，而且报错指向的文件完全正常。

**真相。** AArch64 上加载器链条经 `ld-linux-aarch64.so.1` 终结于 `ld-2.31.so`；`.so.2` 后缀是 x86_64 的习惯，在 AArch64 上根本不存在。猜错后缀经 execvp 漏斗产出误导性 exit 127。

**规避。** 用实体文件 `ld-2.31.so` 而不是符号链接——不依赖 `/lib→usr/lib` 与 `→ld-2.31.so` 两级链环的完整性；glibc 的 ld.so 按任意文件名直呼语义一致。

### npm 的 libc 探测读 PATH 上的 ldd

**症状。** 怀疑 npm 在 musl 环境里装错了 glibc 平台包。

**真相。** npm 的 libc 探测执行的是 `PATH` 上的 `ldd`；在 Debian 里它读到 "GNU C Library"，于是"正确地"选择了 glibc 平台包。"装错包"这个假设本身就不成立——它装的是它探测到的。

**规避。** 排查此类问题先确认探测机制本身，别先改包选择。深读：[案卷 7](../README.zh-CN.md#案卷-7冻结线opencode)。

## 路径翻译层

### 错误消息描述的是最后一次失败

**症状。** npm 报 `spawn sh ENOENT`，所有人都盯着 `/bin/sh` 看，而它好好地在那儿。

**真相。** npm spawn 之前先 `chdir` 进包目录，拿到的逻辑 Debian 路径作为物理路径不存在，`chdir` 失败被包装成关于 spawn 的误导性 `ENOENT`。在路径翻译环境里，错误消息描述的是最后一次失败，不是第一次。

**规避。** 进程死在启动阶段时，先审计它想进入什么（`cwd`），再审计它想执行什么；`cwd` 与 `PATH` 是承重结构。深读：[案卷 4](../README.zh-CN.md#案卷-4chdir-的谎言)。

### execvp 漏斗与 PT_INTERP 伪装

**症状。** 报错持续指向一个你能证明完全正常的 wrapper 脚本，真正的二进制根本没被执行。

**真相。** 对 `execvp` 而言 `ENOENT` 是"继续找下一个 PATH 候选"；ELF 解释器缺失（`PT_INTERP` 指向打不开的路径）同样以 `ENOENT` 面目静默出现。而 `ETXTBSY` 是终止性的，搜索停止并上报。真实序列：真二进制的解释器解析失败（静默、继续搜索）→ 搜索走到被锁住的 wrapper → `ETXTBSY` 上报。被点名的文件是最后一个候选，不是预期的那个。

**规避。** `execvp` 报离奇错误时，逐个检查 PATH 上所有同名候选与目标二进制的 `PT_INTERP` 是否物理可达。深读：[案卷 5](../README.zh-CN.md#案卷-5execvp-漏斗)。

### fd 注入的记账契约

**症状。** `npm install` 死于 `sh: node: Text file busy`，教科书含义是"有人正在执行这个文件"，但没有任何人在执行。

**真相。** `openat` 的 `USER_NOTIF` 处理器本地打开真实文件后用 `SECCOMP_IOCTL_NOTIF_ADDFD` 注入子进程；内核把描述符复制给了子进程，但监督进程的本地副本从未关闭。每次写打开都泄漏一个由监督进程持有的写句柄；若干轮 npm 之后，监督进程手里攥着一把写锁，锁住的恰好是 postinstall 想执行的文件。`ETXTBSY` 这次难得说了实话：文件确实忙，只是忙的人不是执行者。

**规避。** 注入者的记账是契约的一部分：`ADDFD` 之后立即 `close(local_fd)`。诊断手法：把架构里每个长命进程的 `/proc/<pid>/fd` 扫一遍，胜过任何理论推演。防御性手法：写临时路径再 `rename` 完成 inode 置换。深读：[案卷 6](../README.zh-CN.md#案卷-6etxtbsy或一个丢失的-close)。

### usrmerge 桥接是排雷重灾区

**症状。** `apt` 装包时在 symlink 环节随机报 `EROFS`，同一机制有的包能过有的不能。

**真相。** Debian usrmerge 把 `/bin`、`/lib`、`/sbin` 变成指向 `usr/*` 的符号链接，`symlinkat` 的每种形态（绝对 target、相对 target、staging 阶段、`.dpkg-tmp` 分支）在翻译层里都是不同的代码路径，一种没覆盖就断一个包。本项目 P1–P6 六轮全在打这场。

**规避。** 每种 symlink 形态单独取证、单独开分支，不合并假设；同族先例（如 libcap2 通过）是证据，不是保证。写面白名单外一律 `EROFS`，宁可多一轮往返，不预支写权限。

## Android 工程与发布层

### APK 就是公开的 zip

**症状。** 开发期的调试口令、npm registry token、内网测试地址，随着 APK 发布一起公开了。

**真相。** APK 是 zip 容器，任何人用任意解压工具都能读出 `assets/` 与 dex 里的全部字符串。放进去的每一个字节都等于公开张贴。

**规避。** 打包前扫一遍将随 APK 分发的一切文件；口令、token、`.npmrc` 凭据、内网地址一个都不能有。本项目上传 90 个历史构建 APK 前要自检的正是这一条（约定见 [parts/apks/README.md](../parts/apks/README.md)）。

### 签名密钥口令写进构建脚本等于泄露

**症状。** 发布时才发现旧签名密钥不能用了。

**真相。** 本项目的真实事故：上游 Termux 构建配置里带着开发密钥的口令，随代码一起公开，那把密钥从此只能按已泄露处理。v0.1 被迫轮换到全新密钥，而轮换加上 `sharedUserId` 意味着旧开发版无法原地升级到 v0.1——用户必须先卸载，卸载会连带删除应用私有目录里的整个 Debian rootfs。

**规避。** 密钥口令永远放本地不入库；接手任何上游代码先审它的签名配置；决定轮换密钥要趁早，越晚用户代价越大。

### 上游血统的默认值会跟着你发布

**症状。** 发布审计发现：`debuggable=true`、8 项从未使用的权限、2 个外部可调用的导出组件，全部来自上游模板，一个都没被察觉地带到了候选发布包里。

**真相。** 复用上游应用骨架时，manifest 里的每个默认值都是上游的决策，不是你的决策。`debuggable` 让任何人可调试你的应用；悬浮窗、读系统日志、装未知应用、使用统计这类权限是滥用高发户；导出的 `RunCommandService` 意味着任意应用都能驱动你的终端执行命令。

**规避。** 发布前逐项过 manifest：权限问"哪行代码用了它"，导出组件问"谁被允许调它"，`debuggable` 直接删。本项目 v0.1 的完整逐项决策记录在[权限审计](permission-audit.md)。

### 资产命名与实际内容会脱节

**症状。** rootfs 资产文件名写着 `bookworm`，实际内容验证出来是 Bullseye；`.tar.gz` 资产在 APK 里被 Android 打包器悄悄解成了裸文件。

**真相。** 文件名不是证据；Android 打包器对 `assets/` 里的 `.tar.gz` 有自动解 gzip 行为。

**规避。** 资产内容用哈希与实测验证，不信任文件名；gzip 资产改名（如 `.asset` 后缀）绕开打包器的自动解压，复制到目标位置后再恢复原名。

### 大文件分发走 release asset

**症状。** 仓库 clone 越来越慢，或者 git 直接拒收大文件。

**真相。** GitHub 对超 50 MiB 的 git 文件告警，超 100 MiB 拒收；release 附件不占 git 体积、单文件上限 2 GiB。本项目曾把约 55 MiB 无用的 Node 资产打进 APK（Part32 才删掉），90 个历史构建 APK 约 3 GiB，全部走 git 会拖垮仓库。

**规避。** 批量二进制归档挂 release（本项目的做法是 [dev-archive](https://github.com/leiqiaoyu/sandshell-android/releases/tag/dev-archive)）；发布 APK 只留一份在 release，仓库内留哈希清单。

### GPL 血统决定许可证

**症状。** 想把项目标成 MIT 或 Apache-2.0 发布。

**真相。** 本项目 UI 与共享代码逐字衍生自 Termux v0.118.3（GPL-3.0-only），衍生作品只能保持 GPLv3-only，改标更宽松的许可证不合法；musl（MIT）、AndroidX（Apache-2.0）等上游例外按组件记入 NOTICE，不改变整体协议。

**规避。** 定许可证前先画清代码血统：逐字复用的部分服从上游协议，其余部分才谈得上自选。深读：[NOTICE](../NOTICE)。

## 方法论

### 真机退出码是唯一裁决

QEMU 与模拟器只用来列举候选，不能覆盖真机结果；"PASS"只等于目标真机真实 `exit=0`，不等于输出文本长得像成功。本项目的 Node 之墙正是这样裁定的：QEMU 上的全部候选假设，最后都让位给真机的一次 `exit=159`。

### 不预支与冻结纪律

没有证据就不加规则、不扩写面、不伪造成功（无证据不加 `linkat`/`mknodat`、不扩 `/var`、不伪造服务与用户）。每轮迭代列出冻结项，已验证路径不因新实验回退。增强项永远不阻断启动：Node 安装失败只写日志回滚，终端照常进入——这一条让后续几十轮排雷没有一次把应用搞到起不来。

### 只读取证与证据保全

排雷期间不 kill 进程、不重装、不改环境变量；关键 inode 保留原件（如 `node.inode-leak`）作为证据；需要修改时走"写临时文件再 `rename`"的 inode 置换，让症状可以搬家而不是消失。每份取证文档只记录已发生的输出，不掺入未验证的归因。

### 承重墙清单也是交付物

有些墙在兼容层内可修（路径、fd 泄漏、加载器路由），有些不可修（继承的 KILL 优先级、内核内部的解释器打开、静态二进制的裸 `svc`）。精确画出两类边界、并对不可修一侧如实冻结（本项目的 opencode 线），本身就有文档价值——它替后来者省掉的正是当初没人替我们省掉的那几十个 Part。

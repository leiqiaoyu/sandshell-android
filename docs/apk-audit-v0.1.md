# Part58 APK 审计报告 → v0.1 发行版

> 审计基线：`debian-mobile-part58-node-musl-npmrc-auto-bootstrap-arm64-v8a.apk`
> SHA-256 `fd78752548ea7122a7902015b605fa20c98e873b1c843b5953b1037fe2e874ce`（与《开源任务完整移交书》第 3 节对账一致）
> 发行产物：`apk/debian-mobile-v0.1-arm64-v8a.apk`，SHA-256 `b8c6ae5cf6be04511e5ba13d1e66c0c88a7480d5bdf7226c0f2dc8a8ffa4dfc0`
> 审计工具：jadx 1.5.1（反编译 + 资源解码）、pyaxmlparser（manifest/资源交叉解析）、自研 AXML 手术与终检脚本（`scripts/`）
> 审计日期：2026-08-22（2026-08-23 修订：`MANAGE_EXTERNAL_STORAGE` 应维护者要求保留，见[权限审计](permission-audit.md)修订记录）

## 0. 结论速览

| 审计维度 | 结论 |
|---|---|
| 完整性/溯源 | 原包对账通过；v0.1 载荷 724 条目与基线逐字节一致（CRC32 全量对账），仅 manifest 按确认清单修改 |
| UI 中英文 | 无中英混杂、无乱码；中文覆盖 43%，Debian Mobile 专属文案已全部汉化；未译项均为上游 Termux 设置子屏文案 |
| 屎山代码 | **未发现**。自研层仅 4 个手写类约 1,141 行，职责清晰；其余为 Termux v0.118.3 上游代码与标准依赖库 |
| 安全漏洞 | 发现 2 个发布级缺陷（`debuggable=true`、15 项过度权限）+ 2 个导出面风险，**已在 v0.1 修复**（权限 15→8；`MANAGE_EXTERNAL_STORAGE` 经维护者决策保留用于互通桥，须用户手动开启）；无硬编码密钥、无明文敏感数据 |
| 签名 | 已轮换至全新发布密钥（旧开发 keystore 口令视为已泄露）；v1+v2+v3 齐全 |

## 1. 完整性与溯源

- 审计基线 APK 的 SHA-256 与移交书记录完全一致；APK 内三大 native 组件与移交书"变更范围"声明对账一致，证明 v0.1 **未触碰任何运行时二进制**：

| 组件 | SHA-256 | 对账 |
|---|---|---|
| `lib/arm64-v8a/libdebian_runtime.so` | `0174b041cbffed93779d8601812726c21c9ab9e16ef9207ec94e0c57f668804b` | ✅ 与移交书一致 |
| `lib/arm64-v8a/libdebian_exec.so` | `4f4c6e338f736014af4b279f8be3fb949eb1663757b0b8d438a7f7ae4dab2625` | ✅ 与移交书一致 |
| `assets/noshm3-arm64-gcc.bin` | `d98bc5d50e5e1cb07fc48f572fc96643a2ed1be284c1162388f1a9151eea34b3` | ✅ 与移交书一致 |

- v0.1 构建方式：对二进制 `AndroidManifest.xml` 做字节级手术（详见[权限审计](permission-audit.md)），剥离旧 v1 签名三元组后以新密钥 `zipalign` + 重签名；其余 724 个 zip 条目（29 个 dex、5 个 native 库、11 个 assets、全部资源）与基线 CRC32 一一相同。

## 2. UI 中英文审计

对 `resources.arsc` 解码后的字符串资源统计（工具：`scripts/audit_ui_strings.py`）：

| 指标 | 数值 |
|---|---|
| 默认语言（英语）字符串 | 260 条 |
| `values-zh-rCN` 字符串 | 126 条 |
| 实际完成翻译（值不同） | 112 条（**43%**） |
| 默认语言中混入中文 | **0 条**（语言分离干净） |
| 疑似乱码（mojibake） | **0 条** |
| 孤立/失效翻译键 | 0 条 |

要点：

1. **应用名 `application_name` 中英一致为 "Debian Mobile"** —— 品牌名不翻译，正确做法。
2. **Debian Mobile 专属文案已全部汉化**：`debian_validation_title`="验证"、`debian_validation_summary`="运行 Debian Mobile 的 18 项回归验证" 等。
3. **终端主界面框架文案已汉化**：终端、键盘、调试、终端外观/输入输出等设置标题与摘要。
4. **未翻译的 134 条集中在**：上游 Termux 的设置子屏（软键盘开关说明、日志级别、崩溃报告通知、插件错误通知）与 Termux 插件生态标题（Termux:API / Termux:Float / Termux:Tasker / Termux:Widget）。这些属于上游字符串，对本项目的核心使用路径（打开终端 → 跑命令 → Validation 18/18）无影响；后续可作为 i18n 增量任务。
5. 中文环境下 UI 会呈现"已译框架 + 英文设置子屏"的混合观感，属于**待改进项而非缺陷**；没有任何一处中文被错误地放进英语默认资源。

## 3. 代码质量（"屎山"排查）

### 3.1 体量与构成

| 构成 | 规模 | 评估 |
|---|---|---|
| 自研层 `com.debian.runtime` | 4 个手写类共 **1,141 行**（`DebianRootfs` 757 / `MainActivity` 209 / `Supervisor` 111 / `TerminalPanel` 64；另 `R.java` 3,594 行为生成代码） | 小而清晰 |
| 上游层 `com.termux.*` | 154 个 Java 文件（Termux v0.118.3） | 成熟上游，社区维护 |
| 依赖库 | 29 个 dex（AndroidX 全家桶 + Material + Kotlin + coroutines + commons-io） | 上游 Termux 构建配置所致，非本项目引入的膨胀 |

### 3.2 自研层逐类审读

- `DebianRootfs`：rootfs 解包、部署、npmrc 幂等恢复、存储桥配置。方法职责单一、失败路径全部显式（`onFailure` 回调 + 日志），无嵌套地狱，无复制粘贴重复块。**质量良好**。
- `Supervisor`：native 库加载与命令捕获执行，111 行，薄封装。**质量良好**；唯一小瑕疵：`Capture.run()` 中 `IOException` 静默吞掉（管道读端关闭时的正常路径），建议后续加 debug 级日志。
- `MainActivity`（Validation）：18 项回归的可视化驱动，匿名内部类 + 手工线程（上游兼容风格），逻辑平铺直叙。**质量合格**。
- 29 个 dex 反编译仅 1 处 jadx 报错（位于 Kotlin 标准库元数据，非业务代码），主链路反编译完整可读。

### 3.3 结论

**没有屎山**。所谓体积大头全部来自上游 Termux 的依赖树；真正本项目写的代码是一个克制的、可审计的薄层。这一点与项目的"轻薄兼容层"定位一致。

## 4. 安全审计

### 4.1 已在 v0.1 修复的发现

| # | 发现 | 严重度 | 处置 |
|---|---|---|---|
| S1 | `android:debuggable="true"` —— 任何持有 adb/同一设备上下文者可调试应用、读取应用私有内存 | **高** | 已改 `false` |
| S2 | 15 项权限含 `READ_LOGS`、`DUMP`、`WRITE_SECURE_SETTINGS`、`REQUEST_INSTALL_PACKAGES`、`PACKAGE_USAGE_STATS` 等 | **高** | 移除 7 项，保留 8 项（逐项理由见[权限审计](permission-audit.md)）；`MANAGE_EXTERNAL_STORAGE` 经维护者决策保留：`/mnt/sdcard` 互通桥依赖它，且为用户手动开启的开关权限，非安装即得 |
| S3 | `RunCommandService` 导出 + 自定义 **dangerous** 权限 `com.debian.runtime.permission.RUN_COMMAND`：任意应用理论可申请该权限后向终端注入命令与参数 | **高** | `exported=false`，外部 API 整体关闭 |
| S4 | `SettingsActivity` 导出且无任何权限保护 | 中 | `exported=false` |
| S5 | 旧签名密钥口令硬编码于上游 `app/build.gradle`（密钥视为已泄露） | 高（供应链） | 已轮换全新 RSA-2048 发布密钥；源码树发布前必须清除该口令 |

### 4.2 记录在案、有意保留的残余面

- `TermuxOpenReceiver$ContentProvider`（`.files`）仍导出：`termux-open` 把文件交给外部查看器的必需路径；受 RUN_COMMAND 权限保护，滥用前提是攻击者拿到 `pm grant`（需 adb 或物理接触）。后续建议改为 URI 级精确授权。
- `TermuxFileReceiverActivity` 的 SEND/VIEW intent-filter：标准"分享到应用"形态，MIME 白名单未见通配异常。
- 自定义权限 `protectionLevel="dangerous"` 本身（上游设计）：外部 API 已关闭，实际暴露面趋零；后续可升 signature 级。

### 4.3 内容扫描

- **硬编码密钥扫描**（`password|secret|api_key|token|private_key` 模式，覆盖 `com.debian` + `com.termux` 全部反编译源）：**0 命中**。
- **assets 审查**（11 个文件全部列出 SHA-256，见 `apk/SHA256SUMS.txt`）：
  - `install-node22-musl-candidate.sh`：手动运行的实验脚本（不自动执行），从 HTTPS 端点下载 Node/musl/gcc 并**逐一 SHA-256 校验**后才解包，无 `curl | sh` 反模式；`--resolve` IP 锁定仅作 DNS 失败回退。判定：安全。
  - `getrandom-observer` / `node-wall-probe` / `shm-enosys` 系列探针：冻结实验产物（Part30/33/34 调查工具），不参与启动路径。
  - `debian-bookworm-arm64-minbase.tar`（31.9 MB）：**文件名与内容不符**——实际为 Debian Bullseye 11.11 minbase（移交书已记录）。纯命名瑕疵，不影响运行；0.1 不改（改名即改变基线字节）。
  - `debian-mobile-bashrc`：虚拟路径提示符与 `/mnt/sdcard` 重定向配置，无敏感内容。
- **明文隐私/物理路径**：APK 内无 `/data/user/0/...` 硬编码（该问题仅存在于未发布的 19 GB 开发工作区脚本中）。

### 4.4 分发面

- `allowBackup="false"`（正确，阻止 adb backup 拖走应用数据）。
- `extractNativeLibs="true"`、`targetSdkVersion=28`：上游遗留配置，Android 16 上可正常安装运行（真机已验证）；升级 targetSdk 属于后续大版本工作。
- v0.1 已通过 `zipalign`（4 字节）验证；`resources.arsc` 保持 STORED 且对齐。

## 5. 发行判定

Part58 基线 + 上述四项修复（debug / 权限 / 导出面 / 签名轮换）后的产物，满足移交书第 7 节对 0.1 发行版的全部前置要求，**判定为可发布**。真机验收清单（启动、shell、hello/curl/nano、Validation 18/18）沿用 Part58 的既有记录；互通桥（`/mnt/sdcard`）保留，真机回归时应包含"在系统设置开启所有文件访问 → 桥接目录可用"一项（见[权限审计](permission-audit.md)第五节）。

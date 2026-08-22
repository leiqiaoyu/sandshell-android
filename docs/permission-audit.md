# 权限与组件审计 — v0.1 发行版

> 审计对象：`debian-mobile-part58-node-musl-npmrc-auto-bootstrap-arm64-v8a.apk`
> （SHA-256 `fd78752548ea7122a7902015b605fa20c98e873b1c843b5953b1037fe2e874ce`，与移交书对账一致）
> 审计时间：2026-08-22　·　方法：二进制 `AndroidManifest.xml`（AXML）字节级手术，见 [`scripts/manifest_surgery.py`](../scripts/manifest_surgery.py)
> 结论：v0.1 中移除 7 项权限、关闭 2 个导出组件、移除 debug 标记，其余载荷（dex / native 库 / assets / resources）与审计基线逐字节一致（CRC32 全量对账）。
> 修订（2026-08-23）：应维护者要求恢复 `MANAGE_EXTERNAL_STORAGE`（`/storage/emulated/0/Debian-mobile` ↔ Debian `/mnt/sdcard` 互通桥依赖它），移除数由 8 项改为 7 项，保留 8 项。

## 一、权限决策（15 → 8）

### 移除的 7 项（维护者已确认）

| 权限 | 移除理由 |
|---|---|
| `android.permission.SYSTEM_ALERT_WINDOW` | 悬浮窗权限，核心功能未使用。 |
| `android.permission.READ_LOGS` | 读取系统日志，越权且 OEM 差异大，未使用。 |
| `android.permission.DUMP` | 系统诊断信息权限（signature 级，普通应用申请无意义）。 |
| `android.permission.WRITE_SECURE_SETTINGS` | signature 级权限，第三方应用实际无法获得，属无效声明。 |
| `android.permission.REQUEST_INSTALL_PACKAGES` | 未知来源应用安装权限，与本项目分发方式无关且是滥用高发权限。 |
| `android.permission.PACKAGE_USAGE_STATS` | 使用情况统计权限，隐私敏感，未使用。 |
| `com.android.alarm.permission.SET_ALARM` | 上游 Termux 闹钟特性遗留，本项目未使用。 |

### 保留的 8 项

| 权限 | 保留理由 |
|---|---|
| `android.permission.MANAGE_EXTERNAL_STORAGE` | **互通目录桥**（Android 侧 `/storage/emulated/0/Debian-mobile` ↔ Debian 侧 `/mnt/sdcard`）所需。**不会自动授予**：安装后默认关闭，需用户在系统设置中手动开启"所有文件访问"开关，桥接才会创建。应用代码只对该单一目录做桥接。诚实的代价说明：Android 权限模型没有"只授权一个文件夹"的传统路径权限，此开关在系统层面是全共享存储粒度的；按文件夹精确授权的 SAF 方案列入后续路线。维护者于 2026-08-23 确认保留。 |
| `android.permission.INTERNET` | Debian 侧 `curl`、`apt update` 等网络能力的运行前提。 |
| `android.permission.ACCESS_NETWORK_STATE` | 网络状态检测（上游 Termux 逻辑）。 |
| `android.permission.WRITE_EXTERNAL_STORAGE` | `targetSdkVersion=28` 下的遗留存储路径，分享/接收文件逻辑仍引用；迁移 SAF 前保留。 |
| `android.permission.WAKE_LOCK` | 终端会话保持唤醒。 |
| `android.permission.VIBRATE` | 终端 bell / 通知振动。 |
| `android.permission.FOREGROUND_SERVICE` | `TermuxService` 前台服务（会话生命周期）必需。 |
| `android.permission.REQUEST_IGNORE_BATTERY_OPTIMIZATIONS` | 用户在设置页主动发起电池白名单申请（非默认弹窗），见下方回归项。 |

## 二、组件导出面收敛

| 组件 | 原状态 | v0.1 状态 | 说明 |
|---|---|---|---|
| `com.termux.app.activities.SettingsActivity` | `exported="true"`，无权限保护 | **`exported="false"`** | 仅应用内跳转，无外部调用方。 |
| `com.termux.app.RunCommandService` | `exported="true"` + 自定义 dangerous 权限 | **`exported="false"`** | 外部命令 API（RUN_COMMAND intent）是全应用最大攻击面，v0.1 默认关闭。 |
| `com.termux.app.TermuxActivity` | LAUNCHER（隐式导出） | 保留 | 主入口，必须可启动。 |
| `com.termux.filepicker.TermuxDocumentsProvider` | `exported="true"` + `MANAGE_DOCUMENTS` | 保留 | 系统 DocumentsProvider 机制（SAF）本体，保留是正确的。 |
| `com.termux.app.TermuxOpenReceiver$ContentProvider`（`.files`） | `exported="true"` + RUN_COMMAND 权限 | 保留（记录在案） | `termux-open` 将文件交给外部查看器所必需；若设为不导出会破坏该功能。受自定义权限保护，残余风险已知：外部应用理论上可经 `pm grant` 获得该 dangerous 权限（需 adb/物理接触）。后续版本建议改为按 URI 精确授权。 |
| `com.termux.filepicker.TermuxFileReceiverActivity` | SEND/VIEW intent-filter（隐式导出） | 保留（记录在案） | "分享/打开方式 → Debian Mobile"导入功能。已审计其 intent-filter 为标准 MIME 白名单形态。 |
| `com.termux.HomeActivity`（activity-alias） | IOT_LAUNCHER | 保留（记录在案） | 仅 Android Things 设备会使用，普通设备无暴露面。 |

## 三、构建标志

| 项 | 原 | v0.1 |
|---|---|---|
| `android:versionName` | `0.118.3`（上游 Termux 版本号遗留） | **`0.1`** |
| `android:versionCode` | `1002` | `1002`（保持，避免降级安装问题） |
| `application android:debuggable` | **`true`（发布重大缺陷）** | **`false`** |
| `applicationId` / `sharedUserId` | `com.debian.runtime` | 不变（项目硬约束） |
| `minSdkVersion` / `targetSdkVersion` | 24 / 28 | 不变 |

## 四、签名轮换

- 原签名：开发 keystore（口令曾硬编码于上游 `app/build.gradle`，按已泄露处理）。
- v0.1 签名：全新 RSA-2048 发布密钥（有效期至 2056 年），v1 + v2 + v3 三方案齐全，`zipalign` 4 字节对齐验证通过。
- **升级注意**：应用声明 `sharedUserId="com.debian.runtime"`，Android 拒绝跨签名覆盖安装。装有旧开发版的设备必须先卸载再安装 v0.1。

## 五、回归项（发版已知代价）

1. **`/mnt/sdcard` 互通桥：保留但需手动授权**：`MANAGE_EXTERNAL_STORAGE` 声明保留后，桥接代码路径与 Part58 行为完全一致；但该权限安装后默认处于关闭状态，用户需在系统设置（应用 → Debian Mobile → 权限 → "所有文件访问"／文件和媒体）中手动开启，`Environment.isExternalStorageManager()` 才会为 true，桥接才会创建。未授权时的降级路径已验证为非阻断（log warning + 继续运行）。
2. **外部 RUN_COMMAND API 关闭**：Tasker / Termux:Widget 类外部触发方式在 v0.1 不可用。这是有意的攻击面收敛。
3. **电池优化白名单**：`REQUEST_IGNORE_BATTERY_OPTIMIZATIONS` 权限保留，但 Settings 页发起的白名单申请行为在 Android 16 上需真机复核。

## 六、验证方式

```bash
# 1. 从 v0.1 APK 抽取 manifest 并核对（versionName=0.1 / debuggable=false / 8 项权限 / 2 组件不导出）
python3 scripts/audit_ui_strings.py          # UI 文案审计
python3 scripts/verify_final.py              # 终检：manifest、arsc 对齐、载荷 CRC 对账、签名块
```

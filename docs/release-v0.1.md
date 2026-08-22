# Debian Mobile v0.1

**English** | [中文](#中文)

## English

Debian Mobile 0.1 is the first public release: a real Debian Bullseye (ARM64) userland running inside an ordinary, unprivileged Android app sandbox — no root, no PRoot, no ptrace core, no VM. It ships the device-validated Part58 build with open-source release hygiene applied.

### Download

`debian-mobile-v0.1-arm64-v8a.apk` — SHA-256 `b8c6ae5cf6be04511e5ba13d1e66c0c88a7480d5bdf7226c0f2dc8a8ffa4dfc0`

### What changed versus the Part58 baseline

- `versionName` now `0.1` (was the upstream `0.118.3`); `debuggable` removed.
- Permissions cut from 15 to 8 — dropped logs, dump, secure-settings, install-packages, usage-stats, overlay and other unused grants ([permission audit](../permission-audit.md)).
- External command API (`RunCommandService`) and `SettingsActivity` no longer exported.
- Re-signed with a fresh release key (the old development keystore is treated as compromised). **Uninstall older development builds before installing** — the app uses `sharedUserId`, and Android rejects cross-signature upgrades.

Every code, asset and native payload byte inside the APK is identical to the audited Part58 baseline except the manifest (full CRC-level provenance in the [APK audit](../apk-audit-v0.1.md)).

### The interop folder (`/mnt/sdcard`)

The bridge that maps `/storage/emulated/0/Debian-mobile` on the Android side to `/mnt/sdcard` inside Debian is kept. `MANAGE_EXTERNAL_STORAGE` is not auto-granted: after installing, open system Settings → Apps → Debian Mobile → Permissions and enable "All files access" (or the "Files and media" page's all-files toggle) once; the bridge then works as in Part58. Until you flip that switch the app simply runs without the bridge.

### Known limitations

- Node.js is not bundled and not supported; the musl Node experiment remains frozen.
- Verified command set: see the README "Verified Capabilities" table. Anything not listed there is unverified by design.

---

## 中文

Debian Mobile 0.1 是首个公开发行版：在普通无特权 Android 应用沙盒中运行真正的 Debian Bullseye（ARM64）用户态——不需要 Root、不用 PRoot、不把 ptrace 作为运行时核心、没有虚拟机。本版本即经过真机验证的 Part58 构建，叠加了开源发布卫生处理。

### 下载

`debian-mobile-v0.1-arm64-v8a.apk` — SHA-256 `b8c6ae5cf6be04511e5ba13d1e66c0c88a7480d5bdf7226c0f2dc8a8ffa4dfc0`

### 相对 Part58 基线的变更

- `versionName` 调整为 `0.1`（原为上游遗留的 `0.118.3`）；移除 `debuggable` 标记。
- 权限从 15 项收敛到 8 项——移除读日志、dump、安全设置、安装未知应用、使用统计、悬浮窗等未使用授权（见[权限审计](../permission-audit.md)）。
- 外部命令 API（`RunCommandService`）与 `SettingsActivity` 不再导出。
- 使用全新发布密钥重签名（旧开发 keystore 按已泄露处理）。**安装前请先卸载旧开发版**——应用使用 `sharedUserId`，Android 拒绝跨签名覆盖升级。

除 manifest 外，APK 内全部代码、资源与 native 载荷与审计基线逐字节一致（CRC 级溯源见[APK 审计报告](../apk-audit-v0.1.md)）。

### 互通文件夹（`/mnt/sdcard`）

Android 侧 `/storage/emulated/0/Debian-mobile` 与 Debian 侧 `/mnt/sdcard` 之间的互通桥保留。`MANAGE_EXTERNAL_STORAGE` 不会自动授予：安装后请到系统设置 → 应用 → Debian Mobile → 权限，手动开启"所有文件访问"开关，桥接即按 Part58 的方式工作；未开启时应用照常运行，只是没有互通目录。

### 已知限制

- 不内置也不支持 Node.js；musl Node 实验路线维持冻结。
- 已验证命令集以 README"已验证能力"表格为准，未列出的功能一律视为未验证。

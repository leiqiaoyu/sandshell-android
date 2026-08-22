# scripts/ — 可审计的发布管线

v0.1 发行 APK 不是重新编译产物，而是对已真机验收的 Part58 基线做**字节级最小手术**后的重签名产物。本目录的脚本即该过程的完整可复现记录。

## 管线（在审计工作目录中按序执行）

```bash
# 0. 前置：unzip Part58 APK 得到二进制 AndroidManifest.xml（记为 unpacked/AndroidManifest.xml）

# 1. manifest 手术：versionName=0.1、debuggable=false、移除 7 项权限、
#    SettingsActivity/RunCommandService 改 exported=false。
#    注意：MANAGE_EXTERNAL_STORAGE 被保留（维护者决策，/mnt/sdcard 互通桥依赖它）。
#    自带语义 diff 与断言自检（未触碰字符串逐字节保留）。
python3 scripts/manifest_surgery.py

# 2. 重建未签名 APK：替换 manifest、剥离旧 v1 签名三元组、
#    其余条目保持压缩方式与时间戳；CRC32 全量对账确保零意外改动。
python3 scripts/rebuild_apk.py

# 3. 签名：zipalign(4) + v1/v2/v3（Android build-tools 的 zipalign/apksigner，密钥见下）
zipalign -f -p 4 debian-mobile-v0.1-unsigned.apk aligned.apk
apksigner sign --ks <release.jks> --ks-key-alias debian-mobile \
     --ks-pass file:<pw-file> --v1-signing-enabled true \
     --out signed/debian-mobile-v0.1-aligned-signed.apk aligned.apk

# 4. 终检：manifest 语义、resources.arsc 存储方式与 4 字节对齐、
#    724 个载荷条目与基线 CRC 一致、v2/v3 签名块存在。
python3 scripts/verify_final.py

# 5. UI 中英文文案审计（字符串资源对比）
python3 scripts/audit_ui_strings.py
```

## 签名密钥

发布密钥**永不入库**。密钥文件、口令与轮换说明保存在维护者本地的 `release-keys/` 目录（`.gitignore` 已排除 `*.jks`）。丢失发布密钥意味着用户无法从 v0.x 原地升级——请离线备份。

## 脚本一览

| 脚本 | 作用 |
|---|---|
| `manifest_surgery.py` | 二进制 AXML 手术 + 语义 diff 自检 |
| `rebuild_apk.py` | 重建未签名 APK + CRC 对账 |
| `verify_final.py` | 发行产物终检（manifest/对齐/一致性/签名块） |
| `audit_ui_strings.py` | UI 字符资源本地化审计 |

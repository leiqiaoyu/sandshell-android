# apks/ — 工程 APK 上传区（Part 1–90 各阶段构建产物）

本目录预留给项目维护者上传开发过程中的工程 APK。在维护者上传之前，这里保持为空。

## 上传约定

- **命名**：`part-NN-<slug>.apk`（两位数字，例如 `part-14-pythonhome.apk`、`part-58-node-musl-npmrc-auto-bootstrap.apk`）。
- **体积红线**：
  - Git 提交上传：单文件 **勿超 50 MiB**（GitHub 对超 50 MiB 的文件告警，超 100 MiB 直接拒收）；
  - **批量上传 90 个 APK 时强烈建议走 Release Asset 而非 git**：Release 附件不占 git 仓库体积、单文件上限 2 GiB。可建一个 `dev-archive` 专用 release 分批挂载，避免仓库膨胀数 GB 拖慢所有人 clone。
- **脱敏要求**：上传前确认 APK 不含 keystore、口令、token（v0.1 之前的开发构建多为 debug 签名，属可接受范围，但请在文件名或提交说明注明）。

## 与仓库其他部分的关系

- v0.1 正式发行 APK 不放在这里，见 `apk/debian-mobile-v0.1-arm64-v8a.apk` 与 [v0.1 release](https://github.com/leiqiaoyu/debian-mobile/releases/tag/v0.1)。
- 各 Part 的里程碑文档放 `parts/part-NN-<slug>.md`，约定见上级 [parts/README.md](../README.md)。

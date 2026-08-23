# parts/ — Part 1–90 里程碑材料（维护者上传区）

本目录预留给项目维护者上传 Debian Mobile 开发史（Part 1 至 Part 90）的里程碑材料。
在维护者上传之前，这里保持为空。

## 上传约定

- **命名**：`part-NN-<slug>.md`（两位数字，例如 `part-30-node-sigsys-evidence.md`、`part-58-release-candidate.md`）；附件放入同名子目录 `part-NN-<slug>/`。
- **语言**：中文或英文均可；结论性文档优先。
- **体积**：Git 单文件超过 50 MiB 会收到警告、超过 100 MiB 会被 GitHub 拒收。rootfs tar（约 206 MB）与 QEMU Node 二进制（约 122 MB）等大文件**不要**放入本目录，应作为 Release Asset 或 LFS 对象分发，并先完成来源与许可证审计。
- **脱敏要求**（发布前自检）：
  - 不含任何 keystore、口令、token；
  - 不含 `/data/user/0/...` 物理路径（改写为 `<app-files>` 之类的占位符）；
  - 原始 strace/构建日志只保留脱敏结论，完整原始件留在私有归档。

## 目录规划

```text
parts/
├── README.md          ← 本文件
├── apks/              ← 工程 APK 上传区（Part 1–90 各阶段构建产物，约定见 apks/README.md）
├── part-01..part-90   ← 里程碑文档与小型附件
└── index.md           ← （维护者生成）Part 1–90 总索引
```

## 与仓库其他部分的关系

- 项目叙事总览：仓库根 `README.md` 的 "Development History" 与 `docs/DEBIAN_MOBILE_README_RESEARCH_DOSSIER.md`。
- 0.1 发行版与审计：`docs/release-v0.1.md`、`docs/permission-audit.md`、`docs/apk-audit-v0.1.md`。

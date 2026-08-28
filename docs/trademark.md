# Debian 商标往来记录与整改方案

> 本文档记录本项目与 Debian 商标团队的沟通结论与对应整改动作。往来邮件原件由维护者离线存档（涉及个人信息，不公开）。

## 时间线

| 日期 | 事件 |
|---|---|
| 2026-08-24 | 维护者按 Debian 商标政策要求，向 trademark@debian.org 提交使用申请，覆盖三项：应用名 "Debian Mobile"、Debian 漩涡启动器图标、包名 `com.debian.runtime` |
| 2026-08-28 | Debian 商标团队回信：**三项全部拒绝** |

## 拒绝内容与理由

1. **应用名 "Debian Mobile"**：会暗示这是 Debian 项目的官方或背书产品，而它不是；
2. **Debian 漩涡图标作为应用图标**：同上，不得使用；
3. **`com.debian.*` 包名**：维护者不拥有 com.debian（debian.com TLD）命名空间。

团队同时说明：市场上已存在同类应用（支持 Debian 或其他发行版的 userland / chroot / shell），单独授权某一个会误导市场。

## 仍然被允许的使用

回信明确允许：**在应用描述中如实、不误导地声明"本应用使用了 Debian 用户态的组成部分"**。这属于 Debian 商标政策（回信时为 v2.0）"When You Can Use the Debian Trademarks Without Asking Permission" 一节覆盖的说明性使用。本项目运行未经修改的 Debian Bullseye（ARM64）rootfs，这一事实性表述继续成立，项目 README 的技术叙事不受影响。

## 整改动作（2026-08-28 当日执行）

- [x] 撤下 v0.1 发行版（唯一公开发行版）及其 APK 资产与 `v0.1` 标签（公开约 4 天，下载 1 次）；
- [x] 撤下 dev-archive 归档发行版及已上传的 2 个旧工程 APK；
- [x] 从仓库 git 树移除发行 APK（原 `apk/` 目录），原件由维护者离线保存；
- [x] README 双语版添加撤回声明，安装章节与商标章节改写；
- [x] `parts/apks/` 上传区改为暂缓状态，停止旧 APK 公开分发；
- [x] NOTICE 中漩涡图标条目改为"已停用"并记录原因。

## 待办（更名重构）

- [ ] 选定新应用名（不含 "Debian"，建议也不含 "Deb" 字样）；
- [ ] 新包名（建议 `io.github.leiqiaoyu.<新名>`，不占用他人命名空间）；
- [ ] 新启动器图标（原创自绘，不用 Debian 漩涡）；
- [ ] 源码工程内修改 `applicationId`、应用显示名、图标、互通文件夹名（`/storage/emulated/0/Debian-mobile` 同步更名）后重新构建；
- [ ] 重走审计 → 签名 → 发布流程（沿用 v0.1 的发布卫生基线：权限 8 项、无 debug 标记、发布密钥）；
- [ ] 全仓文案更名清扫（README×2、docs、scripts）；
- [ ] GitHub 仓库改名（网页端 Settings → Rename，旧链接自动重定向）；
- [ ] 重新发布发行版并恢复安装章节。

## 备注

- 回信语气友好，明确表示"很高兴看到有产品把 Debian 体验带到 Android"；拒绝仅针对商标使用本身，无任何费用，亦无追责。
- 收到书面拒绝后继续分发即属明知故犯，故当日执行撤回。v0.1 公开仅约 4 天，此刻是更名代价最低的窗口。

# LiteArc 文档索引

2026-11-05 起 `main` 的前端开发方向为 Flutter/Dart，归档功能继续由 C++23
Application/Core 和官方 libarchive 实现。需求变更不等于 Flutter 已落地。
旧 Qt 基线分支为 `codex/qt-baseline-2026-11-05`，提交
`53185742ad1e640b24da397cfa3544507af4edef`。

## 当前需求与架构

- [产品状态与入口](../README.md)
- [完整开发要求](../AGENTS.md)
- [0001 Core 与前端边界](architecture/0001-core-and-frontend.md)
- [0002 任务、索引与有界传输](architecture/0002-managed-indexing.md)
- [0003 安全流式解压](architecture/0003-secure-extraction.md)
- [0004 创建与原子输出](architecture/0004-streaming-compression.md)
- [0005 桌面行为与迁移对齐](architecture/0005-desktop-experience.md)
- [0006 打包与系统集成](architecture/0006-packaging-and-platform-integration.md)
- [0007 Flutter 决策、C ABI 与 F1–F6 验收](architecture/0007-flutter-frontend.md)
- [依赖来源、Flutter 工具链与原生构建](../third_party/README.md)

## 开发与发布说明

以下现有命令服务于 Qt 基线；Flutter 构建与打包入口在实现、验证后补充。

- [Qt 基线开发、操作与构建说明](development/qt-baseline.md)
- [Windows 基线打包与 Flutter 发布待办](../packaging/windows/README.md)
- [Linux 基线打包与 Flutter 发布待办](../packaging/linux/README.md)
- [macOS 基线打包与 Flutter 发布待办](../packaging/macos/README.md)
- [共享图标原始素材与历史 Qt 资源路径](../frontend/qt/resources/icons/README.md)

## 历史验证记录

保留原始测试版本、命令、路径和结论；它们不会自动成为 Flutter 验收结果。

- [2026-10-07-libarchive-zip-abort](validation/2026-10-07-libarchive-zip-abort.md)
- [2026-10-07-m2](validation/2026-10-07-m2.md)
- [2026-10-07-m3](validation/2026-10-07-m3.md)
- [2026-10-07-refactor](validation/2026-10-07-refactor.md)
- [2026-10-08-m4](validation/2026-10-08-m4.md)
- [2026-10-09-workflow](validation/2026-10-09-workflow.md)
- [2026-10-10-dependency-resolution](validation/2026-10-10-dependency-resolution.md)
- [2026-10-11-dependency-resolution](validation/2026-10-11-dependency-resolution.md)
- [2026-10-11-icon-about](validation/2026-10-11-icon-about.md)
- [2026-10-12-m5](validation/2026-10-12-m5.md)
- [2026-10-12-main-packaging-fixes](validation/2026-10-12-main-packaging-fixes.md)
- [2026-10-17-official-libarchive](validation/2026-10-17-official-libarchive.md)
- [2026-10-18-folder-browser](validation/2026-10-18-folder-browser.md)
- [2026-10-18-litearc](validation/2026-10-18-litearc.md)
- [2026-10-19-android-mobile-fixes](validation/2026-10-19-android-mobile-fixes.md)
- [2026-10-19-folder-list](validation/2026-10-19-folder-list.md)
- [2026-10-28-code-cleanup](validation/2026-10-28-code-cleanup.md)
- [2026-10-28-dependency-update](validation/2026-10-28-dependency-update.md)

## 本次需求变更记录

- [2026-11-05 Flutter 需求与文档切换](validation/2026-11-05-flutter-requirements.md)

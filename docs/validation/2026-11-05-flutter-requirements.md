# 2026-11-05 Flutter 需求与文档切换

## 范围与分支

按用户要求将主线前端需求从 Qt Quick/QML 改为 Flutter/Dart，并同步全部项目
Markdown 文档。`main` 继续用于 Flutter 开发；迁移前的完整版本保存在
`codex/qt-baseline-2026-11-05`，指向
`53185742ad1e640b24da397cfa3544507af4edef`，已在编辑前推送到 origin。

本次是需求与文档提交：现有 Qt 源码、构建、CI、打包、依赖 gitlinks 和资产未变。
未新增 Flutter app、C ABI 或可用的 Flutter 发布入口，未宣称完成迁移。

## 更新内容

- AGENTS.md / README 明确主线、目标架构、当前状态和 F1–F6 实施顺序。
- 新增 [Flutter ADR](../architecture/0007-flutter-frontend.md)，定义 C ABI、
  公共 metadata 查询、分页、选择身份、线程/资源寿命与原生平台验收。
- 六篇既有架构文档同步新前端边界；Core/安全/压缩约束继续有效。
- 依赖和三个平台发布说明区分 Flutter 待办与现存 Qt 工具。
- 原 README 的详细构建与操作说明保留为
  [Qt 基线开发文档](../development/qt-baseline.md)，相对链接随位置调整。
- 图标文档明确资产复用；所有既有历史验证记录仅增加背景说明，原始证据保留。
- 增加 [文档索引](../README.md)，区分当前决策、旧版命令和历史证据。

## 验证边界

本次文档检查通过：35 个 Markdown 文件、105 个本地链接/锚点、代码围栏、
全部原有项目文档覆盖、AGENTS.md 的 102 节编号和前端需求一致性。
18 篇既有历史记录的正文保持原样，`git diff --check` 通过；本地与 origin
的保留分支均指向原提交，当前工作分支为 `main`。
无生产源码/构建配置变化，因此未执行编译、Core/Qt 测试或 Flutter 性能验收。
先前 Qt 通过记录不作为本次 Flutter 运行证据。

macOS x86_64 仍是兼容目标；已记录官方 Intel 弃用风险及最新稳定 SDK 的实施
验收门槛，未擅自宣称该平台迁移完成或取消其目标范围。

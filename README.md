# 轻匣 · LiteArc

轻匣（LiteArc）是面向 Windows、Linux、macOS 的跨平台归档管理器。
主线技术方案为 **Flutter / Dart 前端 + 稳定 C ABI + C++23 Application/Core +
[libarchive/libarchive](https://github.com/libarchive/libarchive)**。

## 主线与保留版本

2026-11-05 起，`main` 用于 Flutter 前端开发。迁移前的完整 Qt 版本已保存在
[`codex/qt-baseline-2026-11-05`](https://github.com/ananwanan/AnAnArchiver/tree/codex/qt-baseline-2026-11-05)，
基线提交为 `53185742ad1e640b24da397cfa3544507af4edef`。

F1 已加入独立共享库 `ananarchiver_c_api`，包装现有异步打开、解压、创建与任务服务，
并提供纯 C 消费者和生命周期测试。F2 已将目录索引、Unicode 查询、排序和选择解析
抽离为公共 C++ Application 服务，提供异步、有代次校验的 C ABI 分页和原生选择。
**F3 已实现真正运行的 Flutter 归档浏览：Home、系统文件选择、内容检测打开、
目录树/当前目录虚拟列表、导航、Unicode 搜索排序和原生多选。Windows x64 已验证；
Linux/macOS runner 已接线但尚未原生验证。** `frontend/qt` 仍是迁移回归对照。 现有 Qt 源码、CMake 预设、
打包脚本和历史测试暂留作对照，不能作为 Flutter 的完成或验收证据。

完整需求见 [AGENTS.md](AGENTS.md)，决策与验收见
[Flutter 前端 ADR](docs/architecture/0007-flutter-frontend.md)，其他文档见
[文档索引](docs/README.md)。

## 架构与目标目录

```text
Flutter widgets / Dart Controller / ViewModel
    → Dart FFI → ananarchiver_c_api
    → C++ Application → Core / libarchive adapter → libarchive
```

```text
include/ananarchiver/      现有、独立于 GUI 的 Core / Application API
src/core/                 元数据、格式、Unicode、安全路径
src/backend/libarchive/   唯一调用 libarchive C API 的生产代码
src/application/          归档服务、任务、按需扫描、公共 metadata 浏览
include/ananarchiver/c_api/ ABI v1 公共 C 头文件
src/c_api/                C ABI、结果所有权与异步清理
frontend/flutter/         Flutter app、独立 FFI package、平台 runner/adapter 与测试
frontend/qt/              现有 Qt 基线，迁移期保留
tests/                    Core / Archive / Application / Security / C ABI 回归
third_party/              原生依赖源码与来源记录
docs/                     架构、开发说明、分平台发布与历史证据
```

Flutter 不直接操作 libarchive，也不重写压缩、解压、安全和任务逻辑。
C ABI 不导出 STL 或 C++ 异常，明确 handles、UTF-8、内存释放和版本协商。
IO、索引、查询和清理运行在受管理 worker；UI 只读取有界任务快照及分页视图。
完整 metadata 索引留在 C++，避免在 Dart 再持有百万条目的完整副本。

## 已有能力与 Flutter 待迁移范围

已有 C++ 实现包括内容检测、流式枚举、安全解压、覆盖策略、密码、取消、
实际字节进度及原子创建。writer capabilities 决定可创建的 ZIP、7z 和 TAR
过滤器组合；读取支持不等于写入支持。完整性测试、归档编辑和预览仍需独立实现，
不能因换前端而标记完成。

Flutter 必须对齐现有归档目录树与当前目录列表、隐式父目录/空目录、文件夹优先、
根目录/上级导航、搜索排序、多选、原始条目身份、选中/全部解压、密码与覆盖交互、
创建、任务中心、主题、最近记录、快捷键及系统文件拖入拖出。
失败或超限的目录视图不能执行选中操作；切换目录、搜索和排序清空选择并使旧查询失效。
真实文件系统浏览继续使用独立的按需扫描，所有归档安全与资源上限继续生效。

迁移阶段：

1. F1：已实现 C ABI v1、任务与所有权测试；已验证 Windows x64 Release。
2. F2：已实现公共目录索引、查询、有界分页和原生选择；验证范围见 [F2 记录](docs/validation/2026-11-06-metadata-browsing.md)。
3. F3：已实现 Flutter Home、真实打开和有界归档浏览；[开发说明](docs/development/flutter.md)、[Windows 验证与性能](docs/validation/2026-11-06-flutter-browsing.md)。
4. F4：复用现有解压、压缩、密码、覆盖、进度与取消。
5. F5：设置迁移、任务中心、主题、桌面拖放和系统入口对齐。
6. F6：各平台 Release 打包、来源/许可、安装升级与大规模性能验收。

## 平台与依赖要求

目标仍包括 Windows 10/11 x64、Linux x86_64、macOS arm64 与 macOS x86_64。
Flutter 官方已公布 Intel Mac 弃用规划；x86_64 兼容需要按最新稳定 SDK 核实，
未解决构建/运行支持前不能宣称全平台迁移完成。后续可扩展 Linux/Windows ARM64。
Android 沿用已有实验范围；Flutter 移动端、Storage Access Framework 和 `content://`
接入另行验收，旧 Qt APK 的验证不能直接继承。

使用官方最新稳定 Flutter 和其配套 Dart。原生依赖继续从指定上游解析最新稳定版，
优先使用官方 libarchive；Qt 仅属于旧基线依赖。一次构建记录实际 SDK/engine
revision、Dart、插件、backend/codecs、编译器与平台 SDK，下一次更新重新解析。
应用的 `pubspec.lock` 在创建 Flutter app 时提交，记录一次解析结果，不能替代后续更新。
详见 [依赖方案](third_party/README.md) 和
[官方支持平台](https://docs.flutter.dev/reference/supported-platforms)。

## 当前可用的构建与验证

Core / Backend / Application 无需 Qt 或 Flutter，可继续使用现有入口：

```sh
cmake -P scripts/init-dependencies.cmake
cmake --preset core-release
cmake --build --preset core-release
ctest --preset core-release
```

Windows 非 Developer Shell 可通过 `scripts/Invoke-Msvc.ps1` 执行 CMake/CTest。
Core 构建默认同时生成 `ananarchiver_c_api`；可通过
`ANANARCHIVER_BUILD_C_API=OFF` 关闭。接口、线程规则、限制和消费示例见
[C ABI 开发说明](docs/development/c-api.md) 与 [公共 metadata 浏览](docs/development/metadata-browsing.md)。
此共享库无需 Qt、Flutter 或 Dart SDK。
完整 Qt 构建、依赖重放、桌面操作、Android APK 与历史限制统一见
[Qt 基线开发说明](docs/development/qt-baseline.md)。现有 `desktop-release`、
`desktop-developer`、`Package-*` 和 `Build-Android.ps1` 仍服务 Qt；Flutter 的
归档浏览可按 [Flutter 开发说明](docs/development/flutter.md) 构建，安装器/正式打包继续在 F6 验收。

GitHub Actions 的自动触发目前暂停，保留的手动作业仍是 Core/Qt 基线。
Flutter 迁移需增加 C ABI、Dart format/analyze、binding/widget/integration tests
与各平台 Release 流程，不将已有 Qt CI 状态当作 Flutter 验收。

性能以同一 backend、codec 参数和数据的 Release 构建对照：首屏时间、100k/1M
条目下的峰值内存、滚动帧时间、查询延迟、取消响应、启动及包体积，均需实测。
旧历史结果见 [验证记录索引](docs/README.md#历史验证记录)。

## 产品与发布兼容

中文显示名为“轻匣”，英文为“LiteArc”；可执行文件、安装器和分发包沿用 LiteArc。
保留 `ananarchiver` API/target/选项、应用设置和系统集成身份，以及
`share/AnAnArchiver` 数据位置。Flutter 迁移需明确旧主题/最近记录的迁移策略，
复用原图标和许可证，重新验证系统打开、长路径、安装升级、签名和平台权限。

现有发布说明分别描述 [Windows](packaging/windows/README.md)、
[Linux](packaging/linux/README.md)、[macOS](packaging/macos/README.md) 的 Qt 基线；
各页列出 Flutter 发布待办。用户界面、归档路径和 backend 错误按纯文本显示；
密码不写入日志、设置、构建来源或 crash report。

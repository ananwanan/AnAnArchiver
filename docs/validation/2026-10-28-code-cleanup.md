# 2026-10-28 代码整理验证

## 范围

- 经用户确认，从当前 Git 版本恢复缺失的 Core、Application、测试、脚本、打包文件和依赖源码。
- 保留工作区已有的 Android 布局、存储访问控制和相关测试改动。
- 将四个 Qt controller 中重复的错误详情拼接集中到
  `frontend/qt/platform/archive_error_details.cpp`，保留摘要翻译、字段顺序和换行行为。
- 将创建与解压对话框的焦点滚动逻辑提取为
  `frontend/qt/qml/components/FocusScrollView.qml`，保留各对话框的事件连接和滚动条设置。
- 统一 QML 文件清单，由清单生成资源别名；资源 URI 和原有别名保持一致。
- 将前端测试配置迁至 `frontend/qt/tests/CMakeLists.txt`，集中通用 target 配置。

## 构建与验证

在新的 `out/build/code-cleanup-msvc` 目录执行 Release 构建，使用 MSVC
19.51.36260.0 和现有 Qt 6.12.0 MSVC kit。复用
`out/build/android-ui-host/dependencies/install`，依赖解析记录和实际源码通过
provenance 校验。本次恢复使用 Git 记录的依赖 revision。

```powershell
./scripts/Invoke-Msvc.ps1 -Command cmake -CommandArguments @(
    '--build', 'out/build/code-cleanup-msvc', '--parallel', '6')
ctest --test-dir out/build/code-cleanup-msvc --output-on-failure --parallel 4
```

- MSVC 构建通过；项目源码编译未出现警告。
- CTest **18/18 通过**，总耗时 230.71 秒，包含 Core、压缩、解压、Application、
  Qt 浏览、拖放、桌面流程、文件名渲染、焦点滚动、启动与打包/依赖工具回归。
- 与原构建记录逐项比较，12 个前端及打包测试保留原名称、超时和环境变量。
- 新增 `FocusScrollView.qml` 的 Qt `qmllint` 检查通过。
- 新增 C++ 文件的 `clang-format --dry-run --Werror` 和 `git diff --check` 通过。

Qt 生成的 Main QML 编译单元在 Qt 的 `qmetatype.h` / `qvariant.h` 中产生三条
MSVC C4702（不可达代码）警告。验证覆盖本机 Windows 桌面构建；Android 真机、
Linux、macOS 和重新打包未在本轮执行。

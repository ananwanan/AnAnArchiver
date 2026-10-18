# 2026-10-18：归档文件夹导航与文件列表分栏

本文记录首次分栏实现。后续右侧列表同时显示当前目录的文件和子文件夹，
见 [2026-10-19 补充记录](2026-10-19-folder-list.md)。

## 实现

打开压缩包后，左侧显示仅含文件夹的归档目录树，包括隐式父目录和显式空目录；
右侧只显示所选目录的直接非目录条目，名称列显示文件名。提供根目录入口、
上一级按钮、当前位置，以及方向键展开/移动、Enter/空格激活和 Backspace 返回。
搜索与排序限定于当前目录，切换目录立即清空旧文件列表和选择。

目录树、文件过滤和排序由现有受管理的元数据 worker 执行，不读取文件内容。
不可变目录快照在导航、查询及向既有目录追加文件时复用，保持展开状态；
旧快照在 worker 上回收。显示路径规范化不修改原始归档路径及 entry index，
重复文件名的选中解压和拖出仍定位原始条目。“解压全部”仍作用于整个归档。
接受“浏览目录”对话框会关闭当前归档并切回原有本地目录模型。

归档目录树额外限制单条路径 64 KiB、路径层级 256、总节点 1,000,000
（含内部根节点），以及路径/名称 UTF-16 文本累计 128 MiB。
超限以结构化错误显示，状态提示列表可能不完整，并禁止无效视图的选择与解压；
关闭并重新打开正常归档可恢复。

需求已回写 `AGENTS.md` 第 40、41、43 节；README 和桌面架构说明同步。
本次未更新依赖、子模块、Core 或 Application 的归档操作实现。

## 编译与回归

使用现有完整源码 Qt 的 Windows x64/MSVC Release 配置：
`out/build/desktop-release-traced/application`。

- 完整重编译成功；生产 QML lint 无告警。
- 最终代码的 Core/Application/Qt/平台元数据 CTest：17/17 通过。
  报告：`out/artifacts/folder-browser/regression-results.xml`。
- 本轮依赖工具 Python 回归：98/98 通过；后续只修改前端和文档，未重复执行。
- 新增回归覆盖空目录/隐式父目录、Unicode、直属文件范围、树快照复用、
  切换目录清除选择、重复条目实际解压、目录范围拖出及导出失效、
  30 万条目异步导航/取消、64 KiB 恶意深路径预算和失败后的重新打开。
- 生产 QML 流程覆盖鼠标目录选择、键盘展开/激活、根目录/上级、
  当前目录搜索排序、选中解压、主题，以及切回本地目录。
- 文件夹名、文件名和当前位置按纯文本显示；恶意标记测试没有发出网络请求。
- 中英文 × 100%、125%、150%、200%：生产浏览流程 8/8、纯文本渲染 8/8
  通过；根目录高亮修复后，中英文 100% 流程再次通过。明暗主题截图检查正常。
  明细：`out/artifacts/folder-browser/ui-matrix.json`；最终根目录和嵌套目录截图：
  `out/artifacts/folder-browser/final/zh-CN-archive-root.png`、
  `out/artifacts/folder-browser/final/zh-CN-archive-folders.png`。
- `git diff --check` 通过。

本机已有构建目录缓存了中文 MSVC include 前缀，而默认构建进程输出英文，
首次增量构建遗漏了包含类布局变更的旧对象。通过匹配缓存语言的完整重编译
消除旧对象后，启动、Shell 和桌面回归均通过。复现编译：

```powershell
./scripts/Invoke-Msvc.ps1 -Command cmake -CommandArguments @('-E', 'env', 'VSLANG=2052', 'cmake', '--build', 'out/build/desktop-release-traced/application', '--clean-first', '--parallel', '4')
./scripts/Test-Desktop.ps1 -BuildDirectory out/build/desktop-release-traced/application -TemporaryRoot D:/Cache/Codex/AnAnArchiver-Folders -Jobs 4
```

本次验证为 Windows 主机与生产 QML offscreen 流程；未重新运行 Linux/macOS/
Android 构建、远程 CI 或安装包验收，也未更新现有分发包。

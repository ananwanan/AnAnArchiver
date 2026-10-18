# 2026-10-19：当前目录列表同时显示文件和子文件夹

## 行为与需求

在原有左右分栏基础上，右侧列表同时显示当前目录的直接文件与子文件夹。
所有排序方向均保持文件夹优先；文件夹按名称排序，文件保留原有数值、时间和
稳定重复项排序。目录列表包含隐式目录、显式目录和空目录，同一路径只显示一行。
双击或 Enter 进入子目录；根目录、上一级、左侧目录树与当前目录搜索继续可用。

文件夹可多选、解压和拖出。虚拟目录行使用不可变目录快照中的内部节点，
不伪造 libarchive 条目编号；真实文件仍使用原始 enumeration index。
`ExtractionRequest.selectedDirectoryPaths` 与文件索引选择取并集，由后端验证路径、
按目录边界匹配后代并执行现有安全流式解压，UI 不同步遍历整棵子树。
目录 `foo` 的选择不包含同名根文件 `foo` 或相似前缀目录 `foobar`。
显式空目录保留，只有文件夹选择时不会退化为解压全部。

目录选择在创建输出前执行路径、数量、深度、长度与元数据预算检查，拒绝
空路径、根路径、穿越、绝对路径、无效编码等输入。原有 staging、链接策略、
覆盖交互、取消和回滚机制继续生效。拖出缓存同时记录文件索引与目录路径；
切换两个隐式文件夹的选择也会使旧导出失效。

`AGENTS.md` 第 40、43 节、README 与桌面架构说明已同步到此行为。
前一版仅显示文件的验证保留在 `2026-10-18-folder-browser.md`，不代表本次结果。

## 验证环境

Windows x64、MSVC、现有完整源码 Qt Release 配置：
`out/build/desktop-release-traced/application`。本次未更新依赖或子模块。
生成归档和解压结果使用 `D:/Cache/Codex/AnAnArchiver-Folders`。

## 本地结果

- 应用及测试构建成功，生产 QML lint 无告警。
- 最终 Core/Application/Qt/平台元数据 CTest：17/17 通过，0 失败。
  报告为 `out/artifacts/folder-list/regression-results.xml`。本轮未重复运行
  未改动的依赖工具 Python 测试。
- 回归覆盖所有列/方向的文件夹置顶、目录搜索/激活、隐式/显式目录去重、
  30 万条目导航/取消、重复文件原始索引、混合目录与文件的实际解压、
  空目录保留、相似前缀/同名文件隔离，以及无效目录选择在创建输出前失败。
- 生产 QML 覆盖单击选择、实际双击进入、Enter 进入、上级/根目录、
  目录选中解压、目录拖出及在同层两个隐式目录间切换选择后的导出失效。
- 恶意文件夹名称在右侧目录行同样按纯文本渲染，不发出网络图片请求。
- 中英文 × 100%、125%、150%、200%：生产浏览流程 8/8、名称渲染 8/8
  通过；根目录、文件与子文件夹同层显示，以及明暗主题截图检查正常。
  明细：`out/artifacts/folder-list/ui-matrix.json`；最终截图：
  `out/artifacts/folder-list/final/zh-CN-archive-direct-children.png`。
- `git diff --check` 通过。

复现构建与回归：

```powershell
./scripts/Invoke-Msvc.ps1 -Command cmake -CommandArguments @('-E', 'env', 'VSLANG=2052', 'cmake', '--build', 'out/build/desktop-release-traced/application', '--target', 'all', 'AnAnArchiver_qmllint', '--parallel', '4')
ctest --test-dir out/build/desktop-release-traced/application --output-on-failure --parallel 4 -E '^dependency_tools$'
```

本次不包含 Linux/macOS/Android 构建、远程 CI、实际安装或分发包更新。

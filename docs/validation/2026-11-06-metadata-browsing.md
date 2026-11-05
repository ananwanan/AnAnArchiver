# 2026-11-06 F2 公共 metadata 浏览验证

## 范围与状态

在 `main` 的 `4b0c8804a544ac1789f23cf47ff4bd39574fca03` 上完成未提交的 F2 修改。
主线前端仍是 Flutter/Dart；本轮没有创建 Flutter UI 或 Dart binding，下一阶段是 F3。
Qt 修改仅让现有迁移适配器使用公共服务，并验证其行为未回归。

已实现：

- 无 Qt 依赖的 Application `MetadataIndex` / `BrowserService`：根目录、直接子项、
  隐式父目录、空目录、去重、文件夹优先、搜索、排序和选择解析。
- 与现有 Qt 对照的 Unicode 简单折叠、UTF-16 比较与稳定排序；重复文件保留原始
  枚举编号；文件夹使用独立身份与 `AA_NO_ENTRY_INDEX`，选中目录经安全路径校验。
- 受管理 worker 上完整构建、查询、排序、分页和原生大选择；条目数、节点、深度、
  路径、累计目录文本及索引内存预算；失败不发布部分完整索引。
- 归档/查询/选择 generation，过期拒绝，独立的 lazy folder-tree windows，查询内
  全选以及完整归档解压的原有独立入口。
- ABI v1 的九个新增 `aa_browser_*` 导出，保留原有全部十八个导出及结构布局。
  新接口不传输完整索引或逐字段 FFI 调用，原生选择通过 handle 留在 C++。
- 引擎 cleanup worker 的异步索引回收、任务回收及有界结果所有权；Qt 查询结果
  合并在单个 mailbox，事件队列只传唤醒；解压和拖出在 Application worker 解析选择。
- README、Flutter ADR、C ABI / metadata 开发说明及进度更新。

Core/backend 的解析、解压事务和压缩管线未重写。`FileTreeModel` / `DirectoryService`
职责未改变。`.github` 自动触发策略、第三方子模块和保留的 Qt 基线分支未修改；
基线仍为 `53185742ad1e640b24da397cfa3544507af4edef`。没有提交或推送。

## 环境与来源

Windows x64 Release；MSVC 19.51.36260.0、CMake 4.4.0、Ninja、Python 3.14.7。
构建 provenance 标记 application revision 为上述提交、`dirty: true`。

原生依赖重放已有经校验解析：`2026-10-28T15:46:45.521277+00:00`，没有声称本轮
重新查询了 backend/codec 最新版本。libarchive 3.8.10，revision
`0c48412be2d94afe1859a807ea7896a18d8900bd`；来源为官方
`https://github.com/libarchive/libarchive`。复用 F1 经校验的 native dependency prefix，
没有退回系统 libarchive，也没有修改依赖或构建来源校验。
完整来源、版本、ref、revision、工具链及参数保存在构建目录的
`resolved-dependencies.json`、`dependency-build.json` 和 `dependency-provenance.json`。

历史 `out/build/desktop/application` 的构建配方身份与当前配方不一致，来源检查正确
拒绝复用。Qt 回归因此使用新的 `out/build/f2-qt`，显式设置开发套件选项并选择现有
Qt 6.12.0 kit：`out/build/desktop/dependencies/install/qt`。本次 Qt provenance 将其
记录为 `developer-kit`、revision unavailable；没有把当前源码 HEAD 充当该二进制
的 revision，也没有宣称完成新的正式 Qt source/package build。

唯一新增的数据来源为 Unicode 官方 latest UCD；ReadMe 声明 final，生成的表记录
Unicode 18.0.0、URL、UTC 获取时间和 SHA-256：
`a004797658a457bec4dc11683e39f69249ea3b595b752dbea6721c4c9f587b0d`。
使用 C + S 简单折叠以保留 Qt 语义，Unicode-3.0 许可已保存并安装。
构建不联网下载该表；更新通过 `scripts/generate-case-fold.py` 显式执行。

## 实际命令与结果

以下命令在仓库根目录执行。路径是本次本机验证记录，不是生产代码或固定 SDK 要求。

```powershell
./scripts/Invoke-Msvc.ps1 cmake -S . -B out/build/f2-core -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DANANARCHIVER_SUPERBUILD=OFF `
  -DANANARCHIVER_BUILD_QT=OFF `
  '-DANANARCHIVER_DEPENDENCY_PREFIX=D:/code/AnAnArchiver/out/build/f1-release/dependencies/install'
./scripts/Invoke-Msvc.ps1 cmake --build out/build/f2-core --parallel 6
ctest --test-dir out/build/f2-core --output-on-failure -E '^dependency_tools$' --parallel 4

./scripts/Invoke-Msvc.ps1 cmake -S . -B out/build/f2-qt -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DANANARCHIVER_SUPERBUILD=OFF `
  -DANANARCHIVER_BUILD_QT=ON -DANANARCHIVER_USE_SYSTEM_QT=ON `
  '-DANANARCHIVER_DEPENDENCY_PREFIX=D:/code/AnAnArchiver/out/build/f1-release/dependencies/install' `
  '-DCMAKE_PREFIX_PATH=D:/code/AnAnArchiver/out/build/desktop/dependencies/install/qt'
./scripts/Invoke-Msvc.ps1 cmake --build out/build/f2-qt --parallel 6
ctest --test-dir out/build/f2-qt --output-on-failure -E '^dependency_tools$' --parallel 4

ctest --test-dir out/build/f1-release/application --output-on-failure -R '^dependency_tools$'
```

- 新的 GUI-free 构建成功，**9/9 原生 CTest suites 通过**，约 3.31 秒。
  包括 Core、archive、extraction/security、compression、Application、metadata、
  C11 consumer、原有 C ABI 和新增 browser C ABI。
- 完整 Qt/native 回归 **21/21 suites 通过**，约 12.87 秒；包含 browser、drag、
  extraction、adapters、shell、desktop、desktop flow、filename rendering、compression、
  Home smoke、打包元数据检查，以及同一组原生测试。
- 依赖工具 suite 单独通过，约 232.18 秒；本次没有修改其实现或测试。
- 两个 Release 构建没有本次项目 warning/error。新增 C++ 文件的
  `clang-format --dry-run --Werror`、修改处格式化、Python 脚本语法检查及
  `git diff --check` 通过。

新增测试覆盖目录结构/去重/空目录、组件边界、CJK/emoji/重音/希腊语/大小写特殊项、
Qt 实际搜索与目录排序对照、未知值排序、稳定 ties、重复原始身份、分页条数/字节/
EOF/溢出边界、六类资源预算、无效索引、过期查询与选择、取消、关闭、shutdown，
以及页在 task/archive/engine 释放后的字符串所有权。真实 ZIP 选中第二个同名文件、
隐式目录解压和路径边界均由实际 backend 验证。
Qt 原有 300k 测试还检查 UI heartbeat、快速更换查询、追加合并、取消及关闭恢复。

### ABI 与独立运行

```powershell
cmake --install out/build/f2-core --prefix out/install/f2-c-api
./scripts/Invoke-Msvc.ps1 dumpbin /exports out/build/f2-core/ananarchiver_c_api.dll
./scripts/Invoke-Msvc.ps1 dumpbin /dependents out/build/f2-core/ananarchiver_c_api.dll
```

- 安装成功，包含 C header、DLL/import library、来源记录和 Unicode license。
- Windows 只导出 **27 个 `aa_*` C 函数**；原有 18 个保留，没有导出 STL/backend 类。
  公共头与 macOS export list 的 27 个名字一致；这不等于 macOS 原生链接通过。
- DLL 只依赖 Windows / VC runtime，不依赖 Qt、Flutter 或 Dart。新的 Core 配置不搜索
  GUI SDK；本机现有 Qt kit 没有被卸载，但没有参与这个 Core/C ABI 构建或运行。
- 从 `git show HEAD:include/ananarchiver/c_api/ananarchiver.h` 和原始
  `tests/c_api/c_consumer.c` 复制 **F1 原版头文件与消费者**到 ignored
  `out/abi-v1-compat`，以 C11 编译，链接本次安装的 import library，并在本次 DLL 旁
  成功运行：`C ABI consumer passed`。新 C consumer 另外断言 F1 结构大小未改变。

## 100k / 1M metadata 实测

```powershell
python -X utf8 scripts/measure-metadata.py `
  out/build/f2-core/tests/ananarchiver_metadata_tests.exe
```

每个规模使用独立进程，在内存生成同一目录下具有唯一编号及 `CAFÉ` 名称的文件
metadata。验证百万行名称过滤/大小排序、原始编号、完整分页遍历、全选原生解析和
精确名称搜索。无文件内容，未创建或提交大型 fixture。页上限刻意选 64 KiB / 512 行。
测量 helper 通过 Windows `GetProcessMemoryInfo` 读取退出进程仍保留的峰值计数。

| metadata 条目 | 100,000 | 1,000,000 |
| --- | ---: | ---: |
| 生成耗时 | 7 ms | 75 ms |
| 索引构建 | 38 ms | 396 ms |
| 过滤 + 排序查询 | 9 ms | 123 ms |
| 遍历所有有界页 | 14 ms | 144 ms |
| 原生全选解析 + 精确搜索 | 4 ms | 42 ms |
| 保守索引记账 | 34,171,712 B | 347,415,920 B |
| 进程 peak working set | 36,462,592 B（34.8 MiB） | 300,634,112 B（286.7 MiB） |
| 进程 peak commit | 33,480,704 B | 307,490,816 B |
| 实际页数 | 318 | 3,202 |
| 最大实际页传输 allowance | 65,523 B | 65,524 B |

所有页都验证不超过 512 行/65,536 B；完整 query 和 selection 保持在 C++，没有通过
ABI/Dart 复制一百万行。这是单次本机合成 metadata 场景，不是压缩吞吐量、真实
百万文件解压、所有目录形状或 Flutter 帧率的验收。曾在并行构建时观察到更高延迟；
表中使用整套构建/测试完成后的独立进程运行，未据此承诺固定 SLA。

## 限制与下一阶段

- 本次仅 Windows x64 MSVC Release；Linux/macOS、GCC/Clang、ASan/UBSan/TSan
  以及 ELF/Mach-O 原生装载行为尚未验证。
- 默认是每个 engine 一个当前 archive/list query；多个视图使用多个 engine，各自预算。
  索引预算是保守 native allocation accounting，非全进程硬 RSS 限额；旧/新任务及查询
  取消期间可能重叠。详细上限和最终 off-UI engine destruction 规则见开发说明。
- 归档句柄是 metadata generation，不是外部源文件快照；解压继续沿用重新打开源路径
  的既有行为。外部文件改变后必须重新打开。Qt 最终模型销毁仍 join 其受管理 worker。
- F3 尚未实施：需要 Flutter app / Dart binding、isolate 任务与有界缓存、Home、
  打开、目录树、当前目录虚拟列表、导航、搜索排序及选择的真实端到端验收。
  F4–F6、各平台发布和前端性能对比继续待办；旧 Qt 通过状态不替代 Flutter 验收。

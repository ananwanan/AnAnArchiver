# 2026-11-06 F3 Flutter 归档浏览验证

## 状态与范围

在 `main` 的 `dd03970be37a9b705982807b6a315a59e085a5a2` 上实施 F3；构建时包含未提交
修改。已实现 `frontend/flutter` 桌面 app、独立纯 Dart binding package、持久化 worker
isolate、Home、系统文件选择、真实内容识别打开、归档目录树、当前目录虚拟列表、
根/上级/双击/Enter 导航、Unicode 搜索排序、多选与原生当前查询全选，以及 zh-CN/en。

Windows x64 Flutter Release 已构建并实际运行；Profile 单独用于下面的 renderer 数据。
Linux x86_64、macOS arm64/x86_64 的 runner 和集成入口已创建，但没有原生构建/运行
证据，不能宣称全平台完成。F4 归档操作、F5 完整桌面体验、F6 发布验收继续待办。
未展示压缩、解压、拖出等未接入功能的成功结果；Qt 代码只作回归基线。

## 工具链、依赖与平台检查

- Flutter **3.47.9 stable**，framework revision
  `c197ca5199e9ee5661e865596c9c07294dfab51c`；通过官方仓库
  `git ls-remote https://github.com/flutter/flutter.git refs/heads/stable` 核对与本机相同。
  `.metadata` 记录创建时的实际 SDK；不是长期冻结 SDK 的要求。
- Engine `89c39edfdfae2949f6806e4a315c0c08522238b7`，content hash
  `9e025265bef30b838c0c1b68cad19decf97048f2`；配套 Dart **3.13.7**。
- 本轮直接稳定依赖：file_selector **1.1.0**（Windows **0.9.3+6**、Linux
  **0.9.4+1**、macOS **0.9.5+2**）、flutter_svg **2.3.2**、ffi **2.2.0**、
  intl **0.20.3**；binding 开发工具 ffigen **23.0.0**、test **1.33.0**、crypto **3.0.8**。
  两个 `pubspec.lock` 已记录解析。最终 `pub upgrade` 更新了可升级的传递依赖；
  direct/dev dependencies 均 up-to-date。SDK/插件仍约束 cross_file、
  material_color_utilities、test_api、xml 的较新不兼容版本，没有 overrides 或静默降级。
- Windows 11，宿主内核 **10.0.26300.9818**；MSVC **19.51.36260.0**、
  CMake **4.4.0**、Ninja、Python **3.14.7**。Flutter Windows runner 使用其生成的
  CMake/Visual Studio 工具链；C ABI 单独经现有 Ninja Release pipeline 构建。
- native backend 重放 F1/F2 已校验的解析时间 **2026-10-28T15:46:45.521277+00:00**，
  libarchive **3.8.10** / `0c48412be2d94afe1859a807ea7896a18d8900bd`，上游
  `https://github.com/libarchive/libarchive`。本轮没有重新解析 backend 最新发布版，
  也没有改动子模块/来源校验或回退系统库。

[官方当前部署表](https://docs.flutter.dev/reference/supported-platforms) 仍列出 Windows
10/11 x64、Linux x64、macOS x64/arm64，macOS 为 Monterey 12 起。核对当前 SDK 源码：
`flutter_tools/lib/src/build_system/targets/macos.dart` 仍处理 `x86_64 arm64`，
`src/macos/build_macos.dart` 与 artifacts 保留 Intel host/target 路径。
[Intel Mac 弃用追踪](https://github.com/flutter/flutter/issues/188328) 仍开放，包含未来
停止 Intel development/universal app 的阶段。runner 显式保留 `arm64 x86_64`；
这些检查是 SDK/文档支持证据，不能代替 Intel Mac 实际编译/运行。没有缩减平台范围
或固定旧 SDK。后续更新必须再次检查。

## 实现与资源边界

- 生成绑定读取公共头，记录 SHA-256
  `be33cdc28ecd5980b722571ed55d9113b5440799026e901662f67615180054ed`；ffigen
  使用 Dart generator API，opaque engine 的无定义诊断符合 C ABI 设计。
  ABI v1/AA_FEATURE_BROWSER 协商、生成导出追踪、Dart size 与 C11 layout/offset
  断言均已验证，未改变 ABI 版本、结构或导出。
- UI isolate 不持有指针、不装载 native library、不调用 FFI。持久 worker 负责
  engine、请求准备、任务、50 ms poll、结果复制/释放、cancel/close 及 destroy/join。
  单个 native active/retained task；前端待答 32、worker 队列 16、列表请求 1、folder
  请求 2；重复视图变更/选择合并，取消时清除旧队列。task 在所有完成路径释放。
- 每页 128 行/1 MiB，列表最多 8 页/1,024 行；目录树独立最多 8 个可替换窗口，提供
  翻页和树视口上级/根入口。长名导致页不足 128 行时按真实结束位置继续；单次填充
  最多 8 页，避免为填满预取量而循环淘汰重取。完整索引/查询/百万选择始终在 C++。
- 归档/目录/搜索/排序变化清空选择，controller generation、native archive/query 及
  binding request epoch 拒绝过期状态。目录树展开不替换右侧查询/选择。原始重复文件
  ordinals 保留；目录使用独立 identity 和 AA_NO_ENTRY_INDEX，不能伪造 entry index。
- runner 使用同一原生 CMake pipeline；包内含 C ABI、native provenance、解析记录、
  Flutter SDK/engine/Dart、lockfile、binding 来源和 native 许可。帮助窗口后台读取
  实际记录；保留原品牌素材及 MIT 图标许可。没有 Qt runtime。

## 构建与测试

命令在仓库根目录执行，Flutter 部分切换到 `frontend/flutter`。本机 prefix 路径是
本次显式依赖重放参数，不是生产代码固定路径：

```powershell
$env:ANANARCHIVER_DEPENDENCY_PREFIX = (Resolve-Path out/build/f1-release/dependencies/install).Path
Set-Location frontend/flutter
../../scripts/Invoke-Msvc.ps1 flutter build windows --release
flutter gen-l10n
dart format --output=none --set-exit-if-changed lib test integration_test test_driver packages/litearc_binding/lib packages/litearc_binding/test packages/litearc_binding/tool
flutter analyze
flutter test
```

Windows Release 构建成功；最终 MSBuild **0 warning / 0 error**。首次 SDK 记录接线发现
runner 目录作用域未拿到 Flutter 子目录的 SDK 变量，已改为读取同一 generated config，
并直接记录选中 SDK 的 cache/version JSON，不在 Flutter 构建内部递归调用 Flutter。
这是工具/来源接线修复，没有换用其他 SDK。

Flutter/controller/widget **12/12 通过**，binding Dart **8/8 通过**（均启用真实 fixture，
没有跳过）。`flutter analyze`、binding `dart analyze`、Dart format、原生 clang-format、
Python 语法与 diff whitespace 检查通过。

覆盖 Home 不扫描/不造数据、platform picker adapter、双击/Enter/上级导航、当前目录
搜索排序、多选/Shift 范围/全选、选择清空、stale pages、dispose、独立 tree windows、
缓存边界、7 行短页、取消/关闭/幂等退出、结构化 missing-file 错误和 DLL 装载失败。
widget 覆盖 100/125/150/200% **字体缩放**；这不是各个 OS DPI/屏幕阅读器实机验收。

真实 binding fixtures：

```powershell
python -X utf8 tool/generate_fixture.py ../../out/f3-fixtures/browse.xxx
python -X utf8 tool/generate_fixture.py ../../out/f3-fixtures/long.zip --entries 0 --long-names
$env:LITEARC_TEST_LIBRARY = (Resolve-Path ../../out/build/f2-core/ananarchiver_c_api.dll).Path
$env:LITEARC_TEST_ARCHIVE = (Resolve-Path ../../out/f3-fixtures/browse.xxx).Path
$env:LITEARC_TEST_LONG_ARCHIVE = (Resolve-Path ../../out/f3-fixtures/long.zip).Path
Push-Location packages/litearc_binding
dart test
dart analyze
Pop-Location
```

`.xxx` 实际内容为 ZIP，验证内容识别；包含中文/日文/韩文/emoji/CAFÉ、隐式父目录、
显式空目录和两个同名但不同内容/ordinal 的文件。长名称 fixture 包含 64 个每名称
4,000 个中文字符的 entry，验证 1 MiB 字节上限产生短页、连续 offset 与原始编号。
生成文件全部在 ignored out，不提交大型输入。

原生/Qt 回归：

```powershell
./scripts/Invoke-Msvc.ps1 cmake --build out/build/f2-core --parallel 4
ctest --test-dir out/build/f2-core --output-on-failure --parallel 4
./scripts/Invoke-Msvc.ps1 cmake --build out/build/f2-qt --parallel 4
ctest --test-dir out/build/f2-qt --output-on-failure -E '^dependency_tools$' --parallel 4
```

完整 Core run **10/10 suites 通过**（含 dependency_tools，约 229 s）；最终补充 C11
browser 布局断言后 native **9/9 通过**，约 2.93 s。Qt/native 重建后 **21/21 通过**，
约 15.53 s。包含 Core、archive、extraction/security、compression、Application、
metadata、C consumer、C ABI、browser C ABI 以及 Qt browser/drag/extraction/adapters/
shell/desktop/flow/rendering/compression/packaging；没有继续开发 Qt 产品 UI。

## Release 实际运行

使用 computer-use 启动生成的 Release LiteArc.exe，观察无参数 Home；通过真实 Windows
文件对话框选择 `out/f3-fixtures/浏览.xxx`，确认显示根目录的 8 个真实项目、3 个文件夹、
CAFÉ/日文/韩文、两个大小不同的 duplicate.txt，以及左侧独立目录树。正常关闭窗口后
进程退出。最终 Release 又通过系统对话框打开 1M ZIP，双击进入 many/，实际显示
1,000,000 个项目及按需列表；检查其来源窗口和中文原生标题。此处是真实
runner/plugin/C ABI/backend 链路，不是 mock screenshot。

后续 Release 重建沿用同一流程，检查帮助窗口实际来源记录、主题/语言与 native DLL
依赖。dumpbin 确认 C ABI 只依赖 Windows/VC runtime；runner 另依赖 Flutter engine 和
file_selector 插件，不依赖 Qt。原生窗口标题通过 adapter 同步 zh-CN/en。无 clean-machine 安装/CRT、签名、系统关联或系统拖放的完成声明。

## 100k / 1M 真实 ZIP 与 Profile 测量

生成实际 ZIP：根目录含上述小 fixture 项目，`many/` 下分别有 100,000/1,000,000 个
零内容文件。包含真实 ZIP headers、libarchive 内容识别/枚举、C++ 索引和 Dart FFI；
不是 payload 解压/压缩吞吐量，也不是百万独立磁盘文件。ZIP 大小分别 11,800,883 B /
118,000,883 B。controller/widget 的 synthetic transport 另行验证逻辑边界，不能当作
native IO 性能证据。

```powershell
python -X utf8 tool/generate_fixture.py ../../out/f3-fixtures/100k.zip --entries 100000
python -X utf8 tool/generate_fixture.py ../../out/f3-fixtures/1m.zip --entries 1000000
$fixture = (Resolve-Path ../../out/f3-fixtures/100k.zip).Path
../../scripts/Invoke-Msvc.ps1 flutter drive --device-id=windows --profile `
  --driver=test_driver/integration_test.dart --target=integration_test/browser_test.dart `
  --dart-define=INTEGRATION_TEST_SHOULD_REPORT_RESULTS_TO_NATIVE=false `
  "--dart-define=FIXTURE=$fixture"
```

分别运行两个规模。Dart driver 捕获实际测试结果和 reportData，禁用不适用 Windows 的
native instrumentation reporting。每个规模覆盖打开/首个可用列表、Unicode/空目录、
跳转中间/尾页/返回、真实滚动、native 全选、独立 tree、排序搜索、失效取消、Home 和
shutdown。只采样有界窗口，不遍历/复制百万行到 Dart。以下为本机一次 Profile 运行，
C ABI 为 Release，GPU/引擎/VM/测试框架/native 都包含在进程内存中：

| 指标 | 100k | 1M |
| --- | ---: | ---: |
| 打开到首个可用根视图 | 805 ms | 2,873 ms |
| 采样目录窗口请求总时长 | 464 ms | 463 ms |
| ProcessInfo.maxRss | 307,650,560 B（293.4 MiB） | 556,470,272 B（530.7 MiB） |
| 滚动采样帧 | 60 | 60 |
| frame totalSpan P50 | 5,498 µs | 6,391 µs |
| frame totalSpan P95 | 17,567 µs | 12,815 µs |
| 关闭归档后最终 shutdown | 2 ms | 0 ms（计时分辨率） |
| 列表缓存断言 | ≤8 页/1,024 行 | ≤8 页/1,024 行 |

首屏含进入可用状态的测试等待；分页包含 50 ms 轮询及 isolate 往返。frame totalSpan
是引擎 FrameTiming 的整帧时间跨度，不等于纯 CPU build/raster 或固定 FPS/SLA。
shutdown 是已关闭归档后的最后销毁，不代表最大负载下所有取消耗时。
早期独立运行观察到不同峰值/延迟（约 249/493 MiB），说明这是有环境波动的单次测量。
没有宣称 Flutter 比 Qt 更快/更小，也未完成同参数 Release 前端对照。

Profile 启动日志出现 `libpng iCCP: known incorrect sRGB profile` 诊断；已检查
本项目品牌 PNG/ICO 均无 iCCP chunk，未静默改写原图或加入 Flutter/Qt 运行时补丁。
编译/分析无项目 warning/error；上述运行时诊断来源仍需在独立环境定位，不能将其称作
已修复或将性能测量冒充完全无诊断运行。

## 限制与下一阶段

- 仅 Windows x64 原生验证；Linux/macOS、Intel Mac 实机、sanitizers、各 DPI/辅助
  技术、clean-machine runtime 和正式发布尚未验收。
- 当前缓存是有界窗口，不是完整 Dart 索引。极端长名/超高视口下预取到缓存预算即停，
  滚动继续取页；不承诺把超出字节预算的整个视口常驻。最大树节点/深度等仍由 F2
  native 资源策略限制。百万级宽/深树的全部形状没有在此单目录数据中覆盖。
- 主题/语言仅会话内；旧 QSettings 不读写，一次性迁移策略见开发说明，F5 实现。
  当前任务状态是浏览任务，完整任务中心和历史不在 F3 完成范围。
- F4 接入既有 native 解压全部/选中、密码、覆盖交互、实际进度/取消和 capability
  创建流程；不重写 backend、安全策略或解析器。
- CI 自动触发策略、第三方子模块、保存的 Qt 基线分支均未修改。Flutter CI 与各平台
  Release 打包需后续显式接入；本地通过不等于 CI 已支持。

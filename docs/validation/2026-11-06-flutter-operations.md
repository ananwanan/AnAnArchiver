# 2026-11-06 F4 Flutter 解压与创建验证

## 范围与实现

起点为 `main` 的 `7f0a455`（F3）。F4 实现系统目标目录选择、全归档/原生选中解压、
密码需要/错误/重试/取消、五种覆盖策略与本任务 Apply to all、实际任务进度/取消，
以及按实际 native capabilities 选择输入和输出、创建并重新打开归档。
Windows x64 Release 已构建并实际操作。Linux/macOS 尚未原生验证，macOS x86_64
继续保留；F5/F6、完整任务中心、设置迁移、系统拖放、完整性测试、编辑和正式发布未完成。

所有归档 IO、目录扫描、安全检查、选择展开、压缩和输出事务均使用原有 C++
Application/Core/libarchive。没有改动 ABI v1 的头、结构、导出或 feature bits，
没有 Dart backend、Qt 产品 UI、系统 libarchive 回退或运行时日志压制补丁。
原生变更仅为 C ABI 回归和显式生成测试 fixture 的入口。

F3 的串行浏览队列保留，另有一条异步操作通道共享同一 engine。每条通道轮询各自
task ID；取消和 conflict-ID 回答在 inbox 直接处理。最多两个 active tasks、十六个
retained tasks，完成即 release，snapshot/result/error 精确释放。普通待答请求上限 28，
为控制输入预留 4 个，总上限 32；浏览队列 16，归档操作不排队。
每通道最多一个未确认进度消息和一个可替换的最新状态，终态单独发送；50 ms cadence
并不允许 UI 阻塞时 mailbox 无限增长。所有 FFI、输入准备、结果复制/释放和最终
destroy/join 均在持久 worker，不通过 kill isolate 清理。

选中解压只传 archive/query/selection handle，隐式目录、目录边界、空目录及重复文件
原始 ordinal 由 native 保留；未完成/过期/空选择不能变成全部解压。全部解压显式使用
`aa_archive_extract(extract_all=1)`。目录/搜索/排序改变不取消已提交操作；密码重试若
选中视图已变则明确失败。切换/关闭归档取消操作并等待回滚后切换；dispose/退出结束
密码等待、取消冲突任务，后台完成销毁。强制终止进程不承诺清理。

密码输入和可控 UTF-8/FFI buffer 清空、擦写并及时释放；密码不进入设置、日志、来源
记录或错误详情。不可变 Dart 字符串、VM/系统输入副本无法承诺绝对擦除。
创建 UI 仅显示 native 返回的格式/方法、等级、权限和时间戳选项，没有密码创建、AES、
多线程或 solid 控件。4,096 路径、每路径 64 KiB、累计 4 MiB 限制均明确失败，不截断。
任务面板显示实际字段；未知总量为不确定进度，无伪暂停。取消与清理错误按实际终态显示，
原始诊断仅作为纯文本详情。新文案来自 zh-CN/en ARB。

## 工具链与依赖

- Windows 11 **10.0.26300.9818**，MSVC **19.51.36260.0**、Visual Studio
  **18.11.3**、Windows SDK **10.0.28000.0**、CMake **4.4.0**、Ninja **1.13.2**、
  Python **3.14.7**。验证过程中包含本轮未提交修改，最终来源身份以 bundle 实际记录为准。
- Flutter **3.47.9 stable** / framework
  `c197ca5199e9ee5661e865596c9c07294dfab51c`，两次通过官方仓库
  `git ls-remote https://github.com/flutter/flutter.git refs/heads/stable` 核对一致。
- Engine `89c39edfdfae2949f6806e4a315c0c08522238b7`，content hash
  `9e025265bef30b838c0c1b68cad19decf97048f2`；配套 Dart **3.13.7**。
- `flutter pub upgrade/outdated`：直接依赖和可升级稳定依赖已最新兼容；file_selector
  **1.1.0**、ffi **2.2.0**、flutter_svg **2.3.2**、intl **0.20.3**。cross_file、
  material_color_utilities、test_api、xml 的更新仍受 SDK/插件约束，没有 overrides 或降级。
- binding 的 `dart pub upgrade` 更新 code_assets **2.1.0 → 2.2.0**、
  native_toolchain_c **0.19.5 → 0.19.6**，lockfile 单独提交为 `98ff06e`；应用 lockfile
  无变化，继续提交保留。未引入新的产品运行时插件。
- 本轮 native backend **重放**已校验的解析时间 **2026-10-28T15:46:45.521277+00:00**，
  libarchive **3.8.10** / `0c48412be2d94afe1859a807ea7896a18d8900bd`，来源
  `https://github.com/libarchive/libarchive`。没有声称重新解析最新 native 版本。
  包内保存实际 Flutter SDK/engine/Dart、lockfile、binding header 来源、native provenance
  和许可；Core-only 构建不搜索 Qt/Flutter。

[官方部署表](https://docs.flutter.dev/reference/supported-platforms) 本轮仍列出 Windows
x64、Linux x64、macOS x64/arm64；SDK `build_system/targets/macos.dart` 仍含
`x86_64 arm64`。仓库 runner 继续保留这两个架构；
[Intel Mac 弃用追踪](https://github.com/flutter/flutter/issues/188328) 仍需后续持续检查。
这不是 Intel Mac 原生构建/运行通过的证据。系统文件/目录/保存对话框继续使用
[官方 file_selector](https://pub.dev/packages/file_selector)；macOS entitlement 改为
user-selected read-write，尚无 Mac 原生权限验证。

## 构建与回归

原生命令在仓库根目录执行；Flutter 命令在 `frontend/flutter` 执行：

```powershell
./scripts/Invoke-Msvc.ps1 cmake --build out/build/f2-core --parallel 4
ctest --test-dir out/build/f2-core --output-on-failure -E '^dependency_tools$' --parallel 4
ctest --test-dir out/build/f2-core --output-on-failure -R '^dependency_tools$'
./scripts/Invoke-Msvc.ps1 cmake --build out/build/f2-qt --parallel 4
ctest --test-dir out/build/f2-qt --output-on-failure -E '^dependency_tools$' --parallel 4

# frontend/flutter，显式重放既有已验证 native prefix
$env:ANANARCHIVER_DEPENDENCY_PREFIX = (Resolve-Path ../../out/build/f1-release/dependencies/install).Path
../../scripts/Invoke-Msvc.ps1 flutter build windows --release
flutter gen-l10n
dart format --output=none --set-exit-if-changed lib test integration_test test_driver packages/litearc_binding/lib packages/litearc_binding/test packages/litearc_binding/tool
flutter analyze
flutter test
```

- Windows x64 Flutter Release 构建成功；native/MSVC 构建未产生本次项目 warning/error。
- Core/archive/extraction/security/compression/Application/metadata/C consumer/C ABI/browser
  **9/9 suites 通过**，约 **4.28 s**。dependency_tools 独立 **1/1 通过**，约 **249.36 s**。
- Qt/native 回归 **21/21 suites 通过**，约 **20.65 s**。包含旧浏览、拖放、解压、创建、
  adapter、shell、desktop flow、渲染及打包 metadata；没有开发 Qt 产品 UI。
- Flutter/controller/widget **21/21 通过**；binding 真正启用 fixture 后 **24/24 通过**，
  没有跳过真实测试。`flutter analyze`、binding `dart analyze`、Dart format、
  `clang-format --dry-run --Werror tests/c_api/c_api_tests.cpp` 和 `git diff --check` 通过。

生成和运行真实 binding fixtures：

```powershell
# 仓库根目录，显式生成小型 AES ZIP、覆盖 TAR、恶意路径/链接 TAR
out/build/f2-core/tests/ananarchiver_c_api_tests.exe --generate-flutter-fixtures out/f4-fixtures
python -X utf8 frontend/flutter/tool/generate_fixture.py out/f4-fixtures/browse.xxx
python -X utf8 frontend/flutter/tool/generate_fixture.py out/f3-fixtures/long.zip --entries 0 --long-names
# frontend/flutter/packages/litearc_binding
$env:LITEARC_TEST_LIBRARY = (Resolve-Path ../../../../out/build/f2-core/ananarchiver_c_api.dll).Path
$env:LITEARC_TEST_ARCHIVE = (Resolve-Path ../../../../out/f4-fixtures/browse.xxx).Path
$env:LITEARC_TEST_LONG_ARCHIVE = (Resolve-Path ../../../../out/f3-fixtures/long.zip).Path
$env:LITEARC_TEST_OPERATIONS = (Resolve-Path ../../../../out/f4-fixtures).Path
dart test
dart analyze
```

新增原生测试遍历每一个返回的 writer format/method pair，创建/解压验证 Unicode payload，
并验证创建失败保留原文件。binding 测试覆盖全部解压不受过滤影响、排序后选中第二个
重复文件、隐式目录/空目录、过期与零 selection、密码需要/错误/重试、路径/链接逃逸回滚、
全部直接覆盖策略及时间戳比较、四种 Ask 回答的 Apply to all 与跨任务不继承。
另覆盖冲突中同时查询/快速替换查询、旧/重复 conflict ID、取消/关闭/切换/退出、累计
24 次任务释放、所有实际创建格式的重新打开与内容校验、创建失败保留原文件及输入预算。
核心安全/资源与损坏归档测试继续保留。

Controller/widget 还覆盖百万 synthetic 选择只提交 native handle、未完成选择拒绝、
密码取消/dispose、输入框同步清空、取消早于快照时仍保留 cleanup 错误、未知总量、
原样回答 task/conflict ID、capability 控件和平台路径、F3 Home/缓存/导航/搜索/选择及
100/125/150/200% 字体缩放。synthetic transport 不作为 native IO 或真实百万条目证据。

## Windows Release 实际操作与桌面集成

通过 computer-use 启动生产 Release `LiteArc.exe`，确认无参数 Home。使用真实 Windows
文件对话框打开 `out/f4-fixtures/browse.xxx`（内容是 ZIP），使用真实目录对话框选择
新目标，Ask 遇到两个 duplicate.txt 时勾选本任务 Apply to all 并覆盖，最终成功。
独立检查目标磁盘：第二个 duplicate.txt 内容为 `second`、Unicode 子文件内容正确、
显式空目录存在、many/ 有 300 个真实文件。观察实际条目/字节/速度/耗时和最终状态。

随后经创建对话框的真实系统文件/保存对话框选择生成的输入和新 ZIP 输出，使用 native
ZIP/Deflate/level 6 创建成功。通过同一 Release UI 重新打开，显示实际单文件列表；
另用 Python zipfile 独立比较输出 payload 与输入一致。正常点击关闭后进程退出。
UI helper 最初两次无法激活窗口；用户切到前台后恢复，未把此前其他窗口的图像当作证据。

独立 **Profile Flutter + Release C ABI** 集成测试：

```powershell
# frontend/flutter
$fixtures = (Resolve-Path ../../out/f4-fixtures).Path
../../scripts/Invoke-Msvc.ps1 flutter drive --device-id=windows --profile `
  --driver=test_driver/integration_test.dart --target=integration_test/operations_test.dart `
  --dart-define=INTEGRATION_TEST_SHOULD_REPORT_RESULTS_TO_NATIVE=false `
  "--dart-define=OPERATIONS_FIXTURES=$fixtures"
```

真实 renderer/controller/worker/C ABI/backend 流程通过，覆盖全选/重复文件选中解压、
覆盖与 Apply to all、密码需要/错误/重试/取消、创建/重开/解压、等待冲突时 shutdown。
此测试显式 fixture platform adapter，不能称为系统 picker 验收；系统 picker 来自上述
生产 Release 操作。初次测试发现 IntegrationTest 默认不注册文本输入，导致
tester.enterText 未写入密码框；显式注册测试输入通道后通过，未改动产品键盘实现。
最终一次 Profile 报告等待冲突时 shutdown **41 ms**、ProcessInfo.maxRss **236,625,920 B**。
这些不是 Release 性能或全平台帧率数据。

## 生成数据与性能边界

```powershell
# 仓库根目录（1M ZIP 可重用 F3 生成文件）
python -X utf8 frontend/flutter/tool/generate_fixture.py out/f3-fixtures/1m.zip --entries 1000000
# frontend/flutter/packages/litearc_binding，独立 CLI，结束删除自己新建的临时根
dart run tool/measure_operations.dart ../../../../out/build/f2-core/ananarchiver_c_api.dll ../../../../out/f3-fixtures/1m.zip 1024 5000
```

Dart **CLI/JIT + Release C ABI** 独立进程生成 1 GiB payload（复用 1 MiB buffer，
不把完整内容加载到内存）、5,000 个小文件，实际创建/解压并流式 SHA-256 比较大文件，
比较小文件数量及采样内容。ZIP 使用 native Store，不能外推为 Deflate/7z 吞吐量。
下表取增加分阶段内存记录后的单次运行；此前并行构建时的结果有可见波动。

| 指标 | 实测 |
| --- | ---: |
| 大 payload | 1,073,741,824 B |
| Store ZIP 大小 | 1,073,741,976 B |
| 大文件创建 / 解压 | 1,498 / 2,568 ms |
| 大文件内容 | 流式 SHA-256 一致 |
| 大文件往返后进程累计 maxRss | 288,821,248 B（约 275.4 MiB） |
| 5,000 小文件创建 / 解压 | 3,880 / 13,900 ms |
| 写入中取消压缩 / 解压到终态 | 64 / 62 ms |
| 真实 1M ZIP 打开 / native 全选 | 2,960 / 62 ms |
| 1M 选择返回 Dart rows | 0，仅 selection handle |
| 包含百万索引后的进程 maxRss | 548,843,520 B（约 523.4 MiB） |
| 关闭归档后最终 shutdown | 72 ms |

补充以 64 MiB / 100 小文件运行同一生成器，验证真实百万选择已提交
`aa_browser_extract` 后取消：**61 ms** 到终态，未留下不完整文件；这不是百万磁盘文件
完整解压的性能验收。大文件取消压缩验证原输出保留，取消解压验证部分文件回滚。
1M ZIP 的 payload 为零长文件，确实经过 libarchive/metadata/FFI，但不是百万独立输入
文件压缩吞吐量。ProcessInfo.maxRss 是整个 CLI/native/VM/校验的累计峰值，非 native
allocator 的单项上限，亦非 Flutter AOT/renderer 内存。未承诺固定 SLA 或优于 Qt。
所有大型输入和输出均运行时生成/清理，未提交大型 fixture。

## libpng 与未验证范围

复查当前品牌 PNG 和 Windows ICO 内嵌 PNG 的 chunk，均无 iCCP；没有改写品牌图，
没有压制日志或加入未经证实的 Flutter/native runtime 补丁。F4 Profile 操作流程本次
未重现 F3 的 `libpng iCCP: known incorrect sRGB profile` 诊断；来源仍未确认，
保留 F3 真实记录，不能声称已修复。

本轮仅 Windows x64；Linux/macOS/Intel Mac、其他编译器、sanitizers、clean-machine
运行时、系统 DPI/屏幕阅读器、签名/安装升级与完整 F6 性能对照仍未原生验收。
密码 fixture 是 AES ZIP payload；所有格式/加密变体和 header-encrypted archives
没有逐一验收。归档源是重开路径而非外部内容快照，外部修改后必须重新打开。
CI 自动触发策略、第三方子模块和保留的 Qt 基线分支均未修改。
下一阶段为 F5：设置/最近记录迁移、完整任务中心、快捷键与真实系统拖入拖出/打开事件，
随后继续各原生平台和 F6 发布验收。

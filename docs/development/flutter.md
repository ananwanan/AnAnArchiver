# Flutter 归档浏览与操作（F3/F4）

`frontend/flutter` 是可运行的桌面 app；`packages/litearc_binding` 是不依赖 Flutter
的 Dart package。Windows x64 已构建/运行；Linux/macOS runner 已接线，仍需对应
宿主原生验证。Core/Application/C ABI 仍可独立构建，不搜索 Flutter 或 Qt SDK。

## SDK 与依赖

使用官方当前 stable Flutter 和其配套 Dart，不固定历史 SDK。先确认
`flutter --version --machine`、`flutter doctor -v`；更新执行 `flutter upgrade`、
`flutter pub upgrade`，binding package 同样执行 `dart pub upgrade`。提交两个
`pubspec.lock`，保留一次解析；不添加 dependency_overrides 绕过 SDK/插件约束。
本轮实际版本、上游检查、Intel Mac 状态见 [验证记录](../validation/2026-11-06-flutter-browsing.md)。

原生窗口标题通过各 runner 的 `litearc/window` adapter 通道随 zh-CN/en 更新。
系统文件选择使用 Flutter 官方 `file_selector`，不按扩展名过滤归档。平台 adapter
只返回路径；内容识别、索引、查询和选择全部经过 Application/C ABI。
SVG 使用 `flutter_svg`，复用原有 MIT 图标和原始品牌图；平台图标从同一原图导出。
`assets/branding/PROVENANCE.md` 保存原始素材说明，Flutter 的实际资源路径为当前目录。

## 构建

先按仓库原有流程初始化/获取 native 依赖：

```sh
cmake -P scripts/init-dependencies.cmake
cd frontend/flutter
flutter pub get
flutter build windows --release
```

Windows 在 MSVC x64 Developer PowerShell 执行；非 Developer Shell 使用仓库包装：

```powershell
# 在 frontend/flutter 执行。使用长选项，避免 PowerShell 将 -d 解释为 Debug。
../../scripts/Invoke-Msvc.ps1 flutter build windows --release
```

已有经校验原生依赖 prefix 可通过 `ANANARCHIVER_DEPENDENCY_PREFIX` 显式重放。
不设置时 `tool/build_native.py` 使用现有 superbuild；设置时使用原有 provenance 校验的
standalone pipeline。两者都强制 `ANANARCHIVER_BUILD_QT=OFF`，不另行解析 codec。
不要把重放旧解析称为本轮重新获取最新 backend。失配直接失败，不回退系统库。

Windows/Linux runner 的 `tool/native.cmake` 调用此脚本，并安装 C ABI 到包内相对位置；
macOS embed 阶段同样调用此脚本，传递 `ARCHS`。默认 native 为 Release，Flutter
Debug/Profile 也使用该 Release C ABI；因此 Profile 数据需要标明组合。
macOS 保留 `arm64 x86_64`，最低系统随当前 SDK；不要静默移除 Intel 目标。
跨架构 prefix 必须匹配相同工具链和架构。Linux 使用 `flutter build linux --release`，
macOS 使用 `flutter build macos --release`；这两条尚未在本轮宿主验证。

Windows 可执行文件为 `build/windows/x64/runner/Release/LiteArc.exe`；无参数显示 Home，
可传一个显式归档路径。Linux/macOS 的产品和 bundle 名称同样为 LiteArc，保留
`com.ananwanan.AnAnArchiver` 应用身份。现有 `Package-*` 脚本仍仅适用于 Qt。
F3 runner bundle 不是已完成安装器、签名、clean-machine CRT 或跨平台发布验收。

包内 `litearc-build` 保存 native provenance/解析、Flutter SDK/engine/Dart、应用 lockfile、
binding header 来源与 native 许可。帮助按钮读取这些实际文件，后台 IO，不以当前子模块
HEAD 冒充构建 revision。Flutter 的许可证页显示框架和 package 许可证。

## Binding 与所有权

依赖图为 Widget → BrowserController → BrowserClient → persistent isolate →
C ABI → Application → Core/backend。UI isolate 从不创建指针、装载 DLL 或调用 native。
worker 协商 `AA_ABI_VERSION == 1` 与 `AA_FEATURE_BROWSER`，并检查任务容量。
无 Dart callback 进入 C++。最终 `aa_engine_destroy`、join、native 缓冲释放及输入准备都在
worker isolate；正常关闭等待 worker 完成后再允许窗口退出，不通过 kill 清理。
进度消息逐请求确认：每条通道最多一个未确认快照和一个可替换的最新状态，终态另行发送，
不让 UI 忙碌时在 isolate mailbox 积累无限快照；原生 snapshot 在复制 DTO 后即释放。

生成绑定：

```sh
cd frontend/flutter/packages/litearc_binding
dart pub get
dart run tool/generate.dart <absolute-path-to-libclang-library>
dart test
dart analyze
```

省略参数时使用 ffigen 默认 LLVM 搜索。生成器使用 ffigen 的 Dart API，不依赖旧 YAML。
`binding-source.json` 记录公共头 SHA-256；测试校验源码身份、全部导出及 64 位结构大小，
C11 consumer 对同一组 C 布局/关键 offsets 作编译期断言。更新绑定后必须跑真实 ABI
集成测试；hash/size 检查不能独自代替运行证据。

worker 保留一条串行浏览队列和一条独立归档操作通道，最多两个 native active tasks；
原生 retained 上限仍为 16，每个结果完成/失败/取消后立刻释放任务。
原生 cleaner 可能还在回收已释放任务，因此暂时的 task capacity/AA_BUSY 最多重试
100 次，每次等待 20 ms；真实输入/索引预算或内存错误仍返回失败。
等待期间继续处理取消/覆盖回答/退出。两条通道均以 50 ms cadence 轮询，快任务可立即完成。所有 snapshots、
errors、browser results 精确释放，先复制有界 DTO 再传给 UI。

binding 前端最多 32 个待答请求（普通请求占 28 个，为控制输入预留 4 个）、worker 浏览
队列最多 16 个，归档操作不积累队列；open/query/close/cancel 清除旧浏览队列、取消
当前浏览任务；重复选择合并为最后一次。Controller 只允许一个列表窗口
请求和两个 folder 请求，合并重复窗口/滚动目标。请求 epoch 与 archive/query/selection
代次共同拒绝旧结果。页面 dispose 会启动 transport 关闭；正常应用退出显式 await
shutdown。被强制终止进程不承诺执行 Dart 清理。

## 视图、缓存与选择

右侧 `ListView.builder` 按可见行构建，固定行高；滚动 listener 驱动窗口请求，build
不做 IO、native 查询或排序。行高随 100–200% 字体缩放调整。每页最多 128 行/1 MiB，最多 8 页，即 1,024 行/8 MiB
native transfer allowance；Dart 对象/字符串和传输中的副本另有开销，不能当作 RSS
硬上限。实际字节上限导致页不足 128 行时，从真实结束位置继续取页，不能跳行；单次填充最多 8 页，预取量不会迫使缓存反复淘汰重取。

左侧使用独立 `aa_browser_request_folders`；每节点只保留一个可替换页，提供上一页/
下一页，最多 8 个窗口/1,024 个 folder rows。折叠/展开不改变右侧 query/selection。
深树导致旧树窗口被淘汰时，树视口移到新窗口，并提供树上级和根入口；不缓存整棵树，
也不宣称只返回的一个窗口就是全部目录。右侧路径导航不受树缓存深度影响。

完整索引、查询顺序和全选解析留在 C++；Dart 只保存查询位置区间（最多 4,096 个），
全选是一段/原生 select_all。文件保留原始枚举编号；目录 identity 独立，index 为
`AA_NO_ENTRY_INDEX`（64 位 Dart 表现为 -1），从不拿目录 identity 当 entry index。
支持 Ctrl/Cmd 多选、Shift 连续选择、当前查询全选、双击/Enter 进入文件夹及返回上级。

搜索 debounce 180 ms，选择提交 debounce 80 ms。归档、目录、搜索、排序变化立即清空
可见选择/旧查询；C++ 执行 Unicode 搜索、排序、目录构建和大选择。错误 code 由 ARB
本地化为 zh-CN/en，原始诊断仅在详情显示为纯文本。未知总量显示实际状态/条目数，
不构造百分比。F4 已接入下面的压缩/解压；未实现的拖出等不提供成功占位。

## F4 解压、创建与交互

Widget → BrowserController / OperationController → BrowserClient → 同一 worker/engine
→ C ABI → 现有 Application/Core。浏览和归档任务分别拥有 task ID，轮询/释放不互相
覆盖。`resolveConflict` / `cancelOperation` 在 inbox 直接处理，不进入等待该回答的队列。
正常操作只保留两个当前任务及有界快照，不增加 native 容量。

- **全部解压**：系统目录选择后，显式 `aa_archive_extract(extract_all=1)`，不继承搜索、
  排序或选择。**选中解压**：仅允许完成且属于当前 generation 的 selection handle，
  调用 `aa_browser_extract`；不展开 native 百万选择，不在 Dart 重建原始 ordinals/目录。
  选择尚未完成、失效、空选择或无效视图均明确拒绝。选择目标/选项期间视图变化也拒绝。
- 复用 Core 的路径、绝对路径、链接、预算、输出事务和 rollback；本轮不增加解析器或
  Dart 文件扫描。默认 safe link policy。输出失败/取消显示实际结构化错误；cleanup failure
  的原始路径/诊断可在纯文本详情查看，不把取消请求本身当作清理完成。
- 密码需要/错误时，前一 native 任务已终止并清理，再提交相同操作及新密码；选中操作
  仍重新检查 archive/query/selection，不能在重试时退化为全部。密码取消完成该操作。
  密码不进入设置/日志/来源记录；输入框在回答、取消、关闭和 dispose 时同步清空，
  请求引用在提交后移除。worker 擦写自有 UTF-8 和 FFI buffer 后释放，native 沿用密码
  guard。Dart 不可变字符串、VM/isolate 传输和系统输入可能有副本，不能保证绝对擦除。
- 初始覆盖策略为 Ask/Overwrite/Skip/Rename/OverwriteIfNewer；每次回答同时携带实际
  task ID 和 conflict ID，拒绝过期/重复回答。Apply to all 只在该 native 任务内有效。
- 创建对话框从 `aa_compression_capabilities` 读取实际格式/方法、level 区间及权限/
  时间戳选项；使用系统多文件、目录和保存位置对话框。没有密码创建/AES/多线程/solid
  控件。所有输入扫描、读写、压缩和原子替换仍在 C++ worker；现存输出只在显式允许且
  成功创建后替换。输入上限为 4,096 路径、每路径 64 KiB UTF-8、累计 4 MiB；超限报错，
  不截断。macOS runner 改用 user-selected read-write entitlement，尚未原生验证。
- 任务面板显示实际状态、当前文件、扫描/写入/完成阶段、条目/字节、速度、耗时和
  已知总量；总量未知显示不确定进度，没有伪暂停。面板只显示当前/最近一次操作，完整
  任务中心和持久历史仍属 F5。

目录/搜索/排序变化允许已提交操作继续；若等待密码期间选中视图失效，重试明确失败。
切换/关闭归档会取消在途操作，native worker 完成回滚/释放后才切换归档。
页面 dispose/正常退出会先结束密码等待、取消所有 native 任务与冲突等待，在后台等待
操作终态并最终 destroy/join，之后窗口退出；不使用 isolate.kill。
系统强制终止进程不承诺运行清理。归档源仍是路径而非文件内容快照，外部改变后必须重开。

真实测试、工具链、Release/独立 Profile、IO/百万选择测量和平台限制见
[F4 验证记录](../validation/2026-11-06-flutter-operations.md)。

主题与语言当前仅会话内生效；F3 不读写旧 QSettings。F5 将在保留旧设置的前提下，
后台一次性迁移已知 theme/recent 字段、加迁移标记、不迁移密码、不在启动时 stat 最近路径。
该迁移尚未实现，F3 不将默认 system theme 声称为旧设置已迁移。

## 测试与测量

```powershell
# frontend/flutter
flutter gen-l10n
dart format --output=none --set-exit-if-changed lib test integration_test test_driver packages/litearc_binding/lib packages/litearc_binding/test packages/litearc_binding/tool
flutter analyze
flutter test
python -X utf8 tool/generate_fixture.py ../../out/f3-fixtures/browse.xxx
python -X utf8 tool/generate_fixture.py ../../out/f3-fixtures/long.zip --entries 0 --long-names
# binding package：设置真实 library/archive 后执行 dart test
$env:LITEARC_TEST_LIBRARY = (Resolve-Path ../../out/build/f2-core/ananarchiver_c_api.dll).Path
$env:LITEARC_TEST_ARCHIVE = (Resolve-Path ../../out/f3-fixtures/browse.xxx).Path
$env:LITEARC_TEST_LONG_ARCHIVE = (Resolve-Path ../../out/f3-fixtures/long.zip).Path
Push-Location packages/litearc_binding
dart test
dart analyze
Pop-Location
```

`generate_fixture.py --entries 100000/1000000` 生成 ZIP header/空文件数据，不提交大型文件。
桌面 Profile 通过 drive 和 Dart driver 收集测试结果，无需 native instrumentation 通道：

```powershell
$fixture = (Resolve-Path ../../out/f3-fixtures/100k.zip).Path
../../scripts/Invoke-Msvc.ps1 flutter drive --device-id=windows --profile `
  --driver=test_driver/integration_test.dart --target=integration_test/browser_test.dart `
  --dart-define=INTEGRATION_TEST_SHOULD_REPORT_RESULTS_TO_NATIVE=false `
  "--dart-define=FIXTURE=$fixture"
```

该命令单独报告 Profile renderer 数据；Release 另行 `flutter build windows --release`
并启动实际 executable。Flutter `test` 本身在此 SDK 不提供 `--profile`。
Synthetic transport tests 只证明 controller/cache/widget 行为，不证明真实索引或 backend。
桌面集成通过真实 ZIP/C ABI 检查打开、Unicode、目录、分页、原生全选、滚动、失效、
取消、关闭及 shutdown；具体证据与限制见验证记录。CI 自动触发策略没有修改；
本轮是本地验证，Flutter CI/全平台发布仍需后续单独接入。

## F5 dependency resolution (2026-11-07)

Rechecked official Flutter stable HEAD: `c197ca5199e9ee5661e865596c9c07294dfab51c`
(3.47.9 / bundled Dart 3.13.7); unchanged. Added stable `super_drag_and_drop`
0.9.1 for OS file transfer, and direct ffi/path for worker-owned preferences.
The plugin constrains device_info_plus/win32/win32_registry to older compatible
stable releases; no prerelease or overrides selected. Its native Rust component
is part of the plugin, not an archive backend. Both pub upgrades were run.
Native codec builds replay the existing verified dependency resolution; this
is not a new latest-backend resolution. F5 acceptance is recorded separately.

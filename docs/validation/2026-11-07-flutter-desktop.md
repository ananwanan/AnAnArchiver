# 2026-11-07 F5 Flutter 桌面体验与平台集成

## 状态与证据边界

从 `main` / `b1ebfed` 实施 F5。功能已接入，Windows x64 Release 构建与实际打开、
设置迁移、原生选择导出准备、Explorer 单文件拖出及目标内容校验通过；
**F5 桌面验收尚未全部完成**：Explorer 目录目标内容与原生异步复制/退出组合仍待独立
核验。不能把协议 mock、真实 C ABI
或用户初次笼统反馈作为这些项目已通过的替代证据。Linux/macOS 只有接线，没有原生
构建/运行证据。F6、关联注册、安装器、签名与正式发布未扩展。

保留的 Qt 分支仍为 `53185742ad1e640b24da397cfa3544507af4edef`，CI 触发策略、
第三方子模块均未修改。依赖声明独立提交；功能、文档与验证另行提交。

## 实现

- 后台设置：schema/revision、串行事务、跨进程文件锁与两秒超时、revision CAS、
  相邻临时文件 flush 后原子替换、一个合并待写快照。持久化 system/light/dark、
  system/zh-CN/en、窗口正常尺寸/最大化与最多 20 条最近路径。未知 schema/字段、
  损坏 JSON、未知 Qt 编码明确拒绝，不覆盖原文件。没有断电持久性实机测试。
- 一次迁移 Qt `appearance/theme`、`archives/recent`，保留原存储，记录 imported/absent。
  Windows 真实 QSettings 是 HKCU 的 AnAnArchiver/AnAnArchiver，theme 为 DWORD，
  recent 为 MULTI_SZ；Linux INI 和 macOS plist 来源、Qt 键转换见开发说明。
  支持 Qt 已知空列表 `@Invalid()`；未知 QVariant 不猜测。没有凭据迁移或持久化。
- Home 只显示真实成功打开后的最近记录；词法去重、移除/清空，不在启动逐条 stat。
  失效/权限/改变后的内容由实际打开返回错误；失败不删除记录。
- 32 项前端会话任务摘要，含实际类型/状态、当前文件、条目、字节、速度、耗时、
  可用总量、输出位置及错误详情；不保留 native task/快照/完整索引用于历史。
  一个浏览通道、一个操作通道，2 active / 16 retained 限制不变；队列、进度 ack 有界。
  被替换的浏览仍观察实际终态后释放；没有假暂停或未知总量的假百分比。
  启动设置读取/worker 创建与退出串接，退出等待启动收尾，禁止退出后继续创建 binding。
- 平台习惯快捷键，文本/密码/对话框作用域、列表焦点边框、新 zh-CN/en ARB。
  Ctrl/Cmd+O/N/E/F/A，Shift+E 全部解压，Alt+Up/Home 或 Cmd+Up/Shift+Up 导航，
  Ctrl/Cmd+句点取消操作；Escape 只在非编辑/非对话框作用域取消浏览。
- 启动参数与运行中系统打开经 adapter/controller 统一处理：单事件一个绝对路径，
  多文件明确拒绝、重复在途请求合并、最新待打开替换旧任务、关闭后停止接收。
  Windows mutex/WM_COPYDATA，Linux GApplication，macOS openFiles；不安装系统关联。
- 真实 OS file-URI 拖放插件；单文件先 worker 内容 probe，目录/普通文件进入真实创建，
  多文件作为创建输入，其他 IO/损坏错误不当作普通文件吞掉。不接受虚拟文件 payload。
- 新增 ABI v1 兼容 feature 和三个导出，原结构/导出不变。选中拖出仍使用 C++ selection
  handle，复用安全解压、原始重复编号、隐式/空目录、目录边界与回滚。
- Qt 临时文件业务抽离到无 Qt 的 Application；四根、每次 10,000 源条目 / 512 MiB
  声明及展开数据 / 十分钟、256 顶层 URL / 1 MiB 路径。沿用 Core 保守源扫描预算，
  大源中的小选择也可能被拒绝；常规解压不降预算。没有 Dart 百万选择展开或内容缓存。
- 源文件准备完后第二次真实手势发起系统拖动。普通 file URI 不能证明目标何时读完，
  因此交付后的目录跨退出/重启保留并继续计入容量；用户确认复制完成后明确释放。
  未交付输出十分钟过期；取消/失败/关闭由受管理 worker 清理。删除重试、清理错误
  与 Windows 扩展长路径共用同一实现；私有存储有 OS 锁，失败根可再次明确释放。
  释放对话框关闭后发生的异步失败仍报告到操作状态，保留原生删除失败的系统诊断。

## 工具链与来源

核对官方 stable HEAD 与本机一致：Flutter **3.47.9**，framework
`c197ca5199e9ee5661e865596c9c07294dfab51c`，engine
`89c39edfdfae2949f6806e4a315c0c08522238b7`，Dart **3.13.7**。
两个 package 均执行 upgrade，提交 lockfile。新增稳定 `super_drag_and_drop` **0.9.1**，
其 super_native_extensions/super_clipboard 同为 **0.9.1**；ffi **2.2.0**、path **1.9.1**。
file_selector **1.1.0**、flutter_svg **2.3.2**、intl **0.20.3** 沿用已解析兼容稳定版本。
插件约束的 win32/device_info 等较旧兼容稳定传递依赖明确保留；没有 overrides、
prerelease 或静默换旧 SDK。super_clipboard 另声明为协议回归测试的直接开发依赖。

Windows x64 / MSVC / CMake / Ninja，配置与实际编译器身份来自构建 provenance。
native 重放 **2026-10-28T15:46:45.521277+00:00** 的已验证解析及 F1 prefix，
libarchive **3.8.10** / `0c48412be2d94afe1859a807ea7896a18d8900bd`；没有声称重新
获取了最新 backend/codec，没有回退系统库。包内 litearc-build 保存实际 SDK、锁文件、
binding header hash、native provenance、解析与许可。开发期间产物含 dirty 标记。

最终用 `out/f5-verify-worktree` 的独立干净检出重放同一解析构建 Windows x64 Release，
避免覆盖实机窗口正在加载的 DLL。检出同时建立 manifest 所列原生源码及 Mbed TLS
嵌套源码的相同 revision；首次缺少 framework 时 provenance 正确拒绝，补齐后通过，
没有绕过来源校验。最终功能提交 **`e5518b32d98e1dc03c51b55cf6d7e36dc253bf52`**，
产物 `litearc-build/dependency-provenance.json` 的 application.dirty 为 **false**。
最后一次构建 41.3 s（增量），bundle 85 个文件、35,209,894 B，未包含 Qt DLL。
该构建数据不作为 clean-machine 启动或安装器验收，实际 Explorer 操作使用此前的
交互 Release；最后的启动退出与错误报告补充由 widget/native 回归验证。

[Flutter 部署表](https://docs.flutter.dev/reference/supported-platforms) 仍列出目标桌面架构；
[Intel 弃用追踪](https://github.com/flutter/flutter/issues/188328) 仍开放。
本机 SDK macos.dart 默认仍有 `x86_64 arm64`，runner 没有删除 Intel 架构；这些是源码/
文档检查，不能替代 Intel Mac 原生验收。
[插件上游](https://pub.dev/packages/super_drag_and_drop) 声明支持三个目标桌面平台，
其声明不作为本项目 Linux/Finder 验收。

## 自动化与原生回归

```powershell
# 仓库根目录；原生 prefix 是显式重放已有解析
./scripts/Invoke-Msvc.ps1 cmake --build out/build/f2-core --parallel 4
ctest --test-dir out/build/f2-core --output-on-failure -E '^dependency_tools$' --parallel 4
ctest --test-dir out/build/f2-core --output-on-failure -R '^dependency_tools$'
./scripts/Invoke-Msvc.ps1 cmake --build out/build/f2-qt --parallel 4
ctest --test-dir out/build/f2-qt --output-on-failure -E '^dependency_tools$' --parallel 4

# frontend/flutter
flutter gen-l10n
dart format --output=none --set-exit-if-changed lib test integration_test test_driver packages/litearc_binding/lib packages/litearc_binding/test packages/litearc_binding/tool
flutter analyze
flutter test
$env:ANANARCHIVER_DEPENDENCY_PREFIX = (Resolve-Path ../../out/build/f1-release/dependencies/install).Path
../../scripts/Invoke-Msvc.ps1 flutter build windows --release
```

- Flutter/controller/widget/protocol **33/33 通过**；analyze、Dart format 通过。
- binding 启用真实 library/archive/long-archive/operations fixtures，**27/27 通过**，
  无跳过真实测试；binding analyze 通过。
- Core/archive/extraction/security/compression/Application/metadata/C consumer/C ABI
  **9/9 suites 通过**；dependency_tools 独立 **1/1 通过**，175.53 s。
- 受影响 Qt/native **21/21 suites 通过**，完整一次 18.69 s，含长路径导出与锁定清理。
  最后补充系统清理诊断后，qt_drag/browser_c_api **2/2 通过**，5.16 s。
- 本次 C/C++ 修改经 clang-format；修复临时管理抽离引入的长路径删除和别名前置声明
  回归、移除重复 NOMINMAX 引入的 warning。最终编译无本次项目 warning/error。
- git diff --check 通过。没有修改 CI 自动触发策略。

新增测试包括首次/重复/未知/损坏迁移、并发写入冲突、合并与最近去重/上限/失效路径，
外部打开替换/重复/关闭、32 项任务摘要、文本/密码/对话框快捷键作用域、导航与 handoff
竞态、启动设置未完成时退出、释放对话框结束后的异步失败。协议 fake 验证 native drop
acknowledgment 不等待 modal/controller Future；
这不是 Explorer 复制证据。真实 C ABI 验证第二个同名文件、隐式/空目录、导出结果在
engine 销毁后仍可读、跨 engine 存储独占、重启四根预算、明确释放与后台清理。

独立 **Profile Flutter + Release C ABI**：

```powershell
# frontend/flutter
$fixtures = (Resolve-Path ../../out/f4-fixtures).Path
../../scripts/Invoke-Msvc.ps1 flutter drive --device-id=windows --profile --driver=test_driver/integration_test.dart --target=integration_test/desktop_test.dart --dart-define=INTEGRATION_TEST_SHOULD_REPORT_RESULTS_TO_NATIVE=false "--dart-define=OPERATIONS_FIXTURES=$fixtures"
```

真实 renderer/controller/worker/C ABI 流程通过：打开、选中第二个 duplicate.txt、准备
6 字节 second、任务中心、设置写入、关闭、shutdown 后仍可读、重新启动 engine 发现
保留根并明确释放。picker 为显式 fixture adapter，不能当作系统 picker 或 OS drag。
报告 shutdown+restart **3 ms**，进程 maxRss **198,094,848 B**。不是 Release 性能。
首次测试修正了 Windows 分隔符断言；没有修改实际路径规范化以迎合测试。

## 生成数据与预算测量

`packages/litearc_binding/tool/measure_desktop.dart` 使用真实生成的 1M ZIP，以及通过
Python zipfile 流式写入 513 个 1 MiB 零块生成的 ZIP（磁盘约 5.2 MB，声明 513 MiB）。
没有提交大型输入或完整 payload 内存缓存。命令从 binding package 执行：

```powershell
dart run tool/measure_desktop.dart ../../../../out/build/f2-core/ananarchiver_c_api.dll ../../../../out/f3-fixtures/1m.zip ../../../../out/f5-fixtures/over-budget.zip
```

| CLI/JIT + Release C ABI 单次指标 | 实测 |
| --- | ---: |
| 真实 1,000,007 条目打开 | 2,396 ms |
| native 查询内全选 | 62 ms |
| Dart 选择返回 rows | 0，仅 native handle |
| 百万选择拖出以资源错误 14 拒绝 | 751 ms |
| 513 MiB 条目打开 / 原生选择 | 55 / 63 ms |
| 超字节预算拖出拒绝 | 61 ms |
| 两次拒绝后剩余输出 children | 0 |
| CLI 进程累计 maxRss | 647,286,784 B |
| 最后 shutdown | 2 ms |

不是百万条目完整拖出或 Flutter AOT 帧率验收。maxRss 包含 VM、完整 native metadata
索引及进程累计峰值，不是 native allocator 的单独预算或优于 Qt 的性能承诺。

## Windows Release 实际操作及尚未闭环项

通过 computer-use 启动生产 Release，无参数显示 Home。实际迁移到了旧 Qt 最近记录，
新 JSON 标记 imported，未逐条打开这些用户路径。随后由真实启动参数打开生成的
`out/f5-os/input/browse.xxx`（内容 ZIP），显示真实目录/8 个直接子项。
通过实际 UI 选择第二个 duplicate.txt，点击准备，私有输出确实是 6 字节 `second`，
界面出现真实系统拖动控件。没有把生产 UI 的准备成功当作目标复制成功。

运行期间再次启动同一 executable，向已有进程转交了 **382 字符、含中文**的生成归档
绝对路径，转交进程 exit 0，新最近记录首项证明真实打开成功；主进程仍只有一个。
连续八次交替转交 first.xxx/last.xxx，最后一次成功打开且进程仍响应。先前交给系统的
6 字节临时输出在视图替换后仍存在；这是实际 runner 事件/文件寿命证据，非协议 fake。
实际方向键导航已观察到选择移动；Ctrl 组合键的原生工具注入未得到可靠结果，快捷键
焦点/作用域证据仍以 widget 测试为准，不宣称所有物理键盘/IME 组合实机通过。

用户曾反馈拖入/拖出成功；独立观察明确确认 `plain.txt` 已进入创建对话框，但指定
`out/f5-os/target` 当时仍为空、没有导出根，因此未将那次反馈计为拖出内容校验通过。
桌面工具的 drag API 限制在单窗口边界内，跨 Explorer/LiteArc 拖动返回 outside window
bounds，因此请求用户执行跨窗口真实手势。修复后的 Release 中，随后实际出现目标
`out/f5-os/target/duplicate.txt`，6 字节、内容 `second`，目标与 export-1 源文件的 SHA-256
均为 `16367AACB67A4A017C8DA8AB95682CCB390863780F7114DDA0A0E0C55644C7C4`。
该文件是用户真实拖动复制的独立磁盘证据，验证了第二个原始同名条目的身份。
另已通过 UI 准备 empty、many（300 个文件）、隐式中文目录三项；准备输出已检查，
尚未观察到对应 Explorer 目标目录，不将它记作目录复制通过。

这轮实机过程暴露了等待创建对话框的 native drop 回调。曾出现“未响应”窗口，旧运行
被终止后重建；该终止不是正常清理/退出通过的证据。现已改成取得普通路径后立即结束
回调，再异步处理创建/打开，并添加协议回归。修复版 Release 的启动与真实导出准备
已重测，单文件复制已按上段独立核验；修复后的 Explorer 创建对话框收尾、目录复制、
目标延迟读取、原生拖动中取消/关闭/退出的
完整 OS 组合还不能标为通过。C ABI / Profile 的延迟读取和清理证据如上，分别列出。

本轮检查品牌 PNG/ICO 内嵌 PNG 均无 iCCP；Profile 与修复版 Release 记录没有重现
历史 libpng iCCP 警告。没有压制日志、改写品牌图或添加无证据 runtime 补丁，仍不能
宣称定位并修复了历史来源。

下一步首先闭环上述 Windows Explorer 实机项目，再在 Linux 文件管理器/Finder、
macOS arm64/x86_64 原生构建与运行；随后 F6 的发布、升级、来源/许可、clean-machine
运行时和完整性能对比。未完成这些检查前不宣称全平台迁移或 F5 全部验收通过。

# 2026-11-19 Windows F5 收尾

## 状态

本轮从 `main` / `16b5d6e405e9350a49918b46d2a7db39a82ead63` 开始。
Flutter 3.50 适配、全新 MSVC native 构建、Release 构建及自动回归通过。
Explorer 目录目标内容、原生 Escape 取消、关闭归档、正常退出后首次读取及
重启保留已补验；未交付输出取消/worker 退出清理也已有 Windows C ABI 回归证据。
**本轮 Windows F5 待办已闭环**：Explorer 目录拖出、Esc、退出后首次读取、
重启保留、真实拖入后的创建/系统保存交互及最终明确释放均有独立证据。
工作从 11-19 持续至 11-20；各证据层次如下，不从 Profile/C ABI 推断 OS 复制。
实机为 Windows 11 x64；Windows 10 的独立运行、Linux/macOS、F6 包装与
clean-machine 尚未验收，既有键盘/IME 和 DPI 证据边界未扩展。

历史 F5 记录保留在 [2026-11-07](2026-11-07-flutter-desktop.md)。
新增[可复跑的 Windows 实机流程](../development/windows-desktop-acceptance.md)。

## SDK 适配及构建身份

- Windows 11 Pro x64，10.0.26300；MSVC **19.52.37029.0**，CMake **4.5.0**，
  Ninja **1.13.2**；Rust **1.100.0** 编译现有拖放插件。
- Flutter **3.50.0 stable** / Dart **3.14.0**；framework
  `ab2912035e160d9cad6306df307576da6128ebdb`，engine
  `371f77202a73e8dff57a8b3bff75bca55c090aee`。
  2026-11-19 用 `git ls-remote origin refs/heads/stable` 核对官方 stable 与本机一致。
- 按[官方迁移说明](https://docs.flutter.dev/release/breaking-changes/material-ui-and-cupertino-ui)
  将实际 UI 和测试改为独立 `material_ui` **1.11.1**（解析的 Cupertino **1.1.7**）。
  应用和对话框测试显式使用该包的本地化 delegates。Flutter 3.50 `gen-l10n` 模板
  仍生成旧 convenience delegates；通过 `l10n.yaml` header 对生成文件局部说明并
  忽略其弃用诊断，应用不使用这份旧 delegate 列表，没有全局关闭诊断。
- 两个 package 执行 upgrade，更新 lockfile；Flutter SVG **2.4.0** 及 vector_graphics
  的兼容正式依赖已更新。`flutter pub outdated` 所有直接依赖均为最新可解析稳定版。
  super_drag_and_drop **0.9.1** 仍约束较旧的 cross_file/device_info/win32/registry 等，
  SDK 固定 material_color_utilities/test_api；没有 prerelease 或 overrides。
- 原缓存中的 MSVC 14.51 编译器已移除，第一次 runner configure 失败；`flutter clean`
  后，来源校验进一步拒绝将旧 native prefix 标记为新编译器构建。使用全新
  `out/build/windows-closeout` 重建完整 native 依赖和 Core/C ABI，没有跳过校验、
  改写旧 provenance 或降级编译器。
- Native 显式重放原来的已验证源码解析，libarchive **3.8.10** /
  `0c48412be2d94afe1859a807ea7896a18d8900bd`；本轮没有重新解析最新 backend/codec，
  没有修改 submodule 或 Qt。构建身份准确记录为 application revision `16b5d6e`、
  **dirty=true**，实际 native 工具链与新 prefix 匹配。

关键命令见实机流程；构建日志在 `out/windows-closeout-{configure,build,flutter}.log`。
Release 构建 127.9 s；将完整 bundle 复制到
`out/windows-desktop-2026-11-19/bundle` 后运行，避免后续 Profile 构建替换在用文件。
该 bundle 共 86 文件、35,389,082 B，没有 Qt DLL。
这些是本机开发产物，不是签名、安装升级或干净机器运行证据。

本轮依赖更新提交为 `60ba0f4`，UI 适配、回归和验收工具提交为
`5fa3fe7bf9b7440963fc63f2e948467e0f2e98b7`。上述实机产物仍保留原构建身份，
没有改称这两个提交的 clean-checkout 构建。

## 自动回归

| 检查 | 本轮结果 |
| --- | --- |
| 新工具链 Core/archive/extraction/security/compression/Application/metadata/C consumer/C ABI | 9/9 suites，2.40 s |
| dependency_tools | 1/1，202.54 s |
| Flutter controller/widget/drop protocol | 33/33 |
| 真实 library/archive/long-archive/operations/cancel-archive binding | 29/29，没有跳过 |
| app 和 binding analyze | 无问题 |
| Dart format / git diff --check | 通过 |
| Windows OLE 测试接收程序 | MSVC `/W4 /WX` 编译、clang-format 通过 |

最初 binding 命令的 archive 环境变量指向缺失夹具，报告跳过；重新生成 browse.xxx、
使用 ErrorActionPreference=Stop 校验全部环境路径后重跑，先得到 27/27 无跳过结果。
11-20 新增两例后最终为 29/29 无跳过，日志 out/windows-closeout-binding.log。

新增 9,000 项目录夹具通过现有生成器产生（`--entries 9000`），通过
`LITEARC_TEST_CANCEL_ARCHIVE` 显式传入。真实 C ABI 已发布非终态且处理条目数
大于 0、少于 9,000 时，分别发送取消和关闭 worker；两次均返回真实 Cancelled
错误 12。engine destroy/join 完成后，未交付的私有输出根均不存在。没有手动删除
输出迎合断言。这是 Windows native worker/临时管理清理证据，不是新的 OS 拖动。
随后补充取消后 engine 仍存活时显式 discard、等待根消失的断言，两例定向
2/2 再次通过；不依靠最后销毁 engine 掩盖取消后应及时清理的行为。

独立 Profile + Release C ABI 增加并通过：真实当前查询全选、三个目录的原生选择、
显式空目录、300 个文件及中文隐式目录准备、两根 handoff、关闭/退出后读取、
重启发现两根并明确释放。原第二个同名文件仍精确得到 `second`。
renderer/controller/binding 测试全部通过，报告 shutdown+restart **52 ms**、
maxRss **195,194,880 B**。这些是 Profile 数据，不是 Release 性能承诺或 OS 复制证据。

`flutter drive` 首次与实机 Release 同时运行，被生产单实例机制转交，无法连接 driver；
正常关闭实机窗口后串行重跑通过。没有为测试关闭生产 mutex 或修改单实例行为。

## 实机夹具与独立判据

`tool/windows_desktop_fixture.py prepare out/windows-desktop-2026-11-19` 生成内容 ZIP 的
`folders.xxx`，只有三个根目录：empty、many、隐式；301 个非空文件/4 个目录、
8,629 B。ZIP SHA-256：
`1bb7236cc79b8e153e23ef82c5f671ed929335d6299c60bdb6dcfa1fc52bb84f`。
每个文件的大小与 SHA-256 单列在生成的 expected.json，不提交 payload。

校验器不向 Explorer/延迟目标解压或复制。空 target 以非零状态失败；独立
`verifier-selfcheck` 的 Python zipfile 输出用于校验工具自检，其成功**不是 OS 证据**。

实机使用隔离 APPDATA 与显式测试偏好，不改用户原设置和之前交给系统的导出。
通过生产 UI 选择三个目录、准备导出；Application 真实处理 302 个源条目、8,629 B，
621 ms。私有 export-0 与 expected.json 完全一致，校验结果在 prepared.json。
界面显示真实 `Drag prepared files`。这只证明准备输出，目标复制另验。

## Explorer、取消和退出后读取

用户将真实 `Drag prepared files` 拖入 Explorer 的本轮 `target`。独立磁盘核验通过：
**301 文件、4 目录、8,629 B**，文件集合、大小和逐文件 SHA-256 全部匹配，包括
显式 empty、300 个非空文件和中文/emoji 隐式路径。证据为 explorer.json；此工具
没有向目标复制或解压，准备目录和 Explorer 目标分别核验。

随后通过真实原生手势将拖动落在应用的拒收区域；用户另执行了鼠标保持按下时
Escape 取消。应用仍响应，私有 export-0 和既有 Explorer 目标重新完整核验通过，
没有新增或丢失条目；分别记录 source-after-escape.json、explorer-after-escape.json
及 explorer-after-rejected-drag.json。用户手势与独立内容检查分别记录，没有从
用户一句反馈推断目标正确。

测试接收程序 `tool/windows_drag_probe.cpp` 通过真实 OLE CF_HDROP 获取路径，立即
结束 Drop；受管理 worker 等指定源进程以 code 0 退出后才复制。目标必须为空，
源路径必须在本轮私有导出存储内。它提供可重复的退出后首次读取判据，不能替代
Explorer 目录内容验收。实际接收真实拖动后，源进程 PID 14720 仍在运行时确认
target-after-exit 为空。随后通过 UI 关闭归档回到 Home，再 Alt+F4 正常退出。
报告实际依次出现：

```text
drop_received=3
source_exit_code=0
copied_after_source_exit=3
```

退出后首次读取的 target-after-exit 逐项核验同样为 301 文件、4 目录、8,629 B；
after-exit.json、source-after-exit.json 均成功。源进程已结束，没有强制终止。
桌面工具拒绝跨窗口输入，实际跨窗口手势由用户执行，没有用系统外复制替代。

正常重启生产 Release（PID 7944），无参数显示 Home；打开“Temporary exports”
对话框，真实列出同一 export-0 和跨重启/明确释放提示。没有自动删除交给 OS 的根。

## Explorer 拖入创建及明确释放

11-20 首次反馈 plain.txt 已拖入后，独立观察仍是 Home，任务中心无任务；当时
没有记为通过。重新执行手势后实际观察到 Create archive 对话框，只有本轮
plain.txt 一个真实输入，格式能力为 ZIP / Deflate，等级 6。没有基于第一次反馈
添加无证据 runtime 补丁，也没有把普通文件当作归档成功占位。

从该对话框打开真实 Windows 另存为并 Escape 取消，返回创建对话框且输入仍在、
Choose output 恢复可用。再次打开另存为，将目标设为本轮不存在的
created-from-drop.zip，保持默认不替换现存归档。创建真实成功：1 条目、24 B、
9 ms。没有出现此前等待 modal Future 的 OLE 卡死。第一次未接收的具体手势原因
未定位，不宣称修复了一个无法复现的拖入 bug。

Python zipfile 独立验证只有 plain.txt、CRC 完整且 bytes 与原输入完全一致，
SHA-256 为 `0aedafde513e3323f16395c38b9f6d776acc28d1da919ff9e1ec7ff7312f5a61`，
结果在 created-result.json。再次启动同一 executable 转交该 ZIP，exit code 0，
已有进程真实打开并显示 plain.txt / 24 B。随后从普通创建入口再次打开对话框并
Escape 取消，回到这份真实归档；这是普通创建取消，未改称第二次 OS 拖入取消。

PID 7944 经 Alt+F4 正常退出，独立进程句柄观察 exit code 0，见 final-exit.json。
全部目标已读完后，用本轮生成的 CLI 脚本通过真实 binding/C ABI action 7/4/6
列出、精确检查仅有的 export-0，再明确释放；删除由原生 manager worker 执行，
没有 shell 直接删除导出根。release-result.json 报告 released_by_native_worker=true。
此处是 C ABI 明确释放，不是点击 UI 释放按钮的实机证据；按钮行为仍沿用此前
widget/native 回归。Explorer 和延迟目标随后分别再次通过完整大小/散列校验，
见 explorer-after-release.json、after-exit-after-release.json。

再次正常启动生产 Release（PID 13912），无参数仍为 Home，最近记录含本次真实
成功打开的新 ZIP。Temporary exports 对话框无路径、无释放按钮，独立磁盘也无
export-0，确认重启后清单为空。原用户设置及旧轮交给系统的输出未修改。

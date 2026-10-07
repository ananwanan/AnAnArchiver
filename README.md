# AnAnArchiver

跨平台归档管理器，使用 C++23、Qt 6 Quick/QML 和固定版本的
[ananwanan/libarchive](https://github.com/ananwanan/libarchive)。

当前完成 AGENTS.md 的 M1 与 M2：独立 Core、libarchive RAII 适配器、
结构化错误、内容检测、流式元数据枚举，以及异步打开和安全流式解压。
不传入文件时显示 Home。支持解压全部/多选条目、目录选择、密码、覆盖交互、
实际字节进度、速度与取消。压缩、归档编辑和系统集成属于后续里程碑。

## 目录与依赖方向

```text
include/ananarchiver/      不依赖 Qt 的公开 Core / Application API
src/core/                 元数据、格式、Unicode
src/backend/libarchive/   唯一调用 libarchive C API 的生产代码
src/application/          归档服务、受管理任务、按需目录扫描
frontend/qt/              Qt Controller、Model、QML、平台路径转换
third_party/              固定 Git commit 的依赖源码子仓库
cmake/                    依赖子项目构建、项目 warnings、sanitizers
tests/                    无 GUI 核心测试和 Qt 适配器测试
docs/architecture/        分层与线程决策
```

依赖方向是 QML → Qt Controller → Application → Core/Backend → libarchive。
Core 和 Application 使用标准 C++，支持不安装 Qt 时独立编译和测试。

## 构建依赖

依赖源码全部保存在 `third_party` 的 Git 子模块中，由父仓库 gitlink 固定提交：
libarchive、zlib、bzip2、XZ/liblzma、Zstandard、LZ4、Expat、Mbed TLS、Qt，
以及 Qt 可选图形接口所用的 Khronos Vulkan-Headers。
Qt 使用 qtbase、qtshadertools、qtdeclarative、qtsvg；其内嵌依赖也由 Qt 提交锁定。
编译器、CMake、Ninja、Python 和操作系统 SDK 是本机工具链，不复制到仓库。
各第三方许可证随其源码保留。
具体版本、提交和初始化方式见 [依赖固定版本表](third_party/README.md)。

默认 superbuild 从这些源码构建依赖，安装到当前构建目录的
`dependencies/install`，随后构建应用。默认不搜索机器上的随机 libarchive 或
codec 版本，也不在构建过程中下载浮动版本。依赖更新应修改 gitlink 并重新运行
回归测试。

要求 CMake 3.25+、Ninja 和支持 C++23 / `std::expected` 的编译器。
Windows 推荐 MSVC x64；Linux 使用现代 GCC/Clang；macOS 使用现代 Clang。
Qt 源码构建还需要 Python，以及目标平台的图形/窗口系统开发环境。

先初始化项目固定的归档依赖：

```sh
cmake -P scripts/init-dependencies.cmake
```

构建和测试不依赖 Qt 的 Core、Backend、Application：

```sh
cmake --preset core-release
cmake --build --preset core-release
ctest --preset core-release
```

完整桌面构建默认包含项目内的 Qt 源码构建，第一次构建耗时较长：

```sh
cmake -DANANARCHIVER_INIT_QT=ON -P scripts/init-dependencies.cmake
cmake --preset desktop-release
cmake --build --preset desktop-release
ctest --preset desktop-release
```

Windows 非 Developer Shell 可使用仓库脚本自动发现并初始化 MSVC 环境：

```powershell
./scripts/Invoke-Msvc.ps1 cmake --preset core-release
./scripts/Invoke-Msvc.ps1 cmake --build --preset core-release
./scripts/Invoke-Msvc.ps1 ctest --preset core-release
```

开发时也可以显式选择已安装、与编译器 ABI 匹配的 Qt kit。设置 `QTDIR` 为
该 kit 的安装根目录，再使用 `desktop-developer` 代替 `desktop-release`。
这只替换 Qt 来源，归档 backend 和 codecs 仍由项目固定源码构建。
`ANANARCHIVER_USE_SYSTEM_LIBARCHIVE=ON` 是额外的开发选项，不属于默认发布路径。

`core-debug` 提供 Debug 构建；`core-sanitizers` 在支持的平台启用 ASan/UBSan。
应用子构建位于 `out/build/<preset>/application`，其 CTest 可以独立运行。
个人路径配置放到被忽略的 `CMakeUserPresets.json`，不要提交机器绝对路径。

Windows 源码构建完成后，在当前终端加入项目内 Qt 的运行时目录再启动应用：

```powershell
$env:PATH = (Resolve-Path out/build/desktop-release/dependencies/install/qt/bin).Path + ';' + $env:PATH
./out/build/desktop-release/application/AnAnArchiver.exe
```

开发 Qt kit 则将 `$env:QTDIR/bin` 加入当前终端的 PATH。可移植分发包和安装器
仍属于后续里程碑，当前构建产物使用对应构建目录中的 Qt 运行时。

## 当前行为与测试

应用使用内容识别归档，扩展名只用于文件选择。工作线程流式生成元数据，
以 512 条为一批发送给 Qt；有界队列、条目数/路径长度/元数据预算限制避免
无限积累。列表使用虚拟化视图。文件系统目录树只在加载/展开时扫描直接子节点。
取消和退出会请求停止并回收线程及 archive handle。

索引只读取元数据，不等同于 CRC 校验。流式格式可能需要解压数据才能到达下一
个 header。损坏的 header、截断数据或 backend warning 会返回结构化错误；
未读取的 payload 损坏需要解压读取或后续 TestArchive 功能验证。需要密码才能
读取目录的归档会请求密码后重新索引；解压也支持独立输入密码，不保存或记录密码。

解压通过现有 TaskManager 在工作线程运行。选择目录会同时解压其子条目；
重复文件名用条目序号区分。目标目录和密码在解压对话框指定，覆盖策略支持
询问、覆盖、跳过、重命名、仅覆盖较旧文件；询问支持应用到后续全部冲突。
任务等待覆盖回答时仍可取消。未知总大小显示不确定进度，速度只根据实际读取的
解压字节计算，不把索引大小当作 IO。

输出路径拒绝穿越、绝对路径、驱动器/UNC、ADS 和 Windows 名称别名；
文件系统适配器拒绝通过 symlink/junction 等链接写出。链接支持安全保留、
保留目标目录内链接、跳过三种策略。输出在目标文件系统内暂存，验证成功后
安装，失败或取消会回滚并尽可能清理，已有文件保留。若清理/恢复失败，返回
可诊断错误并保留恢复数据；突然终止进程或断电不提供完整恢复保证。

默认安全上限为 200 万条目、64 KiB 路径、256 层目录、单条目 100 GiB、
总计 1 TiB、512 MiB 事务元数据及 24 小时工作时间，Core 请求可显式调整。
限制依据实际数据和 sparse 逻辑偏移检查，不只信任 header 声明。
POSIX 输出不修改进程 cwd/umask；Windows 使用 Unicode 和长路径 API。

测试动态生成小型归档，不向 Git 提交大型测试数据。覆盖内容识别、常见格式、
Unicode 文件与 entry 路径、无效/截断输入、枚举取消、metadata 限制、任务
背压/并发限制/异常隔离/退出清理，以及按需目录扫描。M2 增加生成式解压回归：
全部/选中、重复路径、目录子条目、Unicode/长路径、覆盖和链接策略、路径攻击、
Windows junction、资源限制、CRC/截断/加密损坏、取消和事务恢复。
Qt 测试使用生产 QML 完成真实解压、覆盖询问、取消和密码重试，验证按钮与任务链路。

桌面程序支持 `--smoke-test <milliseconds>` 验证加载启动，以及
`--smoke-archive <archive>` 验证 Qt → Application → Backend → 条目模型的
完整打开链路并以进程退出码报告成功或失败。

归档文件名、目录名、路径和 backend 错误详情统一按纯文本显示，避免将恶意
文件名解释为 HTML。渲染回归测试包含实际图片请求检测和正向对照，验证文件名
中的 `<img>` 不会触发额外文件或网络读取。

架构说明见 [Core 与前端边界](docs/architecture/0001-core-and-frontend.md)、
[受管理索引任务](docs/architecture/0002-managed-indexing.md) 和
[安全流式解压](docs/architecture/0003-secure-extraction.md)。
M2 的执行证据和验证边界见 [M2 验收记录](docs/validation/2026-10-07-m2.md)。

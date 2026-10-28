# 轻匣 · LiteArc

轻匣（LiteArc）是跨平台归档管理器，使用 C++23、Qt 6 Quick/QML 和
[libarchive/libarchive](https://github.com/libarchive/libarchive)。

当前完成 AGENTS.md 的 M1、M2、M3 与 M4：独立 Core、libarchive RAII 适配器、
结构化错误、内容检测、流式元数据枚举，以及异步打开和安全流式解压。
不传入文件时显示 Home。支持解压全部/多选条目、目录选择、密码、覆盖交互、
实际字节进度、速度与取消。支持创建 ZIP、7z、TAR、TAR.GZ、TAR.BZ2、
TAR.XZ、TAR.ZST 和 TAR.LZ4，创建选项由实际 writer capabilities 驱动。
归档编辑属于后续里程碑。M5 已增加自动发布打包、可选系统集成和安装验收；
各平台的实际验证范围见下文。

桌面体验包含统一的浅色/深色/跟随系统主题、最近归档、异步元数据搜索与
名称/大小/类型/时间排序、任务中心、拖入打开或创建归档、选中条目拖出、
键盘快捷键，以及可复制的纯文本错误详情。主题和最多 20 条最近记录在后台
保存；任务中心保留最多 64 条本次会话记录，支持按任务取消和清除已结束任务。
帮助菜单中的“开源组件与许可”列出随附后端、Qt 及构建依赖的用途和许可证，
显示当前应用版本和可用的 Git revision，以及各组件实际构建版本、可用的 Git
revision 与源码仓库；这些信息来自与最终 provenance 共用的、经验证的构建身份
和配置记录。还提供
项目网站、上游许可原文和 Qt 内嵌第三方组件的许可目录链接。

产品中文显示名为“轻匣”，英文名为“LiteArc”；可执行文件、安装器和分发包使用
LiteArc。源码仓库地址仍为
[ananwanan/AnAnArchiver](https://github.com/ananwanan/AnAnArchiver)。
内部 API、构建选项/targets、应用设置及系统集成身份和 `share/AnAnArchiver`
数据目录保留现有标识，以兼容既有构建、用户设置和升级。
`docs/validation` 的历史记录保留当时真实的 AnAnArchiver 产物名称与测试证据。

## 目录与依赖方向

```text
include/ananarchiver/      不依赖 Qt 的公开 Core / Application API
src/core/                 元数据、格式、Unicode
src/backend/libarchive/   唯一调用 libarchive C API 的生产代码
src/application/          归档服务、受管理任务、按需目录扫描
frontend/qt/              Qt Controller、Model、QML、平台路径转换
third_party/              依赖源码子仓库与上游来源
cmake/                    依赖子项目构建、项目 warnings、sanitizers
tests/                    无 GUI 核心测试和 Qt 适配器测试
docs/architecture/        分层与线程决策
```

依赖方向是 QML → Qt Controller → Application → Core/Backend → libarchive。
Core 和 Application 使用标准 C++，支持不安装 Qt 时独立编译和测试。

Windows 读取未标注 Unicode 编码的旧式 ZIP 时，由 libarchive 按本机 OEM 代码页
解码文件名（例如简体中文系统上的 GBK）；ZIP 的 UTF-8 标记和 Unicode 路径字段
仍优先。归档操作使用线程内的 UTF-8 区域设置并在结束后恢复，不修改全局设置。
缺少编码标记且来自其他代码页的 ZIP 仍可能需要指定来源编码；当前尚未提供
手动编码选择。

## 构建依赖

依赖方案统一采用指定上游的最新稳定发布版，不固定历史版本号、tag 或 commit。
libarchive 从官方 `libarchive/libarchive` 获取最新稳定发布版。
依赖源码保存在 `third_party` 的 Git 子模块中：
libarchive、zlib、bzip2、XZ/liblzma、Zstandard、LZ4、Expat、Mbed TLS、Qt，
以及 Qt 可选图形接口所用的 Khronos Vulkan-Headers。
Qt 使用 qtbase、qtshadertools、qtdeclarative、qtsvg，各模块和内嵌依赖跟随
最新 Qt 正式发布版的配套源码；Mbed TLS framework 也跟随对应发布版。
编译器、CMake、Ninja、Python 和操作系统 SDK 是本机工具链，不复制到仓库。
各第三方许可证随其源码保留。
上游来源、更新策略和解析清单见 [依赖方案](third_party/README.md)。

默认 superbuild 从这些源码构建依赖，安装到当前构建目录的
`dependencies/install`，随后构建应用。默认不搜索机器上的随机 libarchive 或
codec 版本。方案要求在依赖初始化或更新时解析最新版本，并记录本次构建的
实际版本与提交；编译过程中使用同一份源码。依赖更新后重新运行回归测试。

`scripts/resolve-dependencies.py` 在初始化时查询指定上游的正式发布记录或正式版本 tag，
排除预发布版本，解析并检出最新源码；libarchive 与其他发布正式版本的依赖一样
优先选择官方最新稳定版。Qt 子模块和 Mbed TLS framework 使用所选发布版记录的配套
提交，不独立升级。解析失败、脏源码或不兼容版本会明确报错，不自动降级。
`scripts/init-dependencies.cmake` 是调用该解析器的兼容入口。

默认解析清单位于 `out/dependencies/resolved-dependencies.json`，记录 URL、
版本、ref、实际 commit 和 UTC 解析时间。CMake 验证源码与清单一致，构建目录
另存 `resolved-dependencies.json` 和 `dependency-provenance.json`，记录实际
backend、Qt 和编译器信息；安装时复制到 `share/AnAnArchiver`。
删除 `out` 后，必须先重新执行依赖初始化，或从保存在该目录之外的历史清单重放，
再运行配置和编译命令；保留 `third_party` 源码本身不能替代解析清单。
`dist` 仅保存打包输出，删除它不影响编译。恢复最近一次已验证源码集的离线命令见
[2026-10-28 依赖记录](docs/validation/2026-10-28-dependency-update.md)。
构建前再次纯本地验证源码，并将依赖源码、工具链、编译选项和构建适配器绑定到
构建目录及安装 prefix。版本或构建参数变化时使用新的目录，防止旧库误标新版本；
只有解析时间改变而源码与构建参数未改变时可以复用原目录。最终 provenance 在
生产产物成功构建后记录，包含实际生成工具版本；配置/失败诊断与最终记录分开。

要求 Python 3.10+、CMake 3.25+、Ninja 和支持 C++23 / `std::expected` 的编译器。
Windows 推荐 MSVC x64；Linux 使用现代 GCC/Clang；macOS 使用现代 Clang。
Qt 源码构建还需要 Python，以及目标平台的图形/窗口系统开发环境。
Mbed TLS Git 源码构建需要 Perl 和当前稳定版 Python `jinja2` / `jsonschema`，
生成文件写入构建目录，保留上游源码检出的干净状态：

```sh
python -m pip install --upgrade jinja2 jsonschema
```

Windows 可以将 Git for Windows 的 `usr/bin` 临时加入当前终端 PATH 来提供
Perl，或通过 CMake 的 `PERL_EXECUTABLE` 明确选择安装位置。

先解析并初始化最新归档依赖源码：

```sh
cmake -P scripts/init-dependencies.cmake
```

也可直接调用 Python 解析器。先只解析、不修改源码，再按同一份清单检出：

```sh
python scripts/resolve-dependencies.py --qt --resolve-only --manifest out/dependencies/selected.json
python scripts/resolve-dependencies.py --qt --replay-manifest out/dependencies/selected.json
python scripts/resolve-dependencies.py --qt --verify --replay-manifest out/dependencies/selected.json
```

重放用于复现一次构建，不更改后续初始化选择最新版本的策略。已有所需 Git
提交时可加 `--offline` 完全离线重放；缺少提交时失败。清单不是长期版本锁。
GitHub API 限流时通过环境变量 `GITHUB_TOKEN` 提供只读凭据，不把 token 写入
清单或日志。依赖源码有本地修改时，先保存这些修改再执行更新。
`--verify` 只检查清单、配套源码、origin、版本和本地修改，不联网、不检出源码
也不改写清单；CMake 入口可用 `ANANARCHIVER_DEPENDENCY_VERIFY=ON` 配合
`ANANARCHIVER_DEPENDENCY_REPLAY_MANIFEST`。Core-only 验证增加 `--core-only`。

归档 backend 从官方仓库解析最新稳定发布版。为保留 Mbed TLS 4 / TF-PSA-Crypto
兼容与 ZIP 取消资源清理，`cmake/PrepareLibarchive.cmake` 从已验证的官方 revision
导出构建用源码副本，仅对副本应用 `cmake/patches/libarchive-compatibility.patch`，
不修改子模块。补丁不能应用时明确失败，更新上游后需审查并移除已被官方包含的修复。
配置时会从官方 revision 与补丁重建预期完整源码清单；每次构建和记录来源前，
只读校验实际生成源码的全部文件、目录及符号链接，拒绝源码被修改、增添或删除。
provenance 记录官方 base revision 和补丁 SHA-256；安装和分发包将补丁副本保存在
`share/AnAnArchiver/libarchive-compatibility.patch`。复现时使用对应应用源码及相同补丁，
重放随包的官方依赖清单后正常运行 CMake，构建入口会自动应用并记录补丁。

历史 fork 的修复与审计不能作为官方发布版的兼容性证明。旧来源、对应 revision 和
当时的验证结果保留在 [历史依赖验证记录](docs/validation/2026-10-11-dependency-resolution.md)
中。切换来源、版本或补丁后，须重新验证并构建，不能复用旧 backend 的构建来源记录。
当前官方来源切换及验证范围见
[2026-10-17 验证记录](docs/validation/2026-10-17-official-libarchive.md)。

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
若清单包含 Qt，kit 必须与清单中的正式发布版一致；其实际版本会写入构建记录。
这只替换 Qt 来源，归档 backend 和 codecs 仍由项目内已获取的源码构建。
`ANANARCHIVER_USE_SYSTEM_LIBARCHIVE=ON` 是额外的开发选项，不属于默认发布路径。

`core-debug` 提供 Debug 构建；`core-sanitizers` 在支持的平台启用 ASan/UBSan。
应用子构建位于 `out/build/<preset>/application`，其 CTest 可以独立运行。
个人路径配置放到被忽略的 `CMakeUserPresets.json`，不要提交机器绝对路径。

GitHub CI 当前暂时停用，日常使用本地构建和测试。工作流保留全部作业及
`workflow_dispatch` 手动入口，移除了 `push`、`pull_request` 自动触发。
需要手动运行时，先在 GitHub Actions 中启用工作流，再选择 Run workflow；
以后恢复自动 CI 时，将这两个触发事件加回 `.github/workflows/build.yml`。

保留的 CI 作业先解析一次完整清单，随后 MSVC、GCC、Clang/libc++、Apple Clang 和
ASan/UBSan 作业重放同一组依赖提交，防止并行作业之间发生版本漂移。
Windows 桌面作业使用 `desktop-release` 构建所选 Qt 的完整配套源码，执行
Core、生产 QML 解压/创建/桌面交互/拖放回归、QML lint 和应用启动 smoke。
解析清单、构建来源信息与 CTest 结果保存为 CI artifacts。
Windows/Linux 另设独立离线依赖工具回归；各编译作业重放后再次纯本地验证，
失败时也上传可用的 provenance、配置记录、CTest/JUnit 与依赖构建日志。

Windows 源码构建完成后，在当前终端加入项目内 Qt 的运行时目录再启动应用：

```powershell
$env:PATH = (Resolve-Path out/build/desktop-release/dependencies/install/qt/bin).Path + ';' + $env:PATH
./out/build/desktop-release/application/LiteArc.exe
```

开发 Qt kit 则将 `$env:QTDIR/bin` 加入当前终端的 PATH。供最终用户使用的
分发包通过以下打包入口部署运行时，不需要配置 Qt 或开发环境 PATH。

也可以由脚本从应用构建的 CMake cache 自动定位对应 Qt kit 并启动，不修改全局 PATH：

```powershell
./scripts/Start-Desktop.ps1 out/build/desktop-release
```

脚本同时支持 superbuild 和直接应用构建目录，并把附加参数传给应用。

### 自动发布与系统集成

Windows Release MSVC x64 构建完成后：

```powershell
./scripts/Package-Windows.ps1 -BuildDirectory out/build/desktop-release -SkipBuild -BootstrapInstaller
```

去掉 `-SkipBuild` 可先编译再打包；脚本会自动初始化 MSVC x64 环境，也会重新检测
之前因编译环境缺失而失败并被 CMake 缓存的 C++ 能力检查。

`-SkipBuild` 仅适用于已用当前依赖追踪流程完整构建的 Release 目录；应用构建目录
必须同时包含 `dependency-provenance.json` 和 `resolved-dependencies.json`。
superbuild 的记录位于其 `application/` 子目录，直接应用构建的记录位于构建目录根。
若旧目录缺少这些记录，应使用尚未使用的新目录重新配置并完整编译，不能为已有
EXE 或依赖补写、复制记录。以下示例中的 `desktop-release-traced` 必须是新目录：

```powershell
cmake -DANANARCHIVER_INIT_QT=ON -P scripts/init-dependencies.cmake
./scripts/Invoke-Msvc.ps1 cmake --preset desktop-release -B out/build/desktop-release-traced
./scripts/Invoke-Msvc.ps1 cmake --build out/build/desktop-release-traced --parallel 4
./scripts/Package-Windows.ps1 -BuildDirectory out/build/desktop-release-traced -SkipBuild -BootstrapInstaller
```

这会在新目录重新构建配套 Qt 源码、归档依赖和应用，首次编译耗时较长。打包脚本
会在修改产物目录、部署运行时或下载安装器工具之前检查并读取上述两个记录。

脚本使用 CMake 所选的同一 Qt kit 部署 DLL、QML、平台与 SVG 插件，并加入
MSVC app-local runtime、依赖来源、许可证和逐文件 SHA-256 清单。
`out/artifacts/windows` 生成 `LiteArc-<version>-windows-x64.zip` 和
`LiteArc-<version>-windows-x64-setup.exe` 当前用户安装器。
portable 包必须完整解压后运行；安装器提供中英界面，无需管理员权限。
“打开方式”注册、资源管理器右键菜单与桌面快捷方式均为可选项。
关联注册保留 Windows 的现有默认应用；在系统设置中选择默认应用即可双击打开。
Windows 11 的传统菜单项目可能显示在“显示更多选项”中。
升级会更新已选择的集成项目；卸载只删除本程序管理的文件和注册，保留用户另加文件。

系统入口使用以下参数，归档读取和写入仍由原任务系统执行：

```text
LiteArc archive.zip
LiteArc --extract-here archive.zip
LiteArc --extract-to-folder archive.tar.gz
LiteArc --compress file1 folder2
```

解压保留覆盖询问、密码交互与安全限制。压缩入口打开已填入输入和建议输出路径的
创建对话框，确认后才写入归档。文件名以 `-` 开头时，参数与路径之间可加 `--`。

`scripts/Test-Package.ps1` 校验部署哈希、最小 PATH 启动、中文与长路径、系统操作、
Core/Qt 回归，以及隔离注册表下的安装、版本升级、取消集成选项和卸载。
升级测试需要用同一安装脚本构造较低产品版本的安装器 fixture，详细参数见
[Windows 发布说明](packaging/windows/README.md)。

Linux 和 macOS 在相应本机 Release 构建后打包：

```sh
bash scripts/Package-Linux.sh out/build/desktop-release
bash scripts/Package-macOS.sh out/build/desktop-release --dmg
```

Linux 生成 Qt/QML runtime 与启动脚本配套的 tar.gz，提供可选的当前用户
`.desktop`/MIME 安装与卸载脚本，入口为 `LiteArc.sh`，不覆盖默认 MIME 应用。
macOS 部署 `LiteArc.app`、
归档 document types 和 Finder 打开事件，可生成 app ZIP 与 DMG。
系统图形库、glibc 等遵循构建主机的兼容范围；macOS 分发签名与公证需发布者身份。
详细用法见 [Linux 集成](packaging/linux/README.md) 和 [macOS 部署](packaging/macos/README.md)。
历史 M5 的 Windows 可交付包与安装流程已在本机验收；Linux/macOS 已验证元数据和脚本，
尚未进行这两个平台的原生 GUI/打包运行验收。证据见
[M5 验收记录](docs/validation/2026-10-12-m5.md)。

### Windows Store MSIX

`scripts/Package-MSIX.ps1` 可将 Release MSVC x64 构建打包为供 Microsoft Store
提交的未签名 MSIX。需要安装含 `MakeAppx.exe` 的 Windows SDK；脚本自动发现
SDK，也可用 `-MakeAppxPath` 指定工具。以下三个身份值必须替换为 Partner Center
产品的 **Product management > Product identity** 中的准确值：

```powershell
./scripts/Package-MSIX.ps1 -BuildDirectory out/build/desktop-release -SkipBuild `
    -IdentityName '<Partner Center 的 Package/Identity/Name>' `
    -Publisher '<Partner Center 的 Package/Identity/Publisher>' `
    -PublisherDisplayName '<Partner Center 的 Package/Properties/PublisherDisplayName>'
```

脚本复用现有 Qt/CRT、许可证和依赖来源部署，不需要 Inno Setup；也可用
`-PortableDirectory` 输入现有完整 portable 目录，并校验其清单和逐文件哈希。
默认版本取构建记录并补齐第四段 `.0`；`-PackageVersion 1.0.1.0` 可单独设置
Store 包版本，第四段必须为零。产物与身份、来源、工具和哈希记录位于
`out/artifacts/msix`，打包后会解包核对全部文件。同名产物不会覆盖；再次打包时
请使用新的 `-PackageVersion` 或 `-OutputDirectory`。

该包声明桌面 full-trust 能力，最低 Windows 10 1809，不增加安装器的可选右键菜单
或文件关联。Microsoft Store 认证后负责签名；本地侧载需另外签名。生成成功不代表
已完成 MSIX 安装、Windows App Certification Kit 或 Store 审核；详细参数和提交
步骤见 [MSIX 发布说明](packaging/windows/README.md#microsoft-store-msix)。

### Android 测试 APK

Windows 上可用同版本的 Qt Android kit 与宿主 Qt kit 构建测试 APK；归档 backend
与 codecs 仍使用项目内已获取的源码。先按 [Qt Android 环境要求](https://doc.qt.io/qt-6/android-configure-dev-environment.html)
准备对应 NDK、JDK、SDK API 36 与 build-tools。工具链路径通过参数传入：

```powershell
./scripts/Build-Android.ps1 -QtAndroidDir $env:QT_ANDROID_DIR -QtHostDir $env:QT_HOST_PATH `
    -AndroidNdkDir $env:ANDROID_NDK_ROOT -AndroidSdkDir $env:ANDROID_HOME -JavaHome $env:JAVA_HOME
```

脚本会将路径转为绝对路径并统一使用正斜杠，避免 Windows 反斜杠被依赖构建的
CMake 日志脚本当作转义字符；相对路径按当前 PowerShell 工作目录解析。
开始编译前会检查 Android 与宿主 Qt kit 版本一致，并匹配当前依赖解析记录中的 Qt 版本。

默认构建 `arm64-v8a`，最低 Android 9 / API 28、目标 API 36。脚本使用 Release
原生代码和 Android debug key，产物位于 `out/artifacts/android`；同时验证签名、
16 KiB ZIP/ELF 对齐、包名、SDK 与全部原生库的 ABI，并保存 `verification.json`。NDK r27/r28
的 libc++ 默认隐藏 `std::stop_token` / `std::jthread`，构建会先验证其编译和运行库链接，
必要时对项目 C++ targets 启用官方 `-fexperimental-library` 选项。该测试包可供本地安装，
正式发布需要所有者的 release signing key。
若 Windows 的 Java selector pipe 报 `Unable to establish loopback connection` / UnixDomainSockets
连接错误，可加 `-JavaTcpPipes` 使用 JDK 的 TCP fallback；该选项只作用于本次构建进程。

当前 APK 复用桌面 QML 界面，尚未进行手机布局适配。文件选择返回的 Android
`content://` URI 尚未桥接到 Qt 无关的 Core 文件 IO；公共存储浏览、解压和创建归档
需要后续 Storage Access Framework 适配。APK 构建与静态验证不代表 Android 真机功能验收。

## 当前行为与测试

应用使用内容识别归档，扩展名只用于文件选择。工作线程流式生成元数据，
以 512 条为一批发送给 Qt；有界队列、条目数/路径长度/元数据预算限制避免
无限积累。列表使用虚拟化视图。文件系统目录树只在加载/展开时扫描直接子节点。
取消和退出会请求停止并回收线程及 archive handle。

打开归档后，左侧显示文件夹树，包含由条目路径推导出的父目录和显式空目录；
右侧显示所选文件夹的直接子文件和子文件夹，文件夹优先，名称列显示名称。
隐式目录和显式空目录同样显示，同一文件夹只占一行；双击或 Enter 可进入。
可通过根目录入口和返回上级按钮导航，当前路径显示所选目录。
归档目录树来自已索引的元数据，独立于原有的文件系统目录浏览。
目录树构建受深度、节点数量和路径内存预算限制，超限时明确报错。
实现与验证见 [目录列表补充记录](docs/validation/2026-10-19-folder-list.md)。

索引只读取元数据，不等同于 CRC 校验。流式格式可能需要解压数据才能到达下一
个 header。损坏的 header、截断数据或 backend warning 会返回结构化错误；
未读取的 payload 损坏需要解压读取或后续 TestArchive 功能验证。需要密码才能
读取目录的归档会请求密码后重新索引；解压也支持独立输入密码，不保存或记录密码。

解压通过现有 TaskManager 在工作线程运行。Application API 中选择目录会同时
解压其子条目；桌面归档列表的多选作用于当前可见条目，选中文件夹会包括其后代，
重复文件名用原始条目序号区分，隐式文件夹通过安全的目录路径选择。目标目录和密码在解压对话框指定，覆盖策略支持
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

创建通过独立 `ArchiveWriter`、`CompressArchive` 任务和 Qt 创建对话框完成。
可添加多个文件和目录，选择输出、格式、方法和受支持的压缩等级。ZIP 默认
使用 Deflate，7z 默认使用 LZMA2；Store 和普通 TAR 不提供压缩等级。
M3 创建接口支持未加密归档，不暴露密码、多线程或 solid 设置。

输入扫描和数据读取均在工作线程执行。扫描只建立有界元数据索引，支持取消；
写入复用 128 KiB buffer，不缓存完整文件内容。扫描阶段总量未知且不计算 IO
速度；写入按实际读取字节报告进度，最终写入阶段显示不确定进度。完成后可以
直接打开新建归档。重复选择和已被选中目录包含的子路径会去重，归档路径冲突
会明确报错。链接、junction 和特殊输入文件会被拒绝，不沿链接递归扫描。

输出必须位于已存在的目录，并且不能位于选中的输入目录内或别名到输入文件。
先写入目标文件系统的私有临时目录，检查 backend close、flush 和 close 结果后
原子安装。替换已有归档需要明确选择；提交前的失败或取消保留原文件并清理临时
输出。提交后的取消不会把已安装的完整归档报告为取消；提交后清理或持久化失败
会明确报告已提交状态。源文件的身份、大小和时间等变化会导致失败；这不是文件
系统快照。Windows 使用宽字符 entry 名称；POSIX 使用可恢复的工作线程 UTF-8
locale，读写操作不会修改进程全局 locale。

测试动态生成小型归档，不向 Git 提交大型测试数据。覆盖内容识别、常见格式、
Unicode 文件与 entry 路径、无效/截断输入、枚举取消、metadata 限制、任务
背压/并发限制/异常隔离/退出清理，以及按需目录扫描。M2 增加生成式解压回归：
全部/选中、重复路径、目录子条目、Unicode/长路径、覆盖和链接策略、路径攻击、
Windows junction、资源限制、CRC/截断/加密损坏、取消和事务恢复。
Qt 测试使用生产 QML 完成真实解压、覆盖询问、取消和密码重试，验证按钮与任务链路。
M3 增加所有创建格式/方法的生成式往返、等级端点、C locale Unicode、长路径、
真实字节进度、取消、源变化、资源预算和原子输出回归；生产 QML 测试验证创建、
打开、解压完整链路以及键盘、GUI 响应、能力选项和取消时保留旧文件。

桌面程序支持 `--smoke-test <milliseconds>` 验证加载启动，以及
`--smoke-archive <archive>` 验证 Qt → Application → Backend → 条目模型的
完整打开链路并以进程退出码报告成功或失败。

归档文件名、目录名、路径和 backend 错误详情统一按纯文本显示，避免将恶意
文件名解释为 HTML。渲染回归测试包含实际图片请求检测和正向对照，验证文件名
中的 `<img>` 不会触发额外文件或网络读取。

架构说明见 [Core 与前端边界](docs/architecture/0001-core-and-frontend.md)、
[受管理索引任务](docs/architecture/0002-managed-indexing.md) 和
[安全流式解压](docs/architecture/0003-secure-extraction.md) 和
[流式创建与原子输出](docs/architecture/0004-streaming-compression.md)。
M2 的执行证据和验证边界见 [M2 验收记录](docs/validation/2026-10-07-m2.md)。
M3 的执行证据和验证边界见 [M3 验收记录](docs/validation/2026-10-07-m3.md)。
M4 的执行证据和验证边界见 [M4 验收记录](docs/validation/2026-10-08-m4.md)。
轻匣改名、组件版本显示及本机搜狗输入法的超长安装路径限制，见
[2026-10-18 验证记录](docs/validation/2026-10-18-litearc.md)。

## 桌面操作

`Ctrl+O` 打开，`Ctrl+N` 创建，`Ctrl+E` 解压，`Ctrl+F` 聚焦搜索，`Ctrl+J`
打开任务中心，`Ctrl+W` 关闭归档。文件列表支持方向键、Home/End、空格切换
选择及 Ctrl+A。搜索中的 Escape 清空搜索；任务中的 Escape 请求取消。
搜索和排序只作用于当前文件夹的直接子文件和子文件夹，均在后台处理。点列表表头排序，
重复点击切换方向；“归档顺序”恢复原始条目顺序。切换文件夹、更改搜索或排序
会清空选择并使过期查询结果失效，全选仅选择当前可见条目；“解压全部”仍指
整个归档。选中解压与拖出保留条目的原始归档索引。

拖入单个文件时，在工作线程根据内容探测归档；普通文件和多文件/目录进入
创建对话框。网络 URL 不接受。拖出时先选择条目，使用“导出”准备文件
（加密条目可输入密码）；准备成功后按住选中行或导出按钮拖入目标文件夹。
准备在后台安全解压，跳过链接并保留目录结构，只提供复制操作。每次导出
最多 16 GiB、两小时和 10,000 个顶层文件 URL；最多保留四个临时导出目录。
系统拖放返回后暂存文件保留十分钟，便于桌面继续复制；选择更改会使已准备
的拖放失效，失败、取消和退出会尽力清理。清理失败会显示具体残留路径。

Windows 可把生成式测试数据放在指定的 SSD 目录，设置只影响测试子进程：

```powershell
./scripts/Test-Desktop.ps1 -BuildDirectory out/build/m4-msvc -TemporaryRoot D:/Cache/Codex/LiteArc
```

`-BuildDirectory` 可使用任何已配置的应用构建目录。省略 `-TemporaryRoot` 时
使用操作系统临时目录；没有把开发机器路径写入程序或 CMake 默认配置。

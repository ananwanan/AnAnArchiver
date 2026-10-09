# AnAnArchiver

跨平台归档管理器，使用 C++23、Qt 6 Quick/QML 和
[ananwanan/libarchive](https://github.com/ananwanan/libarchive)。

当前完成 AGENTS.md 的 M1、M2、M3 与 M4：独立 Core、libarchive RAII 适配器、
结构化错误、内容检测、流式元数据枚举，以及异步打开和安全流式解压。
不传入文件时显示 Home。支持解压全部/多选条目、目录选择、密码、覆盖交互、
实际字节进度、速度与取消。支持创建 ZIP、7z、TAR、TAR.GZ、TAR.BZ2、
TAR.XZ、TAR.ZST 和 TAR.LZ4，创建选项由实际 writer capabilities 驱动。
归档编辑和系统集成属于后续里程碑。

桌面体验包含统一的浅色/深色/跟随系统主题、最近归档、异步元数据搜索与
名称/大小/类型/时间排序、任务中心、拖入打开或创建归档、选中条目拖出、
键盘快捷键，以及可复制的纯文本错误详情。主题和最多 20 条最近记录在后台
保存；任务中心保留最多 64 条本次会话记录，支持按任务取消和清除已结束任务。

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

## 构建依赖

依赖方案统一采用指定上游的最新稳定发布版，不固定历史版本号、tag 或 commit。
`ananwanan/libarchive` 没有独立正式发布版时使用默认分支最新提交。
依赖源码保存在 `third_party` 的 Git 子模块中：
libarchive、zlib、bzip2、XZ/liblzma、Zstandard、LZ4、Expat、Mbed TLS、Qt，
以及 Qt 可选图形接口所用的 Khronos Vulkan-Headers。
Qt 使用 qtbase、qtshadertools、qtdeclarative、qtsvg，各模块和内嵌依赖跟随
最新 Qt 正式发布版的配套源码；Mbed TLS framework 也跟随对应发布版。
编译器、CMake、Ninja、Python 和操作系统 SDK 是本机工具链，不复制到仓库。
各第三方许可证随其源码保留。
上游来源、更新策略和当前初始化方式见 [依赖方案](third_party/README.md)。

默认 superbuild 从这些源码构建依赖，安装到当前构建目录的
`dependencies/install`，随后构建应用。默认不搜索机器上的随机 libarchive 或
codec 版本。方案要求在依赖初始化或更新时解析最新版本，并记录本次构建的
实际版本与提交；编译过程中使用同一份源码。依赖更新后重新运行回归测试。

当前 `scripts/init-dependencies.cmake` 仍按父仓库 gitlink 记录的提交初始化源码，
superbuild 构建已获取的源码。这次修改的是依赖方案，自动解析最新版本的逻辑
尚未接入，运行下面的现有命令不会自动升级依赖。

要求 CMake 3.25+、Ninja 和支持 C++23 / `std::expected` 的编译器。
Windows 推荐 MSVC x64；Linux 使用现代 GCC/Clang；macOS 使用现代 Clang。
Qt 源码构建还需要 Python，以及目标平台的图形/窗口系统开发环境。

先初始化当前仓库记录的归档依赖源码：

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
这只替换 Qt 来源，归档 backend 和 codecs 仍由项目内已获取的源码构建。
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

也可以由脚本从应用构建的 CMake cache 自动定位对应 Qt kit 并启动，不修改全局 PATH：

```powershell
./scripts/Start-Desktop.ps1 out/build/desktop-release
```

脚本同时支持 superbuild 和直接应用构建目录，并把附加参数传给应用。

### Android 测试 APK

Windows 上可用同版本的 Qt Android kit 与宿主 Qt kit 构建测试 APK；归档 backend
与 codecs 仍使用项目内已获取的源码。先按 [Qt Android 环境要求](https://doc.qt.io/qt-6.11/android-configure-dev-environment.html)
准备对应 NDK、JDK、SDK API 36 与 build-tools。工具链路径通过参数传入：

```powershell
./scripts/Build-Android.ps1 -QtAndroidDir $env:QT_ANDROID_DIR -QtHostDir $env:QT_HOST_PATH `
    -AndroidNdkDir $env:ANDROID_NDK_ROOT -AndroidSdkDir $env:ANDROID_HOME -JavaHome $env:JAVA_HOME
```

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

## 桌面操作

`Ctrl+O` 打开，`Ctrl+N` 创建，`Ctrl+E` 解压，`Ctrl+F` 聚焦搜索，`Ctrl+J`
打开任务中心，`Ctrl+W` 关闭归档。文件列表支持方向键、Home/End、空格切换
选择及 Ctrl+A。搜索中的 Escape 清空搜索；任务中的 Escape 请求取消。
点列表表头排序，重复点击切换方向；“归档顺序”恢复原始条目顺序。更改搜索
或排序会清空选择，全选仅选择当前可见条目；“解压全部”仍指整个归档。

拖入单个文件时，在工作线程根据内容探测归档；普通文件和多文件/目录进入
创建对话框。网络 URL 不接受。拖出时先选择条目，使用“导出”准备文件
（加密条目可输入密码）；准备成功后按住选中行或导出按钮拖入目标文件夹。
准备在后台安全解压，跳过链接并保留目录结构，只提供复制操作。每次导出
最多 16 GiB、两小时和 10,000 个顶层文件 URL；最多保留四个临时导出目录。
系统拖放返回后暂存文件保留十分钟，便于桌面继续复制；选择更改会使已准备
的拖放失效，失败、取消和退出会尽力清理。清理失败会显示具体残留路径。

Windows 可把生成式测试数据放在指定的 SSD 目录，设置只影响测试子进程：

```powershell
./scripts/Test-Desktop.ps1 -BuildDirectory out/build/m4-msvc -TemporaryRoot D:/Cache/Codex/AnAnArchiver-M4
```

`-BuildDirectory` 可使用任何已配置的应用构建目录。省略 `-TemporaryRoot` 时
使用操作系统临时目录；没有把开发机器路径写入程序或 CMake 默认配置。

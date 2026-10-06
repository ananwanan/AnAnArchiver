# AnAnArchiver

跨平台归档管理器，使用 C++23、Qt 6 Quick/QML 和固定版本的
[ananwanan/libarchive](https://github.com/ananwanan/libarchive)。

本次重构完成 AGENTS.md 第一里程碑：独立 Core、libarchive RAII 适配器、
结构化错误、内容检测、流式元数据枚举，以及异步打开归档并显示真实条目。
不传入文件时显示 Home。解压、压缩、密码输入、归档编辑和系统集成属于后续
里程碑，尚未提供相应业务实现。

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
未读取的 payload 损坏需要后续 TestArchive 功能验证。需要密码才能读取目录的
归档目前返回 PasswordRequired，不保存或记录密码。

测试动态生成小型归档，不向 Git 提交大型测试数据。覆盖内容识别、常见格式、
Unicode 文件与 entry 路径、无效/截断输入、枚举取消、metadata 限制、任务
背压/并发限制/异常隔离/退出清理，以及按需目录扫描。危险 entry 路径与链接
目前只作为元数据展示，归档打开不会写出文件；安全解压回归测试属于第二里程碑。

桌面程序支持 `--smoke-test <milliseconds>` 验证加载启动，以及
`--smoke-archive <archive>` 验证 Qt → Application → Backend → 条目模型的
完整打开链路并以进程退出码报告成功或失败。

归档文件名、目录名、路径和 backend 错误详情统一按纯文本显示，避免将恶意
文件名解释为 HTML。渲染回归测试包含实际图片请求检测和正向对照，验证文件名
中的 `<img>` 不会触发额外文件或网络读取。

架构说明见 [Core 与前端边界](docs/architecture/0001-core-and-frontend.md) 和
[受管理索引任务](docs/architecture/0002-managed-indexing.md)。

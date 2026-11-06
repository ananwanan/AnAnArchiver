# AGENTS.md

2026-11-05 前端决策：`main` 使用 Flutter/Dart 开展后续前端开发，复用现有 C++23
Application/Core 和 libarchive。迁移前的 Qt 版本保存在
`codex/qt-baseline-2026-11-05`（`53185742ad1e640b24da397cfa3544507af4edef`）。
本次先切换需求与文档；现存 Qt 源码、构建入口和历史验收仍属于迁移基线，
不能据此宣称 Flutter 已实现或通过验收。迁移方案见
[Flutter 前端决策](docs/architecture/0007-flutter-frontend.md)。

开发进度：F1 C ABI v1 已实现，并完成 Windows x64 Release 的 C 消费者及
Core/C ABI 回归；F2 公共 metadata 浏览已实现并进行 Windows x64 验证。
F3 Flutter/Dart UI 与独立 binding 已实现，并验证 Windows x64 Release 与真实 ZIP
浏览链路；F4 已接入真实解压/创建、密码、覆盖交互、进度与取消并完成 Windows x64
验证，见 [F4 验证记录](docs/validation/2026-11-06-flutter-operations.md)。
F5 已接入设置迁移、最近记录、任务中心、快捷键、系统打开与 OS 文件拖放；
Windows 自动化与原生链路、Explorer 单文件拖出目标内容已验证；目录及完整 OS 取消/退出组合尚待闭环，
见 [F5 验证记录](docs/validation/2026-11-07-flutter-desktop.md)。
Linux/macOS 尚未原生验证，后续为平台补验及 F6。F3 证据见
[Flutter 验证记录](docs/validation/2026-11-06-flutter-browsing.md)。F1/F2 证据见
[F1 验证记录](docs/validation/2026-11-05-c-api.md) 和
[F2 验证记录](docs/validation/2026-11-06-metadata-browsing.md)。

## 1. Project Overview

项目名称：**轻匣（LiteArc）**

仓库：

- Application: `https://github.com/ananwanan/AnAnArchiver`
- Archive backend: `https://github.com/libarchive/libarchive`

轻匣（LiteArc）是一个面向 Windows、Linux、macOS 的现代跨平台压缩/解压缩桌面软件。

项目目标不是简单地给 `libarchive` 套一层 GUI，而是构建一个：

- 美观、现代的桌面 UI
- 高性能
- 低内存占用
- 支持大文件和大型压缩包
- 响应式、无 UI 卡顿
- 安全
- 可测试
- 可维护
- UI 与压缩核心解耦
- 可以长期演进

的完整归档管理器。

目标体验可以参考：

- 7-Zip
- NanaZip
- Bandizip
- PeaZip
- macOS Archive Utility

但不得复制任何产品的 UI、素材或代码。

---

# 2. Core Principle

整个项目必须遵守以下原则：

> Archive Core、Application 与 GUI 完全解耦。

正确依赖方向：

```text
Flutter UI / Dart Controller / ViewModel
    ↓
Dart FFI binding
    ↓
ananarchiver_c_api（稳定 C ABI）
    ↓
C++ Application（ArchiveService / TaskManager / metadata queries）
    ↓
C++ Core（Reader / Writer / policies / errors / cancellation）
    ↓
libarchive adapter（RAII）
    ↓
libarchive/libarchive
```

GUI 和 Dart FFI 不得直接调用 libarchive，也不得持有 `archive*` 或 `archive_entry*`。
旧 Qt 基线仍通过 Qt Controller → Application 访问归档功能；它不进入 Flutter 依赖链。
平台窗口、文件选择、系统拖放和文件打开事件由 frontend/platform adapter 处理。

---

# 3. Language and Toolchain

核心代码优先使用：

```text
C++23
```

构建系统：

```text
CMake
```

代码必须能够使用：

```text
MSVC
Clang
GCC
```

进行构建。

最低目标平台：

```text
Windows 10/11 x64
Linux x86_64
macOS x86_64
macOS arm64
```

macOS x86_64 仍是兼容目标。Flutter 官方已公布 Intel Mac 弃用规划，迁移时必须
检查最新稳定 SDK 的实际支持，并提供对应构建与原生运行证据；不支持时明确报告
平台范围决策，未解决前不能宣称全平台迁移完成，也不能静默固定旧 SDK。

后续可以扩展：

```text
Linux ARM64
Windows ARM64
```

不要为了当前开发机器写死任何路径、SDK 或平台配置。

禁止出现类似：

```cpp
"S:/Folder"
"D:/libarchive"
"C:/Qt/..."
"/home/user/..."
```

的生产代码。

---

# 4. UI Technology Decision

主线前端方案为：

```text
Flutter（官方最新稳定版）
Dart（随所选 Flutter SDK 配套版本）
dart:ffi + ananarchiver_c_api
C++23 + CMake + libarchive
```

`main` 承载 Flutter 开发。Qt Quick/QML 是保留在 `codex/qt-baseline-2026-11-05`
的现有版本，不再作为新功能的默认前端。迁移期间允许现有 Qt 代码和构建继续存在，
用于行为对照和回归；Flutter 达到功能、性能及平台验收前不机械删除它们。

迁移复用压缩、解压、流式 IO、安全策略、任务、取消和结构化错误；不能用 Dart
重新实现这些核心功能。界面美观、压缩性能、内存、启动和包体积都必须实际验证，
不得仅依据框架选择作更快、更小的承诺。

Archive Core、Application 和 C ABI 不依赖 Qt、Flutter 或 Dart runtime。
Core 中禁止 `QString`、`QVector`、`QObject`、`QFile`、`QDir`、`QDateTime`、
`QThread`、`QFuture`、`QAbstractItemModel`，继续使用标准 C++ 类型：

```cpp
std::string
std::u8string
std::filesystem::path
std::vector
std::chrono
std::jthread
std::expected
std::span
std::optional
std::unique_ptr
std::shared_ptr
```

Qt 依赖仅允许留在旧 `frontend/qt/` 或明确的 Qt adapter 中。

---

# 5. Flutter Policy

Flutter 是已确定的主线前端，必须提供稳定的 `ananarchiver_c_api` C ABI。

- C ABI 包装 Application；仅暴露 opaque handles、固定宽度数值、长度明确的
  UTF-8 数据和结构化错误，不导出 STL、C++ 类或 libarchive 指针。
- 明确 ABI 版本、调用约定、结构大小、对象所有权、字符串/批次释放接口及线程规则；
  C++ 异常不得越过 ABI。跨库分配的数据由提供方释放。
- 打开、索引、查询、压缩、解压和清理由受管理 worker 执行；FFI 提交返回任务 ID，
  UI isolate 不等待 IO、排序、线程 join 或完整解压。
- 任务快照、条目页和事件均有界；轮询或异步通知必须合并、限流并可取消。
  如使用跨线程 Dart callback，必须处理回调关闭、资源寿命和退出次序。
- 完整 metadata 索引留在 C++；Flutter 获取当前目录的分页/窗口结果和有限缓存。
  不向 Dart 复制完整百万条目索引，不逐条 FFI 获取字段。
- 从 Qt Model 抽离目录构建、查询、原始条目身份和选择解析，复用至 Application；
  保留 Unicode 搜索语义、隐式目录和资源预算，禁止两套业务逻辑长期分叉。
- Flutter widgets、系统文件拖放、文件选择和窗口集成通过 frontend/platform adapter
  接入。原生插件不能绕过 Application 直接操作 backend。

绑定可由 `ffigen` 从 C 头文件生成；Flutter SDK、配套 Dart、插件和构建工具遵循
最新稳定依赖与实际构建可追踪策略。实施顺序与验收见第 99 节和 Flutter ADR。

---

# 6. Other UI Frameworks

```text
Flutter      = 主线正式前端
Qt Quick/QML = 已保存的旧版与迁移对照基线
Slint        = 可研究
Tauri        = 不作为默认方案
Avalonia     = 可研究
Dear ImGui   = 仅用于内部调试、benchmark 或诊断
```

不同时推进多套正式 frontend；Qt 基线的存在不改变 `main` 的 Flutter 开发方向。

---

# 7. Repository Architecture

迁移后的目标目录（尚未实现的目录不能当作现有功能）：

```text
LiteArc/
├── AGENTS.md
├── README.md
├── CMakeLists.txt
├── CMakePresets.json
├── cmake/
├── include/ananarchiver/
│   ├── application/
│   └── c_api/                 # 已实现：ABI v1 C 头文件
├── src/
│   ├── core/
│   ├── backend/libarchive/
│   ├── application/           # 现有服务 + 公共 metadata 查询
│   ├── c_api/                 # 已实现：C ABI、结果所有权与异步清理
│   └── platform/              # OS 归档 IO / 公共平台服务
├── frontend/
│   ├── flutter/               # 已实现：主线 Flutter 归档浏览
│   │   ├── lib/               # pages / widgets / controllers / models / l10n
│   │   ├── packages/          # FFI binding / 桌面平台 adapter
│   │   ├── assets/
│   │   ├── test/
│   │   ├── integration_test/
│   │   ├── windows/
│   │   ├── linux/
│   │   └── macos/
│   └── qt/                    # 迁移期间保留的旧实现
├── tests/
│   ├── core/
│   ├── archive/
│   ├── security/
│   ├── application/
│   └── c_api/                 # 已实现：C 消费者 / 生命周期 / 错误 / 取消
├── benchmarks/                # 规划：两种 Release 前端对比
├── docs/
│   ├── architecture/
│   ├── development/
│   └── validation/
└── third_party/
```

不一次性机械搬迁全部目录；每阶段保持 Core 可独立构建、已有测试不回归。
Flutter app 通过自己的构建入口集成 CMake 产出的 C ABI，不能将 Qt 带入新运行时。

---

# 8. libarchive Is the Archive Backend

LiteArc 必须优先使用：

```text
https://github.com/libarchive/libarchive
```

而不是默认使用操作系统中的随机版本。

该仓库是 LiteArc 的 libarchive 上游来源。

使用该仓库的最新版本，不固定历史版本号、tag 或 commit。

优先使用该仓库的最新正式发布版；如果没有独立维护的正式发布版，使用默认分支的最新提交。

Release 构建必须记录实际使用的 libarchive 版本和 revision，便于诊断和追踪。

推荐方式：

```text
Git Submodule
```

例如：

```text
third_party/libarchive
```

Git Submodule 用于管理源码来源。父仓库 gitlink 可以记录一次依赖解析的结果，但不能作为长期固定版本的要求；初始化或更新依赖时应重新获取最新版本。

不要让正式构建依赖：

```text
find_package(LibArchive)
```

然后不受控制地链接用户机器里的任意版本。

可以提供开发选项：

```cmake
ANANARCHIVER_USE_SYSTEM_LIBARCHIVE
```

例如：

```text
OFF = 默认，从指定项目上游获取最新版本
ON  = 开发者手动使用系统 libarchive
```

---

# 9. Do Not Fork libarchive Logic Into Application

禁止复制：

```text
archive_read_*.c
archive_write_*.c
ZIP parser
7z parser
RAR parser
TAR parser
```

到 LiteArc。

格式解析应由：

```text
libarchive/libarchive
```

负责。

如果发现 libarchive bug：

1. 确认是否是 LiteArc adapter 问题。
2. 添加最小复现。
3. 如果确实属于 libarchive，则修复 `libarchive/libarchive`。
4. 不要在 GUI 中增加奇怪 workaround。
5. 如果必须 workaround，要明确注释原因和对应 issue/commit。

---

# 10. libarchive Wrapper

禁止在 Application 层散布：

```cpp
archive_read_new();
archive_read_support_filter_all();
archive_read_support_format_all();
archive_read_open_filename();
archive_read_next_header();
archive_read_data();
archive_read_free();
```

创建 C++ RAII wrapper。

例如概念上：

```cpp
class ArchiveHandle
{
public:
    ArchiveHandle();
    ~ArchiveHandle();

    ArchiveHandle(const ArchiveHandle&) = delete;
    ArchiveHandle& operator=(const ArchiveHandle&) = delete;

    ArchiveHandle(ArchiveHandle&&) noexcept;
    ArchiveHandle& operator=(ArchiveHandle&&) noexcept;

private:
    archive* handle_{};
};
```

所有：

```text
archive*
archive_entry*
```

生命周期必须明确。

异常、early return、取消任务后不能泄漏资源。

---

# 11. ArchiveReader API

Core 层设计类似：

```cpp
class ArchiveReader
{
public:
    virtual ~ArchiveReader() = default;

    virtual Result<ArchiveInfo> open(
        const std::filesystem::path& file) = 0;

    virtual Result<void> enumerate(
        const EntryCallback& callback,
        CancellationToken cancellation = {}) = 0;

    virtual Result<void> extract(
        const ExtractionRequest& request,
        ProgressCallback progress,
        CancellationToken cancellation = {}) = 0;

    virtual Result<void> test(
        ProgressCallback progress,
        CancellationToken cancellation = {}) = 0;
};
```

实际 API 可以调整。

重点是：

```text
UI 不知道 libarchive
Application 不知道 archive*
ArchiveReader 不知道 Flutter、Dart 或 QML
```

---

# 12. ArchiveWriter API

压缩功能通过统一 API 提供。

概念接口：

```cpp
struct CompressionRequest
{
    ArchiveFormat format;
    CompressionMethod method;
    int level;

    std::vector<std::filesystem::path> inputPaths;
    std::filesystem::path destination;

    std::optional<std::string> password;

    bool preservePermissions = true;
    bool preserveTimestamps = true;
};
```

不同格式能力不同。

绝对不能假设所有格式都支持：

```text
密码
压缩等级
多线程
solid archive
AES
修改
追加
```

必须通过：

```text
ArchiveCapabilities
```

描述能力。

UI 根据 capabilities 动态显示/禁用选项。

---

# 13. Supported Formats

格式支持能力以当前：

```text
libarchive/libarchive
```

实际功能为准。

目前应重点支持良好的读取体验：

```text
ZIP
ZIPX
7z
RAR
RAR5
TAR
TAR.GZ
TGZ
TAR.XZ
TAR.ZST
GZIP
BZIP2
XZ
ZSTD
LZ4
CAB
CPIO
AR
ISO9660
LHA/LZH
XAR
```

创建格式必须根据当前 libarchive writer capabilities 判断。

不要因为某格式可读取，就认为它也可创建。

例如 RAR 应首先视为：

```text
Read-only
```

除非 libarchive backend 未来明确提供写支持。

---

# 14. Format Detection

打开文件时不能仅依赖扩展名。

必须优先：

```text
Content detection
```

扩展名主要用于：

```text
File Dialog
File Association
UI icon
default output naming
```

真正格式判断使用 libarchive detection。

比如：

```text
abc.xxx
```

只要内容实际是 ZIP，也应该能够识别。

---

# 15. Streaming First

libarchive 本质上是 stream-oriented library。

设计时必须尊重这一点。

不要假设所有归档格式都支持真正随机访问。

对于：

```text
tar.gz
tar.xz
tar.zst
```

不能为了模拟 ZIP 一样的随机访问而写出极度低效的代码。

Archive browser 应建立：

```text
Entry metadata index
```

但不能自动将全部文件内容读入内存。

---

# 16. Large Archive Support

程序必须考虑：

```text
10 GB archive
100 GB archive
100,000 entries
1,000,000 entries
```

这种场景。

禁止：

```cpp
readWholeFile();
```

禁止将完整压缩包加载到 RAM。

禁止将每个文件内容缓存到 RAM。

文件内容采用 streaming。

Entry metadata 可以建立索引，但需要控制对象和字符串开销。

大型列表必须使用：

```text
virtualized view
batch updates
incremental loading
```

不要一次向 GUI 发出几十万次同步更新。

---

# 17. UI Thread Must Never Perform Archive Work

GUI thread 禁止执行：

```text
扫描大型目录
枚举大型压缩包
CRC 校验
解压
压缩
文件复制
大量文件 metadata 查询
Hash
```

这些必须运行在 worker/task system 中。

UI thread 只负责：

```text
rendering
input
small state updates
```

任何可能超过几毫秒的 IO 操作都应该警惕。

---

# 18. Task System

实现统一 Task 系统。

任务类型包括：

```text
OpenArchiveTask
ScanArchiveTask
ExtractTask
CompressTask
TestArchiveTask
DeleteTask
CopyTask
```

任务至少具有：

```text
id
type
state
progress
currentFile
processedBytes
totalBytes
processedEntries
totalEntries
speed
elapsedTime
estimatedRemaining
error
```

状态：

```text
Pending
Running
Paused
Cancelling
Cancelled
Succeeded
Failed
```

如果底层操作无法真正 pause，不允许伪装支持 pause。

---

# 19. Cancellation

长时间任务必须支持取消。

使用类似：

```cpp
std::stop_token
```

或项目自己的：

```text
CancellationToken
```

取消操作必须：

```text
及时
线程安全
资源安全
```

取消不能导致：

```text
archive handle 泄漏
半初始化对象
文件句柄泄漏
线程泄漏
崩溃
```

如果留下不完整输出文件，应根据操作策略删除或明确标记。

---

# 20. Threading Rules

不要共享：

```text
archive*
```

给多个线程同时使用。

原则：

```text
one archive handle
=
one logical archive operation
```

不同压缩包任务可以并行运行。

单一压缩包是否能并行处理必须根据格式和 backend 能力决定。

不要为了“高性能”盲目给 libarchive API 套多线程。

性能首先来自：

```text
Streaming IO
buffering
减少内存复制
减少 UI 更新
合理的 codec
异步 IO pipeline
```

而不是无脑增加线程。

---

# 21. Important libarchive Thread Safety

使用 libarchive 时必须注意线程安全边界。

尤其是使用：

```text
archive_write_disk_*
```

相关能力时，需要确认当前平台的线程安全行为。

在 POSIX 上，如果涉及影响进程级：

```text
umask
chdir
```

等操作，不允许多个 extraction task 无保护并发调用相关代码。

如必须使用相关 API，使用集中式同步机制，而不是让每个调用点自己加 mutex。

---

# 22. Performance

性能是一级需求。

优化顺序：

```text
Correctness
↓
Measure
↓
Algorithm
↓
IO
↓
Memory allocation
↓
Copy reduction
↓
Concurrency
↓
Micro optimization
```

禁止凭感觉优化。

建立：

```text
benchmarks/
```

至少测试：

```text
ZIP enumerate
7z enumerate
TAR.GZ enumerate

ZIP extract
7z extract
TAR.ZST extract

ZIP creation
7z creation

many-small-files
few-large-files
```

记录：

```text
wall time
CPU time
peak memory
throughput
entry count
```

---

# 23. Performance Target

性能目标不是要求超过 codec 自身极限。

LiteArc 应尽量让应用自身额外开销保持很低。

目标：

```text
Archive Core overhead:
尽量 < backend/codec 总耗时的 5~10%
```

这是性能目标，而不是硬性 SLA。

如果存在明显性能问题：

```text
先 profile
后优化
```

不要通过删除错误处理或安全检查换性能。

---

# 24. Buffering

不要在循环中频繁：

```cpp
new
delete
malloc
free
```

大块数据传输尽量复用 buffer。

例如：

```text
64 KiB
128 KiB
256 KiB
1 MiB
```

实际最佳大小通过 benchmark 决定。

不要默认认为 buffer 越大越快。

---

# 25. Memory Allocation

高频 metadata 对象注意：

```text
string allocation
vector reallocation
QObject allocation
signal emission
```

Core metadata 类型保持 lightweight。

例如：

```cpp
struct ArchiveEntry
{
    std::string path;
    std::uint64_t size;
    std::uint64_t compressedSize;
    EntryType type;
    std::chrono::system_clock::time_point modified;
};
```

不要让每个 ArchiveEntry 都继承 QObject。

---

# 26. Error Model

禁止使用：

```cpp
bool extract();
```

然后错误原因全部丢失。

Core 应提供结构化错误。

推荐：

```cpp
enum class ArchiveErrorCode
{
    None,

    FileNotFound,
    PermissionDenied,
    UnsupportedFormat,
    UnsupportedCompression,
    InvalidArchive,
    CorruptedArchive,
    WrongPassword,
    PasswordRequired,

    DiskFull,
    OutputExists,

    Cancelled,

    IoError,
    BackendError,
    Unknown
};
```

可以使用：

```cpp
std::expected<T, ArchiveError>
```

或等价实现。

ArchiveError 应包含：

```text
code
message
backendMessage
path
```

但不能泄露密码等敏感信息。

---

# 27. Security Is Mandatory

Archive extraction 是安全敏感操作。

必须防御：

```text
Zip Slip
path traversal
absolute path escape
symlink escape
hardlink escape
malicious filenames
decompression bomb
resource exhaustion
corrupted archive
```

压缩包里的：

```text
../../Windows/System32/test
/etc/passwd
C:\Windows\...
\\server\share
```

不能突破用户选择的目标目录。

所有输出路径必须经过安全规范化检查。

---

# 28. Extraction Root

所有 entry 最终目标必须验证：

```text
canonicalTarget
```

位于：

```text
canonicalExtractionRoot
```

内部。

不要简单使用：

```cpp
outputRoot / archivePath
```

然后直接写入。

需要处理：

```text
..
.
absolute path
drive prefix
UNC path
symbolic link
```

等情况。

---

# 29. Symlink Policy

压缩包中的 symlink/hardlink 必须采用明确策略。

至少支持：

```text
Safe
Preserve
Skip
```

默认使用安全行为。

不能允许恶意 symbolic link 先创建：

```text
foo -> ../../
```

然后后续 entry：

```text
foo/evil.exe
```

写出 extraction root。

---

# 30. Overwrite Policy

解压覆盖策略必须明确：

```text
Ask
Overwrite
Skip
Rename
OverwriteIfNewer
```

对于批量冲突，UI 可以支持：

```text
Apply to all
```

Core 不应弹 UI。

Core 通过 callback/request 返回冲突。

---

# 31. Atomic Output

创建压缩文件时尽可能：

```text
destination.tmp
↓
finish
↓
fsync/close
↓
rename
```

不要一开始就破坏用户已经存在的：

```text
archive.zip
```

操作失败后应尽可能保留原文件。

---

# 32. Password Handling

密码不得：

```text
写日志
写 crash report
写 settings
打印到 console
```

除非用户明确启用安全的 password manager 集成，否则默认只保存在任务生命周期内。

密码生命周期结束后，应在合理可行范围内清理内存。

不要因为 C++ `std::string` 无法提供绝对内存擦除保证，就完全放弃安全处理。

---

# 33. Logging

实现统一日志接口。

等级：

```text
Trace
Debug
Info
Warning
Error
Critical
```

日志需要包含：

```text
timestamp
thread
module
operation
```

但不得包含：

```text
password
encryption key
private token
完整敏感文件内容
```

Release 默认不要产生海量 Trace 日志。

---

# 34. Existing Project Migration

当前已有 `include/ananarchiver`、`src/core`、`src/backend/libarchive`、
`src/application` 和 `frontend/qt`。Core/Application 已不依赖 Qt。
旧 `AppContext` 阶段的结构不再作为当前目录现状描述。

迁移先区分可复用的 C++ 服务、需抽离的 metadata/选择逻辑和需重写的 UI adapter。
保留现有安全与 Core 测试；不重写 libarchive parser、解压事务或压缩管线。
`ArchiveEntryModel` / `ArchiveFolderModel` 的公共目录索引与查询行为移至 Application，
Qt Model 和 Dart ViewModel 都只适配数据。`FileTreeModel` 属于独立文件系统浏览。

---

# 35. No Hardcoded Test Path

现有 Qt 基线不传文件时显示 Home，Flutter 必须保持该行为。
生产代码禁止自动扫描开发者路径；测试目录由 fixture 或显式参数提供。

---

# 36. Filesystem Directory Browsing

现有 `FileTreeModel` 通过 `DirectoryService` 在 worker 按需获取直接子节点。
Flutter 沿用 lazy loading，只在加载/展开目录时请求 children；禁止为显示树而
在 UI isolate 递归扫描整个磁盘。真实文件系统树与归档 metadata 目录树必须独立。

---

# 37. Remove Fake UI Data

Flutter 和保留的 Qt 生产 UI 均禁止固定条目数量、伪造文件名或假进度。
Mock data 只允许放在 tests、demo、preview 或显式 development-only model 中。
界面必须呈现实际 Application 状态；未实现功能不能以成功结果占位。

---

# 38. Flutter Frontend Architecture

```text
Flutter Widget
    ↓
Dart Controller / ViewModel
    ↓
Dart FFI binding
    ↓
ananarchiver_c_api
    ↓
C++ Application Service
    ↓
Core / Backend
```

按归档、压缩、任务、设置和平台集成拆分职责，禁止万能 AppContext 或单个巨型
ViewModel。UI 状态与 C++ 对象生命周期分离；页面 dispose 不得留下失效 callback，
也不得令任务输出越过清理策略。错误本地化与数值格式化在前端完成。

---

# 39. Flutter Model and Large View Rules

使用按需构建的虚拟化列表、TableView/TreeView 或等价组件。
按当前目录分页/窗口读取，维护有界缓存和稳定 row identity；批量通知并局部刷新。
不能一次性构建所有行，不逐条发送百万次状态通知，不在 build() 中排序或 IO。
完整 C++ 索引不得在 Dart 再常驻一份；视图虚拟化不能替代数据内存预算。
在 100k / 1M metadata 场景验证滚动、导航、搜索、排序、选择和峰值内存。
旧 Qt adapter 的批量插入规则仍适用于迁移期回归。

---

# 40. Modern UI Direction

LiteArc UI 应：

```text
简洁
现代
克制
快速
高信息密度
```

不要把它设计成传统 Windows XP 风格压缩软件。

也不要做成充满：

```text
大渐变
玻璃特效
巨大动画
过量圆角
```

的展示 Demo。

核心页面建议结构：

```text
┌───────────────────────────────────────────────┐
│ Title / Window controls                       │
├───────────────────────────────────────────────┤
│ Open  Compress  Extract  Test  Delete   ⋯     │
├───────────────────────────────────────────────┤
│ ← → ↑    archive.zip / folder                 │
├───────────────┬───────────────────────────────┤
│               │ Name       Size     Modified │
│ Folder tree   │                               │
│ (folders only)│ Files and subfolders          │
│               │ (direct children only)        │
│               │                               │
├───────────────┴───────────────────────────────┤
│ Status / entries / total size / task status   │
└───────────────────────────────────────────────┘
```

打开压缩包后，左侧只显示归档内文件夹树，右侧显示当前文件夹的直接子文件和
子文件夹，文件夹优先显示；名称列显示名称，不平铺其他层级的后代条目。
目录树必须包含由条目路径推导出的父目录，以及归档显式记录的空目录。
右侧同样显示隐式父目录和显式空目录，同一目录只显示一行；支持双击或 Enter
进入子文件夹。文件夹可选择、解压和拖出，隐式目录不能伪造归档条目编号。
目录树构建必须限制深度、节点数量和累计路径内存；超限应明确报错，不能将
部分目录树当作完整结果，也不能继续对无效视图执行选中操作。
提供归档根目录入口、当前目录显示和返回上级操作；切换目录时清空文件选择，
并使过期查询结果失效。文件系统目录浏览仍使用独立的按需扫描模型。

支持：

```text
Light
Dark
Follow System
```

---

# 41. UI Features

最终目标包括：

```text
打开压缩包

浏览压缩包

归档文件夹树与当前文件夹文件列表

归档根目录与返回上级导航

排序

搜索

多选

全选

拖拽打开

拖出解压

拖入压缩

解压全部

解压选中

快速解压

创建压缩包

测试压缩包

密码输入

任务进度

错误详情

覆盖策略

主题

最近文件

文件关联
```

功能按 milestone 实现，不要求一次全部完成。

---

# 42. Archive Editing

不要假装能够“原地修改”所有 archive。

libarchive 本身是 stream-oriented。

对于：

```text
删除 archive 中文件
重命名 archive 中文件
加入文件
修改文件
```

通常应通过：

```text
read old archive
↓
write new archive
↓
skip/modify selected entries
↓
atomic replace
```

实现。

UI 可以表现为编辑操作，但 Core 必须正确处理 rewrite 成本。

---

# 43. Search

Archive search 首先针对已经索引的 metadata。

例如：

```text
path
filename
extension
size
date
type
```

不要为了搜索文件名重复扫描完整 compressed data。

超大压缩包搜索必须异步。

归档浏览中的搜索和排序限定在当前文件夹的直接子文件和子文件夹，切换目录后重新查询，
并清空之前的选择。过滤和排序不得改变条目的原始归档索引，确保同名条目的
选中解压和拖出仍定位原始条目；文件夹选择包含其后代，路径匹配必须按目录
边界进行并经过已有安全验证。全选只作用于当前可见条目，解压全部仍作用于整个归档。

---

# 44. Preview

未来可提供：

```text
text preview
image preview
metadata preview
```

但 preview 必须：

```text
有大小限制
异步
可取消
```

不要为了 preview 一个 20 GB 文件把整个 entry 解压到 RAM。

---

# 45. Temporary Files

需要临时文件时，必须通过统一：

```text
TempFileManager
```

管理。

程序退出、任务失败、任务取消后尽可能清理。

不要散落：

```text
/tmp/test
C:\Temp\abc
```

这种路径。

---

# 46. Platform Layer

操作系统特定代码必须放在：

```text
src/platform/windows
src/platform/linux
src/platform/macos
```

而不是：

```cpp
#ifdef _WIN32
...
#elif __linux__
...
#elif __APPLE__
...
#endif
```

散落整个 Core。

少量不可避免的兼容宏可以例外。

---

# 47. Windows Integration

Windows 后续需要支持：

```text
Open With
File Association
Explorer context menu
Drag & Drop
Unicode paths
Long paths
Recycle Bin integration
native notifications
```

不要假设：

```text
MAX_PATH == 260
```

内部文件路径必须能够处理现代 Windows long path。

---

# 48. Linux Integration

Linux 后续考虑：

```text
XDG directories
.desktop
MIME association
Wayland
X11
GNOME/KDE
file manager integration
```

不要硬依赖某个 Desktop Environment。

---

# 49. macOS Integration

macOS 后续考虑：

```text
.app bundle
Finder integration
drag & drop
file association
Apple Silicon
x86_64
```

Core 不得依赖 Objective-C。

平台 adapter 可以使用：

```text
Objective-C++
```

如果确有必要。

---

# 50. Unicode

内部统一按照 Unicode 设计。

Core 文件路径优先使用：

```cpp
std::filesystem::path
```

文本与 archive entry pathname 的编码转换必须集中处理。

不能到处：

```cpp
QString::toLocal8Bit()
std::string(...)
```

随意转换。

必须测试：

```text
中文
日文
韩文
emoji
accented characters
非常长文件名
```

例如：

```text
安安.txt
可可🐄🐈.png
こんにちは.zip
```

---

# 51. CMake and Flutter Build

C++ 构建继续使用 target-based CMake：`target_link_libraries()`、
`target_include_directories()`、`target_compile_features()`、
`target_compile_definitions()`，避免全局 include/link/definitions。

目标依赖：

```text
ananarchiver_c_api → ananarchiver_application → Core / Backend
```

保留 `ananarchiver_core`、`ananarchiver_libarchive`、`ananarchiver_application`
等现有标识；已新增 `ananarchiver_c_api`。平台独立构建它时不搜索 Qt/Flutter SDK。
共享库导出、PIC、MSVC CRT 和架构配置必须匹配，C ABI 导出的内存由同一库释放。
Flutter 集成通过 build hooks 或平台 runner 调用既有 CMake pipeline，不另建一套
backend/codec 依赖解析。`ANANARCHIVER_BUILD_QT` 在旧构建兼容期间保留。

---

# 52. Frontend Runtime Dependencies

Flutter 发布包只携带实际需要的 Flutter engine、Dart AOT 产物、应用资源、
C ABI/backend、平台插件及许可。Core-only 不要求任何 GUI SDK。
Qt 仅供旧基线构建；迁移期间不得无理由增加 Qt Widgets 或把 Qt 引入 Flutter 包。
现有 Qt 打包脚本与 desktop presets 在明确改造前仍只适用于 Qt 基线。

---

# 53. Third-party Dependencies

依赖原则：

```text
少
稳定
最新正式版本
可维护
可追踪
```

所有选用的第三方依赖采用官方最新稳定发布版，不固定历史版本号或提交。

默认不选择 alpha、beta、RC 或 nightly；没有正式发布版的项目使用约定上游默认分支的最新提交。

Flutter 使用官方最新 stable channel，Dart 使用同一 SDK 配套版本；插件选择支持目标平台
的最新稳定版。提交应用 pubspec.lock 以记录本次解析，后续更新重新解析最新兼容稳定版。
若有不兼容必须明确修复/报告，不能静默降级。构建记录包括 SDK/engine revision、Dart、
插件版本和原生依赖来源。Mbed TLS framework 跟随所选 Mbed TLS 发布版。
维护旧 Qt 基线时，其模块及内嵌依赖仍跟随对应 Qt 正式发布版，不能混用。

编译器、CMake、Ninja、Python 和平台 SDK 优先使用满足目标平台与 C++23 要求的最新稳定工具链；最低兼容版本声明不是固定版本要求。

不要为了一个几十行能完成的功能引入巨大库。

Archive codec/backend 优先由：

```text
libarchive/libarchive
```

统一管理。

不要同时引入：

```text
libzip
7zip SDK
unrar
minizip
```

然后制造四套 Archive API。

除非未来出现明确且经过 benchmark/功能验证的需求。

---

# 54. Testing

任何 Archive Core 功能都应该可在不启动 GUI 的情况下测试。

至少包含：

```text
open
enumerate
extract
compress
test
cancel
invalid archive
encrypted archive
corrupted archive
unicode path
large files
path traversal
symlink
overwrite
```

GUI 测试不能替代 Core 测试。
Flutter 迁移另需 C ABI 版本/所有权/错误边界、批次释放、取消/退出、过期查询、
覆盖冲突回答和密码寿命测试；Dart binding、widget 与真实归档集成测试分别验证。
既有 Qt 测试是基线证据，不能替代 Flutter 原生平台验收。

---

# 55. Test Archives

在：

```text
tests/data/
```

维护小型测试 archive。

不要提交：

```text
几百 MB
几 GB
```

测试文件到 Git。

大型 benchmark 数据通过生成器产生。

例如运行测试时动态创建：

```text
100k small files
1GB sparse/generated stream
nested directories
unicode filenames
```

---

# 56. Security Tests

至少提供针对以下恶意 entry 的测试：

```text
../evil.txt

../../evil.txt

/foo.txt

C:\evil.txt

..\..\evil.exe

folder/../../../evil.txt
```

以及 symlink escape。

这些测试属于必须长期保留的 regression tests。

---

# 57. Corrupted Archive Tests

需要测试：

```text
truncated archive
bad CRC
invalid header
unexpected EOF
invalid encrypted data
unsupported codec
```

不能因为损坏压缩包导致：

```text
crash
assert
infinite loop
OOM
```

---

# 58. Fuzzing

Core 稳定后考虑加入：

```text
libFuzzer
AFL++
```

重点 fuzz：

```text
Archive metadata handling
path normalization
capability mapping
our adapter
```

不要重复 fuzz libarchive 已经直接负责的 parser，除非修改了 backend。

---

# 59. Sanitizers

Linux/macOS Debug/CI 尽可能支持：

```text
AddressSanitizer
UndefinedBehaviorSanitizer
```

可选：

```text
ThreadSanitizer
```

Windows 可以结合：

```text
MSVC AddressSanitizer
```

具体根据 CI 可用性启用。

---

# 60. Compiler Warnings

项目代码应启用严格 warnings。

GCC/Clang 可考虑：

```text
-Wall
-Wextra
-Wpedantic
```

MSVC：

```text
/W4
```

但不要为了追求 `-Werror` 导致第三方 libarchive warning 破坏构建。

第三方代码与本项目 warning policy 分开。

---

# 61. Style

C++：

```text
C++23
RAII
value semantics where reasonable
smart pointers
strong enum
const correctness
```

避免：

```cpp
new
delete
malloc
free
```

散布业务代码。

裸指针可以作为：

```text
non-owning pointer
```

但 ownership 必须明确。

---

# 62. No Premature Abstraction

不要制造：

```text
AbstractArchiveFactoryManagerProvider
ArchiveStrategyFactoryFactory
```

这种没有实际收益的抽象。

抽象应该来自真实边界：

```text
Core / Backend
Core / OS
Application / UI
```

而不是为了设计模式而设计模式。

---

# 63. No Giant Classes

禁止重新制造巨型：

```text
AppContext
ArchiveManager
MainController
Utils
```

单个类承担所有职责。

类应该具有明确责任。

---

# 64. No Utils Dumping Ground

不要创建：

```text
utils.cpp
helpers.cpp
common.cpp
```

然后无限塞函数。

应该按照语义组织：

```text
path_security.cpp
format_detection.cpp
human_size.cpp
unicode.cpp
```

---

# 65. Public API

Core public headers放在：

```text
include/ananarchiver/
```

内部实现留在：

```text
src/
```

不要因为 GUI 需要一个字段就暴露 backend 内部结构。

---

# 66. Naming

正式产品名称统一为：

```text
中文显示名：轻匣
英文产品名：LiteArc
```

中文界面显示“轻匣”，英文界面显示“LiteArc”。窗口标题、关于页面、菜单、
系统集成显示名、安装器和需求/用户文档必须保持一致。

公开可执行文件使用 `LiteArc` / `LiteArc.exe`，macOS bundle 使用 `LiteArc.app`，
Linux portable 启动器使用 `LiteArc.sh`，分发包使用 `LiteArc-<version>-<platform>-<arch>`。
CMake project 名称使用 `LiteArc`。

为保持现有源码、构建、用户设置和安装升级兼容，保留下列技术标识：

- `ananarchiver` namespace、`include/ananarchiver/` 和已有 API 名称。
- `ananarchiver_*` / `AnAnArchiver` CMake targets 与 `ANANARCHIVER_*` 选项。
- 应用组织/设置标识、registry keys、bundle/application IDs。
- 旧 Qt 基线的 QML URI 和资源路径；Flutter 复用品牌资产，明确迁移用户设置，
  不要求新运行时实现 QML URI。
- `share/AnAnArchiver` 安装数据目录。
- GitHub 仓库 `https://github.com/ananwanan/AnAnArchiver`。

这些技术标识不是用户可见产品名，不因品牌变更机械重命名。
历史验证记录、旧产物路径和原始素材生成提示词保留当时的真实名称，明确其历史背景；
不得将旧测试结果描述成 LiteArc 新构建的验证结果。

---

# 67. Internationalization

所有用户可见文本必须能够国际化。

第一阶段：

```text
zh-CN
en
```

Flutter 使用可生成的本地化资源（如 ARB / gen-l10n）管理 zh-CN / en 文案。
不要在业务 C++ 中散落用户文案。

Core error 使用 machine-readable code。

Frontend 根据：

```text
ArchiveErrorCode
```

生成本地化消息。

---

# 68. Accessibility

UI 设计需要考虑：

```text
keyboard navigation
focus
screen scaling
high DPI
contrast
screen readers where practical
```

不要只针对 1920×1080 100% scaling。

至少检查：

```text
100%
125%
150%
200%
```

缩放。

---

# 69. Settings

Settings 属于 Application/Frontend 层。

例如：

```text
theme
language
default extraction directory
overwrite policy
show hidden files
remember window state
recent files
```

不要把 settings 与 Archive Core 强耦合。
Flutter 必须明确旧 QSettings 中主题和最近记录的读取/一次性迁移策略，保留应用身份；
持久化仍在后台执行，密码不得加入设置或迁移数据。

---

# 70. CLI Friendly Core

虽然第一阶段主要做 GUI，但核心功能应允许未来创建：

```text
LiteArc-cli
```

例如：

```bash
LiteArc-cli list file.7z

LiteArc-cli extract file.zip -o ./output

LiteArc-cli create output.zip ./folder

LiteArc-cli test file.rar
```

因此所有核心功能必须独立于 GUI 生命周期。

---

# 71. Benchmark Against Real Workloads

性能比较至少考虑：

```text
SSD
HDD

many small files
large sequential files

compressed data
already-compressed media

ZIP
7z
tar.gz
tar.zst
```

避免拿单个极端 benchmark 得出“更快”的结论。

---

# 72. Progress Calculation

如果 backend 能获得 compressed/uncompressed size，则提供合理进度。

如果无法准确获得总量：

不要伪造：

```text
42%
```

可以显示：

```text
indeterminate progress
entries processed
bytes processed
current filename
elapsed time
```

---

# 73. File Size Formatting

Core 返回：

```text
uint64 bytes
```

不要返回：

```text
"12.4 MB"
```

这种 UI 字符串。

Frontend 决定：

```text
KB / KiB
MB / MiB
locale
decimal places
```

---

# 74. Date Formatting

Core 返回：

```text
std::chrono
```

不要返回：

```text
"2026-07-04 12:00"
```

Frontend 根据 locale 格式化。

---

# 75. Separation of Metadata and UI

错误：

```cpp
struct ArchiveEntry
{
    QString displayName;
    QString icon;
    QColor color;
};
```

正确：

```cpp
struct ArchiveEntry
{
    std::filesystem::path path;
    EntryType type;
    uint64_t size;
    ...
};
```

Icon、颜色、display name 属于 frontend。

---

# 76. Icons

不要在 Flutter 或旧 QML 里大量使用：

```text
📂
📦
📤
🗑
```

emoji 作为正式 UI 图标。

使用统一 icon system。

要求：

```text
SVG
scalable
light/dark theme
consistent stroke
```

并确保许可证允许项目分发。

---

# 77. Drag and Drop

未来支持：

```text
archive → application
files → application
entries → filesystem
```

Drag & Drop 不能在 UI thread 同步执行完整 extraction。

拖出 archive entry 时：

```text
prepare temp extraction asynchronously
```

具体平台行为通过 Flutter 平台 adapter/插件实现；普通 Draggable/DragTarget 不等于
系统文件拖放。必须分别在 Explorer、Finder 和目标 Linux 文件管理器验证。
复用 C++ 安全解压与统一 TempFileManager 策略，提取旧 Qt 临时文件管理依赖，
明确拖动、异步复制、取消、退出和清理失败时的资源寿命。

---

# 78. Shell Integration

Shell integration 属于可选模块。

Core 不得依赖它。

例如 Windows Explorer context menu：

```text
Extract Here
Extract to "folder\"
Open with LiteArc
Compress to ZIP
```

必须能够关闭或不安装。

---

# 79. Package Distribution

最终目标：

Windows：

```text
portable ZIP
installer
```

Linux：

```text
AppImage
.deb
possibly Flatpak
```

macOS：

```text
.dmg / .app
```

但初期首先保证：

```text
clean Core/C ABI CMake build
Flutter release build
```

不要在 Archive Core 尚未稳定时过度投入安装器。

---

# 80. CI

现有 workflow 暂停自动触发，保留的 Qt 作业属于旧基线；本次文档决策不声称已改造 CI。
Flutter 迁移需增加 Dart format/analyze、binding/widget/integration tests、C ABI 与
各平台 Release 构建，复用 Core/compiler/sanitizer 检查和单次依赖解析记录。
GitHub Actions 最终至少覆盖：

```text
Windows + MSVC

Ubuntu + GCC
Ubuntu + Clang

macOS + Clang
```

任务：

```text
configure
build
unit test
```

可增加：

```text
sanitizer
package
benchmark smoke test
```

---

# 81. Formatting

保持统一：

C++：

```text
clang-format
```

CMake：

保持现有可读风格。

Dart：

```text
dart format
flutter analyze
```

旧 QML 保持一致缩进。

不要为了格式化而在功能 PR 中修改几千行无关代码。

---

# 82. Documentation

重要设计决策放在 `docs/architecture/`。当前 Flutter 决策记录为
`0007-flutter-frontend.md`，文档索引为 `docs/README.md`。
README 必须区分目标架构、现有实现和验证状态；尚未存在的 Flutter 构建命令不能
写成当前可运行流程。Qt 基线说明见 `docs/development/qt-baseline.md`。
历史 `docs/validation` 保留原命令、版本、路径和结果，只补背景说明；
不能把历史 Qt 验收改名为 Flutter 验收。只有真正长期有效的决策才创建 ADR。

---

# 83. Development Workflow for Codex

执行任何较大任务前：

1. 阅读 `AGENTS.md`。
2. 阅读相关代码。
3. 理解现有 architecture。
4. 找到最小修改范围。
5. 不要未经检查就重写整个目录。
6. 实现。
7. 编译。
8. 运行测试。
9. 修复 warning/error。
10. 再总结变化。

仅修改文档时验证链接、代码围栏、需求一致性和 diff 范围；如未编译/运行必须如实说明。

---

# 84. Codex Must Inspect Before Editing

例如任务是：

```text
实现 ZIP 打开
```

不要第一步就改 Flutter Widget 或旧 QML。

应该先检查：

```text
CMake
Archive Core
libarchive dependency
application service
C ABI / Dart binding
metadata query / legacy Qt model
Flutter frontend
tests
```

然后决定最小实现路径。

---

# 85. Do Not Leave Fake Implementations

禁止：

```cpp
bool extract(...)
{
    return true;
}
```

禁止：

```cpp
// TODO implement later
```

然后声称功能完成。

如果任务范围无法完成：

明确指出：

```text
implemented
not implemented
limitations
next step
```

---

# 86. No Silent Failure

禁止：

```cpp
catch (...)
{
}
```

禁止忽略：

```text
ARCHIVE_WARN
ARCHIVE_FAILED
ARCHIVE_FATAL
```

必须根据 libarchive return code 正确转换为项目错误模型。

Warning 与 Fatal 不应完全等价，但都不能无声丢失。

---

# 87. Never Block GUI Waiting for Worker

禁止：

```cpp
workerFuture.get();
```

在 UI thread 中等待。

使用：

```text
signal/event
callback
future continuation
message queue
```

通知 GUI。

---

# 88. No Detached Threads

不要：

```cpp
std::thread(...).detach();
```

启动不可管理任务。

所有 worker 必须由：

```text
TaskManager
ThreadPool
jthread
```

等明确拥有。

应用退出时必须能够：

```text
cancel
join
cleanup
```

---

# 89. Avoid Global State

避免：

```text
global archive manager
global task manager
global mutable settings
```

如果 Flutter 或旧 Qt frontend 需要 singleton，它只能是 frontend/application facade。

Core 不依赖 global singleton。

---

# 90. Latest Dependencies and Build Traceability

初始化或更新依赖时，应从指定上游解析最新稳定发布版；没有独立正式发布版的项目使用约定默认分支的最新提交。

不在方案中写死依赖版本号、release tag 或 commit，也不要求 Release 永久使用固定版本。

一次构建在开始前完成依赖解析和源码获取，编译过程中使用同一份解析结果；下一次依赖初始化或更新重新检查上游最新版本。

构建记录必须包含每项依赖的来源、实际版本、tag（如有）、commit 和解析时间，用于诊断、审计与复现该次构建。记录解析结果不改变后续使用最新版本的策略。

帮助菜单中的“开源组件与许可”必须显示当前应用版本及其 Git revision（可用时），
并为各组件显示本次构建实际使用的版本、Git revision（可用时）和源码仓库链接。
数据来自经验证的当前构建身份与配置记录（与最终 provenance 共用输入），不用文档示例或当前子模块 HEAD
代替已构建依赖；未知信息明确显示为不可用，不编造版本。

获取最新版本失败或发现兼容性问题时，应明确报告原因并修复适配，不能静默退回旧版本却宣称使用最新版本。

依赖更新应单独提交，并运行受影响的构建、Core/C ABI regression tests 和必要的
Flutter frontend 验证；触及旧 Qt 基线时再运行其相关检查。

---

# 91. First Major Milestone

M1–M6 继续描述产品能力。已有 Core 和 Qt 的完成记录保留；Flutter 对应能力必须
单独完成和验收，不能继承 Qt GUI 的通过状态。迁移执行阶段见第 99 节。

Milestone 1：

```text
Clean Core Architecture
```

完成：

```text
C++23 Archive Core
libarchive integration
RAII wrappers
error model
entry model
format detection
archive enumeration
unit tests
```

GUI 只需要能：

```text
Open archive
Display entries
```

暂时不追求全部功能。

---

# 92. Second Major Milestone

Milestone 2：

```text
Extraction
```

完成：

```text
Extract all
Extract selected
progress
cancel
overwrite policy
password
secure path handling
corrupted archive handling
```

重点首先保证：

```text
correctness
security
stability
```

---

# 93. Third Major Milestone

Milestone 3：

```text
Compression
```

完成：

```text
Create ZIP
Create 7z if supported by backend
Create TAR.*
compression level
progress
cancel
atomic output
```

格式能力由：

```text
ArchiveCapabilities
```

驱动。

---

# 94. Fourth Major Milestone

Milestone 4：

```text
Modern Desktop Experience
```

完成：

```text
new visual design
light/dark theme
drag/drop
recent archives
search
sort
task center
keyboard shortcuts
better error dialogs
```

---

# 95. Fifth Major Milestone

Milestone 5：

```text
Platform Integration
```

完成：

```text
file associations
context menu
installer
Linux desktop integration
macOS bundle
auto update architecture if desired
```

---

# 96. Sixth Major Milestone

Milestone 6：

```text
Performance and Hardening
```

完成：

```text
benchmarks
profiling
memory optimization
large archives
million-entry archive tests
fuzzing
sanitizers
security regression suite
```

---

# 97. Priority Order

发生设计冲突时，优先级：

```text
1. 数据安全
2. 正确性
3. 稳定性
4. 安全性
5. 用户体验
6. 性能
7. 可维护性
8. 功能数量
```

“高性能”不能成为：

```text
unsafe extraction
data corruption
race condition
memory corruption
```

的理由。

---

# 98. Definition of Done

一个功能只有满足以下条件才算完成：

```text
implementation exists

build succeeds

error handling exists

no obvious resource leak

UI remains responsive

relevant tests exist

platform boundaries are respected

no unrelated regression

no fake production data

no hardcoded developer path
```

如果涉及 archive extraction，还必须：

```text
pass path traversal tests
```

---

# 99. Flutter Migration Tasks

`main` 按以下顺序增量开发，复用已存在的 Core/Application：

```text
F1  Stable C ABI
    handles / ownership / errors / tasks / cancellation / C ABI tests
F2  Shared metadata browsing
    从 Qt 抽离目录索引、Unicode 查询、分页、原始身份与选择解析
F3  Flutter archive browsing
    Home / 内容检测打开 / 目录树 / 当前目录 / 虚拟列表 / 搜索排序
F4  Flutter archive operations
    解压全部与选中 / 密码 / 覆盖交互 / 进度 / 取消 / capabilities 创建
F5  Desktop and platform parity
    主题 / 设置迁移 / 最近记录 / 任务中心 / 快捷键 / 拖入拖出 / 系统打开
F6  Release and performance acceptance
    各平台打包 / 来源与许可 / 安装升级 / 100k 与 1M metadata / 性能对比
```

每阶段保持 Core 可独立测试、资源安全、UI 响应和现有回归不退化。
Qt 基线分支不跟随 Flutter 重写，现有主线 Qt 文件只为迁移对照暂留。

---

# 100. Immediate Technical Direction

F1 原生 C ABI、F2 公共 metadata 浏览、F3 Flutter/Dart 归档浏览、F4 归档操作与 F5
桌面体验已实现，并提供 Windows x64 验证记录。下一步补齐 F5 桌面实机验收，再推进
F6；其他平台仍需原生构建和运行验证。
不重写 backend、不先做复杂动画或无关产品功能。
第一条 Flutter 纵向链路为：

```text
User selects archive
    ↓
Flutter Controller / ViewModel
    ↓
Dart FFI → ananarchiver_c_api
    ↓
ArchiveService → TaskManager worker
    ↓
ArchiveReader → LibArchiveReader → libarchive
    ↓
C++ metadata index / current-folder query
    ↓
bounded C ABI pages → Dart view state → virtualized Flutter file view
```

打开/查询的旧 generation 必须失效；选中项保留原始条目身份。
该链路通过后，接入现有解压和压缩任务，并按 F5/F6 验收桌面体验与发布。

---

# 101. Final Architecture Rule

新代码先确定其属于 Core、Application、C ABI、Frontend 还是 Platform。
调用 libarchive、操作 UI、保存 settings 和管理归档线程不能塞进同一个类。

删除任何 frontend 后仍应独立构建测试：

```text
ananarchiver_core
ananarchiver_libarchive
ananarchiver_application
ananarchiver_c_api（实现后）
```

Flutter 依赖稳定 C ABI，不能依赖旧 Qt Controller；C ABI 不依赖 Flutter engine。
压缩、解压、安全、取消、任务和错误处理保留唯一的 C++ 实现。

---

# 102. Product Vision

LiteArc 最终应成为：

> 一个以现代 C++ 为核心、以 libarchive 为统一归档引擎、以 Flutter 为主线前端、
> UI 边界清晰、跨平台、安全、快速、适合大型压缩包的现代桌面归档管理器。

它不是：

> 一个 Flutter Demo + 几个 archive_read_* 调用。

所有架构决策都应围绕这个长期目标进行。

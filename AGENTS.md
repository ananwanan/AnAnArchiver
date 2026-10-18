# AGENTS.md

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

> Archive Core 与 GUI 必须完全解耦。

GUI 不得直接调用 libarchive C API。

正确依赖方向：

```text
┌──────────────────────────────────────────────┐
│                    UI                        │
│                                              │
│ Qt Quick / Flutter / other frontend          │
└─────────────────────┬────────────────────────┘
                      │
                      ▼
┌──────────────────────────────────────────────┐
│            Application / Controller           │
│                                              │
│ OpenArchive                                  │
│ ExtractTask                                  │
│ CompressTask                                 │
│ TestArchiveTask                              │
│ TaskManager                                  │
│ Settings                                     │
└─────────────────────┬────────────────────────┘
                      │
                      ▼
┌──────────────────────────────────────────────┐
│                  Core                        │
│                                              │
│ ArchiveReader                                │
│ ArchiveWriter                                │
│ ArchiveEntry                                 │
│ ArchiveFormat                                │
│ ArchiveCapabilities                         │
│ ExtractionPolicy                            │
│ Progress / Cancellation                     │
└─────────────────────┬────────────────────────┘
                      │
                      ▼
┌──────────────────────────────────────────────┐
│            libarchive adapter                │
│                                              │
│ LibArchiveReader                             │
│ LibArchiveWriter                             │
│ RAII wrappers                                │
└─────────────────────┬────────────────────────┘
                      │
                      ▼
              libarchive/libarchive
```

禁止：

```text
QML
 ↓
AppContext
 ↓
archive_read_new()
```

或者：

```text
Flutter
 ↓
FFI
 ↓
libarchive
```

UI 永远只能访问 Application/API 层。

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

当前项目已经使用：

```text
Qt 6
Qt Quick
QML
C++23
```

因此 **第一阶段默认继续使用 Qt 6 Quick/QML**。

这是当前首选方案，而不是永久绑定。

理由：

- 当前代码已经使用 Qt Quick
- libarchive 与 C++ Core 可以直接集成
- 无额外语言运行时
- FFI 成本最低
- 桌面文件系统集成成熟
- 原生 Drag & Drop、Clipboard、File Dialog 等支持较好
- C++ 调试链路简单
- Release 包相对容易控制

但是必须保证：

```text
Archive Core
Application Core
```

完全不依赖 Qt。

Core 中禁止：

```cpp
QString
QVector
QObject
QFile
QDir
QDateTime
QThread
QFuture
QAbstractItemModel
```

Core 使用：

```cpp
std::string
std::u8string
std::filesystem::path
std::vector
std::chrono
std::thread
std::jthread
std::future
std::expected
std::span
std::optional
std::unique_ptr
std::shared_ptr
```

Qt 只能存在于：

```text
frontend/qt/
platform/qt/
```

等适配层。

---

# 5. Flutter Policy

未来允许把 GUI 替换为 Flutter。

如果采用 Flutter，不允许重新实现 Archive Core。

结构应该变成：

```text
Flutter UI
    │
    ▼
Dart FFI
    │
    ▼
ananarchiver_c_api
    │
    ▼
C++ Core
    │
    ▼
libarchive
```

如果创建 Flutter frontend，必须提供稳定的 C ABI：

```text
ananarchiver_c_api
```

禁止 Dart FFI 直接操作：

```c
struct archive *
struct archive_entry *
```

所有 libarchive 生命周期仍然由 C++ Core 管理。

除非用户明确要求，否则不要在当前阶段主动将整个 Qt frontend 重写为 Flutter。

---

# 6. Other UI Frameworks

可以研究：

```text
Slint
Flutter
Qt Quick
Tauri
Avalonia
Dear ImGui
```

但对于本项目：

```text
Qt Quick/QML = 默认桌面 UI
Flutter      = 未来主要备选
Slint        = 可研究
Tauri        = 不作为默认方案
Dear ImGui   = 不作为最终用户 GUI
```

Dear ImGui 可以用于：

```text
debug tools
benchmark tools
internal diagnostics
```

但不作为正式 LiteArc GUI。

不要因为“换技术栈看起来更现代”而无理由重写 frontend。

---

# 7. Repository Architecture

重构后的推荐目录：

```text
LiteArc/
│
├── AGENTS.md
├── README.md
├── CMakeLists.txt
├── CMakePresets.json
│
├── cmake/
│   ├── CompilerWarnings.cmake
│   ├── Dependencies.cmake
│   └── Sanitizers.cmake
│
├── include/
│   └── ananarchiver/
│       ├── archive.h
│       ├── archive_entry.h
│       ├── archive_format.h
│       ├── archive_reader.h
│       ├── archive_writer.h
│       ├── archive_capabilities.h
│       ├── archive_error.h
│       ├── progress.h
│       └── cancellation.h
│
├── src/
│   │
│   ├── core/
│   │   ├── archive/
│   │   ├── filesystem/
│   │   ├── tasks/
│   │   └── util/
│   │
│   ├── backend/
│   │   └── libarchive/
│   │       ├── libarchive_reader.cpp
│   │       ├── libarchive_writer.cpp
│   │       ├── libarchive_handle.cpp
│   │       ├── libarchive_error.cpp
│   │       └── libarchive_capabilities.cpp
│   │
│   ├── application/
│   │   ├── archive_service.cpp
│   │   ├── task_manager.cpp
│   │   ├── extract_task.cpp
│   │   ├── compress_task.cpp
│   │   ├── test_task.cpp
│   │   └── settings_service.cpp
│   │
│   └── platform/
│       ├── common/
│       ├── windows/
│       ├── linux/
│       └── macos/
│
├── frontend/
│   └── qt/
│       ├── main.cpp
│       ├── app/
│       ├── controllers/
│       ├── models/
│       ├── qml/
│       │   ├── Main.qml
│       │   ├── pages/
│       │   ├── components/
│       │   ├── dialogs/
│       │   ├── theme/
│       │   └── icons/
│       └── resources/
│
├── tests/
│   ├── core/
│   ├── archive/
│   ├── security/
│   └── integration/
│
├── benchmarks/
│
└── third_party/
```

目录不必一次性机械重构完成。

按照功能逐步迁移。

任何提交都应尽可能保持：

```text
能够编译
能够启动
已有测试不回归
```

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
ArchiveReader 不知道 QML
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

当前仓库中已有：

```text
src/app
src/context
src/models
ui/
```

以及：

```text
App
AppContext
FileTreeItem
FileTreeModel
```

不要无脑全部删除。

先判断：

```text
可以保留
需要迁移
需要替换
```

当前 `FileTreeModel` 主要用于真实目录扫描。

它不能成为 Archive Core。

应逐渐变成：

```text
Qt model adapter
```

数据来源应该来自 Application/Core。

---

# 35. Remove Current Hardcoded Test Path

当前应用中存在类似：

```cpp
_file = "S:/Folder";
```

这只是开发阶段测试逻辑。

正式重构时必须删除。

没有命令行输入文件时，应：

```text
打开 Home 页面
```

而不是自动扫描某个开发者目录。

---

# 36. Existing FileTreeModel

当前 `FileTreeModel` 会递归扫描整个目录。

这种做法不能继续作为大型目录浏览的最终实现。

例如：

```cpp
scanDirectory(path, item);
```

会递归遍历整个树。

大型目录可能造成：

```text
启动缓慢
内存增加
UI 卡顿
深度递归
```

未来目录树应使用：

```text
lazy loading
```

只有展开目录时才扫描其 children。

---

# 37. Remove Fake UI Data

生产 QML 中禁止：

```qml
model: 1024
```

以及：

```qml
text: "test" + index + ".zip"
```

这种占位数据。

Mock data 应放入：

```text
tests
demo
preview
development-only model
```

不能混入正式业务 UI。

---

# 38. Qt Architecture

Qt frontend 推荐：

```text
QML
 ↓
Controller / ViewModel
 ↓
Application Service
 ↓
Core
```

不要制造一个万能：

```text
AppContext
```

把所有功能都塞进去。

按职责拆分，例如：

```text
ApplicationController
ArchiveController
TaskController
SettingsController

ArchiveEntryModel
TaskListModel
DirectoryTreeModel
```

---

# 39. Qt Model Rules

大量 archive entries 不要逐条：

```text
beginInsertRows()
endInsertRows()
```

几十万次。

必须使用：

```text
batch insertion
```

或者适当的：

```text
model reset
pagination
incremental window
```

View 必须使用虚拟化组件。

不得为列表每一行永久创建复杂 QObject tree。

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
│ Tree /        │                               │
│ Navigation    │                               │
│               │ Archive content               │
│               │                               │
├───────────────┴───────────────────────────────┤
│ Status / entries / total size / task status   │
└───────────────────────────────────────────────┘
```

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

目录树

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

# 51. CMake

CMake 必须 target-based。

使用：

```cmake
target_link_libraries()
target_include_directories()
target_compile_features()
target_compile_definitions()
```

避免全局：

```cmake
include_directories()
link_directories()
add_definitions()
```

除非确实无法避免。

推荐 target：

```text
ananarchiver_core
ananarchiver_libarchive
ananarchiver_application
ananarchiver_platform
ananarchiver_qt
AnAnArchiver
```

依赖方向必须单向。

---

# 52. Qt Dependency

如果 Qt frontend 只需要：

```text
Qt Quick
Qt Quick Controls
Qt Quick Dialogs
```

不要无理由链接：

```text
Qt6::Widgets
```

只有确实使用 QWidget / native Widget API 时才能添加。

减少没有必要的依赖。

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

Qt 模块及其内嵌依赖应使用最新 Qt 正式发布版配套的源码，Mbed TLS framework 同样跟随所选 Mbed TLS 发布版，不能单独混用不配套版本。

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
- 已有 QML URI、资源路径、应用组织/设置标识、registry keys、bundle/application IDs。
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

不要在 QML 里大量使用：

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

具体平台行为通过 platform adapter 实现。

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
clean CMake build
```

不要在 Archive Core 尚未稳定时过度投入安装器。

---

# 80. CI

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

QML：

保持一致缩进。

不要为了格式化而在功能 PR 中修改几千行无关代码。

---

# 82. Documentation

重要设计决策写入：

```text
docs/architecture/
```

推荐 ADR：

```text
0001-ui-framework.md

0002-libarchive-integration.md

0003-threading-model.md

0004-archive-security.md

0005-task-system.md
```

只有真正需要长期记录的架构决策才创建 ADR。

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

---

# 84. Codex Must Inspect Before Editing

例如任务是：

```text
实现 ZIP 打开
```

不要第一步就改 QML。

应该先检查：

```text
CMake
Archive Core
libarchive dependency
application service
existing Qt model
frontend
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

如果 Qt frontend 需要 singleton，它只能是 frontend/application facade。

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

依赖更新应单独提交，并运行受影响的构建、Core regression tests 和必要的 Qt frontend 验证。

---

# 91. First Major Milestone

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

# 99. Initial Refactoring Tasks

重写项目时按以下顺序进行，而不是一次性重写：

```text
Phase A
建立 ananarchiver_core

Phase B
集成最新版本 libarchive/libarchive

Phase C
实现 LibArchiveReader

Phase D
实现 ArchiveEntry / ArchiveInfo / ArchiveError

Phase E
实现 archive enumeration

Phase F
将 Qt FileTreeModel 与 backend 解耦

Phase G
实现真实 ArchiveEntryModel

Phase H
删除 FileTable.qml 中 mock data

Phase I
实现异步 OpenArchiveTask

Phase J
实现 extraction

Phase K
实现 compression

Phase L
重新设计 UI
```

每一阶段都应保持项目可运行。

---

# 100. Immediate Technical Direction

当前阶段不要先做：

```text
installer
auto updater
shell extension
复杂动画
plugin system
cloud storage
AI features
```

优先完成这一条完整链路：

```text
User selects archive
        ↓
ApplicationController
        ↓
OpenArchiveTask
        ↓
ArchiveService
        ↓
ArchiveReader
        ↓
LibArchiveReader
        ↓
libarchive/libarchive
        ↓
ArchiveEntry metadata
        ↓
ArchiveEntryModel
        ↓
QML File View
```

当这条链路稳定以后，再继续 extraction 和 compression。

---

# 101. Final Architecture Rule

任何新代码都先问三个问题：

```text
它属于 Core 吗？

它属于 Application 吗？

它属于 Frontend / Platform 吗？
```

如果一个类同时：

```text
调用 libarchive
操作 QObject
弹 Dialog
扫描文件系统
保存 settings
管理线程
```

说明设计已经出了问题，应立即拆分。

最终目标是让项目即使删除：

```text
frontend/qt
```

仍然能够单独构建和测试：

```text
ananarchiver_core
ananarchiver_libarchive
ananarchiver_application
```

未来增加：

```text
frontend/flutter
```

时，不需要重新实现压缩、解压、安全、任务和错误处理逻辑。

---

# 102. Product Vision

LiteArc 最终应成为：

> 一个以现代 C++ 为核心、以 libarchive 为统一归档引擎、UI 可替换、跨平台、安全、快速、适合大型压缩包的现代桌面归档管理器。

它不是：

> 一个 QML Demo + 几个 archive_read_* 调用。

所有架构决策都应围绕这个长期目标进行。

# Dependency resolution validation — 2026-10-11

本次在已合并主线的最新稳定依赖方案上整合增强：沿用单一 canonical 解析器、
现有 manifest/provenance 契约和 Qt 桌面 CI，加入纯本地验证、构建身份绑定、
失败日志与真实全新 checkout 回归。后端修复已移到指定上游，父项目不再维护补丁副本。

本记录的版本和 revision 只描述本次验证，不是长期固定要求。
[2026-10-10 记录](2026-10-10-dependency-resolution.md) 保留为历史证据。

## Canonical 入口与最终快照

源码解析统一使用 `scripts/resolve-dependencies.py`，CMake 兼容入口为
`scripts/init-dependencies.cmake`；构建记录统一由
`cmake/DependencyProvenance.cmake` 和 `scripts/build_traceability.py` 负责。
默认 manifest 为 `out/dependencies/resolved-dependencies.json`。

```powershell
# 每次普通初始化重新检查指定上游最新稳定版本。
cmake -DANANARCHIVER_INIT_QT=ON -P scripts/init-dependencies.cmake

# 先只解析，再重放该次完整源码集，最后纯本地验证。
python scripts/resolve-dependencies.py --qt --resolve-only --manifest out/dependencies/selected.json
python scripts/resolve-dependencies.py --qt --replay-manifest out/dependencies/selected.json
python scripts/resolve-dependencies.py --qt --verify --replay-manifest out/dependencies/selected.json

# 本地已有全部 Git objects 时，可完全离线重放。
python scripts/resolve-dependencies.py --qt --offline --replay-manifest out/dependencies/selected.json
```

Core 作业使用 `--core-only` 重放/验证完整 manifest，保留 Qt metadata 来检查显式
开发 kit。`--verify` 不联网、不检出源码、不重写 manifest。配置和编译前均会验证
实际 revision、origin、版本、配套 gitlink 和本地修改；缺失或不一致明确失败。

最终完整 manifest 为 schema 1，`includes_qt=true`，含 19 个源码仓库，解析时间为
`2026-10-11T10:04:42.821413+00:00`。每项保留 `url`、实际 `version`、`ref`、`revision`、选择规则、
metadata 来源以及 `coupled_to`。Qt 与 Mbed TLS 的嵌套来源跟随所选父仓库，
不会分别升级到默认分支。没有独立数值版本的子仓库记录 `git-<短 revision>`。
libarchive 的实际版本从公开 `archive.h` 读取，为 `3.9.0dev`。

| 源码 | manifest 版本 | ref / 配套父仓库 | revision |
| --- | --- | --- | --- |
| libarchive | 3.9.0dev | refs/heads/master | `d0661f5e7d89c9f3eea55be894340d3976ec97e4` |
| zlib | 1.3.2 | refs/tags/v1.3.2 | `da607da739fa6047df13e66a2af6b8bec7c2a498` |
| bzip2 | 1.0.8 | refs/tags/bzip2-1.0.8 | `6a8690fc8d26c815e798c588f796eabe9d684cf0` |
| xz | 5.8.4 | refs/tags/v5.8.4 | `d3e650e63c110e830fd5391e7f8b45df0b91d3da` |
| zstd | 1.5.7 | refs/tags/v1.5.7 | `f8745da6ff1ad1e7bab384bd1f9d742439278e99` |
| lz4 | 1.10.0 | refs/tags/v1.10.0 | `ebb370ca83af193212df4dcbadcc5d87bc0de2f0` |
| expat | 2.9.0 | refs/tags/R_2_9_0 | `a76b1174c1ea52abb639a4c69d52b630a8a106a2` |
| mbedtls | 4.2.0 | refs/tags/mbedtls-4.2.0 | `ece41aa84d7879d7e55c59e955a5884b541f7f3b` |
| mbedtls/framework | git-dde0c4a0e448 | gitlink: mbedtls | `dde0c4a0e448a0552f18817dcea633bb851fd288` |
| mbedtls/tf-psa-crypto | 1.2.0 | gitlink: mbedtls | `73c5da561c8e5253db7b1fb440eda86fde8d8024` |
| mbedtls/tf-psa-crypto/framework | git-dde0c4a0e448 | gitlink: mbedtls/tf-psa-crypto | `dde0c4a0e448a0552f18817dcea633bb851fd288` |
| mbedtls/tf-psa-crypto/drivers/pqcp/mldsa-native | git-5772b4f4a010 | gitlink: mbedtls/tf-psa-crypto | `5772b4f4a0105694b1203abb582273f78fa951b7` |
| qt | 6.12.0 | refs/tags/v6.12.0 | `ef25002ed292b132d1ed3246bfcee1d24a9cd966` |
| qt/qtbase | git-025bdad181de | gitlink: qt | `025bdad181de241e81bf853c8a2d7bf3d19261a9` |
| qt/qtsvg | git-883a58532078 | gitlink: qt | `883a58532078d003cefef0a549bfe9f885ce41f5` |
| qt/qtdeclarative | git-4332fbbb4c34 | gitlink: qt | `4332fbbb4c340463dc8cee36dda0c85d932122d8` |
| qt/qtdeclarative/tests/auto/qml/ecmascripttests/test262 | git-a0efb7e1ab1c | gitlink: qt/qtdeclarative | `a0efb7e1ab1cee24c387abe1897ff18fa116c17d` |
| qt/qtshadertools | git-bccd516b990a | gitlink: qt | `bccd516b990a6396900051470ac86cc27a7042ea` |
| vulkan-headers | 1.4.365 | refs/tags/v1.4.365 | `c46850864f4661461b0f6cb9922c058ffea4915e` |

## 构建溯源与回归

每个应用构建保存所用子集 `resolved-dependencies.json` 和最终
`dependency-provenance.json`；安装到 `share/AnAnArchiver`，保留原有契约。
`dependency-build.json` 将源码、编译器、编译参数、CNG 选择和构建适配器绑定到
构建目录/依赖 prefix。没有身份的旧库和不匹配的 prefix 会被拒绝；只更新解析
时间、实际输入未改变时可以复用缓存。Qt 源码缓存单独绑定其来源与选项。

最终 provenance 在生产产物成功后生成，记录实际 backend、Qt、编译器、CMake、
Python、Jinja2/jsonschema 信息。显式系统库标为外部来源，不冒用 bundled revision；
superbuild 从已完成应用记录汇入实际外部 kit。`SOURCE_DATE_EPOCH` 可用于可复现时间。

最终离线套件为 51 项：36 项解析器、15 项构建溯源。真实 CI 独立工具作业已通过：
Windows 51/51（75.856 秒）、Ubuntu 51/51（5.987 秒）。本机最终 Core 和 desktop
CTest 也分别运行并通过同一 51 项。测试覆盖 release/prerelease 与 fork/tag 选择、精确重放、
脏文件/未发布提交保护、中断恢复、伪造 metadata、源图、Qt 选项和旧 prefix 拒绝。

首次 PR CI [run 38130561573](https://github.com/ananwanan/AnAnArchiver/actions/runs/38130561573)
暴露全新 clone 的深层父目录未初始化问题：预检以不存在的 TF-PSA-Crypto 目录
作为 Git cwd，六个构建作业在重放阶段失败。修复在未初始化 parent 时不读取旧
baseline，已初始化 parent 的 gitlink/本地提交保护保留；新增真实三层 nested Git
fixture，确认取得四个选定 SHA、未获取不可用历史 gitlink，并验证不联网/不改写清单。
修复 commit 为 `78eede077de7a330c6d37e3b506f54af8052a176`。

当前工作流保留四个平台/编译器、sanitizer 和 Windows 完整 Qt source desktop 作业；
所有作业重放同一次完整解析。Ubuntu 编译器选稳定 runner 仓库中可用的最高版本，
Clang 配套 libc++；Windows 自动发现 Perl。解析器/溯源离线回归独立运行，重放后
再次只读验证。失败也上传 manifest、可用 provenance/配置记录、JUnit/CTest 与依赖日志。

## 最终本机构建

| 配置 | 当前已核实结果 |
| --- | --- |
| `D:/Cache/Codex/AnAnArchiver-Final-Core` | MSVC 19.51.36260.0 生产构建完成；6/6 CTest suites 通过（142.7 秒，含 51 项依赖工具回归）；应用 revision 78eede0、dirty=false；实际 backend d0661f5/3.9.0dev |
| `D:/Cache/Codex/AnAnArchiver-Final-Desktop` | 同一 backend、Qt 6.12.0 bundled-source；15/15 CTest suites 通过（133.32 秒，含同一 51 项）；生产 QML lint exit 0；中文 Home/About native smoke exit 0，并实际检查截图布局 |

桌面 suite 包括 Core/archive/extraction/compression/application、依赖工具，及
qt_browser、qt_desktop、qt_drag、qt_desktop_flow、qt_adapters、qt_home_smoke、
qt_filename_rendering、qt_extraction、qt_compression。最终输出分别保存在两目录的
`application/Testing/Temporary/LastTest.log`；独立依赖工具耗时分别为
142.260 秒和 133.025 秒。这些时长是回归运行耗时，不是归档吞吐量基准。

最终 Core/desktop 的应用 provenance 实读时间分别为 `2026-10-11T10:12:54Z`
和 `2026-10-11T10:14:39Z`；两者均为 d0661f5，记录 Python 3.14.7、Jinja2 3.1.6、
jsonschema 4.26.0 和 CMake 4.4.0。其余生成工具和安装后记录以实际日志/metadata 为准。

Qt 6.12 源码缓存为 `D:/Cache/Codex/AnAnArchiver-LatestPolicy-Qt612`。Qt 拒绝
junction 形式的构建路径，使用真实 SSD 目录；迁移后曾因旧 CMakeCache 目录记录
配置失败，修正生成缓存后重新 configure/generate 成功。最终构建复用已经验证来源、
编译器和选项一致的 Qt 产物。上述路径仅为本次测试缓存，不是生产代码默认路径。

## 已合入的后端修复与独立审计

[libarchive PR #1](https://github.com/ananwanan/libarchive/pull/1) 合入公共 PSA API、
ZIP abort 资源清理、盐头失败资源归属与认证失败保护，merge revision a4be649。
[PR #2](https://github.com/ananwanan/libarchive/pull/2) 补齐原父项目补丁的共享 PSA
并发保护、成功初始化缓存、持久 ECB operation、CryptoOnly 发现与失败处理，
merge revision `d0661f5e7d89c9f3eea55be894340d3976ec97e4`。两项均已合入指定
上游 `master`；实际初始化从该默认分支获得新 HEAD，没有用父仓库补丁生成副本。

原审计命令和记录位于 `out/audit/latest-compatibility/validation-report.md`。
该审计首先在合并前独立 checkout 运行，不能当作当前应用完整测试结果。

- Windows forced PSA（CNG 关闭）独立复跑 17 项、150,637 个断言，零失败/跳过：
  PBKDF2/HMAC/AES known vectors、六种 digest、AES ZIP roundtrip、错误密码、128 次盐头
  输出失败、seekable/7-byte streaming 认证损坏。PR #2 CryptoOnly kit 也独立复跑相同
  17 项零失败/跳过，tiny HMAC 输出失败分支真实执行；八线程共 256 次 AES ZIP 往返零失败。
- Linux/WSL 初次后端审计：13 项、2,783 断言通过，另有盐头/认证故障 1,063 断言通过，
  八线程共 256 次往返零失败。系统 zlib/bzip2 仅用于独立 audit；缺少的 LZMA/XZ/Zstd
  abort 子项明确跳过，不代替完整 bundled codecs 验收。
- Linux 初次 ASan/UBSan audit：14 项、2,814 断言通过，没有 ASan/UBSan/LeakSanitizer
  报告；crypto 库仍为 Release。PR #2 最终 sanitizer audit 结果另行补充。

## 历史轮次与当前边界

早期临时入口的 31/34 项回归、3bdd98d 与 a4be649 的 MSVC Core 6/6 suites，以及
整合前的 47 项结果只属于历史轮次。它们不计入 d0661f5 的最终本机验收；当前推荐
入口、manifest 路径、metadata 文件名和安装目录以上述 canonical 契约为准。

PR #6 [run 38131015354](https://github.com/ananwanan/AnAnArchiver/actions/runs/38131015354)
已经确认单独解析、Windows/Ubuntu 离线工具作业和 Ubuntu sanitizer 完整 job 成功；
sanitizer 的六个 CTest suites 全通过（7.63 秒）。Windows/MSVC、Ubuntu/GCC、
Ubuntu/Clang 和 macOS/Apple Clang 四个 Core 完整 job 也已成功。Windows desktop
完整源 Qt CI 已成功重放/验证，正在 configure/build，尚未声明整个工作流通过。
本机桌面验收已经完成；安装后 metadata/DLL 校验和 desktop CI 的最终结果另行补充。

真实外部 Explorer/Finder 拖放接收、Linux/macOS GUI、Android 设备、安装器/文件关联、
大型性能基准和 fuzzing 不在本次验收范围内。

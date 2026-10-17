# 2026-10-17：切换官方 libarchive 来源

## 实际解析结果

libarchive 的指定来源改为 [libarchive/libarchive](https://github.com/libarchive/libarchive)。
本次选用官方 [v3.8.10 正式发布版](https://github.com/libarchive/libarchive/releases/tag/v3.8.10)，
其发布时间为 2026-10-14，实际 commit 为
`0c48412be2d94afe1859a807ea7896a18d8900bd`。
该 gitlink 是本次解析结果，后续初始化仍查询官方最新稳定版。
其他 codec、Mbed TLS 与 Qt 的已解析版本保持不变。

主机的匿名 GitHub API 配额耗尽时，本次通过已连接的 GitHub 工具读取官方
REST repository/release 元数据，再交给现有 `select_source` 选择逻辑处理。
tag 的 peeled commit 通过官方 Git remote 验证和获取；没有改动解析器在 API
失败时明确报错的行为，也没有将版本永久写死在解析规则中。

`.gitmodules`、本地子模块 origin、依赖来源白名单与解析测试、项目规范、
README/架构/依赖文档、帮助菜单许可链接、Windows/Linux 打包来源声明均已同步。
旧 fork URL 仅作为明确标注的历史证据及拒绝旧来源的负面回归保留，
没有把旧 fork 的 PR 编号错误映射到官方仓库。

## 官方源码与兼容补丁

官方 3.8.10 尚不包含项目此前使用的 Mbed TLS 4 / TF-PSA-Crypto 适配和
ZIP 取消清理修复。构建从官方 revision 用 `git archive` 导出私有副本，
只在副本应用 `cmake/patches/libarchive-compatibility.patch`，不修改子模块。
补丁涵盖公共 PSA API、共享状态保护、流式 AES-CTR、CryptoOnly discovery、
ZIP compressor/crypto abort 清理、salt 输出失败和认证码失败检查及 backend 回归。

补丁 SHA-256：
`b906bd9e8ae9f66ce3d57fc158538fd745bb746d6089ca43a72551e7c1117ab4`。

构建 identity 同时绑定官方 revision、补丁 hash 和源码准备配方；
provenance 的 `libarchive_compatibility_patch` 记录路径、hash 与官方基准提交。
配置还会独立重建完整预期源码，验证生成副本后记录
`libarchive-generated-source.json`。后续 build/record-prefix/record 只读校验
全部文件、目录及符号链接，拒绝补丁内外文件的修改、添加、删除和类型变化。
不能通过重新 configure 将改过的生成源码当成新的可信基线。

安装与分发保留 `share/AnAnArchiver/libarchive-compatibility.patch`。
Windows 打包在修改 staging 前核对补丁记录、官方基准提交和实际 hash。
macOS 另将补丁复制进最终 `.app/Contents/Resources/share/AnAnArchiver`，
确保它随 ZIP/DMG 保留；Linux 的完整安装目录保留该文件。

## 本地验证

主机为 Windows x64，MSVC 19.51.36260.0；桌面使用显式选择的现有 Qt 6.12.0 kit。
所有 backend/codec 均在全新 SSD 构建目录重新编译，没有复用旧 fork 的 prefix。

| 验证 | 结果 |
| --- | --- |
| 默认 CNG 桌面完整构建与来源记录 | 通过 |
| CNG=OFF、Mbed TLS 4.2 / TF-PSA-Crypto 1.2 Core 完整构建 | 通过 |
| CNG=OFF Core CTest | 6/6 |
| 默认桌面完整 CTest（含最终 dependency_tools） | 18/18 |
| 最终依赖工具完整回归 | 78/78 |
| 依赖解析回归 | 39/39 |
| 来源与完整生成源码回归 | 25/25，符号链接测试实际执行 |
| 私有源码准备回归 | 6/6 |
| Windows 打包 preflight | pwsh 8/8；Windows PowerShell 5.1 8/8 |
| 独立 PSA backend 加密/ZIP 回归 | 57/57；716,362 次断言；0 失败、0 跳过 |
| 同进程 PSA 并发 | 8 线程同步启动，128 轮 AES256 ZIP 写读及认证 EOF；0 失败 |
| 生产 QML lint 与中文原生许可窗口 smoke | 通过 |
| CMake 安装补丁 | 安装副本 hash 与源码补丁一致 |
| Linux/macOS 打包脚本 | Bash 语法检查通过 |
| 官方子模块、离线来源 verify、git diff --check | 通过；子模块无工作区修改 |

backend 测试先通过 CTest 57/57。官方 test runner 的多个进程会共用时间戳临时目录，
因此另用单进程执行同样的 57 项，取得无 setup 噪声、零断言失败及零跳过的结果。
并发检查是独立进程内的 8 个线程，每轮独立 reader/writer，交替 store/deflate，
采用短分块验证流式 AES 与认证；没有把多进程结果当成共享 PSA 状态的并发证据。

本地证据目录为 `D:/Cache/Codex/AnAnArchiver-Official`：

- `desktop-cng/application`：桌面产物、CTest 日志及最终 provenance。
- `core-psa/application`：Core CTest 与 provenance。
- `backend-tests-psa-single-process-results.log`：干净的 57 项 backend 结果。
- `backend-tests-psa/backend-crypto-zip-results.xml`：backend CTest XML。
- `backend-tests-psa/parallel`：同进程并发检查的独立源码与 CMake。
- `licenses-official.png`：中文原生许可窗口截图。
- `install-smoke`：安装路径与补丁副本检查。

## 验证边界

本次没有重新编译 Qt 源码，没有在 Linux/macOS/Android 主机或设备上运行构建，
没有触发远程 CI，也没有生成新的正式分发安装包。平台脚本修改与本地回归
不构成这些平台的实际运行证据。旧 fork 的历史验证记录不替代本次官方版本验证。

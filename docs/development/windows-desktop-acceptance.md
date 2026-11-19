# Windows Flutter 桌面实机验收

本流程使用生产 Release、真实 OLE 和 Explorer。夹具及接收程序只用于验收，
不打入发布包，不向 Core 添加复制或解析逻辑。历史及本轮结果分别记录在
[F5 记录](../validation/2026-11-07-flutter-desktop.md)和
[Windows 收尾记录](../validation/2026-11-19-windows-desktop.md)。

## 构建和隔离

在仓库根目录运行。编译器更新后使用新 build/prefix；旧 dependency-build.json
与新工具链不一致时应重建依赖，不改写来源记录或绕过检查。下面显式重放已有的
已验证 native 源码解析，不表示重新解析了最新 backend/codec。

```powershell
./scripts/Invoke-Msvc.ps1 cmake -S . -B out/build/windows-closeout -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DANANARCHIVER_BUILD_QT=OFF `
  -DANANARCHIVER_BUILD_C_API=ON -DBUILD_TESTING=ON -DANANARCHIVER_SUPERBUILD=ON
./scripts/Invoke-Msvc.ps1 cmake --build out/build/windows-closeout --parallel 4
ctest --test-dir out/build/windows-closeout/application --output-on-failure

Push-Location frontend/flutter
$env:ANANARCHIVER_DEPENDENCY_PREFIX = (Resolve-Path ../../out/build/windows-closeout/dependencies/install).Path
../../scripts/Invoke-Msvc.ps1 flutter build windows --release
Pop-Location
python -X utf8 frontend/flutter/tool/windows_desktop_fixture.py prepare out/windows-desktop-case
```

`prepare` 拒绝覆盖现有 case 目录。内容 ZIP 使用 `.xxx` 后缀，根目录只有 `empty`、
`many`、`隐式` 三项；包含显式空目录、300 个非空文本和隐式 Unicode 子目录，
共 301 文件、4 个目录、8,629 B。`expected.json` 记录各文件大小和 SHA-256。

将完整 Release 目录复制到 case 下 `bundle`，并将启动进程的 `APPDATA` 指向 case
下的 `appdata`。在 `appdata/AnAnArchiver/flutter/preferences.json` 预置仅用于测试的
schema 1 设置（revision 0、theme 0、language en、recent 空列表、1280×720、
maximized false、qtMigration absent），避免改动真实偏好和已有导出。
通过启动参数打开 `folders.xxx`。实机窗口与 `flutter drive` 应串行运行；runner
同一用户会话的单实例 mutex 会将第二次启动交给现有进程，测试连接失败不是测试通过。

## Explorer 目录复制和取消

1. 在右侧列表选择三个目录，等待 native 选择完成，点击准备按钮。
2. 将 `Drag prepared files` 拖到 Explorer 打开的 case/target 空白处。
3. 核验目标内容；不要用准备目录的成功替代目标复制：

```powershell
python -X utf8 frontend/flutter/tool/windows_desktop_fixture.py verify out/windows-desktop-case `
  --report out/windows-desktop-case/explorer.json
```

校验要求文件和目录集合完全一致、每个文件大小/散列一致；缺少空目录、额外文件或
损坏内容均失败。工具只生成输入、读取和校验，不向 `target` 解压或复制数据。

另一次原生拖动中按 Escape，确认没有额外目标输出、应用仍响应，随后关闭归档和
正常退出。已交给 OS 的导出根应仍存在并通过同样校验；重启后仍计入四根容量。
未交付的准备输出则应由 native manager 清理。确认所有目标读取完毕后才明确释放。
释放失败的错误应可见，不能把取消请求当作清理完成。

## 退出后首次读取的 OLE 接收程序

`windows_drag_probe.cpp` 实现独立的 Windows `IDropTarget`。它接收真实 CF_HDROP，
立即结束 Drop，只保留普通路径；worker 等待指定源进程以 exit code 0 退出，然后
才开始递归复制。只接受私有导出存储以内的路径；目标必须预先存在且为空。
等待上限 60 秒，错误写入报告并显示 FAILED。此目标用于延迟读取寿命验证，
Explorer 仍须按上节另验。

```powershell
./scripts/Invoke-Msvc.ps1 cl /nologo /std:c++latest /EHsc /W4 /WX /DUNICODE /D_UNICODE `
  frontend/flutter/tool/windows_drag_probe.cpp `
  /Fo:out/windows-desktop-case/windows_drag_probe.obj `
  /Fe:out/windows-desktop-case/windows_drag_probe.exe `
  /link /SUBSYSTEM:WINDOWS ole32.lib shell32.lib user32.lib gdi32.lib uuid.lib

# sourcePid 使用本轮 LiteArc 的实际 PID；四个参数依次为 PID、存储、空目标、报告。
$caseDirectory = (Resolve-Path out/windows-desktop-case).Path
$sourcePid = (Get-Process LiteArc).Id
New-Item -ItemType Directory -Path "$caseDirectory/target-after-exit" | Out-Null
& "$caseDirectory/windows_drag_probe.exe" $sourcePid `
  "$caseDirectory/appdata/AnAnArchiver/flutter/exports" `
  "$caseDirectory/target-after-exit" "$caseDirectory/ole-after-exit.txt"
```

拖入接收窗口后正常退出 LiteArc。报告必须依次出现 `drop_received=3`、
`source_exit_code=0`、`copied_after_source_exit=3`，并且下面命令通过：

```powershell
python -X utf8 frontend/flutter/tool/windows_desktop_fixture.py verify out/windows-desktop-case `
  --target out/windows-desktop-case/target-after-exit `
  --report out/windows-desktop-case/after-exit.json
```

完成后重新启动 LiteArc 检查保留根，再通过临时导出功能明确释放并检查后台删除。
接收程序不能证明 Explorer 已完成读取，也不覆盖强制终止、崩溃或断电行为。

## 拖入创建对话框

将 case/plain.txt 从 Explorer 拖入 LiteArc，确认创建对话框出现后立即可操作；
取消对话框，再打开归档或查看任务中心，确认 OLE Drop 没有等待 modal Future。
另用真实系统选择器选择输出位置并创建，重新打开并核验 plain.txt 内容。
不以协议 fake 或 fixture picker 代替这些系统输入检查。

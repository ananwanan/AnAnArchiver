#include <flutter/dart_project.h>
#include <flutter/flutter_view_controller.h>
#include <windows.h>

#include "flutter_window.h"
#include "utils.h"

int APIENTRY wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE prev, _In_ wchar_t* command_line,
                      _In_ int show_command) {
    // Attach to console when present (e.g., 'flutter run') or create a
    // new console when running with a debugger.
    if (!::AttachConsole(ATTACH_PARENT_PROCESS) && ::IsDebuggerPresent()) {
        CreateAndAttachConsole();
    }

    // Explicit invocations forward to the existing process; no file association
    // or global shell registration is installed here. Paths remain UTF-16.
    HANDLE single = CreateMutexW(nullptr, FALSE, L"Local\\com.ananwanan.AnAnArchiver.desktop");
    if (single && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = nullptr;
        for (int attempt = 0; attempt < 150 && !existing; ++attempt) {
            EnumWindows(
                [](HWND window, LPARAM target) -> BOOL {
                    if (GetPropW(window, L"LiteArc.Desktop.v1")) {
                        *reinterpret_cast<HWND*>(target) = window;
                        return FALSE;
                    }
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(&existing));
            if (!existing)
                Sleep(20); // second process has no UI; allow first runner startup
        }
        const auto* command = GetCommandLineW();
        COPYDATASTRUCT data{0x4c415243, static_cast<DWORD>((wcslen(command) + 1) * sizeof(wchar_t)),
                            const_cast<wchar_t*>(command)};
        DWORD_PTR accepted = 0;
        const auto sent = existing && SendMessageTimeoutW(existing, WM_COPYDATA, 0,
                                                          reinterpret_cast<LPARAM>(&data),
                                                          SMTO_ABORTIFHUNG, 2000, &accepted);
        CloseHandle(single);
        return sent && accepted ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    // Initialize COM, so that it is available for use in the library and/or
    // plugins.
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    flutter::DartProject project(L"data");

    std::vector<std::string> command_line_arguments = GetCommandLineArguments();

    project.set_dart_entrypoint_arguments(std::move(command_line_arguments));

    FlutterWindow window(project);
    Win32Window::Point origin(10, 10);
    Win32Window::Size size(1280, 720);
    if (!window.Create(L"LiteArc", origin, size)) {
        return EXIT_FAILURE;
    }
    window.SetQuitOnClose(true);

    ::MSG msg;
    while (::GetMessage(&msg, nullptr, 0, 0)) {
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
    }

    ::CoUninitialize();
    if (single)
        CloseHandle(single);
    return EXIT_SUCCESS;
}

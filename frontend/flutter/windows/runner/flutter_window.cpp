#include "flutter_window.h"

#include "utils.h"
#include <algorithm>
#include <flutter/standard_method_codec.h>
#include <optional>
#include <shellapi.h>
#include <utility>

#include "flutter/generated_plugin_registrant.h"

FlutterWindow::FlutterWindow(const flutter::DartProject& project) : project_(project) {}

FlutterWindow::~FlutterWindow() {}

bool FlutterWindow::OnCreate() {
    if (!Win32Window::OnCreate()) {
        return false;
    }

    RECT frame = GetClientArea();

    // The size here must match the window dimensions to avoid unnecessary surface
    // creation / destruction in the startup path.
    flutter_controller_ = std::make_unique<flutter::FlutterViewController>(
        frame.right - frame.left, frame.bottom - frame.top, project_);
    // Ensure that basic setup of the controller was successful.
    if (!flutter_controller_->engine() || !flutter_controller_->view()) {
        return false;
    }
    RegisterPlugins(flutter_controller_->engine());
    window_channel_ = std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
        flutter_controller_->engine()->messenger(), "litearc/window",
        &flutter::StandardMethodCodec::GetInstance());
    window_channel_->SetMethodCallHandler([this](const auto& call, auto result) {
        if (call.method_name() == "ready") {
            desktop_ready_ = true;
            result->Success(flutter::EncodableValue(std::exchange(pending_open_, {})));
            return;
        }
        if (call.method_name() == "closing") {
            closing_ = true;
            result->Success();
            return;
        }
        if (call.method_name() == "state") {
            WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
            if (!GetWindowPlacement(GetHandle(), &placement)) {
                result->Error("window_state", "Cannot read window placement");
                return;
            }
            const auto rect = placement.rcNormalPosition;
            const auto dpi = GetDpiForWindow(GetHandle());
            result->Success(flutter::EncodableValue(flutter::EncodableMap{
                {flutter::EncodableValue("width"),
                 flutter::EncodableValue(
                     std::clamp(MulDiv(rect.right - rect.left, 96, dpi), 640, 7680))},
                {flutter::EncodableValue("height"),
                 flutter::EncodableValue(
                     std::clamp(MulDiv(rect.bottom - rect.top, 96, dpi), 480, 4320))},
                {flutter::EncodableValue("maximized"),
                 flutter::EncodableValue(placement.showCmd == SW_SHOWMAXIMIZED)}}));
            return;
        }
        if (call.method_name() == "restore") {
            const auto* data =
                call.arguments() ? std::get_if<flutter::EncodableMap>(call.arguments()) : nullptr;
            if (!data) {
                result->Error("window_state", "Window state map required");
                return;
            }
            auto number = [&](const char* key, int fallback) {
                const auto found = data->find(flutter::EncodableValue(key));
                if (found == data->end())
                    return fallback;
                if (const auto* value = std::get_if<int32_t>(&found->second))
                    return static_cast<int>(*value);
                return fallback;
            };
            MONITORINFO monitor{sizeof(MONITORINFO)};
            GetMonitorInfoW(MonitorFromWindow(GetHandle(), MONITOR_DEFAULTTONEAREST), &monitor);
            const auto dpi = GetDpiForWindow(GetHandle());
            const auto width =
                std::min(MulDiv(std::clamp(number("width", 1280), 640, 7680), dpi, 96),
                         static_cast<int>(monitor.rcWork.right - monitor.rcWork.left));
            const auto height =
                std::min(MulDiv(std::clamp(number("height", 720), 480, 4320), dpi, 96),
                         static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top));
            SetWindowPos(
                GetHandle(), nullptr,
                monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2,
                monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2,
                width, height, SWP_NOZORDER | SWP_NOACTIVATE);
            const auto maximized = data->find(flutter::EncodableValue("maximized"));
            if (maximized != data->end() && std::get_if<bool>(&maximized->second) &&
                std::get<bool>(maximized->second))
                ShowWindow(GetHandle(), SW_MAXIMIZE);
            result->Success();
            return;
        }
        const auto* title = call.arguments() ? std::get_if<std::string>(call.arguments()) : nullptr;
        if (call.method_name() != "title" || !title || title->size() > 256) {
            result->NotImplemented();
            return;
        }
        const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, title->data(),
                                               static_cast<int>(title->size()), nullptr, 0);
        if (length == 0) {
            result->Error("title_encoding", "Invalid UTF-8 window title");
            return;
        }
        std::wstring wide(static_cast<std::size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, title->data(),
                            static_cast<int>(title->size()), wide.data(), length);
        if (!SetWindowTextW(GetHandle(), wide.c_str())) {
            result->Error("title_window", "Could not set window title");
            return;
        }
        result->Success();
    });
    SetChildContent(flutter_controller_->view()->GetNativeWindow());
    SetPropW(GetHandle(), L"LiteArc.Desktop.v1", reinterpret_cast<HANDLE>(1));

    flutter_controller_->engine()->SetNextFrameCallback([&]() { this->Show(); });

    // Flutter can complete the first frame before the "show window" callback is
    // registered. The following call ensures a frame is pending to ensure the
    // window is shown. It is a no-op if the first frame hasn't completed yet.
    flutter_controller_->ForceRedraw();

    return true;
}

void FlutterWindow::OnDestroy() {
    RemovePropW(GetHandle(), L"LiteArc.Desktop.v1");
    window_channel_.reset();
    if (flutter_controller_) {
        flutter_controller_ = nullptr;
    }

    Win32Window::OnDestroy();
}

LRESULT
FlutterWindow::MessageHandler(HWND hwnd, UINT const message, WPARAM const wparam,
                              LPARAM const lparam) noexcept {
    if (message == WM_COPYDATA && !closing_ && window_channel_) {
        const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lparam);
        if (!data || data->dwData != 0x4c415243 || !data->lpData || data->cbData < 2 ||
            data->cbData > 65536 || data->cbData % sizeof(wchar_t))
            return FALSE;
        const auto* text = static_cast<const wchar_t*>(data->lpData);
        if (text[data->cbData / sizeof(wchar_t) - 1] != L'\0')
            return FALSE;
        int count = 0;
        auto* args = CommandLineToArgvW(text, &count);
        if (!args)
            return FALSE;
        flutter::EncodableList files;
        // Two paths suffice to report a rejected multi-file event in Dart.
        for (int i = 1; i < count && i <= 2; ++i)
            files.emplace_back(Utf8FromUtf16(args[i]));
        LocalFree(args);
        if (!files.empty()) {
            if (desktop_ready_)
                window_channel_->InvokeMethod("openFiles",
                                              std::make_unique<flutter::EncodableValue>(files));
            else
                pending_open_ = std::move(files); // one bounded pending event
        }
        ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(hwnd);
        return TRUE;
    }
    // Give Flutter, including plugins, an opportunity to handle window messages.
    if (flutter_controller_) {
        std::optional<LRESULT> result =
            flutter_controller_->HandleTopLevelWindowProc(hwnd, message, wparam, lparam);
        if (result) {
            return *result;
        }
    }

    switch (message) {
    case WM_FONTCHANGE:
        flutter_controller_->engine()->ReloadSystemFonts();
        break;
    }

    return Win32Window::MessageHandler(hwnd, message, wparam, lparam);
}

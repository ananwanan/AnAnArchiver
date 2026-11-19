// Manual Windows acceptance target, not part of the LiteArc bundle.
// Receives real OLE CF_HDROP, ends Drop immediately, then waits for the source
// process to exit before reading any payload file. Use only generated fixtures.
#define NOMINMAX
#include <windows.h>

#include <oleidl.h>
#include <shellapi.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {
namespace fs = std::filesystem;
constexpr UINT status_message = WM_APP + 1;
constexpr const wchar_t* labels[] = {
    L"Drop prepared folders here, then close LiteArc normally.",
    L"Drop received. Close LiteArc; copying starts only after its process exits.",
    L"Copied after source exit. Run the fixture SHA-256 verifier.",
    L"FAILED. Read the probe report.",
};

class DropTarget final : public IDropTarget {
  public:
    DropTarget(HWND window, HANDLE source, fs::path root, fs::path target, fs::path report)
        : window_(window), source_(source), root_(std::move(root)), target_(std::move(target)),
          report_(std::move(report)) {}
    ~DropTarget() {
        worker_.request_stop();
        if (worker_.joinable())
            worker_.join();
        CloseHandle(source_);
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) override {
        if (!output)
            return E_POINTER;
        *output = nullptr;
        if (iid != IID_IUnknown && iid != IID_IDropTarget)
            return E_NOINTERFACE;
        *output = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto count = --references_;
        if (!count)
            delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD, POINTL, DWORD* effect) override {
        FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        accepts_ = !worker_.joinable() && data && data->QueryGetData(&format) == S_OK;
        return DragOver(0, {}, effect);
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* effect) override {
        *effect = accepts_ && (*effect & DROPEFFECT_COPY) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {
        accepts_ = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD, POINTL, DWORD* effect) override {
        *effect = DROPEFFECT_NONE;
        if (!accepts_ || worker_.joinable())
            return S_OK;
        FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM medium{};
        const auto result = data->GetData(&format, &medium);
        if (FAILED(result))
            return result;
        std::vector<fs::path> paths;
        const auto drop = static_cast<HDROP>(medium.hGlobal);
        const auto count = DragQueryFileW(drop, 0xffffffff, nullptr, 0);
        if (count > 0 && count <= 256) {
            for (UINT i = 0; i < count; ++i) {
                const auto length = DragQueryFileW(drop, i, nullptr, 0);
                if (length == 0 || length > 32768) {
                    paths.clear();
                    break;
                }
                std::wstring path(length + 1, L'\0');
                DragQueryFileW(drop, i, path.data(), length + 1);
                path.resize(length);
                paths.emplace_back(path);
            }
        }
        ReleaseStgMedium(&medium);
        accepts_ = false;
        if (paths.empty())
            return S_OK;
        worker_ = std::jthread([this, paths = std::move(paths)](std::stop_token stop) {
            std::ofstream report(report_, std::ios::trunc);
            if (!report) {
                PostMessageW(window_, status_message, 3, 0);
                return;
            }
            report << "drop_received=" << paths.size() << std::endl;
            PostMessageW(window_, status_message, 1, 0);
            try {
                bool exited = false;
                for (int i = 0; i < 600 && !stop.stop_requested(); ++i) {
                    const auto wait = WaitForSingleObject(source_, 100);
                    if (wait == WAIT_OBJECT_0) {
                        exited = true;
                        break;
                    }
                    if (wait != WAIT_TIMEOUT)
                        throw std::runtime_error("Source process wait failed");
                }
                if (!exited)
                    throw std::runtime_error(
                        "Source did not exit within 60 seconds, or probe closed");
                DWORD exit_code = 0;
                if (!GetExitCodeProcess(source_, &exit_code) || exit_code != 0)
                    throw std::runtime_error("Source did not exit successfully");
                report << "source_exit_code=" << exit_code << std::endl;
                const auto root = fs::canonical(root_);
                if (!fs::is_directory(target_) || !fs::is_empty(target_))
                    throw std::runtime_error("Target must be an existing empty directory");
                for (const auto& path : paths) {
                    const auto source = fs::canonical(path);
                    const auto relative = source.lexically_relative(root);
                    if (relative.empty() || relative.is_absolute() || *relative.begin() == L"..")
                        throw std::runtime_error(
                            "Dropped path is outside the generated export store");
                    if (fs::is_symlink(path))
                        throw std::runtime_error("Fixture symlinks are not allowed");
                    if (stop.stop_requested())
                        throw std::runtime_error("Probe closed");
                    fs::copy(source, target_ / source.filename(), fs::copy_options::recursive);
                }
                report << "copied_after_source_exit=" << paths.size() << std::endl;
                PostMessageW(window_, status_message, 2, 0);
            } catch (const std::exception& error) {
                report << "error=" << error.what() << std::endl;
                PostMessageW(window_, status_message, 3, 0);
            }
        });
        *effect = DROPEFFECT_COPY;
        return S_OK;
    }

  private:
    std::atomic<ULONG> references_{1};
    HWND window_;
    HANDLE source_;
    fs::path root_, target_, report_;
    bool accepts_ = false;
    std::jthread worker_;
};

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == status_message) {
        SetWindowTextW(window, labels[wparam]);
        SetWindowLongPtrW(window, GWLP_USERDATA, static_cast<LONG_PTR>(wparam));
        InvalidateRect(window, nullptr, TRUE);
        return 0;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        auto dc = BeginPaint(window, &paint);
        RECT area{};
        GetClientRect(window, &area);
        InflateRect(&area, -20, -20);
        DrawTextW(dc, labels[GetWindowLongPtrW(window, GWLP_USERDATA)], -1, &area,
                  DT_LEFT | DT_WORDBREAK);
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_DESTROY) {
        RevokeDragDrop(window);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    int count = 0;
    auto args = CommandLineToArgvW(GetCommandLineW(), &count);
    // Positional arguments: source PID, private export store, empty target, report.
    if (!args || count != 5) {
        LocalFree(args);
        return 2;
    }
    const auto source = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                    wcstoul(args[1], nullptr, 10));
    const fs::path root = args[2], target = args[3], report = args[4];
    LocalFree(args);
    if (!source)
        return 3;
    if (FAILED(OleInitialize(nullptr))) {
        CloseHandle(source);
        return 4;
    }
    WNDCLASSW type{};
    type.lpfnWndProc = window_proc;
    type.hInstance = instance;
    type.lpszClassName = L"LiteArc.WindowsDragProbe";
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&type);
    // Keep the drop target visible above a maximized source during the test.
    const auto window =
        CreateWindowExW(WS_EX_TOPMOST, type.lpszClassName, labels[0], WS_OVERLAPPEDWINDOW, 1050,
                        500, 720, 260, nullptr, nullptr, instance, nullptr);
    if (!window) {
        OleUninitialize();
        CloseHandle(source);
        return 5;
    }
    auto drop = new DropTarget(window, source, root, target, report);
    const auto registered = RegisterDragDrop(window, drop);
    if (FAILED(registered)) {
        drop->Release();
        DestroyWindow(window);
        OleUninitialize();
        return 6;
    }
    ShowWindow(window, show);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    drop->Release();
    OleUninitialize();
    return 0;
}

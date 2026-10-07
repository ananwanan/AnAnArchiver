#include "controllers/archive_controller.h"
#include "controllers/compression_controller.h"
#include "models/file_tree_model.h"
#include "platform/frontend_translator.h"
#include "platform/path_conversion.h"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimer>
#include <ananarchiver/libarchive_writer.h>
#include <ananarchiver/unicode.h>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <random>
#include <stdexcept>

namespace {

using namespace ananarchiver;

void check(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}

bool waitUntil(const std::function<bool()>& ready) {
    QElapsedTimer timeout;
    timeout.start();
    while (!ready() && timeout.elapsed() < 15000) {
        QEventLoop loop;
        QTimer::singleShot(10, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return ready();
}

QObject* named(QObject* root, const char* name) {
    auto* result = root->findChild<QObject*>(QString::fromUtf8(name));
    check(result != nullptr, std::string("Production QML object missing: ") + name);
    return result;
}

void invoke(QObject* object, const char* method) {
    check(QMetaObject::invokeMethod(object, method, Qt::DirectConnection),
          std::string("Could not trigger production QML: ") + method);
}

QString translated(const char* text) {
    return QCoreApplication::translate("CompressionController", text);
}

void writeFile(const QString& path, const QByteArray& data) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(data) == data.size(),
          "Could not write test input");
}

QByteArray readFile(const std::filesystem::path& path) {
    QFile file(qt::fromPath(path));
    check(file.open(QIODevice::ReadOnly), "Could not read roundtrip output");
    return file.readAll();
}

struct View {
    qt::ArchiveController archives;
    qt::CompressionController compression;
    qt::FileTreeModel directories;
    QQmlApplicationEngine engine;
    QObject* root{};

    explicit View(application::WriterFactory factory = {}) : compression(std::move(factory)) {
        engine.setInitialProperties(
            {{QStringLiteral("archiveController"), QVariant::fromValue(&archives)},
             {QStringLiteral("compressionController"), QVariant::fromValue(&compression)},
             {QStringLiteral("directoryModel"), QVariant::fromValue(&directories)}});
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(ANANARCHIVER_TEST_MAIN_QML)));
        check(!engine.rootObjects().isEmpty(), "Production compression QML failed to load");
        root = engine.rootObjects().first();
    }

    void dialog() {
        invoke(named(root, "compressAction"), "trigger");
        check(named(root, "compressionDialog")->property("opened").toBool(),
              "Create action did not open dialog");
    }

    void begin(const QString& destination, int format = 0, int method = 0, bool replace = false) {
        dialog();
        named(root, "compressionFormat")->setProperty("currentIndex", format);
        named(root, "compressionMethod")->setProperty("currentIndex", method);
        named(root, "compressionDestination")->setProperty("text", destination);
        named(root, "compressionReplaceExisting")->setProperty("checked", replace);
        check(named(root, "startCompression")->property("enabled").toBool(),
              "Creation form did not enable submit");
        if (format == 0)
            capture("compression-dialog");
        invoke(named(root, "startCompression"), "clicked");
        check(compression.busy(), "Create button did not enqueue an asynchronous worker");
        check(!named(root, "compressAction")->property("enabled").toBool(),
              "Concurrent creation remained enabled");
    }

    void complete() {
        check(waitUntil([&] { return !compression.busy(); }),
              "Compression worker did not complete");
        check(compression.status() == translated("Archive created"),
              "Creation failed: " + compression.errorDetails().toStdString());
        check(compression.canOpenCreated(), "Completed archive was not available to open");
    }

    void capture(const char* label) {
        const auto directory =
            qEnvironmentVariable("ANANARCHIVER_UI_CAPTURE_DIR",
                                 qEnvironmentVariable("ANANARCHIVER_TEST_SCREENSHOT_DIR"));
        if (directory.isEmpty())
            return;
        std::filesystem::create_directories(qt::toPath(directory));
        QEventLoop render;
        QTimer::singleShot(120, &render, &QEventLoop::quit);
        render.exec();
        const auto name = QString::fromUtf8(label) + QStringLiteral("-") +
                          qEnvironmentVariable("ANANARCHIVER_TEST_LANGUAGE", "en") +
                          QStringLiteral(".png");
        const auto image = qobject_cast<QQuickWindow*>(root)->grabWindow();
        check(!image.isNull() && image.save(directory + QStringLiteral("/") + name),
              "Could not capture Qt compression layout screenshot");
    }
};

void roundtripAllCapabilities() {
    QTemporaryDir temporary;
    check(temporary.isValid(), "Could not allocate compression test directory");
    const auto rootPath = qt::toPath(temporary.path());
    const auto directory = rootPath / pathFromUtf8("目录🐈");
    std::filesystem::create_directories(directory / "nested" / "empty");
    const QByteArray binary("\0\1\xff\nUnicode payload", 20);
    const auto filename = directory / "nested" / pathFromUtf8("中文こんにちは한국어café🐄.txt");
    writeFile(qt::fromPath(filename), binary);
    writeFile(temporary.path() + QStringLiteral("/standalone.txt"),
              QByteArray("standalone content"));
    View view;
    check(named(view.root, "compressAction")->property("enabled").toBool(),
          "Home did not enable archive creation");
    const auto capabilities = makeLibArchiveWriter()->capabilities();
    check(view.compression.formats().size() == static_cast<qsizetype>(capabilities.size()),
          "UI formats did not follow backend capabilities");
    view.dialog();
    const auto initialMethod =
        named(view.root, "compressionMethod")->property("currentIndex").toInt();
    check(initialMethod >= 0 &&
              static_cast<std::size_t>(initialMethod) < capabilities.front().methods.size() &&
              capabilities.front().methods[static_cast<std::size_t>(initialMethod)].method ==
                  CompressionRequest{}.method &&
              named(view.root, "compressionLevel")->property("value").toInt() ==
                  CompressionRequest{}.level,
          "New ZIP archives did not default to the Core's Deflate level 6 settings");
    auto* folderPicker = named(view.root, "compressionSourceFolder");
    folderPicker->setProperty("selectedFolder", QUrl::fromLocalFile(qt::fromPath(directory)));
    invoke(folderPicker, "accepted");
    auto* filePicker = named(view.root, "compressionSourceFiles");
    check(filePicker->property("fileMode").toInt() == 1,
          "Production input picker did not allow multiple file selections");
    filePicker->setProperty("selectedFile", QUrl::fromLocalFile(qt::fromPath(filename)));
    invoke(filePicker, "accepted");
    filePicker->setProperty(
        "selectedFile", QUrl::fromLocalFile(temporary.path() + QStringLiteral("/standalone.txt")));
    invoke(filePicker, "accepted");
    // Repeated native dialog selections must not add duplicate list rows.
    view.compression.addUrls({QUrl::fromLocalFile(qt::fromPath(directory))});
    check(view.compression.inputCount() == 3, "Repeated selections were not deduplicated");
    invoke(named(view.root, "compressionDialog"), "close");
    for (std::size_t format = 0; format < capabilities.size(); ++format) {
        const auto& capability = capabilities[format];
        check(!capability.methods.empty(), "Backend exposed a format without a method");
        const auto archive = rootPath / pathFromUtf8("创建🐈." + capability.extension);
        const auto methodIndex = 0;
        view.begin(qt::fromPath(archive), static_cast<int>(format), methodIndex);
        check(
            named(view.root, "compressionLevel")->property("value").toInt() ==
                capability.methods[static_cast<std::size_t>(methodIndex)].defaultLevel.value_or(0),
            "Format/method change did not apply the backend's default compression level");
        view.complete();
        check(view.compression.processedBytes() == 38 && view.compression.totalBytes() == 38,
              "Creation progress did not report actual source bytes");
        invoke(named(view.root, "openCreatedArchive"), "clicked");
        check(waitUntil([&] { return !view.archives.busy(); }), "Created archive did not open");
        check(view.archives.canExtract(), "Created archive could not be indexed");
        check(!view.compression.hasTask(),
              "Opening created archive did not restore browser status");
        const auto output = rootPath / ("roundtrip-" + std::to_string(format));
        view.archives.extract(qt::fromPath(output), false, {}, 0, 0);
        check(waitUntil([&] { return !view.archives.busy(); }),
              "Created archive extraction did not complete");
        check(view.archives.errorMessage().isEmpty(),
              "Roundtrip extraction failed: " + view.archives.errorDetails().toStdString());
        check(readFile(output / directory.filename() / "nested" / filename.filename()) == binary,
              "Unicode binary payload changed during UI roundtrip");
        check(readFile(output / "standalone.txt") == QByteArray("standalone content"),
              "Independent input was lost");
        check(std::filesystem::is_directory(output / directory.filename() / "nested" / "empty"),
              "Empty directory was lost");
        std::cout << "PASS production QML create/open/extract " << capability.name << '\n';
    }
}

struct Gate {
    std::mutex mutex;
    std::condition_variable_any changed;
    std::atomic<int> phase{};
    std::atomic<int> requestedLevel{-1};
};

class ControlledWriter final : public ArchiveWriter {
  public:
    explicit ControlledWriter(std::shared_ptr<Gate> gate) : gate_(std::move(gate)) {}
    std::vector<ArchiveCapabilities> capabilities() const override {
        return {{CompressionFormat::Zip,
                 "ZIP",
                 "zip",
                 {{CompressionMethod::Store, "Store", {}, {}, {}},
                  {CompressionMethod::Deflate, "Deflate", 0, 9, 6}}}};
    }
    Result<void> create(const CompressionRequest& request,
                        const CompressionProgressCallback& callback,
                        CancellationToken cancellation) override {
        gate_->requestedLevel.store(request.level);
        callback({CompressionPhase::Scanning, "nested/current.txt", 0, 42, {}, {}});
        std::unique_lock lock(gate_->mutex);
        if (!gate_->changed.wait(lock, cancellation, [&] { return gate_->phase.load() >= 1; }))
            return std::unexpected(
                ArchiveError{ArchiveErrorCode::Cancelled, "Cancelled during scan", {}, {}});
        callback({CompressionPhase::Writing, "current.txt", 1024, 1, 4096, 4});
        if (!gate_->changed.wait(lock, cancellation, [&] { return gate_->phase.load() >= 2; }))
            return std::unexpected(
                ArchiveError{ArchiveErrorCode::Cancelled, "Cancelled during write", {}, {}});
        callback({CompressionPhase::Finalizing, "output.zip", 4096, 4, 4096, 4});
        gate_->changed.wait(lock, cancellation, [] { return false; });
        return std::unexpected(
            ArchiveError{ArchiveErrorCode::Cancelled, "Cancelled during write", {}, {}});
    }

  private:
    std::shared_ptr<Gate> gate_;
};

void asynchronousPhasesAndCancellation() {
    auto gate = std::make_shared<Gate>();
    View view([gate] { return std::make_unique<ControlledWriter>(gate); });
    view.compression.addInput(
        qt::fromPath(std::filesystem::temp_directory_path() / "input-fixture"));
    int heartbeat = 0;
    QTimer alive;
    alive.setInterval(5);
    QObject::connect(&alive, &QTimer::timeout, [&] { ++heartbeat; });
    alive.start();
    view.root->setProperty("width", 760);
    view.root->setProperty("height", 480);
    check(waitUntil([&] { return heartbeat > 2; }),
          "Creation window did not initialize event handling");
    qobject_cast<QQuickWindow*>(view.root)->requestActivate();
    QKeyEvent createShortcut(QEvent::KeyPress, Qt::Key_N, Qt::ControlModifier);
    QCoreApplication::sendEvent(qobject_cast<QQuickWindow*>(view.root), &createShortcut);
    check(named(view.root, "compressionDialog")->property("opened").toBool(),
          "Ctrl+N did not open production creation dialog");
    check(waitUntil([&] {
              auto* button = qobject_cast<QQuickItem*>(named(view.root, "startCompression"));
              const auto topLeft = button->mapToScene(QPointF{});
              const auto bottomRight =
                  button->mapToScene(QPointF(button->width(), button->height()));
              return button->isVisible() && button->width() > 0 && topLeft.x() >= 0 &&
                     topLeft.y() >= 0 && bottomRight.x() <= 760 && bottomRight.y() <= 480;
          }),
          "Create/Cancel footer was clipped at the minimum window size");
    check(named(view.root, "compressionAddFiles")->property("activeFocus").toBool(),
          "Creation dialog did not establish keyboard focus");
    view.capture("compression-minimum-dialog");
    auto* replacement = qobject_cast<QQuickItem*>(named(view.root, "compressionReplaceExisting"));
    replacement->forceActiveFocus(Qt::TabFocusReason);
    check(waitUntil([&] {
              auto* viewport = qobject_cast<QQuickItem*>(named(view.root, "compressionFormScroll"));
              const auto top = replacement->mapToScene(QPointF{}).y();
              const auto viewportTop = viewport->mapToScene(QPointF{}).y();
              return top >= viewportTop &&
                     top + replacement->height() <= viewportTop + viewport->height();
          }),
          "Keyboard focus did not reveal replacement controls at the minimum window size");
    view.capture("compression-minimum-scrolled");
    named(view.root, "compressionMethod")->setProperty("currentIndex", 0);
    check(!named(view.root, "compressionLevel")->property("enabled").toBool(),
          "Store method enabled a fake compression level");
    named(view.root, "compressionMethod")->setProperty("currentIndex", 1);
    check(waitUntil([&] {
              return named(view.root, "compressionLevel")->property("enabled").toBool() &&
                     named(view.root, "compressionLevel")->property("value").toInt() == 6;
          }),
          "Changing to Deflate did not apply its capabilities and default level");
    named(view.root, "compressionLevel")->setProperty("value", 4);
    named(view.root, "compressionDestination")
        ->setProperty("text",
                      qt::fromPath(std::filesystem::temp_directory_path() / "output-fixture.zip"));
    invoke(named(view.root, "startCompression"), "clicked");
    check(waitUntil([&] { return view.compression.processedEntries() == 42; }),
          "Input scan progress was lost");
    check(gate->requestedLevel.load() == 4,
          "User-selected compression level did not reach the worker");
    check(view.compression.totalBytes() < 0 && view.compression.processedBytes() == 0 &&
              view.compression.bytesPerSecond() == 0,
          "Directory scanning fabricated payload bytes or throughput");
    check(named(view.root, "taskProgress")->property("indeterminate").toBool(),
          "Unknown scan totals showed a fabricated percent");
    const auto baseline = heartbeat;
    check(waitUntil([&] { return heartbeat > baseline + 5; }),
          "Input scan blocked GUI event handling");
    gate->phase.store(1);
    gate->changed.notify_all();
    check(waitUntil([&] { return view.compression.processedBytes() == 1024; }),
          "Writing bytes were lost");
    check(view.compression.totalBytes() == 4096 &&
              !named(view.root, "taskProgress")->property("indeterminate").toBool(),
          "Known writing totals did not switch to measured progress");
    view.capture("compression-progress");
    gate->phase.store(2);
    gate->changed.notify_all();
    check(waitUntil([&] { return view.compression.finalizing(); }),
          "Finalization phase was not delivered to the frontend");
    check(named(view.root, "taskProgress")->property("indeterminate").toBool() &&
              view.compression.remainingMilliseconds() < 0 && !view.compression.canOpenCreated(),
          "Uncommitted archive advertised completion or a zero-second finalization estimate");
    view.capture("compression-finalizing");
    invoke(named(view.root, "cancelAction"), "trigger");
    check(waitUntil([&] { return !view.compression.busy(); }),
          "Compression cancel did not wake worker");
    check(view.compression.status() == translated("Archive creation cancelled") &&
              !view.compression.canOpenCreated(),
          "Cancelled task advertised a created archive");
    std::cout << "PASS production QML capability gating, Ctrl+N, scan/write progress, heartbeat "
                 "and cancellation\n";
}

void realCancellationPreservesOutput() {
    QTemporaryDir temporary;
    check(temporary.isValid(), "Could not allocate cancellation fixture directory");
    const auto source = temporary.path() + QStringLiteral("/generated.bin");
    QFile input(source);
    check(input.open(QIODevice::WriteOnly), "Could not open generated input");
    std::minstd_rand random(271828);
    QByteArray buffer(1024 * 1024, '\0');
    for (int block = 0; block < 64; ++block) {
        for (auto& byte : buffer)
            byte = static_cast<char>(random());
        check(input.write(buffer) == buffer.size(), "Could not generate cancellation stream");
    }
    input.close();
    const auto destination = temporary.path() + QStringLiteral("/keep.zip");
    writeFile(destination, QByteArray("existing archive sentinel"));
    View view;
    view.compression.addUrls({QUrl::fromLocalFile(source)});
    bool cancelled = false;
    QObject::connect(&view.compression, &qt::CompressionController::changed, [&] {
        if (!cancelled && view.compression.busy() && view.compression.processedBytes() > 0 &&
            !view.compression.finalizing()) {
            cancelled = true;
            invoke(named(view.root, "cancelAction"), "trigger");
        }
    });
    const auto capabilities = makeLibArchiveWriter()->capabilities();
    int zip = -1;
    int deflate = -1;
    for (std::size_t format = 0; format < capabilities.size(); ++format) {
        if (capabilities[format].format != CompressionFormat::Zip)
            continue;
        zip = static_cast<int>(format);
        for (std::size_t method = 0; method < capabilities[format].methods.size(); ++method) {
            if (capabilities[format].methods[method].method == CompressionMethod::Deflate)
                deflate = static_cast<int>(method);
        }
    }
    check(zip >= 0 && deflate >= 0, "ZIP Deflate missing from required backend");
    view.begin(destination, zip, deflate, true);
    check(waitUntil([&] { return !view.compression.busy(); }),
          "Real creation cancellation did not complete");
    check(cancelled && view.compression.status() == translated("Archive creation cancelled"),
          "Generated stream completed before cancellation was exercised");
    check(readFile(qt::toPath(destination)) == QByteArray("existing archive sentinel"),
          "Cancelled replacement destroyed existing output");
    for (const auto& entry : std::filesystem::directory_iterator(qt::toPath(temporary.path())))
        check(entry.path().filename() == "generated.bin" || entry.path().filename() == "keep.zip",
              "Cancelled creation left a temporary output");
    std::cout << "PASS production QML real ZIP cancellation preserves existing output and cleans "
                 "temporary file\n";
}

void errorAndRetry() {
    QTemporaryDir temporary;
    check(temporary.isValid(), "Could not allocate failure fixture directory");
    writeFile(temporary.path() + QStringLiteral("/source.txt"), QByteArray("source"));
    writeFile(temporary.path() + QStringLiteral("/exists.zip"), QByteArray("retain"));
    View view;
    view.compression.addInput(temporary.path() + QStringLiteral("/source.txt"));
    view.begin(temporary.path() + QStringLiteral("/exists.zip"));
    check(waitUntil([&] { return !view.compression.busy(); }),
          "Existing-output failure did not finish");
    check(!view.compression.errorMessage().isEmpty() &&
              named(view.root, "compressionErrorDialog")->property("opened").toBool(),
          "Creation error did not show localized production error dialog");
    check(readFile(qt::toPath(temporary.path() + QStringLiteral("/exists.zip"))) ==
              QByteArray("retain"),
          "Unconfirmed replacement modified output");
    view.capture("compression-error");
    invoke(named(view.root, "compressionErrorDialog"), "accept");
    check(waitUntil([&] { return view.compression.errorMessage().isEmpty(); }),
          "Creation error could not be dismissed");
    view.begin(temporary.path() + QStringLiteral("/exists.zip"), 0, 0, true);
    view.complete();
    check(readFile(qt::toPath(temporary.path() + QStringLiteral("/exists.zip"))) !=
              QByteArray("retain"),
          "Confirmed successful replacement did not install output");
    std::cout
        << "PASS production QML output-exists error, retained destination and replacement retry\n";
}

} // namespace

int main(int argc, char* argv[]) {
#ifdef _WIN32
    // The offscreen plugin uses QBasicFontDatabase and has no native Windows
    // font discovery. Match the real frontend's UI font for readable layout QA.
    if (qEnvironmentVariable("QT_QPA_PLATFORM") == QStringLiteral("offscreen") &&
        qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR")) {
        qputenv("QT_QPA_FONTDIR",
                QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts")).toUtf8());
    }
#endif
    QGuiApplication application(argc, argv);
#ifdef _WIN32
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
        auto font = QGuiApplication::font();
        font.setFamily(QStringLiteral("Segoe UI"));
        QGuiApplication::setFont(font);
    }
#endif
    qt::FrontendTranslator translator;
    if (qEnvironmentVariable("ANANARCHIVER_TEST_LANGUAGE") == QStringLiteral("zh-CN")) {
        QLocale::setDefault(QLocale(QStringLiteral("zh_CN")));
        application.installTranslator(&translator);
    }
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    try {
        asynchronousPhasesAndCancellation();
        roundtripAllCapabilities();
        realCancellationPreservesOutput();
        errorAndRetry();
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
    return 0;
}

#include "controllers/archive_controller.h"
#include "controllers/compression_controller.h"
#include "models/file_tree_model.h"
#include "platform/path_conversion.h"
#include "platform/frontend_translator.h"
#include "../../../tests/security/extraction_fixtures.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QLocale>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <iostream>
#include <clocale>

namespace {

using namespace ananarchiver;
using namespace extraction_tests;

bool waitUntil(const std::function<bool()>& predicate) {
    QElapsedTimer timeout;
    timeout.start();
    while (!predicate() && timeout.elapsed() < 10000) {
        QEventLoop loop;
        QTimer::singleShot(10, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return predicate();
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
    return QCoreApplication::translate("ArchiveController", text);
}

struct View {
    qt::ArchiveController controller;
    qt::CompressionController compression;
    qt::FileTreeModel directories;
    QQmlApplicationEngine engine;
    QObject* root{};

    explicit View(application::ReaderFactory factory = {}) : controller(std::move(factory)) {
        engine.setInitialProperties({
            {QStringLiteral("archiveController"), QVariant::fromValue(&controller)},
            {QStringLiteral("compressionController"), QVariant::fromValue(&compression)},
            {QStringLiteral("directoryModel"), QVariant::fromValue(&directories)}});
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(ANANARCHIVER_TEST_MAIN_QML)));
        check(!engine.rootObjects().isEmpty(), "Production extraction QML failed to load");
        root = engine.rootObjects().first();
    }

    void open(const std::filesystem::path& archive) {
        controller.openFile(qt::fromPath(archive));
        check(waitUntil([&] { return !controller.busy(); }), "Archive indexing did not complete");
        check(controller.canExtract(), "Indexed fixture did not enable extraction");
    }

    void begin(const std::filesystem::path& destination, const QString& password = {}, int policy = 0) {
        invoke(named(root, "extractAction"), "trigger");
        check(named(root, "extractionDialog")->property("opened").toBool(), "Extract action did not open dialog");
        named(root, "extractionDestination")->setProperty("text", qt::fromPath(destination));
        named(root, "extractionPassword")->setProperty("text", password);
        named(root, "overwritePolicy")->setProperty("currentIndex", policy);
        capture("extraction-dialog");
        invoke(named(root, "startExtraction"), "clicked");
        check(controller.busy(), "Production Extract button did not start an asynchronous task");
        check(named(root, "extractionPassword")->property("text").toString().isEmpty(),
              "Password input retained a task secret after submission");
    }

    void complete() {
        check(waitUntil([&] { return !controller.busy(); }), "Extraction did not complete");
        check(controller.status() == translated("Extraction complete"),
              "Extraction failed: " + controller.errorDetails().toStdString());
    }

    void capture(const char* label) {
        const auto directory = qEnvironmentVariable("ANANARCHIVER_TEST_SCREENSHOT_DIR");
        if (directory.isEmpty())
            return;
        std::filesystem::create_directories(qt::toPath(directory));
        QEventLoop render;
        QTimer::singleShot(120, &render, &QEventLoop::quit);
        render.exec();
        const auto name = QString::fromUtf8(label) + QStringLiteral("-")
            + qEnvironmentVariable("ANANARCHIVER_TEST_LANGUAGE", "en") + QStringLiteral(".png");
        const auto image = qobject_cast<QQuickWindow*>(root)->grabWindow();
        check(!image.isNull() && image.save(directory + QStringLiteral("/") + name),
              "Could not capture requested Qt layout QA screenshot");
    }

    void clickRow(int row, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QQuickItem* item{};
        check(waitUntil([&] {
            QMetaObject::invokeMethod(named(root, "archiveEntries"), "itemAtIndex",
                Qt::DirectConnection, Q_RETURN_ARG(QQuickItem*, item), Q_ARG(int, row));
            return item != nullptr;
        }), "Archive delegate did not appear");
        auto* window = qobject_cast<QQuickWindow*>(root);
        const auto position = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        QMouseEvent down(QEvent::MouseButtonPress, position, position, position,
                         Qt::LeftButton, Qt::LeftButton, modifiers);
        QCoreApplication::sendEvent(window, &down);
        QMouseEvent up(QEvent::MouseButtonRelease, position, position, position,
                       Qt::LeftButton, Qt::NoButton, modifiers);
        QCoreApplication::sendEvent(window, &up);
    }
};

void realExtractionUi() {
    TemporaryDirectory temp;
    const auto archive = temp.path / pathFromUtf8("压缩包🐈.zip");
    createFixture(archive, FixtureFormat::Zip, {{"first.txt", "new first"}, {"中文🐈.txt", "unicode output"}});
    View view;
    check(!named(view.root, "extractAction")->property("enabled").toBool(), "Home enabled Extract");
    view.open(archive);
    auto* list = qobject_cast<QQuickItem*>(named(view.root, "archiveEntries"));
    check(list != nullptr, "Archive entries ListView missing");
    list->forceActiveFocus();
    QKeyEvent selectAll(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
    QCoreApplication::sendEvent(qobject_cast<QQuickWindow*>(view.root), &selectAll);
    check(view.controller.selectionCount() == 2, "Production Ctrl+A did not select the archive");
    view.controller.clearSelection();
    view.clickRow(0);
    view.clickRow(1, Qt::ControlModifier);
    check(view.controller.selectionCount() == 2, "Production Ctrl+click did not add selection");
    view.clickRow(0, Qt::ShiftModifier);
    check(view.controller.selectionCount() == 2, "Production Shift+click did not select range");
    view.controller.clearSelection();

    const auto destination = temp.path / "output";
    std::filesystem::create_directory(destination);
    writeFile(destination / "first.txt", "old first");
    int heartbeat = 0;
    QTimer alive;
    alive.setInterval(5);
    QObject::connect(&alive, &QTimer::timeout, [&] { ++heartbeat; });
    alive.start();
    view.begin(destination);
    check(waitUntil([&] { return view.controller.awaitingConflict(); }), "Existing file did not request a decision");
    check(named(view.root, "overwriteDialog")->property("opened").toBool(), "Conflict dialog did not open");
    view.capture("overwrite-dialog");
    check(named(view.root, "conflictDestination")->property("textFormat").toInt() == 0,
          "Overwrite destination is not plain text");
    const auto baseline = heartbeat;
    check(waitUntil([&] { return heartbeat > baseline + 5; }), "Overwrite wait blocked the GUI event loop");
    named(view.root, "applyToAll")->setProperty("checked", true);
    invoke(named(view.root, "resolveOverwrite"), "clicked");
    view.complete();
    check(readFile(destination / "first.txt") == "new first", "Overwrite did not replace requested data");
    check(readFile(destination / pathFromUtf8("中文🐈.txt")) == "unicode output", "Unicode extraction output changed");
    check(view.controller.processedBytes() == 23 && view.controller.totalBytes() == 23,
          "Controller did not report actual written bytes");
    check(view.controller.canExtract(), "Completed extraction disabled future extraction");

    const auto cancelled = temp.path / "cancelled";
    std::filesystem::create_directory(cancelled);
    writeFile(cancelled / "first.txt", "keep existing");
    view.begin(cancelled);
    check(waitUntil([&] { return view.controller.awaitingConflict(); }), "Cancellation fixture did not wait for conflict");
    invoke(named(view.root, "cancelExtraction"), "clicked");
    check(waitUntil([&] { return !view.controller.busy(); }), "Cancellation did not wake conflict worker");
    check(view.controller.status() == translated("Extraction cancelled"), "Cancelled extraction reported success");
    check(readFile(cancelled / "first.txt") == "keep existing", "Cancelled conflict destroyed existing data");
    check(!std::filesystem::exists(cancelled / pathFromUtf8("中文🐈.txt")), "Cancellation left a later output");
    check(!named(view.root, "overwriteDialog")->property("opened").toBool(), "Cancelled conflict dialog remained open");
    std::cout << "PASS production QML extraction, Unicode, Ctrl+A, overwrite, bytes, event-loop responsiveness and cancellation\n";
}

void duplicateSelectionUi() {
    TemporaryDirectory temp;
    const auto archive = temp.path / "duplicates.tar";
    createFixture(archive, FixtureFormat::Tar, {{"same.txt", "first"}, {"same.txt", "second"}, {"other.txt", "other"},
        {"folder/child.txt", "child"}, {"folder/", {}, EntryType::Directory}});
    View view;
    view.open(archive);
    view.clickRow(1);
    view.begin(temp.path / "selected");
    check(named(view.root, "extractSelectedScope")->property("checked").toBool(), "Selection did not choose Selected scope");
    view.complete();
    check(readFile(temp.path / "selected" / "same.txt") == "second", "Duplicate path selected the wrong ordinal entry");
    check(!std::filesystem::exists(temp.path / "selected" / "other.txt"), "Selected extraction included an unselected file");
    check(view.controller.processedBytes() == 6, "Selected byte progress counted unselected data");
    view.clickRow(4);
    view.begin(temp.path / "selected-directory");
    view.complete();
    check(readFile(temp.path / "selected-directory" / "folder" / "child.txt") == "child",
          "Selected directory did not include descendants before its own header");
    std::cout << "PASS production QML selected extraction keeps duplicate entry identities\n";
}

void passwordExtractionUi() {
    TemporaryDirectory temp;
    const auto archive = temp.path / "encrypted.zip";
    createFixture(archive, FixtureFormat::Zip, {{"secret.txt", "private fixture"}}, true);
    View view;
    view.open(archive);
    view.begin(temp.path / "wrong", QStringLiteral("incorrect password"));
    check(waitUntil([&] { return !view.controller.busy(); }), "Wrong-password extraction did not terminate");
    check(!view.controller.errorMessage().isEmpty() && view.controller.canExtract(), "Wrong password did not permit retry");
    check(!std::filesystem::exists(temp.path / "wrong" / "secret.txt"), "Wrong password left output data");
    invoke(named(view.root, "errorDialog"), "accept");
    check(waitUntil([&] { return view.controller.errorMessage().isEmpty(); }), "Error dialog did not dismiss the password failure");
    view.begin(temp.path / "correct", QStringLiteral("test-only-passphrase"));
    view.complete();
    check(readFile(temp.path / "correct" / "secret.txt") == "private fixture", "Encrypted extraction bytes changed");
    std::cout << "PASS production QML password submission, secret-input clearing, failure cleanup and retry\n";
}

class HeaderEncryptedReader final : public ArchiveReader {
public:
    Result<ArchiveInfo> open(const std::filesystem::path& path, CancellationToken) override {
        return std::unexpected(ArchiveError{ArchiveErrorCode::PasswordRequired, "Encrypted headers", {}, path});
    }
    Result<ArchiveInfo> open(const std::filesystem::path& path, std::string_view password, CancellationToken) override {
        if (password != "correct")
            return std::unexpected(ArchiveError{ArchiveErrorCode::WrongPassword, "Bad password", {}, path});
        ArchiveInfo info;
        info.path = path;
        info.formatName = "Header-encrypted test reader";
        return info;
    }
    Result<void> enumerate(const EntryCallback& callback, CancellationToken) override {
        ArchiveEntry entry;
        entry.path = "visible-after-password.txt";
        entry.type = EntryType::RegularFile;
        callback(entry);
        return {};
    }
};

void headerPasswordUi() {
    View view([] { return std::make_unique<HeaderEncryptedReader>(); });
    view.controller.openFile(QStringLiteral("encrypted-header-fixture"));
    check(waitUntil([&] { return view.controller.passwordRequested(); }), "Encrypted headers did not request password");
    check(named(view.root, "indexPasswordDialog")->property("opened").toBool(), "Index password prompt did not open");
    named(view.root, "indexPassword")->setProperty("text", QStringLiteral("correct"));
    invoke(named(view.root, "indexPasswordDialog"), "accept");
    check(waitUntil([&] { return view.controller.canExtract(); }), "Index password retry did not finish");
    check(view.controller.entryCount() == 1, "Password retry did not index entries");
    check(named(view.root, "indexPassword")->property("text").toString().isEmpty(), "Header password input was retained");
    std::cout << "PASS production QML header-encrypted indexing password retry\n";
}

}

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    qt::FrontendTranslator translator;
    if (qEnvironmentVariable("ANANARCHIVER_TEST_LANGUAGE") == QStringLiteral("zh-CN")) {
        QLocale::setDefault(QLocale(QStringLiteral("zh_CN")));
        app.installTranslator(&translator);
    }
    // Fixture writers translate UTF-8 entry names through the process locale.
    // Establish it once after Qt initializes locale and before workers exist.
#ifdef _WIN32
    std::setlocale(LC_ALL, ".UTF-8");
#else
    std::setlocale(LC_ALL, "C.UTF-8");
#endif
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    try {
        headerPasswordUi();
        realExtractionUi();
        duplicateSelectionUi();
        passwordExtractionUi();
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
    return 0;
}

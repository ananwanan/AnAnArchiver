#include "controllers/archive_controller.h"
#include "controllers/compression_controller.h"
#include "controllers/desktop_controller.h"
#include "controllers/drag_controller.h"
#include "controllers/task_controller.h"
#include "models/file_tree_model.h"
#include "platform/frontend_translator.h"
#include "platform/path_conversion.h"
#include "../../../tests/security/extraction_fixtures.h"
#include <QClipboard>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFont>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLocale>
#include <QMimeData>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>

using namespace ananarchiver;
using namespace extraction_tests;
namespace {
QStringList messages;
void messageHandler(QtMsgType, const QMessageLogContext&, const QString& text) { messages.append(text); }
bool waitUntil(const std::function<bool()>& ready) {
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < 10000) {
        QEventLoop loop;
        QTimer::singleShot(10, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return ready();
}
void settle() { QEventLoop loop; QTimer::singleShot(150, &loop, &QEventLoop::quit); loop.exec(); }
QObject* named(QObject* root, const char* name) {
    auto* result = root->findChild<QObject*>(QString::fromUtf8(name));
    check(result != nullptr, std::string("Missing production UI: ") + name);
    return result;
}
void invoke(QObject* item, const char* method) {
    check(QMetaObject::invokeMethod(item, method, Qt::DirectConnection), std::string("Cannot invoke ") + method);
}
void keyboard(QQuickWindow* window, int key, Qt::KeyboardModifiers modifiers = {}) {
    QKeyEvent press(QEvent::KeyPress, key, modifiers), release(QEvent::KeyRelease, key, modifiers);
    QCoreApplication::sendEvent(window, &press);
    QCoreApplication::sendEvent(window, &release);
    settle();
}
struct View {
    QTemporaryDir settings;
    qt::ArchiveController archive;
    qt::CompressionController compression;
    qt::FileTreeModel directory;
    qt::DesktopController desktop{settings.filePath(QStringLiteral("settings.ini"))};
    qt::TaskController tasks;
    qt::DragController drag{archive, compression};
    QQmlApplicationEngine engine;
    QQuickWindow* window{};
    View() {
        QObject::connect(&archive, &qt::ArchiveController::archiveIndexed, &desktop, &qt::DesktopController::rememberArchive);
        QObject::connect(&desktop, &qt::DesktopController::openRequested, &archive, &qt::ArchiveController::openFile);
        QObject::connect(&drag, &qt::DragController::openRequested, &archive, &qt::ArchiveController::openFile);
        QObject::connect(&archive, &qt::ArchiveController::taskObserved, &tasks,
            [this](const auto& snapshot, const auto& path) {
                tasks.observe(QStringLiteral("archive"), snapshot, path, [this](auto id) { archive.cancelTask(id); });
            });
        engine.setInitialProperties({
            {QStringLiteral("archiveController"), QVariant::fromValue(&archive)},
            {QStringLiteral("compressionController"), QVariant::fromValue(&compression)},
            {QStringLiteral("directoryModel"), QVariant::fromValue(&directory)},
            {QStringLiteral("desktopController"), QVariant::fromValue(&desktop)},
            {QStringLiteral("taskController"), QVariant::fromValue(&tasks)},
            {QStringLiteral("dragController"), QVariant::fromValue(&drag)}});
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(ANANARCHIVER_TEST_MAIN_QML)));
        check(!engine.rootObjects().isEmpty(), "M4 production QML failed to load");
        window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        check(window != nullptr, "M4 root is not a window");
        window->requestActivate();
        settle();
    }
    void capture(const QString& name) {
        const auto directoryName = qEnvironmentVariable("ANANARCHIVER_TEST_CAPTURE_DIR");
        if (directoryName.isEmpty()) return;
        QDir().mkpath(directoryName);
        settle();
        check(window->grabWindow().save(directoryName + QChar(u'/') + name + QStringLiteral(".png")), "M4 screenshot failed");
    }
    void drop(const QString& path) {
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(path)});
        QDragEnterEvent enter(QPoint(400, 220), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &enter);
        check(enter.isAccepted(), "Production DropArea did not accept local file URLs");
        QDropEvent event(QPointF(400, 220), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &event);
        check(event.isAccepted(), "Production DropArea rejected the drop");
    }
};
void flow(const QString& language) {
    TemporaryDirectory fixture;
    const auto file = fixture.path / pathFromUtf8("归档🐈.dat");
    createFixture(file, FixtureFormat::Zip, {{"z.txt", "z"}, {"dup.txt", "first"},
        {"dup.txt", "second payload"}, {"中文/安安.txt", "unicode"}});
    View view;
    view.window->setProperty("themeMode", 1);
    view.capture(language + QStringLiteral("-home-light"));
    view.window->setProperty("themeMode", 2);
    check(view.desktop.themeMode() == 2 && view.window->property("darkTheme").toBool(), "UI theme was not persisted to controller");
    view.capture(language + QStringLiteral("-home-dark"));
    view.drop(qt::fromPath(file));
    check(waitUntil([&] { return !view.drag.busy() && !view.archive.busy() && view.archive.canExtract(); }), "UI drop/open pipeline did not index");
    check(view.archive.entryCount() == 4 && view.desktop.recentArchives().size() == 1, "Content drop/recent indexing failed");
    keyboard(view.window, Qt::Key_F, Qt::ControlModifier);
    auto* search = named(view.window, "archiveSearch");
    check(search->property("activeFocus").toBool(), "Ctrl+F did not focus search");
    search->setProperty("text", QStringLiteral("dup"));
    invoke(search, "textEdited");
    auto* model = qobject_cast<qt::ArchiveEntryModel*>(view.archive.entryModel());
    check(waitUntil([&] { return model->query() == QStringLiteral("dup") && !model->querying(); }), "UI debounced search did not complete");
    check(model->count() == 2 && view.archive.entryCount() == 4, "Search lost archive totals");
    invoke(named(view.window, "sortSize"), "clicked");
    check(waitUntil([&] { return !model->querying(); }), "UI size sort did not complete");
    invoke(named(view.window, "sortSize"), "clicked");
    check(waitUntil([&] { return !model->querying(); }), "UI descending size sort did not complete");
    view.archive.selectRow(0);
    check(model->selectedIndices() == std::vector<std::uint64_t>{2}, "Sorted UI selected the wrong duplicate");
    const auto output = fixture.path / "sorted-extract";
    view.archive.extract(qt::fromPath(output), true, {}, 1, 2);
    check(waitUntil([&] { return !view.archive.busy(); }), "Filtered selected extraction timed out");
    check(readFile(output / "dup.txt") == "second payload", "Filtered extraction restored the wrong payload");
    check(view.tasks.count() == 2 && view.tasks.activeCount() == 0, "Task history lost UI extraction");
    view.capture(language + QStringLiteral("-browser-dark"));
    keyboard(view.window, Qt::Key_J, Qt::ControlModifier);
    auto* center = named(view.window, "taskCenter");
    check(center->property("opened").toBool(), "Ctrl+J did not open task center");
    check(named(view.window, "taskList")->property("count").toInt() == 2, "Task center did not bind the history model");
    view.capture(language + QStringLiteral("-tasks-dark"));
    invoke(named(view.window, "clearFinishedTasks"), "clicked");
    check(view.tasks.count() == 0, "UI clear finished failed");
    invoke(center, "close");
    keyboard(view.window, Qt::Key_F, Qt::ControlModifier);
    keyboard(view.window, Qt::Key_Escape);
    check(model->query().isEmpty(), "Search Escape did not clear query");
    check(waitUntil([&] { return !model->querying(); }), "Search clearing did not settle");
    view.archive.selectRow(0);
    invoke(named(view.window, "exportAction"), "trigger");
    check(named(view.window, "exportDialog")->property("opened").toBool(), "Export action did not open preparation dialog");
    invoke(named(view.window, "prepareExport"), "clicked");
    check(waitUntil([&] { return !view.drag.busy(); }), "UI export preparation did not finish");
    check(view.drag.ready(), "UI export preparation did not produce safe file URLs");
    view.archive.openFile(qt::fromPath(fixture.path / "missing.zip"));
    check(waitUntil([&] { return !view.archive.busy(); }), "Error task did not finish");
    auto* error = named(view.window, "errorDialog");
    check(error->property("opened").toBool(), "Error detail dialog did not open");
    auto* details = named(error, "errorDetailsText");
    check(details->property("text").toString().contains(QStringLiteral("missing.zip")), "Error detail lost path");
    invoke(named(error, "copyErrorDetails"), "clicked");
    check(QGuiApplication::clipboard()->text().contains(QStringLiteral("missing.zip")), "Copy error details failed");
    view.capture(language + QStringLiteral("-error-dark"));
    invoke(error, "close");
    invoke(named(view.window, "homeButton"), "clicked");
    check(view.archive.currentFile().isEmpty(), "Home did not close archive");
    view.window->setWidth(760); view.window->setHeight(480);
    view.capture(language + QStringLiteral("-home-small-dark"));
    check(named(view.window, "recentArchives")->property("count").toInt() == 1, "Home recent list lost successful archive");
    auto* recentView = qobject_cast<QQuickItem*>(named(view.window, "recentArchives"));
    check(recentView && recentView->height() >= 60, "Small Home hid recent archives beneath its layout");
    const auto path = view.desktop.recentArchives().first().toMap().value(QStringLiteral("path")).toString();
    view.desktop.openRecent(path);
    check(waitUntil([&] { return !view.archive.busy(); }), "Recent reopen timed out");
    check(view.archive.canExtract(), "Recent reopen did not recover after failure");
    view.capture(language + QStringLiteral("-browser-small-dark"));
    view.window->setProperty("themeMode", 1);
    view.capture(language + QStringLiteral("-browser-small-light"));
    const auto ordinary = fixture.path / "plain.zip";
    writeFile(ordinary, "plain data");
    view.drop(qt::fromPath(ordinary));
    check(waitUntil([&] { return !view.drag.busy(); }), "UI ordinary file drop timed out");
    check(named(view.window, "compressionDialog")->property("opened").toBool() && view.compression.inputCount() == 1,
        "UI file drop did not open populated creation dialog");
    invoke(named(view.window, "compressionDialog"), "close");
}
} // namespace
int main(int argc, char** argv) {
#ifdef _WIN32
    if (qEnvironmentVariable("QT_QPA_PLATFORM") == QStringLiteral("offscreen") && qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR"))
        qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts")).toUtf8());
#endif
    QGuiApplication app(argc, argv);
#ifdef _WIN32
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
        auto font = QGuiApplication::font();
        font.setFamily(QStringLiteral("Segoe UI"));
        QGuiApplication::setFont(font);
    }
#endif
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    qInstallMessageHandler(messageHandler);
    qt::FrontendTranslator translator;
    const auto language = qEnvironmentVariable("ANANARCHIVER_TEST_LANGUAGE", "en");
    QLocale::setDefault(QLocale(language == QStringLiteral("zh-CN") ? QStringLiteral("zh_CN") : QStringLiteral("en_US")));
    if (language == QStringLiteral("zh-CN")) app.installTranslator(&translator);
    try {
        flow(language);
        for (const auto& message : messages)
            check(!message.contains(QStringLiteral("Binding loop")) && !message.contains(QStringLiteral("Unable to assign"))
                && !message.contains(QStringLiteral("TypeError")) && !message.contains(QStringLiteral("Cannot open")),
                "Production QML emitted a runtime binding or image error");
        std::cout << "PASS production M4 URL drop, search, sorting, selected extraction, tasks, shortcuts, themes, recents and errors\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        for (const auto& message : messages) std::cerr << message.toStdString() << '\n';
        return 1;
    }
}

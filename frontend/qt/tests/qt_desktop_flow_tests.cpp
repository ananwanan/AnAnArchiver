#include "controllers/archive_controller.h"
#include "app/build_info.h"
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
#include <QMouseEvent>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStyleHints>
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
QQuickItem* visualItem(QQuickItem* root, const QString& name) {
    if (!root) return nullptr;
    if (root->objectName() == name) return root;
    for (auto* child : root->childItems())
        if (auto* found = visualItem(child, name)) return found;
    return nullptr;
}
QQuickItem* folderRow(QQuickItem* root, const QString& path) {
    if (!root) return nullptr;
    if (root->objectName() == QStringLiteral("archiveFolderRow")
        && root->property("folderPath").toString() == path)
        return root;
    for (auto* child : root->childItems())
        if (auto* found = folderRow(child, path)) return found;
    return nullptr;
}
bool displaysText(QQuickItem* root, const QString& text) {
    if (!root) return false;
    if (root->property("text").toString() == text) return true;
    for (auto* child : root->childItems())
        if (displaysText(child, text)) return true;
    return false;
}
QObject* namedVisual(QObject* root, const char* name) {
    auto* result = visualItem(qobject_cast<QQuickItem*>(root), QString::fromUtf8(name));
    check(result != nullptr, std::string("Missing production list delegate: ") + name);
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
int entryRow(qt::ArchiveEntryModel* model, const QString& path, bool directory = false, int occurrence = 0) {
    for (int row = 0; row < model->count(); ++row) {
        const auto index = model->index(row);
        if (model->data(index, qt::ArchiveEntryModel::PathRole).toString() == path
            && model->data(index, qt::ArchiveEntryModel::DirectoryRole).toBool() == directory
            && occurrence-- == 0)
            return row;
    }
    check(false, "Archive row missing: " + path.toStdString());
    return -1;
}
struct View {
    QTemporaryDir settings;
    qt::ArchiveController archive;
    qt::CompressionController compression;
    qt::FileTreeModel directory;
    qt::DesktopController desktop{settings.filePath(QStringLiteral("settings.ini"))};
    qt::TaskController tasks;
    qt::DragController drag{archive, compression};
    qt::BuildInfo buildInfo;
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
            {QStringLiteral("buildInfo"), QVariant::fromValue(&buildInfo)},
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
    void click(QQuickItem* item) {
        const auto position = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        const auto timestamp = static_cast<ulong>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        QMouseEvent down(QEvent::MouseButtonPress, position, position, position,
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        down.setTimestamp(timestamp);
        QCoreApplication::sendEvent(window, &down);
        QMouseEvent up(QEvent::MouseButtonRelease, position, position, position,
            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        up.setTimestamp(timestamp + 1);
        QCoreApplication::sendEvent(window, &up);
        settle();
    }
    QQuickItem* fileRow(int row) {
        QQuickItem* item{};
        check(waitUntil([&] {
            QMetaObject::invokeMethod(named(window, "archiveEntries"), "itemAtIndex", Qt::DirectConnection,
                Q_RETURN_ARG(QQuickItem*, item), Q_ARG(int, row));
            return item != nullptr;
        }), "Archive file or folder delegate did not appear");
        return item;
    }
    void doubleClick(QQuickItem* item) {
        click(item);
        const auto position = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        const auto timestamp = static_cast<ulong>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        QMouseEvent down(QEvent::MouseButtonPress, position, position, position,
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        down.setTimestamp(timestamp);
        QCoreApplication::sendEvent(window, &down);
        QMouseEvent up(QEvent::MouseButtonRelease, position, position, position,
            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        up.setTimestamp(timestamp + 1);
        QCoreApplication::sendEvent(window, &up);
        settle();
    }
    void drop(const QString& path) {
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(path)});
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
        QDragEnterEvent enter(QPointF(400, 220), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
#else
        QDragEnterEvent enter(QPoint(400, 220), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
#endif
        QCoreApplication::sendEvent(window, &enter);
        check(enter.isAccepted(), "Production DropArea did not accept local file URLs");
        QDropEvent event(QPointF(400, 220), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &event);
        check(event.isAccepted(), "Production DropArea rejected the drop");
    }
};
void archiveFolders(View& view, const QString& language) {
    auto* model = qobject_cast<qt::ArchiveEntryModel*>(view.archive.entryModel());
    auto* tree = qobject_cast<QQuickItem*>(named(view.window, "archiveFolderTree"));
    auto* entries = qobject_cast<QQuickItem*>(named(view.window, "archiveEntries"));
    auto* currentFolder = named(view.window, "currentArchiveFolder");
    auto* up = named(view.window, "archiveUpButton");
    check(model && tree && entries, "Archive folder browser did not bind its models");
    check(waitUntil([&] { return !model->querying() && model->count() == 5; }),
        "Archive root did not show its direct folders and files");
    check(model->data(model->index(0), qt::ArchiveEntryModel::DirectoryRole).toBool()
        && model->data(model->index(1), qt::ArchiveEntryModel::DirectoryRole).toBool()
        && !model->data(model->index(2), qt::ArchiveEntryModel::DirectoryRole).toBool(),
        "Direct archive folders were not listed before files");
    check(view.archive.property("currentFolder").toString().isEmpty() && !up->property("enabled").toBool(),
        "Archive did not open at its root");
    const auto rootLabel = language == QStringLiteral("zh-CN") ? QString::fromUtf8("压缩包根目录") : QStringLiteral("Archive root");
    check(named(view.window, "archiveRootButton")->property("text").toString() == rootLabel,
        "Archive root navigation was not localized");
    view.capture(language + QStringLiteral("-archive-root"));
    auto* search = named(view.window, "archiveSearch");
    search->setProperty("text", QString::fromUtf8("安安"));
    invoke(search, "textEdited");
    check(waitUntil([&] { return model->query() == QString::fromUtf8("安安") && !model->querying(); }) && model->count() == 0,
        "Archive search exposed a file from another folder");
    invoke(named(view.window, "clearArchiveSearch"), "clicked");
    check(waitUntil([&] { return model->query().isEmpty() && !model->querying() && model->count() == 5; }),
        "Clearing a scoped search did not restore root folders and files");
    const auto verifyFolder = [&](const QString& path, const QString& expectedFile, int expectedCount) {
        check(waitUntil([&] { return view.archive.property("currentFolder").toString() == path && !model->querying(); }),
            "Archive folder activation did not update the current scope");
        check(currentFolder->property("text").toString() == QChar(u'/') + path,
            "Folder location did not show the selected archive path");
        check(currentFolder->property("textFormat").toInt() == 0,
            "Archive folder path reached a rich-text renderer");
        check(model->count() == expectedCount && entries->property("count").toInt() == expectedCount,
            "Archive list did not contain only direct folders and files of the selected folder");
        if (!expectedFile.isEmpty()) {
            entryRow(model, path + QChar(u'/') + expectedFile);
            check(waitUntil([&] { return displaysText(entries, expectedFile); }),
                "File row did not render the basename in the selected folder");
        }
    };
    const auto navigate = [&](const QString& path, const QString& expectedFile, int expectedCount) {
        check(waitUntil([&] { return folderRow(tree, path) != nullptr; }),
            "Archive folder tree lost an explicit or implicit directory");
        view.click(folderRow(tree, path));
        verifyFolder(path, expectedFile, expectedCount);
    };
    const auto chinese = QString::fromUtf8("中文");
    const auto nested = QString::fromUtf8("中文/嵌套");
    view.click(view.fileRow(entryRow(model, chinese, true)));
    check(view.archive.currentFolder().isEmpty() && view.archive.selectionCount() == 1,
        "A single click did not select the implicit folder without entering it");
    QEventLoop doubleClickBoundary;
    QTimer::singleShot(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 20, &doubleClickBoundary, &QEventLoop::quit);
    doubleClickBoundary.exec();
    view.doubleClick(view.fileRow(entryRow(model, chinese, true)));
    verifyFolder(chinese, QString::fromUtf8("安安.txt"), 2);
    check(view.archive.selectionCount() == 0, "Folder navigation retained a hidden selection");
    check(up->property("enabled").toBool(), "Parent-folder navigation stayed disabled below the root");
    view.capture(language + QStringLiteral("-archive-direct-children"));
    view.click(view.fileRow(entryRow(model, nested, true)));
    keyboard(view.window, Qt::Key_Return);
    verifyFolder(nested, QStringLiteral("deep.txt"), 1);
    check(!named(view.window, "extractionDialog")->property("opened").toBool(),
        "Enter on an archive folder opened extraction instead of entering the folder");
    view.click(view.fileRow(entryRow(model, nested + QStringLiteral("/deep.txt"))));
    keyboard(view.window, Qt::Key_Return);
    check(named(view.window, "extractionDialog")->property("opened").toBool(),
        "Enter on a file stopped opening the extraction dialog");
    invoke(named(view.window, "extractionDialog"), "close");
    invoke(up, "clicked");
    verifyFolder(chinese, QString::fromUtf8("安安.txt"), 2);
    navigate(chinese, QString::fromUtf8("安安.txt"), 2);
    keyboard(view.window, Qt::Key_Right);
    check(waitUntil([&] { return folderRow(tree, nested) != nullptr; }),
        "Keyboard expansion did not expose an archive folder's child");
    keyboard(view.window, Qt::Key_Down);
    keyboard(view.window, Qt::Key_Return);
    check(waitUntil([&] { return view.archive.property("currentFolder").toString() == nested
        && !model->querying() && model->count() == 1; }), "Keyboard folder activation did not scope the file list");
    navigate(nested, QStringLiteral("deep.txt"), 1);
    view.capture(language + QStringLiteral("-archive-folders"));
    invoke(up, "clicked");
    check(waitUntil([&] { return view.archive.property("currentFolder").toString() == QString::fromUtf8("中文")
        && !model->querying() && model->count() == 2; }), "Parent-folder button did not return to the direct parent");
    navigate(QStringLiteral("empty"), {}, 0);
    const auto emptyText = language == QStringLiteral("zh-CN") ? QString::fromUtf8("当前文件夹中没有文件或文件夹") : QStringLiteral("No files or folders in this folder");
    check(displaysText(entries, emptyText), "Empty archive folder did not show its localized empty-content message");
    invoke(named(view.window, "archiveRootButton"), "clicked");
    check(waitUntil([&] { return view.archive.property("currentFolder").toString().isEmpty()
        && !model->querying() && model->count() == 5; }), "Root-folder button did not restore root folders and files");
    check(!up->property("enabled").toBool(), "Root-folder navigation allowed walking above the archive");
}
void brandingAndLicenses(View& view, const QString& language) {
    const auto productName = language == QStringLiteral("zh-CN") ? QString::fromUtf8("轻匣") : QStringLiteral("LiteArc");
    check(view.window->title() == productName, "Home title did not use the localized product name");
    auto* about = named(view.window, "aboutDialog");
    invoke(about, "open");
    check(named(about, "aboutProductName")->property("text").toString() == productName,
        "About dialog retained the old product name");
    check(named(about, "aboutGitRevision")->property("text").toString().contains(view.buildInfo.applicationRevision()),
        "About dialog did not show the built application's Git revision");
    check(named(about, "aboutRepositoryLink")->property("text").toString() == view.buildInfo.repositoryUrl().toString(),
        "About dialog repository link changed with the product name");
    view.capture(language + QStringLiteral("-about"));
    invoke(about, "close");
    auto* licenses = named(view.window, "openSourceDialog");
    invoke(licenses, "open");
    check(named(licenses, "licenseApplicationVersion")->property("text").toString().contains(productName),
        "License dialog application version did not use the localized product name");
    check(named(licenses, "licenseApplicationRevision")->property("text").toString().contains(view.buildInfo.applicationRevision()),
        "License dialog omitted the application's full Git revision");
    auto* list = named(licenses, "openSourceComponents");
    check(list->property("count").toInt() == 11, "License component catalog missing");
    const auto qt = view.buildInfo.dependencyInfo().value(QStringLiteral("qt")).toMap();
    check(waitUntil([&] { return visualItem(qobject_cast<QQuickItem*>(list), QStringLiteral("componentVersion-qt")) != nullptr; }),
        "Qt license row was not created");
    check(namedVisual(list, "componentVersion-qt")->property("text").toString().contains(qt.value(QStringLiteral("version")).toString()),
        "Qt license version does not come from selected build metadata");
    check(namedVisual(list, "qtRuntimeVersion")->property("text").toString().contains(view.buildInfo.qtRuntimeVersion()),
        "License dialog omitted the actual Qt runtime version");
    const auto qtRevision = qt.value(QStringLiteral("revision")).toString();
    if (!qtRevision.isEmpty())
        check(namedVisual(list, "componentRevision-qt")->property("text").toString().contains(qtRevision),
            "License dialog abbreviated or lost the Qt source revision");
    view.capture(language + QStringLiteral("-licenses-qt"));
    check(QMetaObject::invokeMethod(list, "positionViewAtIndex", Q_ARG(int, 1), Q_ARG(int, 0)),
        "Cannot scroll license list to backend");
    check(waitUntil([&] { return visualItem(qobject_cast<QQuickItem*>(list), QStringLiteral("componentVersion-libarchive")) != nullptr; }),
        "Backend license row was not created");
    const auto backend = view.buildInfo.dependencyInfo().value(QStringLiteral("libarchive")).toMap();
    check(namedVisual(list, "componentVersion-libarchive")->property("text").toString().contains(backend.value(QStringLiteral("version")).toString()),
        "Backend license row omitted the used version");
    const auto backendRevision = backend.value(QStringLiteral("revision")).toString();
    if (!backendRevision.isEmpty())
        check(namedVisual(list, "componentRevision-libarchive")->property("text").toString().contains(backendRevision),
            "Backend license row omitted the full Git revision");
    view.capture(language + QStringLiteral("-licenses-backend"));
    invoke(licenses, "close");
}
void flow(const QString& language) {
    TemporaryDirectory fixture;
    const auto file = fixture.path / pathFromUtf8("归档🐈.dat");
    createFixture(file, FixtureFormat::Zip, {{"z.txt", "z"}, {"dup.txt", "first"},
        {"dup.txt", "second payload"}, {"中文/安安.txt", "unicode"},
        {"中文/嵌套/deep.txt", "nested"}, {"empty/", {}, EntryType::Directory}});
    View view;
    brandingAndLicenses(view, language);
    view.window->setProperty("themeMode", 1);
    view.capture(language + QStringLiteral("-home-light"));
    view.window->setProperty("themeMode", 2);
    check(view.desktop.themeMode() == 2 && view.window->property("darkTheme").toBool(), "UI theme was not persisted to controller");
    view.capture(language + QStringLiteral("-home-dark"));
    view.drop(qt::fromPath(file));
    check(waitUntil([&] { return !view.drag.busy() && !view.archive.busy() && view.archive.canExtract(); }), "UI drop/open pipeline did not index");
    check(view.archive.entryCount() == 6 && view.desktop.recentArchives().size() == 1, "Content drop/recent indexing failed");
    archiveFolders(view, language);
    keyboard(view.window, Qt::Key_F, Qt::ControlModifier);
    auto* search = named(view.window, "archiveSearch");
    check(search->property("activeFocus").toBool(), "Ctrl+F did not focus search");
    search->setProperty("text", QStringLiteral("dup"));
    invoke(search, "textEdited");
    auto* model = qobject_cast<qt::ArchiveEntryModel*>(view.archive.entryModel());
    check(waitUntil([&] { return model->query() == QStringLiteral("dup") && !model->querying(); }), "UI debounced search did not complete");
    check(model->count() == 2 && view.archive.entryCount() == 6, "Search lost archive totals");
    invoke(named(view.window, "sortSize"), "clicked");
    check(waitUntil([&] { return !model->querying(); }), "UI size sort did not complete");
    invoke(named(view.window, "sortSize"), "clicked");
    check(waitUntil([&] { return !model->querying(); }), "UI descending size sort did not complete");
    view.archive.selectRow(entryRow(model, QStringLiteral("dup.txt")));
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
    view.archive.selectRow(entryRow(model, QStringLiteral("z.txt")));
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
    auto* folderDialog = named(view.window, "localFolderDialog");
    invoke(folderDialog, "open");
    settle();
    check(folderDialog->setProperty("selectedFolder", QUrl::fromLocalFile(qt::fromPath(fixture.path))),
        "Browse folder dialog did not accept the selected local directory");
    invoke(folderDialog, "accept");
    check(waitUntil([&] { return view.archive.currentFile().isEmpty()
        && !view.directory.rootPath().isEmpty() && view.directory.rowCount() > 0; }),
        "Browse folder did not switch from the archive to local directory browsing: archive="
        + view.archive.currentFile().toStdString() + ", root=" + view.directory.rootPath().toStdString()
        + ", rows=" + std::to_string(view.directory.rowCount()) + ", selected="
        + folderDialog->property("selectedFolder").toUrl().toString().toStdString());
    check(view.archive.property("currentFolder").toString().isEmpty(),
        "Local browsing retained an archive folder scope");
    check(named(view.window, "localDirectoryTree")->property("visible").toBool()
        && !named(view.window, "archiveFolderTree")->property("visible").toBool(),
        "Local browsing kept the archive tree visible instead of the local directory tree");
    view.capture(language + QStringLiteral("-local-browse"));
    invoke(named(view.window, "homeButton"), "clicked");
    check(view.directory.rootPath().isEmpty(), "Home did not leave local directory browsing");
}
} // namespace
int main(int argc, char** argv) {
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
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
        std::cout << "PASS production archive folders, URL drop, search, sorting, selected extraction, tasks, shortcuts, themes, recents and errors\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        for (const auto& message : messages) std::cerr << message.toStdString() << '\n';
        return 1;
    }
}

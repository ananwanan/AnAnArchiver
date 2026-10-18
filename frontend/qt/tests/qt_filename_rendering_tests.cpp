#include "controllers/archive_controller.h"
#include "controllers/compression_controller.h"
#include "models/file_tree_model.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlNetworkAccessManagerFactory>
#include <QQuickStyle>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QTimer>
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>

namespace {

const QString boldName = QStringLiteral("<b>literal filename</b>.txt");
const QString imageName = QStringLiteral("<img src=\"http://127.0.0.1:42421/probe.png\">.txt");
const QString archiveName = QStringLiteral("<b>literal archive</b>.zip");
const QString folderName = QStringLiteral("<b>literal folder");
const QString leafName = QStringLiteral("<img src=\"probe.png\">.txt");

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void processFor(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

struct Gate {
    std::mutex mutex;
    std::condition_variable_any changed;
};

class MarkupReader final : public ananarchiver::ArchiveReader
{
public:
    explicit MarkupReader(std::shared_ptr<Gate> gate) : gate_(std::move(gate)) {}

    ananarchiver::Result<ananarchiver::ArchiveInfo> open(
        const std::filesystem::path& path, ananarchiver::CancellationToken) override
    {
        ananarchiver::ArchiveInfo info;
        info.path = path;
        info.formatName = "Security fixture";
        return info;
    }

    ananarchiver::Result<void> enumerate(const ananarchiver::EntryCallback& callback,
                                        ananarchiver::CancellationToken cancellation) override
    {
        for (int i = 0; i < 512; ++i) {
            ananarchiver::ArchiveEntry entry;
            entry.path = (i % 2 == 0 ? boldName : imageName).toUtf8().toStdString();
            entry.size = 1;
            if (i == 0) {
                entry.path = (folderName + QChar(u'/')).toUtf8().toStdString();
                entry.type = ananarchiver::EntryType::Directory;
                entry.size = 0;
            } else if (i == 1) {
                entry.path = (folderName + QChar(u'/') + leafName).toUtf8().toStdString();
            }
            if (!callback(entry))
                return cancelled();
        }
        // Keep the real Application worker active after its first metadata batch,
        // so the production status bar also renders the untrusted current entry.
        std::unique_lock lock(gate_->mutex);
        gate_->changed.wait(lock, cancellation, [] { return false; });
        return cancelled();
    }

private:
    static ananarchiver::Result<void> cancelled()
    {
        return std::unexpected(ananarchiver::ArchiveError{
            ananarchiver::ArchiveErrorCode::Cancelled, "Cancelled", {}, {}});
    }
    std::shared_ptr<Gate> gate_;
};

class DeniedReply final : public QNetworkReply
{
public:
    DeniedReply(const QNetworkRequest& request, QObject* parent) : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        setOpenMode(QIODevice::ReadOnly);
        setError(QNetworkReply::ContentAccessDenied, QStringLiteral("Request blocked by rendering test"));
        QTimer::singleShot(0, this, [this] {
            setFinished(true);
            emit errorOccurred(error());
            emit finished();
        });
    }
    void abort() override { setFinished(true); }

protected:
    qint64 readData(char*, qint64) override { return -1; }
};

class CountingManager final : public QNetworkAccessManager
{
public:
    CountingManager(std::atomic<int>& requests, QObject* parent)
        : QNetworkAccessManager(parent), requests_(requests) {}

protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override
    {
        ++requests_;
        // Never perform network IO, including if this regression reappears.
        return new DeniedReply(request, this);
    }

private:
    std::atomic<int>& requests_;
};

class CountingFactory final : public QQmlNetworkAccessManagerFactory
{
public:
    explicit CountingFactory(std::atomic<int>& requests) : requests_(requests) {}
    QNetworkAccessManager* create(QObject* parent) override
    {
        return new CountingManager(requests_, parent);
    }

private:
    std::atomic<int>& requests_;
};

QQuickItem* folderRow(QQuickItem* root)
{
    if (!root)
        return nullptr;
    if (root->objectName() == QStringLiteral("archiveFolderRow")
        && root->property("folderPath").toString() == folderName)
        return root;
    for (auto* child : root->childItems()) {
        if (auto* found = folderRow(child))
            return found;
    }
    return nullptr;
}

QQuickItem* rightFolderRow(QQuickItem* root)
{
    if (!root)
        return nullptr;
    if (root->objectName() == QStringLiteral("archiveEntryRow")
        && root->property("entryDirectory").toBool()
        && root->property("entryPath").toString() == folderName)
        return root;
    for (auto* child : root->childItems()) {
        if (auto* found = rightFolderRow(child))
            return found;
    }
    return nullptr;
}

int checkPlainText(QObject* object, QSet<QObject*>& visited, QSet<QString>& rendered)
{
    if (visited.contains(object))
        return 0;
    visited.insert(object);
    int found = 0;
    const auto text = object->property("text").toString();
    if (text == boldName || text == imageName || text == archiveName || text == leafName
        || text == folderName || text == QChar(u'/') + folderName) {
        const auto format = object->property("textFormat");
        if (format.isValid()) {
            require(format.toInt() == 0, "Untrusted archive data reached a rich-text renderer");
            rendered.insert(text);
            ++found;
        }
    }
    for (auto* child : object->children())
        found += checkPlainText(child, visited, rendered);
    if (auto* item = qobject_cast<QQuickItem*>(object)) {
        for (auto* child : item->childItems())
            found += checkPlainText(child, visited, rendered);
    }
    if (auto* window = qobject_cast<QQuickWindow*>(object))
        found += checkPlainText(window->contentItem(), visited, rendered);
    return found;
}

}

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    try {
        auto gate = std::make_shared<Gate>();
        ananarchiver::qt::ArchiveController controller([gate] {
            return std::make_unique<MarkupReader>(gate);
        });
        ananarchiver::qt::FileTreeModel directories;
        ananarchiver::qt::CompressionController compression;
        std::atomic<int> networkRequests{};
        CountingFactory networkFactory(networkRequests);
        QQmlApplicationEngine engine;
        engine.setNetworkAccessManagerFactory(&networkFactory);
        engine.setInitialProperties({
            {QStringLiteral("compressionController"), QVariant::fromValue(&compression)},
            {QStringLiteral("archiveController"), QVariant::fromValue(&controller)},
            {QStringLiteral("directoryModel"), QVariant::fromValue(&directories)}});
        engine.load(QUrl::fromLocalFile(QString::fromUtf8(ANANARCHIVER_TEST_MAIN_QML)));
        require(!engine.rootObjects().isEmpty(), "Production QML did not load");
        controller.openFile(archiveName);
        QElapsedTimer timeout;
        timeout.start();
        while (controller.entryCount() < 512 && timeout.elapsed() < 5000)
            processFor(20);
        require(controller.entryCount() == 512 && controller.busy(), "Markup metadata batch did not reach the view");
        require(controller.currentEntry() == imageName, "Current-entry rendering path was not exercised");
        processFor(300);
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        require(window != nullptr, "Production QML root is not a window");
        timeout.restart();
        while (!folderRow(window->contentItem()) && timeout.elapsed() < 5000)
            processFor(20);
        auto* folder = folderRow(window->contentItem());
        require(folder != nullptr, "Untrusted archive folder name did not reach its tree delegate");
        timeout.restart();
        while (!rightFolderRow(window->contentItem()) && timeout.elapsed() < 5000)
            processFor(20);
        auto* rightFolder = rightFolderRow(window->contentItem());
        require(rightFolder != nullptr, "Untrusted archive folder name did not reach the right contents list");
        QSet<QObject*> rootVisited;
        QSet<QString> rootRendered;
        require(checkPlainText(rightFolder, rootVisited, rootRendered) > 0 && rootRendered.contains(folderName),
            "Right folder row did not render its untrusted name as plain text");
        require(QMetaObject::invokeMethod(folder, "clicked", Qt::DirectConnection),
            "Could not select the untrusted archive folder");
        timeout.restart();
        while ((controller.property("currentFolder").toString() != folderName || controller.entryModel()->property("querying").toBool())
               && timeout.elapsed() < 5000)
            processFor(20);
        require(controller.property("currentFolder").toString() == folderName
                && controller.entryModel()->property("count").toInt() == 1,
            "Folder selection did not scope the untrusted filename row");
        processFor(100);
        QSet<QObject*> visited;
        QSet<QString> rendered;
        require(checkPlainText(engine.rootObjects().first(), visited, rendered) >= 5
                && rendered.contains(folderName) && rendered.contains(QChar(u'/') + folderName) && rendered.contains(leafName),
                "Production archive path, folder tree, folder location, file row and status text were not rendered");
        require(networkRequests.load() == 0, "An archive filename triggered a network image request");
        controller.cancel();

        // Verify the observer is active: intentionally rich text in this test
        // must attempt an image request, which the test manager denies locally.
        QQmlComponent control(&engine);
        control.setData(R"qml(import QtQuick
Text { textFormat: Text.RichText; text: '<img src="http://127.0.0.1:42421/control.png">' }
)qml", QUrl());
        std::unique_ptr<QObject> richControl(control.create());
        require(richControl != nullptr, "Network observer control did not load");
        timeout.restart();
        while (networkRequests.load() == 0 && timeout.elapsed() < 2000)
            processFor(20);
        require(networkRequests.load() > 0, "Network observer did not detect the rich-text control");
        std::cout << "PASS untrusted filename markup displayed literally; network image requests=0\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
    return 0;
}

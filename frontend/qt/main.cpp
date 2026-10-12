#include "app/app.h"
#include "platform/frontend_translator.h"
#include "platform/packaged_runtime.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QFileOpenEvent>
#include <QFileInfo>
#include <QSettings>
#include <QPointer>
#include <QIcon>
#include <QLocale>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <iostream>
#include <functional>

namespace {
class DesktopApplication final : public QGuiApplication {
public:
    using QGuiApplication::QGuiApplication;
    std::function<void(const QString&)> openFile;
    QStringList pendingFiles;
protected:
    bool event(QEvent* event) override {
        if (event->type() == QEvent::FileOpen) {
            const auto* request = static_cast<QFileOpenEvent*>(event);
            if (!request->url().isLocalFile()) return false;
            const auto file = request->url().toLocalFile();
            if (openFile) openFile(file);
            else pendingFiles.append(file);
            return true;
        }
        return QGuiApplication::event(event);
    }
};
}

int main(int argc, char* argv[])
{
    if (!ananarchiver::qt::configurePackagedRuntime()) return 6;
    DesktopApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("AnAnArchiver"));
    QCoreApplication::setApplicationVersion(QStringLiteral(ANANARCHIVER_PROJECT_VERSION));
    QCoreApplication::setOrganizationName(QStringLiteral("AnAnArchiver"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/branding/ananarchiver.png")));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"),
                                 QCoreApplication::translate("main", "Archive to open"));
    QCommandLineOption smokeOption(QStringLiteral("smoke-test"),
        QCoreApplication::translate("main", "Exit after this many milliseconds for launch verification"),
        QStringLiteral("milliseconds"));
    QCommandLineOption smokeArchiveOption(QStringLiteral("smoke-archive"),
        QCoreApplication::translate("main", "Exit after archive indexing; failure returns exit code 1"));
    QCommandLineOption screenshotOption(QStringLiteral("smoke-screenshot"),
        QStringLiteral("Save the rendered main window for launch verification"), QStringLiteral("file"));
    QCommandLineOption aboutOption(QStringLiteral("smoke-about"),
        QStringLiteral("Open the About dialog for launch verification"));
    QCommandLineOption languageOption(QStringLiteral("language"),
        QStringLiteral("Select the interface language (en or zh-CN)"), QStringLiteral("language"));
    parser.addOption(smokeOption);
    parser.addOption(smokeArchiveOption);
    parser.addOption(screenshotOption);
    parser.addOption(aboutOption);
    parser.addOption(languageOption);
    parser.addOption({QStringLiteral("extract-here"), QStringLiteral("Extract the archive into its containing folder")});
    parser.addOption({QStringLiteral("extract-to-folder"), QStringLiteral("Extract into a folder named after the archive")});
    parser.addOption({QStringLiteral("compress"), QStringLiteral("Add the input files or folders to the creation dialog")});
    parser.addOption({QStringLiteral("smoke-shell"), QStringLiteral("Exit after shell extraction or creation-dialog verification")});
    parser.process(app);

    QString shellAction;
    for (const auto& action : {QStringLiteral("extract-here"), QStringLiteral("extract-to-folder"), QStringLiteral("compress")}) {
        if (!parser.isSet(action)) continue;
        if (!shellAction.isEmpty()) { std::cerr << "Choose one shell action\n"; return 2; }
        shellAction = action;
    }
    const auto positional = parser.positionalArguments();
    if ((shellAction.isEmpty() && positional.size() > 1) ||
        (!shellAction.isEmpty() && positional.isEmpty()) ||
        (shellAction != QStringLiteral("compress") && positional.size() > 1) ||
        (parser.isSet(QStringLiteral("smoke-shell")) && shellAction.isEmpty()) ||
        (parser.isSet(smokeArchiveOption) && (!shellAction.isEmpty() || positional.isEmpty()))) {
        std::cerr << "Invalid archive or shell action arguments\n"; return 2;
    }
    // Package checks must never add test archives to the user's real recent
    // history. Only explicit smoke runs may redirect settings into test storage.
    const auto settingsFile = qEnvironmentVariable("ANANARCHIVER_SETTINGS_FILE");
    if (!settingsFile.isEmpty() && (parser.isSet(smokeOption) || parser.isSet(smokeArchiveOption) ||
                                  parser.isSet(QStringLiteral("smoke-shell")))) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QFileInfo(settingsFile).absolutePath());
    }

    ananarchiver::qt::FrontendTranslator translator;
    const auto language = parser.value(languageOption);
    if (!language.isEmpty() && language != QStringLiteral("en") && language != QStringLiteral("zh-CN"))
        return 2;
    if (language == QStringLiteral("zh-CN") ||
        (language.isEmpty() && QLocale().language() == QLocale::Chinese))
        app.installTranslator(&translator);

    const auto capture = [&app, &parser] {
        if (!parser.isSet(QStringLiteral("smoke-screenshot")))
            return true;
        const auto filename = parser.value(QStringLiteral("smoke-screenshot"));
        for (auto* window : app.allWindows()) {
            auto* quick = qobject_cast<QQuickWindow*>(window);
            if (quick && quick->isVisible() && quick->width() >= 760)
                return quick->grabWindow().save(filename);
        }
        return false;
    };

    const auto file = positional.isEmpty() ? QString() : positional.first();
    ananarchiver::qt::App application;
    if (!application.initialize(shellAction.isEmpty() ? file : QString()))
        return 1;
    app.openFile = [host = QPointer<ananarchiver::qt::App>(&application)](const QString& path) {
        if (host) host->openArchive(path);
    };
    for (const auto& pending : app.pendingFiles) application.openArchive(pending);
    app.pendingFiles.clear();
    if (parser.isSet(QStringLiteral("smoke-shell"))) {
        const auto finished = [&app, &capture](bool success) {
            QTimer::singleShot(250, &app, [&app, &capture, success] {
                app.exit(capture() ? (success ? 0 : 1) : 4);
            });
        };
        QObject::connect(&application, &ananarchiver::qt::App::shellActionFinished, &app, finished);
        QObject::connect(&application, &ananarchiver::qt::App::shellCompressionReady, &app, [&app, finished] {
            bool ready = false;
            for (auto* window : app.allWindows()) {
                auto* dialog = window->findChild<QObject*>(QStringLiteral("compressionDialog"));
                auto* output = window->findChild<QObject*>(QStringLiteral("compressionDestination"));
                if (dialog && output) {
                    ready = dialog->property("visible").toBool() && !output->property("text").toString().isEmpty();
                    break;
                }
            }
            finished(ready);
        });
        QTimer::singleShot(30000, &app, [&app] { std::cerr << "Shell smoke test timed out\n"; app.exit(3); });
    }
    if (!shellAction.isEmpty()) {
        QTimer::singleShot(0, &application, [&app, &application, shellAction, positional] {
            if (!application.handleShellAction(shellAction, positional)) app.exit(2);
        });
    }
    if (parser.isSet(aboutOption)) {
        bool opened = false;
        for (auto* window : app.allWindows()) {
            auto* dialog = window->findChild<QObject*>(QStringLiteral("aboutDialog"));
            if (dialog) {
                opened = QMetaObject::invokeMethod(dialog, "open");
                break;
            }
        }
        if (!opened)
            return 5;
    }
    if (parser.isSet(smokeArchiveOption)) {
        if (file.isEmpty()) {
            std::cerr << "--smoke-archive requires an archive argument\n";
            return 2;
        }
        QObject::connect(&application.archiveController(),
                         &ananarchiver::qt::ArchiveController::operationFinished, &app,
                         [&app, &application, &capture](bool success) {
            const auto& controller = application.archiveController();
            std::cout << "archive_indexed=" << success << " entries=" << controller.entryCount()
                      << " format=" << controller.formatName().toStdString() << '\n';
            if (!success)
                std::cerr << controller.errorMessage().toStdString() << '\n';
            QTimer::singleShot(250, &app, [&app, &capture, success] {
                app.exit(capture() ? (success ? 0 : 1) : 4);
            });
        });
        QTimer::singleShot(30000, &app, [&app] {
            std::cerr << "Archive smoke test timed out\n";
            app.exit(3);
        });
    }
    if (parser.isSet(smokeOption)) {
        bool valid = false;
        const auto timeout = parser.value(smokeOption).toInt(&valid);
        if (!valid || timeout <= 0)
            return 2;
        QTimer::singleShot(timeout, &app, [&app, &capture] { app.exit(capture() ? 0 : 4); });
    }
    return app.exec();
}

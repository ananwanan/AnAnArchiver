#include "app/app.h"
#include "platform/frontend_translator.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QLocale>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <iostream>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("AnAnArchiver"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("AnAnArchiver"));
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
    QCommandLineOption languageOption(QStringLiteral("language"),
        QStringLiteral("Select the interface language (en or zh-CN)"), QStringLiteral("language"));
    parser.addOption(smokeOption);
    parser.addOption(smokeArchiveOption);
    parser.addOption(screenshotOption);
    parser.addOption(languageOption);
    parser.process(app);

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

    const auto positional = parser.positionalArguments();
    const auto file = positional.isEmpty() ? QString() : positional.first();
    ananarchiver::qt::App application;
    if (!application.initialize(file))
        return 1;
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

#include "app/app.h"
#include "platform/path_conversion.h"
#include "../../../tests/security/extraction_fixtures.h"
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

using namespace ananarchiver;
using namespace extraction_tests;
namespace {
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
void initialize(qt::App& app) {
    check(app.initialize({}, QUrl::fromLocalFile(QString::fromUtf8(ANANARCHIVER_TEST_MAIN_QML))),
          "Shell host did not load production QML");
}
QObject* named(qt::App& app, const char* name) {
    const auto* engine = app.findChild<QQmlApplicationEngine*>();
    check(engine != nullptr && !engine->rootObjects().isEmpty(), "Missing shell QML engine");
    auto* object = engine->rootObjects().first()->findChild<QObject*>(QString::fromUtf8(name));
    check(object != nullptr, std::string("Missing shell UI: ") + name);
    return object;
}
void extractAndConflicts() {
    TemporaryDirectory temporary;
    const auto archive = temporary.path / pathFromUtf8("安安 archive.tar.gz");
    createFixture(archive, FixtureFormat::TarGzip, {{"nested/安安.txt", "shell payload"}});
    qt::App app;
    initialize(app);
    std::optional<bool> result;
    QObject::connect(&app, &qt::App::shellActionFinished, &app, [&](bool succeeded) { result = succeeded; });
    check(app.handleShellAction(QStringLiteral("extract-to-folder"), {qt::fromPath(archive)}), "Folder shell action refused");
    check(waitUntil([&] { return result.has_value(); }) && *result, "Shell folder extraction failed");
    check(readFile(temporary.path / pathFromUtf8("安安 archive/nested/安安.txt")) == "shell payload",
          "Compound suffix extraction folder or payload is wrong");
    result.reset();
    check(app.handleShellAction(QStringLiteral("extract-here"), {qt::fromPath(archive)}), "Here shell action refused");
    check(waitUntil([&] { return result.has_value(); }) && *result, "Shell here extraction failed");
    const auto output = temporary.path / pathFromUtf8("nested/安安.txt");
    check(readFile(output) == "shell payload", "Shell here payload is wrong");
    writeFile(output, "keep existing");
    result.reset();
    app.handleShellAction(QStringLiteral("extract-here"), {qt::fromPath(archive)});
    check(waitUntil([&] { return app.archiveController().awaitingConflict(); }), "Shell extraction did not ask before overwriting");
    check(readFile(output) == "keep existing", "Shell extraction overwrote before a decision");
    app.archiveController().resolveConflict(static_cast<int>(OverwritePolicy::Skip), true);
    check(waitUntil([&] { return result.has_value(); }) && *result, "Shell skip-conflict did not finish");
    check(readFile(output) == "keep existing", "Shell skip policy changed existing content");
}
void unsafeArchive() {
    TemporaryDirectory temporary;
    const auto archive = temporary.path / "unsafe.zip";
    createFixture(archive, FixtureFormat::Zip, {{"../outside.txt", "unsafe"}});
    qt::App app;
    initialize(app);
    std::optional<bool> result;
    QObject::connect(&app, &qt::App::shellActionFinished, &app, [&](bool succeeded) { result = succeeded; });
    app.handleShellAction(QStringLiteral("extract-to-folder"), {qt::fromPath(archive)});
    check(waitUntil([&] { return result.has_value(); }) && !*result, "Shell extraction accepted path traversal");
    check(!std::filesystem::exists(temporary.path / "outside.txt"), "Unsafe shell extraction escaped its folder");
}
void encryptedExtraction() {
    TemporaryDirectory temporary;
    const auto archive = temporary.path / "encrypted.zip";
    createFixture(archive, FixtureFormat::Zip, {{"secret.txt", "password from shell"}}, true);
    qt::App app;
    initialize(app);
    check(app.handleShellAction(QStringLiteral("extract-to-folder"), {qt::fromPath(archive)}), "Encrypted shell action refused");
    check(waitUntil([&] { return app.archiveController().extractionNeedsPassword(); }), "Encrypted shell extraction did not request a password");
    auto* dialog = named(app, "extractionDialog");
    check(dialog->property("visible").toBool(), "Encrypted shell extraction did not open the password form");
    check(qt::toPath(named(app, "extractionDestination")->property("text").toString()) == temporary.path / "encrypted",
          "Encrypted shell extraction lost its target folder");
    named(app, "extractionPassword")->setProperty("text", QStringLiteral("test-only-passphrase"));
    check(QMetaObject::invokeMethod(named(app, "startExtraction"), "clicked"), "Password form cannot confirm extraction");
    check(waitUntil([&] { return !app.archiveController().busy(); }), "Password shell retry timed out");
    check(readFile(temporary.path / "encrypted/secret.txt") == "password from shell", "Password shell retry payload differs");
    check(named(app, "extractionPassword")->property("text").toString().isEmpty(), "Shell password input was retained after close");
}
void compression() {
    TemporaryDirectory temporary;
    const auto input = temporary.path / pathFromUtf8("安安 input.txt");
    writeFile(input, "created from shell");
    qt::App app;
    initialize(app);
    bool ready = false;
    QObject::connect(&app, &qt::App::shellCompressionReady, &app, [&] { ready = true; });
    check(app.handleShellAction(QStringLiteral("compress"), {qt::fromPath(input)}), "Shell creation refused");
    check(ready && named(app, "compressionDialog")->property("visible").toBool(), "Shell creation dialog did not open");
    auto* outputField = named(app, "compressionDestination");
    const auto output = qt::toPath(outputField->property("text").toString());
    check(output == temporary.path / pathFromUtf8("安安 input.txt.zip"), "Shell suggested output is wrong");
    check(!std::filesystem::exists(output), "Shell compression wrote an archive without user confirmation");
    auto* engine = app.findChild<QQmlApplicationEngine*>();
    auto* controller = qobject_cast<qt::CompressionController*>(engine->rootObjects().first()->property("compressionController").value<QObject*>());
    check(controller != nullptr, "Shell creation controller is missing");
    check(controller->inputCount() == 1 && QDir::fromNativeSeparators(controller->inputs().first()) ==
          QDir::fromNativeSeparators(qt::fromPath(input)), "Shell input was not supplied to the creation controller");
    std::optional<bool> result;
    QObject::connect(controller, &qt::CompressionController::operationFinished, &app, [&](bool succeeded) { result = succeeded; });
    const auto format = controller->formats().first().toMap();
    const auto method = format.value(QStringLiteral("defaultMethod")).toInt();
    const auto level = format.value(QStringLiteral("methods")).toList()[method].toMap()
        .value(QStringLiteral("defaultLevel")).toInt();
    check(controller->create(qt::fromPath(output), 0, method, level, false), "Confirmed shell creation did not start");
    check(waitUntil([&] { return result.has_value(); }), "Confirmed shell creation timed out");
    check(*result, "Confirmed shell creation failed: " + controller->errorDetails().toStdString());
    app.openArchive(qt::fromPath(output));
    check(waitUntil([&] { return app.archiveController().canExtract(); }), "Created shell archive cannot be opened");
    app.archiveController().extract(qt::fromPath(temporary.path / "roundtrip"), false, {});
    check(waitUntil([&] { return !app.archiveController().busy(); }), "Shell archive roundtrip timed out");
    check(readFile(temporary.path / "roundtrip" / input.filename()) == "created from shell", "Shell archive roundtrip changed payload");
    check(!app.handleShellAction(QStringLiteral("extract-here"), {qt::fromPath(input), qt::fromPath(output)}), "Shell extraction accepted multiple archives");
}
}

int main(int argc, char** argv) {
#ifdef _WIN32
    if (qEnvironmentVariable("QT_QPA_PLATFORM") == QStringLiteral("offscreen"))
        qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts")).toUtf8());
#endif
    QGuiApplication gui(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("AnAnArchiver"));
    QGuiApplication::setApplicationName(QStringLiteral("ShellTests"));
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    Tests tests;
    tests.run("Shell compound folder, extract here and overwrite decision", extractAndConflicts);
    tests.run("Shell path traversal rejection", unsafeArchive);
    tests.run("Shell encrypted extraction retains destination and prompts password", encryptedExtraction);
    tests.run("Shell populated creation UI and confirmed roundtrip", compression);
    return tests.finish();
}

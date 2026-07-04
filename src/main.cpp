#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>

#include <app/app.h>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("file", "输入文件");
    parser.process(app);

    QString file;
    const QStringList positional = parser.positionalArguments();
    if (!positional.isEmpty()) {
        file = positional.first();
    }

    App application;

    if (!application.initialize(file)) {
        return -1;
    }

    return app.exec();
}
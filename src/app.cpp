#include "app.h"

#include <QQmlApplicationEngine>
#include <QQmlContext>

App::App(QObject *parent)
    : QObject(parent)
{
}

App::~App() = default;

bool App::initialize()
{
    m_engine = std::make_unique<QQmlApplicationEngine>();

    registerTypes();
    registerSingletons();

    return loadQml();
}

void App::registerTypes()
{
    // 以后注册 QML 类型
    //
    // qmlRegisterType<ArchiveModel>("AnZip", 1, 0, "ArchiveModel");
    // qmlRegisterType<TaskModel>("AnZip", 1, 0, "TaskModel");
}

void App::registerSingletons()
{
    // 以后注册单例
    //
    // m_engine->rootContext()->setContextProperty("archiveManager", archiveManager);
    // m_engine->rootContext()->setContextProperty("settings", settings);
}

bool App::loadQml()
{
    m_engine->loadFromModule("AnAnArchiver", "Main");

    return !m_engine->rootObjects().isEmpty();
}
#include "app.h"
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>

App::App(QObject* parent)
  : QObject(parent)
{}

App::~App() = default;

bool App::initialize(const QString &file)
{
  /**
   * 注册 QML 类型和单例
   */
  registerSingletons();

  QString _file = file;
  if (file.isEmpty())
  {
      _file = "S:/Folder";
      // _file = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  }

  m_context.open(_file);

  m_engine = std::make_unique<QQmlApplicationEngine>();

  registerTypes();

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
  qmlRegisterSingletonInstance("App", 1, 0, "AppContext", &m_context);
}

bool App::loadQml()
{
  m_engine->loadFromModule("AnAnArchiver", "Main");

  return !m_engine->rootObjects().isEmpty();
}
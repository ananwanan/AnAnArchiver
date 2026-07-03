#include <QGuiApplication>
#include <QQmlApplicationEngine>

#include "App.h"

int main(int argc, char* argv[])
{
  QGuiApplication app(argc, argv);

  App application;

  if (!application.initialize())
  {
    return -1;
  }

  return app.exec();
}
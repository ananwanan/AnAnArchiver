#pragma once

#include <QObject>
#include <context/app_context.h>
#include <memory>

class QQmlApplicationEngine;

class App : public QObject
{
  Q_OBJECT

public:
  explicit App(QObject* parent = nullptr);
  ~App();

  bool initialize(const QString& file);

private:
  void registerTypes();
  void registerSingletons();
  bool loadQml();

private:
  std::unique_ptr<QQmlApplicationEngine> m_engine;

  AppContext m_context;
};
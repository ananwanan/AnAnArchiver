#pragma once

#include <QObject>
#include <memory>

class QQmlApplicationEngine;

class App : public QObject
{
    Q_OBJECT

public:
    explicit App(QObject *parent = nullptr);
    ~App();

    bool initialize();

private:
    void registerTypes();
    void registerSingletons();
    bool loadQml();

private:
    std::unique_ptr<QQmlApplicationEngine> m_engine;
};
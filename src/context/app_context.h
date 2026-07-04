#pragma once

#include <QFileSystemModel>
#include <QObject>

#include <models/file_tree_model.h>

class AppContext : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString windowTitle READ windowTitle NOTIFY windowTitleChanged)

    Q_PROPERTY(FileTreeModel *treeModel READ treeModel CONSTANT)

    Q_PROPERTY(QString currentFile READ currentFile WRITE setCurrentFile NOTIFY currentFileChanged)

public:
    explicit AppContext(QObject *parent = nullptr);

public:
    /**
     * 窗口标题栏
     * @brief windowTitle
     * @return 
     */
    QString windowTitle() const;

    QString currentFile() const { return m_currentFile; };

    void setCurrentFile(const QString &path);

public: //文件处理
    FileTreeModel *treeModel() { return &m_treeModel; }
    /**
     * 打开文件
     * @brief open
     * @param path
     */
    Q_INVOKABLE void open(const QString &path);

signals:
    void currentFileChanged();
    void windowTitleChanged();

private:
    QString m_currentFile;

    FileTreeModel m_treeModel;
};
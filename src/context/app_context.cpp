#include "app_context.h"

#include <QFileInfo>
#include <QFileSystemModel>

AppContext::AppContext(QObject* parent)
  : QObject(parent)
{}

QString AppContext::windowTitle() const
{
    if (true) {
        return "安安解压";
    }
}

void AppContext::setCurrentFile(const QString &path)
{
    if (m_currentFile == path)
        return;

    m_currentFile = path;

    QFileInfo info(path);

    if (info.isDir()) {
        m_treeModel.loadDirectory(path);
    } else {
        // TODO: 调用 ArchiveReader 读取 zip/7z/rar
        // m_treeModel.loadArchive(path);
    }

    emit currentFileChanged();
}

void AppContext::open(const QString &path)
{
    m_treeModel.loadDirectory(path);
}

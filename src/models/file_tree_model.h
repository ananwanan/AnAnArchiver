#pragma once

#include <QAbstractItemModel>

class FileTreeItem;

class FileTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    explicit FileTreeModel(QObject *parent = nullptr);

    ~FileTreeModel() override;

public:
    /**
     * 第 row 行，第 column 列，对应哪个节点？
     * @brief index
     * @param row
     * @param column
     * @param parent
     * @return 
     */
    QModelIndex index(int row, int column, const QModelIndex &parent) const override;

    /**
     * 这个节点的父节点是谁？
     * @brief parent
     * @param child
     * @return 
     */
    QModelIndex parent(const QModelIndex &child) const override;

    /**
     * 这个节点下面有几个孩子？
     * @brief rowCount
     * @param parent
     * @return 
     */
    int rowCount(const QModelIndex &parent) const override;

    /**
     * 目前我们只有一列：文件名
     * @brief columnCount
     * @param parent
     * @return 
     */
    int columnCount(const QModelIndex &parent) const override;

    /**
     * 这个格子应该显示什么？
     * @brief data
     * @param index
     * @param role
     * @return 
     */
    QVariant data(const QModelIndex &index, int role) const override;

    /**
     * @brief hasChildren
     * @param parent
     * @return 
     */
    bool hasChildren(const QModelIndex &parent = QModelIndex()) const override;

    /**
     * @brief roleNames
     * @return 
     */
    QHash<int, QByteArray> roleNames() const override;

    /**
     * 这个节点有哪些能力？
     * @brief flags
     * @param index
     * @return 
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;

public:
    /**
     * 每次重新打开一个目录，都需要：
     * 
     *   旧数据
     *     ↓
     *    清空
     *     ↓
     *  扫描新目录
     *     ↓
     * 刷新 TreeView
     * 
     * Qt Model 必须通知 View：
     * 
     * beginResetModel();
     * ...
     * endResetModel();
     * 
     * 否则 QML 不知道数据已经变了。
     *  
     * @brief loadDirectory
     * @param path
     */
    void loadDirectory(const QString &path);

    void clear();

private:
    /**
     * 真正读取磁盘
     * @brief scanDirectory
     * @param path
     * @param parent
     */
    void scanDirectory(const QString &path, FileTreeItem *parent);

private:
    FileTreeItem *m_root = nullptr;
};
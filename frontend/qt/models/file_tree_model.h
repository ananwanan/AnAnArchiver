#pragma once

#include <ananarchiver/application/directory_service.h>
#include <QAbstractItemModel>
#include <QTimer>
#include <QUrl>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace ananarchiver::qt {

// Qt adapter only. Filesystem work lives in DirectoryService and runs on the owned worker.
class FileTreeModel final : public QAbstractItemModel
{
    Q_OBJECT
    Q_PROPERTY(QString rootPath READ rootPath NOTIFY rootPathChanged)

public:
    explicit FileTreeModel(QObject* parent = nullptr);
    ~FileTreeModel() override;
    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool hasChildren(const QModelIndex& parent = {}) const override;
    bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;
    QString rootPath() const;
    Q_INVOKABLE void loadDirectory(const QString& path);
    Q_INVOKABLE void loadDirectoryUrl(const QUrl& url);
    Q_INVOKABLE void fetchChildren(const QModelIndex& parent);
    Q_INVOKABLE void clear();

signals:
    void rootPathChanged();
    void scanFailed(const QString& details);

private:
    struct Node {
        application::DirectoryEntry entry;
        Node* parent{};
        std::vector<std::unique_ptr<Node>> children;
        std::uint64_t id{};
        int row{};
        bool loaded{};
        bool loading{};
    };
    struct Request {
        std::uint64_t generation{};
        std::uint64_t parentId{};
        std::filesystem::path path;
        std::stop_source cancellation;
    };
    struct Completed {
        std::uint64_t generation{};
        std::uint64_t parentId{};
        Result<std::vector<application::DirectoryEntry>> result;
        std::size_t consumed{};
    };

    Node* node(const QModelIndex& index) const;
    QModelIndex nodeIndex(Node* item) const;
    void reset(const QString& path);
    void run(std::stop_token stop);
    void consume();
    void consumePending();
    void scanFailedAndReset(const QString& message);

    std::unique_ptr<Node> root_;
    std::unordered_map<std::uint64_t, Node*> nodes_;
    std::uint64_t generation_{};
    std::uint64_t nextId_{1};
    QTimer timer_;
    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<Request> requests_;
    std::optional<Completed> completed_;
    std::optional<Completed> pending_;
    std::optional<std::stop_source> activeCancellation_;
    // Declared last so it joins before the queues and mutex are destroyed.
    std::jthread worker_;
};

}

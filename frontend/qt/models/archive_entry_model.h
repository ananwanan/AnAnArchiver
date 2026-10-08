#pragma once

#include <ananarchiver/archive_entry.h>
#include <QAbstractListModel>
#include <QString>
#include <memory>
#include <vector>
#include <utility>
#include <ananarchiver/extraction.h>

namespace ananarchiver::qt {

class ArchiveEntryModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY totalCountChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(int sortColumn READ sortColumn WRITE setSortColumn NOTIFY sortChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending WRITE setSortDescending NOTIFY sortChanged)
    Q_PROPERTY(bool querying READ querying NOTIFY queryingChanged)

public:
    enum Role { PathRole = Qt::UserRole + 1, SizeRole, ModifiedRole, TypeRole, DirectoryRole, SelectedRole };

    explicit ArchiveEntryModel(QObject* parent = nullptr);
    ~ArchiveEntryModel() override;
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const;
    int totalCount() const { return totalCount_; }
    QString query() const { return query_; }
    void setQuery(const QString& query);
    // -1 preserves archive order; 0 name, 1 numeric size, 2 type, 3 timestamp.
    int sortColumn() const { return sortColumn_; }
    void setSortColumn(int column);
    bool sortDescending() const { return sortDescending_; }
    void setSortDescending(bool descending);
    bool querying() const { return querying_; }
    void clear();
    void append(std::vector<ArchiveEntry> entries);
    int selectionCount() const { return selectionCount_; }
    void selectRow(int row, bool toggle, bool range);
    void selectAll();
    void clearSelection();
    std::vector<std::uint64_t> selectedIndices() const;
    void setExtractionTotals(ExtractionRequest& request, bool selected) const;

signals:
    void countChanged();
    void totalCountChanged();
    void selectionChanged();
    void queryChanged();
    void sortChanged();
    void queryingChanged();
    void queryFailed(const QString& message);

private:
    struct EntryBatch {
        std::size_t first{};
        std::shared_ptr<const std::vector<ArchiveEntry>> entries;
    };
    struct QueryWorker;
    const ArchiveEntry& entryAt(int row) const;
    void requestView(bool configurationChanged = true);
    void setQuerying(bool querying);
    void applyView(std::uint64_t generation, int sourceCount, std::vector<std::size_t> rows, QString error);

    // Immutable batches let the worker read metadata while the UI appends new
    // batches, without copying the metadata index or sharing mutable vectors.
    std::vector<EntryBatch> batches_;
    std::vector<std::size_t> visibleRows_;
    int totalCount_{};
    bool identityView_{true};
    QString query_;
    int sortColumn_{-1};
    bool sortDescending_{};
    bool querying_{};
    std::uint64_t generation_{};
    int appliedSourceCount_{};
    std::unique_ptr<QueryWorker> worker_;
    std::vector<std::uint8_t> selected_;
    std::vector<std::pair<int, int>> selectionRanges_;
    int selectionCount_{};
    int anchor_{-1};
    std::uint64_t totalBytes_{};
    bool totalBytesKnown_{true};
};

}

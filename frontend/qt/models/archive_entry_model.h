#pragma once

#include <ananarchiver/archive_entry.h>
#include <QAbstractListModel>
#include <vector>
#include <utility>
#include <ananarchiver/extraction.h>

namespace ananarchiver::qt {

class ArchiveEntryModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)

public:
    enum Role { PathRole = Qt::UserRole + 1, SizeRole, ModifiedRole, TypeRole, DirectoryRole, SelectedRole };

    explicit ArchiveEntryModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const;
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
    void selectionChanged();

private:
    std::vector<ArchiveEntry> entries_;
    std::vector<std::uint8_t> selected_;
    std::vector<std::pair<int, int>> selectionRanges_;
    int selectionCount_{};
    int anchor_{-1};
    std::uint64_t totalBytes_{};
    bool totalBytesKnown_{true};
};

}

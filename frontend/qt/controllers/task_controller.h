#pragma once

#include <QAbstractListModel>
#include <ananarchiver/application/task_manager.h>
#include <functional>

namespace ananarchiver::qt {

class TaskController final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* model READ model CONSTANT)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY changed)
  public:
    enum Role { IdRole = Qt::UserRole + 1, LabelRole, StateRole, PathRole,
        CurrentFileRole, ProgressRole, BytesRole, SpeedRole, ElapsedRole,
        RemainingRole, ErrorRole, DetailsRole, CanCancelRole };
    explicit TaskController(QObject* parent = nullptr);
    QAbstractItemModel* model() { return this; }
    int count() const { return static_cast<int>(records_.size()); }
    int activeCount() const;
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    using Cancel = std::function<void(application::TaskId)>;
    void observe(const QString& source, const application::TaskSnapshot& snapshot,
                 const QString& path, Cancel cancel);
    Q_INVOKABLE void cancelTask(const QString& id);
    Q_INVOKABLE void clearFinished();
  signals:
    void changed();
  private:
    struct Record {
        QString id;
        QString path;
        application::TaskSnapshot snapshot;
        Cancel cancel;
    };
    static bool terminal(application::TaskState state);
    std::vector<Record> records_;
};

} // namespace ananarchiver::qt

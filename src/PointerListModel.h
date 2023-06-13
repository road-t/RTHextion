#ifndef POINTERLISTMODEL_H
#define POINTERLISTMODEL_H

#include <QAbstractTableModel>
#include <QMap>

class PointerListModel : public QAbstractTableModel
{
    Q_OBJECT
public:

    enum MapRoles
    {
        KeyRole = Qt::UserRole + 1,
        ValueRole
    };

    explicit PointerListModel(QObject *parent = 0);
    int rowCount(const QModelIndex& parent = QModelIndex()) const;
    int columnCount(const QModelIndex& parent = QModelIndex()) const;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const;
    bool setSectionNames(QStringList names);

    void clear();

    bool dropPointer(const qint64 ptrOffset);
    quint32 dropOffset(qint64 offset);
    quint32 addPointer(qint64 ptrOffset, qint64 offset);
    QList<qint64> getPointers(qint64 dataOffset);
    qint64 getOffset(qint64 ptrOffset);
    QString getOffsetText(qint64 offset) const;
    bool isPointer(qint64 offset);
    bool hasOffset(qint64 offset);
    bool empty();

private:
    QMap<qint64, qint64> _pointers;
    QMap<qint64, quint32> _offsets;

    QStringList sectionName;

signals:
    void dataChanged();
};

#endif // POINTERLISTMODEL_H

#ifndef POINTERLISTMODEL_H
#define POINTERLISTMODEL_H

#include <QAbstractTableModel>
#include <QMap>
#include <QVector>
#include <QPair>

// TODO: get rid of this dependency
class QHexEdit;

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
    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    int columnCount(const QModelIndex &parent = QModelIndex()) const;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const;
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder);
    bool setSectionNames(QStringList names);

    void clear();

    bool dropPointer(const qint64 ptrOffset);
    quint32 dropPointersBatch(const QVector<qint64> &ptrOffsets);
    quint32 dropOffset(qint64 offset);
    quint32 addPointer(qint64 ptrOffset, qint64 offset);
    quint32 addPointersBatch(const QVector<QPair<qint64, qint64>> &pointers);
    QList<qint64> getPointers(qint64 dataOffset);
    qint64 getOffset(qint64 ptrOffset);
    QString getOffsetText(qint64 offset) const;
    bool isPointer(qint64 offset);
    bool hasOffset(qint64 offset);
    bool empty();
    void refreshData();

    /** Returns all offsets where pointer values are stored (keys of _pointers map). */
    QList<qint64> pointerKeys() const { return _pointers.keys(); }
    /** Returns all target offsets that pointers point to (keys of _offsets map). */
    QList<qint64> offsetKeys() const { return _offsets.keys(); }

    void setHexEdit(QHexEdit *hexEdit);

signals:
    void pointersChanged();

private:
    void rebuildRowOrder();

    QMap<qint64, qint64> _pointers;
    QMap<qint64, quint32> _offsets;
    QVector<qint64> _rowOrder;
    int _sortColumn = 0;
    Qt::SortOrder _sortOrder = Qt::AscendingOrder;

    QStringList sectionName;
    QHexEdit *_hexEdit = nullptr;
};

#endif // POINTERLISTMODEL_H

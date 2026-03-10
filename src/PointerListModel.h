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

    // Bits 61-62 of the stored value in _pointers encode pointer byte size:
    //   0 (00) = 4 bytes, 1 (01) = 3 bytes, 2 (10) = 2 bytes
    static constexpr qint64 kPtrSizeBitMask = static_cast<qint64>(3) << 61;

    static qint64 encodePtrValue(qint64 target, int ptrSize) noexcept
    {
        const int code = (ptrSize == 2) ? 2 : (ptrSize == 3) ? 1 : 0;
        return (target & ~kPtrSizeBitMask) | (static_cast<qint64>(code) << 61);
    }
    static qint64 decodePtrTarget(qint64 stored) noexcept
    {
        return stored & ~kPtrSizeBitMask;
    }
    static int decodePtrSize(qint64 stored) noexcept
    {
        const int code = static_cast<int>((stored >> 61) & 3);
        return (code == 2) ? 2 : (code == 1) ? 3 : 4;
    }

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
    /** Adds a pointer. ptrSize (2, 3, or 4) is encoded into the stored value. */
    quint32 addPointer(qint64 ptrOffset, qint64 offset, int ptrSize = 4);
    /** Adds a batch of pointers. The second element of each pair must be pre-encoded
     *  via encodePtrValue(target, ptrSize). */
    quint32 addPointersBatch(const QVector<QPair<qint64, qint64>> &pointers);
    QList<qint64> getPointers(qint64 dataOffset);
    qint64 getOffset(qint64 ptrOffset);
    int getPointerSize(qint64 ptrOffset) const;
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

#include "PointerListModel.h"
#include "qhexedit/qhexedit.h"
#include <algorithm>

PointerListModel::PointerListModel(QObject *parent) : QAbstractTableModel(parent)
{
}

int PointerListModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return _rowOrder.count();
}

int PointerListModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return 3;
}

QVariant PointerListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
        return sectionName[section];

    return QAbstractItemModel::headerData(section, orientation, role);
}

bool PointerListModel::setSectionNames(QStringList names)
{
    if (names.size() < columnCount())
        return false;

    sectionName = names;

    return true;
}

QVariant PointerListModel::data(const QModelIndex &index, int role) const
{
    if (_pointers.empty() || !index.isValid() || index.row() >= _rowOrder.count())
        return QVariant();

    const qint64 key = _rowOrder[index.row()];
    const qint64 value = _pointers.value(key);

    if (role == KeyRole)
        return key;

    if (role == ValueRole)
        return decodePtrTarget(value);

    if (role != Qt::DisplayRole)
        return QVariant();

    if (index.column() == 0)
        return QString::number(decodePtrTarget(value), 16).toUpper();

    if (index.column() == 1)
        return QString::number(key, 16).toUpper();

    if (index.column() == 2)
        return getOffsetText(key);

    return QVariant();
}

void PointerListModel::sort(int column, Qt::SortOrder order)
{
    if (column < 0 || column > 1)
        return;

    beginResetModel();
    _sortColumn = column;
    _sortOrder = order;
    rebuildRowOrder();
    endResetModel();
}

bool PointerListModel::empty()
{
    return _pointers.isEmpty();
}

void PointerListModel::refreshData()
{
    if (_rowOrder.isEmpty())
        return;

    emit dataChanged(index(0, 0), index(_rowOrder.count() - 1, 2));
}

void PointerListModel::clear()
{
    beginResetModel();
    _pointers.clear();
    _offsets.clear();
    _rowOrder.clear();
    endResetModel();
    emit pointersChanged();
}

quint32 PointerListModel::addPointer(const qint64 ptrOffset, qint64 offset, int ptrSize)
{
    const qint64 encodedValue = encodePtrValue(offset, ptrSize);
    const qint64 oldStored = _pointers.value(ptrOffset, -1);
    const qint64 oldOffset = (oldStored != -1) ? decodePtrTarget(oldStored) : -1;

    beginResetModel();
    if (oldOffset != -1 && oldOffset != offset)
    {
        if (!--_offsets[oldOffset])
            _offsets.remove(oldOffset);
    }

    _pointers.insert(ptrOffset, encodedValue);
    ++_offsets[offset];
    rebuildRowOrder();
    endResetModel();
    emit pointersChanged();

    return _offsets[offset];
}

quint32 PointerListModel::addPointersBatch(const QVector<QPair<qint64, qint64>> &pointers)
{
    if (pointers.isEmpty())
        return 0;

    quint32 added = 0;

    beginResetModel();
    for (const auto &entry : pointers)
    {
        const qint64 ptrOffset = entry.first;
        // second is pre-encoded via encodePtrValue(target, ptrSize)
        const qint64 encodedValue = entry.second;
        const qint64 offset = decodePtrTarget(encodedValue);
        const qint64 oldStored = _pointers.value(ptrOffset, -1);
        const qint64 oldOffset = (oldStored != -1) ? decodePtrTarget(oldStored) : -1;

        if (oldOffset != -1 && oldOffset != offset)
        {
            if (!--_offsets[oldOffset])
                _offsets.remove(oldOffset);
        }

        _pointers.insert(ptrOffset, encodedValue);
        ++_offsets[offset];
        ++added;
    }
    rebuildRowOrder();
    endResetModel();
    emit pointersChanged();

    return added;
}

bool PointerListModel::dropPointer(const qint64 offset)
{
    if (_pointers.contains(offset))
    {
        const qint64 pointedOffset = decodePtrTarget(_pointers.value(offset));
        beginResetModel();
        if (!--_offsets[pointedOffset])
            _offsets.remove(pointedOffset);

        _pointers.remove(offset);
        rebuildRowOrder();
        endResetModel();
        emit pointersChanged();

        return true;
    }
    else
        return false;
}

quint32 PointerListModel::dropPointersBatch(const QVector<qint64> &ptrOffsets)
{
    if (ptrOffsets.isEmpty())
        return 0;

    beginResetModel();
    quint32 dropped = 0;

    for (const qint64 offset : ptrOffsets)
    {
        if (_pointers.contains(offset))
        {
            const qint64 pointedOffset = decodePtrTarget(_pointers.value(offset));
            if (!--_offsets[pointedOffset])
                _offsets.remove(pointedOffset);

            _pointers.remove(offset);
            ++dropped;
        }
    }

    if (dropped > 0)
        rebuildRowOrder();

    endResetModel();
    if (dropped > 0)
        emit pointersChanged();
    return dropped;
}

quint32 PointerListModel::dropOffset(const qint64 offset)
{
    if (_offsets.contains(offset))
    {
        beginResetModel();

        // Find all pointer keys whose decoded target matches offset
        QList<qint64> ptrs;
        for (auto it = _pointers.cbegin(); it != _pointers.cend(); ++it)
        {
            if (decodePtrTarget(it.value()) == offset)
                ptrs.append(it.key());
        }

        for (const qint64 key : ptrs)
            _pointers.remove(key);

        _offsets.remove(offset);
        rebuildRowOrder();
        endResetModel();
        emit pointersChanged();

        return true;
    }
    else
        return 0;
}

QList<qint64> PointerListModel::getPointers(qint64 dataOffset)
{
    if (!_offsets.contains(dataOffset))
        return QList<qint64>();

    QList<qint64> result;
    for (auto it = _pointers.cbegin(); it != _pointers.cend(); ++it)
    {
        if (decodePtrTarget(it.value()) == dataOffset)
            result.append(it.key());
    }
    return result;
}

qint64 PointerListModel::getOffset(qint64 ptrOffset)
{
    const qint64 stored = _pointers.value(ptrOffset, -1);
    return (stored == -1) ? -1 : decodePtrTarget(stored);
}

int PointerListModel::getPointerSize(qint64 ptrOffset) const
{
    const qint64 stored = _pointers.value(ptrOffset, 0);
    return decodePtrSize(stored);
}

bool PointerListModel::isPointer(qint64 offset)
{
    return _pointers.contains(offset);
}

bool PointerListModel::hasOffset(qint64 offset)
{
    return _offsets.contains(offset);
}

QString PointerListModel::getOffsetText(qint64 offset) const
{
    if (!_hexEdit)
        return "<unavailable>";

    constexpr qint64 kMaxPreviewBytes = 0x100;

    const qint64 rawStored = _pointers.value(offset, -1);
    if (rawStored < 0)
        return "<unavailable>";
    const qint64 targetOffset = decodePtrTarget(rawStored);
    if (targetOffset < 0)
        return "<unavailable>";

    QString txt;
    auto _tb = _hexEdit->getTranslationTable();

    const QByteArray raw = _hexEdit->dataAt(targetOffset, kMaxPreviewBytes);
    QByteArray preview;
    preview.reserve(raw.size());

    bool truncatedByLength = false;

    for (int i = 0; i < raw.size(); ++i)
    {
        if (i > 0 && _offsets.contains(targetOffset + i))
            break;

        preview.append(raw.at(i));
    }

    if (preview.size() >= kMaxPreviewBytes)
        truncatedByLength = true;

    if (_tb)
    {
        txt = _tb->encode(preview, true);
    }
    else
    {
        txt = QString(preview);
    }

    if (truncatedByLength)
    {
        txt += "...";
    }
    else if (txt.isEmpty())
    {
        txt = "<binary>";
    }

    return txt;
}

void PointerListModel::setHexEdit(QHexEdit *hexEdit)
{
    _hexEdit = hexEdit;
}

void PointerListModel::rebuildRowOrder()
{
    _rowOrder = _pointers.keys().toVector();

    auto comparator = [this](qint64 lhs, qint64 rhs)
    {
        if (_sortColumn == 0)
        {
            const qint64 lv = _pointers.value(lhs);
            const qint64 rv = _pointers.value(rhs);

            if (lv == rv)
                return (_sortOrder == Qt::AscendingOrder) ? (lhs < rhs) : (lhs > rhs);

            return (_sortOrder == Qt::AscendingOrder) ? (lv < rv) : (lv > rv);
        }

        return (_sortOrder == Qt::AscendingOrder) ? (lhs < rhs) : (lhs > rhs);
    };

    std::sort(_rowOrder.begin(), _rowOrder.end(), comparator);
}

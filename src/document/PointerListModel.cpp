#include "PointerListModel.h"
#include "hexeditor/hexeditor.h"
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
    return 5;
}

bool PointerListModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!_pointers.empty() && index.isValid() && index.row() < _rowOrder.count()
        && role == Qt::EditRole && index.column() == 3)
    {
        const qint64 ptrOffset = _rowOrder[index.row()];
        const qint64 targetOffset = decodePtrTarget(_pointers.value(ptrOffset, -1));
        if (targetOffset < 0)
            return false;

        const QString newName = value.toString().trimmed();
        const QString oldName = _offsetNames.value(targetOffset);
        if (newName == oldName)
            return false;

        if (newName.isEmpty())
            _offsetNames.remove(targetOffset);
        else
            _offsetNames.insert(targetOffset, newName);

        emit dataChanged(this->index(index.row(), 3), this->index(index.row(), 3));
        emit pointersChanged();
        return true;
    }

    return false;
}

Qt::ItemFlags PointerListModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (index.isValid() && index.column() == 3)
        f |= Qt::ItemIsEditable;
    return f;
}

QVariant PointerListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole && section < sectionName.size())
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

    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return QVariant();

    if (index.column() == 0)
        // Row number (1-based)
        return index.row() + 1;

    if (index.column() == 1)
    {
        // Offset (where pointer is located)
        return QStringLiteral("0x") + QString::number(key, 16).toUpper().rightJustified(8, QLatin1Char('0'));
    }

    if (index.column() == 2)
    {
        // Pointer value (target address)
        const int ptrSize = getPointerSize(key);
        const int hexChars = ptrSize * 2;
        return QStringLiteral("0x") + QString::number(decodePtrTarget(value), 16).toUpper().rightJustified(hexChars, QLatin1Char('0'));
    }

    if (index.column() == 3)
    {
        const qint64 targetOffset = decodePtrTarget(value);
        return _offsetNames.value(targetOffset);
    }

    if (index.column() == 4)
        return getOffsetText(key);

    return QVariant();
}

void PointerListModel::sort(int column, Qt::SortOrder order)
{
    // Column 1: Offset (where pointer is stored)
    // Column 2: Pointer value (target address)
    // Column 0 (row #) and column 3 (data) are not sortable
    if (column < 1 || column > 2)
        return;

    beginResetModel();
    _sortColumn = column;  // column 1 or 2 directly
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

    emit dataChanged(index(0, 0), index(_rowOrder.count() - 1, 4));
}

void PointerListModel::clear()
{
    beginResetModel();
    _pointers.clear();
    _offsets.clear();
    _offsetNames.clear();
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
    if (oldOffset != -1)
        _offsets.remove(oldOffset, ptrOffset);

    _pointers.insert(ptrOffset, encodedValue);
    _offsets.insert(offset, ptrOffset);
    rebuildRowOrder();
    endResetModel();
    emit pointersChanged();

    return static_cast<quint32>(_offsets.count(offset));
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

        if (oldOffset != -1)
            _offsets.remove(oldOffset, ptrOffset);

        _pointers.insert(ptrOffset, encodedValue);
        _offsets.insert(offset, ptrOffset);
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
        _offsets.remove(pointedOffset, offset);

        if (!_offsets.contains(pointedOffset))
            _offsetNames.remove(pointedOffset);

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
            _offsets.remove(pointedOffset, offset);

            if (!_offsets.contains(pointedOffset))
                _offsetNames.remove(pointedOffset);

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

        const QList<qint64> ptrs = _offsets.values(offset);
        for (const qint64 key : ptrs)
            _pointers.remove(key);

        _offsets.remove(offset);
        _offsetNames.remove(offset);
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
    return _offsets.values(dataOffset);
}

qint64 PointerListModel::getOffset(qint64 ptrOffset) const
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

QString PointerListModel::getPointerTooltip(qint64 ptrOffset) const
{
    const qint64 targetOffset = getOffset(ptrOffset);
    if (targetOffset >= 0)
    {
        const QString name = offsetName(targetOffset);
        if (!name.isEmpty())
            return name;
    }
    return getOffsetText(ptrOffset);
}

QString PointerListModel::offsetName(qint64 offset) const
{
    return _offsetNames.value(offset);
}

bool PointerListModel::setOffsetName(qint64 offset, const QString &name)
{
    const QString trimmed = name.trimmed();
    const QString oldName = _offsetNames.value(offset);
    if (trimmed == oldName)
        return false;

    if (trimmed.isEmpty())
        _offsetNames.remove(offset);
    else
        _offsetNames.insert(offset, trimmed);

    if (!_rowOrder.isEmpty())
    {
        for (int row = 0; row < _rowOrder.size(); ++row)
        {
            const qint64 ptrOffset = _rowOrder[row];
            const qint64 targetOffset = decodePtrTarget(_pointers.value(ptrOffset, -1));
            if (targetOffset == offset)
                emit dataChanged(index(row, 3), index(row, 3));
        }
    }
    emit pointersChanged();
    return true;
}

void PointerListModel::setOffsetNames(const QMap<qint64, QString> &names)
{
    _offsetNames = names;
    if (!_rowOrder.isEmpty())
        emit dataChanged(index(0, 3), index(_rowOrder.count() - 1, 3));
    emit pointersChanged();
}

void PointerListModel::setHexEdit(HexEditor *hexEdit)
{
    _hexEdit = hexEdit;
}

void PointerListModel::rebuildRowOrder()
{
    _rowOrder = _pointers.keys().toVector();

    auto comparator = [this](qint64 lhs, qint64 rhs)
    {
        if (_sortColumn == 1)
        {
            // Sort by Offset (key): where the pointer is stored
            return (_sortOrder == Qt::AscendingOrder) ? (lhs < rhs) : (lhs > rhs);
        }
        else if (_sortColumn == 2)
        {
            // Sort by Pointer value (target address): decode and compare target values
            const qint64 lv = decodePtrTarget(_pointers.value(lhs));
            const qint64 rv = decodePtrTarget(_pointers.value(rhs));

            if (lv == rv)
                return (_sortOrder == Qt::AscendingOrder) ? (lhs < rhs) : (lhs > rhs);

            return (_sortOrder == Qt::AscendingOrder) ? (lv < rv) : (lv > rv);
        }

        return (_sortOrder == Qt::AscendingOrder) ? (lhs < rhs) : (lhs > rhs);
    };

    std::sort(_rowOrder.begin(), _rowOrder.end(), comparator);
}

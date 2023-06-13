#include "PointerListModel.h"

PointerListModel::PointerListModel(QObject *parent) : QAbstractTableModel(parent)
{}

int PointerListModel::rowCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent)
    return _pointers.count();
}

int PointerListModel::columnCount(const QModelIndex & parent) const
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

QVariant PointerListModel::data(const QModelIndex& index, int role) const
{
    if (_pointers.empty() || !index.isValid() || index.row() >= _pointers.count() || role != Qt::DisplayRole)
        return QVariant();

    auto it = _pointers.cbegin();

    std::advance(it, index.row());

    if (index.column() == 0)
        return QString::number(it.key(), 16).toUpper();

    if (index.column() == 1)
        return QString::number(it.value(), 16).toUpper();

    if (index.column() == 2)
        return getOffsetText(it.value());

    return QVariant();
}

bool PointerListModel::empty()
{
    return _pointers.isEmpty();
}

void PointerListModel::clear()
{
    beginResetModel();
    _pointers.clear();
    _offsets.clear();
    endResetModel();
}

quint32 PointerListModel::addPointer(const qint64 ptrOffset, qint64 offset)
{
    QString txt;

    _pointers.insert(ptrOffset, offset);

    emit dataChanged();

    return ++_offsets[offset];
}

bool PointerListModel::dropPointer(const qint64 offset)
{
    if (_pointers.contains(offset))
    {
        // drop if it was the last pointer to offset
        if (!--_offsets[offset])
            _offsets.remove(_pointers.value(offset));

        _pointers.remove(offset);

        emit dataChanged();

        return true;
    }
    else
        return false;
}

quint32 PointerListModel::dropOffset(const qint64 offset)
{
    if (_offsets.contains(offset))
    {
        auto ptrs = _pointers.keys(offset);

        for (auto it = ptrs.begin(); it != ptrs.end(); it++)
            _pointers.remove(*it);

        _offsets.remove(offset);

        emit dataChanged();

        return true;
    }
    else
        return 0;
}

QList<qint64> PointerListModel::getPointers(qint64 dataOffset)
{
    if (_offsets.contains(dataOffset))
    {
        return _pointers.keys(dataOffset);
    }

    return QList<qint64>();
}

qint64 PointerListModel::getOffset(qint64 ptrOffset)
{
    return _pointers.value(ptrOffset, -1);
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
    /*
        if (_tb)
        {
            auto pointedString = _chunks->data(val, 0x40);
            pointedString.truncate(pointedString.indexOf(stopChar));

            txt = _tb->encode(pointedString, true);
        }
        else
        {
            txt = QString(_chunks->data(val, 0x40)).toStdString().c_str();
        }

        if (txt.size() > 0)
        {
            if (txt.size() > 0x40)
            {
                txt += "...";
            }
        }
        else
        {
            txt = "<binary>";
        }
    */

    return "Not implemented(";
}

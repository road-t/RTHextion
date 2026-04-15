#include "internal.h"

// ═══════════════════════════════════════════════════════════════════════════
// HexEditor pointer search and undoable operations
// ═══════════════════════════════════════════════════════════════════════════

namespace {
    struct PointerState
    {
        qint64 pointerOffset = -1;
        bool hasTarget = false;
        qint64 targetOffset = -1;
        int ptrSize = 4;
    };

    class PointerEditCommand : public QUndoCommand
    {
    public:
        PointerEditCommand(PointerListModel *model,
                           const QVector<PointerState> &before,
                           const QVector<PointerState> &after,
                           const QString &text,
                           QUndoCommand *parent = nullptr)
            : QUndoCommand(text, parent)
            , _model(model)
            , _before(before)
            , _after(after)
        {
        }

        void undo() override
        {
            apply(_before);
        }

        void redo() override
        {
            apply(_after);
        }

    private:
        void apply(const QVector<PointerState> &states)
        {
            if (!_model)
                return;

            for (const PointerState &state : states)
            {
                if (state.hasTarget)
                    _model->addPointer(state.pointerOffset, state.targetOffset, state.ptrSize);
                else
                    _model->dropPointer(state.pointerOffset);
            }
        }

        PointerListModel *_model = nullptr;
        QVector<PointerState> _before;
        QVector<PointerState> _after;
    };

    PointerState capturePointerState(PointerListModel *model, qint64 pointerOffset)
    {
        PointerState state;
        state.pointerOffset = pointerOffset;
        if (!model)
            return state;

        const qint64 target = model->getOffset(pointerOffset);
        if (target >= 0)
        {
            state.hasTarget = true;
            state.targetOffset = target;
            state.ptrSize = model->getPointerSize(pointerOffset);
        }

        return state;
    }
} // anon

qint64 HexEditor::pointerStartAt(qint64 bytePos, int /*pointerSize*/)
{
    if (bytePos < 0)
        return -1;

    // Search back up to the maximum possible pointer size (4 bytes)
    const qint64 startMin = qMax(static_cast<qint64>(0), bytePos - 4 + 1);

    for (qint64 candidate = bytePos; candidate >= startMin; --candidate)
    {
        if (_pointers.isPointer(candidate))
        {
            const int storedSize = _pointers.getPointerSize(candidate);
            if (bytePos < candidate + storedSize)
                return candidate;
        }
    }

    return -1;
}


qint64 HexEditor::pointerTargetAt(qint64 bytePos, int pointerSize)
{
    const qint64 ptrStart = pointerStartAt(bytePos, pointerSize);
    return (ptrStart >= 0) ? _pointers.getOffset(ptrStart) : -1;
}


void HexEditor::setShowPointers(bool show)
{
    _showPointers = show;
    viewport()->update();
}


bool HexEditor::showPointers()
{
    return _showPointers;
}

// ── Disassembly view mode ──────────────────────────────────────


void HexEditor::setPointersColor(const QColor &color)
{
    _brushPointers = QBrush(color);
    _penPointers = QPen(_pointerFontColor);
    viewport()->update();
}


QColor HexEditor::pointersColor()
{
    return _brushPointers.color();
}


void HexEditor::setPointedColor(const QColor &color)
{
    _brushPointed = QBrush(color);
    _penPointed = QPen(_pointedFontColor);
    if (_scrollMapTarget)
        _scrollMapTarget->setColor(color);
    viewport()->update();
}


QColor HexEditor::pointedColor()
{
    return _brushPointed.color();
}


void HexEditor::setPointedFontColor(const QColor &color)
{
    _pointedFontColor = color;
    _penPointed = QPen(_pointedFontColor);
    viewport()->update();
}


QColor HexEditor::pointedFontColor()
{
    return _pointedFontColor;
}


void HexEditor::setPointerFontColor(const QColor &color)
{
    _pointerFontColor = color;
    _penPointers = QPen(_pointerFontColor);
    viewport()->update();
}


QColor HexEditor::pointerFontColor()
{
    return _pointerFontColor;
}


void HexEditor::setPointerFrameColor(const QColor &color)
{
    _pointerFrameColor = color;
    viewport()->update();
}


QColor HexEditor::pointerFrameColor()
{
    return _pointerFrameColor;
}


void HexEditor::setPointerFrameBackgroundColor(const QColor &color)
{
    _pointerFrameBackgroundColor = color;
    viewport()->update();
}


QColor HexEditor::pointerFrameBackgroundColor()
{
    return _pointerFrameBackgroundColor;
}


PointerListModel *HexEditor::pointers()
{
    return &_pointers;
}


void HexEditor::clearPointers()
{
    _pointers.clear();

    viewport()->update();
}

/**
 * The function searches for pointers in the specified direction(s) and adds them to the pointers list.
 *
 * @param order - byte order: LittleEndian, BigEndian or SwappedBytes
 * @param searchBefore - whether to search for pointers before the current selection
 * @param searchAfter - whether to search for pointers after the current selection
 * @param firstPrintable - if set, only consider values as pointers if the previous byte is not a printable character (between firstPrintable and lastPrintable)
 * @param lastPrintable - see firstPrintable
 * @param stopChar - if set, skip offsets where the first byte is equal to stopChar
 * @param excludeSelection - whether to exclude the current selection from search (only applicable if both searchBefore and searchAfter are true)
 * @return number of pointers found
 */

qint64 HexEditor::findPointers(int pointerSize, ByteOrder order, bool searchBefore, bool searchAfter, const char *firstPrintable, const char *lastPrintable, char stopChar, bool excludeSelection)
{
    // Validate pointer size
    if (pointerSize < 2 || pointerSize > 4)
        pointerSize = 4;

    const QByteArray fileData = data();
    const char *buf = fileData.constData();
    const qint64 fileSize = fileData.size();

    qint64 selBegin = _bSelectionBegin;
    qint64 selEnd = _bSelectionEnd;
    if (selBegin >= selEnd)
    {
        selBegin = 0;
        selEnd = fileSize;
    }

    struct SearchRange
    {
        qint64 startOffset;
        qint64 endOffsetExclusive;
    };

    const qint64 maxDecodedStartExclusive = fileSize - pointerSize + 1;

    QVector<SearchRange> ranges;
    auto addRange = [&ranges](qint64 startOffset, qint64 endOffsetExclusive)
    {
        if (startOffset < endOffsetExclusive)
        {
            SearchRange range;
            range.startOffset = startOffset;
            range.endOffsetExclusive = endOffsetExclusive;
            ranges.push_back(range);
        }
    };

    if (searchBefore)
        addRange(0, selBegin);
    if (searchAfter)
        addRange(selEnd, fileSize);
    if (searchBefore && searchAfter && !excludeSelection)
    {
        ranges.clear();
        addRange(0, fileSize);
    }

    auto isCandidateOffset = [&](quint64 value) -> bool
    {
        const qint64 targetOffset = static_cast<qint64>(value);

        if (targetOffset < selBegin || targetOffset >= selEnd)
            return false;

        if (!firstPrintable || !lastPrintable)
            return true;

        if (targetOffset == 0)
            return true;

        const char prevChar = buf[targetOffset - 1];
        return !(prevChar >= *firstPrintable && prevChar <= *lastPrintable);
    };

    qint64 found = 0;
    qint64 its = 0;
    QElapsedTimer timer;
    timer.start();

    for (const auto &range : ranges)
    {
        const qint64 startOffset = qBound(static_cast<qint64>(0), range.startOffset, fileSize);
        const qint64 endOffsetExclusive = qBound(static_cast<qint64>(0), range.endOffsetExclusive, fileSize);
        const qint64 decodeEndExclusive = qMin(endOffsetExclusive, maxDecodedStartExclusive);

        for (qint64 j = startOffset; j < decodeEndExclusive; ++j)
        {
            if (stopChar && buf[j] == stopChar)
            {
                ++its;
                continue;
            }

            quint64 value = 0;
            const uchar *ptr = reinterpret_cast<const uchar *>(buf + j);
            value = decodePointer(ptr, pointerSize, order);

            if (isCandidateOffset(value))
            {
                _pointers.addPointer(j, static_cast<qint64>(value));
                ++found;
            }
            ++its;
        }
    }

    return found;
}


bool HexEditor::addPointerUndoable(qint64 pointerOffset, qint64 targetOffset, int ptrSize)
{
    if (pointerOffset < 0 || targetOffset < 0)
        return false;

    const PointerState before = capturePointerState(&_pointers, pointerOffset);

    if (before.hasTarget && before.targetOffset == targetOffset && before.ptrSize == ptrSize)
        return false;

    PointerState after;
    after.pointerOffset = pointerOffset;
    after.hasTarget = true;
    after.targetOffset = targetOffset;
    after.ptrSize = ptrSize;

    _undoStack->push(new PointerEditCommand(&_pointers,
                                            QVector<PointerState>{before},
                                            QVector<PointerState>{after},
                                            tr("Add pointer")));
    refresh();
    return true;
}


bool HexEditor::removePointerUndoable(qint64 pointerOffset)
{
    const PointerState before = capturePointerState(&_pointers, pointerOffset);
    if (!before.hasTarget)
        return false;

    PointerState after;
    after.pointerOffset = pointerOffset;
    after.hasTarget = false;

    _undoStack->push(new PointerEditCommand(&_pointers,
                                            QVector<PointerState>{before},
                                            QVector<PointerState>{after},
                                            tr("Drop pointer")));
    refresh();
    return true;
}


int HexEditor::removePointersUndoable(const QVector<qint64> &pointerOffsets)
{
    if (pointerOffsets.isEmpty())
        return 0;

    QVector<PointerState> before;
    QVector<PointerState> after;
    QSet<qint64> uniqueOffsets;
    uniqueOffsets.reserve(pointerOffsets.size());

    for (qint64 pointerOffset : pointerOffsets)
    {
        if (uniqueOffsets.contains(pointerOffset))
            continue;
        uniqueOffsets.insert(pointerOffset);

        const PointerState stateBefore = capturePointerState(&_pointers, pointerOffset);
        if (!stateBefore.hasTarget)
            continue;

        before.append(stateBefore);

        PointerState stateAfter;
        stateAfter.pointerOffset = pointerOffset;
        stateAfter.hasTarget = false;
        after.append(stateAfter);
    }

    if (before.isEmpty())
        return 0;

    _undoStack->push(new PointerEditCommand(&_pointers, before, after, tr("Drop pointer")));
    refresh();
    return before.size();
}


int HexEditor::removePointersToOffsetUndoable(qint64 targetOffset)
{
    const QList<qint64> pointersAtOffset = _pointers.getPointers(targetOffset);
    if (pointersAtOffset.isEmpty())
        return 0;

    QVector<PointerState> before;
    QVector<PointerState> after;
    before.reserve(pointersAtOffset.size());
    after.reserve(pointersAtOffset.size());

    for (qint64 pointerOffset : pointersAtOffset)
    {
        const PointerState stateBefore = capturePointerState(&_pointers, pointerOffset);
        if (!stateBefore.hasTarget)
            continue;

        before.append(stateBefore);

        PointerState stateAfter;
        stateAfter.pointerOffset = pointerOffset;
        stateAfter.hasTarget = false;
        after.append(stateAfter);
    }

    if (before.isEmpty())
        return 0;

    _undoStack->push(new PointerEditCommand(&_pointers, before, after, tr("Drop all")));
    refresh();
    return before.size();
}

#include "internal.h"
#include "encoding.h"

// ═══════════════════════════════════════════════════════════════════════════
// HexEditor layout: line breaks, visual rows, adjust, readBuffers, caches
// ═══════════════════════════════════════════════════════════════════════════

namespace {
    class LineBreakAddCommand : public QUndoCommand
    {
    public:
        LineBreakAddCommand(HexEditor *editor, qint64 offset, QUndoCommand *parent = nullptr)
            : QUndoCommand(parent), _editor(editor), _offset(offset) {}
        int id() const override { return kLineBreakCmdId; }
        void redo() override { _editor->addLineBreakDirect(_offset); }
        void undo() override { _editor->removeLineBreakDirect(_offset); }
    private:
        HexEditor *_editor;
        qint64 _offset;
    };

    class LineBreakRemoveCommand : public QUndoCommand
    {
    public:
        LineBreakRemoveCommand(HexEditor *editor, qint64 offset, QUndoCommand *parent = nullptr)
            : QUndoCommand(parent), _editor(editor), _offset(offset) {}
        int id() const override { return kLineBreakCmdId; }
        void redo() override { _editor->removeLineBreakDirect(_offset); }
        void undo() override { _editor->addLineBreakDirect(_offset); }
    private:
        HexEditor *_editor;
        qint64 _offset;
    };

} // anon

void HexEditor::setFont(const QFont &font)
{
    QFont theFont(font);
    theFont.setStyleHint(QFont::Monospace);
    QWidget::setFont(theFont);
    QFontMetrics metrics = fontMetrics();
    _pxCharWidth = metrics.horizontalAdvance(QLatin1Char('2'));
    _pxCharHeight = metrics.height();
    _pxGapAdr = _pxCharWidth / 2;
    _pxGapAdrHex = _pxCharWidth * 2;
    _pxGapHexAscii = 2 * _pxCharWidth;
    _pxCursorWidth = _pxCharHeight / 7;
    _pxSelectionSub = _pxCharHeight / 5;
    updateColumnNumbersMetrics();
    invalidateAsciiAreaWidthCache();
    updateAsciiAreaMaxWidth();
    viewport()->update();
}


void HexEditor::updateColumnNumbersMetrics()
{
    const QFontMetrics metrics(_columnNumbersFont);
    _pxColumnNumbersHeight = _showColumnNumbers ? (metrics.height() + kHexRowExtraGapPx) : 0;
}


void HexEditor::init()
{
    _undoStack->clear();
    _lineBreakCmdCount = 0;
    _cleanUndoIndex = 0;
    setAddressOffset(0);
    resetSelection(0);
    setCursorPosition(0);
    verticalScrollBar()->setValue(0);

    _baseModified = false;
    _modified = false;
}

// ── Virtual line breaks ────────────────────────────────────────

QVector<qint64> HexEditor::lineBreaks() const
{
    return _savedLineBreaksValid ? _savedLineBreaks : _lineBreaks;
}


void HexEditor::setLineBreaks(const QVector<qint64> &breaks)
{
    if (_savedLineBreaksValid) {
        _savedLineBreaks = breaks;
        std::sort(_savedLineBreaks.begin(), _savedLineBreaks.end());
        if (_showDisasm)
            emit lineBreaksChanged();
        else
            rebuildSectionAwareLayout();
        return;
    }

    _lineBreaks = breaks;
    std::sort(_lineBreaks.begin(), _lineBreaks.end());
    adjust();
    viewport()->update();
    emit lineBreaksChanged();
}


void HexEditor::addLineBreakDirect(qint64 offset)
{
    QVector<qint64> &target = _savedLineBreaksValid ? _savedLineBreaks : _lineBreaks;
    auto it = std::upper_bound(target.begin(), target.end(), offset);
    target.insert(it, offset);
    if (_savedLineBreaksValid) {
        if (_showDisasm)
            emit lineBreaksChanged();
        else
            rebuildSectionAwareLayout();
        return;
    }
    adjust();
    viewport()->update();
    emit lineBreaksChanged();
}


void HexEditor::removeLineBreakDirect(qint64 offset)
{
    QVector<qint64> &target = _savedLineBreaksValid ? _savedLineBreaks : _lineBreaks;
    auto it = std::lower_bound(target.begin(), target.end(), offset);
    if (it != target.end() && *it == offset) {
        target.erase(it);
        if (_savedLineBreaksValid) {
            if (_showDisasm)
                emit lineBreaksChanged();
            else
                rebuildSectionAwareLayout();
            return;
        }
        adjust();
        viewport()->update();
        emit lineBreaksChanged();
    }
}


void HexEditor::addLineBreak(qint64 offset)
{
    ++_lineBreakCmdCount;  // pre-update before indexChanged fires
    _lineBreakChangeInProgress = true;
    _undoStack->push(new LineBreakAddCommand(this, offset));
    _lineBreakChangeInProgress = false;
}


void HexEditor::removeLineBreak(qint64 offset)
{
    const QVector<qint64> currentBreaks = lineBreaks();
    auto it = std::lower_bound(currentBreaks.constBegin(), currentBreaks.constEnd(), offset);
    if (it != currentBreaks.constEnd() && *it == offset) {
        ++_lineBreakCmdCount;  // pre-update before indexChanged fires
        _lineBreakChangeInProgress = true;
        _undoStack->push(new LineBreakRemoveCommand(this, offset));
        _lineBreakChangeInProgress = false;
    }
}


void HexEditor::toggleLineBreak(qint64 offset)
{
    QVector<qint64> &target = _savedLineBreaksValid ? _savedLineBreaks : _lineBreaks;
    auto it = std::lower_bound(target.begin(), target.end(), offset);
    if (it != target.end() && *it == offset)
        target.erase(it);
    else
        target.insert(it, offset);

    if (_savedLineBreaksValid) {
        if (_showDisasm)
            emit lineBreaksChanged();
        else
            rebuildSectionAwareLayout();
        return;
    }

    adjust();
    viewport()->update();
    emit lineBreaksChanged();
}


void HexEditor::clearLineBreaks()
{
    QVector<qint64> &target = _savedLineBreaksValid ? _savedLineBreaks : _lineBreaks;
    if (!target.isEmpty()) {
        target.clear();
        if (_savedLineBreaksValid) {
            if (_showDisasm)
                emit lineBreaksChanged();
            else
                rebuildSectionAwareLayout();
            return;
        }
        adjust();
        viewport()->update();
        emit lineBreaksChanged();
    }
}


void HexEditor::clearLineBreaksInRange(qint64 from, qint64 to)
{
    QVector<qint64> &target = _savedLineBreaksValid ? _savedLineBreaks : _lineBreaks;
    auto lo = std::lower_bound(target.begin(), target.end(), from);
    auto hi = std::upper_bound(lo, target.end(), to);
    if (lo == hi)
        return;
    target.erase(lo, hi);
    if (_savedLineBreaksValid) {
        if (_showDisasm)
            emit lineBreaksChanged();
        else
            rebuildSectionAwareLayout();
        return;
    }
    adjust();
    viewport()->update();
    emit lineBreaksChanged();
}


qint64 HexEditor::totalVisualRows() const
{
    const qint64 fileSize = _chunks->size();
    if (fileSize == 0) return 0;
    if (_lineBreaks.isEmpty())
        return (fileSize + _bytesPerLine - 1) / _bytesPerLine;

    qint64 total = 0;
    qint64 segStart = 0;
    for (qint64 brk : _lineBreaks) {
        if (brk >= fileSize) continue;
        if (brk < 0) {
            total += 1;
            continue;
        }
        if (brk < segStart) {
            total += 1;
            continue;
        }
        qint64 segSize = brk - segStart + 1;
        total += (segSize + _bytesPerLine - 1) / _bytesPerLine;
        segStart = brk + 1;
    }
    if (segStart < fileSize) {
        qint64 segSize = fileSize - segStart;
        total += (segSize + _bytesPerLine - 1) / _bytesPerLine;
    }
    return total;
}


qint64 HexEditor::byteOffsetForVisualRow(qint64 visualRow) const
{
    const qint64 fileSize = _chunks->size();
    if (_lineBreaks.isEmpty())
        return qMin(visualRow * _bytesPerLine, fileSize);

    qint64 rowsSoFar = 0;
    qint64 segStart = 0;
    for (qint64 brk : _lineBreaks) {
        if (brk >= fileSize) continue;
        if (brk < 0) {
            if (rowsSoFar == visualRow)
                return 0;
            rowsSoFar += 1;
            continue;
        }
        if (brk < segStart) {
            if (rowsSoFar == visualRow)
                return segStart;
            rowsSoFar += 1;
            continue;
        }
        qint64 segSize = brk - segStart + 1;
        qint64 segRows = (segSize + _bytesPerLine - 1) / _bytesPerLine;
        if (rowsSoFar + segRows > visualRow)
            return segStart + (visualRow - rowsSoFar) * _bytesPerLine;
        rowsSoFar += segRows;
        segStart = brk + 1;
    }
    return qMin(segStart + (visualRow - rowsSoFar) * _bytesPerLine, fileSize);
}

QVector<qint64> HexEditor::byteOffsetsForVisualRows(qint64 startRow, int count) const
{
    QVector<qint64> result(count);
    const qint64 fileSize = _chunks->size();

    if (_lineBreaks.isEmpty()) {
        for (int i = 0; i < count; ++i)
            result[i] = qMin((startRow + i) * _bytesPerLine, fileSize);
        return result;
    }

    int ri = 0; // index into result
    qint64 rowsSoFar = 0;
    qint64 segStart = 0;
    const qint64 endRow = startRow + count;

    for (qint64 brk : _lineBreaks) {
        if (ri >= count) break;
        if (brk >= fileSize) continue;
        if (brk < 0) {
            if (rowsSoFar >= startRow && rowsSoFar < endRow)
                result[ri++] = 0;
            rowsSoFar += 1;
            continue;
        }
        if (brk < segStart) {
            if (rowsSoFar >= startRow && rowsSoFar < endRow)
                result[ri++] = segStart;
            rowsSoFar += 1;
            continue;
        }
        qint64 segSize = brk - segStart + 1;
        qint64 segRows = (segSize + _bytesPerLine - 1) / _bytesPerLine;
        // Emit rows that fall within [startRow, endRow) from this segment
        if (rowsSoFar + segRows > startRow) {
            qint64 localFirst = qMax<qint64>(0, startRow - rowsSoFar);
            qint64 localLast = qMin(segRows, endRow - rowsSoFar);
            for (qint64 lr = localFirst; lr < localLast && ri < count; ++lr) {
                result[ri++] = segStart + lr * _bytesPerLine;
            }
        }
        rowsSoFar += segRows;
        segStart = brk + 1;
    }
    // Trailing segment after last break
    if (ri < count && segStart < fileSize) {
        qint64 segSize = fileSize - segStart;
        qint64 segRows = (segSize + _bytesPerLine - 1) / _bytesPerLine;
        if (rowsSoFar + segRows > startRow) {
            qint64 localFirst = qMax<qint64>(0, startRow - rowsSoFar);
            qint64 localLast = qMin(segRows, endRow - rowsSoFar);
            for (qint64 lr = localFirst; lr < localLast && ri < count; ++lr) {
                result[ri++] = qMin(segStart + lr * _bytesPerLine, fileSize);
            }
        }
    }
    // Fill any remaining slots with fileSize
    for (; ri < count; ++ri)
        result[ri] = fileSize;
    return result;
}


qint64 HexEditor::visualRowForByte(qint64 bytePos) const
{
    const qint64 fileSize = _chunks->size();
    if (_lineBreaks.isEmpty())
        return bytePos / _bytesPerLine;

    qint64 rowsSoFar = 0;
    qint64 segStart = 0;
    for (qint64 brk : _lineBreaks) {
        if (brk >= fileSize) continue;
        if (brk < 0) {
            rowsSoFar += 1;
            continue;
        }
        if (brk < segStart) {
            rowsSoFar += 1;
            continue;
        }
        if (bytePos <= brk)
            return rowsSoFar + (bytePos - segStart) / _bytesPerLine;
        qint64 segSize = brk - segStart + 1;
        rowsSoFar += (segSize + _bytesPerLine - 1) / _bytesPerLine;
        segStart = brk + 1;
    }
    return rowsSoFar + (bytePos - segStart) / _bytesPerLine;
}


qint64 HexEditor::firstByteOfVisualRowContaining(qint64 bytePos) const
{
    return byteOffsetForVisualRow(visualRowForByte(bytePos));
}


int HexEditor::bytesOnVisualRowAt(qint64 byteOffset) const
{
    const qint64 fileSize = _chunks->size();
    if (byteOffset >= fileSize) return 0;
    int maxBytes = static_cast<int>(qMin<qint64>(_bytesPerLine, fileSize - byteOffset));

    auto it = std::lower_bound(_lineBreaks.constBegin(), _lineBreaks.constEnd(), byteOffset);
    if (it != _lineBreaks.constEnd() && *it < byteOffset + maxBytes)
        return static_cast<int>(*it - byteOffset + 1);

    return maxBytes;
}


int HexEditor::visibleRowForByte(qint64 bytePos) const
{
    if (_visualRowStartBytes.size() < 2) return 0;
    for (int i = 0; i < _visualRowStartBytes.size() - 1; ++i) {
        if (bytePos >= _visualRowStartBytes[i] && bytePos < _visualRowStartBytes[i + 1])
            return i;
    }
    return _visualRowStartBytes.size() - 2;
}


void HexEditor::adjust()
{
    // recalc Graphics
    if (_addressArea)
    {
        _addrDigits = addressWidth();
        _pxPosHexX = _pxGapAdr + _addrDigits * _pxCharWidth + _pxGapAdrHex + kAddressRightPaddingPx;
    }
    else
        _pxPosHexX = _pxGapAdrHex;

    _pxPosAdrX = _pxGapAdr;
    _pxPosAsciiX = _pxPosHexX + _hexCharsInLine * _pxCharWidth + (_bytesPerLine - 1) * kHexColumnExtraGapPx + _pxGapHexAscii;

    // set horizontalScrollBar()
    int pxWidth;
    if (_asciiArea)
    {
        pxWidth = _pxPosAsciiX + kAsciiAreaLeftPaddingPx + static_cast<int>(_asciiAreaMaxWidth);

        // If any graphics mode is active, the tile canvas may be wider
        // than the normal ASCII area.  Account for that.
        if (_showGraphicsPanel || (_sectionModel && _chunks)) {
            // Use the widest possible auto-computed tile columns
            const int rowStridePx = _pxCharHeight + kHexRowExtraGapPx;
            const int gfxWidth = _pxPosAsciiX + kAsciiAreaLeftPaddingPx
                               + _gfxAutoTileCols * 8 * rowStridePx;
            if (gfxWidth > pxWidth)
                pxWidth = gfxWidth;
        }

        bool hasPaletteSections = false;
        if (_sectionModel) {
            for (const auto &section : _sectionModel->sections()) {
                if (section.displayMode == SectionDisplay_Palette) {
                    hasPaletteSections = true;
                    break;
                }
            }
        }

        if (_showPalettePanel || hasPaletteSections) {
            const int rowStridePx = _pxCharHeight + kHexRowExtraGapPx;
            const int paletteWidth = _pxPosAsciiX + kAsciiAreaLeftPaddingPx
                                   + qMax(1, _bytesPerLine) * rowStridePx;
            if (paletteWidth > pxWidth)
                pxWidth = paletteWidth;
        }
    }
    else
        pxWidth = _pxPosAsciiX - _pxGapHexAscii; // no gap wasted when ASCII area is hidden

    horizontalScrollBar()->setRange(0, pxWidth - viewport()->width());
    horizontalScrollBar()->setPageStep(viewport()->width());

    // set verticalScrollbar()
    const int rowStridePx = _pxCharHeight + kHexRowExtraGapPx;
    _rowsShown = qMax(0, (viewport()->height() - _pxColumnNumbersHeight - 4) / rowStridePx);

    const qint64 lineCount = totalVisualRows();

    verticalScrollBar()->setRange(0, qMax<qint64>(0, lineCount - _rowsShown));
    verticalScrollBar()->setPageStep(_rowsShown);

    auto value = verticalScrollBar()->value();

    // Precompute absolute byte offsets for each visible row in a single pass
    _visualRowStartBytes = byteOffsetsForVisualRows(value, _rowsShown + 1);
    _bPosFirst = _visualRowStartBytes.isEmpty() ? 0 : _visualRowStartBytes[0];

    _bPosLast = _visualRowStartBytes[_rowsShown] - 1;
    if (_bPosLast >= _chunks->size())
        _bPosLast = _chunks->size() - 1;
    if (_bPosLast < _bPosFirst)
        _bPosLast = _bPosFirst;

    readBuffers();
    setCursorPosition(_cursorPosition);

    // Reposition scroll map strips to the right margin reserved by setViewportMargins().
    // Always use viewport geometry — scrollbar geometry is unreliable on macOS
    // overlay scrollbars (often zero/transient).
    {
        const QRect vp = viewport()->geometry();
        if (vp.isValid() && vp.height() > 0)
        {
            int x = vp.right() + 1;
            if (_scrollMapChanges && _scrollMapChanges->isVisible())
            {
                _scrollMapChanges->setGeometry(x, vp.top(), _scrollMapWidth, vp.height());
                x += _scrollMapWidth;
            }
            if (_scrollMapTarget && _scrollMapTarget->isVisible())
            {
                _scrollMapTarget->setGeometry(x, vp.top(), _scrollMapWidth, vp.height());
            }
        }
    }
}


void HexEditor::dataChangedPrivate(int)
{
    const int logicalUndoIndex = _undoStack->index() - _lineBreakCmdCount;
    _modified = _baseModified || (logicalUndoIndex != _cleanUndoIndex);

    if (_nonDataChangeInProgress) {
        // Pointer/section commands don't alter hex data — skip layout rebuild
        // and stale dataChangedAt emission.  The undo()/redo() caller handles
        // cursor + refresh.
        emit dataChanged();
        emit undoAvailable(_undoStack->canUndo());
        emit redoAvailable(_undoStack->canRedo());
        return;
    }

    invalidateAsciiAreaWidthCache();
    updateAsciiAreaMaxWidth();

    if (_lineBreakChangeInProgress) {
        // add/remove line-break already updated layout directly
    } else if (_showDisasm) {
        // Global disasm: full rescan needed.
        _disasmBoundaries.clear();
        _disasmCache.clear();
        _disasmCacheStart = _disasmCacheEnd = -1;
        rebuildDisasmLayout();
    } else if (hasSectionDisasmMode()) {
        // Only rescan disasm boundaries when the edit falls inside a
        // section displayed as disassembly; otherwise the instruction
        // layout is unchanged and the normal adjust() path is enough.
        const qint64 changedPos = _chunks->pos();
        const bool inDisasmSection = _sectionModel
            && _sectionModel->displayModeAtOffset(changedPos) == SectionDisplay_Disasm;
        if (inDisasmSection) {
            _disasmBoundaries.clear();
            _disasmCache.clear();
            _disasmCacheStart = _disasmCacheEnd = -1;
            rebuildSectionAwareLayout();
        } else {
            _disasmCache.clear();
            _disasmCacheStart = _disasmCacheEnd = -1;
            adjust();
        }
    } else {
        adjust();
    }

    emit dataChanged();
    emit dataChangedAt(_chunks->pos());

    emit undoAvailable(_undoStack->canUndo());
    emit redoAvailable(_undoStack->canRedo());
}


void HexEditor::refresh()
{
    // If cursor is within the already-visible range, ensureVisible() won't
    // change scrollbar values and adjust()/readBuffers() won't be called.
    // In that case, skip the redundant readBuffers().
    const qint64 oldFirst = _bPosFirst;
    const qint64 oldLast  = _bPosLast;

    ensureVisible();

    // If ensureVisible changed the scroll position, adjust() was triggered
    // which already called readBuffers(). Only re-read if nothing changed.
    if (_bPosFirst == oldFirst && _bPosLast == oldLast)
    {
        // Buffers are still valid — no need to re-read from IO.
        // Just schedule a repaint.
        viewport()->update();
    }
}


uint32_t HexEditor::computeAsciiAreaMaxWidthForBytesPerLine(int bytesPerLine)
{
    if (!_asciiArea || bytesPerLine <= 0)
        return 0;

    // When a table is loaded use its max symbol width, otherwise one char per byte.
    const int slotWidthPx = (_tb && _tbMaxSymbolWidthPx > 0)
        ? (_tbMaxSymbolWidthPx + ((_tbMaxSymbolWidthPx > _pxCharWidth) ? kAsciiColumnGapWidePx : kAsciiColumnGapSinglePx))
        : (_pxCharWidth + kAsciiColumnGapSinglePx);

    return static_cast<uint32_t>(bytesPerLine * slotWidthPx);
}


void HexEditor::updateAsciiAreaMaxWidth()
{
    if (_tb)
        ensureAsciiAreaWidthCache();

    _asciiAreaMaxWidth = computeAsciiAreaMaxWidthForBytesPerLine(_bytesPerLine);
}


void HexEditor::restoreTopVisibleByte(qint64 topByte)
{
    if (_bytesPerLine <= 0)
        return;

    const int topLine = static_cast<int>(qMax<qint64>(0, topByte) / _bytesPerLine);
    verticalScrollBar()->setValue(topLine);
    adjust();
}


void HexEditor::invalidateAsciiAreaWidthCache()
{
    _asciiAreaWidthCacheValid = false;
    _tbMaxSymbolWidthPx = 0;
    _asciiAreaWidthCacheTable = nullptr;
    _asciiAreaWidthCacheDataSize = -1;
    _asciiAreaWidthCacheCharWidth = 0;
}


void HexEditor::ensureAsciiAreaWidthCache()
{
    if (!_tb)
        return;

    if (_asciiAreaWidthCacheValid && _asciiAreaWidthCacheTable == _tb && _asciiAreaWidthCacheCharWidth == _pxCharWidth)
    {
        return;
    }

    _tbSymbolWidthPxCache = QVector<int>(256, _pxCharWidth);
    _asciiAreaMaxWidthByBpl = QVector<uint32_t>(65, 0);
    _tbMaxSymbolWidthPx = _pxCharWidth;

    const QFontMetrics fm(font());

    // Compute actual rendered width for each possible byte value using the table
    for (int value = 0; value <= 0xFF; ++value)
    {
        char rawByte = static_cast<char>(value);
        QString sym;
        
        if (_tb)
        {
            sym = _tb->encodeSymbol(rawByte);
            // Replace "not in table" placeholder with actual placeholder char
            if (!sym.size())
                sym = QString(_notInTableChar);
        }
        else
        {
            QChar ch = QChar::fromLatin1(rawByte);
            sym = ch;
            // Replace non-printable chars with placeholder
            if (sym.size() == 1 && !sym[0].isPrint())
                sym = QString(_nonPrintableNoTableChar);
        }
        
        // Measure actual font width rather than just counting characters
        int widthPx = qMax(_pxCharWidth, fm.horizontalAdvance(sym));
        _tbSymbolWidthPxCache[value] = widthPx;
        if (widthPx > _tbMaxSymbolWidthPx)
            _tbMaxSymbolWidthPx = widthPx;
    }

    for (int bpl = 4; bpl <= 64; bpl += 4) {
        const int gap = (_tbMaxSymbolWidthPx > _pxCharWidth) ? kAsciiColumnGapWidePx : kAsciiColumnGapSinglePx;
        _asciiAreaMaxWidthByBpl[bpl] = static_cast<uint32_t>(bpl * (_tbMaxSymbolWidthPx + gap));
    }

    // Also scan multi-byte entries to get the true maximum symbol width (e.g. kanji)
    if (_tb->hasMultiByteEntries())
    {
        for (const QString &val : _tb->getMultiByteItems())
        {
            const int widthPx = qMax(_pxCharWidth, fm.horizontalAdvance(val));
            if (widthPx > _tbMaxSymbolWidthPx)
                _tbMaxSymbolWidthPx = widthPx;
        }
        // Recompute per-bpl widths with updated max
        for (int bpl = 4; bpl <= 64; bpl += 4) {
            const int gap = (_tbMaxSymbolWidthPx > _pxCharWidth) ? kAsciiColumnGapWidePx : kAsciiColumnGapSinglePx;
            _asciiAreaMaxWidthByBpl[bpl] = static_cast<uint32_t>(bpl * (_tbMaxSymbolWidthPx + gap));
        }
    }

    _asciiAreaWidthCacheTable = _tb;
    _asciiAreaWidthCacheDataSize = -1;
    _asciiAreaWidthCacheCharWidth = _pxCharWidth;
    _asciiAreaWidthCacheValid = true;
}


void HexEditor::readBuffers()
{
    if (_showOriginal && !_originalData.isEmpty()) {
        const qint64 count = _bPosLast - _bPosFirst + _bytesPerLine + 1;
        const qint64 origSize = static_cast<qint64>(_originalData.size());
        const qint64 safeFrom = qMin(_bPosFirst, origSize);
        const qint64 safeCount = qMax<qint64>(0, qMin(count, origSize - safeFrom));
        _dataShown = _originalData.mid(static_cast<int>(safeFrom), static_cast<int>(safeCount));
        _hexDataShown = QByteArray(_dataShown.toHex());
        // Mark positions that differ between original and the current (edited) data
        const QByteArray currentSlice = _chunks->data(_bPosFirst, count);
        _markedShown = QByteArray(_dataShown.size(), char(0));
        for (int i = 0; i < _dataShown.size() && i < currentSlice.size(); ++i) {
            if (_dataShown.at(i) != currentSlice.at(i))
                _markedShown[i] = char(1);
        }
    } else {
        _dataShown = _chunks->data(_bPosFirst, _bPosLast - _bPosFirst + _bytesPerLine + 1, &_markedShown);
        _hexDataShown = QByteArray(_dataShown.toHex());
    }
    _encodingCacheValid = false;
    _tbDisplayCacheValid = false;
}


void HexEditor::ensureEncodingDisplayCache()
{
    if (_encodingCacheValid) return;
    _encodingCacheValid = true;
    _encodingChars.clear();
    _encodingSpan.clear();
    if (_tb || _currentEncoding == QLatin1String("ASCII"))
        return; // not applicable: TBL active or plain ASCII
    _encodingChars = decodeBufferWithEncoding(_dataShown, _currentEncoding);

    // Derive span from _encodingChars: lead byte gets count of bytes in its sequence;
    // continuation bytes get 0 (same convention as _tbDisplaySpan).
    const int n = _encodingChars.size();
    _encodingSpan = QVector<int>(n, 0);
    int i = 0;
    while (i < n) {
        if (_encodingChars[i].isNull()) {
            // Orphaned continuation (shouldn't happen with valid data, but handle gracefully)
            _encodingSpan[i] = 0;
            ++i;
        } else {
            // Lead byte: span = 1 + number of consecutive null (continuation) entries
            int span = 1;
            while ((i + span) < n && _encodingChars[i + span].isNull())
                ++span;
            _encodingSpan[i] = span;
            i += span;
        }
    }
}


void HexEditor::ensureTableDisplayCache()
{
    if (_tbDisplayCacheValid) return;
    _tbDisplayCacheValid = true;
    _tbDisplayChars.clear();
    _tbDisplaySpan.clear();
    if (!_tb || !_tb->hasMultiByteEntries())
        return;
    decodeBufferWithTable(_dataShown, _tb, _tbDisplayChars, _tbDisplaySpan);
}


QString HexEditor::toReadable(const QByteArray &ba)
{
    QString result;

    auto baLen = ba.size();

    for (int i = 0; i < baLen; i += 16)
    {
        QString addrStr = QString("%1").arg(_addressOffset + i, addressWidth(), 16, QChar('0'));
        QString hexStr;
        const QByteArray rowBytes = ba.mid(i, qMin(16, baLen - i));
        QString ascStr = decodeTextForCurrentEncoding(rowBytes);

        for (int j = 0; j < 16; j++)
        {
            if ((i + j) < baLen)
            {
                hexStr.append(" ").append(ba.mid(i + j, 1).toHex());
            }
        }

        for (int k = 0; k < ascStr.size(); ++k)
            if (!ascStr[k].isPrint())
                ascStr[k] = QChar('.');

        result += addrStr + " " + QString("%1").arg(hexStr, -48) + "  " + QString("%1").arg(ascStr, -17) + "\n";
    }
    return result;
}


void HexEditor::updateCursor()
{
    _blink = !_blink;

    viewport()->update(_asciiCursorRect);
    viewport()->update(_hexCursorRect);
}

// ---- Scroll map visibility API -------------------------------------------------


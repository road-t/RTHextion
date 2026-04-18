#include "internal.h"
#include "encoding.h"
#include <QRegularExpression>

namespace {
struct DisasmOperandLink {
    int start = 0;
    int length = 0;
    qint64 target = -1;
};

static QVector<DisasmOperandLink> disasmPointerLinksForOperands(
    const QString &operands,
    PointerListModel *pointerModel,
    qint64 fileSize)
{
    QVector<DisasmOperandLink> links;
    if (!pointerModel || operands.isEmpty() || fileSize <= 0)
        return links;

    static const QRegularExpression kHexValueRx(
        QStringLiteral("(?:\\$|0x)([0-9A-Fa-f]{1,8})"),
        QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatchIterator it = kHexValueRx.globalMatch(operands);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        if (!m.hasMatch())
            continue;

        bool ok = false;
        const QString digits = m.captured(1);
        const qint64 target = digits.toLongLong(&ok, 16);
        if (!ok || target < 0 || target >= fileSize)
            continue;
        if (!pointerModel->hasOffset(target))
            continue;

        const int start = m.capturedStart(1);
        const int len = m.capturedLength(1);
        if (start < 0 || len <= 0)
            continue;

        links.append({start, len, target});
    }

    return links;
}
}

// ═══════════════════════════════════════════════════════════════════════════
// HexEditor event handlers: keyboard, mouse, paint, resize
// ═══════════════════════════════════════════════════════════════════════════

void HexEditor::keyPressEvent(QKeyEvent *event)
{
    // Pure modifier keys must not trigger ensureVisible()/refresh() because that
    // can unexpectedly jump vertical scroll position.
    if (event->key() == Qt::Key_Shift
        || event->key() == Qt::Key_Control
        || event->key() == Qt::Key_Alt
        || event->key() == Qt::Key_Meta)
    {
        event->accept();
        return;
    }

    // Virtual line break: Enter adds a break before the current byte
    if (event->key() == Qt::Key_Return && event->modifiers() == Qt::NoModifier) {
        const qint64 brk = _cursorPosition / 2;
        if (brk > 0)
            addLineBreak(brk - 1);
        event->accept();
        return;
    }

    // Virtual line break removal: Backspace removes break before cursor, Delete removes break at cursor.
    // Handled here (before the read-only guard) so breaks can be removed regardless of edit mode.
    if (event->modifiers() == Qt::NoModifier
        && (event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete))
    {
        const QVector<qint64> currentBreaks = lineBreaks();
        if (!currentBreaks.isEmpty()) {
            const qint64 brkOffset = (event->key() == Qt::Key_Delete)
                                     ? _bPosCurrent       // Delete: break after cursor byte
                                     : _bPosCurrent - 1;  // Backspace: break before cursor byte
            if (brkOffset >= 0) {
                auto it = std::lower_bound(currentBreaks.constBegin(), currentBreaks.constEnd(), brkOffset);
                if (it != currentBreaks.constEnd() && *it == brkOffset) {
                    removeLineBreak(brkOffset);
                    event->accept();
                    return;
                }
            }
        }
        // No line break at that offset — fall through to normal data-edit handling below.
    }

    // Pre-compute visual row info for navigation
    const qint64 navByte = _cursorPosition / 2;
    const qint64 navNibble = _cursorPosition % 2;
    const qint64 navVisRow = visualRowForByte(navByte);
    const qint64 navRowStart = byteOffsetForVisualRow(navVisRow);
    const int navCol = static_cast<int>(navByte - navRowStart);

    // Graphics tile shift: Shift+Arrow when cursor is in graphics area
    if (isGraphicsAt(_bPosCurrent)
        && (event->modifiers() & Qt::ShiftModifier)
        && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier)))
    {
        if (event->key() == Qt::Key_Left) {
            --_gfxTileShift;
            viewport()->update();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Right) {
            ++_gfxTileShift;
            viewport()->update();
            event->accept();
            return;
        }
    }

    // Cursor movements
    if (event->matches(QKeySequence::MoveToNextChar))
    {
        qint64 pos;
        if (_showOriginal && !_editAreaIsAscii)
            pos = (_cursorPosition / 2 + 1) * 2;  // byte-only: skip to next byte boundary
        else if (_editAreaIsAscii)
            pos = _cursorPosition + 2;
        else
            pos = _cursorPosition + 1;

        setCursorPosition(pos);
        resetSelection(pos);
    }

    if (event->matches(QKeySequence::MoveToPreviousChar))
    {
        qint64 pos;
        if (_showOriginal && !_editAreaIsAscii)
            pos = (_cursorPosition / 2 - 1) * 2;  // byte-only: skip to previous byte boundary
        else if (_editAreaIsAscii)
            pos = _cursorPosition - 2;
        else
            pos = _cursorPosition - 1;

        setCursorPosition(pos);
        resetSelection(pos);
    }

    if (event->matches(QKeySequence::MoveToEndOfLine))
    {
        qint64 rowBytes = bytesOnVisualRowAt(navRowStart);
        qint64 pos = (navRowStart + rowBytes - 1) * 2 + 1;
        setCursorPosition(pos);
        resetSelection(_cursorPosition);
    }

    if (event->matches(QKeySequence::MoveToStartOfLine))
    {
        qint64 pos = navRowStart * 2;
        setCursorPosition(pos);
        resetSelection(_cursorPosition);
    }

    if (event->matches(QKeySequence::MoveToPreviousLine))
    {
        if (navVisRow > 0) {
            qint64 prevRowStart = byteOffsetForVisualRow(navVisRow - 1);
            int prevRowBytes = bytesOnVisualRowAt(prevRowStart);
            qint64 newByte = prevRowStart + qMin(navCol, prevRowBytes - 1);
            setCursorPosition(newByte * 2 + navNibble);
        }
        resetSelection(_cursorPosition);
    }

    if (event->matches(QKeySequence::MoveToNextLine))
    {
        qint64 nextRowStart = byteOffsetForVisualRow(navVisRow + 1);
        if (nextRowStart < _chunks->size()) {
            int nextRowBytes = bytesOnVisualRowAt(nextRowStart);
            qint64 newByte = nextRowStart + qMin(navCol, qMax(0, nextRowBytes - 1));
            setCursorPosition(newByte * 2 + navNibble);
        }
        resetSelection(_cursorPosition);
    }

    if (event->matches(QKeySequence::MoveToNextPage))
    {
        qint64 targetRow = qMin(navVisRow + _rowsShown - 1, totalVisualRows() - 1);
        qint64 targetRowStart = byteOffsetForVisualRow(targetRow);
        int targetRowBytes = bytesOnVisualRowAt(targetRowStart);
        qint64 newByte = targetRowStart + qMin(navCol, qMax(0, targetRowBytes - 1));
        setCursorPosition(newByte * 2 + navNibble);
        resetSelection(_cursorPosition);
    }

    if (event->matches(QKeySequence::MoveToPreviousPage))
    {
        qint64 targetRow = qMax<qint64>(0, navVisRow - _rowsShown + 1);
        qint64 targetRowStart = byteOffsetForVisualRow(targetRow);
        int targetRowBytes = bytesOnVisualRowAt(targetRowStart);
        qint64 newByte = targetRowStart + qMin(navCol, qMax(0, targetRowBytes - 1));
        setCursorPosition(newByte * 2 + navNibble);
        resetSelection(_cursorPosition);
    }

    if (event->matches(QKeySequence::MoveToEndOfDocument))
    {
        setCursorPosition(_chunks->size() * 2);
        resetSelection(_cursorPosition);
    }

    if (event->matches(QKeySequence::MoveToStartOfDocument))
    {
        setCursorPosition(0);
        resetSelection(_cursorPosition);
    }

    // Select commands
    if (event->matches(QKeySequence::SelectAll))
    {
        resetSelection(0);
        setSelection(2 * _chunks->size() + 1);
    }

    if (event->matches(QKeySequence::SelectNextChar))
    {
        qint64 pos = _cursorPosition + 1;
        if (_editAreaIsAscii)
            pos += 1;
        else
            pos = (_cursorPosition | 1) + 1; // snap to next byte boundary
        setCursorPosition(pos);
        setSelection(pos);
    }

    if (event->matches(QKeySequence::SelectPreviousChar))
    {
        qint64 pos = _cursorPosition - 1;
        if (_editAreaIsAscii)
            pos -= 1;
        else
            pos = (_cursorPosition & ~1) - 2; // snap to previous byte boundary
        setSelection(pos);
        setCursorPosition(pos);
    }

    if (event->matches(QKeySequence::SelectEndOfLine))
    {
        qint64 rowBytes = bytesOnVisualRowAt(navRowStart);
        qint64 pos = (navRowStart + rowBytes - 1) * 2 + 1;
        setCursorPosition(pos);
        setSelection(pos);
    }

    if (event->matches(QKeySequence::SelectStartOfLine))
    {
        qint64 pos = navRowStart * 2;
        setCursorPosition(pos);
        setSelection(pos);
    }

    if (event->matches(QKeySequence::SelectPreviousLine))
    {
        if (navVisRow > 0) {
            qint64 prevRowStart = byteOffsetForVisualRow(navVisRow - 1);
            int prevRowBytes = bytesOnVisualRowAt(prevRowStart);
            qint64 newByte = prevRowStart + qMin(navCol, prevRowBytes - 1);
            qint64 pos = newByte * 2 + navNibble;
            setCursorPosition(pos);
            setSelection(pos);
        }
    }

    if (event->matches(QKeySequence::SelectNextLine))
    {
        qint64 nextRowStart = byteOffsetForVisualRow(navVisRow + 1);
        if (nextRowStart < _chunks->size()) {
            int nextRowBytes = bytesOnVisualRowAt(nextRowStart);
            qint64 newByte = nextRowStart + qMin(navCol, qMax(0, nextRowBytes - 1));
            qint64 pos = newByte * 2 + navNibble;
            setCursorPosition(pos);
            setSelection(pos);
        }
    }

    if (event->matches(QKeySequence::SelectNextPage))
    {
        qint64 targetRow = qMin(navVisRow + _rowsShown - 1, totalVisualRows() - 1);
        qint64 targetRowStart = byteOffsetForVisualRow(targetRow);
        int targetRowBytes = bytesOnVisualRowAt(targetRowStart);
        qint64 newByte = targetRowStart + qMin(navCol, qMax(0, targetRowBytes - 1));
        qint64 pos = newByte * 2 + navNibble;
        setCursorPosition(pos);
        setSelection(pos);
    }

    if (event->matches(QKeySequence::SelectPreviousPage))
    {
        qint64 targetRow = qMax<qint64>(0, navVisRow - _rowsShown + 1);
        qint64 targetRowStart = byteOffsetForVisualRow(targetRow);
        int targetRowBytes = bytesOnVisualRowAt(targetRowStart);
        qint64 newByte = targetRowStart + qMin(navCol, qMax(0, targetRowBytes - 1));
        qint64 pos = newByte * 2 + navNibble;
        setCursorPosition(pos);
        setSelection(pos);
    }

    if (event->matches(QKeySequence::SelectEndOfDocument))
    {
        qint64 pos = _chunks->size() * 2;
        setCursorPosition(pos);
        setSelection(pos);
    }

    if (event->matches(QKeySequence::SelectStartOfDocument))
    {
        qint64 pos = 0;
        setCursorPosition(pos);
        setSelection(pos);
    }

    // Edit Commands
    if (!_readOnly && !_showOriginal)
    {
        // Helper: block data modification when cursor or selection touches a disasm, audio, or graphics section.
        const auto editBlockedByDisasm = [this]() {
            if (isDisasmAt(_bPosCurrent) || isAudioAt(_bPosCurrent) || isGraphicsAt(_bPosCurrent))
                return true;
            if (hasSelection()
                && (isDisasmAt(getSelectionBegin()) || isDisasmAt(getSelectionEnd() - 1)
                    || isAudioAt(getSelectionBegin()) || isAudioAt(getSelectionEnd() - 1)
                    || isGraphicsAt(getSelectionBegin()) || isGraphicsAt(getSelectionEnd() - 1)))
                return true;
            return false;
        };

        /* Cut */
        if (event->matches(QKeySequence::Cut))
        {
            const qint64 selBegin = getSelectionBegin();
            const qint64 selEnd = getSelectionEnd();
            const QByteArray raw = _chunks->data(selBegin, selEnd - selBegin);

            if (_editAreaIsAscii)
            {
                QApplication::clipboard()->setText(decodeTextForCurrentEncoding(raw));
            }
            else
            {
                QApplication::clipboard()->setText(QString::fromLatin1(raw.toHex(' ')).toUpper());
            }

            // In REPLACE mode, cut only copies without deleting
            if (!_overwriteMode && !editBlockedByDisasm())
            {
                remove(selBegin, selEnd - selBegin);
                setCursorPosition(2 * selBegin);
                resetSelection(2 * selBegin);
            }
        }
        else

            /* Paste */
            if (event->matches(QKeySequence::Paste))
            {
                if (editBlockedByDisasm()) { /* disasm — no paste */ }
                else {
                QClipboard *clipboard = QApplication::clipboard();
                QByteArray ba;
                if (_editAreaIsAscii)
                {
                    ba = encodeTextForCurrentEncoding(clipboard->text());
                }
                else
                {
                    const QString stripped = clipboard->text()
                                                .remove(' ').remove('\t').remove('\n').remove('\r');
                    ba = QByteArray::fromHex(stripped.toLatin1());
                }

                const qint64 selBegin = getSelectionBegin();
                const qint64 selEnd = getSelectionEnd();
                const bool hasSelection = (selBegin != selEnd);

                if (_overwriteMode)
                {
                    if (hasSelection)
                    {
                        // REPLACE mode with selection: truncate paste to selection size, paste at selection beginning
                        const qint64 selLen = selEnd - selBegin;
                        ba = ba.left(static_cast<int>(selLen));
                        replace(selBegin, ba.size(), ba);
                        setCursorPosition(2 * (selBegin + ba.size()));
                    }
                    else
                    {
                        // REPLACE mode without selection: paste at cursor position
                        ba = ba.left(static_cast<int>(std::min<qint64>(ba.size(), (_chunks->size() - _bPosCurrent))));
                        replace(_bPosCurrent, ba.size(), ba);
                        setCursorPosition(_cursorPosition + 2 * ba.size());
                    }
                }
                else
                {
                    // INSERT mode
                    if (hasSelection)
                    {
                        // INSERT mode with selection: delete entire selection, then insert paste at selection beginning
                        const qint64 selLen = selEnd - selBegin;
                        remove(selBegin, static_cast<int>(selLen));
                        insert(selBegin, ba);
                        setCursorPosition(2 * (selBegin + ba.size()));
                    }
                    else
                    {
                        // INSERT mode without selection: insert at cursor position
                        insert(_bPosCurrent, ba);
                        setCursorPosition(_cursorPosition + 2 * ba.size());
                    }
                }
                resetSelection(getSelectionBegin());
                } // !editBlockedByDisasm
            }
            else

                /* Delete char */
                if (event->matches(QKeySequence::Delete))
                {
                    if (!editBlockedByDisasm()) {
                    if (getSelectionEnd() - getSelectionBegin() > 1)
                    {
                        _bPosCurrent = getSelectionBegin();
                        if (_overwriteMode)
                        {
                            QByteArray ba = QByteArray(getSelectionEnd() - getSelectionBegin(), char(0));
                            replace(_bPosCurrent, ba.size(), ba);
                        }
                        else
                        {
                            remove(_bPosCurrent, getSelectionEnd() - getSelectionBegin());
                        }
                    }
                    else
                    {
                        if (_overwriteMode)
                            replace(_bPosCurrent, char(0));
                        else
                            remove(_bPosCurrent, 1);
                    }
                    setCursorPosition(2 * _bPosCurrent);
                    resetSelection(2 * _bPosCurrent);
                    } // !editBlockedByDisasm
                }
                else

                    /* Backspace */
                    if ((event->key() == Qt::Key_Backspace) && (event->modifiers() == Qt::NoModifier))
                    {
                        // Line break removal is handled above before the read-only check.
                        // Here we only handle data deletion.
                        if (!editBlockedByDisasm()) {
                        if (!_overwriteMode)
                        {
                            // In INSERT mode backspace only removes line breaks (handled above).
                            // No data deletion — do nothing.
                        }
                        else
                        if (getSelectionEnd() - getSelectionBegin() > 1)
                        {
                            _bPosCurrent = getSelectionBegin();
                            setCursorPosition(2 * _bPosCurrent);

                            {
                                QByteArray ba = QByteArray(getSelectionEnd() - getSelectionBegin(), char(0));
                                replace(_bPosCurrent, ba.size(), ba);
                            }
                            resetSelection(2 * _bPosCurrent);
                        }
                        else
                        {
                            bool behindLastByte = false;
                            if ((_cursorPosition / 2) == _chunks->size())
                                behindLastByte = true;

                            _bPosCurrent -= 1;
                            replace(_bPosCurrent, char(0));

                            if (!behindLastByte)
                                _bPosCurrent -= 1;

                            setCursorPosition(2 * _bPosCurrent);
                            resetSelection(2 * _bPosCurrent);
                        }
                        } // !editBlockedByDisasm
                    }
                    else

                        if (event->matches(QKeySequence::Undo)) // UNDO
                    {
                        undo();
                    }
                    else if (event->matches(QKeySequence::Redo)) // REDO
                    {
                        redo();
                    }
                    else if (event->key() == Qt::Key_Space && isAudioAt(_bPosCurrent))
                    {
                        // Space in audio section → toggle playback
                        emit audioPlaybackToggled(_bPosCurrent);
                    }
                    else if (event->text() != "")
                    {
                        /* Hex and ascii input */
                        auto key = _editAreaIsAscii ? event->text().at(0) : event->text().at(0).toLower().toLatin1();

                        // Filter hex input
                        if ((((key >= '0' && key <= '9') || (key >= 'a' && key <= 'f')) && _editAreaIsAscii == false) || (key >= ' ' && _editAreaIsAscii))
                        {
                            if (editBlockedByDisasm()) { /* disasm — no typing */ }
                            else if (hasSelection())
                            {
                                if (_overwriteMode)
                                {
                                    qint64 len = getSelectionEnd() - getSelectionBegin();
                                    replace(getSelectionBegin(), (int)len, QByteArray((int)len, char(0)));
                                }
                                else
                                {
                                    remove(getSelectionBegin(), getSelectionEnd() - getSelectionBegin());
                                    _bPosCurrent = getSelectionBegin();
                                }

                                setCursorPosition(2 * _bPosCurrent);
                                resetSelection(2 * _bPosCurrent);
                            }

                            // If insert mode, then insert a byte
                            if (!_overwriteMode && !(_cursorPosition % 2))
                                insert(_bPosCurrent, char(0));

                            // Change content
                            if (_chunks->size() > 0)
                            {
                                if (!_editAreaIsAscii)
                                {
                                    QByteArray hexValue = _chunks->data(_bPosCurrent, 1).toHex();

                                    if ((_cursorPosition % 2) == 0)
                                        hexValue[0] = key.toLatin1();
                                    else
                                        hexValue[1] = key.toLatin1();

                                    char ch = QByteArray().fromHex(hexValue)[0];
                                    replace(_bPosCurrent, ch);
                                    setCursorPosition(_cursorPosition + 1);
                                }
                                else
                                {
                                    // ASCII edit mode: try multi-byte TBL lookup first
                                    QByteArray bytesToWrite;
                                    if (_tb)
                                    {
                                        bytesToWrite = _tb->decodeToBytes(QString(key));
                                        if (bytesToWrite.isEmpty())
                                        {
                                            // Not in any table entry — keep as raw byte
                                            bytesToWrite = QByteArray(1, key.toLatin1());
                                        }
                                    }
                                    else
                                    {
                                        // Use encoding-aware conversion so non-ASCII characters
                                        // (e.g. Cyrillic typed via keyboard IME) are stored correctly.
                                        bytesToWrite = encodeTextForCurrentEncoding(QString(key));
                                        if (bytesToWrite.isEmpty())
                                            bytesToWrite = QByteArray(1, key.toLatin1());
                                    }

                                    const int bytesLen = bytesToWrite.size();

                                    // In insert mode, the outer block already inserted 1 null byte.
                                    // For multi-byte entries we need bytesLen total, so insert bytesLen-1 more.
                                    if (!_overwriteMode && !(_cursorPosition % 2) && bytesLen > 1)
                                        insert(_bPosCurrent + 1, QByteArray(bytesLen - 1, char(0)));

                                    if (bytesLen == 1)
                                        replace(_bPosCurrent, bytesToWrite[0]);
                                    else
                                        replace(_bPosCurrent, bytesLen, bytesToWrite);

                                    setCursorPosition(_cursorPosition + 2 * bytesLen);
                                }

                                resetSelection(_cursorPosition);
                            }
                        }
                    }
    }

    /* Copy */
    if (event->matches(QKeySequence::Copy))
    {
        const qint64 selBegin = getSelectionBegin();
        const qint64 selEnd = getSelectionEnd();
        if (selEnd <= selBegin) return;
        const QByteArray raw = _chunks->data(selBegin, selEnd - selBegin);
        const qint64 selLast = qMax<qint64>(selBegin, selEnd - 1);

        const bool copyDisasm = _showDisasm
            || (_sectionModel && (_sectionModel->displayModeAtOffset(selBegin) == SectionDisplay_Disasm
                || _sectionModel->displayModeAtOffset(selLast) == SectionDisplay_Disasm));

        if (copyDisasm) {
            const QString disasmText = selectedDisasmText();
            if (!disasmText.isEmpty()) {
                QApplication::clipboard()->setText(disasmText);
                return;
            }
            QApplication::clipboard()->setText(decodeTextForCurrentEncoding(raw));
        } else if (_editAreaIsAscii) {
            QApplication::clipboard()->setText(decodeTextForCurrentEncoding(raw));
        } else {
            QApplication::clipboard()->setText(QString::fromLatin1(raw.toHex(' ')).toUpper());
        }
    }

    // Switch between insert/overwrite mode
    if ((event->key() == Qt::Key_Insert) && (event->modifiers() == Qt::NoModifier))
    {
        setOverwriteMode(!overwriteMode());
        setCursorPosition(_cursorPosition);
    }

    // switch from hex to ascii edit
    if (event->key() == Qt::Key_Tab && !_editAreaIsAscii)
    {
        _editAreaIsAscii = true;
        setCursorPosition(_cursorPosition);
    }

    // switch from ascii to hex edit
    if (event->key() == Qt::Key_Backtab && _editAreaIsAscii)
    {
        _editAreaIsAscii = false;
        setCursorPosition(_cursorPosition);
    }

    refresh();
    QAbstractScrollArea::keyPressEvent(event);
}


void HexEditor::mouseMoveEvent(QMouseEvent *event)
{
    _blink = false;
    viewport()->update();

    // Handle address area boundary drag (resize by 1 byte per step)
    if (_addrDragging && _addressArea)
    {
        const int pixelDelta = event->x() - _addrDragStartX;
        const int pixelsPerByte = 2 * _pxCharWidth;  // 2 hex digits per byte
        const int byteDelta = pixelDelta / pixelsPerByte;
        if (byteDelta != 0)
        {
            const int currentBytes = _addressWidth / 2;
            const int newBytes = qBound(1, currentBytes + byteDelta, 8);
            const int newWidth = newBytes * 2;  // hex digits
            if (newWidth != _addressWidth)
            {
                _addrDragStartX += byteDelta * pixelsPerByte;
                _addressWidth = newWidth;
                if (_dynamicBytesPerLine)
                    resizeEvent(nullptr);
                else
                    adjust();
                viewport()->update();
            }
        }
        viewport()->setCursor(Qt::SizeHorCursor);
        return;
    }

    // Handle separator dragging (only when autosize is OFF and ASCII area visible)
    if (_separatorDragging && !_dynamicBytesPerLine && _asciiArea)
    {
        int pixelDelta = event->x() - _separatorDragStartX;
        
        // Convert pixel delta to bytes per line delta.
        // Each byte takes 3 characters in hex area (2 hex digits + 1 space),
        // plus extra gaps between columns.
        int pixelsPerByte = 3 * _pxCharWidth + kHexColumnExtraGapPx;
        
        // Subtract the gap for the last column to get realistic byte changes
        pixelsPerByte = 3 * _pxCharWidth + kHexColumnExtraGapPx - kHexColumnExtraGapPx / _bytesPerLine;
        
        // Use 3 chars width + gap as the primary measure
        int byteDelta = pixelDelta / (3 * _pxCharWidth);
        
        // Calculate new bytes per line
        int newBytesPerLine = _bytesPerLine + byteDelta;
        
        // Clamp to reasonable range: 4-64 bytes per line
        if (newBytesPerLine < 4)
            newBytesPerLine = 4;
        else if (newBytesPerLine > 64)
            newBytesPerLine = 64;
        
        if (newBytesPerLine != _bytesPerLine)
        {
            _separatorDragStartX = event->x();
            setBytesPerLine(newBytesPerLine);
        }
        
        viewport()->setCursor(Qt::SizeHorCursor);
        return;
    }

    // Determine cursor based on autosize mode and hover position
    {
        const int pxOfsX_mv = horizontalScrollBar()->value();
        bool cursorSet = false;

        // Address/hex boundary: click collapses/expands address area
        if (_addressArea)
        {
            const int addrSepX = _pxPosHexX - _pxGapAdrHex - pxOfsX_mv;
            if (std::abs(event->x() - addrSepX) < 8)
            {
                viewport()->setCursor(Qt::SizeHorCursor);
                cursorSet = true;
            }
        }

        if (!cursorSet)
        {
            if (_dynamicBytesPerLine)
            {
                viewport()->setCursor(Qt::ArrowCursor);
            }
            else if (_asciiArea)
            {
                const int separatorScreenX = _pxPosAsciiX - (_pxGapHexAscii / 2) - pxOfsX_mv;
                if (std::abs(event->x() - separatorScreenX) < 8) {
                    viewport()->setCursor(Qt::SizeHorCursor);
                } else {
                    // Hand cursor over branch instructions in disasm area
                    bool handSet = false;
                    const int posX = event->x() + pxOfsX_mv;
                    if (posX >= _pxPosAsciiX) {
                        const qint64 nPos = cursorPosition(event->pos());
                        if (nPos >= 0) {
                            const qint64 byteOfs = nPos / 2;
                            const bool isDisasm = _showDisasm
                                || (_sectionModel && _sectionModel->displayModeAtOffset(byteOfs) == SectionDisplay_Disasm);
                            if (isDisasm) {
                                const DisasmInstruction *instr = disasmInstructionAtOffset(byteOfs);
                                if (instr && instr->isBranch && instr->branchTarget >= 0
                                    && instr->branchTarget < _chunks->size()) {
                                    viewport()->setCursor(Qt::PointingHandCursor);
                                    handSet = true;
                                }
                            }
                        }
                    }
                    if (!handSet)
                        viewport()->setCursor(Qt::ArrowCursor);
                }
            }
            else
            {
                viewport()->setCursor(Qt::ArrowCursor);
            }
        }
    }

    // Only update selection if mouse button is pressed (not on pure hover)
    if (event->buttons() != Qt::NoButton)
    {
        qint64 actPos = cursorPosition(event->pos());

        if (actPos >= 0)
        {
            // Continuous graphics drawing while dragging with pressed mouse button.
            if (_gfxClickPixX >= 0 && _gfxClickPixY >= 0) {
                Qt::MouseButton drawButton = Qt::NoButton;
                if (event->buttons() & Qt::LeftButton)
                    drawButton = Qt::LeftButton;
                else if (event->buttons() & Qt::RightButton)
                    drawButton = Qt::RightButton;

                if (drawButton != Qt::NoButton) {
                    setCursorPosition(actPos);
                    resetSelection(actPos);
                    gfxSetPixel(drawButton);
                    _gfxClickPixX = -1;
                    _gfxClickPixY = -1;
                    return;
                }
            }

            setCursorPosition(actPos);
            setSelection(actPos);
        }
    }
}


bool HexEditor::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::ToolTip && _showPointers)
    {
        QHelpEvent *helpEvent = static_cast<QHelpEvent *>(event);
        const qint64 nibblePos = cursorPosition(helpEvent->pos());
        const qint64 bytePos = nibblePos / 2;

        if (bytePos >= 0)
        {
            const qint64 ptrStart = pointerStartAt(bytePos, kPointerByteSize);
            if (ptrStart >= 0)
            {
                QToolTip::showText(helpEvent->globalPos(), _pointers.getOffsetText(ptrStart), viewport());
                return true;
            }
            else if (_pointers.hasOffset(bytePos))
            {
                const auto ptrs = _pointers.getPointers(bytePos);
                const QString tip = (ptrs.size() == 1)
                    ? QStringLiteral("0x") + QStringLiteral("%1").arg(ptrs[0], 8, 16, QChar('0')).toUpper()
                    : tr("%1 pointers").arg(ptrs.size());
                QToolTip::showText(helpEvent->globalPos(), tip, viewport());
                return true;
            }
        }
        QToolTip::hideText();
        event->ignore();
        return true;
    }
    return QAbstractScrollArea::viewportEvent(event);
}


void HexEditor::mousePressEvent(QMouseEvent *event)
{
    _blink = false;
    viewport()->update();

    // Address/hex boundary: start drag to resize address area by 1 byte per step
    if (_addressArea && event->button() == Qt::LeftButton)
    {
        const int pxOfsX = horizontalScrollBar()->value();
        const int addrSepX = _pxPosHexX - _pxGapAdrHex - pxOfsX;
        if (std::abs(event->x() - addrSepX) < 8)
        {
            _addrDragging = true;
            _addrDragStartX = event->x();
            viewport()->setCursor(Qt::SizeHorCursor);
            return;
        }
    }

    // Check if separator drag is starting (only if not in dynamic/autosize mode)
    if (!_dynamicBytesPerLine && _asciiArea)
    {
        int pxOfsX = horizontalScrollBar()->value();
        int separatorScreenX = _pxPosAsciiX - (_pxGapHexAscii / 2) - pxOfsX;
        
        // If click is within 8 pixels of separator, start drag
        if (std::abs(event->x() - separatorScreenX) < 8)
        {
            _separatorDragging = true;
            _separatorDragStartX = event->x();
            viewport()->setCursor(Qt::SizeHorCursor);
            return;
        }
    }

    qint64 cPos = cursorPosition(event->pos());

    // Graphics pixel drawing: if click was in the graphics area, paint the pixel
    if (cPos >= 0 && _gfxClickPixX >= 0 && _gfxClickPixY >= 0) {
        setCursorPosition(cPos);
        resetSelection(cPos);
        gfxSetPixel(event->button());
        _gfxClickPixX = -1;
        _gfxClickPixY = -1;
        return;
    }

    // Empty buffer bootstrap: first click in editable area creates one byte
    // so overwrite-mode typing can immediately replace it.
    if (cPos >= 0 && _chunks->size() == 0 && !_readOnly && event->button() == Qt::LeftButton)
    {
        insert(0, char(0));
        cPos = 0;
    }

    if (cPos >= 0)
    {
        if (event->button() == Qt::RightButton)
        {
            // On right-click: only update cursor position, do NOT reset or
            // change the selection so it is preserved for the context menu.
            setCursorPosition(cPos);
        }
        else
        {
            if (event->modifiers() != Qt::ShiftModifier)
                resetSelection(cPos);

            setCursorPosition(cPos);
            const bool isDisasmClick = _showDisasm
                || (_sectionModel && _sectionModel->displayModeAtOffset(cPos / 2) == SectionDisplay_Disasm);
            // In disasm mode, simple click should only move the cursor.
            // Keep selection updates for Shift-click (range selection) and mouse drag.
            if (!isDisasmClick || event->modifiers() == Qt::ShiftModifier)
                setSelection(cPos);

            if (_showPointers)
            {
                const qint64 ptrStart = pointerStartAt(_bPosCurrent, kPointerByteSize);

                if (ptrStart >= 0)
                {
                    QToolTip::showText(mapToGlobal(event->pos()), _pointers.getOffsetText(ptrStart));
                }
                else if (_pointers.hasOffset(_bPosCurrent))
                {
                    auto ptrs = _pointers.getPointers(_bPosCurrent);

                    if (ptrs.size() == 1)
                    {
                        QToolTip::showText(mapToGlobal(event->pos()), QString("0x%1").arg(ptrs[0], 8, 16, QChar('0')));
                    }
                    else
                    {
                        QToolTip::showText(mapToGlobal(event->pos()), tr("%1 pointers").arg(ptrs.size()));
                    }
                }
            }
        }
    }
}


void HexEditor::mouseReleaseEvent(QMouseEvent *event)
{
    // End address area drag
    if (_addrDragging)
    {
        _addrDragging = false;
        viewport()->setCursor(Qt::ArrowCursor);
        // Persist the new address width to QSettings
        auto &s = AppSettings::instance();
        s.setValue(QStringLiteral("AddressAreaWidth"), _addressWidth);
        event->accept();
        return;
    }

    // End separator dragging
    if (_separatorDragging)
    {
        _separatorDragging = false;
        viewport()->setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }
    
    QAbstractScrollArea::mouseReleaseEvent(event);
}


void HexEditor::mouseDoubleClickEvent(QMouseEvent *event)
{
    // ── Section header rename: detect click on empty header row ──
    if (_sectionModel) {
        const int rowStridePx = _pxCharHeight + kHexRowExtraGapPx;
        const int posY = static_cast<int>(event->position().y()) - 3;
        const int row = posY / rowStridePx;
        if (row >= 0 && row < _visualRowStartBytes.size()) {
            const int bytesThisRow = (row + 1 < _visualRowStartBytes.size())
                ? static_cast<int>(_visualRowStartBytes[row + 1] - _visualRowStartBytes[row])
                : _bytesPerLine;
            if (bytesThisRow <= 0) {
                const qint64 absOfs = _visualRowStartBytes[row];
                // Only react on the last consecutive empty row (the one
                // that visually shows the section name), not on blank
                // separator rows above it.
                bool nextRowIsData = true;
                if (row + 2 < _visualRowStartBytes.size()) {
                    nextRowIsData = (_visualRowStartBytes[row + 2]
                                     - _visualRowStartBytes[row + 1]) > 0;
                }
                if (nextRowIsData) {
                    const int secIdx = _sectionModel->sectionIndexAtStartOffset(absOfs);
                    if (secIdx >= 0) {
                        // Edit directly from the main hex area on double-click.
                        bool ok = false;
                        const QString oldName = _sectionModel->at(secIdx).name;
                        const QString newName = QInputDialog::getText(
                            this,
                            tr("Rename section"),
                            tr("Section name:"),
                            QLineEdit::Normal,
                            oldName,
                            &ok);
                        if (ok) {
                            const QString trimmed = newName.trimmed();
                            if (!trimmed.isEmpty() && trimmed != oldName)
                                _sectionModel->renameSection(secIdx, trimmed);
                        }
                        return;
                    }
                }
            }
        }
    }

    if (_showPointers)
    {
        // Highest priority: if something points TO the current offset, jump to that pointer source
        if (_pointers.hasOffset(_bPosCurrent))
        {
            _editAreaIsAscii = false;

            auto ptrs = _pointers.getPointers(_bPosCurrent);

            if (ptrs.size() == 1)
            {
                setCursorPosition(ptrs[0] * 2);
                ensureVisible();
            }
            else
            {
                // TODO: display context menu/listbox with pointers list
            }
            return;
        }
        // Otherwise, if the current byte is part of a pointer, follow it to the data target
        else
        {
            const qint64 ptrStart = pointerStartAt(_bPosCurrent, kPointerByteSize);
            if (ptrStart >= 0)
            {
                _editAreaIsAscii = true;
                setCursorPosition(_pointers.getOffset(ptrStart) * 2);
                ensureVisible();
                return;
            }
        }
    }

    // ── Follow branch target in disassembly rows ──
    {
        const bool isDisasm = _showDisasm
            || (_sectionModel && _sectionModel->displayModeAtOffset(_bPosCurrent) == SectionDisplay_Disasm);
        if (isDisasm) {
            const int clickX = static_cast<int>(event->position().x()) + horizontalScrollBar()->value();
            const int clickY = static_cast<int>(event->position().y());

            if (_asciiArea && clickX >= _pxPosAsciiX && clickY >= 0) {
                const int rowStridePx = _pxCharHeight + kHexRowExtraGapPx;
                const int row = clickY / rowStridePx;
                if (row >= 0 && row < _visualRowStartBytes.size()) {
                    const qint64 rowOffset = _visualRowStartBytes[row];
                    const DisasmInstruction *rowInstr = disasmInstructionAtOffset(rowOffset);
                    if (rowInstr && !rowInstr->operands.isEmpty()) {
                        QString displayOps = rowInstr->operands;
                        const bool clickableBranch = rowInstr->isBranch
                            && rowInstr->branchTarget >= 0
                            && rowInstr->branchTarget < _chunks->size();
                        bool usingSectionLabel = false;
                        if (clickableBranch && _sectionModel) {
                            const QString label = _sectionModel->sectionNameAtStartOffset(rowInstr->branchTarget);
                            if (!label.isEmpty()) {
                                displayOps = label;
                                usingSectionLabel = true;
                            }
                        }

                        if (!usingSectionLabel) {
                            const QVector<DisasmOperandLink> links = disasmPointerLinksForOperands(
                                displayOps, &_pointers, _chunks->size());
                            if (!links.isEmpty()) {
                                const QFontMetrics fm = QFontMetrics(font());
                                int opsStartX = _pxPosAsciiX + kAsciiAreaLeftPaddingPx;
                                opsStartX += fm.horizontalAdvance(rowInstr->mnemonic);
                                opsStartX += fm.horizontalAdvance(QLatin1Char(' '));

                                for (const auto &link : links) {
                                    const int prefixW = fm.horizontalAdvance(displayOps.left(link.start));
                                    const int linkW = fm.horizontalAdvance(displayOps.mid(link.start, link.length));
                                    const int linkStartX = opsStartX + prefixW;
                                    const int linkEndX = linkStartX + linkW;
                                    if (clickX >= linkStartX && clickX < linkEndX) {
                                        setCursorPosition(link.target * 2);
                                        ensureVisibleTop();
                                        if (verticalScrollBar()->value() > 0)
                                            verticalScrollBar()->setValue(verticalScrollBar()->value() - 1);
                                        return;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            const DisasmInstruction *instr = disasmInstructionAtOffset(_bPosCurrent);
            if (instr && instr->isBranch && instr->branchTarget >= 0
                && instr->branchTarget < _chunks->size())
            {
                setCursorPosition(instr->branchTarget * 2);
                // Show one row above the target so the section header is visible
                ensureVisibleTop();
                if (verticalScrollBar()->value() > 0)
                    verticalScrollBar()->setValue(verticalScrollBar()->value() - 1);
                return;
            }
        }
    }
}


void HexEditor::contextMenuEvent(QContextMenuEvent *event)
{
    const int posX = event->pos().x() + horizontalScrollBar()->value();
    const int rowStridePx = _pxCharHeight + kHexRowExtraGapPx;
    const int row = static_cast<int>(event->pos().y()) / rowStridePx;

    if (_asciiArea && posX >= _pxPosAsciiX && row >= 0 && row < _visualRowStartBytes.size()) {
        const auto rowBytesThisRow = [this](int r) -> int {
            if (r < 0 || r >= _visualRowStartBytes.size())
                return 0;
            return (r + 1 < _visualRowStartBytes.size())
                ? static_cast<int>(_visualRowStartBytes[r + 1] - _visualRowStartBytes[r])
                : _bytesPerLine;
        };

        const qint64 rowAbsOfs = _visualRowStartBytes[row];
        bool suppressMenu = isGraphicsAt(rowAbsOfs);
        if (!suppressMenu && rowBytesThisRow(row) <= 0 && _sectionModel) {
            int prev = row - 1;
            while (prev >= 0 && rowBytesThisRow(prev) <= 0)
                --prev;
            if (prev >= 0) {
                const qint64 prevOfs = _visualRowStartBytes[prev];
                suppressMenu = (_sectionModel->displayModeAtOffset(prevOfs) == SectionDisplay_Graphics);
            }
        }
        if (suppressMenu) {
            event->accept();
            return;
        }
    }

    emit contextMenuRequested(event->globalPos(), _bPosCurrent);
}


void HexEditor::paintEvent(QPaintEvent *event)
{
    QPainter painter(viewport());
    auto pxOfsX = horizontalScrollBar()->value();

    if (event->rect() != _asciiCursorRect && event->rect() != _hexCursorRect)
    {
        const int rowStridePx = _pxCharHeight + kHexRowExtraGapPx;
        int pxPosStartY = _pxCharHeight;

        // draw some patterns if needed
        painter.fillRect(event->rect(), viewport()->palette().color(QPalette::Base));

        // Fill hex area with background color
        if (_asciiArea)
        {
            // Stop at the separator line (half-gap before ASCII area)
            const int hexAreaWidth = _pxPosAsciiX - _pxPosHexX - (_pxGapHexAscii / 2);
            painter.fillRect(QRect(_pxPosHexX - pxOfsX, event->rect().top(), hexAreaWidth, height()), _hexAreaBackgroundColor);
        }
        else
        {
            // No ASCII area: hex background extends to the right edge of the viewport
            painter.fillRect(QRect(_pxPosHexX - pxOfsX, event->rect().top(), viewport()->width(), height()), _hexAreaBackgroundColor);
        }

        painter.setPen(viewport()->palette().color(QPalette::WindowText));

        // paint address area
        if (_addressArea)
        {
            painter.fillRect(QRect(-pxOfsX, event->rect().top(), _pxPosHexX - _pxGapAdrHex, height()), _addressAreaColor);

            {
                QString address;
                auto rowsCount = 0;
                for (int i = 0; i < _visualRowStartBytes.size(); ++i) {
                    if (_visualRowStartBytes[i] < _bPosFirst + _dataShown.size())
                        rowsCount = i;
                    else break;
                }

                const QFont originalFont = painter.font();
                QFont boldFont = originalFont;
                boldFont.setBold(true);
                painter.setFont(boldFont);

                // Mask to lower _addrDigits hex digits so wide addresses wrap gracefully
                const int maskBitsCount = _addrDigits * 4;
                const quint64 addrMask = (maskBitsCount >= 64) ? ~quint64(0)
                                                                : ((quint64(1) << maskBitsCount) - 1);

                for (int row = 0, pxPosY = _pxCharHeight; row <= rowsCount; row++, pxPosY += rowStridePx)
                {
                    // Skip empty rows (duplicate line breaks produce rows with same start byte)
                    if (row + 1 < _visualRowStartBytes.size()
                        && _visualRowStartBytes[row] == _visualRowStartBytes[row + 1])
                        continue;

                    const qint64 rawAddr = _visualRowStartBytes[row] + _addressOffset;
                    const quint64 maskedAddr = static_cast<quint64>(rawAddr) & addrMask;
                    address = QString("%1").arg(maskedAddr, _addrDigits, 16, QChar('0'));
                    if (_hexCaps) address = address.toUpper();

                    if (_addressZeroByteFontColor.isValid() && _addressZeroByteFontColor != _addressFontColor)
                    {
                        // Find first non-zero digit to limit coloring to leading zeros only
                        int firstNonZero = 0;
                        while (firstNonZero < address.size() - 1 && address[firstNonZero] == QLatin1Char('0'))
                            ++firstNonZero;

                        int xPx = _pxPosAdrX - pxOfsX;
                        for (int d = 0; d < address.size(); ++d)
                        {
                            const bool isLeadingZero = (d < firstNonZero);
                            painter.setPen(QPen(isLeadingZero ? _addressZeroByteFontColor : _addressFontColor));
                            painter.drawText(xPx, pxPosY, address.mid(d, 1));
                            xPx += _pxCharWidth;
                        }
                    }
                    else
                    {
                        painter.setPen(QPen(_addressFontColor));
                        painter.drawText(_pxPosAdrX - pxOfsX, pxPosY, address);
                    }
                }

                painter.setFont(originalFont);
            }
        }

        // paint hex and ascii area
        painter.setBackgroundMode(Qt::TransparentMode);

        if (_asciiArea)
        {
            // ASCII area starts flush with the separator line (no gap between hex and ascii backgrounds)
            const int asciiAreaStartX = _pxPosAsciiX - (_pxGapHexAscii / 2);
            painter.fillRect(QRect(asciiAreaStartX - pxOfsX, event->rect().top(), width(), height()), _asciiAreaColor);

            // Draw the separator line on top of the ASCII background
            painter.setPen(Qt::gray);
            painter.drawLine(asciiAreaStartX - pxOfsX, event->rect().top(), asciiAreaStartX - pxOfsX, height());

            ensureAsciiAreaWidthCache();
        }

        const int hexStridePx = 3 * _pxCharWidth + kHexColumnExtraGapPx;

        // Build display caches once for the whole viewport (handles cross-row sequences)
        ensureTableDisplayCache();
        ensureEncodingDisplayCache();
        const bool useTbDisplayCache = !_tbDisplayChars.isEmpty();
        const bool useEncodingDecoder = !_encodingChars.isEmpty();
        const int cursorSectionMode = _sectionModel
            ? _sectionModel->displayModeAtOffset(_bPosCurrent)
            : SectionDisplay_Default;
        const bool cursorForcesRaw = (cursorSectionMode == SectionDisplay_Raw);
        const QFontMetrics paintFm = QFontMetrics(font());
        const auto slotGapPx = [this](int baseWidth) {
            return (baseWidth > _pxCharWidth) ? kAsciiColumnGapWidePx : kAsciiColumnGapSinglePx;
        };

        // Pre-compute which buffer group the cursor belongs to (for multi-byte cursor highlight)
        const qint64 cursorBufIdxGlobal = _cursorPosition / 2 - _bPosFirst;
        qint64 cursorLeadBufIdx = cursorBufIdxGlobal;
        int cursorMultiByteSpan = 1;
        if (!cursorForcesRaw && useTbDisplayCache && cursorBufIdxGlobal >= 0 && cursorBufIdxGlobal < _tbDisplayChars.size()) {
            qint64 li = cursorBufIdxGlobal;
            while (li > 0 && li < _tbDisplayChars.size() && _tbDisplayChars[(int)li].isNull())
                --li;
            if (li >= 0 && li < _tbDisplaySpan.size() && _tbDisplaySpan[(int)li] > 1) {
                cursorLeadBufIdx = li;
                cursorMultiByteSpan = _tbDisplaySpan[(int)li];
            }
        } else if (useEncodingDecoder && cursorBufIdxGlobal >= 0 && cursorBufIdxGlobal < _encodingChars.size()) {
            qint64 li = cursorBufIdxGlobal;
            while (li > 0 && li < _encodingChars.size() && _encodingChars[(int)li].isNull())
                --li;
            if (li >= 0 && li < _encodingSpan.size() && _encodingSpan[(int)li] > 1) {
                cursorLeadBufIdx = li;
                cursorMultiByteSpan = _encodingSpan[(int)li];
            }
        }

        for (int row = 0; row < _rowsShown; row++)
        {
            QByteArray hex;
            int pxPosY = pxPosStartY + row * rowStridePx;
            int pxPosX = _pxPosHexX - pxOfsX;
            int pxPosAsciiX2 = _pxPosAsciiX + kAsciiAreaLeftPaddingPx - pxOfsX;
            qint64 bPosLine = _visualRowStartBytes[row] - _bPosFirst;
            int bytesThisRow = (row + 1 < _visualRowStartBytes.size())
                ? static_cast<int>(_visualRowStartBytes[row + 1] - _visualRowStartBytes[row])
                : _bytesPerLine;
            bytesThisRow = qMin(bytesThisRow, static_cast<int>(_dataShown.size() - bPosLine));
            if (bPosLine < 0) continue;
            if (bytesThisRow <= 0) {
                // ── Section header row: draw the name only on the LAST
                //    consecutive empty row so earlier rows stay blank. ──
                if (_sectionModel) {
                    const qint64 absOfs = _visualRowStartBytes[row];
                    // Check whether the next row is a data row (bytesThisRow > 0).
                    // If so, the current row is the last empty row before data —
                    // that's where we draw the section header.
                    bool nextRowIsData = true;
                    if (row + 2 < _visualRowStartBytes.size()) {
                        nextRowIsData = (_visualRowStartBytes[row + 2]
                                         - _visualRowStartBytes[row + 1]) > 0;
                    }
                    if (nextRowIsData) {
                        const int secIdx = _sectionModel->sectionIndexAtStartOffset(absOfs);
                        const QString secName = (secIdx >= 0) ? _sectionModel->at(secIdx).name
                                                              : _sectionModel->sectionNameAtStartOffset(absOfs);
                        if (!secName.isEmpty()) {
                            const int textX = _pxPosHexX - pxOfsX;
                            const QRect textRect(textX, pxPosY - _pxCharHeight + _pxSelectionSub + 2,
                                                 viewport()->width() - textX, _pxCharHeight);

                            if (_sectionHeaderBackgroundColor.isValid())
                                painter.fillRect(textRect, _sectionHeaderBackgroundColor);

                            const QFont prevFont = painter.font();
                            const QPen prevPen = painter.pen();
                            painter.setFont(_sectionHeaderFont);
                            painter.setPen(_sectionHeaderFontColor.isValid()
                                               ? _sectionHeaderFontColor
                                               : palette().color(QPalette::Text));

                            painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                                             secName + QStringLiteral(":"));
                            painter.setFont(prevFont);
                            painter.setPen(prevPen);
                        }
                    }
                }
                continue;
            }

            const qint64 rowStart = bPosLine;
            const qint64 rowEnd = qMin(bPosLine + bytesThisRow, (qint64)_dataShown.size());
            const int rowSectionMode = _sectionModel
                ? _sectionModel->displayModeAtOffset(_bPosFirst + bPosLine)
                : SectionDisplay_Default;
            const bool rowForcesRaw = (rowSectionMode == SectionDisplay_Raw);
            const bool useTbMultiByte = useTbDisplayCache && !rowForcesRaw;
            const bool rowUsesDisasm = (rowSectionMode == SectionDisplay_Disasm)
                || (rowSectionMode == SectionDisplay_Default && _showDisasm);

            // can be slow here
            for (int colIdx = 0; ((bPosLine + colIdx) < _dataShown.size() && (colIdx < bytesThisRow)); colIdx++)
            {
                QColor c = viewport()->palette().color(QPalette::Base);

                // Section background (lowest priority — drawn first, overridden by everything)
                if (_showSections && _sectionModel) {
                    const qint64 posBaSection = _bPosFirst + bPosLine + colIdx;
                    const QColor sc = _sectionModel->colorAtOffset(posBaSection);
                    if (sc.isValid())
                        c = sc;
                }

                const char rawByte = _dataShown.at(bPosLine + colIdx);
                const bool isZeroByte = (rawByte == 0);
                painter.setPen(QPen((isZeroByte && !rowUsesDisasm) ? _zeroByteFontColor : _hexFontColor));

                qint64 posBa = _bPosFirst + bPosLine + colIdx;
                const qint64 pointerStart = (_showPointers && !rowUsesDisasm)
                    ? pointerStartAt(posBa, kPointerByteSize)
                    : -1;
                const bool isPointerByte = pointerStart >= 0;
                const int actualPtrSize = isPointerByte ? _pointers.getPointerSize(pointerStart) : kPointerByteSize;
                const bool isPointedByte = _showPointers && _pointers.hasOffset(posBa);
                const bool isSelectedByte = (getSelectionEnd() - getSelectionBegin() > 1)
                                         && (getSelectionBegin() <= posBa) && (getSelectionEnd() > posBa);
                const bool isHighlightedByte = _highlighting && _markedShown.at((int)(posBa - _bPosFirst));
                const bool isChangedByte = _showChanges && (_changedPositions.contains(posBa) 
                    || (posBa >= _changedRangeStart && posBa < _changedRangeEnd));

                if (isSelectedByte)
                {
                    c = _brushSelection.color();
                    painter.setPen(_penSelection);
                }
                else
                {
                    if (isHighlightedByte)
                    {
                        c = _brushHighlighted.color();
                        painter.setPen(_penHighlighted);
                    }
                    else if (isChangedByte)
                    {
                        c = _brushChanges.color();
                        painter.setPen(_penChanges);
                    }
                }

                // POINTERS
                if (_showPointers && !rowUsesDisasm)
                {
                    // cursor image for pointed data
                    if (isPointedByte)
                    {
                        static const QImage ptrIcon(QStringLiteral(":/images/pointer.png"));

                        if (!isSelectedByte && !isHighlightedByte && !isChangedByte)
                            c = _brushPointed.color();

                        if (!isSelectedByte)
                            painter.setPen(_penPointed);

                        painter.drawImage(pxPosX - _pxCharWidth - 2, pxPosY - (_pxCharHeight / 2), ptrIcon, 0, 0, 10, 10);
                    }

                    if (isPointerByte && !isSelectedByte && !isHighlightedByte && !isChangedByte && !isPointedByte)
                        painter.setPen(_penPointers);
                    
                    if (isPointerByte)
                    {
                        // Draw pointer frame only at the first byte of the pointer on each row
                        // The frame is clipped to the current line so it doesn't bleed into ASCII area
                        const int colInLine = colIdx;

                        // We draw a partial frame segment on every row that the pointer occupies
                        // Determine how many bytes of this pointer are on the current row starting from colIdx
                        const int ptrEndByteExcl = static_cast<int>((pointerStart - _bPosFirst) + actualPtrSize);

                        if (posBa == pointerStart || colInLine == 0)
                        {
                            // First pointer byte on this row — draw frame segment
                            const int bytesOnThisRow = qMin(ptrEndByteExcl - static_cast<int>(bPosLine + colIdx), bytesThisRow - colInLine);

                            if (bytesOnThisRow > 0)
                            {
                                QPen pen;
                                pen.setColor(_pointerFrameColor);
                                pen.setWidth(1);
                                painter.setPen(pen);

                                auto frame = QRect(pxPosX - 6, pxPosY - _pxCharHeight + _pxSelectionSub + 1,
                                                    (3 * bytesOnThisRow) * _pxCharWidth + (bytesOnThisRow - 1) * kHexColumnExtraGapPx,
                                                    _pxCharHeight - _pxSelectionSub + 4);

                                painter.drawRect(frame);
                                painter.fillRect(frame, _pointerFrameBackgroundColor);

                                if (_asciiArea)
                                {
                                    const int asciiStartX = pxPosAsciiX2;
                                    int asciiFrameWidth = 0;
                                    for (int k = 0; k < bytesOnThisRow; ++k)
                                    {
                                        const qint64 rowBytePos = bPosLine + colIdx + k;
                                        if (rowBytePos >= _dataShown.size())
                                            break;

                                        const uint8_t rowByte = static_cast<uint8_t>(_dataShown.at(rowBytePos));
                                        const int baseW = (_tb && !_tbSymbolWidthPxCache.isEmpty())
                                            ? _tbSymbolWidthPxCache[rowByte]
                                            : _pxCharWidth;
                                        const int slotW = baseW + slotGapPx(baseW);
                                        asciiFrameWidth += slotW;
                                    }

                                    if (asciiFrameWidth > 0)
                                    {
                                        auto asciiFrame = QRect(asciiStartX - 4,
                                                                pxPosY - _pxCharHeight + _pxSelectionSub + 2,
                                                                asciiFrameWidth + 2,
                                                                _pxCharHeight - _pxSelectionSub + 4);
                                        painter.drawRect(asciiFrame);
                                        painter.fillRect(asciiFrame, _pointerFrameBackgroundColor);
                                    }
                                }
                            }
                        }
                    }
                }

                // Pointer arrow for pointed bytes in disasm mode (function entry points)
                if (_showPointers && rowUsesDisasm && isPointedByte)
                {
                    static const QImage ptrIcon(QStringLiteral(":/images/pointer.png"));

                    if (!isSelectedByte && !isHighlightedByte && !isChangedByte)
                        c = _brushPointed.color();

                    if (!isSelectedByte)
                        painter.setPen(_penPointed);

                    painter.drawImage(pxPosX - _pxCharWidth - 2, pxPosY - (_pxCharHeight / 2), ptrIcon, 0, 0, 10, 10);
                }

                // render hex value
                auto r = QRect(pxPosX - 1, pxPosY - _pxCharHeight + _pxSelectionSub, 2 * _pxCharWidth + 2, _pxCharHeight + 1);

                // Only fill background if there's actual highlighting/selection (not just base color)
                if (c != viewport()->palette().color(QPalette::Base))
                    painter.fillRect(r, c);

                // Overlay cursor-char highlight: single-byte cursor fills here; multi-byte handled below.
                const bool isCursorByte = (bPosLine + colIdx) == (_cursorPosition / 2 - _bPosFirst);
                const qint64 byteInBuf = bPosLine + colIdx;
                const bool isCursorGroupByte = (byteInBuf >= cursorLeadBufIdx)
                                            && (byteInBuf < cursorLeadBufIdx + cursorMultiByteSpan);

                if (cursorMultiByteSpan == 1 && isCursorByte && _cursorCharColor.alpha() > 0 && !_showOriginal)
                    painter.fillRect(r, _cursorCharColor);

                hex = _hexDataShown.mid((bPosLine + colIdx) * 2, 2);

                // In hex area: draw the active nibble of the cursor byte in bold
                if (isCursorByte && !_editAreaIsAscii && !_showOriginal)
                {
                    const int activeNibble = _cursorPosition % 2; // 0 = high, 1 = low
                    const QString ch0 = (hexCaps() ? hex.toUpper() : hex).mid(0, 1);
                    const QString ch1 = (hexCaps() ? hex.toUpper() : hex).mid(1, 1);

                    QFont boldFont = painter.font();
                    boldFont.setBold(true);
                    QFont normalFont = painter.font();

                    if (activeNibble == 0)
                    {
                        painter.setFont(boldFont);
                        painter.drawText(pxPosX, pxPosY, ch0);
                        painter.setFont(normalFont);
                        painter.drawText(pxPosX + _pxCharWidth, pxPosY, ch1);
                    }
                    else
                    {
                        painter.drawText(pxPosX, pxPosY, ch0);
                        painter.setFont(boldFont);
                        painter.drawText(pxPosX + _pxCharWidth, pxPosY, ch1);
                        painter.setFont(normalFont);
                    }
                }
                else
                {
                    painter.drawText(pxPosX, pxPosY, hexCaps() ? hex.toUpper() : hex);
                }
                // Multi-byte TBL entry frame in hex area; draw per-row segment so wrapped entries are framed too.
                if (useTbMultiByte && _showMultibyteFrame)
                {
                    const qint64 globalIdx = bPosLine + colIdx;
                    if (globalIdx < _tbDisplayChars.size())
                    {
                        qint64 leadIdx = globalIdx;
                        while (leadIdx > 0
                               && leadIdx < _tbDisplayChars.size()
                               && _tbDisplayChars[(int)leadIdx].isNull())
                            --leadIdx;

                        if (leadIdx >= 0 && leadIdx < _tbDisplaySpan.size())
                        {
                            const int span = _tbDisplaySpan[(int)leadIdx];
                            if (span > 1)
                            {
                                const qint64 entryEnd = leadIdx + span;
                                const qint64 segmentStart = qMax(leadIdx, rowStart);
                                const qint64 segmentEnd = qMin(entryEnd, rowEnd);
                                const int bytesOnThisRow = (int)qMax<qint64>(0, segmentEnd - segmentStart);
                                if (bytesOnThisRow > 0 && globalIdx == segmentStart)
                                {
                                    const int fW = (bytesOnThisRow - 1) * hexStridePx + 2 * _pxCharWidth + 2;
                                    const int x = pxPosX - 1;
                                    const int y = pxPosY - _pxCharHeight + _pxSelectionSub;
                                    const int h = _pxCharHeight + 1;

                                    const bool drawLeft = (segmentStart == leadIdx);
                                    const bool drawRight = (segmentEnd == entryEnd);

                                    QPen fPen(_multibyteFrameColor, 1, Qt::DashLine);
                                    const QPen savedPen = painter.pen();
                                    painter.setPen(fPen);
                                    painter.drawLine(x, y, x + fW - 1, y);
                                    painter.drawLine(x, y + h, x + fW - 1, y + h);
                                    if (drawLeft)
                                        painter.drawLine(x, y, x, y + h);
                                    if (drawRight)
                                        painter.drawLine(x + fW - 1, y, x + fW - 1, y + h);
                                    painter.setPen(savedPen);
                                }
                            }
                        }
                    }
                }
                // Multi-byte encoding frame in hex area (analogous to TBL multi-byte frame)
                else if (useEncodingDecoder && _showMultibyteFrame)
                {
                    const qint64 globalIdx = bPosLine + colIdx;
                    if (globalIdx < _encodingChars.size())
                    {
                        qint64 leadIdx = globalIdx;
                        while (leadIdx > 0
                               && leadIdx < _encodingChars.size()
                               && _encodingChars[(int)leadIdx].isNull())
                            --leadIdx;

                        if (leadIdx >= 0 && leadIdx < _encodingSpan.size())
                        {
                            const int span = _encodingSpan[(int)leadIdx];
                            if (span > 1)
                            {
                                const qint64 entryEnd = leadIdx + span;
                                const qint64 segmentStart = qMax(leadIdx, rowStart);
                                const qint64 segmentEnd = qMin(entryEnd, rowEnd);
                                const int bytesOnThisRow = (int)qMax<qint64>(0, segmentEnd - segmentStart);
                                if (bytesOnThisRow > 0 && globalIdx == segmentStart)
                                {
                                    const int fW = (bytesOnThisRow - 1) * hexStridePx + 2 * _pxCharWidth + 2;
                                    const int x = pxPosX - 1;
                                    const int y = pxPosY - _pxCharHeight + _pxSelectionSub;
                                    const int h = _pxCharHeight + 1;

                                    const bool drawLeft = (segmentStart == leadIdx);
                                    const bool drawRight = (segmentEnd == entryEnd);

                                    QPen fPen(_multibyteFrameColor, 1, Qt::DashLine);
                                    const QPen savedPen = painter.pen();
                                    painter.setPen(fPen);
                                    painter.drawLine(x, y, x + fW - 1, y);
                                    painter.drawLine(x, y + h, x + fW - 1, y + h);
                                    if (drawLeft)
                                        painter.drawLine(x, y, x, y + h);
                                    if (drawRight)
                                        painter.drawLine(x + fW - 1, y, x + fW - 1, y + h);
                                    painter.setPen(savedPen);
                                }
                            }
                        }
                    }
                }

                // Disassembly instruction frame in hex area — disabled.
                // Disasm rows already have mnemonic text in the ASCII area;
                // the dashed frame adds visual noise without benefit.
                // Multi-byte table/encoding frames are handled separately above.

                // Multi-byte cursor group: unconditional fill + frame for this row segment.
                // Uses per-byte walkback identical to the TBL/encoding frame blocks above.
                // Runs regardless of _showMultibyteFrame so the cursor is always visible.
                if (cursorMultiByteSpan > 1)
                {
                    const qint64 globalIdx = bPosLine + colIdx;
                    qint64 leadIdx = cursorLeadBufIdx; // already pre-computed
                    const int entryEnd = (int)(leadIdx + cursorMultiByteSpan);
                    if (globalIdx >= leadIdx && globalIdx < entryEnd)
                    {
                        const qint64 segmentStart = qMax(leadIdx, rowStart);
                        const qint64 segmentEnd   = qMin((qint64)entryEnd, rowEnd);
                        const int bytesOnThisRow   = (int)qMax<qint64>(0, segmentEnd - segmentStart);
                        if (bytesOnThisRow > 0 && globalIdx == segmentStart)
                        {
                            const int fW = (bytesOnThisRow - 1) * hexStridePx + 2 * _pxCharWidth + 2;
                            const int fx = pxPosX - 1;
                            const int fy = pxPosY - _pxCharHeight + _pxSelectionSub;
                            const int fh = _pxCharHeight + 1;
                            const bool drawLeft  = (segmentStart == leadIdx);
                            const bool drawRight = (segmentEnd == (qint64)entryEnd);

                            // Wide fill covering all segment bytes on this row
                            if (_cursorCharColor.alpha() > 0 && !_showOriginal)
                            {
                                painter.fillRect(QRect(fx, fy, fW, fh), _cursorCharColor);
                                // Redraw the hex text of all bytes in the segment (fill covered them)
                                for (int k = 0; k < bytesOnThisRow; ++k)
                                {
                                    const qint64 kBufIdx = segmentStart + k;
                                    const QByteArray kHex = _hexDataShown.mid((int)kBufIdx * 2, 2);
                                    const int kX = pxPosX + k * hexStridePx;
                                    if ((kBufIdx + _bPosFirst) == (qint64)_bPosCurrent && !_editAreaIsAscii)
                                    {
                                        const int activeNibble = _cursorPosition % 2;
                                        const QString ch0 = hexCaps() ? kHex.mid(0,1).toUpper() : QString(kHex.mid(0,1));
                                        const QString ch1 = hexCaps() ? kHex.mid(1,1).toUpper() : QString(kHex.mid(1,1));
                                        QFont boldFont = painter.font(); boldFont.setBold(true);
                                        const QFont normalFont = painter.font();
                                        if (activeNibble == 0) {
                                            painter.setFont(boldFont); painter.drawText(kX, pxPosY, ch0);
                                            painter.setFont(normalFont); painter.drawText(kX + _pxCharWidth, pxPosY, ch1);
                                        } else {
                                            painter.drawText(kX, pxPosY, ch0);
                                            painter.setFont(boldFont); painter.drawText(kX + _pxCharWidth, pxPosY, ch1);
                                            painter.setFont(normalFont);
                                        }
                                    }
                                    else
                                    {
                                        painter.drawText(kX, pxPosY, hexCaps() ? kHex.toUpper() : QString(kHex));
                                    }
                                }
                            }
                            // Draw cursor-color solid frame around this segment (pen width 2 to match single-byte cursor)
                            if (!_readOnly)
                            {
                                const QPen savedPen = painter.pen();
                                painter.setPen(QPen(_cursorFrameColor, 2, Qt::SolidLine));
                                painter.drawLine(fx, fy, fx + fW - 1, fy);
                                painter.drawLine(fx, fy + fh, fx + fW - 1, fy + fh);
                                if (drawLeft)  painter.drawLine(fx, fy, fx, fy + fh);
                                if (drawRight) painter.drawLine(fx + fW - 1, fy, fx + fW - 1, fy + fh);
                                painter.setPen(savedPen);
                            }
                        }
                    }
                }

                pxPosX += hexStridePx;

                // render ascii value
                if (_asciiArea)
                {
                    if (c == viewport()->palette().color(QPalette::Base))
                        c = _asciiAreaColor;

                    // ── Determine effective display mode ──
                    // Per-section override: if the section has a non-Default mode, use it.
                    // Otherwise fall back to the global mode.
                    int secMode = SectionDisplay_Default;
                    if (_sectionModel)
                        secMode = _sectionModel->displayModeAtOffset(_bPosFirst + bPosLine);

                    const bool effectiveRaw = (secMode == SectionDisplay_Raw);
                    const bool effectiveDisasm = (secMode == SectionDisplay_Disasm)
                        || (secMode == SectionDisplay_Default && _showDisasm);
                    const bool effectiveGraphics = (secMode == SectionDisplay_Graphics)
                        || (secMode == SectionDisplay_Default && _showGraphicsPanel);
                    // Per-section table: 1-based index into _allTables
                    TranslationTable *secTable = nullptr;
                    if (secMode > 0 && secMode <= _allTables.size())
                        secTable = _allTables[secMode - 1];

                    if (effectiveDisasm)
                    {
                    // ── Disassembly view mode: show mnemonic + operands per row ──
                        const int symWidthPx = _pxCharWidth;
                        r.setRect(pxPosAsciiX2 - 1, pxPosY - _pxCharHeight + _pxSelectionSub + 2,
                                  qMax(1, symWidthPx), _pxCharHeight);

                        if (colIdx == 0) {
                            // Draw instruction mnemonic + operands once for the whole row
                            const qint64 rowFileOffset = _bPosFirst + bPosLine;
                            const DisasmInstruction *instr = disasmInstructionAtOffset(rowFileOffset);
                            if (instr) {
                                const qint64 selBegin = getSelectionBegin();
                                const qint64 selEnd   = getSelectionEnd();
                                const bool hasSel = (selEnd - selBegin) > 0;

                                const qint64 instrStart  = instr->fileOffset;
                                const qint64 instrEnd    = instrStart + instr->size;

                                // Any byte of the instruction selected → highlight whole row
                                const bool instrSel = hasSel && selBegin < instrEnd && selEnd > instrStart;

                                const int baseY = pxPosY - _pxCharHeight + _pxSelectionSub + 2;
                                int textX = pxPosAsciiX2 - 1;

                                // ── Draw mnemonic ──
                                const int mnW = paintFm.horizontalAdvance(instr->mnemonic);
                                QRect mnRect(textX, baseY, mnW, _pxCharHeight);
                                if (instrSel) {
                                    painter.fillRect(mnRect, _brushSelection.color());
                                    painter.setPen(_penSelection);
                                } else {
                                    if (c != _asciiAreaColor)
                                        painter.fillRect(mnRect, c);
                                    painter.setPen(QPen(instr->isBranch
                                        ? palette().color(QPalette::Link) : _asciiFontColor));
                                }
                                painter.drawText(mnRect, Qt::AlignLeft | Qt::AlignVCenter, instr->mnemonic);
                                textX += mnW;

                                // ── Draw space + operands ──
                                if (!instr->operands.isEmpty()) {
                                    const int spW = paintFm.horizontalAdvance(QLatin1Char(' '));
                                    QRect spRect(textX, baseY, spW, _pxCharHeight);
                                    if (instrSel) {
                                        painter.fillRect(spRect, _brushSelection.color());
                                    } else if (c != _asciiAreaColor) {
                                        painter.fillRect(spRect, c);
                                    }
                                    painter.setPen(instrSel ? _penSelection : QPen(_asciiFontColor));
                                    painter.drawText(spRect, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral(" "));
                                    textX += spW;

                                    // Show section name for branch/call targets when available.
                                    QString displayOps = instr->operands;
                                    const bool clickableBranch = instr->isBranch
                                        && instr->branchTarget >= 0
                                        && instr->branchTarget < _chunks->size();
                                    bool usingSectionLabel = false;
                                    if (clickableBranch && _sectionModel) {
                                        const QString label = _sectionModel->sectionNameAtStartOffset(instr->branchTarget);
                                        if (!label.isEmpty()) {
                                            displayOps = label;
                                            usingSectionLabel = true;
                                        }
                                    }

                                    const QVector<DisasmOperandLink> pointerLinks =
                                        (!instrSel && !usingSectionLabel)
                                            ? disasmPointerLinksForOperands(displayOps, &_pointers, _chunks->size())
                                            : QVector<DisasmOperandLink>();

                                    if (instrSel || (clickableBranch && !usingSectionLabel) || pointerLinks.isEmpty()) {
                                        const int opW = paintFm.horizontalAdvance(displayOps);
                                        QRect opRect(textX, baseY, opW, _pxCharHeight);
                                        if (instrSel) {
                                            painter.fillRect(opRect, _brushSelection.color());
                                            painter.setPen(_penSelection);
                                        } else {
                                            if (c != _asciiAreaColor)
                                                painter.fillRect(opRect, c);
                                            if (usingSectionLabel)
                                                painter.fillRect(opRect, _brushPointers.color());
                                            painter.setPen(QPen(instr->isBranch
                                                ? palette().color(QPalette::Link) : _asciiFontColor));
                                        }
                                        if (clickableBranch && !instrSel) {
                                            QFont uf = painter.font();
                                            uf.setUnderline(true);
                                            painter.setFont(uf);
                                        }
                                        painter.drawText(opRect, Qt::AlignLeft | Qt::AlignVCenter, displayOps);
                                        if (clickableBranch && !instrSel) {
                                            QFont uf = painter.font();
                                            uf.setUnderline(false);
                                            painter.setFont(uf);
                                        }
                                    } else {
                                        int drawPos = 0;
                                        int segX = textX;
                                        const QPen savedPen = painter.pen();
                                        const QFont savedFont = painter.font();

                                        auto drawOpsSegment = [&](int start, int len, bool linked) {
                                            if (len <= 0)
                                                return;
                                            const QString seg = displayOps.mid(start, len);
                                            const int segW = paintFm.horizontalAdvance(seg);
                                            QRect segRect(segX, baseY, segW, _pxCharHeight);

                                            if (c != _asciiAreaColor)
                                                painter.fillRect(segRect, c);
                                            if (linked)
                                                painter.fillRect(segRect, _brushPointers.color());

                                            painter.setPen(linked ? QPen(palette().color(QPalette::Link))
                                                                  : QPen(_asciiFontColor));
                                            QFont f = savedFont;
                                            f.setUnderline(linked);
                                            painter.setFont(f);
                                            painter.drawText(segRect, Qt::AlignLeft | Qt::AlignVCenter, seg);
                                            segX += segW;
                                        };

                                        for (const auto &link : pointerLinks) {
                                            if (link.start > drawPos)
                                                drawOpsSegment(drawPos, link.start - drawPos, false);
                                            drawOpsSegment(link.start, link.length, true);
                                            drawPos = link.start + link.length;
                                        }
                                        if (drawPos < displayOps.size())
                                            drawOpsSegment(drawPos, displayOps.size() - drawPos, false);

                                        painter.setPen(savedPen);
                                        painter.setFont(savedFont);
                                    }
                                }
                            }
                        }

                        if ((bPosLine + colIdx) == cursorLeadBufIdx)
                            _asciiCursorRect = QRect(r.x() - 1, r.y() - 2, _pxCharWidth + 2, r.height() + 2);

                        pxPosAsciiX2 += symWidthPx;
                    }
                    else if (secMode == SectionDisplay_Audio)
                    {
                    // ── Audio waveform: monolithic full-width vertical display ──
                    // The entire ASCII area is one waveform strip.  Time flows
                    // top→bottom; each byte maps to a sub-row.  Amplitude maps
                    // left→right across the full ASCII-area width.
                    const uint8_t byteVal = static_cast<uint8_t>(rawByte);
                    const int cellTop = pxPosY - _pxCharHeight + _pxSelectionSub + 2;
                    const int cellH = _pxCharHeight;
                    const int waveLeft = _pxPosAsciiX + kAsciiAreaLeftPaddingPx - pxOfsX;
                    const int waveW = bytesThisRow * _pxCharWidth;
                    const int effN = qMax(1, bytesThisRow);

                    // Sub-row for this byte within the visual row
                    const int subY = cellTop + (colIdx * cellH) / effN;
                    const int nextSubY = cellTop + ((colIdx + 1) * cellH) / effN;
                    const int subH = qMax(1, nextSubY - subY);

                    // On first audio column: fill entire waveform background
                    if (colIdx == 0)
                        painter.fillRect(waveLeft, cellTop, waveW, cellH, _asciiAreaColor);

                    // Per-byte selection/section highlight on its sub-row
                    r.setRect(waveLeft, subY, waveW, subH);
                    if (isSelectedByte)
                        painter.fillRect(r, _brushSelection.color());
                    else if (c != _asciiAreaColor)
                        painter.fillRect(r, c);

                    // Waveform color
                    QColor waveColor;
                    if (isSelectedByte)
                        waveColor = _penSelection.color();
                    else if (isHighlightedByte)
                        waveColor = _penHighlighted.color();
                    else
                        waveColor = QColor(0x40, 0xA0, 0xFF);

                    // Map amplitude across full waveform width
                    const int midX = waveLeft + waveW / 2;
                    const int ampX = waveLeft + (byteVal * (waveW - 1)) / 255;
                    const int subMidY = subY + subH / 2;

                    // Filled area from centre to amplitude
                    {
                        QColor fillColor = waveColor;
                        fillColor.setAlpha(80);
                        const int x1 = qMin(midX, ampX);
                        const int x2 = qMax(midX, ampX);
                        if (x2 > x1)
                            painter.fillRect(x1, subY, x2 - x1, subH, fillColor);
                    }

                    // Waveform line connecting consecutive samples
                    {
                        QPen wavePen(waveColor);
                        wavePen.setWidth(1);
                        const QPen oldPen = painter.pen();
                        painter.setPen(wavePen);
                        if (_lastAudioAmpY >= 0) {
                            const int prevMidY = subMidY - subH;
                            painter.drawLine(_lastAudioAmpY, prevMidY, ampX, subMidY);
                        }
                        painter.setPen(oldPen);
                    }
                    _lastAudioAmpY = ampX;

                    // Dotted centre line (once per row)
                    if (colIdx == 0) {
                        QPen midPen(QColor(waveColor.red(), waveColor.green(),
                                           waveColor.blue(), 30));
                        midPen.setStyle(Qt::DotLine);
                        const QPen oldPen = painter.pen();
                        painter.setPen(midPen);
                        painter.drawLine(midX, cellTop, midX, cellTop + cellH);
                        painter.setPen(oldPen);
                    }

                    // Cursor: horizontal band spanning full waveform width
                    if ((bPosLine + colIdx) == cursorLeadBufIdx)
                        _asciiCursorRect = QRect(waveLeft - 1, subY - 1, waveW + 2, subH + 2);

                    pxPosAsciiX2 += _pxCharWidth;
                    }
                    else if (effectiveGraphics)
                    {
                    // ── Tile graphics view (per-section or global) ──
                    paintGraphicsSection(painter, pxOfsX, rowStridePx,
                                         pxPosY, colIdx, bPosLine,
                                         _bPosFirst + bPosLine, bytesThisRow,
                                         isSelectedByte, isHighlightedByte,
                                         c, pxPosAsciiX2);
                    }
                    else if (secTable)
                    {
                    // ── Per-section table rendering (single-byte lookup) ──
                    const QString sym = secTable->encodeSymbol(rawByte);
                    const QString displaySym = sym.isEmpty()
                        ? QString(_notInTableChar) : sym;
                    const int baseSymWidthPx = qMax(_pxCharWidth, paintFm.horizontalAdvance(displaySym));
                    const int symWidthPx = baseSymWidthPx + slotGapPx(baseSymWidthPx);
                    r.setRect(pxPosAsciiX2 - 1, pxPosY - _pxCharHeight + _pxSelectionSub + 2,
                              qMax(1, symWidthPx), _pxCharHeight);
                    if (c != _asciiAreaColor)
                        painter.fillRect(r, c);
                    if (isSelectedByte)
                        painter.setPen(_penSelection);
                    else if (isHighlightedByte)
                        painter.setPen(_penHighlighted);
                    else {
                        const bool isUnmappedZero = isZeroByte && sym.isEmpty();
                        painter.setPen(QPen(isUnmappedZero ? _zeroByteFontColor : _asciiFontColor));
                    }
                    painter.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, displaySym);
                    if ((bPosLine + colIdx) == cursorLeadBufIdx)
                        _asciiCursorRect = QRect(r.x() - 1, r.y() - 2, baseSymWidthPx + 2, r.height() + 2);
                    pxPosAsciiX2 += symWidthPx;
                    }
                    else
                    {
                    // ── Normal ASCII rendering ──

                    QChar ch = QChar::fromLatin1(rawByte);

                    QString sym;
                    TranslationTable *activeTable = effectiveRaw ? nullptr : _tb;
                    const bool activeTbMultiByte = (activeTable != nullptr) && useTbMultiByte;
                    const bool activeEncodingDecoder = useEncodingDecoder && (effectiveRaw || activeTable == nullptr);

                    // For multi-byte table mode: continuation bytes are null entries in cache.
                    const int encBufIdx = (int)(bPosLine + colIdx);
                    const bool isTbContinuation = activeTbMultiByte
                        && encBufIdx < _tbDisplayChars.size()
                        && _tbDisplayChars[encBufIdx].isNull();

                    // For encoding mode: look up from the buffer-level cache
                    const bool isEncodingContinuation = activeEncodingDecoder
                        && encBufIdx < _encodingChars.size()
                        && _encodingChars[encBufIdx].isNull();
                    const bool isContinuationByte = isTbContinuation || isEncodingContinuation;

                    if (activeTable) {
                        if (activeTbMultiByte && encBufIdx < _tbDisplayChars.size()) {
                            sym = _tbDisplayChars[encBufIdx];
                        } else {
                            sym = activeTable->encodeSymbol(rawByte);
                        }
                    } else if (activeEncodingDecoder && encBufIdx < _encodingChars.size()) {
                        sym = _encodingChars[encBufIdx];
                    } else {
                        sym = ch;
                    }

                    if (activeTable)
                    {
                        if (!sym.size() && !isTbContinuation)
                            sym = QString(_notInTableChar); // □ for unmapped single-byte
                        // TBL continuation: leave sym null — zero-width slot, no drawing
                    }
                    else if (!isContinuationByte)
                    {
                        if (sym.isEmpty() || (sym.size() == 1 && !sym[0].isPrint()))
                            sym = QString(_nonPrintableNoTableChar);
                    }
                    // Encoding/TBL continuation: sym is null/empty and isContinuationByte=true → zero-width, no draw

                    const uint8_t byteValue = static_cast<uint8_t>(rawByte);

                    // Slot width: continuation=0; multi-byte TBL uses actual advance; single-byte TBL uses cache;
                    // encoding uses actual advance; otherwise fixed
                    int baseSymWidthPx;
                    if (isContinuationByte)
                        baseSymWidthPx = 0;
                    else if (activeTbMultiByte && !sym.isEmpty())
                        baseSymWidthPx = qMax(_pxCharWidth, paintFm.horizontalAdvance(sym));
                    else if (activeTable && !_tbSymbolWidthPxCache.isEmpty())
                        baseSymWidthPx = _tbSymbolWidthPxCache[byteValue];
                    else if (activeEncodingDecoder && !sym.isEmpty())
                        baseSymWidthPx = qMax(_pxCharWidth, paintFm.horizontalAdvance(sym));
                    else
                        baseSymWidthPx = _pxCharWidth;
                    const int symWidthPx = isContinuationByte ? 0 : (baseSymWidthPx + slotGapPx(baseSymWidthPx));

                    r.setRect(pxPosAsciiX2 - 1, pxPosY - _pxCharHeight + _pxSelectionSub + 2,
                              qMax(1, symWidthPx), _pxCharHeight);

                    if (!isContinuationByte) {
                        if (c != _asciiAreaColor)
                            painter.fillRect(r, c);

                        if (isCursorGroupByte && _cursorCharColor.alpha() > 0)
                            painter.fillRect(QRect(r.x(), r.y() - 2, baseSymWidthPx, r.height() + 2), _cursorCharColor);

                        if (isSelectedByte)
                            painter.setPen(_penSelection);
                        else if (isHighlightedByte)
                            painter.setPen(_penHighlighted);
                        else if (isPointedByte)
                            painter.setPen(_penPointed);
                        else if (isPointerByte)
                            painter.setPen(_penPointers);
                        else {
                            const bool isUnmappedZero = isZeroByte && !useTbMultiByte
                                && (!_tb || sym == QString(_notInTableChar));
                            painter.setPen(QPen(isUnmappedZero ? _zeroByteFontColor : _asciiFontColor));
                        }
                        painter.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, sym);
                    }

                    if (!isContinuationByte && (bPosLine + colIdx) == cursorLeadBufIdx)
                        _asciiCursorRect = QRect(r.x() - 1, r.y() - 2, baseSymWidthPx + 2, r.height() + 2);

                    pxPosAsciiX2 += symWidthPx;
                    } // end else (normal ASCII rendering)
                }
            }
        }

        painter.setBackgroundMode(Qt::TransparentMode);
        painter.setPen(viewport()->palette().color(QPalette::WindowText));
    }

    // Paint continuous graphics tile canvas over ASCII area (after all rows)
    if (_asciiArea) {
        const int rowStridePx = _pxCharHeight + kHexRowExtraGapPx;
        const int pxPosStartY = _pxCharHeight;
        paintGraphicsArea(painter, pxOfsX, rowStridePx, pxPosStartY);
        paintGraphicsCursor(painter, pxOfsX, rowStridePx, pxPosStartY);
    }

    // Draw hex area grid (vertical lines every 4 bytes)
    if (_showHexGrid && event->rect() != _asciiCursorRect && event->rect() != _hexCursorRect)
    {
        painter.setPen(QPen(_hexAreaGridColor, 1));

        const int rowStridePx = _pxCharHeight + kHexRowExtraGapPx;
        int pxPosStartY = _pxCharHeight;
        int pxPosEndY = pxPosStartY + (_rowsShown + 1) * rowStridePx;

        // Draw vertical grid lines every 4 bytes (between groups)
        const int hexStridePx = 3 * _pxCharWidth + kHexColumnExtraGapPx;
        for (int col = 4; col < _bytesPerLine; col += 4)
        {
            int pxPosX = _pxPosHexX + col * hexStridePx - ((_pxCharWidth + kHexColumnExtraGapPx) / 2) - pxOfsX;
            painter.drawLine(pxPosX, pxPosStartY - _pxCharHeight, pxPosX, pxPosEndY);
        }
    }

    // _cursorPosition counts in 2, _bPosFirst counts in 1
    int hexPositionInShowData = _cursorPosition - 2 * _bPosFirst;

    // due to scrolling the cursor can go out of the currently displayed data
    if ((hexPositionInShowData >= 0) && (hexPositionInShowData < _hexDataShown.size()))
    {
        // paint cursor
        if (_readOnly)
        {
            QColor color = viewport()->palette().dark().color();
            painter.fillRect(QRect(_pxCursorX - pxOfsX, _pxCursorY - _pxCharHeight + _pxSelectionSub, _pxCharWidth, _pxCharHeight), color);
        }
        else
        {
            QPen pen;
            pen.setColor(_cursorFrameColor);
            pen.setWidth(2);
            painter.setPen(pen);

            // For multi-byte cursor, the in-loop block already draws the per-row frame segments
            // (with correct open sides at row-wrap points). Only use drawRect for single-byte cursor.
            if (_cursorMultiByteSpan <= 1)
                painter.drawRect(_hexCursorRect);

            // draw cursor rect in ASCII area (skip for disasm — not per-byte text)
            if (_asciiArea && !isDisasmAt(_bPosCurrent)) {
                if (!isGraphicsAt(_bPosCurrent))
                    painter.drawRect(_asciiCursorRect);
                // Graphics cursor is drawn by paintGraphicsCursor above
            }

            painter.setPen(QPen(_hexFontColor));
        }
    }

    // emit event, if size has changed
    if (_lastEventSize != _chunks->size())
    {
        _lastEventSize = _chunks->size();
        emit currentSizeChanged(_lastEventSize);
    }

    // Visual indicator: draw an amber border around the viewport when showing original content
    if (_showOriginal)
    {
        const QRect vr = QRect(0, 0, viewport()->width(), viewport()->height());
        painter.setPen(QPen(QColor(220, 140, 0), 3, Qt::SolidLine));
        painter.drawRect(vr.adjusted(1, 1, -2, -2));
    }

}


void HexEditor::resizeEvent(QResizeEvent *)
{
    if (_dynamicBytesPerLine)
    {
        int selectedBpl = 4;
        const int viewportWidthPx = viewport()->width();

        for (int candidateBpl = 64; candidateBpl >= 4; candidateBpl -= 4)
        {
            const int addrDigits = _addressArea ? addressWidth() : 0;
            const int pxPosHexX = _addressArea
                                      ? (_pxGapAdr + addrDigits * _pxCharWidth + _pxGapAdrHex + kAddressRightPaddingPx)
                                      : _pxGapAdrHex;
            const int candidateHexCharsInLine = candidateBpl * 3 - 1;
            const int pxHexEndX = pxPosHexX + candidateHexCharsInLine * _pxCharWidth + (candidateBpl - 1) * kHexColumnExtraGapPx;
            const int pxPosAsciiX = pxHexEndX + _pxGapHexAscii;
            const int asciiWidthPx = _asciiArea ? static_cast<int>(computeAsciiAreaMaxWidthForBytesPerLine(candidateBpl)) : 0;
            const int requiredWidthPx = _asciiArea ? (pxPosAsciiX + kAsciiAreaLeftPaddingPx + asciiWidthPx) : pxHexEndX;

            if (requiredWidthPx <= viewportWidthPx)
            {
                selectedBpl = candidateBpl;
                break;
            }
        }

        if (selectedBpl != _bytesPerLine)
            setBytesPerLine(selectedBpl);
        else
            updateAsciiAreaMaxWidth();
    }

    adjust();
}


bool HexEditor::focusNextPrevChild(bool next)
{
    return QWidget::focusNextPrevChild(next);
}

// ********************************************************************** Handle selections

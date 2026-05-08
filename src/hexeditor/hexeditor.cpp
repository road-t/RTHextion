#include "internal.h"
#include "encoding.h"

// ********************************************************************** Constructor, destructor

HexEditor::HexEditor(QWidget *parent) : QAbstractScrollArea(parent), _addressArea(true), _addressWidth(4), _asciiArea(true), _bytesPerLine(32), _hexCharsInLine(47), _highlighting(true), _overwriteMode(true), _showHexGrid(true), _readOnly(false), _showPointers(true), _hexCaps(true), _dynamicBytesPerLine(true), _editAreaIsAscii(true), _chunks(new Chunks(this)), _cursorPosition(0), _lastEventSize(0), _undoStack(new UndoStack(_chunks, this))
{
    setFont(QFont("Courier New", 14));

    auto font = QFont("Courier New", 16, 10);
    QToolTip::setFont(font);

    setAddressAreaColor(this->palette().alternateBase().color());
    setHighlightingColor(QColor(0xff, 0xff, 0x99, 0xff));
    setChangesColor(QColor(0x99, 0xff, 0x99, 0xff));
    setPointersColor(QColor(0xff, 0x99, 0x00, 0xff));
    setPointedColor(QColor(0x99, 0xff, 0x00, 0xff));
    setPointerFontColor(this->palette().color(QPalette::WindowText));
    setPointedFontColor(this->palette().color(QPalette::WindowText));
    setPointerFrameColor(QColor(0, 0, 0xff));
    setPointerFrameBackgroundColor(QColor(0, 0xFF, 0, 0x80));
    setMultibyteFrameColor(QColor(0x20, 0x20, 0x20));
    setSelectionColor(this->palette().highlight().color());
    setAddressFontColor(QPalette::WindowText);
    setAsciiAreaColor(this->palette().alternateBase().color());
    setAsciiFontColor(QPalette::WindowText);
    setHexAreaBackgroundColor(Qt::white);
    setHexAreaGridColor(QColor(0x99, 0x99, 0x99));
    setShowColumnNumbers(true);
    setColumnNumbersFont(this->font());
    setColumnNumbersFontColor(this->palette().color(QPalette::WindowText));
    setColumnNumbersBackgroundColor(this->palette().alternateBase().color());
    setSectionHeaderFontColor(this->palette().color(QPalette::WindowText));
    setSectionHeaderBackgroundColor(QColor(0xD8, 0xD8, 0xD8, 0x90));
    {
        QFont secFont = this->font();
        secFont.setBold(true);
        setSectionHeaderFont(secFont);
    }
    _cursorCharColor = QColor(0x00, 0x60, 0xFF, 0x80);
    _zeroByteFontColor = QColor(0xCC, 0xCC, 0xCC);
    _addressZeroByteFontColor = QColor(0xCC, 0xCC, 0xCC);  // same default as _zeroByteFontColor

    connect(&_cursorTimer, &QTimer::timeout, this, &HexEditor::updateCursor);
    connect(verticalScrollBar(), &QAbstractSlider::valueChanged, this, &HexEditor::adjust);
    connect(horizontalScrollBar(), &QAbstractSlider::valueChanged, this, &HexEditor::adjust);
    connect(_undoStack, &QUndoStack::indexChanged, this, &HexEditor::dataChangedPrivate);

//    _cursorTimer.setInterval(500);
//    _cursorTimer.start();

    // not the best idea though
    _pointers.setHexEdit(this);

    // Scroll map — two thin strips to the right of the viewport:
    //   _scrollMapChanges (same color as edit area highlight) — changed bytes
    //   _scrollMapTarget  — pointer storage (orange, primary) + targets (sky-blue, secondary)
    _scrollMapChanges = new HexScrollMap(this);
    _scrollMapChanges->setColor(QColor(0x99, 0xff, 0x99));  // default; overridden by setChangesColor()
    _scrollMapChanges->hide();  // shown only when changes are present

    _scrollMapTarget = new HexScrollMap(this);
    _scrollMapTarget->setColor(QColor(0xff, 0x99, 0x00));           // orange  — pointer storage locations
    _scrollMapTarget->setSecondaryColor(QColor(0x40, 0xbf, 0xff));  // sky-blue — pointer target addresses
    _scrollMapTarget->hide();

    setViewportMargins(0, 0, 0, 0);  // margins set dynamically by updateScrollMapMargins()

    // Debounce timer: coalesces rapid per-pointer model signals into one batch recompute
    _scrollMapTimer = new QTimer(this);
    _scrollMapTimer->setSingleShot(true);
    _scrollMapTimer->setInterval(200);  // ms — waits for pointer search to complete
    connect(_scrollMapTimer, &QTimer::timeout,
            this, &HexEditor::scheduleScrollMapCompute);

    // Background computation result handler
    _scrollMapWatcher = new QFutureWatcher<ScrollMapMarkers>(this);
    connect(_scrollMapWatcher, &QFutureWatcher<ScrollMapMarkers>::finished,
            this, [this]() {
        if (_scrollMapWatcher->isCanceled()) return;
        const auto &r = _scrollMapWatcher->result();
        if (_scrollMapChanges) {
            _scrollMapChanges->setTickOffsets(r.changesYToOff);
            _scrollMapChanges->setTicks(r.changesYs);
        }
        if (_scrollMapTarget) {
            _scrollMapTarget->setTickOffsets(r.pointerYToOff);
            _scrollMapTarget->setTicks(r.pointerYs);
            _scrollMapTarget->setSecondaryTickOffsets(r.targetYToOff);
            _scrollMapTarget->setSecondaryTicks(r.targetYs);
        }
    });

    // Recompute when either strip height changes (Y-mapping depends on height).
    // Goes through debounce timer to avoid rapid recomputes during window resize.
    connect(_scrollMapChanges, &HexScrollMap::heightChanged, this, &HexEditor::updateScrollMap);
    connect(_scrollMapTarget,  &HexScrollMap::heightChanged, this, &HexEditor::updateScrollMap);

    // Click on a tick → jump to that exact byte offset (centered in viewport)
    auto scrollMapJump = [this](qint64 off) {
        const int bpl = qMax(1, _bytesPerLine);
        const int targetLine = static_cast<int>(off / bpl);
        const int targetValue = qBound(0,
                                       targetLine - verticalScrollBar()->pageStep() / 2,
                                       verticalScrollBar()->maximum());
        verticalScrollBar()->setValue(targetValue);
        setCursorPosition(off * 2);
        resetSelection(off * 2);
        ensureVisible();
    };
    connect(_scrollMapChanges,  &HexScrollMap::tickClicked, this, scrollMapJump);
    connect(_scrollMapTarget,   &HexScrollMap::tickClicked, this, scrollMapJump);

    // Kick debounce timer on any pointer model change
    auto onModelChanged = [this]() { updateScrollMap(); };
    connect(&_pointers, &QAbstractTableModel::modelReset,   this, onModelChanged);
    connect(&_pointers, &QAbstractTableModel::rowsInserted, this, onModelChanged);
    connect(&_pointers, &QAbstractTableModel::rowsRemoved,  this, onModelChanged);
    connect(&_pointers, &QAbstractTableModel::dataChanged,  this, onModelChanged);
    connect(&_pointers, &PointerListModel::pointersChanged, this, onModelChanged);

    // Enable mouse tracking to get mouseMoveEvent() on hover (not just on drag)
    viewport()->setMouseTracking(true);

    init();
}

HexEditor::~HexEditor()
{
    // Disconnect before _undoStack is destroyed — its destruction clears
    // commands, emitting indexChanged, which would call dataChangedPrivate()
    // → adjust() on a partially destroyed widget, crashing in QRect.
    disconnect(_undoStack, nullptr, this, nullptr);
}

// ********************************************************************** Properties

void HexEditor::setAddressArea(bool addressArea)
{
    _addressArea = addressArea;
    
    if (_dynamicBytesPerLine)
        resizeEvent(nullptr);
    else
        adjust();
    
    setCursorPosition(_cursorPosition);
    viewport()->update();
}


bool HexEditor::addressArea()
{
    return _addressArea;
}


void HexEditor::setAddressAreaColor(const QColor &color)
{
    _addressAreaColor = color;
    viewport()->update();
}


QColor HexEditor::addressAreaColor()
{
    return _addressAreaColor;
}


void HexEditor::setAddressFontColor(const QColor &color)
{
    _addressFontColor = color;
    viewport()->update();
}


QColor HexEditor::addressFontColor()
{
    return _addressFontColor;
}


void HexEditor::setAsciiAreaColor(const QColor &color)
{
    _asciiAreaColor = color;
    viewport()->update();
}


QColor HexEditor::asciiAreaColor()
{
    return _asciiAreaColor;
}


void HexEditor::setAsciiFontColor(const QColor &color)
{
    _asciiFontColor = color;
    viewport()->update();
}


QColor HexEditor::asciiFontColor()
{
    return _asciiFontColor;
}


QChar HexEditor::nonPrintableNoTableChar() const
{
    return _nonPrintableNoTableChar;
}


void HexEditor::setNonPrintableNoTableChar(const QChar &ch)
{
    _nonPrintableNoTableChar = ch.isNull() ? QChar(0x25AA) : ch;
    invalidateAsciiAreaWidthCache();
    updateAsciiAreaMaxWidth();
    viewport()->update();
}


QChar HexEditor::notInTableChar() const
{
    return _notInTableChar;
}


void HexEditor::setNotInTableChar(const QChar &ch)
{
    _notInTableChar = ch.isNull() ? QChar(0x25A1) : ch;
    invalidateAsciiAreaWidthCache();
    updateAsciiAreaMaxWidth();
    viewport()->update();
}


QColor HexEditor::cursorCharColor()
{
    return _cursorCharColor;
}


void HexEditor::setCursorCharColor(const QColor &color)
{
    _cursorCharColor = color;
    viewport()->update();
}


QColor HexEditor::cursorFrameColor()
{
    return _cursorFrameColor;
}


void HexEditor::setCursorFrameColor(const QColor &color)
{
    _cursorFrameColor = color;
    viewport()->update();
}


void HexEditor::setHexFontColor(const QColor &color)
{
    _hexFontColor = color;
    viewport()->update();
}


QColor HexEditor::hexFontColor()
{
    return _hexFontColor;
}


QColor HexEditor::zeroByteFontColor()
{
    return _zeroByteFontColor;
}


void HexEditor::setZeroByteFontColor(const QColor &color)
{
    _zeroByteFontColor = color;
    viewport()->update();
}


QColor HexEditor::addressZeroByteFontColor()
{
    return _addressZeroByteFontColor;
}


void HexEditor::setAddressZeroByteFontColor(const QColor &color)
{
    _addressZeroByteFontColor = color;
    viewport()->update();
}


void HexEditor::setAddressOffset(qint64 addressOffset)
{
    _addressOffset = addressOffset;
    adjust();
    setCursorPosition(_cursorPosition);
    viewport()->update();
}


qint64 HexEditor::addressOffset()
{
    return _addressOffset;
}


void HexEditor::setAddressWidth(int addressWidth)
{
    _addressWidth = addressWidth;
    adjust();
    setCursorPosition(_cursorPosition);
    viewport()->update();
}


int HexEditor::addressWidth()
{
    // Compute the minimum number of hex digits needed to represent the largest address
    qint64 size = _chunks->size();
    int n = 1;
    while (size >= 0x10)
    {
        size >>= 4;
        ++n;
    }
    return qMax(n, _addressWidth);
}


void HexEditor::setAsciiArea(bool asciiArea)
{
    if (!asciiArea)
        _editAreaIsAscii = false;
    _asciiArea = asciiArea;
    invalidateAsciiAreaWidthCache();
    updateAsciiAreaMaxWidth();
    
    if (_dynamicBytesPerLine)
        resizeEvent(nullptr);
    else
        adjust();
    
    setCursorPosition(_cursorPosition);
    viewport()->update();
}


bool HexEditor::asciiArea()
{
    return _asciiArea;
}


void HexEditor::setBytesPerLine(int count)
{
    _bytesPerLine = count;
    _hexCharsInLine = count * 3 - 1;
    updateAsciiAreaMaxWidth();

    if (_sectionModel && _sectionModel->count() > 0)
        rebuildSectionAwareLayout();
    else
        adjust();

    setCursorPosition(_cursorPosition);
    viewport()->update();
}


int HexEditor::bytesPerLine()
{
    return _bytesPerLine;
}


void HexEditor::setCursorPosition(qint64 position)
{
    // 1. Check, if cursor in range?
    if (position > (_chunks->size() * 2 - 1))
        position = _chunks->size() * 2 - (_overwriteMode ? 1 : 0);

    if (position < 0)
        position = 0;

    // 2. Calc new position of cursor
    _bPosCurrent = position / 2;

    // Absolute visual row of the cursor (whole-file row number)
    const qint64 absVisRow = visualRowForByte(_bPosCurrent);
    // Absolute first byte of the cursor's visual row (for byteInLine / column)
    const qint64 absRowStart = byteOffsetForVisualRow(absVisRow);
    int byteInLine = static_cast<int>(_bPosCurrent - absRowStart);

    // Screen row = absVisRow minus the top visible row (may be negative or >= _rowsShown)
    const qint64 topRow = static_cast<qint64>(verticalScrollBar()->value());
    int visRow = static_cast<int>(absVisRow - topRow);
    auto line = visRow + 1;
    const int rowStridePx = _pxCharHeight + kHexRowExtraGapPx;
    _pxCursorY = _pxColumnNumbersHeight + line * rowStridePx;
    auto x = byteInLine * 2 + static_cast<int>(position % 2);

    _cursorPosition = position;

    // ascii area cursor
    int asciiOffsetPx = 0;
    int asciiCursorWidthPx = _pxCharWidth;
    const int cursorSectionMode = _sectionModel
        ? _sectionModel->displayModeAtOffset(_bPosCurrent)
        : SectionDisplay_Default;
    const bool cursorForcesRaw = (cursorSectionMode == SectionDisplay_Raw);
    // Buffer offset of cursor's row start (may be negative when cursor is above visible area)
    const int bufRowStart = static_cast<int>(absRowStart - _bPosFirst);

    if (_asciiArea)
    {
        ensureTableDisplayCache();
        ensureEncodingDisplayCache();

        const auto slotGapPx = [this](int baseWidth) {
            return (baseWidth > _pxCharWidth) ? kAsciiColumnGapWidePx : kAsciiColumnGapSinglePx;
        };

        asciiCursorWidthPx = _pxCharWidth + 2;
        asciiOffsetPx = 0;

        if (!cursorForcesRaw && _tb && !_tbDisplayChars.isEmpty())
        {
            const QFontMetrics fm(font());
            int lastLeadOffsetPx = 0;
            int lastLeadWidth = _pxCharWidth + 2;

            for (int col = 0; col < byteInLine; ++col)
            {
                const int idx = bufRowStart + col;
                if (idx < 0 || idx >= _tbDisplayChars.size()) break;
                if (_tbDisplayChars[idx].isNull()) continue;

                lastLeadOffsetPx = asciiOffsetPx;
                const int baseW = qMax(_pxCharWidth, fm.horizontalAdvance(_tbDisplayChars[idx]));
                const int w = baseW + slotGapPx(baseW);
                lastLeadWidth = baseW + 2;
                asciiOffsetPx += w;
            }

            const int curIdx = bufRowStart + byteInLine;
            if (curIdx >= 0 && curIdx < _tbDisplayChars.size())
            {
                if (_tbDisplayChars[curIdx].isNull())
                {
                    asciiOffsetPx = lastLeadOffsetPx;
                    asciiCursorWidthPx = lastLeadWidth;
                }
                else
                {
                    const int baseW = qMax(_pxCharWidth, fm.horizontalAdvance(_tbDisplayChars[curIdx]));
                    asciiCursorWidthPx = baseW + 2;
                }
            }
        }
        else if (!_encodingChars.isEmpty())
        {
            const QFontMetrics fm(font());
            int lastLeadOffsetPx = 0;
            int lastLeadWidth = _pxCharWidth + 2;

            for (int col = 0; col < byteInLine; ++col)
            {
                const int idx = bufRowStart + col;
                if (idx < 0 || idx >= _encodingChars.size()) break;
                if (_encodingChars[idx].isNull()) continue;
                lastLeadOffsetPx = asciiOffsetPx;
                const int baseW = qMax(_pxCharWidth, fm.horizontalAdvance(_encodingChars[idx]));
                const int w = baseW + slotGapPx(baseW);
                lastLeadWidth = baseW + 2;
                asciiOffsetPx += w;
            }

            const int curIdx = bufRowStart + byteInLine;
            if (curIdx >= 0 && curIdx < _encodingChars.size())
            {
                if (_encodingChars[curIdx].isNull())
                {
                    asciiOffsetPx = lastLeadOffsetPx;
                    asciiCursorWidthPx = lastLeadWidth;
                }
                else
                {
                    const int baseW = qMax(_pxCharWidth, fm.horizontalAdvance(_encodingChars[curIdx]));
                    asciiCursorWidthPx = baseW + 2;
                }
            }
        }
        else
        {
            for (int col = 0; col < byteInLine; ++col)
            {
                const qint64 bytePos = bufRowStart + col;
                const uint8_t bv = (bytePos >= 0 && bytePos < _dataShown.size())
                    ? static_cast<uint8_t>(_dataShown.at(bytePos))
                    : 0;
                const int baseW = (!cursorForcesRaw && _tb && !_tbSymbolWidthPxCache.isEmpty()) ? _tbSymbolWidthPxCache[bv] : _pxCharWidth;
                asciiOffsetPx += baseW + slotGapPx(baseW);
            }

            const qint64 curBytePos = bufRowStart + byteInLine;
            const uint8_t curBv = (curBytePos >= 0 && curBytePos < _dataShown.size())
                ? static_cast<uint8_t>(_dataShown.at(curBytePos))
                : 0;
            const int baseW = (!cursorForcesRaw && _tb && !_tbSymbolWidthPxCache.isEmpty()) ? _tbSymbolWidthPxCache[curBv] : _pxCharWidth;
            asciiCursorWidthPx = baseW + 2;
        }
    }

    // Save old rects so both old and new positions get repainted (clears stale cursor artifacts)
    const QRect oldAsciiCursorRect = _asciiCursorRect;
    const QRect oldHexCursorRect   = _hexCursorRect;

    _pxCursorX = _pxPosAsciiX + kAsciiAreaLeftPaddingPx + asciiOffsetPx;

    _asciiCursorRect = QRect(_pxCursorX - horizontalScrollBar()->value() - 2, _pxCursorY - _pxCharHeight + _pxSelectionSub - 4, asciiCursorWidthPx, _pxCharHeight + 2);

    // hex area cursor
    const int hexStridePx = 3 * _pxCharWidth + kHexColumnExtraGapPx;
    _pxCursorX = (x / 2) * hexStridePx + _pxPosHexX;

    {
        const int scrollX = horizontalScrollBar()->value();
        const int bufIdx  = (int)(_bPosCurrent - _bPosFirst);
        int leadBufIdx = bufIdx;
        int span = 1;
        if (!cursorForcesRaw && !_tbDisplayChars.isEmpty() && bufIdx >= 0 && bufIdx < _tbDisplayChars.size()) {
            int li = bufIdx;
            while (li > 0 && _tbDisplayChars[li].isNull()) --li;
            if (li >= 0 && li < _tbDisplaySpan.size() && _tbDisplaySpan[li] > 1)
                { leadBufIdx = li; span = _tbDisplaySpan[li]; }
        } else if (!_encodingChars.isEmpty() && bufIdx >= 0 && bufIdx < _encodingChars.size()) {
            int li = bufIdx;
            while (li > 0 && _encodingChars[li].isNull()) --li;
            if (li >= 0 && li < _encodingSpan.size() && _encodingSpan[li] > 1)
                { leadBufIdx = li; span = _encodingSpan[li]; }
        }
        int hexCursorWidthPx = _pxCharWidth * 2 + 4;
        int hexCursorStartPx = _pxCursorX;
        if (span > 1 && _bytesPerLine > 0) {
            const int curRowStart = bufRowStart;
            const int rowBytes = bytesOnVisualRowAt(absRowStart);
            const int segStart    = qMax(leadBufIdx, curRowStart);
            const int segEnd      = qMin(leadBufIdx + span, curRowStart + rowBytes);
            const int bytesOnRow  = segEnd - segStart;
            if (bytesOnRow > 0) {
                hexCursorWidthPx = (bytesOnRow - 1) * hexStridePx + _pxCharWidth * 2 + 4;
                hexCursorStartPx = (segStart - curRowStart) * hexStridePx + _pxPosHexX;
            }
        }
        _hexCursorRect = QRect(hexCursorStartPx - scrollX - 2,
                               _pxCursorY - _pxCharHeight + _pxSelectionSub - 4,
                               hexCursorWidthPx, _pxCharHeight + 2);
        _cursorMultiByteSpan = span;
    }

    // 3. Immediately draw new cursor (also repaint old positions to clear stale frames)
    viewport()->update(oldAsciiCursorRect);
    viewport()->update(oldHexCursorRect);
    viewport()->update(_asciiCursorRect);
    viewport()->update(_hexCursorRect);
    // For multi-byte groups, the cursor spans multiple rows / slots; a full repaint
    // is needed to correctly clear old highlights and fill both row segments.
    {
        const int bufIdx = (int)(_bPosCurrent - _bPosFirst);
        bool isMultiByte = false;
        if (!cursorForcesRaw && !_tbDisplayChars.isEmpty() && bufIdx >= 0 && bufIdx < _tbDisplayChars.size()) {
            int li = bufIdx;
            while (li > 0 && _tbDisplayChars[li].isNull()) --li;
            if (li >= 0 && li < _tbDisplaySpan.size() && _tbDisplaySpan[li] > 1)
                isMultiByte = true;
        } else if (!_encodingChars.isEmpty() && bufIdx >= 0 && bufIdx < _encodingChars.size()) {
            int li = bufIdx;
            while (li > 0 && _encodingChars[li].isNull()) --li;
            if (li >= 0 && li < _encodingSpan.size() && _encodingSpan[li] > 1)
                isMultiByte = true;
        }
        if (isMultiByte)
            viewport()->update();
    }

    emit currentAddressChanged(_bPosCurrent);
}


qint64 HexEditor::cursorPosition(QPoint pos)
{
    // Calc cursor position depending on a graphical position
    qint64 result = -1;
    _gfxClickPixX = -1;
    _gfxClickPixY = -1;

    auto posX = pos.x() + horizontalScrollBar()->value();
    const int rawPosY = pos.y();
    auto posY = rawPosY - _pxColumnNumbersHeight - 3;
    const int rowStridePx = _pxCharHeight + kHexRowExtraGapPx;

    const auto rowBytesThisRow = [this](int r) -> int {
        if (r < 0 || r >= _visualRowStartBytes.size())
            return 0;
        return (r + 1 < _visualRowStartBytes.size())
            ? static_cast<int>(_visualRowStartBytes[r + 1] - _visualRowStartBytes[r])
            : _bytesPerLine;
    };

    const auto isPadRowOfSection = [this, &rowBytesThisRow](int r, int secIdx) -> bool {
        if (!_sectionModel || secIdx < 0 || r < 0 || r >= _visualRowStartBytes.size())
            return false;
        if (rowBytesThisRow(r) > 0)
            return false;

        int prevDataRow = r - 1;
        while (prevDataRow >= 0 && rowBytesThisRow(prevDataRow) <= 0)
            --prevDataRow;
        if (prevDataRow < 0)
            return false;

        const qint64 prevOfs = _visualRowStartBytes[prevDataRow];
        if (_sectionModel->displayModeAtOffset(prevOfs) != SectionDisplay_Graphics)
            return false;
        if (_sectionModel->sectionIndexAtOffset(prevOfs) != secIdx)
            return false;

        const qint64 fileSize = _chunks ? _chunks->size() : 0;
        const Section &sec = _sectionModel->at(secIdx);
        const qint64 dataStart = sec.startOffset;
        const qint64 dataEnd = _sectionModel->endOffsetOf(secIdx, fileSize);
        const qint64 bytes = qMax<qint64>(0, dataEnd - dataStart);
        const int dataRows = static_cast<int>((bytes + _bytesPerLine - 1) / _bytesPerLine);

        const int bpt = tileCodecBytesPerTile(sec.tileCodec);
        const int tileCols = graphicsResolvedTileCols(sec.tileCodec, sec.tileCols);
        int padRows = 0;
        if (bpt > 0 && tileCols > 0) {
            const int totalTiles = static_cast<int>((bytes + bpt - 1) / bpt);
            const int tileRows = (totalTiles + tileCols - 1) / tileCols;
            const int virtualRows = tileRows * 8;
            padRows = qMax(0, virtualRows - dataRows);
        }
        if (padRows <= 0)
            return false;

        int emptiesFromPrev = 0;
        for (int rr = prevDataRow + 1; rr <= r; ++rr) {
            if (rowBytesThisRow(rr) > 0)
                return false;
            ++emptiesFromPrev;
        }
        return emptiesFromPrev <= padRows;
    };

    const int hexStridePx = 3 * _pxCharWidth + kHexColumnExtraGapPx;
    const int hexAreaWidthPx = (_bytesPerLine > 0) ? ((_bytesPerLine - 1) * hexStridePx + 2 * _pxCharWidth) : 0;

    if ((posX >= _pxPosHexX) && (posX < (_pxPosHexX + hexAreaWidthPx)))
    {
        _editAreaIsAscii = false;
        const int relX = posX - _pxPosHexX;
        const int byteIndex = relX / hexStridePx;

        if (byteIndex < 0 || byteIndex >= _bytesPerLine)
            return -1;

        const int inByteX = relX - byteIndex * hexStridePx;
        const int nibble = (inByteX >= _pxCharWidth) ? 1 : 0;
        int x = byteIndex * 2 + nibble;
        int row = posY / rowStridePx;
        if (row < 0 || row >= _visualRowStartBytes.size())
            return -1;
        const qint64 rowAbsStart = _visualRowStartBytes[row];
        const int bytesThisRow = (row + 1 < _visualRowStartBytes.size())
            ? static_cast<int>(_visualRowStartBytes[row + 1] - rowAbsStart)
            : static_cast<int>(qMax(qint64(0), qMin((qint64)_bytesPerLine, _chunks->size() - rowAbsStart)));
        if (bytesThisRow <= 0 || byteIndex >= bytesThisRow)
            return -1;
        qint64 rowByteStart = rowAbsStart - _bPosFirst;

        result = _bPosFirst * 2 + static_cast<qint64>(rowByteStart) * 2 + x;
    }
    else if (_asciiArea && (posX >= _pxPosAsciiX))
    {
        qint64 paletteColorOffset = -1;
        if (paletteColorAtPoint(pos, &paletteColorOffset)) {
            _editAreaIsAscii = false;
            return paletteColorOffset * 2;
        }

        int row = posY / rowStridePx;
        if (row < 0 || row >= _visualRowStartBytes.size())
            return -1;
        const qint64 rowAbsOfs = _visualRowStartBytes[row];
        const int bytesThisRowAtClick = rowBytesThisRow(row);

        bool rowIsGraphics = isGraphicsAt(rowAbsOfs);
        int inheritedSecIdx = -1;
        if (bytesThisRowAtClick <= 0 && _sectionModel) {
            int prev = row - 1;
            while (prev >= 0 && rowBytesThisRow(prev) <= 0)
                --prev;
            if (prev >= 0) {
                const qint64 prevOfs = _visualRowStartBytes[prev];
                if (_sectionModel->displayModeAtOffset(prevOfs) == SectionDisplay_Graphics) {
                    const int prevSecIdx = _sectionModel->sectionIndexAtOffset(prevOfs);
                    if (prevSecIdx >= 0 && isPadRowOfSection(row, prevSecIdx)) {
                        rowIsGraphics = true;
                        inheritedSecIdx = prevSecIdx;
                    }
                }
            }
        }

        // ── Graphics area click (takes priority over regular ASCII area) ──
        if (rowIsGraphics)
        {
            _editAreaIsAscii = false; // direct keyboard input to hex area

            // Graphics canvas rows are painted from exact row top without the -3 tweak.
            const int gfxRow = rawPosY / rowStridePx;
            if (gfxRow < 0 || gfxRow >= _visualRowStartBytes.size())
                return -1;
            row = gfxRow;
            const qint64 gfxRowAbsOfs = _visualRowStartBytes[row];
            const int gfxBytesThisRow = rowBytesThisRow(row);

            // Header/structural empty rows are not drawable. Only explicit pad
            // rows inherited from the previous graphics section are allowed.
            if (gfxBytesThisRow <= 0 && inheritedSecIdx < 0)
                return -1;

            const qint64 fileSize = _chunks ? _chunks->size() : 0;

            // Determine section or global settings
            TileCodec codec = _globalTileCodec;
            int tileColsSetting = _globalTileCols;
            qint64 dataStart = 0;
            qint64 dataEnd   = fileSize;
            int secIdx = inheritedSecIdx;
            if (_sectionModel) {
                int secMode = _sectionModel->displayModeAtOffset(gfxRowAbsOfs);
                if (secMode == SectionDisplay_Graphics || secIdx >= 0) {
                    if (secIdx < 0)
                        secIdx = _sectionModel->sectionIndexAtOffset(gfxRowAbsOfs);
                    if (secIdx >= 0) {
                        const Section &sec = _sectionModel->at(secIdx);
                        codec = sec.tileCodec;
                        tileColsSetting = sec.tileCols;
                        dataStart = sec.startOffset;
                        dataEnd   = _sectionModel->endOffsetOf(secIdx, fileSize);
                    }
                }
            }

            // If a row is empty and inherited from the previous graphics section,
            // ensure section resolution cannot jump to a following section.
            if (gfxBytesThisRow <= 0 && inheritedSecIdx >= 0)
                secIdx = inheritedSecIdx;

            // Apply tile shift
            dataStart = qMax(qint64(0), qMin(dataStart + _gfxTileShift, dataEnd - 1));

            const int bpt = tileCodecBytesPerTile(codec);
            const int tileCols = graphicsResolvedTileCols(codec, tileColsSetting);

            // Compute pixel size (must match paintGraphicsArea)
            const int pixW = qMax(2, (rowStridePx * 17) / 20); // width only

            const int gfxAreaX = _pxPosAsciiX + kAsciiAreaLeftPaddingPx;
            const int xPx = qMax(0, posX - gfxAreaX);
            const int pixCol = xPx / pixW;      // pixel column in the canvas
            const int tileCol = pixCol / 8;
            const int pixX = pixCol % 8;

            // 1 section row == 1 tile pixel row (full row height).

            int visRowInSection = static_cast<int>((gfxRowAbsOfs - dataStart) / _bytesPerLine);

            // Tail padding rows after section data: continue row index by
            // visual row distance from the previous real data row.
            if (_sectionModel && secIdx >= 0 && gfxBytesThisRow <= 0 && gfxRowAbsOfs >= dataEnd - 1) {
                int prevDataRow = row - 1;
                while (prevDataRow >= 0) {
                    if (rowBytesThisRow(prevDataRow) > 0) {
                        const qint64 prevOfs = _visualRowStartBytes[prevDataRow];
                        if (_sectionModel->displayModeAtOffset(prevOfs) == SectionDisplay_Graphics
                            && _sectionModel->sectionIndexAtOffset(prevOfs) == secIdx)
                            break;
                    }
                    --prevDataRow;
                }
                if (prevDataRow >= 0) {
                    const qint64 prevOfs = _visualRowStartBytes[prevDataRow];
                    const int prevVis = static_cast<int>((prevOfs - dataStart) / _bytesPerLine);
                    visRowInSection = prevVis + (row - prevDataRow);
                }
            }

            const int tileRow = visRowInSection / 8;
            const int pixY = visRowInSection % 8;

            if (tileCol < tileCols && bpt > 0) {
                const qint64 tileFileOfs = dataStart
                    + static_cast<qint64>(tileRow) * tileCols * bpt
                    + tileCol * bpt;
                if (tileFileOfs + bpt > dataEnd)
                    return -1; // do not edit outside existing tiles
                const int byteOfs = byteInTileForPixel(codec, pixX, pixY);
                const qint64 targetByte = qMin(tileFileOfs + byteOfs, fileSize - 1);

                // Store highlight info + clicked pixel position
                _gfxHighlightTileCol = tileCol;
                _gfxHighlightTileRow = tileRow;
                _gfxClickPixX = pixX;
                _gfxClickPixY = pixY;
                _gfxClickPadRow = (gfxBytesThisRow <= 0);

                result = targetByte * 2;
            } else {
                return -1;
            }
        }
        // ── Regular ASCII area click ──
        else
        {
        _editAreaIsAscii = true;
        ensureTableDisplayCache();
        ensureEncodingDisplayCache();

        const auto slotGapPx = [this](int baseWidth) {
            return (baseWidth > _pxCharWidth) ? kAsciiColumnGapWidePx : kAsciiColumnGapSinglePx;
        };

        if (row < 0 || row >= _visualRowStartBytes.size())
            return -1;

        const int xPx = qMax(0, posX - (_pxPosAsciiX + kAsciiAreaLeftPaddingPx));
        const qint64 rowByteStart = _visualRowStartBytes[row] - _bPosFirst;
        int bytesThisRowAscii = (row + 1 < _visualRowStartBytes.size())
            ? static_cast<int>(_visualRowStartBytes[row + 1] - _visualRowStartBytes[row])
            : _bytesPerLine;
        const qint64 rowStart = rowByteStart;
        const qint64 rowEnd = qMin(rowStart + bytesThisRowAscii, static_cast<qint64>(_dataShown.size()));

        if (rowStart >= rowEnd)
            return -1;

        // In disasm rows the ASCII area shows a mnemonic string, not per-byte
        // glyphs.  A click anywhere on that row should select the first byte
        // so the hex area highlights the instruction start.
        {
            const qint64 rowAbsOffset = _visualRowStartBytes[row];
            const bool rowIsDisasm = _showDisasm
                || (_sectionModel && _sectionModel->displayModeAtOffset(rowAbsOffset) == SectionDisplay_Disasm);
            if (rowIsDisasm) {
                _editAreaIsAscii = false;          // move focus to hex area
                return _bPosFirst * 2 + rowByteStart * 2; // first byte of row
            }

            const bool rowIsGraphicsSection = _sectionModel
                && _sectionModel->displayModeAtOffset(rowAbsOffset) == SectionDisplay_Graphics;
            if (rowIsGraphicsSection)
                return -1;
        }

        const int rowSectionMode = _sectionModel
            ? _sectionModel->displayModeAtOffset(_visualRowStartBytes[row])
            : SectionDisplay_Default;
        const bool rowForcesRaw = (rowSectionMode == SectionDisplay_Raw);
        TranslationTable *secTable = nullptr;
        if (!rowForcesRaw && rowSectionMode > 0 && rowSectionMode <= _allTables.size())
            secTable = _allTables[rowSectionMode - 1];
        const bool rowUsesTableDisplay = !rowForcesRaw && _tb && !_tbDisplayChars.isEmpty();
        const bool rowUsesTableWidthCache = !rowForcesRaw && _tb && !_tbSymbolWidthPxCache.isEmpty();

        // Walk slots left-to-right accumulating pixel widths until we hit the click X.
        // Multi-byte TBL/encoding entries have zero-width continuation bytes; clicks snap to the lead byte.
        int accumulated = 0;
        int byteCol = 0; // lead byte of the entry being hit

        bool hitSlot = false;
        if (secTable)
        {
            const QFontMetrics fm(font());
            for (int col = 0; col < static_cast<int>(rowEnd - rowStart); ++col)
            {
                const uint8_t rowByte = static_cast<uint8_t>(_dataShown.at(rowStart + col));
                const QString sym = secTable->encodeSymbol(static_cast<char>(rowByte));
                const QString displaySym = sym.isEmpty() ? QString(_notInTableChar) : sym;
                const int baseW = qMax(_pxCharWidth, fm.horizontalAdvance(displaySym));
                const int slotW = baseW + slotGapPx(baseW);
                byteCol = col;
                if (xPx < accumulated + slotW) { hitSlot = true; break; }
                accumulated += slotW;
            }
        }
        else if (rowUsesTableDisplay)
        {
            const QFontMetrics fm(font());
            for (int col = 0; col < static_cast<int>(rowEnd - rowStart); ++col)
            {
                const int idx = (int)rowStart + col;
                if (idx < 0 || idx >= _tbDisplayChars.size()) break;
                if (_tbDisplayChars[idx].isNull()) continue;
                const int baseW = qMax(_pxCharWidth, fm.horizontalAdvance(_tbDisplayChars[idx]));
                const int slotW = baseW + slotGapPx(baseW);
                byteCol = col;
                if (xPx < accumulated + slotW) { hitSlot = true; break; }
                accumulated += slotW;
            }
        }
        else if (!_encodingChars.isEmpty())
        {
            // Encoding mode: continuation bytes have zero width
            const QFontMetrics fm(font());
            for (int col = 0; col < static_cast<int>(rowEnd - rowStart); ++col)
            {
                const int idx = (int)rowStart + col;
                if (idx < 0 || idx >= _encodingChars.size()) break;
                if (_encodingChars[idx].isNull()) continue; // continuation: zero width
                const int baseW = qMax(_pxCharWidth, fm.horizontalAdvance(_encodingChars[idx]));
                const int slotW = baseW + slotGapPx(baseW);
                byteCol = col;
                if (xPx < accumulated + slotW) { hitSlot = true; break; }
                accumulated += slotW;
            }
        }
        else
        {
            byteCol = static_cast<int>(rowEnd - rowStart) - 1; // default: last byte
            for (int col = 0; col < static_cast<int>(rowEnd - rowStart); ++col)
            {
                const uint8_t bv = static_cast<uint8_t>(_dataShown.at(rowStart + col));
                const int baseW = rowUsesTableWidthCache ? _tbSymbolWidthPxCache[bv] : _pxCharWidth;
                const int slotW = baseW + slotGapPx(baseW);
                if (xPx < accumulated + slotW)
                {
                    byteCol = col;
                    hitSlot = true;
                    break;
                }
                accumulated += slotW;
            }
        }

        if (!hitSlot)
            return -1;

        result = _bPosFirst * 2 + static_cast<qint64>(rowByteStart) * 2 + byteCol * 2;
        } // end regular ASCII area click
    } // end ascii/graphics area

    return result;
}


qint64 HexEditor::cursorPosition()
{
    return _cursorPosition;
}


void HexEditor::setData(const QByteArray &ba)
{
    _data = ba;
    _bData.setData(_data);
    setData(_bData);
}


QByteArray HexEditor::data()
{
    return _chunks->data(0, -1);
}


QByteArray HexEditor::getRawSelection()
{
    return _chunks->data(getSelectionBegin(), getSelectionEnd() - getSelectionBegin());
}


QString HexEditor::selectedDisasmText() const
{
    const qint64 selBegin = getSelectionBegin();
    const qint64 selEnd = getSelectionEnd();
    if (selEnd <= selBegin)
        return QString();

    const_cast<HexEditor *>(this)->ensureDisasmBoundaries();
    if (_disasmBoundaries.isEmpty())
        return QString();

    QStringList lines;
    qint64 lastInstrStart = -1;
    for (const auto &boundary : _disasmBoundaries) {
        const qint64 instrStart = boundary.offset;
        const qint64 instrEnd = instrStart + boundary.size;

        if (instrStart >= selEnd)
            break;
        if (instrEnd <= selBegin)
            continue;
        if (!isDisasmAt(instrStart))
            continue;

        const DisasmInstruction *instr = disasmInstructionAtOffset(instrStart);
        if (!instr || instr->fileOffset == lastInstrStart)
            continue;

        const QString line = disasmDisplayText(instr);
        if (!line.isEmpty()) {
            lines.append(line);
            lastInstrStart = instr->fileOffset;
        }
    }

    return lines.join(QLatin1Char('\n'));
}


Datas HexEditor::getValue(qint64 offset)
{
    Datas value{};

    // Fast path: read from already-buffered visible data to avoid IO
    const qint64 relOfs = offset - _bPosFirst;
    if (relOfs >= 0 && relOfs + 4 <= _dataShown.size())
    {
        memcpy(&value, _dataShown.constData() + relOfs, 4);
        return value;
    }

    // Fall back to chunks (triggers IO only for out-of-view offsets)
    const QByteArray buf = _chunks->data(offset, 4);
    if (buf.size() >= 4)
        memcpy(&value, buf.constData(), 4);
    else if (!buf.isEmpty())
        memcpy(&value, buf.constData(), buf.size());

    return value;
}


qint64 HexEditor::getCurrentOffset()
{
    return _bPosCurrent;
}


void HexEditor::setHighlighting(bool highlighting)
{
    _highlighting = highlighting;
    viewport()->update();
}


bool HexEditor::highlighting()
{
    return _highlighting;
}


void HexEditor::setHighlightingColor(const QColor &color)
{
    _brushHighlighted = QBrush(color);
    _penHighlighted = QPen(viewport()->palette().color(QPalette::WindowText));
    viewport()->update();
}


QColor HexEditor::highlightingColor()
{
    return _brushHighlighted.color();
}


void HexEditor::setShowChanges(bool mode)
{
    _showChanges = mode;
    viewport()->update();
}


bool HexEditor::showChanges()
{
    return _showChanges;
}


void HexEditor::setOriginalData(const QByteArray &data)
{
    _originalData = data;
    if (_showOriginal)
        readBuffers();
    viewport()->update();
}


void HexEditor::setShowOriginal(bool show)
{
    if (_showOriginal == show)
        return;
    _showOriginal = show;
    readBuffers();
    viewport()->update();
}


bool HexEditor::showOriginal() const
{
    return _showOriginal;
}


bool HexEditor::hasOriginalData() const
{
    return !_originalData.isEmpty();
}


void HexEditor::setChangesColor(const QColor &color)
{
    _changesColor = color;
    _brushChanges = QBrush(color);
    _penChanges = QPen(viewport()->palette().color(QPalette::WindowText));
    if (_scrollMapChanges) _scrollMapChanges->setColor(color);
    viewport()->update();
}


QColor HexEditor::changesColor()
{
    return _changesColor;
}


void HexEditor::setChangedPositions(const QSet<qint64> &positions)
{
    _changedPositions = positions;
    viewport()->update();
    updateScrollMap();  // refresh the changes map strip
}


void HexEditor::clearChangedPositions()
{
    _changedPositions.clear();
    clearChangedRange();
    viewport()->update();
    updateScrollMap();  // hide the changes map strip
}


void HexEditor::setChangedRange(qint64 start, qint64 end)
{
    if (start < 0 || end <= start) {
        clearChangedRange();
        return;
    }
    _changedRangeStart = start;
    _changedRangeEnd = end;
    viewport()->update();
    updateScrollMap();
}


void HexEditor::clearChangedRange()
{
    _changedRangeStart = -1;
    _changedRangeEnd = -1;
}


void HexEditor::setAllTables(const QVector<TranslationTable*> &tables)
{
    _allTables = tables;
}


void HexEditor::setMultibyteFrameColor(const QColor &color)
{
    _multibyteFrameColor = color;
    viewport()->update();
}


QColor HexEditor::multibyteFrameColor()
{
    return _multibyteFrameColor;
}


QColor HexEditor::sectionHeaderFontColor() const
{
    return _sectionHeaderFontColor;
}


bool HexEditor::showColumnNumbers() const
{
    return _showColumnNumbers;
}


void HexEditor::setShowColumnNumbers(bool show)
{
    _showColumnNumbers = show;
    updateColumnNumbersMetrics();
    adjust();
    viewport()->update();
}


QFont HexEditor::columnNumbersFont() const
{
    return _columnNumbersFont;
}


void HexEditor::setColumnNumbersFont(const QFont &font)
{
    _columnNumbersFont = font;
    updateColumnNumbersMetrics();
    adjust();
    viewport()->update();
}


QColor HexEditor::columnNumbersFontColor() const
{
    return _columnNumbersFontColor;
}


void HexEditor::setColumnNumbersFontColor(const QColor &color)
{
    _columnNumbersFontColor = color;
    viewport()->update();
}


QColor HexEditor::columnNumbersBackgroundColor() const
{
    return _columnNumbersBackgroundColor;
}


void HexEditor::setColumnNumbersBackgroundColor(const QColor &color)
{
    _columnNumbersBackgroundColor = color;
    viewport()->update();
}


void HexEditor::setSectionHeaderFontColor(const QColor &color)
{
    _sectionHeaderFontColor = color;
    viewport()->update();
}


QColor HexEditor::sectionHeaderBackgroundColor() const
{
    return _sectionHeaderBackgroundColor;
}


void HexEditor::setSectionHeaderBackgroundColor(const QColor &color)
{
    _sectionHeaderBackgroundColor = color;
    viewport()->update();
}


QFont HexEditor::sectionHeaderFont() const
{
    return _sectionHeaderFont;
}


void HexEditor::setSectionHeaderFont(const QFont &font)
{
    _sectionHeaderFont = font;
    viewport()->update();
}


bool HexEditor::showMultibyteFrame() const
{
    return _showMultibyteFrame;
}


void HexEditor::setShowMultibyteFrame(bool show)
{
    _showMultibyteFrame = show;
    viewport()->update();
}


void HexEditor::setOverwriteMode(bool overwriteMode)
{
    _overwriteMode = overwriteMode;
    emit overwriteModeChanged(overwriteMode);
}


bool HexEditor::overwriteMode()
{
    return _overwriteMode;
}


void HexEditor::setSelectionColor(const QColor &color)
{
    _brushSelection = QBrush(color);
    _penSelection = QPen(Qt::white);
    viewport()->update();
}


QColor HexEditor::selectionColor()
{
    return _brushSelection.color();
}


bool HexEditor::showHexGrid()
{
    return _showHexGrid;
}


void HexEditor::setShowHexGrid(bool mode)
{
    _showHexGrid = mode;
    viewport()->update();
}


QColor HexEditor::hexAreaBackgroundColor()
{
    return _hexAreaBackgroundColor;
}


void HexEditor::setHexAreaBackgroundColor(const QColor &color)
{
    _hexAreaBackgroundColor = color;
    viewport()->update();
}


QColor HexEditor::hexAreaGridColor()
{
    return _hexAreaGridColor;
}


void HexEditor::setHexAreaGridColor(const QColor &color)
{
    _hexAreaGridColor = color;
    viewport()->update();
}


bool HexEditor::isReadOnly()
{
    return _readOnly;
}


void HexEditor::setReadOnly(bool readOnly)
{
    _readOnly = readOnly;
}


void HexEditor::setHexCaps(const bool isCaps)
{
    if (_hexCaps != isCaps)
    {
        _hexCaps = isCaps;
        viewport()->update();
    }
}


bool HexEditor::hexCaps()
{
    return _hexCaps;
}


void HexEditor::setDynamicBytesPerLine(const bool isDynamic)
{
    _dynamicBytesPerLine = isDynamic;
    resizeEvent(NULL);
}


bool HexEditor::dynamicBytesPerLine()
{
    return _dynamicBytesPerLine;
}

// ********************************************************************** Access to data of hexeditor

bool HexEditor::setData(QIODevice &iODevice)
{
    bool ok = _chunks->setIODevice(iODevice);

    init();

    dataChangedPrivate();

    return ok;
}


QByteArray HexEditor::dataAt(qint64 pos, qint64 count)
{
    return _chunks->data(pos, count);
}


qint64 HexEditor::dataSize() const
{
    return _chunks->size();
}


bool HexEditor::write(QIODevice &iODevice, qint64 pos, qint64 count)
{
    return _chunks->write(iODevice, pos, count);
}

// ********************************************************************** Char handling

void HexEditor::insert(qint64 index, char ch)
{
    _undoStack->insert(index, ch);
    refresh();
}


void HexEditor::remove(qint64 index, qint64 len)
{
    _undoStack->removeAt(index, len);
    refresh();
}


void HexEditor::replace(qint64 index, char ch)
{
    _undoStack->overwrite(index, ch);
    refresh();
}

// ********************************************************************** ByteArray handling

void HexEditor::insert(qint64 pos, const QByteArray &ba)
{
    _undoStack->insert(pos, ba);
    refresh();
}


void HexEditor::replace(qint64 pos, qint64 len, const QByteArray &ba)
{
    _undoStack->overwrite(pos, len, ba);
    refresh();
}

void HexEditor::replaceNoUndo(qint64 pos, qint64 len, const QByteArray &ba)
{
    if (!_chunks || pos < 0 || len <= 0 || ba.isEmpty() || pos >= _chunks->size())
        return;

    const qint64 maxWritable = qMin<qint64>(len, qMin<qint64>(ba.size(), _chunks->size() - pos));
    if (maxWritable <= 0)
        return;

    bool changed = false;
    for (qint64 i = 0; i < maxWritable; ++i)
    {
        const qint64 bytePos = pos + i;
        const char newByte = ba.at(static_cast<int>(i));
        if ((*_chunks)[bytePos] == newByte)
            continue;
        _chunks->overwrite(bytePos, newByte);
        changed = true;
    }

    if (!changed)
        return;

    _baseModified = true;
    dataChangedPrivate();
    refresh();
}

// ********************************************************************** Utility functions

void HexEditor::ensureVisible()
{
    const qint64 cursorByte = _cursorPosition / 2;
    const qint64 cursorVisRow = visualRowForByte(cursorByte);

    if (_cursorPosition < (_bPosFirst * 2))
        verticalScrollBar()->setValue(static_cast<int>(cursorVisRow));

    if (_cursorPosition > (_bPosLast * 2 + 1))
        verticalScrollBar()->setValue(static_cast<int>(cursorVisRow) - _rowsShown + 1);

    if (_pxCursorX < horizontalScrollBar()->value())
        horizontalScrollBar()->setValue(_pxCursorX);

    if ((_pxCursorX + _pxCharWidth) > (horizontalScrollBar()->value() + viewport()->width()))
        horizontalScrollBar()->setValue(_pxCursorX + _pxCharWidth - viewport()->width());

    viewport()->update();
}


void HexEditor::ensureVisibleCentered()
{
    const qint64 cursorByte = _cursorPosition / 2;
    const qint64 cursorVisRow = visualRowForByte(cursorByte);
    const int half = qMax(1, _rowsShown) / 2;
    const int target = qBound(0,
                               static_cast<int>(cursorVisRow) - half,
                               verticalScrollBar()->maximum());
    verticalScrollBar()->setValue(target);
    viewport()->update();
}


void HexEditor::ensureVisibleTop()
{
    const qint64 cursorByte = _cursorPosition / 2;
    const qint64 row = visualRowForByte(cursorByte);
    const int target = qBound(0, static_cast<int>(row), verticalScrollBar()->maximum());
    verticalScrollBar()->setValue(target);
    viewport()->update();
}


qint64 HexEditor::indexOf(const QByteArray &ba, qint64 from)
{
    const qint64 pos = findNextIndex(ba, from);
    if (pos > -1)
        highlightMatch(pos, ba.length());
    return pos;
}


qint64 HexEditor::findNextIndex(const QByteArray &ba, qint64 from, bool relative)
{
    if (ba.isEmpty())
        return -1;

    if (!relative)
        return _chunks->indexOf(ba, from);

    const QByteArray haystack = _chunks->data(0, -1);
    const char *buf = haystack.constData();
    const int searchLen = ba.size();
    const qint64 maxOffset = haystack.size() - searchLen;
    if (maxOffset < 0)
        return -1;

    QByteArray relNeedle;
    relNeedle.reserve(searchLen);
    relNeedle.append('\0');

    for (int j = 1; j < searchLen; ++j)
        relNeedle.append(ba[0] - ba[j]);

    for (qint64 i = qBound<qint64>(0, from, maxOffset); i <= maxOffset; ++i)
    {
        int coin = 1;

        for (int j = 1; j < searchLen; ++j)
        {
            if ((buf[i] - buf[i + j]) != relNeedle[j])
                break;

            ++coin;
        }

        if (coin == searchLen)
            return i;
    }

    return -1;
}


qint64 HexEditor::relativeSearch(const QByteArray &ba, qint64 from)
{
    const qint64 pos = findNextIndex(ba, from, true);
    if (pos > -1)
        highlightMatch(pos, ba.length());
    return pos;
}


void HexEditor::jumpTo(qint64 offset, bool relative)
{
    auto newPos = qBound(0LL, (relative ? (_cursorPosition / 2) + offset : offset), dataSize());

    setCursorPosition(newPos * 2);
    resetSelection(_cursorPosition);
    ensureVisible();
}


bool HexEditor::isModified()
{
    return _modified;
}


void HexEditor::setModified(bool modified)
{
    _baseModified = modified;
    if (!modified)
        _cleanUndoIndex = _undoStack ? (_undoStack->index() - _lineBreakCmdCount) : 0;
    _modified = modified;
}


bool HexEditor::canUndo()
{
    return _undoStack->canUndo();
}


bool HexEditor::canRedo()
{
    return _undoStack->canRedo();
}


void HexEditor::selectByteRange(qint64 start, qint64 end)
{
    const qint64 clampedStart = qMax<qint64>(0, start);
    const qint64 clampedEnd = qMin(end, _chunks->size());
    if (clampedEnd <= clampedStart)
        return;

    const qint64 cursorPos = clampedStart * 2;
    const qint64 selectionEndPos = clampedEnd * 2 - 1;
    setCursorPosition(cursorPos);
    resetSelection(cursorPos);
    setSelection(selectionEndPos);
}


void HexEditor::highlightMatch(qint64 pos, qint64 length)
{
    if (pos < 0 || length <= 0)
        return;

    const qint64 curPos = pos * 2;
    const qint64 selectionEndPos = curPos + length * 2 - 1;

    setCursorPosition(curPos);
    resetSelection(curPos);
    setSelection(selectionEndPos);
    ensureVisible();
}


qint64 HexEditor::lastIndexOf(const QByteArray &ba, qint64 from)
{
    const qint64 pos = findPreviousIndex(ba, from);
    if (pos > -1)
        highlightMatch(pos, ba.length());
    return pos;
}


qint64 HexEditor::findPreviousIndex(const QByteArray &ba, qint64 from, bool relative)
{
    if (ba.isEmpty())
        return -1;

    if (!relative)
        return _chunks->lastIndexOf(ba, from);

    const QByteArray haystack = _chunks->data(0, -1);
    const char *buf = haystack.constData();
    const int searchLen = ba.size();
    const qint64 maxOffset = haystack.size() - searchLen;
    if (maxOffset < 0)
        return -1;

    QByteArray relNeedle;
    relNeedle.reserve(searchLen);
    relNeedle.append('\0');

    for (int j = 1; j < searchLen; ++j)
        relNeedle.append(ba[0] - ba[j]);

    for (qint64 i = qBound<qint64>(0, from, maxOffset); i >= 0; --i)
    {
        int coin = 1;

        for (int j = 1; j < searchLen; ++j)
        {
            if ((buf[i] - buf[i + j]) != relNeedle[j])
                break;

            ++coin;
        }

        if (coin == searchLen)
            return i;
    }

    return -1;
}


void HexEditor::redo()
{
    if (!_undoStack->canRedo())
        return;
    // Pre-update lb count before indexChanged fires in redo()
    const QUndoCommand *redoCmd = _undoStack->command(_undoStack->index());
#ifndef QT_NO_DEBUG
    qDebug("REDO idx=%d/%d cmd=\"%s\" id=%d children=%d",
           _undoStack->index(), _undoStack->count(),
           redoCmd ? qPrintable(redoCmd->text()) : "(null)",
           redoCmd ? redoCmd->id() : -999,
           redoCmd ? redoCmd->childCount() : 0);
#endif
    const bool isLineBreakCmd = (redoCmd && redoCmd->id() == kLineBreakCmdId);
    // CharCommand id=1234; macros wrapping CharCommands have id=-1 but child(0)->id()==1234
    const bool isDataCmd = redoCmd && (redoCmd->id() == 1234
        || (redoCmd->childCount() > 0 && redoCmd->child(0)->id() == 1234));
    // Macros (e.g. "Add section") may contain line-break children that were
    // counted during creation.  Adjust the count so logicalUndoIndex stays correct.
    int lbChildren = 0;
    if (redoCmd && !isLineBreakCmd && redoCmd->childCount() > 0) {
        for (int i = 0; i < redoCmd->childCount(); ++i)
            if (redoCmd->child(i)->id() == kLineBreakCmdId)
                ++lbChildren;
    }
    if (isLineBreakCmd)
        ++_lineBreakCmdCount;
    else
        _lineBreakCmdCount += lbChildren;
    const bool touchesLineBreaks = isLineBreakCmd || lbChildren > 0;
    _lineBreakChangeInProgress = touchesLineBreaks;
    _nonDataChangeInProgress = !isDataCmd && !touchesLineBreaks;
    const qint64 savedCursor = _cursorPosition;
    _undoStack->redo();
    _lineBreakChangeInProgress = false;
    _nonDataChangeInProgress = false;
    // Only CharCommands update _chunks->pos(); pointer/section/linebreak
    // commands don't touch chunks so the stored position is stale.
    setCursorPosition(isDataCmd ? _chunks->pos() * 2 : savedCursor);
    refresh();
}


QString HexEditor::selectionToReadableString()
{
    QByteArray ba = _chunks->data(getSelectionBegin(), getSelectionEnd() - getSelectionBegin());
    return toReadable(ba);
}


QString HexEditor::selectedData()
{
    QByteArray ba = _chunks->data(getSelectionBegin(), getSelectionEnd() - getSelectionBegin()).toHex();
    return ba;
}


QString HexEditor::toReadableString()
{
    QByteArray ba = _chunks->data();
    return toReadable(ba);
}


void HexEditor::undo()
{
    if (!_undoStack->canUndo())
        return;
    // Pre-update lb count before indexChanged fires in undo()
    const int idx = _undoStack->index();
    bool isLineBreakCmd = false;
    bool isDataCmd = false;
    int lbChildren = 0;
    if (idx > 0) {
        const QUndoCommand *undoCmd = _undoStack->command(idx - 1);
#ifndef QT_NO_DEBUG
        qDebug("UNDO idx=%d/%d cmd=\"%s\" id=%d children=%d",
               idx - 1, _undoStack->count(),
               undoCmd ? qPrintable(undoCmd->text()) : "(null)",
               undoCmd ? undoCmd->id() : -999,
               undoCmd ? undoCmd->childCount() : 0);
#endif
        isLineBreakCmd = (undoCmd && undoCmd->id() == kLineBreakCmdId);
        // CharCommand id=1234; macros wrapping CharCommands have id=-1 but child(0)->id()==1234
        isDataCmd = undoCmd && (undoCmd->id() == 1234
            || (undoCmd->childCount() > 0 && undoCmd->child(0)->id() == 1234));
        // Macros (e.g. "Add section") may contain line-break children that were
        // counted during creation.  Adjust the count so logicalUndoIndex stays correct.
        if (undoCmd && !isLineBreakCmd && undoCmd->childCount() > 0) {
            for (int i = 0; i < undoCmd->childCount(); ++i)
                if (undoCmd->child(i)->id() == kLineBreakCmdId)
                    ++lbChildren;
        }
        if (isLineBreakCmd)
            --_lineBreakCmdCount;
        else
            _lineBreakCmdCount -= lbChildren;
    }
    const bool touchesLineBreaks = isLineBreakCmd || lbChildren > 0;
    _lineBreakChangeInProgress = touchesLineBreaks;
    _nonDataChangeInProgress = !isDataCmd && !touchesLineBreaks;
    const qint64 savedCursor = _cursorPosition;
    _undoStack->undo();
    _lineBreakChangeInProgress = false;
    _nonDataChangeInProgress = false;
    // Only CharCommands update _chunks->pos(); pointer/section/linebreak
    // commands don't touch chunks so the stored position is stale.
    setCursorPosition(isDataCmd ? _chunks->pos() * 2 : savedCursor);
    refresh();
}


TranslationTable *HexEditor::getTranslationTable()
{
    return _tb;
}


void HexEditor::setTranslationTable(TranslationTable *tb)
{
    // Save viewport position so toggling the table/autosize doesn't jump vertically
    const qint64 savedTopByte = static_cast<qint64>(verticalScrollBar()->value()) * qMax(1, _bytesPerLine);
    const qint64 savedCursorPos = _cursorPosition;
    const int savedHorizontal = horizontalScrollBar()->value();

    _tb = tb;
    _tbDisplayCacheValid = false;
    invalidateAsciiAreaWidthCache();
    ensureAsciiAreaWidthCache();
    updateAsciiAreaMaxWidth();

    if (_dynamicBytesPerLine)
        resizeEvent(nullptr);
    else
        adjust();

    restoreTopVisibleByte(savedTopByte);
    horizontalScrollBar()->setValue(savedHorizontal);
    setCursorPosition(savedCursorPos);
    viewport()->update();
}


void HexEditor::removeTranslationTable()
{
    setTranslationTable(); // with no parameters removes translation table
}


QString HexEditor::currentEncoding() const
{
    return _currentEncoding;
}


void HexEditor::setCurrentEncoding(const QString &encoding)
{
    if (_currentEncoding == encoding) return;
    _currentEncoding = encoding;
    _encodingCacheValid = false;
    invalidateAsciiAreaWidthCache();
    viewport()->update();
}


QString HexEditor::decodeTextForCurrentEncoding(const QByteArray &bytes) const
{
    if (_tb)
        return _tb->encode(bytes, true);
    return decodeTextWithEncoding(bytes, _currentEncoding);
}


QByteArray HexEditor::encodeTextForCurrentEncoding(const QString &text) const
{
    if (_tb)
        return _tb->decode(text.toUtf8());
    return encodeTextWithEncoding(text, _currentEncoding);
}

QVector<QString> HexEditor::decodeBufferForCurrentEncoding(const QByteArray &data) const
{
    if (_tb) {
        QVector<QString> chars;
        QVector<int> span;
        decodeBufferWithTable(data, _tb, chars, span);
        return chars;
    }
    return decodeBufferWithEncoding(data, _currentEncoding);
}

// ********************************************************************** Handle events

bool HexEditor::hasSelection()
{
    return _bSelectionEnd - _bSelectionBegin > 1;
}


void HexEditor::resetSelection()
{
    _bSelectionBegin = _bSelectionInit;
    _bSelectionEnd = _bSelectionInit;

    emit selectionChanged(_bSelectionBegin, _bSelectionEnd);
}


void HexEditor::resetSelection(qint64 pos)
{
    pos = pos / 2;
    if (pos < 0)
        pos = 0;
    if (pos > _chunks->size())
        pos = _chunks->size();

    _bSelectionInit = pos;
    _bSelectionBegin = pos;
    _bSelectionEnd = pos;

    emit selectionChanged(_bSelectionBegin, _bSelectionEnd);
}


void HexEditor::setSelection(qint64 pos)
{
    pos = pos / 2;

    if (pos < 0)
        pos = 0;

    if (pos > _chunks->size())
        pos = _chunks->size();

    if (pos >= _bSelectionInit)
    {
        // Include the cursor byte: end is exclusive, so +1
        _bSelectionEnd = qMin(pos + 1, _chunks->size());
        _bSelectionBegin = _bSelectionInit;
    }
    else
    {
        // Cursor is before init: include init byte too
        _bSelectionBegin = pos;
        _bSelectionEnd = qMin(_bSelectionInit + 1, _chunks->size());
    }

    emit selectionChanged(_bSelectionBegin, _bSelectionEnd);
}


qint64 HexEditor::getSelectionBegin() const
{
    return _bSelectionBegin;
}


qint64 HexEditor::getSelectionEnd() const
{
    return _bSelectionEnd;
}

// ********************************************************************** Private utility functions

#include <QtGlobal>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStyleOptionSlider>
#include <QToolTip>
#include <QHelpEvent>
#include <QProgressDialog>
#include <QTimer>
#include <QSet>
#include <QUndoCommand>

#include "qhexedit.h"
#include "hexscrollmap.h"
#include <algorithm>
#include <cmath>
#include <QtConcurrent>

namespace
{
    const int kHexColumnExtraGapPx = 4;
    const int kHexRowExtraGapPx = 4;
    const int kAddressRightPaddingPx = 4;
    const int kAsciiAreaLeftPaddingPx = 4;
    const int kAsciiColumnGapPx = 2;  // extra padding between ASCII column slots
    const int kPointerByteSize = 4;
    const int kScrollMapWidth = 12;   // width of the side-bar scroll map strip in pixels

    struct PointerState
    {
        qint64 pointerOffset = -1;
        bool hasTarget = false;
        qint64 targetOffset = -1;
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
                    _model->addPointer(state.pointerOffset, state.targetOffset);
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
        }

        return state;
    }
}

// ********************************************************************** Constructor, destructor

QHexEdit::QHexEdit(QWidget *parent) : QAbstractScrollArea(parent), _addressArea(true), _addressWidth(4), _asciiArea(true), _bytesPerLine(32), _hexCharsInLine(47), _highlighting(true), _overwriteMode(true), _showHexGrid(true), _readOnly(false), _showPointers(true), _hexCaps(true), _dynamicBytesPerLine(true), _editAreaIsAscii(true), _chunks(new Chunks(this)), _cursorPosition(0), _lastEventSize(0), _undoStack(new UndoStack(_chunks, this))
{
    setFont(QFont("Courier New", 14));

    auto font = QFont("Courier New", 16, 10);
    QToolTip::setFont(font);

    setAddressAreaColor(this->palette().alternateBase().color());
    setHighlightingColor(QColor(0xff, 0xff, 0x99, 0xff));
    setPointersColor(QColor(0xff, 0x99, 0x00, 0xff));
    setPointedColor(QColor(0x99, 0xff, 0x00, 0xff));
    setPointerFontColor(this->palette().color(QPalette::WindowText));
    setPointedFontColor(this->palette().color(QPalette::WindowText));
    setPointerFrameColor(QColor(0, 0, 0xff));
    setPointerFrameBackgroundColor(QColor(0, 0xFF, 0, 0x80));
    setSelectionColor(this->palette().highlight().color());
    setAddressFontColor(QPalette::WindowText);
    setAsciiAreaColor(this->palette().alternateBase().color());
    setAsciiFontColor(QPalette::WindowText);
    setHexAreaBackgroundColor(Qt::white);
    setHexAreaGridColor(QColor(0x99, 0x99, 0x99));
    _cursorCharColor = QColor(0x00, 0x60, 0xFF, 0x80);

    connect(&_cursorTimer, SIGNAL(timeout()), this, SLOT(updateCursor()));
    connect(verticalScrollBar(), SIGNAL(valueChanged(int)), this, SLOT(adjust()));
    connect(horizontalScrollBar(), SIGNAL(valueChanged(int)), this, SLOT(adjust()));
    connect(_undoStack, SIGNAL(indexChanged(int)), this, SLOT(dataChangedPrivate(int)));

//    _cursorTimer.setInterval(500);
//    _cursorTimer.start();

    // not the best idea though
    _pointers.setHexEdit(this);

    // Scroll map — two thin strips to the right of the viewport:
    //   _scrollMapPtr    (orange)    — pointer storage locations
    //   _scrollMapTarget (sky-blue)  — pointer target locations
    _scrollMapPtr = new HexScrollMap(this);
    _scrollMapPtr->setColor(pointersColor());
    _scrollMapPtr->hide();  // shown only when pointers are present

    _scrollMapTarget = new HexScrollMap(this);
    _scrollMapTarget->setColor(QColor(0x40, 0xbf, 0xff));  // sky-blue — pointer targets
    _scrollMapTarget->hide();

    setViewportMargins(0, 0, 0, 0);  // margins set dynamically by updateScrollMapMargins()

    // Debounce timer: coalesces rapid per-pointer model signals into one batch recompute
    _scrollMapTimer = new QTimer(this);
    _scrollMapTimer->setSingleShot(true);
    _scrollMapTimer->setInterval(200);  // ms — waits for pointer search to complete
    connect(_scrollMapTimer, &QTimer::timeout,
            this, &QHexEdit::scheduleScrollMapCompute);

    // Background computation result handler
    _scrollMapWatcher = new QFutureWatcher<ScrollMapMarkers>(this);
    connect(_scrollMapWatcher, &QFutureWatcher<ScrollMapMarkers>::finished,
            this, [this]() {
        if (_scrollMapWatcher->isCanceled()) return;
        const auto &r = _scrollMapWatcher->result();
        if (_scrollMapPtr) {
            _scrollMapPtr->setTickOffsets(r.ptrYToOff);
            _scrollMapPtr->setTicks(r.ptrYs);
        }
        if (_scrollMapTarget) {
            _scrollMapTarget->setTickOffsets(r.targetYToOff);
            _scrollMapTarget->setTicks(r.targetYs);
        }
    });

    // Recompute when either strip height changes (Y-mapping depends on height).
    // Goes through debounce timer to avoid rapid recomputes during window resize.
    connect(_scrollMapPtr,    &HexScrollMap::heightChanged, this, &QHexEdit::updateScrollMap);
    connect(_scrollMapTarget, &HexScrollMap::heightChanged, this, &QHexEdit::updateScrollMap);

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
    connect(_scrollMapPtr,    &HexScrollMap::tickClicked, this, scrollMapJump);
    connect(_scrollMapTarget, &HexScrollMap::tickClicked, this, scrollMapJump);

    // Kick debounce timer on any pointer model change
    auto onModelChanged = [this]() { updateScrollMap(); };
    connect(&_pointers, &QAbstractTableModel::modelReset,   this, onModelChanged);
    connect(&_pointers, &QAbstractTableModel::rowsInserted, this, onModelChanged);
    connect(&_pointers, &QAbstractTableModel::rowsRemoved,  this, onModelChanged);
    connect(&_pointers, &QAbstractTableModel::dataChanged,  this, onModelChanged);
    connect(&_pointers, &PointerListModel::pointersChanged, this, onModelChanged);

    init();
}

QHexEdit::~QHexEdit()
{
}

// ********************************************************************** Properties

void QHexEdit::setAddressArea(bool addressArea)
{
    _addressArea = addressArea;
    adjust();
    setCursorPosition(_cursorPosition);
    viewport()->update();
}

bool QHexEdit::addressArea()
{
    return _addressArea;
}

void QHexEdit::setAddressAreaColor(const QColor &color)
{
    _addressAreaColor = color;
    viewport()->update();
}

QColor QHexEdit::addressAreaColor()
{
    return _addressAreaColor;
}

void QHexEdit::setAddressFontColor(const QColor &color)
{
    _addressFontColor = color;
    viewport()->update();
}

QColor QHexEdit::addressFontColor()
{
    return _addressFontColor;
}

void QHexEdit::setAsciiAreaColor(const QColor &color)
{
    _asciiAreaColor = color;
    viewport()->update();
}

QColor QHexEdit::asciiAreaColor()
{
    return _asciiAreaColor;
}

void QHexEdit::setAsciiFontColor(const QColor &color)
{
    _asciiFontColor = color;
    viewport()->update();
}

QColor QHexEdit::asciiFontColor()
{
    return _asciiFontColor;
}

QChar QHexEdit::nonPrintableNoTableChar() const
{
    return _nonPrintableNoTableChar;
}

void QHexEdit::setNonPrintableNoTableChar(const QChar &ch)
{
    _nonPrintableNoTableChar = ch.isNull() ? QChar(0x25AA) : ch;
    invalidateAsciiAreaWidthCache();
    updateAsciiAreaMaxWidth();
    viewport()->update();
}

QChar QHexEdit::notInTableChar() const
{
    return _notInTableChar;
}

void QHexEdit::setNotInTableChar(const QChar &ch)
{
    _notInTableChar = ch.isNull() ? QChar(0x25A1) : ch;
    invalidateAsciiAreaWidthCache();
    updateAsciiAreaMaxWidth();
    viewport()->update();
}

QColor QHexEdit::cursorCharColor()
{
    return _cursorCharColor;
}

void QHexEdit::setCursorCharColor(const QColor &color)
{
    _cursorCharColor = color;
    viewport()->update();
}

QColor QHexEdit::cursorFrameColor()
{
    return _cursorFrameColor;
}

void QHexEdit::setCursorFrameColor(const QColor &color)
{
    _cursorFrameColor = color;
    viewport()->update();
}

void QHexEdit::setHexFontColor(const QColor &color)
{
    _hexFontColor = color;
    viewport()->update();
}

QColor QHexEdit::hexFontColor()
{
    return _hexFontColor;
}

void QHexEdit::setAddressOffset(qint64 addressOffset)
{
    _addressOffset = addressOffset;
    adjust();
    setCursorPosition(_cursorPosition);
    viewport()->update();
}

qint64 QHexEdit::addressOffset()
{
    return _addressOffset;
}

void QHexEdit::setAddressWidth(int addressWidth)
{
    _addressWidth = addressWidth;
    adjust();
    setCursorPosition(_cursorPosition);
    viewport()->update();
}

int QHexEdit::addressWidth()
{
    auto size = _chunks->size();

    int n = 1;

    if (size > Q_INT64_C(0x100000000))
    {
        n += 8;
        size /= Q_INT64_C(0x100000000);
    }
    else if (size > 0x10000)
    {
        n += 4;
        size /= 0x10000;
    }
    else if (size > 0x100)
    {
        n += 2;
        size /= 0x100;
    }
    else if (size > 0x10)
    {
        n += 1;
    }
    else if (n > _addressWidth)
    {
        return n;
    }

    return _addressWidth;
}

void QHexEdit::setAsciiArea(bool asciiArea)
{
    if (!asciiArea)
        _editAreaIsAscii = false;
    _asciiArea = asciiArea;
    invalidateAsciiAreaWidthCache();
    updateAsciiAreaMaxWidth();
    adjust();
    setCursorPosition(_cursorPosition);
    viewport()->update();
}

bool QHexEdit::asciiArea()
{
    return _asciiArea;
}

void QHexEdit::setBytesPerLine(int count)
{
    _bytesPerLine = count;
    _hexCharsInLine = count * 3 - 1;
    updateAsciiAreaMaxWidth();

    adjust();
    setCursorPosition(_cursorPosition);
    viewport()->update();
}

int QHexEdit::bytesPerLine()
{
    return _bytesPerLine;
}

void QHexEdit::setCursorPosition(qint64 position)
{
    // 1. Check, if cursor in range?
    if (position > (_chunks->size() * 2 - 1))
        position = _chunks->size() * 2 - (_overwriteMode ? 1 : 0);

    if (position < 0)
        position = 0;

    // 2. Calc new position of cursor
    _bPosCurrent = position / 2;
    auto line = ((_bPosCurrent - _bPosFirst) / _bytesPerLine + 1);
    const int rowStridePx = _pxCharHeight + kHexRowExtraGapPx;
    _pxCursorY = line * rowStridePx;
    auto x = (position % (2 * _bytesPerLine));

    _cursorPosition = position;

    // ascii area cursor
    const int byteInLine = x / 2;
    int asciiOffsetPx = 0;
    int asciiCursorWidthPx = _pxCharWidth;

    if (_asciiArea)
    {
        // Accumulate per-slot widths up to byteInLine so cursor lands on the right token.
        asciiOffsetPx = 0;
        for (int col = 0; col < byteInLine; ++col)
        {
            // Find the byte value at this column in the current row
            const qint64 bytePos = (_bPosCurrent / _bytesPerLine) * _bytesPerLine + col;
            const uint8_t bv = (bytePos < _dataShown.size())
                ? static_cast<uint8_t>(_dataShown.at(bytePos))
                : 0;
            asciiOffsetPx += (_tb && !_tbSymbolWidthPxCache.isEmpty())
                ? (_tbSymbolWidthPxCache[bv] + kAsciiColumnGapPx)
                : (_pxCharWidth + kAsciiColumnGapPx);
        }
        // Width of the current slot
        const qint64 curBytePos = (_bPosCurrent / _bytesPerLine) * _bytesPerLine + byteInLine;
        const uint8_t curBv = (curBytePos < _dataShown.size())
            ? static_cast<uint8_t>(_dataShown.at(curBytePos))
            : 0;
        asciiCursorWidthPx = (_tb && !_tbSymbolWidthPxCache.isEmpty())
            ? (_tbSymbolWidthPxCache[curBv] + kAsciiColumnGapPx)
            : (_pxCharWidth + kAsciiColumnGapPx);
    }

    _pxCursorX = _pxPosAsciiX + kAsciiAreaLeftPaddingPx + asciiOffsetPx;

    _asciiCursorRect = QRect(_pxCursorX - horizontalScrollBar()->value() - 2, _pxCursorY - _pxCharHeight + _pxSelectionSub - 6, asciiCursorWidthPx, _pxCharHeight);

    // hex area cursor
    const int hexStridePx = 3 * _pxCharWidth + kHexColumnExtraGapPx;
    _pxCursorX = (x / 2) * hexStridePx + _pxPosHexX;

    _hexCursorRect = QRect(_pxCursorX - horizontalScrollBar()->value() - 2, _pxCursorY - _pxCharHeight + _pxSelectionSub - 4, _pxCharWidth * 2 + 4, _pxCharHeight + 2);

    // 3. Immediately draw new cursor
    viewport()->update(_asciiCursorRect);
    viewport()->update(_hexCursorRect);

    emit currentAddressChanged(_bPosCurrent);
}

qint64 QHexEdit::cursorPosition(QPoint pos)
{
    // Calc cursor position depending on a graphical position
    qint64 result = -1;

    auto posX = pos.x() + horizontalScrollBar()->value();
    auto posY = pos.y() - 3;
    const int rowStridePx = _pxCharHeight + kHexRowExtraGapPx;

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
        int y = (posY / rowStridePx) * 2 * _bytesPerLine;

        result = _bPosFirst * 2 + x + y;
    }
    else if (_asciiArea && (posX >= _pxPosAsciiX) && (posX < (_pxPosAsciiX + kAsciiAreaLeftPaddingPx + static_cast<int>(_asciiAreaMaxWidth))))
    {
        _editAreaIsAscii = true;

        const int row = posY / rowStridePx;
        if (row < 0)
            return -1;

        const int xPx = qMax(0, posX - (_pxPosAsciiX + kAsciiAreaLeftPaddingPx));
        auto y = row * 2 * _bytesPerLine;
        const qint64 rowStart = static_cast<qint64>(row) * _bytesPerLine;
        const qint64 rowEnd = qMin(rowStart + _bytesPerLine, static_cast<qint64>(_dataShown.size()));

        if (rowStart >= rowEnd)
            return -1;

        // Walk slots left-to-right accumulating pixel widths until we hit the click X
        // This correctly handles variable-width translation-table tokens
        int accumulated = 0;
        
        int byteCol = static_cast<int>(rowEnd - rowStart) - 1; // default: last byte in row

        for (int col = 0; col < static_cast<int>(rowEnd - rowStart); ++col)
        {
            const uint8_t bv = static_cast<uint8_t>(_dataShown.at(rowStart + col));
            const int slotW = (_tb && !_tbSymbolWidthPxCache.isEmpty())
                ? (_tbSymbolWidthPxCache[bv] + kAsciiColumnGapPx)
                : (_pxCharWidth + kAsciiColumnGapPx);
            if (xPx < accumulated + slotW)
            {
                byteCol = col;
                break;
            }
            accumulated += slotW;
        }

        result = _bPosFirst * 2 + byteCol * 2 + y;
    }

    return result;
}

qint64 QHexEdit::cursorPosition()
{
    return _cursorPosition;
}

void QHexEdit::setData(const QByteArray &ba)
{
    _data = ba;
    _bData.setData(_data);
    setData(_bData);
}

QByteArray QHexEdit::data()
{
    return _chunks->data(0, -1);
}

QByteArray QHexEdit::getRawSelection()
{
    return _chunks->data(getSelectionBegin(), getSelectionEnd() - getSelectionBegin());
}

Datas QHexEdit::getValue(qint64 offset)
{
    Datas value;

    value.leDword = qFromLittleEndian<quint32_le>(_chunks->data(offset, 4));

    return value;
}

qint64 QHexEdit::getCurrentOffset()
{
    return _bPosCurrent;
}

qint64 QHexEdit::pointerStartAt(qint64 bytePos, int pointerSize)
{
    if (bytePos < 0 || pointerSize <= 0)
        return -1;

    const qint64 startMin = qMax(static_cast<qint64>(0), bytePos - pointerSize + 1);

    for (qint64 candidate = bytePos; candidate >= startMin; --candidate)
    {
        if (_pointers.isPointer(candidate) && bytePos < candidate + pointerSize)
            return candidate;
    }

    return -1;
}

qint64 QHexEdit::pointerTargetAt(qint64 bytePos, int pointerSize)
{
    const qint64 ptrStart = pointerStartAt(bytePos, pointerSize);
    return (ptrStart >= 0) ? _pointers.getOffset(ptrStart) : -1;
}

void QHexEdit::setHighlighting(bool highlighting)
{
    _highlighting = highlighting;
    viewport()->update();
}

bool QHexEdit::highlighting()
{
    return _highlighting;
}

void QHexEdit::setHighlightingColor(const QColor &color)
{
    _brushHighlighted = QBrush(color);
    _penHighlighted = QPen(viewport()->palette().color(QPalette::WindowText));
    viewport()->update();
}

QColor QHexEdit::highlightingColor()
{
    return _brushHighlighted.color();
}

void QHexEdit::setShowPointers(bool show)
{
    _showPointers = show;
    viewport()->update();
}

bool QHexEdit::showPointers()
{
    return _showPointers;
}

void QHexEdit::setPointersColor(const QColor &color)
{
    _brushPointers = QBrush(color);
    _penPointers = QPen(_pointerFontColor);
    if (_scrollMapPtr)
        _scrollMapPtr->setColor(color);
    viewport()->update();
}

QColor QHexEdit::pointersColor()
{
    return _brushPointers.color();
}

void QHexEdit::setPointedColor(const QColor &color)
{
    _brushPointed = QBrush(color);
    _penPointed = QPen(_pointedFontColor);
    if (_scrollMapTarget)
        _scrollMapTarget->setColor(color);
    viewport()->update();
}

QColor QHexEdit::pointedColor()
{
    return _brushPointed.color();
}

void QHexEdit::setPointedFontColor(const QColor &color)
{
    _pointedFontColor = color;
    _penPointed = QPen(_pointedFontColor);
    viewport()->update();
}

QColor QHexEdit::pointedFontColor()
{
    return _pointedFontColor;
}

void QHexEdit::setPointerFontColor(const QColor &color)
{
    _pointerFontColor = color;
    _penPointers = QPen(_pointerFontColor);
    viewport()->update();
}

QColor QHexEdit::pointerFontColor()
{
    return _pointerFontColor;
}

void QHexEdit::setPointerFrameColor(const QColor &color)
{
    _pointerFrameColor = color;
    viewport()->update();
}

QColor QHexEdit::pointerFrameColor()
{
    return _pointerFrameColor;
}

void QHexEdit::setPointerFrameBackgroundColor(const QColor &color)
{
    _pointerFrameBackgroundColor = color;
    viewport()->update();
}

QColor QHexEdit::pointerFrameBackgroundColor()
{
    return _pointerFrameBackgroundColor;
}

void QHexEdit::setOverwriteMode(bool overwriteMode)
{
    _overwriteMode = overwriteMode;
    emit overwriteModeChanged(overwriteMode);
}

bool QHexEdit::overwriteMode()
{
    return _overwriteMode;
}

void QHexEdit::setSelectionColor(const QColor &color)
{
    _brushSelection = QBrush(color);
    _penSelection = QPen(Qt::white);
    viewport()->update();
}

QColor QHexEdit::selectionColor()
{
    return _brushSelection.color();
}

bool QHexEdit::showHexGrid()
{
    return _showHexGrid;
}

void QHexEdit::setShowHexGrid(bool mode)
{
    _showHexGrid = mode;
    viewport()->update();
}

QColor QHexEdit::hexAreaBackgroundColor()
{
    return _hexAreaBackgroundColor;
}

void QHexEdit::setHexAreaBackgroundColor(const QColor &color)
{
    _hexAreaBackgroundColor = color;
    viewport()->update();
}

QColor QHexEdit::hexAreaGridColor()
{
    return _hexAreaGridColor;
}

void QHexEdit::setHexAreaGridColor(const QColor &color)
{
    _hexAreaGridColor = color;
    viewport()->update();
}

bool QHexEdit::isReadOnly()
{
    return _readOnly;
}

void QHexEdit::setReadOnly(bool readOnly)
{
    _readOnly = readOnly;
}

void QHexEdit::setHexCaps(const bool isCaps)
{
    if (_hexCaps != isCaps)
    {
        _hexCaps = isCaps;
        viewport()->update();
    }
}

bool QHexEdit::hexCaps()
{
    return _hexCaps;
}

void QHexEdit::setDynamicBytesPerLine(const bool isDynamic)
{
    _dynamicBytesPerLine = isDynamic;
    resizeEvent(NULL);
}

bool QHexEdit::dynamicBytesPerLine()
{
    return _dynamicBytesPerLine;
}

// ********************************************************************** Access to data of qhexedit
bool QHexEdit::setData(QIODevice &iODevice)
{
    bool ok = _chunks->setIODevice(iODevice);

    init();

    dataChangedPrivate();

    return ok;
}

QByteArray QHexEdit::dataAt(qint64 pos, qint64 count)
{
    return _chunks->data(pos, count);
}

bool QHexEdit::write(QIODevice &iODevice, qint64 pos, qint64 count)
{
    return _chunks->write(iODevice, pos, count);
}

// ********************************************************************** Char handling
void QHexEdit::insert(qint64 index, char ch)
{
    _undoStack->insert(index, ch);
    refresh();
}

void QHexEdit::remove(qint64 index, qint64 len)
{
    _undoStack->removeAt(index, len);
    refresh();
}

void QHexEdit::replace(qint64 index, char ch)
{
    _undoStack->overwrite(index, ch);
    refresh();
}

// ********************************************************************** ByteArray handling
void QHexEdit::insert(qint64 pos, const QByteArray &ba)
{
    _undoStack->insert(pos, ba);
    refresh();
}

void QHexEdit::replace(qint64 pos, qint64 len, const QByteArray &ba)
{
    _undoStack->overwrite(pos, len, ba);
    refresh();
}

// ********************************************************************** Utility functions
void QHexEdit::ensureVisible()
{
    if (_cursorPosition < (_bPosFirst * 2))
        verticalScrollBar()->setValue((int)(_cursorPosition / 2 / _bytesPerLine));

    if (_cursorPosition > ((_bPosFirst + (_rowsShown - 1) * _bytesPerLine) * 2))
        verticalScrollBar()->setValue((int)(_cursorPosition / 2 / _bytesPerLine) - _rowsShown + 1);

    if (_pxCursorX < horizontalScrollBar()->value())
        horizontalScrollBar()->setValue(_pxCursorX);

    if ((_pxCursorX + _pxCharWidth) > (horizontalScrollBar()->value() + viewport()->width()))
        horizontalScrollBar()->setValue(_pxCursorX + _pxCharWidth - viewport()->width());

    viewport()->update();
}

qint64 QHexEdit::indexOf(const QByteArray &ba, qint64 from)
{
    qint64 pos = _chunks->indexOf(ba, from);

    if (pos > -1)
    {
        qint64 curPos = pos * 2;

        setCursorPosition(curPos + ba.length() * 2);
        resetSelection(curPos);
        setSelection(curPos + ba.length() * 2);
        ensureVisible();
    }

    return pos;
}

qint64 QHexEdit::relativeSearch(const QByteArray &ba, qint64 from)
{
    auto buf = data().constData();
    uint8_t coin;
    QByteArray relNeedle;
    auto searchLen = ba.size();

    relNeedle.append('\0');

    for (uint8_t j = 1; j < searchLen; j++)
    {
        relNeedle.append(ba[0] - ba[j]);
    }

    auto maxOffset = data().size() - searchLen;

    for (qint64 i = from; i < maxOffset; i++)
    {
        coin = 1;

        for (uint8_t j = 1; j < searchLen; j++)
        {
            if ((buf[i] - buf[i + j]) != relNeedle[j])
            {
                break;
            }

            coin++;
        }

        if (coin == searchLen)
        {
            qint64 curPos = i * 2;

            setCursorPosition(curPos + searchLen * 2);
            resetSelection(curPos);
            setSelection(curPos + searchLen * 2);
            ensureVisible();

            /*if (!_tb)
                _tb = new TranslationTable();

            _tb->generateTable(QString::fromLatin1(_chunks->data(i, searchLen), searchLen), ba);*/

            return i;
        }
    }

    return -1;
}

void QHexEdit::jumpTo(qint64 offset, bool relative)
{
    auto newPos = qBound(0LL, (relative ? (_cursorPosition / 2) + offset : offset), static_cast<qint64>(data().size()));

    setCursorPosition(newPos * 2);
    resetSelection(_cursorPosition);
    ensureVisible();
}

bool QHexEdit::isModified()
{
    return _modified;
}

bool QHexEdit::canUndo()
{
    return _undoStack->canUndo();
}

bool QHexEdit::canRedo()
{
    return _undoStack->canRedo();
}

qint64 QHexEdit::lastIndexOf(const QByteArray &ba, qint64 from)
{
    qint64 pos = _chunks->lastIndexOf(ba, from);

    if (pos > -1)
    {
        qint64 curPos = pos * 2;
        setCursorPosition(curPos - 1);
        resetSelection(curPos);
        setSelection(curPos + ba.length() * 2);
        ensureVisible();
    }

    return pos;
}

void QHexEdit::redo()
{
    _undoStack->redo();
    setCursorPosition(_chunks->pos() * (_editAreaIsAscii ? 1 : 2));
    refresh();
}

QString QHexEdit::selectionToReadableString()
{
    QByteArray ba = _chunks->data(getSelectionBegin(), getSelectionEnd() - getSelectionBegin());
    return toReadable(ba);
}

QString QHexEdit::selectedData()
{
    QByteArray ba = _chunks->data(getSelectionBegin(), getSelectionEnd() - getSelectionBegin()).toHex();
    return ba;
}

void QHexEdit::setFont(const QFont &font)
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
    invalidateAsciiAreaWidthCache();
    updateAsciiAreaMaxWidth();
    viewport()->update();
}

QString QHexEdit::toReadableString()
{
    QByteArray ba = _chunks->data();
    return toReadable(ba);
}

void QHexEdit::undo()
{
    _undoStack->undo();
    setCursorPosition(_chunks->pos() * (_editAreaIsAscii ? 1 : 2));
    refresh();
}

TranslationTable *QHexEdit::getTranslationTable()
{
    return _tb;
}

void QHexEdit::setTranslationTable(TranslationTable *tb)
{
    // Save viewport position so toggling the table/autosize doesn't jump vertically
    const qint64 savedTopByte = static_cast<qint64>(verticalScrollBar()->value()) * qMax(1, _bytesPerLine);
    const qint64 savedCursorPos = _cursorPosition;
    const int savedHorizontal = horizontalScrollBar()->value();

    _tb = tb;
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

void QHexEdit::removeTranslationTable()
{
    setTranslationTable(); // with no parameters removes translation table
}

// ********************************************************************** Handle events
void QHexEdit::keyPressEvent(QKeyEvent *event)
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

    // Cursor movements
    if (event->matches(QKeySequence::MoveToNextChar))
    {
        qint64 pos = _cursorPosition + 1;

        if (_editAreaIsAscii)
            pos += 1;

        setCursorPosition(pos);
        resetSelection(pos);
    }

    if (event->matches(QKeySequence::MoveToPreviousChar))
    {
        qint64 pos = _cursorPosition - 1;

        if (_editAreaIsAscii)
            pos -= 1;

        setCursorPosition(pos);
        resetSelection(pos);
    }

    if (event->matches(QKeySequence::MoveToEndOfLine))
    {
        qint64 pos = _cursorPosition - (_cursorPosition % (2 * _bytesPerLine)) + (2 * _bytesPerLine) - 1;
        setCursorPosition(pos);
        resetSelection(_cursorPosition);
    }

    if (event->matches(QKeySequence::MoveToStartOfLine))
    {
        qint64 pos = _cursorPosition - (_cursorPosition % (2 * _bytesPerLine));
        setCursorPosition(pos);
        resetSelection(_cursorPosition);
    }

    if (event->matches(QKeySequence::MoveToPreviousLine))
    {
        setCursorPosition(_cursorPosition - (2 * _bytesPerLine));
        resetSelection(_cursorPosition);
    }

    if (event->matches(QKeySequence::MoveToNextLine))
    {
        setCursorPosition(_cursorPosition + (2 * _bytesPerLine));
        resetSelection(_cursorPosition);
    }

    if (event->matches(QKeySequence::MoveToNextPage))
    {
        setCursorPosition(_cursorPosition + (((_rowsShown - 1) * 2 * _bytesPerLine)));
        resetSelection(_cursorPosition);
    }

    if (event->matches(QKeySequence::MoveToPreviousPage))
    {
        setCursorPosition(_cursorPosition - (((_rowsShown - 1) * 2 * _bytesPerLine)));
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
        setCursorPosition(pos);
        setSelection(pos);
    }

    if (event->matches(QKeySequence::SelectPreviousChar))
    {
        qint64 pos = _cursorPosition - 1;
        if (_editAreaIsAscii)
            pos -= 1;
        setSelection(pos);
        setCursorPosition(pos);
    }

    if (event->matches(QKeySequence::SelectEndOfLine))
    {
        qint64 pos = _cursorPosition - (_cursorPosition % (2 * _bytesPerLine)) + (2 * _bytesPerLine) - 1;
        setCursorPosition(pos);
        setSelection(pos);
    }

    if (event->matches(QKeySequence::SelectStartOfLine))
    {
        qint64 pos = _cursorPosition - (_cursorPosition % (2 * _bytesPerLine));
        setCursorPosition(pos);
        setSelection(pos);
    }

    if (event->matches(QKeySequence::SelectPreviousLine))
    {
        qint64 pos = _cursorPosition - (2 * _bytesPerLine);
        setCursorPosition(pos);
        setSelection(pos);
    }

    if (event->matches(QKeySequence::SelectNextLine))
    {
        qint64 pos = _cursorPosition + (2 * _bytesPerLine);
        setCursorPosition(pos);
        setSelection(pos);
    }

    if (event->matches(QKeySequence::SelectNextPage))
    {
        qint64 pos = _cursorPosition + ((_rowsShown - 1) * 2 * _bytesPerLine);
        setCursorPosition(pos);
        setSelection(pos);
    }

    if (event->matches(QKeySequence::SelectPreviousPage))
    {
        qint64 pos = _cursorPosition - ((_rowsShown - 1) * 2 * _bytesPerLine);
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
    if (!_readOnly)
    {
        /* Cut */
        if (event->matches(QKeySequence::Cut))
        {
            const qint64 selBegin = getSelectionBegin();
            const qint64 selEnd = getSelectionEnd();
            const QByteArray raw = _chunks->data(selBegin, selEnd - selBegin);

            if (_editAreaIsAscii)
            {
                // ASCII area: copy raw bytes as Latin-1 text
                QApplication::clipboard()->setText(QString::fromLatin1(raw.constData(), raw.size()));
            }
            else
            {
                // Hex area: space-separated uppercase hex pairs
                QApplication::clipboard()->setText(QString::fromLatin1(raw.toHex(' ')).toUpper());
            }

            // In REPLACE mode, cut only copies without deleting
            if (!_overwriteMode)
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
                QClipboard *clipboard = QApplication::clipboard();
                QByteArray ba;
                if (_editAreaIsAscii)
                {
                    // ASCII area: paste raw bytes (Latin-1 encoding)
                    ba = clipboard->text().toLatin1();
                }
                else
                {
                    // Hex area: strip whitespace then decode hex pairs
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
            }
            else

                /* Delete char */
                if (event->matches(QKeySequence::Delete))
                {
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
                }
                else

                    /* Backspace */
                    if ((event->key() == Qt::Key_Backspace) && (event->modifiers() == Qt::NoModifier))
                    {
                        if (getSelectionEnd() - getSelectionBegin() > 1)
                        {
                            _bPosCurrent = getSelectionBegin();
                            setCursorPosition(2 * _bPosCurrent);

                            if (_overwriteMode)
                            {
                                QByteArray ba = QByteArray(getSelectionEnd() - getSelectionBegin(), char(0));
                                replace(_bPosCurrent, ba.size(), ba);
                            }
                            else
                            {
                                remove(_bPosCurrent, getSelectionEnd() - getSelectionBegin());
                            }
                            resetSelection(2 * _bPosCurrent);
                        }
                        else
                        {
                            bool behindLastByte = false;
                            if ((_cursorPosition / 2) == _chunks->size())
                                behindLastByte = true;

                            _bPosCurrent -= 1;
                            if (_overwriteMode)
                                replace(_bPosCurrent, char(0));
                            else
                                remove(_bPosCurrent, 1);

                            if (!behindLastByte)
                                _bPosCurrent -= 1;

                            setCursorPosition(2 * _bPosCurrent);
                            resetSelection(2 * _bPosCurrent);
                        }
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
                    else if (event->text() != "")
                    {
                        /* Hex and ascii input */
                        auto key = _editAreaIsAscii ? event->text().at(0) : event->text().at(0).toLower().toLatin1();

                        // Filter hex input
                        if ((((key >= '0' && key <= '9') || (key >= 'a' && key <= 'f')) && _editAreaIsAscii == false) || (key >= ' ' && _editAreaIsAscii))
                        {
                            if (getSelectionBegin() != getSelectionEnd())
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
                                char ch = key.toLatin1();

                                if (!_editAreaIsAscii)
                                {
                                    QByteArray hexValue = _chunks->data(_bPosCurrent, 1).toHex();

                                    if ((_cursorPosition % 2) == 0)
                                        hexValue[0] = ch;
                                    else
                                        hexValue[1] = ch;

                                    ch = QByteArray().fromHex(hexValue)[0];
                                }
                                else
                                {
                                    if (_tb)
                                    {
                                        ch = _tb->decodeSymbol(key);
                                    }
                                }

                                replace(_bPosCurrent, ch);

                                if (_editAreaIsAscii)
                                    setCursorPosition(_cursorPosition + 2);
                                else
                                    setCursorPosition(_cursorPosition + 1);

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
        const QByteArray raw = _chunks->data(selBegin, selEnd - selBegin);

        // Always copy raw bytes (ASCII area behavior), regardless of which area cursor is in
        QApplication::clipboard()->setText(QString::fromLatin1(raw.constData(), raw.size()));
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

void QHexEdit::mouseMoveEvent(QMouseEvent *event)
{
    _blink = false;
    viewport()->update();
    qint64 actPos = cursorPosition(event->pos());

    if (actPos >= 0)
    {
        setCursorPosition(actPos);
        setSelection(actPos);
    }
}

bool QHexEdit::viewportEvent(QEvent *event)
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
                    ? QStringLiteral("0x%1").arg(ptrs[0], 8, 16, QChar('0')).toUpper()
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

void QHexEdit::mousePressEvent(QMouseEvent *event)
{
    _blink = false;
    viewport()->update();

    qint64 cPos = cursorPosition(event->pos());

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

void QHexEdit::mouseDoubleClickEvent(QMouseEvent *event)
{
    Q_UNUSED(event)

    if (_showPointers)
    {
        const qint64 ptrStart = pointerStartAt(_bPosCurrent, kPointerByteSize);

        // jump to pointed offset
        if (ptrStart >= 0)
        {
            _editAreaIsAscii = true;
            setCursorPosition(_pointers.getOffset(ptrStart) * 2);
            ensureVisible();
        }
        // jump to pointer
        else if (_pointers.hasOffset(_bPosCurrent))
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
        }
    }
}

void QHexEdit::contextMenuEvent(QContextMenuEvent *event)
{
    emit contextMenuRequested(event->globalPos(), _bPosCurrent);
}

void QHexEdit::paintEvent(QPaintEvent *event)
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
        int hexAreaWidth = _pxPosAsciiX - _pxPosHexX;
        painter.fillRect(QRect(_pxPosHexX - pxOfsX, event->rect().top(), hexAreaWidth - (_pxGapHexAscii / 2), height()), _hexAreaBackgroundColor);

        painter.setPen(viewport()->palette().color(QPalette::WindowText));

        // paint address area
        if (_addressArea)
        {
            QString address;

            auto rowsCount = (_dataShown.size() / _bytesPerLine);

            painter.fillRect(QRect(-pxOfsX, event->rect().top(), _pxPosHexX - _pxGapAdrHex, height()), _addressAreaColor);

            const QFont originalFont = painter.font();
            QFont boldFont = originalFont;
            boldFont.setBold(true);
            painter.setFont(boldFont);

            for (int row = 0, pxPosY = _pxCharHeight; row <= rowsCount; row++, pxPosY += rowStridePx)
            {
                address = QString("%1").arg(_bPosFirst + row * _bytesPerLine + _addressOffset, _addrDigits, 16, QChar('0'));
                painter.setPen(QPen(_addressFontColor));
                painter.drawText(_pxPosAdrX - pxOfsX, pxPosY, hexCaps() ? address.toUpper() : address);
            }

            painter.setFont(originalFont);
        }

        // paint hex and ascii area
        painter.setBackgroundMode(Qt::TransparentMode);

        if (_asciiArea)
        {
            painter.fillRect(QRect(_pxPosAsciiX - pxOfsX, event->rect().top(), width(), height()), _asciiAreaColor);

            int linePos = _pxPosAsciiX - (_pxGapHexAscii / 2);
            painter.setPen(Qt::gray);
            painter.drawLine(linePos - pxOfsX, event->rect().top(), linePos - pxOfsX, height());

            ensureAsciiAreaWidthCache();
        }

        const int hexStridePx = 3 * _pxCharWidth + kHexColumnExtraGapPx;

        for (int row = 0; row < _rowsShown; row++)
        {
            QByteArray hex;
            int pxPosY = pxPosStartY + row * rowStridePx;
            int pxPosX = _pxPosHexX - pxOfsX;
            int pxPosAsciiX2 = _pxPosAsciiX + kAsciiAreaLeftPaddingPx - pxOfsX;
            qint64 bPosLine = row * _bytesPerLine;

            // can be slow here
            for (int colIdx = 0; ((bPosLine + colIdx) < _dataShown.size() && (colIdx < _bytesPerLine)); colIdx++)
            {
                QColor c = viewport()->palette().color(QPalette::Base);
                painter.setPen(QPen(_hexFontColor));

                qint64 posBa = _bPosFirst + bPosLine + colIdx;
                const qint64 pointerStart = _showPointers ? pointerStartAt(posBa, kPointerByteSize) : -1;
                const bool isPointerByte = pointerStart >= 0;
                const bool isPointedByte = _showPointers && _pointers.hasOffset(posBa);
                const bool isSelectedByte = (getSelectionBegin() <= posBa) && (getSelectionEnd() > posBa);
                const bool isHighlightedByte = _highlighting && _markedShown.at((int)(posBa - _bPosFirst));

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
                }

                // POINTERS
                if (_showPointers)
                {
                    // cursor image for pointed data
                    if (isPointedByte)
                    {
                        QImage ptrIcon = QImage(":/images/pointer.png");

                        if (!isSelectedByte && !isHighlightedByte)
                            c = _brushPointed.color();

                        if (!isSelectedByte)
                            painter.setPen(_penPointed);

                        painter.drawImage(pxPosX - _pxCharWidth - 2, pxPosY - (_pxCharHeight / 2), ptrIcon, 0, 0, 10, 10);
                    }

                    if (isPointerByte && !isSelectedByte && !isHighlightedByte && !isPointedByte)
                        painter.setPen(_penPointers);
                    
                    if (isPointerByte)
                    {
                        // Draw pointer frame only at the first byte of the pointer on each row
                        // The frame is clipped to the current line so it doesn't bleed into ASCII area
                        const int colInLine = static_cast<int>(posBa % _bytesPerLine);
                        const int ptrStartCol = static_cast<int>(pointerStart % _bytesPerLine);

                        // We draw a partial frame segment on every row that the pointer occupies
                        // Determine how many bytes of this pointer are on the current row starting from colIdx
                        const int ptrEndByteExcl = static_cast<int>((pointerStart - _bPosFirst) + kPointerByteSize);
                        const int rowEndCol = _bytesPerLine; // exclusive

                        if (posBa == pointerStart || colInLine == 0)
                        {
                            // First pointer byte on this row — draw frame segment
                            const int bytesOnThisRow = qMin(ptrEndByteExcl - static_cast<int>(bPosLine + colIdx), rowEndCol - colInLine);

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
                                        const int slotW = (_tb && !_tbSymbolWidthPxCache.isEmpty())
                                            ? (_tbSymbolWidthPxCache[rowByte] + kAsciiColumnGapPx)
                                            : (_pxCharWidth + kAsciiColumnGapPx);
                                        asciiFrameWidth += slotW;
                                    }

                                    if (asciiFrameWidth > 0)
                                    {
                                        auto asciiFrame = QRect(asciiStartX - 4,
                                                                pxPosY - _pxCharHeight + _pxSelectionSub - 3,
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

                // render hex value
                auto r = QRect(pxPosX - 1, pxPosY - _pxCharHeight + _pxSelectionSub, 2 * _pxCharWidth + 2, _pxCharHeight + 1);

                // Only fill background if there's actual highlighting/selection (not just base color)
                if (c != viewport()->palette().color(QPalette::Base))
                    painter.fillRect(r, c);

                // Overlay cursor-char highlight on the byte under cursor
                const bool isCursorByte = (bPosLine + colIdx) == (_cursorPosition / 2 - _bPosFirst);
                
                if (isCursorByte && _cursorCharColor.alpha() > 0)
                    painter.fillRect(r, _cursorCharColor);

                hex = _hexDataShown.mid((bPosLine + colIdx) * 2, 2);

                // In hex area: draw the active nibble of the cursor byte in bold
                if (isCursorByte && !_editAreaIsAscii)
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
                pxPosX += hexStridePx;

                // render ascii value
                if (_asciiArea)
                {
                    if (c == viewport()->palette().color(QPalette::Base))
                        c = _asciiAreaColor;

                    char rawByte = _dataShown.at(bPosLine + colIdx);
                    QChar ch = QChar::fromLatin1(rawByte);

                    QString sym = _tb ? _tb->encodeSymbol(rawByte) : ch;

                    if (_tb)
                    {
                        if (!sym.size())
                            sym = QString(_notInTableChar);
                    }
                    else if (sym.size() == 1 && sym[0].toLatin1() < 0x20)
                    {
                        sym = QString(_nonPrintableNoTableChar);
                    }
                    
                    const uint8_t byteValue = static_cast<uint8_t>(rawByte);
                    
                    // Use cached per-byte width when a translation table is active
                    // fall back to single char width otherwise
                    const int baseSymWidthPx = (_tb && !_tbSymbolWidthPxCache.isEmpty())
                        ? _tbSymbolWidthPxCache[byteValue]
                        : _pxCharWidth;

                    const int symWidthPx = baseSymWidthPx + kAsciiColumnGapPx;

                    r.setRect(pxPosAsciiX2 - 1, pxPosY - _pxCharHeight + _pxSelectionSub - 2, symWidthPx, _pxCharHeight);
                    
                    if (c != _asciiAreaColor)
                        painter.fillRect(r, c);

                    if (isCursorByte && _cursorCharColor.alpha() > 0)
                        painter.fillRect(r, _cursorCharColor);

                    if (isSelectedByte)
                        painter.setPen(_penSelection);
                    else if (isHighlightedByte)
                        painter.setPen(_penHighlighted);
                    else if (isPointedByte)
                        painter.setPen(_penPointed);
                    else if (isPointerByte)
                        painter.setPen(_penPointers);
                    else
                        painter.setPen(QPen(_asciiFontColor));
                    // Draw text into the full slot (no clip) so multi-char tokens are visible
                    painter.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, sym);

                    if (_tb && (_cursorPosition - 2 * _bPosFirst) / 2 == (bPosLine + colIdx))
                        _asciiCursorRect = r;

                    pxPosAsciiX2 += symWidthPx;
                }
            }
        }

        painter.setBackgroundMode(Qt::TransparentMode);
        painter.setPen(viewport()->palette().color(QPalette::WindowText));
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

            painter.drawRect(_hexCursorRect);

            if (_asciiArea)
                painter.drawRect(_asciiCursorRect);

            painter.setPen(QPen(_hexFontColor));
        }
    }

    // emit event, if size has changed
    if (_lastEventSize != _chunks->size())
    {
        _lastEventSize = _chunks->size();
        emit currentSizeChanged(_lastEventSize);
    }

}

void QHexEdit::resizeEvent(QResizeEvent *)
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
            const int pxPosAsciiX = pxPosHexX + candidateHexCharsInLine * _pxCharWidth + (candidateBpl - 1) * kHexColumnExtraGapPx + _pxGapHexAscii;
            const int asciiWidthPx = _asciiArea ? static_cast<int>(computeAsciiAreaMaxWidthForBytesPerLine(candidateBpl)) : 0;
            const int requiredWidthPx = _asciiArea ? (pxPosAsciiX + kAsciiAreaLeftPaddingPx + asciiWidthPx) : pxPosAsciiX;

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

bool QHexEdit::focusNextPrevChild(bool next)
{
    if (_addressArea)
    {
        if ((next && _editAreaIsAscii) || (!next && !_editAreaIsAscii))
            return QWidget::focusNextPrevChild(next);
        else
            return false;
    }
    else
    {
        return QWidget::focusNextPrevChild(next);
    }
}

// ********************************************************************** Handle selections
bool QHexEdit::hasSelection()
{
    return _bSelectionEnd - _bSelectionBegin > 1;
}

void QHexEdit::resetSelection()
{
    _bSelectionBegin = _bSelectionInit;
    _bSelectionEnd = _bSelectionInit;

    emit selectionChanged(_bSelectionBegin, _bSelectionEnd);
}

void QHexEdit::resetSelection(qint64 pos)
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

void QHexEdit::setSelection(qint64 pos)
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

qint64 QHexEdit::getSelectionBegin()
{
    return _bSelectionBegin;
}

qint64 QHexEdit::getSelectionEnd()
{
    return _bSelectionEnd;
}

// ********************************************************************** Private utility functions
void QHexEdit::init()
{
    _undoStack->clear();
    setAddressOffset(0);
    resetSelection(0);
    setCursorPosition(0);
    verticalScrollBar()->setValue(0);

    _modified = false;
}

void QHexEdit::adjust()
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
    int pxWidth = _pxPosAsciiX;

    if (_asciiArea)
        pxWidth += (kAsciiAreaLeftPaddingPx + static_cast<int>(_asciiAreaMaxWidth));

    horizontalScrollBar()->setRange(0, pxWidth - viewport()->width());
    horizontalScrollBar()->setPageStep(viewport()->width());

    // set verticalScrollbar()
    const int rowStridePx = _pxCharHeight + kHexRowExtraGapPx;
    _rowsShown = ((viewport()->height() - 4) / rowStridePx);

    auto lineCount = (_chunks->size() / _bytesPerLine) + 1;

    verticalScrollBar()->setRange(0, lineCount - _rowsShown);
    verticalScrollBar()->setPageStep(_rowsShown);

    auto value = verticalScrollBar()->value();

    _bPosFirst = value * _bytesPerLine;
    _bPosLast = _bPosFirst + (_rowsShown * _bytesPerLine) - 1;

    if (_bPosLast >= _chunks->size())
        _bPosLast = _chunks->size() - 1;

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
            if (_scrollMapPtr && _scrollMapPtr->isVisible())
            {
                _scrollMapPtr->setGeometry(x, vp.top(), kScrollMapWidth, vp.height());
                x += kScrollMapWidth;
            }
            if (_scrollMapTarget && _scrollMapTarget->isVisible())
            {
                _scrollMapTarget->setGeometry(x, vp.top(), kScrollMapWidth, vp.height());
            }
        }
    }
}

void QHexEdit::dataChangedPrivate(int)
{
    _modified = _undoStack->index() != 0;

    invalidateAsciiAreaWidthCache();

    updateAsciiAreaMaxWidth();

    adjust();

    emit dataChanged();

    emit undoAvailable(_undoStack->canUndo());
    emit redoAvailable(_undoStack->canRedo());
}

void QHexEdit::refresh()
{
    ensureVisible();
    readBuffers();
}

uint32_t QHexEdit::computeAsciiAreaMaxWidthForBytesPerLine(int bytesPerLine)
{
    if (!_asciiArea || bytesPerLine <= 0)
        return 0;

    // When a table is loaded use its max symbol width, otherwise one char per byte.
    const int slotWidthPx = (_tb && _tbMaxSymbolWidthPx > 0)
        ? (_tbMaxSymbolWidthPx + kAsciiColumnGapPx)
        : (_pxCharWidth + kAsciiColumnGapPx);

    return static_cast<uint32_t>(bytesPerLine * slotWidthPx);
}

void QHexEdit::updateAsciiAreaMaxWidth()
{
    if (_tb)
        ensureAsciiAreaWidthCache();

    _asciiAreaMaxWidth = computeAsciiAreaMaxWidthForBytesPerLine(_bytesPerLine);
}

void QHexEdit::restoreTopVisibleByte(qint64 topByte)
{
    if (_bytesPerLine <= 0)
        return;

    const int topLine = static_cast<int>(qMax<qint64>(0, topByte) / _bytesPerLine);
    verticalScrollBar()->setValue(topLine);
    adjust();
}

void QHexEdit::invalidateAsciiAreaWidthCache()
{
    _asciiAreaWidthCacheValid = false;
    _tbMaxSymbolWidthPx = 0;
    _asciiAreaWidthCacheTable = nullptr;
    _asciiAreaWidthCacheDataSize = -1;
    _asciiAreaWidthCacheCharWidth = 0;
}

void QHexEdit::ensureAsciiAreaWidthCache()
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
            if (sym.size() == 1 && sym[0].toLatin1() < 0x20)
                sym = QString(_nonPrintableNoTableChar);
        }
        
        int widthPx = sym.size() * _pxCharWidth;
        _tbSymbolWidthPxCache[value] = widthPx;
        if (widthPx > _tbMaxSymbolWidthPx)
            _tbMaxSymbolWidthPx = widthPx;
    }

    for (int bpl = 4; bpl <= 64; bpl += 4)
        _asciiAreaMaxWidthByBpl[bpl] = static_cast<uint32_t>(bpl * (_tbMaxSymbolWidthPx + kAsciiColumnGapPx));

    _asciiAreaWidthCacheTable = _tb;
    _asciiAreaWidthCacheDataSize = -1;
    _asciiAreaWidthCacheCharWidth = _pxCharWidth;
    _asciiAreaWidthCacheValid = true;
}

void QHexEdit::readBuffers()
{
    _dataShown = _chunks->data(_bPosFirst, _bPosLast - _bPosFirst + _bytesPerLine + 1, &_markedShown);
    _hexDataShown = QByteArray(_dataShown.toHex());
}

QString QHexEdit::toReadable(const QByteArray &ba)
{
    QString result;

    auto baLen = ba.size();

    for (int i = 0; i < baLen; i += 16)
    {
        QString addrStr = QString("%1").arg(_addressOffset + i, addressWidth(), 16, QChar('0'));
        QString hexStr;
        QString ascStr;

        for (int j = 0; j < 16; j++)
        {
            if ((i + j) < baLen)
            {
                hexStr.append(" ").append(ba.mid(i + j, 1).toHex());
                char ch = ba[i + j];
                if ((ch < 0x20) || (ch > 0x7e))
                    ch = '.';
                ascStr.append(QChar::fromLatin1(ch));
            }
        }

        result += addrStr + " " + QString("%1").arg(hexStr, -48) + "  " + QString("%1").arg(ascStr, -17) + "\n";
    }
    return result;
}

void QHexEdit::updateCursor()
{
    _blink = !_blink;

    viewport()->update(_asciiCursorRect);
    viewport()->update(_hexCursorRect);
}

// ---- Scroll map visibility API -------------------------------------------------

bool QHexEdit::scrollMapPtrVisible() const
{
    return _scrollMapPtrEnabled;
}

void QHexEdit::setScrollMapPtrVisible(bool visible)
{
    if (_scrollMapPtrEnabled == visible) return;
    _scrollMapPtrEnabled = visible;
    updateScrollMapMargins();
}

bool QHexEdit::scrollMapTargetVisible() const
{
    return _scrollMapTargetEnabled;
}

void QHexEdit::setScrollMapTargetVisible(bool visible)
{
    if (_scrollMapTargetEnabled == visible) return;
    _scrollMapTargetEnabled = visible;
    updateScrollMapMargins();
}

void QHexEdit::setScrollMapPtrBgColor(const QColor &c)
{
    if (_scrollMapPtr) _scrollMapPtr->setBgColor(c);
}

void QHexEdit::setScrollMapTargetBgColor(const QColor &c)
{
    if (_scrollMapTarget) _scrollMapTarget->setBgColor(c);
}

void QHexEdit::updateScrollMapMargins()
{
    // Immediate (non-debounced) re-evaluation of visibility + margins.
    // Also kicks the debounce timer for future model changes.
    scheduleScrollMapCompute();
    updateScrollMap();
}

// ---- Scroll map computation ---------------------------------------------------

void QHexEdit::updateScrollMap()
{
    // Debounce: restart timer on every rapid model change.
    // scheduleScrollMapCompute() fires once things settle.
    if (_scrollMapTimer)
        _scrollMapTimer->start();
}

void QHexEdit::scheduleScrollMapCompute()
{
    if (!_scrollMapWatcher || !_chunks)
        return;

    const bool hasPointers = !_pointers.empty();
    const bool wantPtr    = _scrollMapPtrEnabled    && hasPointers && _scrollMapPtr;
    const bool wantTarget = _scrollMapTargetEnabled && hasPointers && _scrollMapTarget;

    // Update strip visibility
    if (_scrollMapPtr)    _scrollMapPtr->setVisible(wantPtr);
    if (_scrollMapTarget) _scrollMapTarget->setVisible(wantTarget);
    if (wantPtr)    _scrollMapPtr->raise();
    if (wantTarget) _scrollMapTarget->raise();

    // Update viewport right margin only when it actually changes (avoids recursive relayout)
    const int newMargin = (wantPtr ? kScrollMapWidth : 0) + (wantTarget ? kScrollMapWidth : 0);
    if (newMargin != _scrollMapCurrentMargin)
    {
        _scrollMapCurrentMargin = newMargin;
        setViewportMargins(0, 0, newMargin, 0);
        // setViewportMargins relayouts the viewport synchronously in Qt6,
        // so we call adjust() immediately to size the strip before reading mapH.
    }
    adjust();  // always reposition strips so height() is up-to-date

    if (!wantPtr && !wantTarget)
    {
        if (_scrollMapPtr)    _scrollMapPtr->setTicks({});
        if (_scrollMapTarget) _scrollMapTarget->setTicks({});
        return;
    }

    // If previous background task still running, retry after debounce
    if (_scrollMapWatcher->isRunning())
    {
        _scrollMapTimer->start();
        return;
    }

    // Both strips share the same height (set by adjust()).
    // Read from viewport() directly — adjust() has just positioned the strips,
    // but reading viewport()->height() is always authoritative and avoids any
    // stale-geometry edge cases the first time the strips are shown.
    const int mapH = viewport()->height();
    const qint64 totalBytes = _chunks->size();

    if (mapH <= 0 || totalBytes <= 0)
        return;

    // Get real groove geometry via QStyleOptionSlider (initFrom is public).
    auto *vbar = verticalScrollBar();
    const int sbMin  = vbar->minimum();
    const int sbMax  = vbar->maximum();
    const int sbPage = vbar->pageStep();
    const int bytesPerLine = qMax(1, _bytesPerLine);

    // Build QStyleOptionSlider from public scrollbar API — no initStyleOption needed.
    // Use strip dimensions as the rect so subControlRect returns groove/thumb in strip coords.
    QStyleOptionSlider vbarOpt;
    vbarOpt.initFrom(vbar);                         // sets palette, state from the widget
    vbarOpt.rect          = QRect(0, 0, kScrollMapWidth, mapH);  // ← strip size, not vbar size
    vbarOpt.minimum       = sbMin;
    vbarOpt.maximum       = sbMax;
    vbarOpt.pageStep      = sbPage;
    vbarOpt.singleStep    = vbar->singleStep();
    vbarOpt.sliderValue   = vbar->value();
    vbarOpt.sliderPosition = vbar->sliderPosition();
    vbarOpt.orientation   = Qt::Vertical;
    vbarOpt.upsideDown    = vbar->invertedAppearance();
    vbarOpt.subControls   = QStyle::SC_All;
    vbarOpt.activeSubControls = QStyle::SC_None;

    // Groove rect — in strip coords because we set opt.rect to strip size.
    const QRect grooveRect = vbar->style()->subControlRect(
        QStyle::CC_ScrollBar, &vbarOpt, QStyle::SC_ScrollBarGroove, vbar);

    // Slider (thumb) rect — also in strip coords.
    const QRect sliderRect = vbar->style()->subControlRect(
        QStyle::CC_ScrollBar, &vbarOpt, QStyle::SC_ScrollBarSlider, vbar);

    const int grooveTop = grooveRect.isValid() ? grooveRect.top() : 0;
    const int grooveH   = qMax(1, grooveRect.isValid() ? grooveRect.height() : mapH);
    const int mThumbH   = qMax(1, sliderRect.isValid() ? sliderRect.height() : 1);

    // mGrooveTop/mGrooveH are already in strip (mapH) coords — no scaling needed.
    const int mGrooveTop = grooveTop;
    const int mGrooveH   = grooveH;

    QList<qint64> ptrKeys    = wantPtr    ? _pointers.pointerKeys() : QList<qint64>{};
    QList<qint64> targetKeys = wantTarget ? _pointers.offsetKeys()  : QList<qint64>{};

    auto future = QtConcurrent::run(
        [mapH, totalBytes,
         sbMin, sbMax, sbPage, bytesPerLine,
         mGrooveTop, mGrooveH, mThumbH,
         wantPtr, wantTarget,
         ptrKeys    = std::move(ptrKeys),
         targetKeys = std::move(targetKeys)]() -> ScrollMapMarkers
        {
            ScrollMapMarkers result;

            // Map byte offset → Y that matches the scrollbar thumb CENTER,
            // using real groove geometry obtained from QStyle on the main thread.
            auto offToY = [mapH, totalBytes,
                           sbMin, sbMax, sbPage, bytesPerLine,
                           mGrooveTop, mGrooveH, mThumbH](qint64 off) -> int
            {
                if (totalBytes <= 1 || mapH <= 1)
                    return 0;

                const qint64 offClamped = qBound<qint64>(0, off, totalBytes - 1);
                const double line = static_cast<double>(offClamped) / static_cast<double>(bytesPerLine);

                // Scrollbar value that centers this offset in the viewport.
                const double value = qBound(static_cast<double>(sbMin),
                                            line - static_cast<double>(sbPage) * 0.5,
                                            static_cast<double>(sbMax));

                // Thumb travels within groove: from mGrooveTop to mGrooveTop+mGrooveH-mThumbH.
                const double travel = qMax(0.0, static_cast<double>(mGrooveH - mThumbH));
                const double valueRange = (sbMax > sbMin) ? static_cast<double>(sbMax - sbMin) : 1.0;
                const double normVal = (value - sbMin) / valueRange;

                // Y of the thumb CENTER in strip coordinates.
                const double thumbTopInStrip = mGrooveTop + normVal * travel;
                const double thumbCenterY = thumbTopInStrip + static_cast<double>(mThumbH) * 0.5;

                return qBound(0, static_cast<int>(std::round(thumbCenterY)), mapH - 1);
            };

            if (wantPtr)
            {
                QVector<bool> px(mapH, false);
                for (qint64 off : ptrKeys) {
                    int y = offToY(off);
                    px[y] = true;
                    if (!result.ptrYToOff.contains(y))
                        result.ptrYToOff.insert(y, off);
                }
                for (int i = 0; i < mapH; ++i)
                    if (px[i]) result.ptrYs.push_back(i);
            }

            if (wantTarget)
            {
                QVector<bool> px(mapH, false);
                for (qint64 off : targetKeys) {
                    int y = offToY(off);
                    px[y] = true;
                    if (!result.targetYToOff.contains(y))
                        result.targetYToOff.insert(y, off);
                }
                for (int i = 0; i < mapH; ++i)
                    if (px[i]) result.targetYs.push_back(i);
            }

            return result;
        });

    _scrollMapWatcher->setFuture(future);
}

PointerListModel *QHexEdit::pointers()
{
    return &_pointers;
}

void QHexEdit::clearPointers()
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
qint64 QHexEdit::findPointers(int pointerSize, ByteOrder order, bool searchBefore, bool searchAfter, const char *firstPrintable, const char *lastPrintable, char stopChar, bool excludeSelection)
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

bool QHexEdit::addPointerUndoable(qint64 pointerOffset, qint64 targetOffset)
{
    if (pointerOffset < 0 || targetOffset < 0)
        return false;

    const PointerState before = capturePointerState(&_pointers, pointerOffset);

    if (before.hasTarget && before.targetOffset == targetOffset)
        return false;

    PointerState after;
    after.pointerOffset = pointerOffset;
    after.hasTarget = true;
    after.targetOffset = targetOffset;

    _undoStack->push(new PointerEditCommand(&_pointers,
                                            QVector<PointerState>{before},
                                            QVector<PointerState>{after},
                                            tr("Add pointer")));
    refresh();
    return true;
}

bool QHexEdit::removePointerUndoable(qint64 pointerOffset)
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

int QHexEdit::removePointersUndoable(const QVector<qint64> &pointerOffsets)
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

int QHexEdit::removePointersToOffsetUndoable(qint64 targetOffset)
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

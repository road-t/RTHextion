#include <QtGlobal>
#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QPainter>
#include <QScrollBar>
#include <QToolTip>
#include <QProgressDialog>

#include "qhexedit.h"
#include <algorithm>

// ********************************************************************** Constructor, destructor

QHexEdit::QHexEdit(QWidget *parent) : QAbstractScrollArea(parent)
    , _addressArea(true)
    , _addressWidth(4)
    , _asciiArea(true)
    , _bytesPerLine(32)
    , _hexCharsInLine(47)
    , _highlighting(true)
    , _overwriteMode(true)
    , _readOnly(false)
    , _showPointers(true)
    , _hexCaps(true)
    , _dynamicBytesPerLine(true)
    , _editAreaIsAscii(true)
    , _chunks(new Chunks(this))
    , _cursorPosition(0)
    , _lastEventSize(0)
    , _undoStack(new UndoStack(_chunks, this))
{
#ifdef Q_OS_WIN32
    setFont(QFont("Courier", 14));
#else
    setFont(QFont("monospace", 14));
#endif

    auto font = QFont("monospace", 16, 10);
    QToolTip::setFont(font);

    setAddressAreaColor(this->palette().alternateBase().color());
    setHighlightingColor(QColor(0xff, 0xff, 0x99, 0xff));
    setPointersColor(QColor(0xff, 0x99, 0x00, 0xff));
    setPointedColor(QColor(0x99, 0xff, 0x00, 0xff));
    setSelectionColor(this->palette().highlight().color());
    setAddressFontColor(QPalette::WindowText);
    setAsciiAreaColor(this->palette().alternateBase().color());
    setAsciiFontColor(QPalette::WindowText);

    connect(&_cursorTimer, SIGNAL(timeout()), this, SLOT(updateCursor()));
    connect(verticalScrollBar(), SIGNAL(valueChanged(int)), this, SLOT(adjust()));
    connect(horizontalScrollBar(), SIGNAL(valueChanged(int)), this, SLOT(adjust()));
    connect(_undoStack, SIGNAL(indexChanged(int)), this, SLOT(dataChangedPrivate(int)));

//    _cursorTimer.setInterval(500);
//    _cursorTimer.start();

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

    if (size > Q_INT64_C(0x100000000)) { n += 8; size /= Q_INT64_C(0x100000000);}
    if (size > 0x10000) { n += 4; size /= 0x10000;}
    if (size > 0x100) { n += 2; size /= 0x100;}
    if (size > 0x10) { n += 1; }

    if (n > _addressWidth)
        return n;
    else
        return _addressWidth;
}

void QHexEdit::setAsciiArea(bool asciiArea)
{
    if (!asciiArea)
        _editAreaIsAscii = false;
    _asciiArea = asciiArea;
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
        position = _chunks->size() * 2  - (_overwriteMode ? 1 : 0);

    if (position < 0)
        position = 0;

    // 2. Calc new position of cursor
    _bPosCurrent = position / 2;
    auto line = ((_bPosCurrent - _bPosFirst) / _bytesPerLine + 1);
    _pxCursorY = line * _pxCharHeight;
    auto x = (position % (2 * _bytesPerLine));

    _cursorPosition = position;

    // ascii area cursor
    _pxCursorX = x / 2 * _pxCharWidth + _pxPosAsciiX;

//    _cursorPosition = position & 0xFFFFFFFFFFFFFFFE;

    _asciiCursorRect = QRect(_pxCursorX - horizontalScrollBar()->value(), _pxCursorY - _pxCharHeight + _pxSelectionSub, _pxCharWidth, _pxCharHeight);

    // hex area cursor
    _pxCursorX = ((x / 2) * 3) * _pxCharWidth + _pxPosHexX;

    _hexCursorRect = QRect(_pxCursorX - horizontalScrollBar()->value(), _pxCursorY - _pxCharHeight + _pxSelectionSub, _pxCharWidth * 2, _pxCharHeight);

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

    if ((posX >= _pxPosHexX) && (posX < (_pxPosHexX + (1 + _hexCharsInLine) * _pxCharWidth)))
    {
        _editAreaIsAscii = false;
        int x = (posX - _pxPosHexX) / _pxCharWidth;
        x = (x / 3) * 2 + x % 3;
        int y = (posY / _pxCharHeight) * 2 * _bytesPerLine;

        result = _bPosFirst * 2 + x + y;
    }
    else if (_asciiArea && (posX >= _pxPosAsciiX) && (posX < (_pxPosAsciiX + _asciiAreaMaxWidth)))
    {
        _editAreaIsAscii = true;

        auto x = (posX - _pxPosAsciiX) / _pxCharWidth;
        auto y = (posY / _pxCharHeight) * 2 * _bytesPerLine;


        if (_tb)
        {
            uint16_t lineWidth = 0;
            auto starting = (posY / _pxCharHeight) * _bytesPerLine;
            uint8_t symbolsOffset = 0;

            while (lineWidth <= x)
            {
                QString sym = _tb->encodeSymbol(_dataShown.at(starting++));
                lineWidth += sym.size();
                symbolsOffset++;
            }

            x = ((symbolsOffset <= _bytesPerLine) ? symbolsOffset : _bytesPerLine) - 1;
        }


        result = _bPosFirst * 2 + x * 2 + y;
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
    _penPointers = QPen(viewport()->palette().color(QPalette::WindowText));
    viewport()->update();
}

QColor QHexEdit::pointersColor()
{
    return _brushPointers.color();
}

void QHexEdit::setPointedColor(const QColor &color)
{
    _brushPointed = QBrush(color);
    _penPointed = QPen(viewport()->palette().color(QPalette::WindowText));
    viewport()->update();
}

QColor QHexEdit::pointedColor()
{
    return _brushPointed.color();
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

    if (_cursorPosition > ((_bPosFirst + (_rowsShown - 1)*_bytesPerLine) * 2))
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
    auto newPos = qBound(0, (relative ? (_cursorPosition / 2) + offset : offset), data().size());

    setCursorPosition(newPos * 2);
    resetSelection();
    ensureVisible();
}

bool QHexEdit::isModified()
{
    return _modified;
}

qint64 QHexEdit::lastIndexOf(const QByteArray &ba, qint64 from)
{
    qint64 pos = _chunks->lastIndexOf(ba, from);

    if (pos > -1)
    {
        qint64 curPos = pos*2;
        setCursorPosition(curPos - 1);
        resetSelection(curPos);
        setSelection(curPos + ba.length()*2);
        ensureVisible();
    }

    return pos;
}

void QHexEdit::redo()
{
    _undoStack->redo();
    setCursorPosition(_chunks->pos()*(_editAreaIsAscii ? 1 : 2));
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
#if QT_VERSION > QT_VERSION_CHECK(5, 11, 0)
    _pxCharWidth = metrics.horizontalAdvance(QLatin1Char('2'));
#else
    _pxCharWidth = metrics.width(QLatin1Char('2'));
#endif
    _pxCharHeight = metrics.height();
    _pxGapAdr = _pxCharWidth / 2;
    _pxGapAdrHex = _pxCharWidth;
    _pxGapHexAscii = 2 * _pxCharWidth;
    _pxCursorWidth = _pxCharHeight / 7;
    _pxSelectionSub = _pxCharHeight / 5;
    _asciiAreaMaxWidth = _pxCharWidth * _bytesPerLine;
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
    setCursorPosition(_chunks->pos()*(_editAreaIsAscii ? 1 : 2));
    refresh();
}

TranslationTable* QHexEdit::getTranslationTable()
{
    return _tb;
}

void QHexEdit::setTranslationTable(TranslationTable* tb)
{
    _tb = tb;

    viewport()->update();
    //ensureVisible();
    adjust();
}

void QHexEdit::removeTranslationTable()
{
    _asciiAreaMaxWidth = _bytesPerLine * _pxCharWidth;
    horizontalScrollBar()->setValue(0);
    setTranslationTable(); // with no parameters removes translation table
}

// ********************************************************************** Handle events
void QHexEdit::keyPressEvent(QKeyEvent *event)
{
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
        setCursorPosition(_chunks->size() * 2 );
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
        qint64 pos = _cursorPosition + (((viewport()->height() / _pxCharHeight) - 1) * 2 * _bytesPerLine);
        setCursorPosition(pos);
        setSelection(pos);
    }

    if (event->matches(QKeySequence::SelectPreviousPage))
    {
        qint64 pos = _cursorPosition - (((viewport()->height() / _pxCharHeight) - 1) * 2 * _bytesPerLine);
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
            QByteArray ba = _chunks->data(getSelectionBegin(), getSelectionEnd() - getSelectionBegin()).toHex();

            auto baLen = ba.size();

            for (qint64 idx = 32; idx < baLen; idx += 33)
                ba.insert(idx, "\n");

            QClipboard *clipboard = QApplication::clipboard();
            clipboard->setText(ba);

            if (_overwriteMode)
            {
                qint64 len = getSelectionEnd() - getSelectionBegin();
                replace(getSelectionBegin(), (int)len, QByteArray((int)len, char(0)));
            }
            else
            {
                remove(getSelectionBegin(), getSelectionEnd() - getSelectionBegin());
            }

            setCursorPosition(2 * getSelectionBegin());
            resetSelection(2 * getSelectionBegin());
        }
        else

        /* Paste */
        if (event->matches(QKeySequence::Paste))
        {
            QClipboard *clipboard = QApplication::clipboard();
            QByteArray ba = QByteArray().fromHex(clipboard->text().toLatin1());

            if (_overwriteMode)
            {
                ba = ba.left(std::min<qint64>(ba.size(), (_chunks->size() - _bPosCurrent)));
                replace(_bPosCurrent, ba.size(), ba);
            }
            else
                insert(_bPosCurrent, ba);

            setCursorPosition(_cursorPosition + 2 * ba.size());
            resetSelection(getSelectionBegin());
        }
        else

        /* Delete char */
        if (event->matches(QKeySequence::Delete))
        {
            if (getSelectionBegin() != getSelectionEnd())
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
            if (getSelectionBegin() != getSelectionEnd())
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
            if ((((key >= '0' && key <= '9') || (key >= 'a' && key <= 'f')) && _editAreaIsAscii == false)
                || (key >= ' ' && _editAreaIsAscii))
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
        QByteArray ba = _chunks->data(getSelectionBegin(), getSelectionEnd() - getSelectionBegin()).toHex();

        auto baLen = ba.size();

        for (qint64 idx = 32; idx < baLen; idx +=33)
            ba.insert(idx, "\n");

        QClipboard *clipboard = QApplication::clipboard();
        clipboard->setText(ba);
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
    if (event->key() == Qt::Key_Backtab  && _editAreaIsAscii)
    {
        _editAreaIsAscii = false;
        setCursorPosition(_cursorPosition);
    }

    refresh();
    QAbstractScrollArea::keyPressEvent(event);
}

void QHexEdit::mouseMoveEvent(QMouseEvent * event)
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

void QHexEdit::mousePressEvent(QMouseEvent * event)
{
    _blink = false;
    viewport()->update();

    qint64 cPos = cursorPosition(event->pos());

    if (cPos >= 0)
    {
        if (event->button() != Qt::RightButton && event->modifiers() != Qt::ShiftModifier)
            resetSelection(cPos);

        setCursorPosition(cPos);
        setSelection(cPos);

        if (_showPointers)
        {
            if (_pointers.isPointer(_bPosCurrent))
            {
                if (event->button() == Qt::RightButton)
                {
                    _pointers.dropPointer(_bPosCurrent);
                }
                else
                {
                    QToolTip::showText(mapToGlobal(event->pos()), _pointers.getOffsetText(_bPosCurrent));
                }
            }
            else if (_pointers.hasOffset(_bPosCurrent))
            {
                if (event->button() == Qt::RightButton)
                {
                    _pointers.dropOffset(_bPosCurrent);
                }
                else
                {
                    auto ptrs = _pointers.getPointers(_bPosCurrent);

                    if (ptrs.size() == 1)
                    {
                        QToolTip::showText(mapToGlobal(event->pos()), QString("0x%1").arg(ptrs[0], 8, 16, QChar('0')));
                    }
                    else
                    {
                        QToolTip::showText(mapToGlobal(event->pos()), "...<list of pointers>...");
                    }
                }
            }
        }
    }
}

void QHexEdit::mouseDoubleClickEvent(QMouseEvent * event)
{
    if (_showPointers)
    {
        // jump to pointed offset
        if (_pointers.isPointer(_bPosCurrent))
        {
            _editAreaIsAscii = true;
            setCursorPosition(_pointers.getOffset(_bPosCurrent) * 2);
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

void QHexEdit::paintEvent(QPaintEvent *event)
{
    QPainter painter(viewport());
    auto pxOfsX = horizontalScrollBar()->value();

    if (event->rect() != _asciiCursorRect && event->rect() != _hexCursorRect)
    {
        int pxPosStartY = _pxCharHeight;

        // draw some patterns if needed
        painter.fillRect(event->rect(), viewport()->palette().color(QPalette::Base));

        if (_addressArea)
            painter.fillRect(QRect(-pxOfsX, event->rect().top(), _pxPosHexX - _pxGapAdrHex/2, height()), _addressAreaColor);

        if (_asciiArea)
        {
            int linePos = _pxPosAsciiX - (_pxGapHexAscii / 2);
            painter.setPen(Qt::gray);
            painter.drawLine(linePos - pxOfsX, event->rect().top(), linePos - pxOfsX, height());
        }

        painter.setPen(viewport()->palette().color(QPalette::WindowText));

        // paint address area
        if (_addressArea)
        {
            QString address;

            auto rowsCount = (_dataShown.size() / _bytesPerLine);

            for (int row = 0, pxPosY = _pxCharHeight; row <= rowsCount; row++, pxPosY +=_pxCharHeight)
            {
                address = QString("%1").arg(_bPosFirst + row * _bytesPerLine + _addressOffset, _addrDigits, 16, QChar('0'));
                painter.setPen(QPen(_addressFontColor));
                painter.drawText(_pxPosAdrX - pxOfsX, pxPosY, hexCaps() ? address.toUpper() : address);
            }
        }

        // paint hex and ascii area
        //QPen colStandard = QPen(viewport()->palette().color(QPalette::WindowText));

        painter.setBackgroundMode(Qt::TransparentMode);

        _asciiAreaMaxWidth = _pxCharWidth * _bytesPerLine; // gotta calculate the longest ascii row visible

        for (int row = 0, pxPosY = pxPosStartY; row <= _rowsShown; row++, pxPosY +=_pxCharHeight)
        {
            QByteArray hex;

            uint32_t pxPosX = _pxPosHexX  - pxOfsX;
            uint32_t pxPosAsciiX2 = _pxPosAsciiX  - pxOfsX;
            qint64 bPosLine = row * _bytesPerLine;

            // WARNING: probably slow here
            for (int colIdx = 0; ((bPosLine + colIdx) < _dataShown.size() && (colIdx < _bytesPerLine)); colIdx++)
            {
                QColor c = viewport()->palette().color(QPalette::Base);
                painter.setPen(QPen(_hexFontColor));

                qint64 posBa = _bPosFirst + bPosLine + colIdx;

                if ((getSelectionBegin() <= posBa) && (getSelectionEnd() > posBa))
                {
                    c = _brushSelection.color();
                    painter.setPen(_penSelection);
                }
                else
                {
                    if (_highlighting)
                    {
                        if (_markedShown.at((int)(posBa - _bPosFirst)))
                        {
                            c = _brushHighlighted.color();
                            painter.setPen(_penHighlighted);
                        }
                    }
                }

                if (_showPointers)
                {
                    if (_pointers.hasOffset(posBa))
                    {
                        QImage ptrIcon = QImage(":/images/pointer.png");

                        c = _brushPointed.color();
                        painter.setPen(_penPointed);

                        painter.drawImage(pxPosX - _pxCharWidth, pxPosY - (_pxCharHeight / 2 + 3), ptrIcon, 0, 0, 10, 10);
                    }

                    if (_pointers.isPointer(posBa))
                    {
                        QPen pen;
                        pen.setColor(QColor(0, 0, 0xff));
                        pen.setWidth(2);

                        painter.setPen(pen);

                        c = _brushPointers.color();

                        auto frame = QRect(pxPosX, pxPosY - _pxCharHeight + _pxSelectionSub + 1, 11 * _pxCharWidth, _pxCharHeight - _pxSelectionSub);

                        painter.drawRect(frame);
                        painter.fillRect(frame, QColor(0, 0xFF, 0, 0x80));
                    }
                }

                // render hex value
                auto r = QRect(pxPosX, pxPosY - _pxCharHeight + _pxSelectionSub, 2 * _pxCharWidth, _pxCharHeight);

                painter.fillRect(r, c);

                hex = _hexDataShown.mid((bPosLine + colIdx) * 2, 2);
                painter.drawText(pxPosX, pxPosY, hexCaps() ? hex.toUpper() : hex);
                pxPosX += 3 * _pxCharWidth;

                // render ascii value
                if (_asciiArea)
                {
                    if (c == viewport()->palette().color(QPalette::Base) || c == _brushPointers.color()) // don't highlight pointers in ASCII area
                        c = _asciiAreaColor;

                    QChar ch = _dataShown.at(bPosLine + colIdx);

                    r.setRect(pxPosAsciiX2, pxPosY - _pxCharHeight + _pxSelectionSub, _pxCharWidth, _pxCharHeight);
                    painter.fillRect(r, c);

                    QString sym = _tb ? _tb->encodeSymbol(ch.toLatin1()) : ch;

                    if (!_tb && sym.size() == 1 && sym[0].toLatin1() < 0x20)
                        sym = '.';

                    painter.setPen(QPen(_asciiFontColor));
                    painter.drawText(pxPosAsciiX2, pxPosY, sym);
                    pxPosAsciiX2 += _pxCharWidth * sym.size();

                    // if it has a cursor on
                    if (_tb && (_cursorPosition - 2 * _bPosFirst) / 2 == (bPosLine + colIdx))
                    {
                        _asciiCursorRect.setWidth(_pxCharWidth * sym.size());
                        _asciiCursorRect.moveRight(pxPosAsciiX2);
                    }
                }
            }

            if (_tb)
                _asciiAreaMaxWidth = qMax(_asciiAreaMaxWidth, pxPosAsciiX2);
        }

        painter.setBackgroundMode(Qt::TransparentMode);
        painter.setPen(viewport()->palette().color(QPalette::WindowText));
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

            pen.setWidth(2);
            painter.setPen(pen);

            painter.drawRect(_hexCursorRect);

            //pen.setColor(QColor(0xFF, 0, 0));
            //painter.setPen(pen);

            painter.drawRect(_asciiCursorRect);
            painter.setPen(QPen(_hexFontColor));
        }

/*
        if (_editAreaIsAscii)
        {
            // every 2 hex there is 1 ascii
            int asciiPositionInShowData = hexPositionInShowData / 2;
            int ch = (uchar)_dataShown.at(asciiPositionInShowData);

            if (ch < ' ' || ch > '~')
                ch = '.';

            painter.setPen(QPen(_penHighlighted));
            painter.drawText(_pxCursorX - pxOfsX, _pxCursorY, QChar(ch));
        }
        else
        {
            painter.drawText(_pxCursorX - pxOfsX, _pxCursorY, hexCaps() ? _hexDataShown.mid(hexPositionInShowData, 1).toUpper() : _hexDataShown.mid(hexPositionInShowData, 1));
        }*/
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
    if (_dynamicBytesPerLine && !_tb)
    {
        int pxFixGaps = 0;
        if (_addressArea)
            pxFixGaps = addressWidth() * _pxCharWidth + _pxGapAdr;

        pxFixGaps += _pxGapAdrHex;

        if (_asciiArea)
            pxFixGaps += _pxGapHexAscii;

        // +1 because the last hex value do not have space. so it is effective one char more
        int charWidth = (viewport()->width() - pxFixGaps ) / _pxCharWidth + 1;

        // 2 hex alfa-digits 1 space 1 ascii per byte = 4; if ascii is disabled then 3
        // to prevent devision by zero use the min value 1
        setBytesPerLine(std::max(charWidth / (_asciiArea ? 4 : 3),1));
    }

    adjust();
}

bool QHexEdit::focusNextPrevChild(bool next)
{
    if (_addressArea)
    {
        if ( (next && _editAreaIsAscii) || (!next && !_editAreaIsAscii ))
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
    return _bSelectionBegin != _bSelectionEnd;
}

void QHexEdit::resetSelection()
{
    _bSelectionBegin = _bSelectionInit;
    _bSelectionEnd = _bSelectionInit;

    emit selectionChanged(_bSelectionBegin, _bSelectionEnd);
}

void QHexEdit::resetSelection(qint64 pos)
{
    pos = pos / 2 ;
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
        _bSelectionEnd = pos;
        _bSelectionBegin = _bSelectionInit;
    }
    else
    {
        _bSelectionBegin = pos;
        _bSelectionEnd = _bSelectionInit;
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
        _pxPosHexX = _pxGapAdr + _addrDigits * _pxCharWidth + _pxGapAdrHex;
    }
    else
        _pxPosHexX = _pxGapAdrHex;

    _pxPosAdrX = _pxGapAdr;
    _pxPosAsciiX = _pxPosHexX + _hexCharsInLine * _pxCharWidth + _pxGapHexAscii;

    // set horizontalScrollBar()
    int pxWidth = _pxPosAsciiX;

    if (_asciiArea)
        pxWidth += _asciiAreaMaxWidth;

    horizontalScrollBar()->setRange(0, pxWidth - viewport()->width());
    horizontalScrollBar()->setPageStep(viewport()->width());

    // set verticalScrollbar()
    _rowsShown = ((viewport()->height() - 4) /_pxCharHeight);

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
}

void QHexEdit::dataChangedPrivate(int)
{
    _modified = _undoStack->index() != 0;
    adjust();
    emit dataChanged();
}

void QHexEdit::refresh()
{
    ensureVisible();
    readBuffers();
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

    for (int i=0; i < baLen; i += 16)
    {
        QString addrStr = QString("%1").arg(_addressOffset + i, addressWidth(), 16, QChar('0'));
        QString hexStr;
        QString ascStr;

        for (int j=0; j<16; j++)
        {
            if ((i + j) < baLen)
            {
                hexStr.append(" ").append(ba.mid(i+j, 1).toHex());
                char ch = ba[i + j];
                if ((ch < 0x20) || (ch > 0x7e))
                        ch = '.';
                ascStr.append(QChar(ch));
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

PointerListModel* QHexEdit::pointers()
{
    return &_pointers;
}

void QHexEdit::clearPointers()
{
    _pointers.clear();

    viewport()->update();
}

qint64 QHexEdit::findPointers(bool bigEndian, bool searchBefore, bool searchAfter, const char* firstPrintable, const char* lastPrintable, char stopChar)
{
    auto buf = data().constData();

    uint64_t found = 0, its = 0;

    union off
    {
        uint32_t i;
        char b[4];
    };

    off offset;

    QProgressDialog progress(this->parentWidget());
    progress.setMinimum(_bSelectionBegin);
    progress.setMaximum(_bSelectionEnd);
    progress.setAutoClose(true);
    progress.setWindowModality(Qt::WindowModal);
    progress.setCancelButtonText(tr("Stop"));
    progress.setLabelText(tr("Searching for pointers..."));

    progress.show();

    QElapsedTimer timer;

    timer.start();

    uint64_t ptrsStart = searchBefore ? 0 : _bSelectionEnd;
    uint64_t ptrsEnd = searchAfter ? _chunks->size() : _bSelectionBegin;

    for (offset.i = _bSelectionBegin; offset.i < _bSelectionEnd; offset.i++) // pointers
    {
        // skip areas that start with printable characters to increase search speed
        if (firstPrintable && lastPrintable && offset.i)
        {
            auto prevChar = buf[offset.i - 1];

            if (prevChar >= *firstPrintable && prevChar <= *lastPrintable)
                continue;
        }

        for (uint64_t j = ptrsStart; j < ptrsEnd; j++) // data
        {
            bool match = (bigEndian) ?
                    (buf[j] == offset.b[3] && buf[j + 1] == offset.b[2] && buf[j + 2] == offset.b[1] && buf[j + 3] == offset.b[0]) :
                    *(reinterpret_cast<const uint32_t*>(&buf[j])) == offset.i;

            if (match)
            {
                _pointers.addPointer(j, offset.i);
                found++;
            }

            its++;
        }


        if (progress.wasCanceled())
            break;

        if (offset.i % 100 == 0)
        {
            progress.setValue(offset.i);
            progress.setLabelText(tr("Searching for pointers...\nFound: %1").arg(found));
        }
    }

    progress.close();

    qDebug() << found << " pointers found in " << its << " iterations, " << timer.elapsed() << "ms elapsed";

    return found;
}

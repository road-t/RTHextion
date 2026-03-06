#ifndef QHEXEDIT_H
#define QHEXEDIT_H

#include <QAbstractScrollArea>
#include <QPen>
#include <QBrush>
#include <QVector>

class QContextMenuEvent;

#include "chunks.h"
#include "commands.h"
#include "translationtable.h"
#include "../Datas.h"
#include "../PointerListModel.h"
#include "hexscrollmap.h"

#include <QFutureWatcher>
#include <QTimer>

#ifdef QHEXEDIT_EXPORTS
#define QHEXEDIT_API Q_DECL_EXPORT
#elif QHEXEDIT_IMPORTS
#define QHEXEDIT_API Q_DECL_IMPORT
#else
#define QHEXEDIT_API
#endif

#define QT_NO_CAST_FROM_ASCII

/** \mainpage
QHexEdit is a binary editor widget for Qt.

\version Version 0.8.9
\image html qhexedit.png
*/


/** QHexEdit is a hex editor widget written in C++ for the Qt (Qt4, Qt5) framework.
It is a simple editor for binary data, just like QPlainTextEdit is for text
data. There are sip configuration files included, so it is easy to create
bindings for PyQt and you can use this widget also in python 2 and 3.

QHexEdit takes the data of a QByteArray (setData()) and shows it. You can use
the mouse or the keyboard to navigate inside the widget. If you hit the keys
(0..9, a..f) you will change the data. Changed data is highlighted and can be
accessed via data().

Normally QHexEdit works in the overwrite mode. You can set overwrite mode(false)
and insert data. In this case the size of data() increases. It is also possible
to delete bytes (del or backspace), here the size of data decreases.

You can select data with keyboard hits or mouse movements. The copy-key will
copy the selected data into the clipboard. The cut-key copies also but deletes
it afterwards. In overwrite mode, the paste function overwrites the content of
the (does not change the length) data. In insert mode, clipboard data will be
inserted. The clipboard content is expected in ASCII Hex notation. Unknown
characters will be ignored.

QHexEdit comes with undo/redo functionality. All changes can be undone, by
pressing the undo-key (usually ctr-z). They can also be redone afterwards.
The undo/redo framework is cleared, when setData() sets up a new
content for the editor. You can search data inside the content with indexOf()
and lastIndexOf(). The replace() function is to change located subdata. This
'replaced' data can also be undone by the undo/redo framework.

QHexEdit is based on QIODevice, that's why QHexEdit can handle big amounts of
data. The size of edited data can be more then two gigabytes without any
restrictions.
*/

class QHEXEDIT_API QHexEdit : public QAbstractScrollArea
{
    Q_OBJECT

    /*! Property address area switch the address area on or off. Set addressArea true
    (show it), false (hide it).
    */
    Q_PROPERTY(bool addressArea READ addressArea WRITE setAddressArea)

    /*! Property address area color sets (setAddressAreaColor()) the background
    color of address areas. You can also read the color (addressAreaColor()).
    */
    Q_PROPERTY(QColor addressAreaColor READ addressAreaColor WRITE setAddressAreaColor)

    /*! Property address font color sets (setAddressFontColor()) the text
    color of address areas. You can also read the color (addressFontColor()).
    */
    Q_PROPERTY(QColor addressFontColor READ addressFontColor WRITE setAddressFontColor)

    /*! Property ascii area color sets (setAsciiAreaColor()) the backgorund
    color of ascii areas. You can also read the color (asciiAreaColor()).
    */
    Q_PROPERTY(QColor asciiAreaColor READ asciiAreaColor WRITE setAsciiAreaColor)

    /*! Property ascii font color sets (setAsciiFontColor()) the text
    color of ascii areas. You can also read the color (asciiFontColor()).
    */
    Q_PROPERTY(QColor asciiFontColor READ asciiFontColor WRITE setAsciiFontColor)

    /*! Property hex font color sets (setHexFontColor()) the text
    color of hex areas. You can also read the color (hexFontColor()).
    */
    Q_PROPERTY(QColor hexFontColor READ hexFontColor WRITE setHexFontColor)

    /*! Property addressOffset is added to the Numbers of the Address Area.
    A offset in the address area (left side) is sometimes useful, whe you show
    only a segment of a complete memory picture. With setAddressOffset() you set
    this property - with addressOffset() you get the current value.
    */
    Q_PROPERTY(qint64 addressOffset READ addressOffset WRITE setAddressOffset)

    /*! Set and get the minimum width of the address area, width in characters.
    */
    Q_PROPERTY(int addressWidth READ addressWidth WRITE setAddressWidth)

    /*! Switch the ascii area on (true, show it) or off (false, hide it).
    */
    Q_PROPERTY(bool asciiArea READ asciiArea WRITE setAsciiArea)

    /*! Set and get bytes number per line.*/
    Q_PROPERTY(int bytesPerLine READ bytesPerLine WRITE setBytesPerLine)

    /*! Property cursorPosition sets or gets the position of the editor cursor
    in QHexEdit. Every byte in data has two cursor positions: the lower and upper
    Nibble. Maximum cursor position is factor two of data.size().
    */
    Q_PROPERTY(qint64 cursorPosition READ cursorPosition WRITE setCursorPosition)

    /*! Property data holds the content of QHexEdit. Call setData() to set the
    content of QHexEdit, data() returns the actual content. When calling setData()
    with a QByteArray as argument, QHexEdit creates a internal copy of the data
    If you want to edit big files please use setData(), based on QIODevice.
    */
    Q_PROPERTY(QByteArray data READ data WRITE setData NOTIFY dataChanged)

    /*! That property defines if the hex values looks as a-f if the value is false(default)
    or A-F if value is true.
    */
    Q_PROPERTY(bool hexCaps READ hexCaps WRITE setHexCaps)

    /*! Property defines the dynamic calculation of bytesPerLine parameter depends of width of widget.
    set this property true to avoid horizontal scrollbars and show the maximal possible data. defalut value is false*/
    Q_PROPERTY(bool dynamicBytesPerLine READ dynamicBytesPerLine WRITE setDynamicBytesPerLine)

    /*! Switch the highlighting feature on or of: true (show it), false (hide it).
    */
    Q_PROPERTY(bool highlighting READ highlighting WRITE setHighlighting)

    /*! Property highlighting color sets (setHighlightingColor()) the background
    color of highlighted text areas. You can also read the color
    (highlightingColor()).
    */
    Q_PROPERTY(QColor highlightingColor READ highlightingColor WRITE setHighlightingColor)

    /*! Property overwrite mode sets (setOverwriteMode()) or gets (overwriteMode()) the mode
    in which the editor works. In overwrite mode the user will overwrite existing data. The
    size of data will be constant. In insert mode the size will grow, when inserting
    new data.
    */
    Q_PROPERTY(bool overwriteMode READ overwriteMode WRITE setOverwriteMode)

    /*! Property selection color sets (setSelectionColor()) the background
    color of selected text areas. You can also read the color
    (selectionColor()).
    */
    Q_PROPERTY(QColor selectionColor READ selectionColor WRITE setSelectionColor)

    /*! Property readOnly sets (setReadOnly()) or gets (isReadOnly) the mode
    in which the editor works. In readonly mode the the user can only navigate
    through the data and select data; modifying is not possible. This
    property's default is false.
    */
    Q_PROPERTY(bool readOnly READ isReadOnly WRITE setReadOnly)

    /*! Property showHexGrid switches the hex area grid on or off.
     */
    Q_PROPERTY(bool showHexGrid READ showHexGrid WRITE setShowHexGrid)

    /*! Property hex area background color sets (setHexAreaBackgroundColor()) the background
    color of the hex area. You can also read the color (hexAreaBackgroundColor()).
    */
    Q_PROPERTY(QColor hexAreaBackgroundColor READ hexAreaBackgroundColor WRITE setHexAreaBackgroundColor)

    /*! Property hex area grid color sets (setHexAreaGridColor()) the color
    of the hex area grid lines. You can also read the color (hexAreaGridColor()).
    */
    Q_PROPERTY(QColor hexAreaGridColor READ hexAreaGridColor WRITE setHexAreaGridColor)

    /*! Property cursor char color is used to highlight the byte/character currently
    under the cursor. You can set it (setCursorCharColor()) and read it (cursorCharColor()).
    */
    Q_PROPERTY(QColor cursorCharColor READ cursorCharColor WRITE setCursorCharColor)

    /*! Property cursor frame color is used for the border around the byte/character
    currently under the cursor.
    */
    Q_PROPERTY(QColor cursorFrameColor READ cursorFrameColor WRITE setCursorFrameColor)

    /*! Set the font of the widget. Please use fixed width fonts like Mono or Courier.*/
    Q_PROPERTY(QFont font READ font WRITE setFont)

public:
    /*! Creates an instance of QHexEdit.
    \param parent Parent widget of QHexEdit.
    */
    QHexEdit(QWidget *parent=0);

    // Access to data of qhexedit

    /*! Sets the data of QHexEdit. The QIODevice will be opened just before reading
    and closed immediately afterwards. This is to allow other programs to rewrite
    the file while editing it.
    */
    bool setData(QIODevice &iODevice);

    /*! Gives back the data as a QByteArray starting at position \param pos and
    delivering \param count bytes.
    */
    QByteArray dataAt(qint64 pos, qint64 count=-1);

    /*! Gives back the data into a \param iODevice starting at position \param pos
    and delivering \param count bytes.
    */
    bool write(QIODevice &iODevice, qint64 pos=0, qint64 count=-1);


    // Char handling

    /*! Inserts a char.
    \param pos Index position, where to insert
    \param ch Char, which is to insert
    The char will be inserted and size of data grows.
    */
    void insert(qint64 pos, char ch);

    /*! Removes len bytes from the content.
    \param pos Index position, where to remove
    \param len Amount of bytes to remove
    */
    void remove(qint64 pos, qint64 len=1);

    /*! Replaces a char.
    \param pos Index position, where to overwrite
    \param ch Char, which is to insert
    The char will be overwritten and size remains constant.
    */
    void replace(qint64 pos, char ch);


    // ByteArray handling

    /*! Inserts a byte array.
    \param pos Index position, where to insert
    \param ba QByteArray, which is to insert
    The QByteArray will be inserted and size of data grows.
    */
    void insert(qint64 pos, const QByteArray &ba);

    /*! Replaces \param len bytes with a byte array \param ba
    \param pos Index position, where to overwrite
    \param ba QByteArray, which is inserted
    \param len count of bytes to overwrite
    The data is overwritten and size of data may change.
    */
    void replace(qint64 pos, qint64 len, const QByteArray &ba);


    // Utility functions
    /*! Calc cursor position from graphics position
     * \param point from where the cursor position should be calculated
     * \return Cursor position
     */
    qint64 cursorPosition(QPoint point);

    /*! Ensure the cursor to be visbile
     */
    void ensureVisible();

    /*! Find first occurrence of ba in QHexEdit data
     * \param ba Data to find
     * \param from Point where the search starts
     * \return pos if fond, else -1
     */
    qint64 indexOf(const QByteArray &ba, qint64 from);

    /*! Returns if any changes where done on document
     * \return true when document is modified else false
     */
    bool isModified();

    bool canUndo();
    bool canRedo();

    /*! Find last occurrence of ba in QHexEdit data
     * \param ba Data to find
     * \param from Point where the search starts
     * \return pos if found, else -1
     */
    qint64 lastIndexOf(const QByteArray &ba, qint64 from);

    /*! Peform a relative text search useful for finding encodings
     * \param ba Data to find
     * \return pos if found, else -1
     */
    qint64 relativeSearch(const QByteArray &ba, qint64 from);

    void jumpTo(qint64 offset, bool relative = false);

    /*! Gives back a formatted image of the selected content of QHexEdit
    */
    QString selectionToReadableString();

    /*! Return the selected content of QHexEdit as QByteArray
    */
    QString selectedData();

    /*! Set Font of QHexEdit
     * \param font
     */
    void setFont(const QFont &font);

    /*! Gives back a formatted image of the content of QHexEdit
    */
    QString toReadableString();


public slots:
    /*! Redoes the last operation. If there is no operation to redo, i.e.
      there is no redo step in the undo/redo history, nothing happens.
      */
    void redo();

    /*! Undoes the last operation. If there is no operation to undo, i.e.
      there is no undo step in the undo/redo history, nothing happens.
      */
    void undo();

signals:

    /*! Contains the address, where the cursor is located. */
    void currentAddressChanged(qint64 address);

    /*! Contains the size of the data to edit. */
    void currentSizeChanged(qint64 size);

    /*! The signal is emitted every time, the data is changed. */
    void dataChanged();

    /*! The signal is emitted every time, the overwrite mode is changed. */
    void overwriteModeChanged(bool state);

    /*! The signal is emitted when selection is chenged. */
    void selectionChanged(qint64 start, qint64 end);

    /*! The signal is emitted when undo availability changes. */
    void undoAvailable(bool available);

    /*! The signal is emitted when redo availability changes. */
    void redoAvailable(bool available);

    /*! The signal is emitted when the right mouse button is pressed and the
        context menu should be shown. The point is in global screen coordinates.
        bytePos is the byte offset under the cursor at the time of the click. */
    void contextMenuRequested(const QPoint &globalPos, qint64 bytePos);

    /*! \cond docNever */
public:
    ~QHexEdit();

    // Properties
    bool addressArea();
    void setAddressArea(bool addressArea);

    QColor addressAreaColor();
    void setAddressAreaColor(const QColor &color);

    QColor addressFontColor();
    void setAddressFontColor(const QColor &color);

    QColor asciiAreaColor();
    void setAsciiAreaColor(const QColor &color);

    QColor asciiFontColor();
    void setAsciiFontColor(const QColor &color);

    QChar nonPrintableNoTableChar() const;
    void setNonPrintableNoTableChar(const QChar &ch);

    QChar notInTableChar() const;
    void setNotInTableChar(const QChar &ch);

    QColor cursorCharColor();
    void setCursorCharColor(const QColor &color);

    QColor cursorFrameColor();
    void setCursorFrameColor(const QColor &color);

    QColor hexFontColor();
    void setHexFontColor(const QColor &color);

    qint64 addressOffset();
    void setAddressOffset(qint64 addressArea);

    int addressWidth();
    void setAddressWidth(int addressWidth);

    bool asciiArea();
    void setAsciiArea(bool asciiArea);

    int bytesPerLine();
    void setBytesPerLine(int count);

    qint64 cursorPosition();
    void setCursorPosition(qint64 position);

    QByteArray data();
    void setData(const QByteArray &ba);

    PointerListModel* pointers();
    QMap<qint64, qint64>* pointed();
    void clearPointers();
    void dropPointer(const qint64 offset);
    void dropPointed(const qint64 offset);
    void addPointer(const qint64 offset, qint64 val, char stopChar);

    qint64 getPointerOfsset(qint64 dataOffset);
    qint64 pointerStartAt(qint64 bytePos, int pointerSize = 4);
    qint64 pointerTargetAt(qint64 bytePos, int pointerSize = 4);
    bool addPointerUndoable(qint64 pointerOffset, qint64 targetOffset);
    bool removePointerUndoable(qint64 pointerOffset);
    int removePointersUndoable(const QVector<qint64> &pointerOffsets);
    int removePointersToOffsetUndoable(qint64 targetOffset);

    qint64 findPointers(int pointerSize = 4, ByteOrder order = ByteOrder::LittleEndian, bool searchBefore = true, bool searchAfter = false, const char *firstPrintable = nullptr, const char *lastPrintable = nullptr, char stopChar = 0, bool excludeSelection = false);

    void setHexCaps(const bool isCaps);
    bool hexCaps();

    void setDynamicBytesPerLine(const bool isDynamic);
    bool dynamicBytesPerLine();

    bool highlighting();
    void setHighlighting(bool mode);

    QColor highlightingColor();
    void setHighlightingColor(const QColor &color);

    bool showPointers();
    void setShowPointers(bool mode);

    QColor pointersColor();
    void setPointersColor(const QColor &color);

    QColor pointedColor();
    void setPointedColor(const QColor &color);

    QColor pointedFontColor();
    void setPointedFontColor(const QColor &color);

    QColor pointerFontColor();
    void setPointerFontColor(const QColor &color);

    QColor pointerFrameColor();
    void setPointerFrameColor(const QColor &color);

    /** Show/hide the pointer-storage scroll map strip (orange). */
    bool scrollMapPtrVisible() const;
    void setScrollMapPtrVisible(bool visible);

    /** Show/hide the pointer-target scroll map strip (sky-blue). */
    bool scrollMapTargetVisible() const;
    void setScrollMapTargetVisible(bool visible);

    /** Set background color of each scroll map strip. */
    void setScrollMapPtrBgColor(const QColor &c);
    void setScrollMapTargetBgColor(const QColor &c);

    QColor pointerFrameBackgroundColor();
    void setPointerFrameBackgroundColor(const QColor &color);

    TranslationTable* getTranslationTable();
    void setTranslationTable(TranslationTable* tb = nullptr);
    void removeTranslationTable();

    bool overwriteMode();
    void setOverwriteMode(bool overwriteMode);

    bool isReadOnly();
    void setReadOnly(bool readOnly);

    /*! Returns true if the cursor / last click was in the ASCII area, false if in the hex area. */
    bool editAreaIsAscii() const { return _editAreaIsAscii; }

    QColor selectionColor();
    void setSelectionColor(const QColor &color);

    bool showHexGrid();
    void setShowHexGrid(bool mode);

    QColor hexAreaBackgroundColor();
    void setHexAreaBackgroundColor(const QColor &color);

    QColor hexAreaGridColor();
    void setHexAreaGridColor(const QColor &color);

    bool hasSelection();
    void resetSelection();                      // set selectionEnd to selectionStart
    qint64 getSelectionBegin();
    qint64 getSelectionEnd();

    QByteArray getRawSelection();
    Datas getValue(qint64 offset);
    qint64 getCurrentOffset();

    ByteOrder byteOrder = ByteOrder::LittleEndian;

protected:
    // Handle events
    void keyPressEvent(QKeyEvent *event);
    void mouseMoveEvent(QMouseEvent * event);
    void mousePressEvent(QMouseEvent * event);
    void mouseDoubleClickEvent(QMouseEvent * event);
    void paintEvent(QPaintEvent *event);
    void resizeEvent(QResizeEvent *);
    void contextMenuEvent(QContextMenuEvent *event) override;
    virtual bool focusNextPrevChild(bool next);

private:
    // Handle selections
    void resetSelection(qint64 pos);            // set selectionStart and selectionEnd to pos
    void setSelection(qint64 pos);              // set min (if below init) or max (if greater init)

    // Private utility functions
    void init();
    void readBuffers();
    QString toReadable(const QByteArray &ba);
    uint32_t computeAsciiAreaMaxWidthForBytesPerLine(int bytesPerLine);
    void updateAsciiAreaMaxWidth();
    void invalidateAsciiAreaWidthCache();
    void ensureAsciiAreaWidthCache();
    void restoreTopVisibleByte(qint64 topByte);

private slots:
    void adjust();                              // recalc pixel positions
    void dataChangedPrivate(int idx=0);         // emit dataChanged() signal
    void refresh();                             // ensureVisible() and readBuffers()
    void updateCursor();                        // update blinking cursor
    void updateScrollMap();                     // debounce trigger: restart timer
    void scheduleScrollMapCompute();            // launch background marker computation
    void updateScrollMapMargins();              // sync viewport margins + positions after visibility change

private:
    // Name convention: pixel positions start with _px
    int _pxCharWidth, _pxCharHeight;            // char dimensions (dependend on font)
    int _pxPosHexX;                             // X-Pos of HeaxArea
    int _pxPosAdrX;                             // X-Pos of Address Area
    int _pxPosAsciiX;                           // X-Pos of Ascii Area
    int _pxGapAdr;                              // gap left from AddressArea
    int _pxGapAdrHex;                           // gap between AddressArea and HexAerea
    int _pxGapHexAscii;                         // gap between HexArea and AsciiArea
    int _pxCursorWidth;                         // cursor width
    int _pxSelectionSub;                        // offset selection rect
    int _pxCursorX;                             // current cursor pos
    int _pxCursorY;                             // current cursor pos

    // Name convention: absolute byte positions in chunks start with _b
    qint64 _bSelectionBegin;                    // first position of Selection
    qint64 _bSelectionEnd;                      // end of Selection
    qint64 _bSelectionInit;                     // memory position of Selection
    qint64 _bPosFirst;                          // position of first byte shown
    qint64 _bPosLast;                           // position of last byte shown
    qint64 _bPosCurrent;                        // current position

    // variables to store the property values
    bool _addressArea;                          // left area of QHexEdit
    QColor _addressAreaColor;
    QColor _asciiAreaColor;
    QColor _addressFontColor;
    QColor _asciiFontColor;
    QColor _hexFontColor;
    QColor _hexAreaBackgroundColor;
    QColor _hexAreaGridColor;
    QColor _cursorCharColor;
    QColor _cursorFrameColor;
    int _addressWidth;
    bool _asciiArea;
    qint64 _addressOffset;
    int _bytesPerLine;
    int _hexCharsInLine;
    bool _highlighting;
    bool _overwriteMode;
    bool _showHexGrid;
    QBrush _brushSelection;
    QPen _penSelection;
    QBrush _brushHighlighted;
    QPen _penHighlighted;
    QBrush _brushPointers;
    QPen _penPointers;
    QBrush _brushPointed;
    QPen _penPointed;
    QColor _pointerFontColor;
    QColor _pointedFontColor;
    QColor _pointerFrameColor;
    QColor _pointerFrameBackgroundColor;
    bool _readOnly;
    bool _showPointers;
    bool _hexCaps;
    bool _dynamicBytesPerLine;

    // other variables
    bool _editAreaIsAscii;                      // flag about the ascii mode edited
    uint32_t _asciiAreaMaxWidth;                // maximum width of ascii area – useful with translation tables
    uint8_t _addrDigits;                        // real no of addressdigits, may be > addressWidth
    bool _blink;                                // help get cursor blinking
    QBuffer _bData;                             // buffer, when setup with QByteArray
    Chunks *_chunks;                            // IODevice based access to data
    QTimer _cursorTimer;                        // for blinking cursor
    qint64 _cursorPosition;                     // absolute position of cursor, 1 Byte == 2 tics
    QRect _asciiCursorRect;                     // physical dimensions of cursor
    QRect _hexCursorRect;                       // physical dimensions of cursor
    QByteArray _data;                           // QHexEdit's data, when setup with QByteArray
    QByteArray _dataShown;                      // data in the current View
    QByteArray _hexDataShown;                   // data in view, transformed to hex
    qint64 _lastEventSize;                      // size, which was emitted last time
    QByteArray _markedShown;                    // marked data in view
    PointerListModel _pointers;                 // pointers in view
    HexScrollMap *_scrollMapPtr    = nullptr;   // pointer-storage strip (orange)
    HexScrollMap *_scrollMapTarget = nullptr;   // pointer-target strip (sky-blue)
    bool          _scrollMapPtrEnabled    = true;  // user preference: show ptr strip
    bool          _scrollMapTargetEnabled = true;  // user preference: show target strip
    int           _scrollMapCurrentMargin = 0;     // currently applied right viewport margin
    QTimer       *_scrollMapTimer   = nullptr;  // debounce before launching computation
    QFutureWatcher<ScrollMapMarkers> *_scrollMapWatcher = nullptr;  // background computation
    bool _modified;                             // Is any data in editor modified?
    int _rowsShown;                             // lines of text shown
    UndoStack * _undoStack;                     // Stack to store edit actions for undo/redo
    TranslationTable* _tb = nullptr;            // Translation table
    QVector<int> _tbSymbolWidthPxCache;         // cached rendered width for byte values 0x00..0xFF
    QVector<uint32_t> _asciiAreaMaxWidthByBpl;  // cached max ASCII row width by bytes-per-line
    int _tbMaxSymbolWidthPx = 0;
    bool _asciiAreaWidthCacheValid = false;
    int _asciiAreaWidthCacheCharWidth = 0;
    qint64 _asciiAreaWidthCacheDataSize = -1;
    const TranslationTable *_asciiAreaWidthCacheTable = nullptr;
    QChar _nonPrintableNoTableChar = QChar(0x25AA); // ▪
    QChar _notInTableChar = QChar(0x25A1);          // □
    /*! \endcond docNever */
};

#endif // QHEXEDIT_H

#include <QTest>
#include <QBuffer>
#include <QSignalSpy>

#include "../../src/hexeditor/hexeditor.h"

class TstHexEdit : public QObject
{
    Q_OBJECT

private slots:

    // ---- Data loading ----

    void setDataByteArray()
    {
        HexEditor editor;
        QByteArray data(256, '\0');
        for (int i = 0; i < 256; ++i) data[i] = char(i);
        editor.setData(data);
        QCOMPARE(editor.data().size(), 256);
        QCOMPARE(editor.data(), data);
    }

    void setDataIODevice()
    {
        HexEditor editor;
        QByteArray raw(128, 'X');
        QBuffer buf(&raw);
        QVERIFY(editor.setData(buf));
        QCOMPARE(editor.data().size(), 128);
    }

    // ---- Insert ----

    void insertChar()
    {
        HexEditor editor;
        editor.setData(QByteArray(8, 'A'));
        editor.insert(0, 'Z');
        QCOMPARE(editor.data().size(), 9);
        QCOMPARE(editor.data().at(0), 'Z');
        QCOMPARE(editor.data().at(1), 'A');
    }

    void insertByteArray()
    {
        HexEditor editor;
        editor.setData(QByteArray(8, 'A'));
        editor.insert(4, QByteArray("BCD"));
        QCOMPARE(editor.data().size(), 11);
        QCOMPARE(editor.data().at(4), 'B');
        QCOMPARE(editor.data().at(5), 'C');
        QCOMPARE(editor.data().at(6), 'D');
    }

    // ---- Replace ----

    void replaceChar()
    {
        HexEditor editor;
        editor.setData(QByteArray(8, 'A'));
        editor.replace(3, 'Z');
        QCOMPARE(editor.data().size(), 8);
        QCOMPARE(editor.data().at(3), 'Z');
    }

    void replaceByteArray()
    {
        HexEditor editor;
        editor.setData(QByteArray(8, 'A'));
        editor.replace(2, 3, QByteArray("XYZ"));
        QCOMPARE(editor.data().size(), 8);
        QCOMPARE(editor.data().at(2), 'X');
        QCOMPARE(editor.data().at(3), 'Y');
        QCOMPARE(editor.data().at(4), 'Z');
    }

    // ---- Remove ----

    void removeBytes()
    {
        HexEditor editor;
        QByteArray data(8, '\0');
        for (int i = 0; i < 8; ++i) data[i] = char('A' + i);
        editor.setData(data);
        editor.remove(2, 3);  // remove C, D, E
        QCOMPARE(editor.data().size(), 5);
        QCOMPARE(editor.data().at(0), 'A');
        QCOMPARE(editor.data().at(1), 'B');
        QCOMPARE(editor.data().at(2), 'F');
    }

    // ---- Search ----

    void indexOfFindsData()
    {
        HexEditor editor;
        QByteArray data(32, '\0');
        data[10] = 'T'; data[11] = 'E'; data[12] = 'S'; data[13] = 'T';
        editor.setData(data);
        QCOMPARE(editor.indexOf(QByteArray("TEST"), 0), qint64(10));
    }

    void indexOfNotFound()
    {
        HexEditor editor;
        editor.setData(QByteArray(32, 'A'));
        QCOMPARE(editor.indexOf(QByteArray("ZZZ"), 0), qint64(-1));
    }

    void lastIndexOfFindsData()
    {
        HexEditor editor;
        QByteArray data(32, '\0');
        data[5] = 'X'; data[6] = 'Y';
        data[20] = 'X'; data[21] = 'Y';
        editor.setData(data);
        QCOMPARE(editor.lastIndexOf(QByteArray("XY"), 31), qint64(20));
    }

    void indexOfSetsCursorToMatchStartAndSelectsFullLength()
    {
        HexEditor editor;
        QByteArray data(32, '\0');
        data[10] = char(0xDE);
        data[11] = char(0xAD);
        data[12] = char(0xBE);
        data[13] = char(0xEF);
        editor.setData(data);

        const QByteArray needle = QByteArray::fromHex("DEADBEEF");
        QCOMPARE(editor.indexOf(needle, 0), qint64(10));
        QCOMPARE(editor.cursorPosition(), qint64(20));
        QCOMPARE(editor.getSelectionBegin(), qint64(10));
        QCOMPARE(editor.getSelectionEnd(), qint64(14));
    }

    void lastIndexOfSetsCursorToMatchStartAndSelectsFullLength()
    {
        HexEditor editor;
        QByteArray data(48, '\0');
        data[8] = char(0xAA);
        data[9] = char(0xBB);
        data[30] = char(0xAA);
        data[31] = char(0xBB);
        editor.setData(data);

        const QByteArray needle = QByteArray::fromHex("AABB");
        QCOMPARE(editor.lastIndexOf(needle, 47), qint64(30));
        QCOMPARE(editor.cursorPosition(), qint64(60));
        QCOMPARE(editor.getSelectionBegin(), qint64(30));
        QCOMPARE(editor.getSelectionEnd(), qint64(32));
    }

    // ---- Undo / Redo ----

    void undoReplace()
    {
        HexEditor editor;
        editor.setData(QByteArray(8, 'A'));
        editor.replace(0, 'Z');
        QVERIFY(editor.canUndo());
        editor.undo();
        QCOMPARE(editor.data().at(0), 'A');
    }

    void redoAfterUndo()
    {
        HexEditor editor;
        editor.setData(QByteArray(8, 'A'));
        editor.replace(0, 'Z');
        editor.undo();
        QVERIFY(editor.canRedo());
        editor.redo();
        QCOMPARE(editor.data().at(0), 'Z');
    }

    // ---- Properties ----

    void addressAreaProperty()
    {
        HexEditor editor;
        editor.setAddressArea(true);
        QVERIFY(editor.addressArea());
        editor.setAddressArea(false);
        QVERIFY(!editor.addressArea());
    }

    void asciiAreaProperty()
    {
        HexEditor editor;
        editor.setAsciiArea(true);
        QVERIFY(editor.asciiArea());
        editor.setAsciiArea(false);
        QVERIFY(!editor.asciiArea());
    }

    void bytesPerLineProperty()
    {
        HexEditor editor;
        editor.setBytesPerLine(32);
        QCOMPARE(editor.bytesPerLine(), 32);
    }

    void readOnlyProperty()
    {
        HexEditor editor;
        editor.setReadOnly(true);
        QVERIFY(editor.isReadOnly());
        editor.setReadOnly(false);
        QVERIFY(!editor.isReadOnly());
    }

    void overwriteModeProperty()
    {
        HexEditor editor;
        editor.setOverwriteMode(true);
        QVERIFY(editor.overwriteMode());
        editor.setOverwriteMode(false);
        QVERIFY(!editor.overwriteMode());
    }

    void hexCapsProperty()
    {
        HexEditor editor;
        editor.setHexCaps(true);
        QVERIFY(editor.hexCaps());
        editor.setHexCaps(false);
        QVERIFY(!editor.hexCaps());
    }

    void highlightingProperty()
    {
        HexEditor editor;
        editor.setHighlighting(true);
        QVERIFY(editor.highlighting());
        editor.setHighlighting(false);
        QVERIFY(!editor.highlighting());
    }

    // ---- Colors ----

    void addressAreaColor()
    {
        HexEditor editor;
        QColor color(Qt::red);
        editor.setAddressAreaColor(color);
        QCOMPARE(editor.addressAreaColor(), color);
    }

    void selectionColor()
    {
        HexEditor editor;
        QColor color(Qt::blue);
        editor.setSelectionColor(color);
        QCOMPARE(editor.selectionColor(), color);
    }

    void highlightingColor()
    {
        HexEditor editor;
        QColor color(Qt::green);
        editor.setHighlightingColor(color);
        QCOMPARE(editor.highlightingColor(), color);
    }

    // ---- Modified state ----

    void isModifiedAfterEdit()
    {
        HexEditor editor;
        editor.setData(QByteArray(8, 'A'));
        QVERIFY(!editor.isModified());
        editor.replace(0, 'Z');
        QVERIFY(editor.isModified());
    }

    // ---- Cursor position ----

    void cursorPositionRoundTrip()
    {
        HexEditor editor;
        editor.setData(QByteArray(256, 'A'));
        editor.setCursorPosition(100);
        QCOMPARE(editor.cursorPosition(), qint64(100));
    }

    // ---- Address offset ----

    void addressOffset()
    {
        HexEditor editor;
        editor.setAddressOffset(0x8000);
        QCOMPARE(editor.addressOffset(), qint64(0x8000));
    }

    // ---- DataAt ----

    void dataAtReturnsSubArray()
    {
        HexEditor editor;
        QByteArray data(32, '\0');
        for (int i = 0; i < 32; ++i) data[i] = char(i);
        editor.setData(data);
        QByteArray sub = editor.dataAt(10, 5);
        QCOMPARE(sub.size(), 5);
        QCOMPARE(sub.at(0), char(10));
        QCOMPARE(sub.at(4), char(14));
    }

    // ---- Write to device ----

    void writeToDevice()
    {
        HexEditor editor;
        QByteArray data(16, 'A');
        editor.setData(data);
        editor.replace(0, 'Z');

        QBuffer outBuf;
        outBuf.open(QIODevice::WriteOnly);
        editor.write(outBuf);
        outBuf.close();

        QCOMPARE(outBuf.data().at(0), 'Z');
        QCOMPARE(outBuf.data().size(), 16);
    }

    // ---- Selection ----

    void selectionReset()
    {
        HexEditor editor;
        editor.setData(QByteArray(32, 'A'));
        editor.resetSelection();
        QVERIFY(!editor.hasSelection());
    }

    // ---- Translation table ----

    void translationTableSetAndGet()
    {
        HexEditor editor;
        TranslationTable *table = new TranslationTable();
        table->setItem(0x41, QStringLiteral("A"));
        editor.setTranslationTable(table);
        QCOMPARE(editor.getTranslationTable(), table);

        editor.removeTranslationTable();
        QCOMPARE(editor.getTranslationTable(), nullptr);
        delete table;
    }

    // ---- Encoding ----

    void currentEncodingRoundTrip()
    {
        HexEditor editor;
        editor.setCurrentEncoding(QStringLiteral("Shift-JIS"));
        QCOMPARE(editor.currentEncoding(), QStringLiteral("Shift-JIS"));
    }

    // ---- Pointer operations ----

    void addAndGetPointer()
    {
        HexEditor editor;
        editor.setData(QByteArray(0x1000, '\0'));
        editor.addPointerUndoable(0x100, 0x200, 4);
        QVERIFY(editor.pointers() != nullptr);
    }

    void clearPointers()
    {
        HexEditor editor;
        editor.setData(QByteArray(0x1000, '\0'));
        editor.addPointerUndoable(0x100, 0x200, 4);
        editor.clearPointers();
        QVERIFY(editor.pointers()->empty());
    }

    // ---- Signals ----

    void dataChangedSignal()
    {
        HexEditor editor;
        editor.setData(QByteArray(8, 'A'));
        QSignalSpy spy(&editor, &HexEditor::dataChanged);
        editor.replace(0, 'Z');
        QVERIFY(spy.count() > 0);
    }

    void overwriteModeChangedSignal()
    {
        HexEditor editor;
        QSignalSpy spy(&editor, &HexEditor::overwriteModeChanged);
        editor.setOverwriteMode(!editor.overwriteMode());
        QCOMPARE(spy.count(), 1);
    }

    // ---- Font ----

    void setFontMono()
    {
        HexEditor editor;
        QFont font("Courier", 12);
        editor.setFont(font);
        QCOMPARE(editor.font().family(), font.family());
    }

    // ---- Hex grid ----

    void showHexGrid()
    {
        HexEditor editor;
        editor.setShowHexGrid(true);
        QVERIFY(editor.showHexGrid());
        editor.setShowHexGrid(false);
        QVERIFY(!editor.showHexGrid());
    }

    // ---- Changes highlighting ----

    void showChangesProperty()
    {
        HexEditor editor;
        editor.setShowChanges(true);
        QVERIFY(editor.showChanges());
        editor.setShowChanges(false);
        QVERIFY(!editor.showChanges());
    }

    // ---- Pointer highlighting ----

    void showPointersProperty()
    {
        HexEditor editor;
        editor.setShowPointers(true);
        QVERIFY(editor.showPointers());
        editor.setShowPointers(false);
        QVERIFY(!editor.showPointers());
    }

    // ---- Address area collapse ----

    void addressCollapsedProperty()
    {
        HexEditor editor;
        QVERIFY(!editor.addressCollapsed());
        editor.setAddressCollapsed(true);
        QVERIFY(editor.addressCollapsed());
        editor.setAddressCollapsed(false);
        QVERIFY(!editor.addressCollapsed());
    }

    void addressCollapsedSignal()
    {
        HexEditor editor;
        QSignalSpy spy(&editor, &HexEditor::addressCollapsedChanged);
        editor.setAddressCollapsed(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), true);
        editor.setAddressCollapsed(false);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).toBool(), false);
    }

    void addressCollapsedNoDuplicateSignal()
    {
        HexEditor editor;
        QSignalSpy spy(&editor, &HexEditor::addressCollapsedChanged);
        // Setting same value should not emit signal
        editor.setAddressCollapsed(false);
        QCOMPARE(spy.count(), 0);
    }

    // ---- Original data view ----

    void hasNoOriginalDataByDefault()
    {
        HexEditor editor;
        QVERIFY(!editor.hasOriginalData());
    }

    void setOriginalDataSetsFlag()
    {
        HexEditor editor;
        editor.setOriginalData(QByteArray(16, '\xAB'));
        QVERIFY(editor.hasOriginalData());
    }

    void showOriginalDefaultIsFalse()
    {
        HexEditor editor;
        QVERIFY(!editor.showOriginal());
    }

    void showOriginalToggle()
    {
        HexEditor editor;
        editor.setOriginalData(QByteArray(16, '\xAB'));
        editor.setShowOriginal(true);
        QVERIFY(editor.showOriginal());
        editor.setShowOriginal(false);
        QVERIFY(!editor.showOriginal());
    }

    void showOriginalIdempotent()
    {
        HexEditor editor;
        // Should not crash or emit extra updates when called with same value
        editor.setShowOriginal(false);
        editor.setShowOriginal(false);
        QVERIFY(!editor.showOriginal());
    }

    // ---- Changed positions ----

    void setAndClearChangedPositions()
    {
        HexEditor editor;
        editor.setData(QByteArray(32, 'A'));
        QSet<qint64> positions = {0, 5, 10, 31};
        editor.setChangedPositions(positions);
        // Just verify it doesn't crash; visual state only
        editor.clearChangedPositions();
        QVERIFY(true);
    }

    // ---- Dynamic bytes per line ----

    void dynamicBytesPerLineProperty()
    {
        HexEditor editor;
        editor.setDynamicBytesPerLine(true);
        QVERIFY(editor.dynamicBytesPerLine());
        editor.setDynamicBytesPerLine(false);
        QVERIFY(!editor.dynamicBytesPerLine());
    }

    // ---- Multibyte frame ----

    void showMultibyteFrameProperty()
    {
        HexEditor editor;
        editor.setShowMultibyteFrame(true);
        QVERIFY(editor.showMultibyteFrame());
        editor.setShowMultibyteFrame(false);
        QVERIFY(!editor.showMultibyteFrame());
    }

    // ---- Encoding decode/encode round-trip ----

    void encodeDecodeASCIIRoundTrip()
    {
        HexEditor editor;
        editor.setCurrentEncoding(QStringLiteral("ASCII"));
        QByteArray bytes("Hello");
        QString decoded = editor.decodeTextForCurrentEncoding(bytes);
        QByteArray reEncoded = editor.encodeTextForCurrentEncoding(decoded);
        QCOMPARE(reEncoded, bytes);
    }
};

QTEST_MAIN(TstHexEdit)
#include "tst_hexedit.moc"

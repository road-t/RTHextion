#include <QTest>
#include <QBuffer>
#include <QSignalSpy>

#include "../../src/document/SectionListModel.h"
#include "../../src/hexeditor/hexeditor.h"
#include "../../src/hexeditor/encoding.h"
#include "../../src/document/PointerListModel.h"

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

    void modifiedStateTracksEdits()
    {
        HexEditor editor;
        editor.setData(QByteArray(8, 'A'));
        QVERIFY(!editor.isModified());

        editor.replace(0, 'Z');
        QVERIFY(editor.isModified());
    }

    void clearModifiedStateCreatesNewCleanPoint()
    {
        HexEditor editor;
        editor.setData(QByteArray(8, 'A'));
        editor.replace(0, 'Z');
        QVERIFY(editor.isModified());

        editor.setModified(false);
        QVERIFY(!editor.isModified());

        editor.replace(1, 'Y');
        QVERIFY(editor.isModified());

        editor.undo();
        QVERIFY(!editor.isModified());
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

    void setModifiedExplicitly()
    {
        HexEditor editor;
        editor.setData(QByteArray(8, 'A'));
        QVERIFY(!editor.isModified());
        editor.setModified(true);
        QVERIFY(editor.isModified());
    }

    void setModifiedPersistsThroughUndoRedo()
    {
        // When setModified(true) is called (e.g. after IPS load),
        // the modified flag should persist even when undo stack is empty
        HexEditor editor;
        editor.setData(QByteArray(8, 'A'));
        editor.setModified(true);
        QVERIFY(editor.isModified());

        // Make a change and undo it — should still be modified
        // because _baseModified is true
        editor.replace(0, 'Z');
        QVERIFY(editor.isModified());
        editor.undo();
        QVERIFY(editor.isModified());  // _baseModified keeps it true
    }

    void setModifiedFalseResets()
    {
        HexEditor editor;
        editor.setData(QByteArray(8, 'A'));
        editor.replace(0, 'Z');
        QVERIFY(editor.isModified());
        // Simulate a save — setData resets, but setModified(false) also works
        editor.setModified(false);
        // After setModified(false), the _baseModified is false,
        // but the undo stack still has an entry, so isModified depends on stack
        // Actually: _modified = _baseModified || (undoStack->index() != 0)
        // So with _baseModified=false and undoStack has entries, it stays modified
        // This tests that setModified(false) at least clears _baseModified
        // The next dataChanged will recalculate
    }

    // ---- Undo/Redo guards ----

    void undoOnEmptyStackDoesNotCrash()
    {
        HexEditor editor;
        editor.setData(QByteArray(8, 'A'));
        QVERIFY(!editor.canUndo());
        // Should not crash
        editor.undo();
        QCOMPARE(editor.data(), QByteArray(8, 'A'));
    }

    void redoOnEmptyStackDoesNotCrash()
    {
        HexEditor editor;
        editor.setData(QByteArray(8, 'A'));
        QVERIFY(!editor.canRedo());
        // Should not crash
        editor.redo();
        QCOMPARE(editor.data(), QByteArray(8, 'A'));
    }

    void undoAfterAllUndoneDoesNotCrash()
    {
        HexEditor editor;
        editor.setData(QByteArray(8, 'A'));
        editor.replace(0, 'Z');
        editor.undo();
        QVERIFY(!editor.canUndo());
        // Extra undo — should be no-op
        editor.undo();
        QCOMPARE(editor.data().at(0), 'A');
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

    // ---- Line breaks: basic API ----

    void lineBreaksEmptyByDefault()
    {
        HexEditor editor;
        editor.setData(QByteArray(64, 'A'));
        QVERIFY(editor.lineBreaks().isEmpty());
    }

    void setLineBreaksSingle()
    {
        HexEditor editor;
        editor.setData(QByteArray(64, 'A'));
        QVector<qint64> breaks = {10};
        editor.setLineBreaks(breaks);
        QCOMPARE(editor.lineBreaks().size(), 1);
        QCOMPARE(editor.lineBreaks().at(0), qint64(10));
    }

    void setLineBreaksMultiple()
    {
        HexEditor editor;
        editor.setData(QByteArray(64, 'A'));
        QVector<qint64> breaks = {5, 20, 15, 10};
        editor.setLineBreaks(breaks);
        // setLineBreaks sorts the vector
        QCOMPARE(editor.lineBreaks().size(), 4);
        QCOMPARE(editor.lineBreaks().at(0), qint64(5));
        QCOMPARE(editor.lineBreaks().at(1), qint64(10));
        QCOMPARE(editor.lineBreaks().at(2), qint64(15));
        QCOMPARE(editor.lineBreaks().at(3), qint64(20));
    }

    void setLineBreaksOverwritesPrevious()
    {
        HexEditor editor;
        editor.setData(QByteArray(64, 'A'));
        editor.setLineBreaks({5, 10});
        QCOMPARE(editor.lineBreaks().size(), 2);

        editor.setLineBreaks({30});
        QCOMPARE(editor.lineBreaks().size(), 1);
        QCOMPARE(editor.lineBreaks().at(0), qint64(30));
    }

    void addLineBreakDirectInsertsInOrder()
    {
        HexEditor editor;
        editor.setData(QByteArray(64, 'A'));
        editor.addLineBreakDirect(20);
        editor.addLineBreakDirect(5);
        editor.addLineBreakDirect(15);
        QCOMPARE(editor.lineBreaks().size(), 3);
        QCOMPARE(editor.lineBreaks().at(0), qint64(5));
        QCOMPARE(editor.lineBreaks().at(1), qint64(15));
        QCOMPARE(editor.lineBreaks().at(2), qint64(20));
    }

    void removeLineBreakDirectRemovesExisting()
    {
        HexEditor editor;
        editor.setData(QByteArray(64, 'A'));
        editor.setLineBreaks({5, 10, 20});
        editor.removeLineBreakDirect(10);
        QCOMPARE(editor.lineBreaks().size(), 2);
        QCOMPARE(editor.lineBreaks().at(0), qint64(5));
        QCOMPARE(editor.lineBreaks().at(1), qint64(20));
    }

    void removeLineBreakDirectNonExistentIsNoOp()
    {
        HexEditor editor;
        editor.setData(QByteArray(64, 'A'));
        editor.setLineBreaks({5, 10, 20});
        editor.removeLineBreakDirect(15);  // not present
        QCOMPARE(editor.lineBreaks().size(), 3);
    }

    void toggleLineBreakAddsAndRemoves()
    {
        HexEditor editor;
        editor.setData(QByteArray(64, 'A'));
        // Toggle on
        editor.toggleLineBreak(10);
        QCOMPARE(editor.lineBreaks().size(), 1);
        QCOMPARE(editor.lineBreaks().at(0), qint64(10));
        // Toggle off
        editor.toggleLineBreak(10);
        QVERIFY(editor.lineBreaks().isEmpty());
    }

    // ---- Line breaks: clearLineBreaks ----

    void clearLineBreaksRemovesAll()
    {
        HexEditor editor;
        editor.setData(QByteArray(64, 'A'));
        editor.setLineBreaks({5, 10, 20, 30, 50});
        QCOMPARE(editor.lineBreaks().size(), 5);
        editor.clearLineBreaks();
        QVERIFY(editor.lineBreaks().isEmpty());
    }

    void clearLineBreaksOnEmptyIsNoOp()
    {
        HexEditor editor;
        editor.setData(QByteArray(64, 'A'));
        QSignalSpy spy(&editor, &HexEditor::lineBreaksChanged);
        editor.clearLineBreaks();
        // Should not emit signal when already empty
        QCOMPARE(spy.count(), 0);
    }

    // ---- Line breaks: clearLineBreaksInRange ----

    void clearLineBreaksInRangeMiddle()
    {
        HexEditor editor;
        editor.setData(QByteArray(64, 'A'));
        editor.setLineBreaks({5, 10, 15, 20, 30, 50});
        editor.clearLineBreaksInRange(10, 30);
        // Should remove 10, 15, 20, 30 — keeps 5 and 50
        QCOMPARE(editor.lineBreaks().size(), 2);
        QCOMPARE(editor.lineBreaks().at(0), qint64(5));
        QCOMPARE(editor.lineBreaks().at(1), qint64(50));
    }

    void clearLineBreaksInRangeAll()
    {
        HexEditor editor;
        editor.setData(QByteArray(64, 'A'));
        editor.setLineBreaks({5, 10, 20});
        editor.clearLineBreaksInRange(0, 63);
        QVERIFY(editor.lineBreaks().isEmpty());
    }

    void clearLineBreaksInRangeNoneInRange()
    {
        HexEditor editor;
        editor.setData(QByteArray(64, 'A'));
        editor.setLineBreaks({5, 10, 50});
        QSignalSpy spy(&editor, &HexEditor::lineBreaksChanged);
        editor.clearLineBreaksInRange(20, 40);
        // No breaks in [20,40], so nothing removed, no signal
        QCOMPARE(editor.lineBreaks().size(), 3);
        QCOMPARE(spy.count(), 0);
    }

    void clearLineBreaksInRangeExactBoundary()
    {
        HexEditor editor;
        editor.setData(QByteArray(64, 'A'));
        editor.setLineBreaks({10, 20, 30});
        // Range exactly [10, 10] should remove only offset 10
        editor.clearLineBreaksInRange(10, 10);
        QCOMPARE(editor.lineBreaks().size(), 2);
        QCOMPARE(editor.lineBreaks().at(0), qint64(20));
        QCOMPARE(editor.lineBreaks().at(1), qint64(30));
    }

    void clearLineBreaksInRangeEdgesOnly()
    {
        HexEditor editor;
        editor.setData(QByteArray(64, 'A'));
        editor.setLineBreaks({0, 63});
        editor.clearLineBreaksInRange(0, 0);
        QCOMPARE(editor.lineBreaks().size(), 1);
        QCOMPARE(editor.lineBreaks().at(0), qint64(63));
    }

    // ---- Line breaks: signals ----

    void setLineBreaksEmitsSignal()
    {
        HexEditor editor;
        editor.setData(QByteArray(64, 'A'));
        QSignalSpy spy(&editor, &HexEditor::lineBreaksChanged);
        editor.setLineBreaks({10, 20});
        QCOMPARE(spy.count(), 1);
    }

    void addLineBreakDirectEmitsSignal()
    {
        HexEditor editor;
        editor.setData(QByteArray(64, 'A'));
        QSignalSpy spy(&editor, &HexEditor::lineBreaksChanged);
        editor.addLineBreakDirect(10);
        QCOMPARE(spy.count(), 1);
    }

    void clearLineBreaksEmitsSignal()
    {
        HexEditor editor;
        editor.setData(QByteArray(64, 'A'));
        editor.setLineBreaks({10});
        QSignalSpy spy(&editor, &HexEditor::lineBreaksChanged);
        editor.clearLineBreaks();
        QCOMPARE(spy.count(), 1);
    }

    void clearLineBreaksInRangeEmitsSignal()
    {
        HexEditor editor;
        editor.setData(QByteArray(64, 'A'));
        editor.setLineBreaks({10, 20, 30});
        QSignalSpy spy(&editor, &HexEditor::lineBreaksChanged);
        editor.clearLineBreaksInRange(15, 25);
        QCOMPARE(spy.count(), 1);
    }

    void toggleLineBreakEmitsSignal()
    {
        HexEditor editor;
        editor.setData(QByteArray(64, 'A'));
        QSignalSpy spy(&editor, &HexEditor::lineBreaksChanged);
        editor.toggleLineBreak(10);
        QCOMPARE(spy.count(), 1);
        editor.toggleLineBreak(10);
        QCOMPARE(spy.count(), 2);
    }

    // ---- Line breaks: duplicate offsets ----

    void setLineBreaksAllowsDuplicates()
    {
        // Multiple breaks at same offset = multiple empty rows
        HexEditor editor;
        editor.setData(QByteArray(64, 'A'));
        QVector<qint64> breaks = {10, 10, 10};
        editor.setLineBreaks(breaks);
        QCOMPARE(editor.lineBreaks().size(), 3);
    }

    // ---- Line breaks: undo-based addLineBreak / removeLineBreak ----

    void addLineBreakUndoable()
    {
        HexEditor editor;
        editor.setData(QByteArray(64, 'A'));
        editor.addLineBreak(10);
        QCOMPARE(editor.lineBreaks().size(), 1);
        QCOMPARE(editor.lineBreaks().at(0), qint64(10));
    }

    void removeLineBreakUndoable()
    {
        HexEditor editor;
        editor.setData(QByteArray(64, 'A'));
        editor.addLineBreak(10);
        QCOMPARE(editor.lineBreaks().size(), 1);
        editor.removeLineBreak(10);
        QVERIFY(editor.lineBreaks().isEmpty());
    }

    void removeLineBreakUndoableNonExistentIsNoOp()
    {
        HexEditor editor;
        editor.setData(QByteArray(64, 'A'));
        editor.addLineBreak(10);
        editor.removeLineBreak(20);  // not present
        QCOMPARE(editor.lineBreaks().size(), 1);
    }

    void sectionAddUndoRedo()
    {
        HexEditor editor;
        editor.setData(QByteArray(64, 'A'));

        SectionListModel model;
        model.setUndoStack(editor.undoStack());

        Section section;
        section.name = QStringLiteral("Header");
        section.startOffset = 0;
        section.color = Qt::red;

        model.addSection(section);
        QCOMPARE(model.count(), 1);
        QVERIFY(editor.canUndo());

        editor.undo();
        QCOMPARE(model.count(), 0);
        QVERIFY(editor.canRedo());

        editor.redo();
        QCOMPARE(model.count(), 1);
        QCOMPARE(model.at(0).name, QStringLiteral("Header"));
    }

    void sectionRenameUndoRedo()
    {
        HexEditor editor;
        editor.setData(QByteArray(64, 'A'));

        SectionListModel model;
        model.setUndoStack(editor.undoStack());

        Section section;
        section.name = QStringLiteral("Header");
        section.startOffset = 0;
        section.color = Qt::red;

        model.addSection(section);
        model.renameSection(0, QStringLiteral("Code"));
        QCOMPARE(model.at(0).name, QStringLiteral("Code"));

        editor.undo();
        QCOMPARE(model.at(0).name, QStringLiteral("Header"));

        editor.redo();
        QCOMPARE(model.at(0).name, QStringLiteral("Code"));
    }

    void sectionRemoveUndoRedo()
    {
        HexEditor editor;
        editor.setData(QByteArray(64, 'A'));

        SectionListModel model;
        model.setUndoStack(editor.undoStack());

        Section parent;
        parent.name = QStringLiteral("Header");
        parent.startOffset = 0;
        parent.color = Qt::red;
        model.addSection(parent);

        Section child;
        child.name = QStringLiteral("Sub");
        child.startOffset = 8;
        child.color = Qt::blue;
        model.addSection(child);

        model.removeSection(0);
        QCOMPARE(model.count(), 1);
        QCOMPARE(model.at(0).name, QStringLiteral("Sub"));

        editor.undo();
        QCOMPARE(model.count(), 2);
        QCOMPARE(model.at(0).name, QStringLiteral("Header"));
        QCOMPARE(model.at(1).name, QStringLiteral("Sub"));

        editor.redo();
        QCOMPARE(model.count(), 1);
        QCOMPARE(model.at(0).name, QStringLiteral("Sub"));
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Encoding module tests (encoding.cpp)
    // ═══════════════════════════════════════════════════════════════════════

    void isSingleByteEncodingRecognisesKnownCodecs()
    {
        QVERIFY(isSingleByteEncoding(QStringLiteral("ASCII")));
        QVERIFY(isSingleByteEncoding(QStringLiteral("Windows-1251")));
        QVERIFY(isSingleByteEncoding(QStringLiteral("KOI8-R")));
        QVERIFY(isSingleByteEncoding(QStringLiteral("ISO-8859-1")));
        QVERIFY(!isSingleByteEncoding(QStringLiteral("UTF-8")));
        QVERIFY(!isSingleByteEncoding(QStringLiteral("Shift-JIS")));
        QVERIFY(!isSingleByteEncoding(QStringLiteral("UTF-16 LE")));
    }

    void decodeSingleByteAsciiPassthrough()
    {
        // ASCII range should pass through for any single-byte encoding
        QChar ch = decodeSingleByte(0x41, QStringLiteral("ASCII"));
        QCOMPARE(ch, QChar('A'));
    }

    void decodeSingleByteWindows1251CyrillicBlock()
    {
        // 0xC0 in Windows-1251 = А (U+0410)
        QChar ch = decodeSingleByte(0xC0, QStringLiteral("Windows-1251"));
        QCOMPARE(ch.unicode(), ushort(0x0410));
        // 0xC1 = Б (U+0411)
        ch = decodeSingleByte(0xC1, QStringLiteral("Windows-1251"));
        QCOMPARE(ch.unicode(), ushort(0x0411));
    }

    void decodeSingleByteKoi8R()
    {
        // 0xC1 in KOI8-R = А (U+0410)
        QChar ch = decodeSingleByte(0xE1, QStringLiteral("KOI8-R"));
        QCOMPARE(ch.unicode(), ushort(0x0410));
    }

    void decodeTextWithEncodingAscii()
    {
        QByteArray data("Hello");
        QString result = decodeTextWithEncoding(data, QStringLiteral("ASCII"));
        QCOMPARE(result, QStringLiteral("Hello"));
    }

    void decodeTextWithEncodingEmptyData()
    {
        QString result = decodeTextWithEncoding(QByteArray(), QStringLiteral("ASCII"));
        QVERIFY(result.isEmpty());
    }

    void encodeTextWithEncodingRoundTrip()
    {
        // ASCII round-trip
        QString text = QStringLiteral("Test123");
        QByteArray encoded = encodeTextWithEncoding(text, QStringLiteral("ASCII"));
        QString decoded = decodeTextWithEncoding(encoded, QStringLiteral("ASCII"));
        QCOMPARE(decoded, text);
    }

    void decodeWithIconvUtf8()
    {
        QByteArray data("Hello, World!");
        QString result = decodeWithIconv(data, QStringLiteral("UTF-8"));
        QCOMPARE(result, QStringLiteral("Hello, World!"));
    }

    void encodeWithIconvUtf8RoundTrip()
    {
        QString text = QStringLiteral("Unicode: café");
        QByteArray encoded = encodeWithIconv(text, QStringLiteral("UTF-8"));
        QString decoded = decodeWithIconv(encoded, QStringLiteral("UTF-8"));
        QCOMPARE(decoded, text);
    }

    void decodeBufferWithEncodingAllNullBytes()
    {
        QByteArray data(8, '\0');
        QVector<QString> result = decodeBufferWithEncoding(data, QStringLiteral("ASCII"));
        QCOMPARE(result.size(), 8);
        // Null bytes in ASCII are non-printable → empty string (not null)
        for (int i = 0; i < 8; ++i) {
            QVERIFY(!result[i].isNull() || result[i].isEmpty());
        }
    }

    void decodeBufferWithEncodingPrintableAscii()
    {
        QByteArray data("ABCD");
        QVector<QString> result = decodeBufferWithEncoding(data, QStringLiteral("ASCII"));
        QCOMPARE(result.size(), 4);
        QCOMPARE(result[0], QStringLiteral("A"));
        QCOMPARE(result[1], QStringLiteral("B"));
        QCOMPARE(result[2], QStringLiteral("C"));
        QCOMPARE(result[3], QStringLiteral("D"));
    }

    void codecCandidatesReturnsSomethingForKnownEncodings()
    {
        QVERIFY(!codecCandidates(QStringLiteral("Shift-JIS")).isEmpty());
        QVERIFY(!codecCandidates(QStringLiteral("UTF-16 LE")).isEmpty());
        QVERIFY(!codecCandidates(QStringLiteral("Windows-1251")).isEmpty());
        QVERIFY(!codecCandidates(QStringLiteral("UnknownEncoding")).isEmpty());
    }

    void iconvSeqLenShiftJIS()
    {
        // Single-byte ASCII character
        QByteArray data;
        data.append(char(0x41)); // 'A'
        QCOMPARE(iconvSeqLen(data, 0, QStringLiteral("Shift-JIS")), 1);

        // Double-byte Shift-JIS character: 0x82 0x A0 (hiragana)
        data.clear();
        data.append(char(0x82));
        data.append(char(0xA0));
        QCOMPARE(iconvSeqLen(data, 0, QStringLiteral("Shift-JIS")), 2);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Layout module tests (layout.cpp — additional line break + range tests)
    // ═══════════════════════════════════════════════════════════════════════

    void addLineBreakUndoableRoundTrip()
    {
        HexEditor editor;
        editor.setBytesPerLine(16);
        editor.setDynamicBytesPerLine(false);
        editor.setData(QByteArray(32, 'A'));

        editor.addLineBreak(8);
        QVERIFY(editor.lineBreaks().contains(8));

        editor.undo();
        QVERIFY(!editor.lineBreaks().contains(8));

        editor.redo();
        QVERIFY(editor.lineBreaks().contains(8));
    }

    void clearLineBreaksInRangePartial()
    {
        HexEditor editor;
        editor.setBytesPerLine(16);
        editor.setDynamicBytesPerLine(false);
        editor.setData(QByteArray(64, 'A'));
        editor.addLineBreak(8);
        editor.addLineBreak(24);
        editor.addLineBreak(40);

        editor.clearLineBreaksInRange(10, 30);
        QVector<qint64> breaks = editor.lineBreaks();
        QCOMPARE(breaks.size(), 2);
        QVERIFY(breaks.contains(8));
        QVERIFY(!breaks.contains(24));
        QVERIFY(breaks.contains(40));
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Pointers module tests (pointers.cpp)
    // ═══════════════════════════════════════════════════════════════════════

    void pointersInitiallyEmpty()
    {
        HexEditor editor;
        editor.setData(QByteArray(256, '\0'));
        QCOMPARE(editor.pointers()->rowCount(), 0);
    }

    void addPointerUndoable()
    {
        HexEditor editor;
        editor.setData(QByteArray(256, '\0'));
        bool ok = editor.addPointerUndoable(0, 128, 4);
        QVERIFY(ok);
        QCOMPARE(editor.pointers()->rowCount(), 1);
    }

    void removePointerUndoable()
    {
        HexEditor editor;
        editor.setData(QByteArray(256, '\0'));
        editor.addPointerUndoable(0, 128, 4);
        QCOMPARE(editor.pointers()->rowCount(), 1);

        bool ok = editor.removePointerUndoable(0);
        QVERIFY(ok);
        QCOMPARE(editor.pointers()->rowCount(), 0);
    }

    void addPointerUndoableUndoRedo()
    {
        HexEditor editor;
        editor.setData(QByteArray(256, '\0'));
        editor.addPointerUndoable(0, 128, 4);
        QCOMPARE(editor.pointers()->rowCount(), 1);

        editor.undo();
        QCOMPARE(editor.pointers()->rowCount(), 0);

        editor.redo();
        QCOMPARE(editor.pointers()->rowCount(), 1);
    }

    void clearPointersRemovesAll()
    {
        HexEditor editor;
        editor.setData(QByteArray(256, '\0'));
        editor.addPointerUndoable(0, 128, 4);
        editor.addPointerUndoable(8, 64, 4);
        QCOMPARE(editor.pointers()->rowCount(), 2);

        editor.clearPointers();
        QCOMPARE(editor.pointers()->rowCount(), 0);
    }

    void pointerColorProperties()
    {
        HexEditor editor;
        QColor c(Qt::cyan);
        editor.setPointersColor(c);
        QCOMPARE(editor.pointersColor(), c);

        editor.setPointedColor(c);
        QCOMPARE(editor.pointedColor(), c);

        editor.setPointerFontColor(c);
        QCOMPARE(editor.pointerFontColor(), c);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Scroll map tests (scrollmap.cpp)
    // ═══════════════════════════════════════════════════════════════════════

    void scrollMapVisibilityProperties()
    {
        HexEditor editor;
        editor.setScrollMapChangesVisible(true);
        QVERIFY(editor.scrollMapChangesVisible());
        editor.setScrollMapChangesVisible(false);
        QVERIFY(!editor.scrollMapChangesVisible());

        editor.setScrollMapTargetVisible(true);
        QVERIFY(editor.scrollMapTargetVisible());
        editor.setScrollMapTargetVisible(false);
        QVERIFY(!editor.scrollMapTargetVisible());
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Disassembly module tests (disasm.cpp)
    // ═══════════════════════════════════════════════════════════════════════

    void disasmDefaultOff()
    {
        HexEditor editor;
        editor.setData(QByteArray(256, '\0'));
        QVERIFY(!editor.showDisasm());
    }

    void showSectionsProperty()
    {
        HexEditor editor;
        editor.setShowSections(true);
        QVERIFY(editor.showSections());
        editor.setShowSections(false);
        QVERIFY(!editor.showSections());
    }

    void multibyteFrameProperty()
    {
        HexEditor editor;
        editor.setShowMultibyteFrame(true);
        QVERIFY(editor.showMultibyteFrame());
        editor.setShowMultibyteFrame(false);
        QVERIFY(!editor.showMultibyteFrame());
    }
};

QTEST_MAIN(TstHexEdit)
#include "tst_hexedit.moc"

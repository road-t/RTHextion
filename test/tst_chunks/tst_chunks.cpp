#include <QTest>
#include <QBuffer>
#include <QByteArray>

#include "../../src/hexeditor/chunks.h"

class TstChunks : public QObject
{
    Q_OBJECT

private:
    Chunks *newChunks(const QByteArray &data)
    {
        // Chunks needs a QIODevice; wrap data in a QBuffer on the heap
        auto *buf = new QBuffer(this);
        buf->setData(data);
        auto *c = new Chunks(this);
        c->setIODevice(*buf);
        return c;
    }

private slots:

    // ---- Basic size / read ----

    void sizeAfterSetData()
    {
        QByteArray orig(256, '\0');
        for (int i = 0; i < 256; ++i) orig[i] = char(i);
        auto *c = newChunks(orig);
        QCOMPARE(c->size(), 256);
    }

    void readBackOriginalData()
    {
        QByteArray orig(16, '\0');
        for (int i = 0; i < 16; ++i) orig[i] = char(i);
        auto *c = newChunks(orig);
        QByteArray result = c->data(0, 16);
        QCOMPARE(result, orig);
    }

    void readSubRange()
    {
        QByteArray orig(64, 'A');
        orig[10] = 'X'; orig[11] = 'Y'; orig[12] = 'Z';
        auto *c = newChunks(orig);
        QByteArray sub = c->data(10, 3);
        QCOMPARE(sub, QByteArray("XYZ"));
    }

    // ---- Overwrite ----

    void overwriteSingleByte()
    {
        QByteArray orig(16, '\0');
        auto *c = newChunks(orig);
        c->overwrite(5, 'X');
        QCOMPARE((*c)[5], 'X');
        QCOMPARE(c->size(), 16);       // size unchanged
    }

    void overwriteMarksDataChanged()
    {
        QByteArray orig(16, '\0');
        auto *c = newChunks(orig);
        QVERIFY(!c->dataChanged(5));
        c->overwrite(5, 'X');
        QVERIFY(c->dataChanged(5));
    }

    // ---- Insert ----

    void insertGrowsSize()
    {
        QByteArray orig(16, '\0');
        auto *c = newChunks(orig);
        c->insert(0, 'A');
        QCOMPARE(c->size(), 17);
    }

    void insertAtBeginning()
    {
        QByteArray orig(4, '\0');
        orig[0] = 'B'; orig[1] = 'C'; orig[2] = 'D'; orig[3] = 'E';
        auto *c = newChunks(orig);
        c->insert(0, 'A');
        QCOMPARE((*c)[0], 'A');
        QCOMPARE((*c)[1], 'B');
        QCOMPARE(c->size(), 5);
    }

    void insertAtEnd()
    {
        QByteArray orig(4, 'A');
        auto *c = newChunks(orig);
        c->insert(4, 'Z');
        QCOMPARE((*c)[4], 'Z');
        QCOMPARE(c->size(), 5);
    }

    void insertInMiddle()
    {
        QByteArray orig(4, '\0');
        orig[0] = 'A'; orig[1] = 'C'; orig[2] = 'D'; orig[3] = 'E';
        auto *c = newChunks(orig);
        c->insert(1, 'B');
        QCOMPARE((*c)[0], 'A');
        QCOMPARE((*c)[1], 'B');
        QCOMPARE((*c)[2], 'C');
        QCOMPARE(c->size(), 5);
    }

    // ---- Remove ----

    void removeShrinksSize()
    {
        QByteArray orig(16, '\0');
        auto *c = newChunks(orig);
        c->removeAt(0);
        QCOMPARE(c->size(), 15);
    }

    void removeAtBeginning()
    {
        QByteArray orig(4, '\0');
        orig[0] = 'A'; orig[1] = 'B'; orig[2] = 'C'; orig[3] = 'D';
        auto *c = newChunks(orig);
        c->removeAt(0);
        QCOMPARE((*c)[0], 'B');
        QCOMPARE(c->size(), 3);
    }

    void removeAtEnd()
    {
        QByteArray orig(4, '\0');
        orig[0] = 'A'; orig[1] = 'B'; orig[2] = 'C'; orig[3] = 'D';
        auto *c = newChunks(orig);
        c->removeAt(3);
        QCOMPARE(c->size(), 3);
        QCOMPARE((*c)[2], 'C');
    }

    // ---- Search ----

    void indexOfFindsPattern()
    {
        QByteArray orig(32, '\0');
        orig[10] = 'H'; orig[11] = 'E'; orig[12] = 'L'; orig[13] = 'L'; orig[14] = 'O';
        auto *c = newChunks(orig);
        QCOMPARE(c->indexOf(QByteArray("HELLO"), 0), 10);
    }

    void indexOfReturnsMinusOneWhenNotFound()
    {
        QByteArray orig(32, 'A');
        auto *c = newChunks(orig);
        QCOMPARE(c->indexOf(QByteArray("ZZZ"), 0), -1);
    }

    void lastIndexOfFindsPattern()
    {
        QByteArray orig(32, '\0');
        orig[5] = 'A'; orig[6] = 'B';
        orig[20] = 'A'; orig[21] = 'B';
        auto *c = newChunks(orig);
        QCOMPARE(c->lastIndexOf(QByteArray("AB"), 31), 20);
    }

    // ---- Write to device ----

    void writeToDevice()
    {
        QByteArray orig(16, '\0');
        for (int i = 0; i < 16; ++i) orig[i] = char(i);
        auto *c = newChunks(orig);
        c->overwrite(0, 'X');

        QBuffer outBuf;
        outBuf.open(QIODevice::WriteOnly);
        c->write(outBuf);
        outBuf.close();

        QByteArray expected = orig;
        expected[0] = 'X';
        QCOMPARE(outBuf.data(), expected);
    }

    // ---- Large data crossing chunk boundaries ----

    void largeDataCrossesChunkBoundary()
    {
        // Chunks uses 4KB internal chunks; test with 8KB+ data
        QByteArray orig(0x4000, '\0');
        for (int i = 0; i < orig.size(); ++i) orig[i] = char(i & 0xFF);
        auto *c = newChunks(orig);

        // Overwrite across the 4KB boundary
        c->overwrite(0x0FFF, 'X');
        c->overwrite(0x1000, 'Y');
        QCOMPARE((*c)[0x0FFF], 'X');
        QCOMPARE((*c)[0x1000], 'Y');
        QCOMPARE(c->size(), 0x4000);
    }

    // ---- Mixed operations ----

    void insertThenOverwrite()
    {
        QByteArray orig(8, 'A');
        auto *c = newChunks(orig);
        c->insert(4, 'B');     // size = 9
        c->overwrite(4, 'C');  // overwrite the inserted byte
        QCOMPARE((*c)[4], 'C');
        QCOMPARE(c->size(), 9);
    }

    void multipleInserts()
    {
        QByteArray orig(4, '\0');
        auto *c = newChunks(orig);
        for (int i = 0; i < 100; ++i)
            c->insert(0, char(i));
        QCOMPARE(c->size(), 104);
    }

    void multipleRemoves()
    {
        QByteArray orig(100, 'A');
        auto *c = newChunks(orig);
        for (int i = 0; i < 50; ++i)
            c->removeAt(0);
        QCOMPARE(c->size(), 50);
    }

    // ---- Highlighted data tracking ----

    void highlightedDataTracking()
    {
        QByteArray orig(16, '\0');
        auto *c = newChunks(orig);
        c->overwrite(3, 'X');

        QByteArray highlighted;
        c->data(0, 16, &highlighted);
        // Byte 3 should be marked as changed
        QCOMPARE(highlighted[3], char(1));
        // Byte 0 should not be changed
        QCOMPARE(highlighted[0], char(0));
    }
};

QTEST_MAIN(TstChunks)
#include "tst_chunks.moc"

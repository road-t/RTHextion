#include <QTest>
#include <QTemporaryFile>
#include <QTextStream>

#include "translationtable.h"

class TstTranslationTable : public QObject
{
    Q_OBJECT

private:
    // Writes a .tbl file with given content and returns the path
    QString writeTblFile(const QString &content)
    {
        auto *tmp = new QTemporaryFile(this);
        tmp->setAutoRemove(true);
        tmp->open();
        QTextStream out(tmp);
        out << content;
        out.flush();
        tmp->close();
        return tmp->fileName();
    }

private slots:

    // ---- Construction ----

    void emptyTableHasZeroSize()
    {
        TranslationTable table;
        QCOMPARE(table.size(), 0u);
    }

    // ---- Single-byte entries ----

    void setAndEncodeSymbol()
    {
        TranslationTable table;
        table.setItem(0x41, QStringLiteral("A"));
        QCOMPARE(table.encodeSymbol(0x41), QStringLiteral("A"));
    }

    void decodeSingleSymbol()
    {
        TranslationTable table;
        table.setItem(0x41, QStringLiteral("A"));
        QCOMPARE(table.decodeSymbol(QStringLiteral("A")), char(0x41));
    }

    void encodeByteArray()
    {
        TranslationTable table;
        table.setItem(0x48, QStringLiteral("H"));
        table.setItem(0x49, QStringLiteral("I"));
        QByteArray data;
        data.append(char(0x48));
        data.append(char(0x49));
        QCOMPARE(table.encode(data), QStringLiteral("HI"));
    }

    void decodeByteArrayRoundTrip()
    {
        TranslationTable table;
        table.setItem(0x48, QStringLiteral("H"));
        table.setItem(0x49, QStringLiteral("I"));
        table.buildFallbackDecodeEntries();

        QByteArray orig;
        orig.append(char(0x48));
        orig.append(char(0x49));
        QString encoded = table.encode(orig);
        QByteArray decoded = table.decode(encoded.toLatin1());
        QCOMPARE(decoded, orig);
    }

    // ---- Multi-byte entries ----

    void multiByteEncode()
    {
        TranslationTable table;
        QByteArray key;
        key.append(char(0x80));
        key.append(char(0x01));
        table.setMultiByteItem(key, QStringLiteral("AB"));
        QVERIFY(table.hasMultiByteEntries());
        QCOMPARE(table.maxKeyLength(), 2);

        int consumed = 0;
        QByteArray data = key;
        QString result = table.encodeBytes(data, 0, consumed);
        QCOMPARE(result, QStringLiteral("AB"));
        QCOMPARE(consumed, 2);
    }

    void multiByteDecodeToBytes()
    {
        TranslationTable table;
        QByteArray key;
        key.append(char(0x80));
        key.append(char(0x01));
        table.setMultiByteItem(key, QStringLiteral("AB"));

        QByteArray result = table.decodeToBytes(QStringLiteral("AB"));
        QCOMPARE(result, key);
    }

    // ---- decodeToBytes - single byte fallback ----

    void decodeToBytesForSingleByte()
    {
        TranslationTable table;
        table.setItem(0x42, QStringLiteral("B"));
        QByteArray result = table.decodeToBytes(QStringLiteral("B"));
        QCOMPARE(result.size(), 1);
        QCOMPARE(static_cast<uint8_t>(result[0]), uint8_t(0x42));
    }

    void decodeToBytesNotFound()
    {
        TranslationTable table;
        QByteArray result = table.decodeToBytes(QStringLiteral("XXX"));
        QVERIFY(result.isEmpty());
    }

    // ---- Fallback entries ----

    void fallbackEntriesForUnmappedBytes()
    {
        TranslationTable table;
        table.setItem(0x41, QStringLiteral("A"));
        table.buildFallbackDecodeEntries();

        // buildFallbackDecodeEntries populates decodeTable, used by decode()
        QByteArray decoded = table.decode(QByteArray("{00}"));
        QCOMPARE(decoded.size(), 1);
        QCOMPARE(static_cast<uint8_t>(decoded[0]), uint8_t(0x00));
    }

    // ---- Remove items ----

    void removeItem()
    {
        TranslationTable table;
        table.setItem(0x41, QStringLiteral("A"));
        table.setItem(0x42, QStringLiteral("B"));
        QCOMPARE(table.size(), 2u);
        table.removeItem(0x41);
        QCOMPARE(table.size(), 1u);
    }

    void removeMultiByteItem()
    {
        TranslationTable table;
        QByteArray key;
        key.append(char(0x80));
        key.append(char(0x01));
        table.setMultiByteItem(key, QStringLiteral("X"));
        QCOMPARE(table.size(), 1u);
        table.removeMultiByteItem(key);
        QCOMPARE(table.size(), 0u);
    }

    void clearItems()
    {
        TranslationTable table;
        table.setItem(0x41, QStringLiteral("A"));
        table.setItem(0x42, QStringLiteral("B"));
        table.clearItems();
        QCOMPARE(table.size(), 0u);
    }

    // ---- Load from file ----

    void loadFromFile()
    {
        QString content = QStringLiteral("41=A\n42=B\n43=C\n");
        QString path = writeTblFile(content);
        TranslationTable table(path);
        QCOMPARE(table.size(), 3u);
        QCOMPARE(table.encodeSymbol(0x41), QStringLiteral("A"));
    }

    void loadMultiByteFromFile()
    {
        QString content = QStringLiteral("41=A\n8001=AB\n");
        QString path = writeTblFile(content);
        TranslationTable table(path);
        QVERIFY(table.hasMultiByteEntries());
        QCOMPARE(table.maxKeyLength(), 2);
    }

    // ---- Save and reload ----

    void saveAndReload()
    {
        TranslationTable table;
        table.setItem(0x48, QStringLiteral("H"));
        table.setItem(0x65, QStringLiteral("e"));
        QByteArray key;
        key.append(char(0x80));
        key.append(char(0x01));
        table.setMultiByteItem(key, QStringLiteral("XY"));

        QTemporaryFile tmp;
        tmp.setAutoRemove(true);
        tmp.open();
        QString path = tmp.fileName();
        tmp.close();

        QVERIFY(table.save(path));

        TranslationTable loaded(path);
        QCOMPARE(loaded.encodeSymbol(0x48), QStringLiteral("H"));
        QCOMPARE(loaded.encodeSymbol(0x65), QStringLiteral("e"));
        QVERIFY(loaded.hasMultiByteEntries());
    }

    // ---- Iterator ----

    void iteratorTraversal()
    {
        TranslationTable table;
        table.setItem(0x41, QStringLiteral("A"));
        table.setItem(0x42, QStringLiteral("B"));
        table.setItem(0x43, QStringLiteral("C"));
        table.reset();

        // next() pre-increments, so first call returns the second item
        auto pair = table.next();
        QCOMPARE(pair.second, QStringLiteral("B"));
        pair = table.next();
        QCOMPARE(pair.second, QStringLiteral("C"));
    }

    // ---- Utility ----

    void charToHexFormat()
    {
        QString hex = TranslationTable::charToHex('A');
        QCOMPARE(hex, QStringLiteral("{41}"));
    }

    void escapeNonPrintable()
    {
        QByteArray data;
        data.append(char(0x01));
        data.append(char(0x41));
        QString escaped = TranslationTable::escapeNonPrintable(data);
        // Non-printable 0x01 should be escaped; 'A' (0x41) should remain
        QVERIFY(escaped.contains(QStringLiteral("A")));
    }
};

QTEST_MAIN(TstTranslationTable)
#include "tst_translationtable.moc"

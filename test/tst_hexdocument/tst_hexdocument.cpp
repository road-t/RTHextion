#include <QTest>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QFile>

#include "hexdocument.h"
#include "PointerListModel.h"
#include "translationtable.h"

class TstHexDocument : public QObject
{
    Q_OBJECT

private slots:

    // ---- Default state ----

    void defaultConstruction()
    {
        HexDocument doc;
        QVERIFY(doc.isUntitled);
        QVERIFY(doc.filePath.isEmpty());
        QCOMPARE(doc.romType, RomType::Unknown);
        QCOMPARE(doc.byteOrder, ByteOrder::LittleEndian);
        QCOMPARE(doc.cursorPosition, 0);
        QVERIFY(doc.pointerSnapshot.isEmpty());
        QVERIFY(doc.navigationHistory.isEmpty());
        QCOMPARE(doc.currentEncoding, QStringLiteral("ASCII"));
    }

    // ---- Save and load project (v3 multi-table) ----

    void saveAndLoadBasicProject()
    {
        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());
        QString projectPath = tmpDir.path() + "/test.rthp";

        // Create a dummy data file so the relative path works
        QString dataPath = tmpDir.path() + "/data.bin";
        {
            QFile f(dataPath);
            f.open(QIODevice::WriteOnly);
            f.write(QByteArray(16, 'A'));
            f.close();
        }

        // Setup document
        HexDocument doc;
        doc.filePath = dataPath;
        doc.projectName = QStringLiteral("Test Project");
        doc.currentEncoding = QStringLiteral("Shift-JIS");
        doc.romType = RomType::NES;
        doc.byteOrder = ByteOrder::LittleEndian;
        doc.cursorPosition = 0x100;
        doc.showPointers = true;
        doc.showChanges = false;
        doc.changesHexMode = true;

        // Navigation history
        doc.navigationHistory = {0x10, 0x20, 0x30};
        doc.navigationHistoryIndex = 1;

        // Pointer snapshot
        doc.pointerSnapshot.append({0x1000, PointerListModel::encodePtrValue(0x2000, 4)});
        doc.pointerSnapshot.append({0x3000, PointerListModel::encodePtrValue(0x4000, 2)});

        // Original bytes
        doc.originalBytes.append({0x100, QByteArray("\x41\x42\x43", 3)});

        // Save with multi-table API (empty tables for now)
        QVector<DocTableEntry> tables;
        QVERIFY(doc.saveProject(projectPath, tables, -1));

        // Load into a fresh document
        HexDocument loaded;
        QVERIFY(loaded.loadProject(projectPath));

        QCOMPARE(loaded.projectName, QStringLiteral("Test Project"));
        QCOMPARE(loaded.currentEncoding, QStringLiteral("Shift-JIS"));
        QCOMPARE(loaded.romType, RomType::NES);
        QCOMPARE(loaded.byteOrder, ByteOrder::LittleEndian);
        QCOMPARE(loaded.cursorPosition, qint64(0x100));
        QCOMPARE(loaded.showPointers, true);
        QCOMPARE(loaded.showChanges, false);
        QCOMPARE(loaded.changesHexMode, true);

        // Navigation
        QCOMPARE(loaded.navigationHistory.size(), 3);
        QCOMPARE(loaded.navigationHistory[0], qint64(0x10));
        QCOMPARE(loaded.navigationHistoryIndex, 1);

        // Pointers
        QCOMPARE(loaded.pointerSnapshot.size(), 2);
        QCOMPARE(loaded.pointerSnapshot[0].first, qint64(0x1000));
        QCOMPARE(PointerListModel::decodePtrTarget(loaded.pointerSnapshot[0].second), qint64(0x2000));
        QCOMPARE(PointerListModel::decodePtrSize(loaded.pointerSnapshot[0].second), 4);
        QCOMPARE(PointerListModel::decodePtrTarget(loaded.pointerSnapshot[1].second), qint64(0x4000));
        QCOMPARE(PointerListModel::decodePtrSize(loaded.pointerSnapshot[1].second), 2);

        // Original bytes
        QCOMPARE(loaded.originalBytes.size(), 1);
        QCOMPARE(loaded.originalBytes[0].first, qint64(0x100));
        QCOMPARE(loaded.originalBytes[0].second, QByteArray("\x41\x42\x43", 3));
    }

    // ---- Save with embedded table and reload ----

    void saveAndLoadWithTable()
    {
        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());
        QString projectPath = tmpDir.path() + "/test_table.rthp";

        HexDocument doc;
        doc.filePath = tmpDir.path() + "/data.bin";
        doc.currentEncoding = QStringLiteral("ASCII");

        // Create table
        DocTableEntry entry;
        entry.name = QStringLiteral("Main Table");
        entry.isOriginal = true;
        entry.table = new TranslationTable();
        entry.table->setItem(0x41, QStringLiteral("A"));
        entry.table->setItem(0x42, QStringLiteral("B"));

        QVector<DocTableEntry> tables;
        tables.append(entry);

        QVERIFY(doc.saveProject(projectPath, tables, 0));

        // Load
        HexDocument loaded;
        QVERIFY(loaded.loadProject(projectPath));
        QCOMPARE(loaded.tables.size(), 1);
        QCOMPARE(loaded.tables[0].name, QStringLiteral("Main Table"));
        QCOMPARE(loaded.tables[0].isOriginal, true);
        QVERIFY(loaded.tables[0].table != nullptr);
        QCOMPARE(loaded.activeTableIndex, 0);

        // Clean up - table is owned by entry
        delete entry.table;
    }

    // ---- Byte order variants ----

    void saveAndLoadByteOrders()
    {
        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());

        ByteOrder orders[] = {ByteOrder::LittleEndian, ByteOrder::BigEndian, ByteOrder::SwappedBytes};
        for (auto order : orders) {
            QString projectPath = tmpDir.path() + "/test_bo.rthp";
            HexDocument doc;
            doc.byteOrder = order;
            QVector<DocTableEntry> tables;
            QVERIFY(doc.saveProject(projectPath, tables, -1));

            HexDocument loaded;
            QVERIFY(loaded.loadProject(projectPath));
            QCOMPARE(loaded.byteOrder, order);
        }
    }

    // ---- Pointer snapshot/restore with PointerListModel ----

    void snapshotAndRestorePointers()
    {
        PointerListModel model;
        model.addPointer(0x100, 0x2000, 4);
        model.addPointer(0x200, 0x3000, 2);

        HexDocument doc;
        doc.snapshotPointers(&model);
        QCOMPARE(doc.pointerSnapshot.size(), 2);

        PointerListModel model2;
        doc.restorePointers(&model2);
        QCOMPARE(model2.rowCount(), 2);
        QCOMPARE(model2.getOffset(0x100), qint64(0x2000));
        QCOMPARE(model2.getPointerSize(0x100), 4);
        QCOMPARE(model2.getOffset(0x200), qint64(0x3000));
        QCOMPARE(model2.getPointerSize(0x200), 2);
    }

    // ---- Legacy v2 save format ----

    void saveAndLoadLegacyV2()
    {
        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());
        QString projectPath = tmpDir.path() + "/test_v2.rthp";

        HexDocument doc;
        doc.filePath = tmpDir.path() + "/data.bin";
        doc.projectName = QStringLiteral("Legacy");
        doc.cursorPosition = 0x50;
        doc.useTable = false;

        TranslationTable table;
        table.setItem(0x41, QStringLiteral("A"));

        QVERIFY(doc.saveProject(projectPath, &table));

        HexDocument loaded;
        QVERIFY(loaded.loadProject(projectPath));
        QCOMPARE(loaded.projectName, QStringLiteral("Legacy"));
        QCOMPARE(loaded.cursorPosition, qint64(0x50));
    }

    // ---- Empty project ----

    void saveEmptyProject()
    {
        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());
        QString projectPath = tmpDir.path() + "/empty.rthp";

        HexDocument doc;
        QVector<DocTableEntry> tables;
        QVERIFY(doc.saveProject(projectPath, tables, -1));

        HexDocument loaded;
        QVERIFY(loaded.loadProject(projectPath));
        QCOMPARE(loaded.romType, RomType::Unknown);
        QVERIFY(loaded.pointerSnapshot.isEmpty());
    }

    // ---- Load non-existent file fails ----

    void loadNonExistentFails()
    {
        HexDocument doc;
        QVERIFY(!doc.loadProject(QStringLiteral("/tmp/nonexistent_12345.rthp")));
    }
};

QTEST_MAIN(TstHexDocument)
#include "tst_hexdocument.moc"

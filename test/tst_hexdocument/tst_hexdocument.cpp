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

    void saveAndLoadDisplaySettings()
    {
        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());
        QString projectPath = tmpDir.path() + "/display_settings.rthp";

        HexDocument doc;
        doc.showPointers = false;
        doc.showChanges = true;
        doc.changesHexMode = false;

        QVector<DocTableEntry> tables;
        QVERIFY(doc.saveProject(projectPath, tables, -1));

        HexDocument loaded;
        QVERIFY(loaded.loadProject(projectPath));
        QCOMPARE(loaded.showPointers, false);
        QCOMPARE(loaded.showChanges, true);
        QCOMPARE(loaded.changesHexMode, false);
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

    void saveAndLoadV3ActiveTableAndUseTable()
    {
        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());
        QString projectPath = tmpDir.path() + "/test_v3_active.rthp";

        HexDocument doc;
        doc.useTable = true;

        DocTableEntry t1;
        t1.name = QStringLiteral("Orig");
        t1.isOriginal = true;
        t1.table = new TranslationTable();
        t1.table->setItem(0x41, QStringLiteral("A"));

        DocTableEntry t2;
        t2.name = QStringLiteral("Current");
        t2.isOriginal = false;
        t2.table = new TranslationTable();
        t2.table->setItem(0x42, QStringLiteral("B"));

        QVector<DocTableEntry> tables;
        tables.append(t1);
        tables.append(t2);

        QVERIFY(doc.saveProject(projectPath, tables, 1));

        HexDocument loaded;
        QVERIFY(loaded.loadProject(projectPath));
        QCOMPARE(loaded.tables.size(), 2);
        QCOMPARE(loaded.activeTableIndex, 1);
        QCOMPARE(loaded.useTable, true);
        QCOMPARE(loaded.tables[0].name, QStringLiteral("Orig"));
        QCOMPARE(loaded.tables[1].name, QStringLiteral("Current"));

        delete t1.table;
        delete t2.table;
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

        DocTableEntry entry;
        entry.name = QStringLiteral("Table 1");
        entry.table = &table;
        QVector<DocTableEntry> tables;
        tables.append(entry);

        QVERIFY(doc.saveProject(projectPath, tables, 0));

        // Don't let ~HexDocument delete un-owned table
        entry.table = nullptr;

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

    // ---- originalFileSize serialization ----

    void saveAndLoadOriginalFileSize()
    {
        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());
        QString projectPath = tmpDir.path() + "/test_ofs.rthp";

        HexDocument doc;
        doc.originalFileSize = 0x20000;

        QVector<DocTableEntry> tables;
        QVERIFY(doc.saveProject(projectPath, tables, -1));

        HexDocument loaded;
        QVERIFY(loaded.loadProject(projectPath));
        QCOMPARE(loaded.originalFileSize, qint64(0x20000));
    }

    void originalFileSizeDefaultNotSaved()
    {
        QTemporaryDir tmpDir;
        QVERIFY(tmpDir.isValid());
        QString projectPath = tmpDir.path() + "/test_ofs_default.rthp";

        HexDocument doc;
        // originalFileSize = -1 by default (not set)

        QVector<DocTableEntry> tables;
        QVERIFY(doc.saveProject(projectPath, tables, -1));

        HexDocument loaded;
        QVERIFY(loaded.loadProject(projectPath));
        QCOMPARE(loaded.originalFileSize, qint64(-1));
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

#include <QTest>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QPushButton>

#include "../../src/document/SectionListModel.h"
#include "../../src/dialogs/InsertScriptDialog.h"
#include "../../src/hexeditor/hexeditor.h"
#include "../../src/dockwidgets/TablesDockWidget.h"
#include "../../src/utils/palettedetector.h"

namespace {

quint64 readPtr(HexEditor &editor, qint64 fileOffset, int ptrSize)
{
    const QByteArray raw = editor.dataAt(fileOffset, ptrSize);
    if (raw.size() != ptrSize)
        return 0;
    return decodePointer(reinterpret_cast<const uchar *>(raw.constData()), ptrSize, editor.byteOrder);
}

} // namespace

class TstGraphics : public QObject
{
    Q_OBJECT

private slots:
    void paletteDetectorGba16Color()
    {
        QByteArray data(0x80, char(0xFF));
        const int offset = 0x20;

        auto putLe = [&](int index, quint16 value) {
            data[offset + index * 2] = char(value & 0xFF);
            data[offset + index * 2 + 1] = char((value >> 8) & 0xFF);
        };

        for (int index = 0; index < 16; ++index)
            putLe(index, quint16((index & 0x1F) | (((index * 2) & 0x1F) << 5) | (((index * 3) & 0x1F) << 10)));

        PaletteDetector detector;
        const auto palettes = detector.detect(data, RomType::GBA);

        QCOMPARE(palettes.size(), 1);
        QCOMPARE(palettes.first().offset, qint64(offset));
        QCOMPARE(palettes.first().length, qint64(32));
        QCOMPARE(palettes.first().colorCount, 16);
        QCOMPARE(palettes.first().format, PaletteStorageFormat::RGB555_LE);
        QCOMPARE(palettes.first().suggestedCodec, TileCodec::Linear4bpp);
        QCOMPARE(palettes.first().colors.size(), 16);
    }

    void paletteDetectorMd16ColorBigEndian()
    {
        QByteArray data(0x80, char(0xFF));
        const int offset = 0x10;

        auto putBe = [&](int index, quint16 value) {
            data[offset + index * 2] = char((value >> 8) & 0xFF);
            data[offset + index * 2 + 1] = char(value & 0xFF);
        };

        for (int index = 0; index < 16; ++index) {
            const quint16 red = quint16((index & 0x7) << 1);
            const quint16 green = quint16(((index + 2) & 0x7) << 5);
            const quint16 blue = quint16(((index + 4) & 0x7) << 9);
            putBe(index, quint16(red | green | blue));
        }

        PaletteDetector detector;
        const auto palettes = detector.detect(data, RomType::MD);

        QCOMPARE(palettes.size(), 1);
        QCOMPARE(palettes.first().offset, qint64(offset));
        QCOMPARE(palettes.first().length, qint64(32));
        QCOMPARE(palettes.first().colorCount, 16);
        QCOMPARE(palettes.first().format, PaletteStorageFormat::MD_CRAM9_BE);
        QCOMPARE(palettes.first().suggestedCodec, TileCodec::SegaMD4bpp);
    }

    void paletteDetectorMdContiguousPalettesStaySplit()
    {
        constexpr int kPaletteCount = 20;
        QByteArray data(kPaletteCount * 32, char(0xFF));

        auto putBe = [&](int byteOffset, quint16 value) {
            data[byteOffset] = char((value >> 8) & 0xFF);
            data[byteOffset + 1] = char(value & 0xFF);
        };

        for (int paletteIndex = 0; paletteIndex < kPaletteCount; ++paletteIndex) {
            const int base = paletteIndex * 32;
            for (int colorIndex = 0; colorIndex < 16; ++colorIndex) {
                const int ramp = paletteIndex + colorIndex;
                const quint16 red = quint16((ramp & 0x7) << 1);
                const quint16 green = quint16((((ramp * 2) + 1) & 0x7) << 5);
                const quint16 blue = quint16((((ramp * 3) + 2) & 0x7) << 9);
                putBe(base + colorIndex * 2, quint16(red | green | blue));
            }
        }

        PaletteDetector detector;
        const auto palettes = detector.detect(data, RomType::MD);

        QCOMPARE(palettes.size(), kPaletteCount);
        for (int index = 0; index < palettes.size(); ++index) {
            QCOMPARE(palettes[index].offset, qint64(index * 32));
            QCOMPARE(palettes[index].length, qint64(32));
            QCOMPARE(palettes[index].colorCount, 16);
            QCOMPARE(palettes[index].format, PaletteStorageFormat::MD_CRAM9_BE);
        }
    }

    void paletteDetectorNes32ByteTable()
    {
        QByteArray data(0x80, char(0xFF));
        const int offset = 0x20;
        static const quint8 paletteBytes[32] = {
            0x0F, 0x21, 0x11, 0x01, 0x0F, 0x27, 0x17, 0x07,
            0x0F, 0x29, 0x19, 0x09, 0x0F, 0x2C, 0x1C, 0x0C,
            0x0F, 0x16, 0x27, 0x38, 0x0F, 0x11, 0x21, 0x31,
            0x0F, 0x15, 0x25, 0x35, 0x0F, 0x19, 0x29, 0x39,
        };
        for (int index = 0; index < 32; ++index)
            data[offset + index] = char(paletteBytes[index]);

        PaletteDetector detector;
        const auto palettes = detector.detect(data, RomType::NES);

        QCOMPARE(palettes.size(), 1);
        QCOMPARE(palettes.first().offset, qint64(offset));
        QCOMPARE(palettes.first().length, qint64(32));
        QCOMPARE(palettes.first().colorCount, 32);
        QCOMPARE(palettes.first().format, PaletteStorageFormat::NES_Indexed6);
        QCOMPARE(palettes.first().suggestedCodec, TileCodec::Linear2bpp);
    }

    void paletteDetectorNesAllowsDistinctSpriteFirstSlots()
    {
        QByteArray data(0x80, char(0xFF));
        const int offset = 0x20;
        static const quint8 paletteBytes[32] = {
            0x0F, 0x21, 0x11, 0x01, 0x0F, 0x27, 0x17, 0x07,
            0x0F, 0x29, 0x19, 0x09, 0x0F, 0x2C, 0x1C, 0x0C,
            0x30, 0x16, 0x27, 0x38, 0x21, 0x11, 0x21, 0x31,
            0x10, 0x15, 0x25, 0x35, 0x0F, 0x19, 0x29, 0x39,
        };
        for (int index = 0; index < 32; ++index)
            data[offset + index] = char(paletteBytes[index]);

        PaletteDetector detector;
        const auto palettes = detector.detect(data, RomType::NES);

        QCOMPARE(palettes.size(), 1);
        QCOMPARE(palettes.first().offset, qint64(offset));
        QCOMPARE(palettes.first().format, PaletteStorageFormat::NES_Indexed6);
    }

    void paletteDetectorNesRejectsLooseIndexedData()
    {
        QByteArray data(0x80, char(0xFF));
        const int offset = 0x20;
        for (int index = 0; index < 32; ++index)
            data[offset + index] = char(index);

        PaletteDetector detector;
        QVERIFY(detector.detect(data, RomType::NES).isEmpty());
    }

    void paletteDetectorRejectsNoise()
    {
        QByteArray data(0x80, char(0xAA));

        PaletteDetector detector;
        QVERIFY(detector.detect(data, RomType::GBA).isEmpty());
        QVERIFY(detector.detect(data, RomType::MD).isEmpty());
        QVERIFY(detector.detect(data, RomType::NES).isEmpty());
    }

    void tileCodecHelpers_data()
    {
        QTest::addColumn<int>("codec");
        QTest::addColumn<int>("bpp");
        QTest::addColumn<int>("bytesPerTile");

        QTest::newRow("1bpp") << int(TileCodec::Linear1bpp) << 1 << 8;
        QTest::newRow("2bpp_linear") << int(TileCodec::Linear2bpp) << 2 << 16;
        QTest::newRow("2bpp_interleaved") << int(TileCodec::Interleaved2bpp) << 2 << 16;
        QTest::newRow("3bpp") << int(TileCodec::Planar3bpp) << 3 << 24;
        QTest::newRow("4bpp_interleaved") << int(TileCodec::Interleaved4bpp) << 4 << 32;
        QTest::newRow("4bpp_linear") << int(TileCodec::Linear4bpp) << 4 << 32;
        QTest::newRow("4bpp_md") << int(TileCodec::SegaMD4bpp) << 4 << 32;
        QTest::newRow("4bpp_sms") << int(TileCodec::SegaSMS4bpp) << 4 << 32;
        QTest::newRow("8bpp") << int(TileCodec::Linear8bpp) << 8 << 64;
    }

    void tileCodecHelpers()
    {
        QFETCH(int, codec);
        QFETCH(int, bpp);
        QFETCH(int, bytesPerTile);

        const auto tc = static_cast<TileCodec>(codec);
        QCOMPARE(tileCodecBpp(tc), bpp);
        QCOMPARE(tileCodecBytesPerTile(tc), bytesPerTile);

        const QString name = QString::fromLatin1(tileCodecName(tc));
        QVERIFY(!name.trimmed().isEmpty());
    }

    void graphicsPanelToggle()
    {
        HexEditor editor;

        editor.setShowGraphicsPanel(false);
        QVERIFY(!editor.showGraphicsPanel());

        editor.setShowGraphicsPanel(true);
        QVERIFY(editor.showGraphicsPanel());

        editor.setShowGraphicsPanel(false);
        QVERIFY(!editor.showGraphicsPanel());
    }

    void globalCodecAndTileCols()
    {
        HexEditor editor;

        editor.setGlobalTileCodec(TileCodec::SegaMD4bpp);
        QCOMPARE(editor.globalTileCodec(), TileCodec::SegaMD4bpp);

        editor.setGlobalTileCols(12);
        QCOMPARE(editor.globalTileCols(), 12);

        editor.setGlobalTileCols(0);
        QCOMPARE(editor.globalTileCols(), 1);
    }

    void graphicsModeViaSectionModel()
    {
        SectionListModel sections;
        Section s;
        s.name = QStringLiteral("gfx");
        s.startOffset = 0;
        s.displayMode = SectionDisplay_Graphics;
        s.tileCodec = TileCodec::Linear2bpp;
        QVERIFY(sections.addSection(s));

        HexEditor editor;
        editor.setSectionModel(&sections);
        editor.setData(QByteArray(64, char(0xAA)));

        // Rendering path should stay stable and cursor should be valid for origin click.
        const qint64 pos = editor.cursorPosition(QPoint(0, 0));
        QVERIFY(pos >= -1);
    }

    void importScriptUpdatesAllPointers()
    {
        HexEditor editor;
        editor.byteOrder = ByteOrder::LittleEndian;
        editor.setData(QByteArray(0x80, char(0x00)));

        TranslationTable table;
        table.setItem('A', QStringLiteral("A"));
        table.setItem('B', QStringLiteral("B"));

        QVector<TableTab> tabs;
        TableTab tab;
        tab.name = QStringLiteral("T");
        tab.table = table;
        tabs.append(tab);

        InsertScriptDialog dlg(&editor);
        dlg.setAvailableTables(tabs);
        dlg.setRomProfile(4, 0);

        editor.setCursorPosition(0x20 * 2);

        auto *script = dlg.findChild<QPlainTextEdit *>("pteScript");
        auto *buttons = dlg.findChild<QDialogButtonBox *>("bbControls");
        QVERIFY(script);
        QVERIFY(buttons);

        script->setPlainText(QStringLiteral("{|0004,0008,000C:2,0010:4|}:\nA\nB\n"));

        QPushButton *ok = buttons->button(QDialogButtonBox::Ok);
        QVERIFY(ok);
        QTest::mouseClick(ok, Qt::LeftButton);

        QCOMPARE(readPtr(editor, 0x04, 4), quint64(0x20));
        QCOMPARE(readPtr(editor, 0x08, 4), quint64(0x20));
        QCOMPARE(readPtr(editor, 0x0C, 2), quint64(0x20));
        QCOMPARE(readPtr(editor, 0x10, 4), quint64(0x20));
    }

    void importScriptPointerDefaultSizeFallsBackToRomProfile()
    {
        HexEditor editor;
        editor.byteOrder = ByteOrder::LittleEndian;
        editor.setData(QByteArray(0x40, char(0x00)));

        TranslationTable table;
        table.setItem('A', QStringLiteral("A"));

        QVector<TableTab> tabs;
        TableTab tab;
        tab.name = QStringLiteral("T");
        tab.table = table;
        tabs.append(tab);

        InsertScriptDialog dlg(&editor);
        dlg.setAvailableTables(tabs);
        dlg.setRomProfile(2, 0);

        editor.setCursorPosition(0x12 * 2);

        auto *script = dlg.findChild<QPlainTextEdit *>("pteScript");
        auto *buttons = dlg.findChild<QDialogButtonBox *>("bbControls");
        QVERIFY(script);
        QVERIFY(buttons);

        // Pointer has no :N suffix -> must use ROM profile size (2 here).
        script->setPlainText(QStringLiteral("{|0006|}:\nA\n"));

        QPushButton *ok = buttons->button(QDialogButtonBox::Ok);
        QVERIFY(ok);
        QTest::mouseClick(ok, Qt::LeftButton);

        QCOMPARE(readPtr(editor, 0x06, 2), quint64(0x12));
    }
};

QTEST_MAIN(TstGraphics)
#include "tst_graphics.moc"

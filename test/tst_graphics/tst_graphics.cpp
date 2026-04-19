#include <QTest>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QPushButton>

#include "../../src/document/SectionListModel.h"
#include "../../src/dialogs/InsertScriptDialog.h"
#include "../../src/hexeditor/hexeditor.h"
#include "../../src/dockwidgets/TablesDockWidget.h"

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

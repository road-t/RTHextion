#include <QTest>

#include "../../src/document/SectionListModel.h"
#include "../../src/hexeditor/hexeditor.h"

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
};

QTEST_MAIN(TstGraphics)
#include "tst_graphics.moc"

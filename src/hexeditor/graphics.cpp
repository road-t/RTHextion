#include "hexeditor.h"
#include "internal.h"
#include "SectionListModel.h"
#include <QPainter>
#include <QImage>
#include <cstdint>
#include <random>

// ── Compute byte offset within a tile for a given pixel (px, py) ─────────

int HexEditor::byteInTileForPixel(TileCodec codec, int px, int py)
{
    switch (codec) {
    case TileCodec::Linear1bpp:       return py;               // 1 byte per row
    case TileCodec::Linear2bpp:       return py;               // bp0 at offset py
    case TileCodec::Interleaved2bpp:  return py * 2;           // interleaved pair start
    case TileCodec::Planar3bpp:       return py * 2;           // first pair
    case TileCodec::Interleaved4bpp:  return py * 2;           // first pair
    case TileCodec::Linear4bpp:       return py * 4 + px / 2;  // 4 bytes/row, 2 px/byte
    case TileCodec::SegaMD4bpp:       return py * 4 + px / 2;
    case TileCodec::SegaSMS4bpp:      return py * 4;           // 4 bitplanes per row
    case TileCodec::Linear8bpp:       return py * 8 + px;      // 1 byte per pixel
    }
    return 0;
}

// ── Reverse mapping: which pixels does a byte contribute to? ─────────────

void HexEditor::tilePixelRangeForByte(TileCodec codec, int byteInTile,
                                      int &py, int &px0, int &pxCount)
{
    py = 0; px0 = 0; pxCount = 8;
    switch (codec) {
    case TileCodec::Linear1bpp:
        py = qMin(byteInTile, 7);
        break;
    case TileCodec::Linear2bpp:
        py = qMin(byteInTile % 8, 7);   // bytes 0-7 = bp0, 8-15 = bp1
        break;
    case TileCodec::Interleaved2bpp:
        py = qMin(byteInTile / 2, 7);
        break;
    case TileCodec::Planar3bpp:
        py = (byteInTile < 16) ? qMin(byteInTile / 2, 7) : qMin(byteInTile - 16, 7);
        break;
    case TileCodec::Interleaved4bpp:
        py = qMin((byteInTile % 16) / 2, 7);
        break;
    case TileCodec::Linear4bpp:
        py = qMin(byteInTile / 4, 7);
        px0 = (byteInTile % 4) * 2;
        pxCount = 2;
        break;
    case TileCodec::SegaMD4bpp:
        py = qMin(byteInTile / 4, 7);
        px0 = (byteInTile % 4) * 2;
        pxCount = 2;
        break;
    case TileCodec::SegaSMS4bpp:
        py = qMin(byteInTile / 4, 7);
        break;
    case TileCodec::Linear8bpp:
        py = qMin(byteInTile / 8, 7);
        px0 = byteInTile % 8;
        pxCount = 1;
        break;
    }
}

// ── Set a single pixel in raw tile data ──────────────────────────────────

void HexEditor::setTilePixel(TileCodec codec, uint8_t *tile,
                             int px, int py, int colorIndex)
{
    const int bit = 7 - px;
    switch (codec) {
    case TileCodec::Linear1bpp:
        tile[py] = (tile[py] & ~(1 << bit)) | ((colorIndex & 1) << bit);
        break;
    case TileCodec::Linear2bpp:
        tile[py]     = (tile[py]     & ~(1 << bit)) | ((colorIndex & 1) << bit);
        tile[py + 8] = (tile[py + 8] & ~(1 << bit)) | (((colorIndex >> 1) & 1) << bit);
        break;
    case TileCodec::Interleaved2bpp:
        tile[py * 2]     = (tile[py * 2]     & ~(1 << bit)) | ((colorIndex & 1) << bit);
        tile[py * 2 + 1] = (tile[py * 2 + 1] & ~(1 << bit)) | (((colorIndex >> 1) & 1) << bit);
        break;
    case TileCodec::Planar3bpp:
        tile[py * 2]     = (tile[py * 2]     & ~(1 << bit)) | ((colorIndex & 1) << bit);
        tile[py * 2 + 1] = (tile[py * 2 + 1] & ~(1 << bit)) | (((colorIndex >> 1) & 1) << bit);
        tile[16 + py]    = (tile[16 + py]    & ~(1 << bit)) | (((colorIndex >> 2) & 1) << bit);
        break;
    case TileCodec::Interleaved4bpp:
        tile[py * 2]          = (tile[py * 2]          & ~(1 << bit)) | ((colorIndex & 1) << bit);
        tile[py * 2 + 1]      = (tile[py * 2 + 1]      & ~(1 << bit)) | (((colorIndex >> 1) & 1) << bit);
        tile[16 + py * 2]     = (tile[16 + py * 2]     & ~(1 << bit)) | (((colorIndex >> 2) & 1) << bit);
        tile[16 + py * 2 + 1] = (tile[16 + py * 2 + 1] & ~(1 << bit)) | (((colorIndex >> 3) & 1) << bit);
        break;
    case TileCodec::Linear4bpp: {
        const int bo = py * 4 + px / 2;
        if (px % 2 == 0)
            tile[bo] = (tile[bo] & 0xF0) | (colorIndex & 0x0F);
        else
            tile[bo] = (tile[bo] & 0x0F) | ((colorIndex & 0x0F) << 4);
        break;
    }
    case TileCodec::SegaMD4bpp: {
        const int bo = py * 4 + px / 2;
        if (px % 2 == 0)
            tile[bo] = (tile[bo] & 0x0F) | ((colorIndex & 0x0F) << 4);
        else
            tile[bo] = (tile[bo] & 0xF0) | (colorIndex & 0x0F);
        break;
    }
    case TileCodec::SegaSMS4bpp:
        tile[py * 4]     = (tile[py * 4]     & ~(1 << bit)) | ((colorIndex & 1) << bit);
        tile[py * 4 + 1] = (tile[py * 4 + 1] & ~(1 << bit)) | (((colorIndex >> 1) & 1) << bit);
        tile[py * 4 + 2] = (tile[py * 4 + 2] & ~(1 << bit)) | (((colorIndex >> 2) & 1) << bit);
        tile[py * 4 + 3] = (tile[py * 4 + 3] & ~(1 << bit)) | (((colorIndex >> 3) & 1) << bit);
        break;
    case TileCodec::Linear8bpp:
        tile[py * 8 + px] = static_cast<uint8_t>(colorIndex);
        break;
    }
}

// ── Auto-compute tile columns from current bytesPerLine and codec ────────

int HexEditor::graphicsAutoTileCols(TileCodec codec) const
{
    const int bpt = tileCodecBytesPerTile(codec);
    if (bpt <= 0) return 1;
    const int cols = (8 * _bytesPerLine) / bpt;
    return qMax(1, cols);
}

int HexEditor::graphicsResolvedTileCols(TileCodec codec, int preferredCols) const
{
    if (preferredCols > 0)
        return preferredCols;
    return graphicsAutoTileCols(codec);
}

// ── Graphics palette initialization ──────────────────────────────

void HexEditor::initGraphicsPalette(int bpp, QVector<QRgb> &palette) const
{
    const int colors = 1 << bpp;
    palette.resize(colors);
    palette[0] = qRgb(0, 0, 0);

    if (bpp == 1) {
        palette[1] = qRgb(255, 255, 255);
        return;
    }

    if (bpp == 2) {
        palette[0] = qRgb(0, 0, 0);
        palette[1] = qRgb(96, 96, 210);
        palette[2] = qRgb(180, 80, 80);
        palette[3] = qRgb(240, 240, 240);
        return;
    }

    // For higher bpp: fixed seed for stability, distribute hue
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(60, 255);
    for (int i = 1; i < colors; ++i)
        palette[i] = qRgb(dist(rng), dist(rng), dist(rng));
}

// ── Tile decode ──────────────────────────────────────────────────

void HexEditor::decodeTile(TileCodec codec, const uint8_t *src, int bytesAvail,
                           const QVector<QRgb> &palette, QRgb *dest8x8) const
{
    const int bpt = tileCodecBytesPerTile(codec);
    const int palSize = palette.size();

    // Clear tile to palette[0] (or black)
    const QRgb bg = palette.isEmpty() ? qRgb(0, 0, 0) : palette[0];
    for (int i = 0; i < 64; ++i)
        dest8x8[i] = bg;

    if (bytesAvail < bpt)
        return;

    auto pal = [&](int idx) -> QRgb {
        return (idx < palSize) ? palette[idx] : qRgb(255, 0, 255);
    };

    switch (codec) {

    case TileCodec::Linear1bpp:
        for (int row = 0; row < 8; ++row) {
            const uint8_t b = src[row];
            for (int x = 0; x < 8; ++x)
                dest8x8[row * 8 + x] = pal((b >> (7 - x)) & 1);
        }
        break;

    case TileCodec::Linear2bpp:
        for (int row = 0; row < 8; ++row) {
            const uint8_t bp0 = src[row];
            const uint8_t bp1 = src[row + 8];
            for (int x = 0; x < 8; ++x) {
                const int bit = 7 - x;
                const int idx = ((bp0 >> bit) & 1) | (((bp1 >> bit) & 1) << 1);
                dest8x8[row * 8 + x] = pal(idx);
            }
        }
        break;

    case TileCodec::Interleaved2bpp:
        for (int row = 0; row < 8; ++row) {
            const uint8_t bp0 = src[row * 2];
            const uint8_t bp1 = src[row * 2 + 1];
            for (int x = 0; x < 8; ++x) {
                const int bit = 7 - x;
                const int idx = ((bp0 >> bit) & 1) | (((bp1 >> bit) & 1) << 1);
                dest8x8[row * 8 + x] = pal(idx);
            }
        }
        break;

    case TileCodec::Planar3bpp:
        for (int row = 0; row < 8; ++row) {
            const uint8_t bp0 = src[row * 2];
            const uint8_t bp1 = src[row * 2 + 1];
            const uint8_t bp2 = src[16 + row];
            for (int x = 0; x < 8; ++x) {
                const int bit = 7 - x;
                const int idx = ((bp0 >> bit) & 1)
                              | (((bp1 >> bit) & 1) << 1)
                              | (((bp2 >> bit) & 1) << 2);
                dest8x8[row * 8 + x] = pal(idx);
            }
        }
        break;

    case TileCodec::Interleaved4bpp:
        for (int row = 0; row < 8; ++row) {
            const uint8_t bp0 = src[row * 2];
            const uint8_t bp1 = src[row * 2 + 1];
            const uint8_t bp2 = src[16 + row * 2];
            const uint8_t bp3 = src[16 + row * 2 + 1];
            for (int x = 0; x < 8; ++x) {
                const int bit = 7 - x;
                const int idx = ((bp0 >> bit) & 1)
                              | (((bp1 >> bit) & 1) << 1)
                              | (((bp2 >> bit) & 1) << 2)
                              | (((bp3 >> bit) & 1) << 3);
                dest8x8[row * 8 + x] = pal(idx);
            }
        }
        break;

    case TileCodec::Linear4bpp:
        for (int row = 0; row < 8; ++row) {
            for (int col = 0; col < 4; ++col) {
                const uint8_t b = src[row * 4 + col];
                dest8x8[row * 8 + col * 2]     = pal(b & 0x0F);
                dest8x8[row * 8 + col * 2 + 1] = pal((b >> 4) & 0x0F);
            }
        }
        break;

    case TileCodec::SegaMD4bpp:
        for (int row = 0; row < 8; ++row) {
            for (int col = 0; col < 4; ++col) {
                const uint8_t b = src[row * 4 + col];
                dest8x8[row * 8 + col * 2]     = pal((b >> 4) & 0x0F);
                dest8x8[row * 8 + col * 2 + 1] = pal(b & 0x0F);
            }
        }
        break;

    case TileCodec::SegaSMS4bpp:
        for (int row = 0; row < 8; ++row) {
            const uint8_t bp0 = src[row * 4];
            const uint8_t bp1 = src[row * 4 + 1];
            const uint8_t bp2 = src[row * 4 + 2];
            const uint8_t bp3 = src[row * 4 + 3];
            for (int x = 0; x < 8; ++x) {
                const int bit = 7 - x;
                const int idx = ((bp0 >> bit) & 1)
                              | (((bp1 >> bit) & 1) << 1)
                              | (((bp2 >> bit) & 1) << 2)
                              | (((bp3 >> bit) & 1) << 3);
                dest8x8[row * 8 + x] = pal(idx);
            }
        }
        break;

    case TileCodec::Linear8bpp:
        for (int i = 0; i < 64; ++i)
            dest8x8[i] = pal(src[i]);
        break;
    }
}

// ── Paint graphics: per-byte stub (fills background, advances cursor) ────
//
// The actual tile rendering is done by paintGraphicsArea() which is called
// once after all rows, painting a continuous canvas over the ASCII area.

void HexEditor::paintGraphicsSection(
    QPainter &painter, int pxOfsX, int /*rowStridePx*/,
    int pxPosStartY, int colIdx, int bPosLine,
    qint64 /*rowAbsOffset*/, int bytesThisRow,
    bool /*isSelectedByte*/, bool /*isHighlightedByte*/,
    const QColor &/*bgColor*/, int &pxPosAsciiX2)
{
    // On first byte of the row: fill the ASCII area background so the
    // per-character rendering doesn't leave artifacts under the tile canvas.
    if (colIdx == 0) {
        const int waveLeft = _pxPosAsciiX + kAsciiAreaLeftPaddingPx - pxOfsX;
        const int availW   = qMax(1, bytesThisRow * _pxCharWidth);
        const int cellTop  = pxPosStartY - _pxCharHeight + _pxSelectionSub + 2;
        const int cellH    = _pxCharHeight;
        painter.fillRect(waveLeft, cellTop, availW, cellH, _asciiAreaColor);
    }

    // Cursor rect — full row width
    if ((bPosLine + colIdx) == static_cast<int>(_bPosCurrent - _bPosFirst)) {
        const int waveLeft = _pxPosAsciiX + kAsciiAreaLeftPaddingPx - pxOfsX;
        const int availW   = qMax(1, bytesThisRow * _pxCharWidth);
        const int cellTop  = pxPosStartY - _pxCharHeight + _pxSelectionSub + 2;
        const int cellH    = _pxCharHeight;
        _asciiCursorRect = QRect(waveLeft - 1, cellTop - 1, availW + 2, cellH + 2);
    }

    pxPosAsciiX2 += _pxCharWidth;
}

// ── Paint continuous graphics canvas (called after all rows) ─────
//
// This paints a tile grid over the ASCII area for every visible row that
// is in graphics mode (per-section or global). Canvas height matches section
// height (1 section row == 1 tile pixel row on screen). Pixel size is reduced
// slightly relative to row height for better fit.

void HexEditor::paintGraphicsArea(QPainter &painter, int pxOfsX,
                                  int rowStridePx, int pxPosStartY)
{
    if (!_chunks)
        return;

    const qint64 fileSize = _chunks->size();

    const auto rowBytesThisRow = [this](int r) -> int {
        if (r < 0 || r >= _visualRowStartBytes.size())
            return 0;
        return (r + 1 < _visualRowStartBytes.size())
            ? static_cast<int>(_visualRowStartBytes[r + 1] - _visualRowStartBytes[r])
            : _bytesPerLine;
    };

    const auto isPadRowOfSection = [this, &rowBytesThisRow, fileSize](int r, int secIdx) -> bool {
        if (!_sectionModel || secIdx < 0 || r < 0 || r >= _visualRowStartBytes.size())
            return false;
        if (rowBytesThisRow(r) > 0)
            return false;

        int prevDataRow = r - 1;
        while (prevDataRow >= 0 && rowBytesThisRow(prevDataRow) <= 0)
            --prevDataRow;
        if (prevDataRow < 0)
            return false;

        const qint64 prevOfs = _visualRowStartBytes[prevDataRow];
        if (_sectionModel->displayModeAtOffset(prevOfs) != SectionDisplay_Graphics)
            return false;
        if (_sectionModel->sectionIndexAtOffset(prevOfs) != secIdx)
            return false;

        const Section &sec = _sectionModel->at(secIdx);
        const qint64 dataStart = sec.startOffset;
        const qint64 dataEnd = _sectionModel->endOffsetOf(secIdx, fileSize);
        const qint64 bytes = qMax<qint64>(0, dataEnd - dataStart);
        const int dataRows = static_cast<int>((bytes + _bytesPerLine - 1) / _bytesPerLine);

        const int bpt = tileCodecBytesPerTile(sec.tileCodec);
        const int tileCols = graphicsResolvedTileCols(sec.tileCodec, sec.tileCols);
        int padRows = 0;
        if (bpt > 0 && tileCols > 0) {
            const int totalTiles = static_cast<int>((bytes + bpt - 1) / bpt);
            const int tileRows = (totalTiles + tileCols - 1) / tileCols;
            const int virtualRows = tileRows * 8;
            padRows = qMax(0, virtualRows - dataRows);
        }
        if (padRows <= 0)
            return false;

        int emptiesFromPrev = 0;
        for (int rr = prevDataRow + 1; rr <= r; ++rr) {
            if (rowBytesThisRow(rr) > 0)
                return false;
            ++emptiesFromPrev;
        }
        return emptiesFromPrev <= padRows;
    };

    // Iterate over visible rows
    for (int row = 0; row < _rowsShown; ) {
        if (row >= _visualRowStartBytes.size())
            break;
        const qint64 rowAbsOfs = _visualRowStartBytes[row];
        if (rowAbsOfs > fileSize)
            break;

        const int bytesThisRow = rowBytesThisRow(row);
        if (rowAbsOfs >= fileSize && bytesThisRow > 0)
            break;

        // Determine if this row is in graphics mode
        int secMode = SectionDisplay_Default;
        if (_sectionModel)
            secMode = _sectionModel->displayModeAtOffset(rowAbsOfs);
        bool isGfx = (secMode == SectionDisplay_Graphics)
                  || (secMode == SectionDisplay_Default && _showGraphicsPanel);

        // Tail padding rows have bytesThisRow==0 and offset at section end,
        // so mode lookup can point to the next section. Re-associate them with
        // the previous graphics section.
        int secIdx = -1;
        if (bytesThisRow <= 0 && _sectionModel) {
            int prev = row - 1;
            while (prev >= 0 && rowBytesThisRow(prev) <= 0)
                --prev;
            if (prev >= 0) {
                const qint64 prevOfs = _visualRowStartBytes[prev];
                if (_sectionModel->displayModeAtOffset(prevOfs) == SectionDisplay_Graphics) {
                    const int prevSecIdx = _sectionModel->sectionIndexAtOffset(prevOfs);
                    if (prevSecIdx >= 0 && isPadRowOfSection(row, prevSecIdx)) {
                        isGfx = true;
                        secMode = SectionDisplay_Graphics;
                        secIdx = prevSecIdx;
                    }
                }
            }
        }

        if (!isGfx) {
            ++row;
            continue;
        }

        // Get section or global settings
        TileCodec codec = _globalTileCodec;
        int tileColsSetting = _globalTileCols;
        qint64 dataStart = 0;
        qint64 dataEnd   = fileSize;
        QVector<QRgb> customPalette;

        if (_sectionModel && secMode == SectionDisplay_Graphics) {
            if (secIdx < 0)
                secIdx = _sectionModel->sectionIndexAtOffset(rowAbsOfs);
            if (secIdx >= 0) {
                const Section &sec = _sectionModel->at(secIdx);
                codec    = sec.tileCodec;
                tileColsSetting = sec.tileCols;
                dataStart = sec.startOffset;
                dataEnd   = _sectionModel->endOffsetOf(secIdx, fileSize);
                customPalette = sec.palette;
            }
        }

        // Apply tile shift
        dataStart = qMax(qint64(0), qMin(dataStart + _gfxTileShift, dataEnd - 1));

        const int tileCols = graphicsResolvedTileCols(codec, tileColsSetting);
        _gfxAutoTileCols = tileCols;

        const int bpt = tileCodecBytesPerTile(codec);
        const int bpp = tileCodecBpp(codec);
        if (bpt <= 0 || tileCols <= 0) {
            ++row;
            continue;
        }

        // Empty rows can be either section headers (skip) or graphics tail
        // padding rows (keep, so canvas height matches padded section height).
        if (bytesThisRow <= 0) {
            const bool isTailPad = (_sectionModel && secMode == SectionDisplay_Graphics
                                    && secIdx >= 0 && isPadRowOfSection(row, secIdx));
            if (!isTailPad) {
                ++row;
                continue;
            }
        }

        // Find how many consecutive DATA rows share this same section
        int runLen = 1;
        while (row + runLen < _rowsShown) {
            if (row + runLen >= _visualRowStartBytes.size())
                break;
            const qint64 nextOfs = _visualRowStartBytes[row + runLen];
            if (nextOfs > fileSize)
                break;

            const int nextBytesThisRow = rowBytesThisRow(row + runLen);
            if (nextOfs >= fileSize && nextBytesThisRow > 0)
                break;

            int nextMode = SectionDisplay_Default;
            if (_sectionModel)
                nextMode = _sectionModel->displayModeAtOffset(nextOfs);

            if (nextBytesThisRow <= 0) {
                const bool keepTailPad = (_sectionModel && secIdx >= 0
                                          && isPadRowOfSection(row + runLen, secIdx));
                if (!keepTailPad)
                    break;
            }

            const bool nextGfx = (nextMode == SectionDisplay_Graphics && secIdx >= 0
                                  && _sectionModel->sectionIndexAtOffset(nextOfs) == secIdx)
                              || (nextMode == SectionDisplay_Default && _showGraphicsPanel && secIdx < 0);
            if (!nextGfx)
                break;
            ++runLen;
        }

        // Build palette (custom from section or default).
        QVector<QRgb> palette;
        initGraphicsPalette(bpp, palette);
        if (!customPalette.isEmpty()) {
            const int maxColors = 1 << bpp;
            const int copyCount = qMin(customPalette.size(), maxColors);
            for (int i = 0; i < copyCount; ++i)
                palette[i] = customPalette[i];
        }

        // Keep full row height (no gaps between rows), but make pixels narrower.
        const int pixW = qMax(2, (rowStridePx * 17) / 20); // ~85% width
        const int pixH = rowStridePx;
        const int canvasPixelCols = tileCols * 8;

        // Build tile canvas: tileCols*8 wide × 8 tall (one tile row)
        const int imgW = canvasPixelCols;
        const int imgH = 8;
        if (_tileCanvasBuffer.width() != imgW || _tileCanvasBuffer.height() != imgH)
            _tileCanvasBuffer = QImage(imgW, imgH, QImage::Format_ARGB32);

        QRgb tilePixels[64]; // decode buffer
        const int tileScreenW = 8 * pixW;

        // Graphics area X position = ASCII area start
        const int gfxAreaX = _pxPosAsciiX + kAsciiAreaLeftPaddingPx - pxOfsX;

        // Visual row in section. For tail padding rows (bytesThisRow == 0 after
        // dataEnd), continue row index past real data rows so canvas grows.
        int visualRowInSection = static_cast<int>((rowAbsOfs - dataStart) / _bytesPerLine);
        if (_sectionModel && secMode == SectionDisplay_Graphics && secIdx >= 0 && bytesThisRow <= 0 && rowAbsOfs >= dataEnd - 1) {
            // Find nearest previous data row of this section and then add
            // distance in visual rows to account for all pad rows.
            int prevDataRow = row - 1;
            while (prevDataRow >= 0) {
                if (rowBytesThisRow(prevDataRow) > 0) {
                    const qint64 prevOfs = _visualRowStartBytes[prevDataRow];
                    if (_sectionModel->displayModeAtOffset(prevOfs) == SectionDisplay_Graphics
                        && _sectionModel->sectionIndexAtOffset(prevOfs) == secIdx)
                        break;
                }
                --prevDataRow;
            }

            if (prevDataRow >= 0) {
                const qint64 prevOfs = _visualRowStartBytes[prevDataRow];
                const int prevVis = static_cast<int>((prevOfs - dataStart) / _bytesPerLine);
                visualRowInSection = prevVis + (row - prevDataRow);
            }
        }

        const int bytesPerTileRow = tileCols * bpt;
        const int firstTileRow = visualRowInSection / 8;
        const int firstPixRow  = visualRowInSection % 8;

        int screenRowsRendered = 0;
        for (int tileRow = firstTileRow; screenRowsRendered < runLen; ++tileRow) {
            _tileCanvasBuffer.fill(Qt::transparent);

            for (int tileCol = 0; tileCol < tileCols; ++tileCol) {
                const qint64 tileFileOfs = dataStart
                    + static_cast<qint64>(tileRow) * bytesPerTileRow
                    + tileCol * bpt;
                if (tileFileOfs + bpt > dataEnd)
                    continue; // absent tile -> transparent

                const qint64 bufOfs = tileFileOfs - _bPosFirst;
                const uint8_t *tileData = nullptr;
                QByteArray tmpBuf;
                if (bufOfs >= 0 && bufOfs + bpt <= _dataShown.size()) {
                    tileData = reinterpret_cast<const uint8_t *>(_dataShown.constData() + bufOfs);
                } else {
                    tmpBuf = _chunks->data(tileFileOfs, bpt);
                    if (tmpBuf.size() >= bpt)
                        tileData = reinterpret_cast<const uint8_t *>(tmpBuf.constData());
                }
                if (!tileData)
                    continue;

                decodeTile(codec, tileData, bpt, palette, tilePixels);
                for (int py = 0; py < 8; ++py) {
                    auto *scanline = reinterpret_cast<QRgb *>(_tileCanvasBuffer.scanLine(py));
                    for (int px = 0; px < 8; ++px)
                        scanline[tileCol * 8 + px] = tilePixels[py * 8 + px];
                }
            }

            const int startPixRow = (tileRow == firstTileRow) ? firstPixRow : 0;
            for (int pr = startPixRow; pr < 8 && screenRowsRendered < runLen; ++pr) {
                const int screenRow = row + screenRowsRendered;
                const int y = pxPosStartY - _pxCharHeight + screenRow * rowStridePx;
                const int screenW = canvasPixelCols * pixW;

                const QRect srcRect(0, pr, imgW, 1);
                const QRect destRect(gfxAreaX, y, screenW, pixH);

                painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
                painter.setRenderHint(QPainter::Antialiasing, false);
                painter.drawImage(destRect, _tileCanvasBuffer, srcRect);

                ++screenRowsRendered;
            }
        }

        // Draw tile grid lines (vertical between tiles, horizontal between tile rows)
        {
            const int screenW = canvasPixelCols * pixW;
            const int y0 = pxPosStartY - _pxCharHeight + row * rowStridePx;
            const int canvasH = runLen * rowStridePx;
            const int yEnd = y0 + canvasH;

            painter.setPen(QPen(QColor(255, 255, 255, 60), 1));
            // Vertical lines between tile columns
            for (int col = 1; col < tileCols; ++col) {
                const int lx = gfxAreaX + col * tileScreenW;
                if (lx < gfxAreaX + screenW)
                    painter.drawLine(lx, y0, lx, yEnd - 1);
            }
            // Horizontal separators every tile row (exactly 1 screen pixel).
            const QColor separator = this->palette().color(QPalette::Base);
            for (int sr = 0; sr <= runLen; sr += 8) {
                const int localVis = visualRowInSection + sr;
                if (localVis % 8 != 0)
                    continue;
                const int ly = pxPosStartY - _pxCharHeight + (row + sr) * rowStridePx;
                if (ly > y0 && ly < yEnd)
                    painter.fillRect(gfxAreaX, ly, screenW, 1, separator);
            }
        }

        row += runLen;
    }
}

// ── Paint graphics cursor — per-pixel highlight ─────────────────
//
// Always highlights exactly 1 tile pixel (the first pixel of the byte).

void HexEditor::paintGraphicsCursor(QPainter &painter, int pxOfsX,
                                    int rowStridePx, int pxPosStartY)
{
    Q_UNUSED(painter);
    Q_UNUSED(pxOfsX);
    Q_UNUSED(rowStridePx);
    Q_UNUSED(pxPosStartY);
}

// ── Set pixel color in the file data ────────────────────────────

void HexEditor::gfxSetPixel(Qt::MouseButton button)
{
    if (_gfxClickPixX < 0 || _gfxClickPixY < 0 || !_chunks || _readOnly)
        return;
    if (!isGraphicsAt(_bPosCurrent))
        return;

    const int palIdx = (button == Qt::RightButton) ? _gfxRightPalIdx : _gfxLeftPalIdx;

    // Find the section/global context for the cursor byte
    TileCodec codec = _globalTileCodec;
    int tileColsSetting = _globalTileCols;
    qint64 dataStart = 0;
    qint64 dataEnd = _chunks->size();
    if (_sectionModel) {
        const int secMode = _sectionModel->displayModeAtOffset(_bPosCurrent);
        if (secMode == SectionDisplay_Graphics) {
            const int secIdx = _sectionModel->sectionIndexAtOffset(_bPosCurrent);
            if (secIdx >= 0) {
                const Section &sec = _sectionModel->at(secIdx);
                codec = sec.tileCodec;
                tileColsSetting = sec.tileCols;
                dataStart = sec.startOffset;
                dataEnd = _sectionModel->endOffsetOf(secIdx, _chunks->size());
            }
        }
    }

    // Apply tile shift
    dataStart = qMax(qint64(0), qMin(dataStart + _gfxTileShift, dataEnd - 1));

    const int bpt = tileCodecBytesPerTile(codec);
    const int tileCols = graphicsResolvedTileCols(codec, tileColsSetting);
    if (bpt <= 0 || _bPosCurrent < dataStart || _bPosCurrent >= dataEnd)
        return;

    int tileIndex = -1;
    if (_gfxHighlightTileCol >= 0 && _gfxHighlightTileRow >= 0 && tileCols > 0)
        tileIndex = _gfxHighlightTileRow * tileCols + _gfxHighlightTileCol;
    else {
        const qint64 byteInSection = _bPosCurrent - dataStart;
        tileIndex = static_cast<int>(byteInSection / bpt);
    }

    const qint64 tileFileOfs = dataStart + static_cast<qint64>(tileIndex) * bpt;
    if (tileFileOfs + bpt > dataEnd)
        return;

    // Read the full tile
    QByteArray tileData = _chunks->data(tileFileOfs, bpt);
    if (tileData.size() < bpt)
        return;

    const QByteArray oldTileData = tileData;

    // Modify the pixel
    setTilePixel(codec, reinterpret_cast<uint8_t *>(tileData.data()),
                 _gfxClickPixX, _gfxClickPixY, palIdx);

    if (tileData == oldTileData)
        return;

    // Write only changed bytes into explicit undo macro to keep drawing undoable.
    _undoStack->beginMacro(tr("Draw graphics pixel"));
    for (int i = 0; i < bpt; ++i) {
        if (oldTileData.at(i) == tileData.at(i))
            continue;
        _undoStack->overwrite(tileFileOfs + i, tileData.at(i));
    }
    _undoStack->endMacro();

    refresh();

    viewport()->update();
}

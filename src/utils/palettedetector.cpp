#include "palettedetector.h"

#include <QtEndian>

#include <QSet>

#include <algorithm>
#include <limits>

namespace {

int scaleChannel(int value, int maxValue)
{
    if (maxValue <= 0)
        return 0;
    return (value * 255 + maxValue / 2) / maxValue;
}

TileCodec suggestedCodecForPalette(RomType romType, int colorCount)
{
    switch (romType) {
    case RomType::NES:
        return TileCodec::Linear2bpp;
    case RomType::GB:
    case RomType::GBC:
        return TileCodec::Interleaved2bpp;
    case RomType::SNES:
    case RomType::SNES_SMC:
    case RomType::SNES_HIROM:
    case RomType::SNES_HIROM_SMC:
        if (colorCount <= 4)
            return TileCodec::Interleaved2bpp;
        if (colorCount <= 8)
            return TileCodec::Planar3bpp;
        return TileCodec::Interleaved4bpp;
    case RomType::GBA:
        return (colorCount > 16) ? TileCodec::Linear8bpp : TileCodec::Linear4bpp;
    case RomType::MD:
    case RomType::X32:
        return TileCodec::SegaMD4bpp;
    case RomType::SMS:
    case RomType::GG:
    case RomType::SG1000:
    case RomType::ColecoVision:
        return TileCodec::SegaSMS4bpp;
    default:
        return TileCodec::Linear2bpp;
    }
}

bool isClaimed(const QByteArray &claimed, int offset, int length)
{
    for (int i = 0; i < length; ++i) {
        if (claimed.at(offset + i) != 0)
            return true;
    }
    return false;
}

void markClaimed(QByteArray &claimed, int offset, int length)
{
    for (int i = 0; i < length; ++i)
        claimed[offset + i] = 1;
}

float scoreDecodedColors(const QVector<QRgb> &colors)
{
    if (colors.size() < 4)
        return 0.0f;

    QSet<quint32> unique;
    int minR = 255, minG = 255, minB = 255;
    int maxR = 0, maxG = 0, maxB = 0;
    int nearBlackCount = 0;

    for (QRgb color : colors) {
        unique.insert(color & 0x00FFFFFFu);
        const int red = qRed(color);
        const int green = qGreen(color);
        const int blue = qBlue(color);
        minR = qMin(minR, red);
        minG = qMin(minG, green);
        minB = qMin(minB, blue);
        maxR = qMax(maxR, red);
        maxG = qMax(maxG, green);
        maxB = qMax(maxB, blue);
        if (red + green + blue <= 24)
            ++nearBlackCount;
    }

    if (unique.size() < qMax(3, colors.size() / 4))
        return 0.0f;

    const int spanSum = (maxR - minR) + (maxG - minG) + (maxB - minB);
    if (spanSum < 72)
        return 0.0f;

    if (nearBlackCount > (colors.size() * 3) / 4)
        return 0.0f;

    const float uniqueRatio = static_cast<float>(unique.size())
                            / static_cast<float>(colors.size());
    const float spanScore = qMin(1.0f, static_cast<float>(spanSum) / 384.0f);
    const float sizeScore = qMin(1.0f, static_cast<float>(colors.size()) / 32.0f);

    return 0.25f + uniqueRatio * 0.45f + spanScore * 0.20f + sizeScore * 0.10f;
}

QRgb rgb555ToRgb(quint16 word)
{
    const int red = scaleChannel(word & 0x1F, 31);
    const int green = scaleChannel((word >> 5) & 0x1F, 31);
    const int blue = scaleChannel((word >> 10) & 0x1F, 31);
    return qRgb(red, green, blue);
}

QRgb mdCramWordToRgb(quint16 word)
{
    const int red = scaleChannel((word >> 1) & 0x07, 7);
    const int green = scaleChannel((word >> 5) & 0x07, 7);
    const int blue = scaleChannel((word >> 9) & 0x07, 7);
    return qRgb(red, green, blue);
}

QRgb smsCramByteToRgb(quint8 value)
{
    const int red = scaleChannel(value & 0x03, 3);
    const int green = scaleChannel((value >> 2) & 0x03, 3);
    const int blue = scaleChannel((value >> 4) & 0x03, 3);
    return qRgb(red, green, blue);
}

QRgb ggRgb444ToRgb(quint16 word)
{
    const int red = scaleChannel(word & 0x0F, 15);
    const int green = scaleChannel((word >> 4) & 0x0F, 15);
    const int blue = scaleChannel((word >> 8) & 0x0F, 15);
    return qRgb(red, green, blue);
}

static const QRgb kNesMasterPalette[64] = {
    qRgb(0x7C, 0x7C, 0x7C), qRgb(0x00, 0x00, 0xFC), qRgb(0x00, 0x00, 0xBC), qRgb(0x44, 0x28, 0xBC),
    qRgb(0x94, 0x00, 0x84), qRgb(0xA8, 0x00, 0x20), qRgb(0xA8, 0x10, 0x00), qRgb(0x88, 0x14, 0x00),
    qRgb(0x50, 0x30, 0x00), qRgb(0x00, 0x78, 0x00), qRgb(0x00, 0x68, 0x00), qRgb(0x00, 0x58, 0x00),
    qRgb(0x00, 0x40, 0x58), qRgb(0x00, 0x00, 0x00), qRgb(0x00, 0x00, 0x00), qRgb(0x00, 0x00, 0x00),
    qRgb(0xBC, 0xBC, 0xBC), qRgb(0x00, 0x78, 0xF8), qRgb(0x00, 0x58, 0xF8), qRgb(0x68, 0x44, 0xFC),
    qRgb(0xD8, 0x00, 0xCC), qRgb(0xE4, 0x00, 0x58), qRgb(0xF8, 0x38, 0x00), qRgb(0xE4, 0x5C, 0x10),
    qRgb(0xAC, 0x7C, 0x00), qRgb(0x00, 0xB8, 0x00), qRgb(0x00, 0xA8, 0x00), qRgb(0x00, 0xA8, 0x44),
    qRgb(0x00, 0x88, 0x88), qRgb(0x00, 0x00, 0x00), qRgb(0x00, 0x00, 0x00), qRgb(0x00, 0x00, 0x00),
    qRgb(0xF8, 0xF8, 0xF8), qRgb(0x3C, 0xBC, 0xFC), qRgb(0x68, 0x88, 0xFC), qRgb(0x98, 0x78, 0xF8),
    qRgb(0xF8, 0x78, 0xF8), qRgb(0xF8, 0x58, 0x98), qRgb(0xF8, 0x78, 0x58), qRgb(0xFC, 0xA0, 0x44),
    qRgb(0xF8, 0xB8, 0x00), qRgb(0xB8, 0xF8, 0x18), qRgb(0x58, 0xD8, 0x54), qRgb(0x58, 0xF8, 0x98),
    qRgb(0x00, 0xE8, 0xD8), qRgb(0x78, 0x78, 0x78), qRgb(0x00, 0x00, 0x00), qRgb(0x00, 0x00, 0x00),
    qRgb(0xFC, 0xFC, 0xFC), qRgb(0xA4, 0xE4, 0xFC), qRgb(0xB8, 0xB8, 0xF8), qRgb(0xD8, 0xB8, 0xF8),
    qRgb(0xF8, 0xB8, 0xF8), qRgb(0xF8, 0xA4, 0xC0), qRgb(0xF0, 0xD0, 0xB0), qRgb(0xFC, 0xE0, 0xA8),
    qRgb(0xF8, 0xD8, 0x78), qRgb(0xD8, 0xF8, 0x78), qRgb(0xB8, 0xF8, 0xB8), qRgb(0xB8, 0xF8, 0xD8),
    qRgb(0x00, 0xFC, 0xFC), qRgb(0xF8, 0xD8, 0xF8), qRgb(0x00, 0x00, 0x00), qRgb(0x00, 0x00, 0x00),
};

QRgb nesIndexToRgb(quint8 value)
{
    return kNesMasterPalette[value & 0x3F];
}

bool isLikelyNesPaletteTable(const uchar *bytes, int colorCount)
{
    if (!bytes || colorCount != 32)
        return false;

    QSet<quint8> backgroundFirstSlots;
    QSet<quint8> accentColors;
    for (int paletteIndex = 0; paletteIndex < 4; ++paletteIndex) {
        backgroundFirstSlots.insert(bytes[paletteIndex * 4] & 0x3Fu);
    }
    for (int index = 0; index < 32; ++index) {
        if ((index % 4) != 0)
            accentColors.insert(bytes[index] & 0x3Fu);
    }

    // Background palettes on real NES data usually share the universal
    // background color, but sprite slot 0 is commonly varied or repurposed.
    return backgroundFirstSlots.size() == 1
        && accentColors.size() >= 6;
}

template <typename ReadWordFn, typename ValidateWordFn, typename DecodeWordFn>
QVector<DetectedPalette> scanWordPaletteBlocks(const QByteArray &data,
                                               const QVector<int> &colorCounts,
                                               int alignment,
                                               PaletteStorageFormat format,
                                               RomType romType,
                                               ReadWordFn readWord,
                                               ValidateWordFn validateWord,
                                               DecodeWordFn decodeWord)
{
    QVector<DetectedPalette> results;
    const int size = data.size();
    if (size < 8)
        return results;

    QByteArray claimed(size, 0);
    const auto *bytes = reinterpret_cast<const uchar *>(data.constData());

    QVector<int> sortedCounts = colorCounts;
    std::sort(sortedCounts.begin(), sortedCounts.end(), std::greater<int>());

    for (int colorCount : sortedCounts) {
        const int byteCount = colorCount * 2;
        const int requiredAlignment = qMax(2, qMin(alignment, byteCount));
        if (byteCount <= 0 || byteCount > size)
            continue;

        for (int pos = 0; pos + byteCount <= size; pos += 2) {
            if ((pos % requiredAlignment) != 0)
                continue;
            if (isClaimed(claimed, pos, byteCount))
                continue;

            QVector<QRgb> colors;
            colors.reserve(colorCount);
            bool valid = true;
            for (int index = 0; index < colorCount; ++index) {
                const quint16 word = readWord(bytes + pos + index * 2);
                if (!validateWord(word)) {
                    valid = false;
                    break;
                }
                colors.append(decodeWord(word));
            }
            if (!valid)
                continue;

            const float confidence = scoreDecodedColors(colors);
            if (confidence < 0.55f)
                continue;

            markClaimed(claimed, pos, byteCount);

            DetectedPalette palette;
            palette.offset = pos;
            palette.length = byteCount;
            palette.colorCount = colorCount;
            palette.format = format;
            palette.suggestedCodec = suggestedCodecForPalette(romType, colorCount);
            palette.colors = colors;
            palette.confidence = confidence;
            results.append(palette);
        }
    }

    return results;
}

template <typename ValidateByteFn, typename DecodeByteFn>
QVector<DetectedPalette> scanBytePaletteBlocks(const QByteArray &data,
                                               const QVector<int> &colorCounts,
                                               int alignment,
                                               PaletteStorageFormat format,
                                               RomType romType,
                                               ValidateByteFn validateByte,
                                               DecodeByteFn decodeByte)
{
    QVector<DetectedPalette> results;
    const int size = data.size();
    if (size < 8)
        return results;

    QByteArray claimed(size, 0);
    const auto *bytes = reinterpret_cast<const uchar *>(data.constData());

    QVector<int> sortedCounts = colorCounts;
    std::sort(sortedCounts.begin(), sortedCounts.end(), std::greater<int>());

    for (int colorCount : sortedCounts) {
        const int byteCount = colorCount;
        const int requiredAlignment = qMax(1, qMin(alignment, byteCount));
        if (byteCount <= 0 || byteCount > size)
            continue;

        for (int pos = 0; pos + byteCount <= size; ++pos) {
            if ((pos % requiredAlignment) != 0)
                continue;
            if (isClaimed(claimed, pos, byteCount))
                continue;

            QVector<QRgb> colors;
            colors.reserve(colorCount);
            bool valid = true;
            for (int index = 0; index < colorCount; ++index) {
                const quint8 value = bytes[pos + index];
                if (!validateByte(value)) {
                    valid = false;
                    break;
                }
                colors.append(decodeByte(value));
            }
            if (!valid)
                continue;

            const float confidence = scoreDecodedColors(colors);
            if (confidence < 0.55f)
                continue;

            markClaimed(claimed, pos, byteCount);

            DetectedPalette palette;
            palette.offset = pos;
            palette.length = byteCount;
            palette.colorCount = colorCount;
            palette.format = format;
            palette.suggestedCodec = suggestedCodecForPalette(romType, colorCount);
            palette.colors = colors;
            palette.confidence = confidence;
            results.append(palette);
        }
    }

    return results;
}

QVector<int> candidateRgb555Counts(RomType romType)
{
    switch (romType) {
    case RomType::SNES:
    case RomType::SNES_SMC:
    case RomType::SNES_HIROM:
    case RomType::SNES_HIROM_SMC:
        return {256, 16, 8};
    case RomType::GBC:
        return {32, 16, 8};
    case RomType::GBA:
        return {256, 16};
    default:
        return {};
    }
}

QVector<int> candidatePaletteCounts(RomType romType)
{
    switch (romType) {
    case RomType::MD:
    case RomType::X32:
        return {16};
    case RomType::SMS:
    case RomType::GG:
        return {16};
    case RomType::NES:
        return {32};
    default:
        return {};
    }
}

} // namespace

const char *paletteStorageFormatName(PaletteStorageFormat format)
{
    switch (format) {
    case PaletteStorageFormat::NES_Indexed6: return "NES Indexed 6-bit";
    case PaletteStorageFormat::RGB555_LE: return "RGB555 LE";
    case PaletteStorageFormat::MD_CRAM9_BE: return "MD CRAM 9-bit";
    case PaletteStorageFormat::SMS_CRAM6: return "SMS CRAM 6-bit";
    case PaletteStorageFormat::GG_RGB444_LE: return "GG RGB444 LE";
    case PaletteStorageFormat::Unknown:
    default:
        return "Unknown";
    }
}

const char *paletteStorageFormatMnemonic(PaletteStorageFormat format)
{
    switch (format) {
    case PaletteStorageFormat::NES_Indexed6: return "nes-indexed6";
    case PaletteStorageFormat::RGB555_LE: return "rgb555-le";
    case PaletteStorageFormat::MD_CRAM9_BE: return "md-cram9-be";
    case PaletteStorageFormat::SMS_CRAM6: return "sms-cram6";
    case PaletteStorageFormat::GG_RGB444_LE: return "gg-rgb444-le";
    case PaletteStorageFormat::Unknown:
    default:
        return "unknown";
    }
}

PaletteStorageFormat paletteStorageFormatFromMnemonic(const QString &mnemonic)
{
    const QString m = mnemonic.trimmed().toLower();
    if (m == QLatin1String("nes-indexed6"))
        return PaletteStorageFormat::NES_Indexed6;
    if (m == QLatin1String("rgb555-le"))
        return PaletteStorageFormat::RGB555_LE;
    if (m == QLatin1String("md-cram9-be"))
        return PaletteStorageFormat::MD_CRAM9_BE;
    if (m == QLatin1String("sms-cram6"))
        return PaletteStorageFormat::SMS_CRAM6;
    if (m == QLatin1String("gg-rgb444-le"))
        return PaletteStorageFormat::GG_RGB444_LE;
    return PaletteStorageFormat::Unknown;
}

QVector<PaletteStorageFormat> paletteStorageFormatsForRom(RomType romType)
{
    QVector<PaletteStorageFormat> formats;
    const auto appendUnique = [&](PaletteStorageFormat format) {
        if (format == PaletteStorageFormat::Unknown || formats.contains(format))
            return;
        formats.append(format);
    };

    switch (romType) {
    case RomType::NES:
        appendUnique(PaletteStorageFormat::NES_Indexed6);
        break;
    case RomType::SNES:
    case RomType::SNES_SMC:
    case RomType::SNES_HIROM:
    case RomType::SNES_HIROM_SMC:
    case RomType::GBC:
    case RomType::GBA:
        appendUnique(PaletteStorageFormat::RGB555_LE);
        break;
    case RomType::MD:
    case RomType::X32:
        appendUnique(PaletteStorageFormat::MD_CRAM9_BE);
        break;
    case RomType::SMS:
        appendUnique(PaletteStorageFormat::SMS_CRAM6);
        break;
    case RomType::GG:
        appendUnique(PaletteStorageFormat::GG_RGB444_LE);
        break;
    default:
        break;
    }

    for (PaletteStorageFormat format : {
             PaletteStorageFormat::RGB555_LE,
             PaletteStorageFormat::MD_CRAM9_BE,
             PaletteStorageFormat::SMS_CRAM6,
             PaletteStorageFormat::GG_RGB444_LE,
             PaletteStorageFormat::NES_Indexed6,
         }) {
        appendUnique(format);
    }

    return formats;
}

int paletteStorageFormatBytesPerColor(PaletteStorageFormat format)
{
    switch (format) {
    case PaletteStorageFormat::NES_Indexed6:
    case PaletteStorageFormat::SMS_CRAM6:
        return 1;
    case PaletteStorageFormat::RGB555_LE:
    case PaletteStorageFormat::MD_CRAM9_BE:
    case PaletteStorageFormat::GG_RGB444_LE:
        return 2;
    case PaletteStorageFormat::Unknown:
    default:
        return 0;
    }
}

QVector<QRgb> decodePaletteColors(const QByteArray &data,
                                  PaletteStorageFormat format,
                                  int maxColors)
{
    const int bytesPerColor = paletteStorageFormatBytesPerColor(format);
    if (bytesPerColor <= 0 || data.isEmpty())
        return {};

    int colorCount = data.size() / bytesPerColor;
    if (maxColors >= 0)
        colorCount = qMin(colorCount, maxColors);
    if (colorCount <= 0)
        return {};

    QVector<QRgb> colors;
    colors.reserve(colorCount);
    const auto *bytes = reinterpret_cast<const uchar *>(data.constData());
    for (int index = 0; index < colorCount; ++index) {
        switch (format) {
        case PaletteStorageFormat::NES_Indexed6:
            colors.append(nesIndexToRgb(bytes[index] & 0x3Fu));
            break;
        case PaletteStorageFormat::RGB555_LE:
            colors.append(rgb555ToRgb(qFromLittleEndian<quint16>(bytes + index * 2) & 0x7FFFu));
            break;
        case PaletteStorageFormat::MD_CRAM9_BE:
            colors.append(mdCramWordToRgb(qFromBigEndian<quint16>(bytes + index * 2)));
            break;
        case PaletteStorageFormat::SMS_CRAM6:
            colors.append(smsCramByteToRgb(bytes[index] & 0x3Fu));
            break;
        case PaletteStorageFormat::GG_RGB444_LE:
            colors.append(ggRgb444ToRgb(qFromLittleEndian<quint16>(bytes + index * 2)));
            break;
        case PaletteStorageFormat::Unknown:
        default:
            return colors;
        }
    }

    return colors;
}

QByteArray encodePaletteColor(QRgb color, PaletteStorageFormat format)
{
    const auto quantizeChannel = [](int value, int maxValue) -> quint16 {
        if (maxValue <= 0)
            return 0;
        return static_cast<quint16>(qBound(0, (value * maxValue + 127) / 255, maxValue));
    };

    const int bytesPerColor = paletteStorageFormatBytesPerColor(format);
    if (bytesPerColor <= 0)
        return {};

    QByteArray encoded(bytesPerColor, Qt::Uninitialized);
    switch (format) {
    case PaletteStorageFormat::NES_Indexed6: {
        int bestIndex = 0;
        int bestDistance = std::numeric_limits<int>::max();
        for (int index = 0; index < 64; ++index) {
            const QRgb candidate = kNesMasterPalette[index];
            const int dr = qRed(candidate) - qRed(color);
            const int dg = qGreen(candidate) - qGreen(color);
            const int db = qBlue(candidate) - qBlue(color);
            const int distance = dr * dr + dg * dg + db * db;
            if (distance < bestDistance) {
                bestDistance = distance;
                bestIndex = index;
            }
        }
        encoded[0] = static_cast<char>(bestIndex);
        break;
    }
    case PaletteStorageFormat::RGB555_LE: {
        const quint16 word = quantizeChannel(qRed(color), 31)
            | (quantizeChannel(qGreen(color), 31) << 5)
            | (quantizeChannel(qBlue(color), 31) << 10);
        qToLittleEndian<quint16>(word, reinterpret_cast<uchar *>(encoded.data()));
        break;
    }
    case PaletteStorageFormat::MD_CRAM9_BE: {
        const quint16 word = (quantizeChannel(qRed(color), 7) << 1)
            | (quantizeChannel(qGreen(color), 7) << 5)
            | (quantizeChannel(qBlue(color), 7) << 9);
        qToBigEndian<quint16>(word, reinterpret_cast<uchar *>(encoded.data()));
        break;
    }
    case PaletteStorageFormat::SMS_CRAM6: {
        const quint8 value = static_cast<quint8>(quantizeChannel(qRed(color), 3)
            | (quantizeChannel(qGreen(color), 3) << 2)
            | (quantizeChannel(qBlue(color), 3) << 4));
        encoded[0] = static_cast<char>(value);
        break;
    }
    case PaletteStorageFormat::GG_RGB444_LE: {
        const quint16 word = quantizeChannel(qRed(color), 15)
            | (quantizeChannel(qGreen(color), 15) << 4)
            | (quantizeChannel(qBlue(color), 15) << 8);
        qToLittleEndian<quint16>(word, reinterpret_cast<uchar *>(encoded.data()));
        break;
    }
    case PaletteStorageFormat::Unknown:
    default:
        return {};
    }

    return encoded;
}

QVector<DetectedPalette> PaletteDetector::detect(const QByteArray &data, RomType romType) const
{
    QVector<DetectedPalette> results;

    switch (romType) {
    case RomType::SNES:
    case RomType::SNES_SMC:
    case RomType::SNES_HIROM:
    case RomType::SNES_HIROM_SMC:
    case RomType::GBC:
    case RomType::GBA:
        results = scanWordPaletteBlocks(
            data,
            candidateRgb555Counts(romType),
            16,
            PaletteStorageFormat::RGB555_LE,
            romType,
            [](const uchar *ptr) { return qFromLittleEndian<quint16>(ptr); },
            [](quint16 word) { return (word & 0x8000u) == 0; },
            [](quint16 word) { return rgb555ToRgb(word); });
        break;

    case RomType::MD:
    case RomType::X32:
        results = scanWordPaletteBlocks(
            data,
            candidatePaletteCounts(romType),
            16,
            PaletteStorageFormat::MD_CRAM9_BE,
            romType,
            [](const uchar *ptr) { return qFromBigEndian<quint16>(ptr); },
            [](quint16 word) { return (word & 0xF111u) == 0; },
            [](quint16 word) { return mdCramWordToRgb(word); });
        break;

    case RomType::SMS:
        results = scanBytePaletteBlocks(
            data,
            candidatePaletteCounts(romType),
            16,
            PaletteStorageFormat::SMS_CRAM6,
            romType,
            [](quint8 value) { return (value & 0xC0u) == 0; },
            [](quint8 value) { return smsCramByteToRgb(value); });
        break;

    case RomType::GG:
        results = scanWordPaletteBlocks(
            data,
            candidatePaletteCounts(romType),
            16,
            PaletteStorageFormat::GG_RGB444_LE,
            romType,
            [](const uchar *ptr) { return qFromLittleEndian<quint16>(ptr); },
            [](quint16 word) { return (word & 0xF000u) == 0; },
            [](quint16 word) { return ggRgb444ToRgb(word); });
        break;

    case RomType::NES:
        results = scanBytePaletteBlocks(
            data,
            candidatePaletteCounts(romType),
            16,
            PaletteStorageFormat::NES_Indexed6,
            romType,
            [](quint8 value) { return (value & 0xC0u) == 0; },
            [](quint8 value) { return nesIndexToRgb(value); });
        results.erase(std::remove_if(results.begin(),
                                     results.end(),
                                     [&data](const DetectedPalette &palette) {
            const qint64 start = palette.offset;
            const qint64 end = start + palette.length;
            if (start < 0 || end > data.size())
                return true;
            const auto *bytes = reinterpret_cast<const uchar *>(data.constData() + start);
            return !isLikelyNesPaletteTable(bytes, palette.colorCount);
        }),
                      results.end());
        break;

    default:
        break;
    }

    std::sort(results.begin(), results.end(), [](const DetectedPalette &lhs, const DetectedPalette &rhs) {
        if (lhs.offset != rhs.offset)
            return lhs.offset < rhs.offset;
        return lhs.length > rhs.length;
    });
    return results;
}
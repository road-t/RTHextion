#ifndef PALETTEDETECTOR_H
#define PALETTEDETECTOR_H

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QRgb>

#include "romdetect.h"
#include "SectionListModel.h"

enum class PaletteStorageFormat : int {
    Unknown = 0,
    NES_Indexed6,
    RGB555_LE,
    MD_CRAM9_BE,
    SMS_CRAM6,
    GG_RGB444_LE,
};

struct DetectedPalette {
    qint64 offset = 0;
    qint64 length = 0;
    int colorCount = 0;
    PaletteStorageFormat format = PaletteStorageFormat::Unknown;
    TileCodec suggestedCodec = TileCodec::Linear2bpp;
    QVector<QRgb> colors;
    float confidence = 0.0f;
};

const char *paletteStorageFormatName(PaletteStorageFormat format);
const char *paletteStorageFormatMnemonic(PaletteStorageFormat format);
PaletteStorageFormat paletteStorageFormatFromMnemonic(const QString &mnemonic);
QVector<PaletteStorageFormat> paletteStorageFormatsForRom(RomType romType);
int paletteStorageFormatBytesPerColor(PaletteStorageFormat format);
QVector<QRgb> decodePaletteColors(const QByteArray &data,
                                  PaletteStorageFormat format,
                                  int maxColors = -1);
QByteArray encodePaletteColor(QRgb color, PaletteStorageFormat format);

class PaletteDetector
{
public:
    QVector<DetectedPalette> detect(const QByteArray &data, RomType romType) const;
};

#endif // PALETTEDETECTOR_H
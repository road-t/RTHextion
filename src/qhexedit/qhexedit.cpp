#include <QtGlobal>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStyleOptionSlider>
#include <QToolTip>
#include <QHelpEvent>
#include <QProgressDialog>
#include <QTimer>
#include <QSet>
#include <QUndoCommand>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringDecoder>
#include <QStringEncoder>
#endif

#include "qhexedit.h"
#include "hexscrollmap.h"
#include <algorithm>
#include <cmath>
#include <cerrno>
#include <QtConcurrent>
#include <iconv.h>

namespace
{
    const int kHexColumnExtraGapPx = 4;
    const int kHexRowExtraGapPx = 4;
    const int kAddressRightPaddingPx = 4;
    const int kAsciiAreaLeftPaddingPx = 4;
    const int kAsciiColumnGapSinglePx = 2; // spacing for one-cell glyphs
    const int kAsciiColumnGapWidePx = 3;   // spacing for wide/multi-byte glyphs
    const int kPointerByteSize = 4;
    const int kScrollMapWidth = 12;   // width of the side-bar scroll map strip in pixels

    // Returns true for encodings that are always one-byte-per-character
    bool isSingleByteEncoding(const QString &enc)
    {
        return enc == QLatin1String("ASCII")
            || enc == QLatin1String("ISO-8859-1")
            || enc == QLatin1String("Windows-1252")
            || enc == QLatin1String("KOI8-R")
            || enc == QLatin1String("KOI8-U")
            || enc == QLatin1String("Windows-1251")
            || enc == QLatin1String("CP-866")
            || enc == QLatin1String("Mac Cyrillic");
    }

    QVector<QByteArray> codecCandidates(const QString &enc)
    {
        if (enc == QLatin1String("UTF-16 LE"))    return { QByteArrayLiteral("UTF-16LE"), QByteArrayLiteral("UTF-16") };
        if (enc == QLatin1String("UTF-16 BE"))    return { QByteArrayLiteral("UTF-16BE"), QByteArrayLiteral("UTF-16") };
        if (enc == QLatin1String("UTF-32 LE"))    return { QByteArrayLiteral("UTF-32LE"), QByteArrayLiteral("UTF-32") };
        if (enc == QLatin1String("UTF-32 BE"))    return { QByteArrayLiteral("UTF-32BE"), QByteArrayLiteral("UTF-32") };
        if (enc == QLatin1String("Shift-JIS"))    return { QByteArrayLiteral("Shift-JIS"), QByteArrayLiteral("Shift_JIS"), QByteArrayLiteral("CP932"), QByteArrayLiteral("windows-31j") };
        if (enc == QLatin1String("EUC-JP"))       return { QByteArrayLiteral("EUC-JP"), QByteArrayLiteral("EUCJP") };
        if (enc == QLatin1String("ISO-2022-JP"))  return { QByteArrayLiteral("ISO-2022-JP") };
        if (enc == QLatin1String("GB2312"))       return { QByteArrayLiteral("GB2312"), QByteArrayLiteral("GB_2312-80") };
        if (enc == QLatin1String("GBK"))          return { QByteArrayLiteral("GBK"), QByteArrayLiteral("CP936") };
        if (enc == QLatin1String("GB18030"))      return { QByteArrayLiteral("GB18030") };
        if (enc == QLatin1String("EUC-KR"))       return { QByteArrayLiteral("EUC-KR"), QByteArrayLiteral("CP949") };
        if (enc == QLatin1String("ISO-8859-1"))   return { QByteArrayLiteral("ISO-8859-1") };
        if (enc == QLatin1String("ISO-8859-2"))   return { QByteArrayLiteral("ISO-8859-2") };
        if (enc == QLatin1String("ISO-8859-3"))   return { QByteArrayLiteral("ISO-8859-3") };
        if (enc == QLatin1String("ISO-8859-4"))   return { QByteArrayLiteral("ISO-8859-4") };
        if (enc == QLatin1String("ISO-8859-5"))   return { QByteArrayLiteral("ISO-8859-5") };
        if (enc == QLatin1String("ISO-8859-6"))   return { QByteArrayLiteral("ISO-8859-6") };
        if (enc == QLatin1String("ISO-8859-7"))   return { QByteArrayLiteral("ISO-8859-7") };
        if (enc == QLatin1String("ISO-8859-8"))   return { QByteArrayLiteral("ISO-8859-8") };
        if (enc == QLatin1String("ISO-8859-9"))   return { QByteArrayLiteral("ISO-8859-9") };
        if (enc == QLatin1String("ISO-8859-10"))  return { QByteArrayLiteral("ISO-8859-10") };
        if (enc == QLatin1String("ISO-8859-11"))  return { QByteArrayLiteral("ISO-8859-11") };
        if (enc == QLatin1String("ISO-8859-13"))  return { QByteArrayLiteral("ISO-8859-13") };
        if (enc == QLatin1String("ISO-8859-14"))  return { QByteArrayLiteral("ISO-8859-14") };
        if (enc == QLatin1String("ISO-8859-15"))  return { QByteArrayLiteral("ISO-8859-15") };
        if (enc == QLatin1String("ISO-8859-16"))  return { QByteArrayLiteral("ISO-8859-16") };
        if (enc == QLatin1String("Windows-1252")) return { QByteArrayLiteral("windows-1252"), QByteArrayLiteral("CP1252") };
        if (enc == QLatin1String("Windows-1251")) return { QByteArrayLiteral("windows-1251"), QByteArrayLiteral("CP1251") };
        if (enc == QLatin1String("KOI8-R"))       return { QByteArrayLiteral("KOI8-R") };
        if (enc == QLatin1String("KOI8-U"))       return { QByteArrayLiteral("KOI8-U") };
        if (enc == QLatin1String("Mac Cyrillic")) return { QByteArrayLiteral("x-mac-cyrillic"), QByteArrayLiteral("mac-cyrillic"), QByteArrayLiteral("maccyrillic") };
        return { enc.toLatin1() };
    }

    QChar decodeSingleByte(uint8_t byte, const QString &encoding);

    // iconv-based helpers for codecs not supported by Qt 6.2 QStringDecoder
    const char *iconvCodecName(const QString &enc)
    {
        if (enc == QLatin1String("Shift-JIS"))    return "CP932";
        if (enc == QLatin1String("EUC-JP"))       return "EUC-JP";
        if (enc == QLatin1String("ISO-2022-JP"))  return "ISO-2022-JP";
        if (enc == QLatin1String("GB2312"))       return "GB2312";
        if (enc == QLatin1String("GBK"))          return "GBK";
        if (enc == QLatin1String("GB18030"))      return "GB18030";
        if (enc == QLatin1String("EUC-KR"))       return "EUC-KR";
        if (enc == QLatin1String("UTF-8"))        return "UTF-8";
        if (enc == QLatin1String("UTF-16 LE"))    return "UTF-16LE";
        if (enc == QLatin1String("UTF-16 BE"))    return "UTF-16BE";
        if (enc == QLatin1String("UTF-32 LE"))    return "UTF-32LE";
        if (enc == QLatin1String("UTF-32 BE"))    return "UTF-32BE";
        if (enc == QLatin1String("ISO-8859-1"))   return "ISO-8859-1";
        if (enc == QLatin1String("ISO-8859-2"))   return "ISO-8859-2";
        if (enc == QLatin1String("ISO-8859-5"))   return "ISO-8859-5";
        if (enc == QLatin1String("Windows-1251")) return "CP1251";
        if (enc == QLatin1String("Windows-1252")) return "CP1252";
        if (enc == QLatin1String("KOI8-R"))       return "KOI8-R";
        if (enc == QLatin1String("KOI8-U"))       return "KOI8-U";
        if (enc == QLatin1String("CP-866"))        return "CP866";
        if (enc == QLatin1String("Mac Cyrillic")) return "MAC-CYRILLIC";
        return nullptr;
    }

    // Returns the byte sequence length at position `pos` in `data` for the given multi-byte
    // encoding. Used to feed complete sequences to iconv rather than byte-by-byte, which is
    // required because POSIX iconv EINVAL does NOT consume the incomplete lead byte.
    int iconvSeqLen(const QByteArray &data, int pos, const QString &enc)
    {
        const int rem = data.size() - pos;
        const unsigned char b = (unsigned char)data[pos];
        if (enc == QLatin1String("Shift-JIS")) {
            // Lead bytes: 0x81-0x9F or 0xE0-0xFC, trail bytes: 0x40-0x7E or 0x80-0xFC
            if ((b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC)) {
                if (rem >= 2) {
                    const unsigned char b1 = (unsigned char)data[pos + 1];
                    if ((b1 >= 0x40 && b1 <= 0x7E) || (b1 >= 0x80 && b1 <= 0xFC))
                        return 2;
                }
            }
            return 1;
        }
        if (enc == QLatin1String("EUC-JP")) {
            if (b < 0x80) return 1;
            if (b == 0x8E) return (rem >= 2) ? 2 : 1; // SS2: half-width katakana
            if (b == 0x8F) return (rem >= 3) ? 3 : 1; // SS3: JIS X 0212
            if (b >= 0xA1 && b <= 0xFE) return (rem >= 2) ? 2 : 1;
            return 1;
        }
        if (enc == QLatin1String("GB2312") || enc == QLatin1String("GBK")) {
            if (b < 0x81) return 1;
            return (rem >= 2) ? 2 : 1;
        }
        if (enc == QLatin1String("GB18030")) {
            if (b < 0x81) return 1;
            if (rem >= 2) {
                const unsigned char b1 = (unsigned char)data[pos + 1];
                if (b1 >= 0x30 && b1 <= 0x39) return (rem >= 4) ? 4 : 1; // 4-byte sequence
                return 2; // 2-byte sequence
            }
            return 1;
        }
        if (enc == QLatin1String("EUC-KR")) {
            if (b < 0x80) return 1;
            if (b >= 0xA1 && b <= 0xFE) return (rem >= 2) ? 2 : 1;
            return 1;
        }
        // ISO-2022-JP is stateful; iconv maintains internal shift state so feed 1 byte at a time
        return 1;
    }

    QString decodeWithIconv(const QByteArray &data, const QString &encoding)
    {
        const char *fromCodec = iconvCodecName(encoding);
        if (!fromCodec) {
            // Try the raw encoding name
            QByteArray encBytes = encoding.toLatin1();
            iconv_t cd = iconv_open("UTF-8", encBytes.constData());
            if (cd == (iconv_t)-1)
                return QString();
            size_t inLeft = data.size();
            char *inBuf = const_cast<char*>(data.constData());
            QByteArray outBuf(data.size() * 4 + 16, '\0');
            size_t outLeft = outBuf.size();
            char *outPtr = outBuf.data();
            iconv(cd, &inBuf, &inLeft, &outPtr, &outLeft);
            iconv_close(cd);
            return QString::fromUtf8(outBuf.constData(), outBuf.size() - outLeft);
        }
        iconv_t cd = iconv_open("UTF-8", fromCodec);
        if (cd == (iconv_t)-1)
            return QString();
        size_t inLeft = data.size();
        char *inBuf = const_cast<char*>(data.constData());
        QByteArray outBuf(data.size() * 4 + 16, '\0');
        size_t outLeft = outBuf.size();
        char *outPtr = outBuf.data();
        iconv(cd, &inBuf, &inLeft, &outPtr, &outLeft);
        iconv_close(cd);
        return QString::fromUtf8(outBuf.constData(), outBuf.size() - outLeft);
    }

    QByteArray encodeWithIconv(const QString &text, const QString &encoding)
    {
        QByteArray utf8 = text.toUtf8();
        const char *toCodec = iconvCodecName(encoding);
        QByteArray encBytes;
        if (!toCodec) {
            encBytes = encoding.toLatin1();
            toCodec = encBytes.constData();
        }
        iconv_t cd = iconv_open(toCodec, "UTF-8");
        if (cd == (iconv_t)-1)
            return text.toLatin1();
        size_t inLeft = utf8.size();
        char *inBuf = const_cast<char*>(utf8.constData());
        QByteArray outBuf(utf8.size() * 2 + 16, '\0');
        size_t outLeft = outBuf.size();
        char *outPtr = outBuf.data();
        iconv(cd, &inBuf, &inLeft, &outPtr, &outLeft);
        iconv_close(cd);
        outBuf.resize(outBuf.size() - outLeft);
        return outBuf;
    }

    QString decodeTextWithEncoding(const QByteArray &data, const QString &encoding)
    {
        if (data.isEmpty())
            return QString();

        if (encoding == QLatin1String("ASCII"))
            return QString::fromLatin1(data.constData(), data.size());

        if (isSingleByteEncoding(encoding)) {
            QString out;
            out.reserve(data.size());
            for (int i = 0; i < data.size(); ++i) {
                const QChar ch = decodeSingleByte(static_cast<uint8_t>(static_cast<unsigned char>(data[i])), encoding);
                out.append(ch.unicode() ? ch : QChar('.'));
            }
            return out;
        }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        for (const QByteArray &codecName : codecCandidates(encoding)) {
            auto dec = QStringDecoder(codecName.constData());
            if (dec.isValid())
                return dec(data);
        }
#endif
        // Fallback to iconv for codecs not supported by QStringDecoder (CJK, etc.)
        QString result = decodeWithIconv(data, encoding);
        if (!result.isEmpty())
            return result;
        return QString::fromLatin1(data.constData(), data.size());
    }

    QByteArray encodeTextWithEncoding(const QString &text, const QString &encoding)
    {
        if (encoding == QLatin1String("ASCII"))
            return text.toLatin1();

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        for (const QByteArray &codecName : codecCandidates(encoding)) {
            auto enc = QStringEncoder(codecName.constData());
            if (!enc.isValid())
                continue;
            const QByteArray out = enc(text);
            if (!out.isEmpty() || text.isEmpty())
                return out;
        }
#endif
        // Fallback to iconv for codecs not supported by QStringEncoder
        if (!isSingleByteEncoding(encoding)) {
            QByteArray result = encodeWithIconv(text, encoding);
            if (!result.isEmpty() || text.isEmpty())
                return result;
        }
        return text.toLatin1();
    }

    // ---- Static codec tables for single-byte encodings (0x80-0xFF → Unicode) ----
    static const char16_t kWindows1251[128] = {
        0x0402,0x0403,0x201A,0x0453,0x201E,0x2026,0x2020,0x2021,
        0x20AC,0x2030,0x0409,0x2039,0x040A,0x040C,0x040B,0x040F,
        0x0452,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,
        0x0000,0x2122,0x0459,0x203A,0x045A,0x045C,0x045B,0x045F,
        0x00A0,0x040E,0x045E,0x0408,0x00A4,0x0490,0x00A6,0x00A7,
        0x0401,0x00A9,0x0404,0x00AB,0x00AC,0x00AD,0x00AE,0x0407,
        0x00B0,0x00B1,0x0406,0x0456,0x0491,0x00B5,0x00B6,0x00B7,
        0x0451,0x2116,0x0454,0x00BB,0x0458,0x0405,0x0455,0x0457,
        0x0410,0x0411,0x0412,0x0413,0x0414,0x0415,0x0416,0x0417,
        0x0418,0x0419,0x041A,0x041B,0x041C,0x041D,0x041E,0x041F,
        0x0420,0x0421,0x0422,0x0423,0x0424,0x0425,0x0426,0x0427,
        0x0428,0x0429,0x042A,0x042B,0x042C,0x042D,0x042E,0x042F,
        0x0430,0x0431,0x0432,0x0433,0x0434,0x0435,0x0436,0x0437,
        0x0438,0x0439,0x043A,0x043B,0x043C,0x043D,0x043E,0x043F,
        0x0440,0x0441,0x0442,0x0443,0x0444,0x0445,0x0446,0x0447,
        0x0448,0x0449,0x044A,0x044B,0x044C,0x044D,0x044E,0x044F
    };

    static const char16_t kKOI8R[128] = {
        0x2500,0x2502,0x250C,0x2510,0x2514,0x2518,0x251C,0x2524,
        0x252C,0x2534,0x253C,0x2580,0x2584,0x2588,0x258C,0x2590,
        0x2591,0x2592,0x2593,0x2320,0x25A0,0x2219,0x221A,0x2248,
        0x2264,0x2265,0x00A0,0x2321,0x00B0,0x00B2,0x00B7,0x00F7,
        0x2550,0x2551,0x2552,0x0451,0x2553,0x2554,0x2555,0x2556,
        0x2557,0x2558,0x2559,0x255A,0x255B,0x255C,0x255D,0x255E,
        0x255F,0x2560,0x2561,0x0401,0x2562,0x2563,0x2564,0x2565,
        0x2566,0x2567,0x2568,0x2569,0x256A,0x256B,0x256C,0x00A9,
        0x044E,0x0430,0x0431,0x0446,0x0434,0x0435,0x0444,0x0433,
        0x0445,0x0438,0x0439,0x043A,0x043B,0x043C,0x043D,0x043E,
        0x043F,0x044F,0x0440,0x0441,0x0442,0x0443,0x0436,0x0432,
        0x044C,0x044B,0x0437,0x0448,0x044D,0x0449,0x0447,0x044A,
        0x042E,0x0410,0x0411,0x0426,0x0414,0x0415,0x0424,0x0413,
        0x0425,0x0418,0x0419,0x041A,0x041B,0x041C,0x041D,0x041E,
        0x041F,0x042F,0x0420,0x0421,0x0422,0x0423,0x0416,0x0412,
        0x042C,0x042B,0x0417,0x0428,0x042D,0x0429,0x0427,0x042A
    };

    static const char16_t kCP866[128] = {
        0x0410,0x0411,0x0412,0x0413,0x0414,0x0415,0x0416,0x0417,
        0x0418,0x0419,0x041A,0x041B,0x041C,0x041D,0x041E,0x041F,
        0x0420,0x0421,0x0422,0x0423,0x0424,0x0425,0x0426,0x0427,
        0x0428,0x0429,0x042A,0x042B,0x042C,0x042D,0x042E,0x042F,
        0x0430,0x0431,0x0432,0x0433,0x0434,0x0435,0x0436,0x0437,
        0x0438,0x0439,0x043A,0x043B,0x043C,0x043D,0x043E,0x043F,
        0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,
        0x2555,0x2563,0x2551,0x2557,0x255D,0x255C,0x255B,0x2510,
        0x2514,0x2534,0x252C,0x251C,0x2500,0x253C,0x255E,0x255F,
        0x255A,0x2554,0x2569,0x2566,0x2560,0x2550,0x256C,0x2567,
        0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256B,
        0x256A,0x2518,0x250C,0x2588,0x2584,0x258C,0x2590,0x2580,
        0x0440,0x0441,0x0442,0x0443,0x0444,0x0445,0x0446,0x0447,
        0x0448,0x0449,0x044A,0x044B,0x044C,0x044D,0x044E,0x044F,
        0x0401,0x0451,0x0404,0x0454,0x0407,0x0457,0x040E,0x045E,
        0x00B0,0x2219,0x00B7,0x221A,0x2116,0x00A4,0x25A0,0x00A0
    };

    // KOI8-U: same as KOI8-R but with 6 Ukrainian letters replacing box-drawing chars
    static const char16_t kKOI8U[128] = {
        0x2500,0x2502,0x250C,0x2510,0x2514,0x2518,0x251C,0x2524,
        0x252C,0x2534,0x253C,0x2580,0x2584,0x2588,0x258C,0x2590,
        0x2591,0x2592,0x2593,0x2320,0x25A0,0x2219,0x221A,0x2248,
        0x2264,0x2265,0x00A0,0x2321,0x00B0,0x00B2,0x00B7,0x00F7,
        // 0xA0-0xA7: KOI8-U replaces some box-drawing with Ukrainian letters
        0x2550,0x2551,0x2552,0x0451,0x0491,0x2554,0x0456,0x0457,
        0x2557,0x2558,0x2559,0x255A,0x255B,0x0490,0x0406,0x0407,
        0x255F,0x2560,0x2561,0x0401,0x2562,0x2563,0x2564,0x2565,
        0x2566,0x2567,0x2568,0x2569,0x256A,0x256B,0x256C,0x00A9,
        0x044E,0x0430,0x0431,0x0446,0x0434,0x0435,0x0444,0x0433,
        0x0445,0x0438,0x0439,0x043A,0x043B,0x043C,0x043D,0x043E,
        0x043F,0x044F,0x0440,0x0441,0x0442,0x0443,0x0436,0x0432,
        0x044C,0x044B,0x0437,0x0448,0x044D,0x0449,0x0447,0x044A,
        0x042E,0x0410,0x0411,0x0426,0x0414,0x0415,0x0424,0x0413,
        0x0425,0x0418,0x0419,0x041A,0x041B,0x041C,0x041D,0x041E,
        0x041F,0x042F,0x0420,0x0421,0x0422,0x0423,0x0416,0x0412,
        0x042C,0x042B,0x0417,0x0428,0x042D,0x0429,0x0427,0x042A
    };

    // Mac Cyrillic (x-mac-cyrillic / Apple Macintosh Cyrillic)
    // Mapping verified against the canonical codec table.
    static const char16_t kMacCyrillic[128] = {
        0x0410,0x0411,0x0412,0x0413,0x0414,0x0415,0x0416,0x0417,
        0x0418,0x0419,0x041A,0x041B,0x041C,0x041D,0x041E,0x041F,
        0x0420,0x0421,0x0422,0x0423,0x0424,0x0425,0x0426,0x0427,
        0x0428,0x0429,0x042A,0x042B,0x042C,0x042D,0x042E,0x042F,
        0x2020,0x00B0,0x0490,0x00A3,0x00A7,0x2022,0x00B6,0x0406,
        0x00AE,0x00A9,0x2122,0x0402,0x0452,0x2260,0x0403,0x0453,
        0x221E,0x00B1,0x2264,0x2265,0x0456,0x00B5,0x0491,0x0408,
        0x0404,0x0454,0x0407,0x0457,0x0409,0x0459,0x040A,0x045A,
        0x0458,0x0405,0x00AC,0x221A,0x0192,0x2248,0x2206,0x00AB,
        0x00BB,0x2026,0x00A0,0x040B,0x045B,0x040C,0x045C,0x0455,
        0x2013,0x2014,0x201C,0x201D,0x2018,0x2019,0x00F7,0x201E,
        0x040E,0x045E,0x040F,0x045F,0x2116,0x0401,0x0451,0x044F,
        0x0430,0x0431,0x0432,0x0433,0x0434,0x0435,0x0436,0x0437,
        0x0438,0x0439,0x043A,0x043B,0x043C,0x043D,0x043E,0x043F,
        0x0440,0x0441,0x0442,0x0443,0x0444,0x0445,0x0446,0x0447,
        0x0448,0x0449,0x044A,0x044B,0x044C,0x044D,0x044E,0x20AC
    };

    static const char16_t kWindows1252[128] = {
        0x20AC,0x0081,0x201A,0x0192,0x201E,0x2026,0x2020,0x2021,
        0x02C6,0x2030,0x0160,0x2039,0x0152,0x008D,0x017D,0x008F,
        0x0090,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,
        0x02DC,0x2122,0x0161,0x203A,0x0153,0x009D,0x017E,0x0178,
        0x00A0,0x00A1,0x00A2,0x00A3,0x00A4,0x00A5,0x00A6,0x00A7,
        0x00A8,0x00A9,0x00AA,0x00AB,0x00AC,0x00AD,0x00AE,0x00AF,
        0x00B0,0x00B1,0x00B2,0x00B3,0x00B4,0x00B5,0x00B6,0x00B7,
        0x00B8,0x00B9,0x00BA,0x00BB,0x00BC,0x00BD,0x00BE,0x00BF,
        0x00C0,0x00C1,0x00C2,0x00C3,0x00C4,0x00C5,0x00C6,0x00C7,
        0x00C8,0x00C9,0x00CA,0x00CB,0x00CC,0x00CD,0x00CE,0x00CF,
        0x00D0,0x00D1,0x00D2,0x00D3,0x00D4,0x00D5,0x00D6,0x00D7,
        0x00D8,0x00D9,0x00DA,0x00DB,0x00DC,0x00DD,0x00DE,0x00DF,
        0x00E0,0x00E1,0x00E2,0x00E3,0x00E4,0x00E5,0x00E6,0x00E7,
        0x00E8,0x00E9,0x00EA,0x00EB,0x00EC,0x00ED,0x00EE,0x00EF,
        0x00F0,0x00F1,0x00F2,0x00F3,0x00F4,0x00F5,0x00F6,0x00F7,
        0x00F8,0x00F9,0x00FA,0x00FB,0x00FC,0x00FD,0x00FE,0x00FF
    };

    // Decode one byte using the static tables for legacy single-byte encodings
    QChar decodeSingleByte(uint8_t byte, const QString &encoding)
    {
        if (byte < 0x80)
            return QChar::fromLatin1((char)byte);
        if (encoding == QLatin1String("ISO-8859-1"))
            return QChar(byte); // Latin-1 identity
        const char16_t *table = nullptr;
        if      (encoding == QLatin1String("Windows-1251"))  table = kWindows1251;
        else if (encoding == QLatin1String("KOI8-R"))        table = kKOI8R;
        else if (encoding == QLatin1String("KOI8-U"))        table = kKOI8U;
        else if (encoding == QLatin1String("CP-866"))        table = kCP866;
        else if (encoding == QLatin1String("Mac Cyrillic"))  table = kMacCyrillic;
        else if (encoding == QLatin1String("Windows-1252"))  table = kWindows1252;
        if (table) {
            char16_t cp = table[byte - 0x80];
            return (cp != 0) ? QChar(cp) : QChar(0);
        }
        return QChar::fromLatin1((char)byte);
    }

    // Decode a byte buffer using the given encoding.
    // Returns a vector the same length as 'data', where:
    //   - Lead byte positions:  non-null QString (printable char, or empty "" for non-printable)
    //   - Continuation bytes:   null QString (default-constructed, isNull()==true)
    QVector<QString> decodeBufferWithEncoding(const QByteArray &data, const QString &encoding)
    {
        const int len = data.size();
        QVector<QString> result(len); // all entries start as null QString

        // --- Single-byte encodings: every byte is a lead byte ---
        if (isSingleByteEncoding(encoding)) {
            for (int i = 0; i < len; ++i) {
                QChar ch = decodeSingleByte((uint8_t)(unsigned char)data[i], encoding);
                result[i] = (ch.unicode() != 0 && ch.isPrint()) ? QString(ch) : QStringLiteral("");
            }
            return result;
        }

        // --- UTF-8: manual byte-structure parsing (no re-encoding) ---
        if (encoding == QLatin1String("UTF-8")) {
            int pos = 0;
            while (pos < len) {
                const uint8_t b = (uint8_t)(unsigned char)data[pos];
                int seqLen = 1;
                uint32_t cp = 0;

                if (b < 0x80) {
                    cp = b; seqLen = 1;
                } else if ((b & 0xE0) == 0xC0 && b >= 0xC2) {
                    cp = b & 0x1F; seqLen = 2;
                } else if ((b & 0xF0) == 0xE0) {
                    cp = b & 0x0F; seqLen = 3;
                } else if ((b & 0xF8) == 0xF0 && b <= 0xF4) {
                    cp = b & 0x07; seqLen = 4;
                } else {
                    result[pos] = QStringLiteral("");
                    ++pos; continue;
                }

                bool valid = (pos + seqLen <= len);
                if (valid) {
                    for (int j = 1; j < seqLen; ++j) {
                        uint8_t cb = (uint8_t)(unsigned char)data[pos + j];
                        if ((cb & 0xC0) != 0x80) { valid = false; break; }
                        cp = (cp << 6) | (cb & 0x3F);
                    }
                }
                if (valid) {
                    if (cp > 0x10FFFF) valid = false;
                    if (cp >= 0xD800 && cp <= 0xDFFF) valid = false;
                }

                if (valid) {
                    QString sym;
                    if (cp <= 0xFFFF)
                        sym = QString(QChar((char16_t)cp));
                    else {
                        QChar pair[2];
                        pair[0] = QChar::highSurrogate(cp);
                        pair[1] = QChar::lowSurrogate(cp);
                        sym = QString(pair, 2);
                    }
                    result[pos] = (!sym.isEmpty() && sym[0].isPrint()) ? sym : QStringLiteral("");
                    // continuation bytes stay null (default from QVector init)
                    pos += seqLen;
                } else {
                    result[pos] = QStringLiteral("");
                    ++pos;
                }
            }
            return result;
        }

        // --- Other multi-byte encodings (Shift-JIS, GB2312, UTF-16, etc.) ---
        // Try QStringDecoder first (Qt6), then iconv as fallback
        {
            bool usedQtDecoder = false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            {
                QByteArray selectedCodec;
                bool validDecoder = false;
                for (const QByteArray &codecName : codecCandidates(encoding)) {
                    auto probe = QStringDecoder(codecName.constData());
                    if (probe.isValid()) {
                        selectedCodec = codecName;
                        validDecoder = true;
                        break;
                    }
                }

                // ISO-2022-JP is handled by a dedicated per-token iconv decoder below;
                // QStringDecoder misattributes characters to ESC-sequence bytes.
                if (validDecoder && encoding != QLatin1String("ISO-2022-JP")) {
                    usedQtDecoder = true;
                    auto dec = QStringDecoder(selectedCodec.constData());
                    int pendingStart = -1;
                    for (int i = 0; i < len; ++i) {
                        const QString out = dec(QByteArray(1, data[i]));
                        if (out.isEmpty()) {
                            if (pendingStart < 0)
                                pendingStart = i;
                            continue;
                        }
                        const int leadIndex = (pendingStart >= 0) ? pendingStart : i;
                        pendingStart = -1;
                        QString sym;
                        for (int j = 0; j < out.size(); ++j) {
                            const QChar c = out[j];
                            if (c == QChar(0xFFFD)) continue;
                            if (c.isHighSurrogate() && (j + 1) < out.size() && out[j + 1].isLowSurrogate()) {
                                const QString pair = out.mid(j, 2);
                                if (pair[0].isPrint()) { sym = pair; break; }
                                ++j; continue;
                            }
                            if (c.isPrint()) { sym = QString(c); break; }
                        }
                        result[leadIndex] = sym.isEmpty() ? QStringLiteral("") : sym;
                    }
                    const QString flushOut = dec(QByteArray());
                    if (!flushOut.isEmpty()) {
                        const int leadIndex = (pendingStart >= 0) ? pendingStart : (len - 1);
                        for (int j = 0; j < flushOut.size(); ++j) {
                            const QChar c = flushOut[j];
                            if (c == QChar(0xFFFD) || !c.isPrint()) continue;
                            result[leadIndex] = QString(c);
                            break;
                        }
                    }
                }
            }
#endif
            // iconv fallback: two-pass decode for CJK and stateful encodings.
            // Pass 1: decode entire buffer with iconv (robust: EILSEQ → U+FFFD, keep going).
            // Pass 2: determine character boundaries, assign result entries.
            // Avoids per-sequence iconv state issues and correctly handles ISO-2022-JP
            // escape sequences (which must NOT reset the iconv shift-state on each byte).
            if (!usedQtDecoder) {
                const char *fromCodec = iconvCodecName(encoding);
                QByteArray encBytes;
                if (!fromCodec) {
                    encBytes = encoding.toLatin1();
                    fromCodec = encBytes.constData();
                }

                iconv_t cd = iconv_open("UTF-8", fromCodec);
                if (cd == (iconv_t)-1) {
                    // Encoding not supported: Latin-1 fallback
                    for (int i = 0; i < len; ++i) {
                        QChar ch = QChar::fromLatin1(data[i]);
                        result[i] = ch.isPrint() ? QString(ch) : QStringLiteral("");
                    }
                } else {
                    // --- Pass 1: robust full-buffer iconv decode ---
                    QByteArray utf8Out;
                    utf8Out.reserve(len * 3 + 32);
                    const char *inPtr = data.constData();
                    size_t inLeft = (size_t)len;
                    char chunk[256];
                    while (inLeft > 0) {
                        char *outPtr2 = chunk;
                        size_t outLeft2 = sizeof(chunk);
                        size_t ret = iconv(cd, const_cast<char **>(&inPtr), &inLeft, &outPtr2, &outLeft2);
                        utf8Out.append(chunk, (int)(sizeof(chunk) - outLeft2));
                        if (ret != (size_t)-1) break;          // success: all consumed
                        if (errno == E2BIG)  continue;         // output chunk full, loop again
                        if (errno == EILSEQ) {
                            utf8Out.append("\xEF\xBF\xBD", 3); // U+FFFD for invalid byte
                            ++inPtr; --inLeft;
                            iconv(cd, nullptr, nullptr, nullptr, nullptr); // reset shift state
                            continue;
                        }
                        break; // EINVAL: incomplete sequence at end of buffer
                    }
                    // Flush any pending shift state (e.g. ISO-2022-JP end-of-file)
                    {
                        char *outPtr2 = chunk;
                        size_t outLeft2 = sizeof(chunk);
                        iconv(cd, nullptr, nullptr, &outPtr2, &outLeft2);
                        utf8Out.append(chunk, (int)(sizeof(chunk) - outLeft2));
                    }
                    iconv_close(cd);
                    const QString fullDecoded = QString::fromUtf8(utf8Out);

                    // --- Pass 2: build byte → QChar-offset map ---
                    // byteToQChar[i]:
                    //   -2 = continuation byte  → result[i] stays null
                    //   -1 = non-printable/ctrl  → result[i] = ""
                    //   >=0 = QChar offset into fullDecoded for the lead character
                    QVector<int> byteToQChar(len, -1);
                    bool iso2022jpDirect = false; // true when ISO-2022-JP fills result[] directly

                    if (encoding == QLatin1String("ISO-2022-JP")) {
                        // Per-token iconv decode: feed tokens sequentially to a fresh
                        // iconv handle that maintains the ISO-2022-JP shift state.
                        // Avoids qoff-synchronisation issues caused by EILSEQ substitutions
                        // and other Pass-1/Pass-2 mismatches in the fullDecoded approach.
                        iso2022jpDirect = true;
                        iconv_t cd2 = iconv_open("UTF-8", "ISO-2022-JP");
                        if (cd2 != (iconv_t)-1) {
                            bool inJIS = false;   // 2-byte JIS mode
                            bool inKana = false;  // 1-byte kana mode (ESC ( I / SO)
                            int pos = 0;

                            // Feed `count` bytes at data+offset to cd2.
                            // cd2 accumulates shift state across calls.
                            // Returns decoded QString only when captureOutput is true.
                            // errOut: if non-null, set to true when iconv returned an error.
                            auto feedToken = [&](int offset, int count, bool captureOutput, bool *errOut = nullptr) -> QString {
                                const char *p2 = data.constData() + offset;
                                size_t left2 = (size_t)count;
                                char obuf[32]; char *op = obuf; size_t ol = sizeof(obuf);
                                size_t ret = iconv(cd2, const_cast<char **>(&p2), &left2, &op, &ol);
                                if (errOut) *errOut = (ret == (size_t)-1);
                                if (!captureOutput) return QString();
                                int n2 = (int)(sizeof(obuf) - ol);
                                return n2 > 0 ? QString::fromUtf8(obuf, n2) : QString();
                            };

                            auto assignChar = [&](int idx, const QString &dec) {
                                if (dec.isEmpty() || dec[0] == QChar(0xFFFD)) {
                                    result[idx] = QStringLiteral("");
                                } else if (dec[0].isHighSurrogate() && dec.size() >= 2
                                           && dec[1].isLowSurrogate()) {
                                    result[idx] = dec[0].isPrint() ? dec.left(2) : QStringLiteral("");
                                } else {
                                    result[idx] = dec[0].isPrint() ? QString(dec[0]) : QStringLiteral("");
                                }
                            };

                            while (pos < len) {
                                const unsigned char b = (unsigned char)data[pos];

                                if (b == 0x1B) {
                                    // 4-byte: ESC $ ( D  (JIS X 0212)
                                    if (pos + 3 < len
                                        && (unsigned char)data[pos+1] == '$'
                                        && (unsigned char)data[pos+2] == '('
                                        && (unsigned char)data[pos+3] == 'D') {
                                        feedToken(pos, 4, false);
                                        result[pos]=result[pos+1]=result[pos+2]=result[pos+3]=QStringLiteral("");
                                        inJIS = true; inKana = false; pos += 4; continue;
                                    }
                                    // 3-byte designations
                                    if (pos + 2 < len) {
                                        const unsigned char b1 = (unsigned char)data[pos+1];
                                        const unsigned char b2 = (unsigned char)data[pos+2];
                                        bool matched = true;
                                        if      (b1 == '(' && (b2 == 'B' || b2 == 'J')) { inJIS=false; inKana=false; }
                                        else if (b1 == '(' && b2 == 'I')                { inJIS=false; inKana=true;  }
                                        else if (b1 == '$' && (b2 == 'B' || b2 == '@')) { inJIS=true;  inKana=false; }
                                        else matched = false;
                                        if (matched) {
                                            feedToken(pos, 3, false);
                                            result[pos]=result[pos+1]=result[pos+2]=QStringLiteral("");
                                            pos += 3; continue;
                                        }
                                    }
                                    // Unknown/incomplete ESC: consume 1 byte
                                    feedToken(pos, 1, false);
                                    result[pos] = QStringLiteral(""); ++pos; continue;
                                }

                                if (b == 0x0E) { feedToken(pos,1,false); inJIS=false; inKana=true;  result[pos]=QStringLiteral(""); ++pos; continue; }
                                if (b == 0x0F) { feedToken(pos,1,false); inJIS=false; inKana=false; result[pos]=QStringLiteral(""); ++pos; continue; }

                                if (inJIS && pos + 1 < len) {
                                    bool err = false;
                                    assignChar(pos, feedToken(pos, 2, true, &err));
                                    if (err) {
                                        // iconv rejected the pair and reset its shift state → sync ours
                                        inJIS = false; inKana = false;
                                    }
                                    result[pos+1] = QString(); // null = continuation byte
                                    pos += 2;
                                } else {
                                    // kana (single-byte) or ASCII
                                    assignChar(pos, feedToken(pos, 1, true));
                                    ++pos;
                                }
                            }
                            iconv_close(cd2);
                        } else {
                            // iconv not available: mark all bytes as empty
                            for (int i = 0; i < len; ++i)
                                result[i] = QStringLiteral("");
                        }
                    } else {
                        // Stateless CJK encodings (EUC-JP, Shift-JIS, GB*, EUC-KR …):
                        // use iconvSeqLen for byte-boundary detection.
                        int qoff = 0;
                        int pos = 0;
                        while (pos < len) {
                            int sl = iconvSeqLen(data, pos, encoding);
                            sl = qMin(sl, len - pos);
                            byteToQChar[pos] = qoff;
                            for (int j = 1; j < sl; ++j)
                                byteToQChar[pos + j] = -2; // continuation bytes
                            // Advance qoff by how many QChars this sequence decoded to
                            if (qoff < fullDecoded.size() && fullDecoded[qoff].isHighSurrogate()
                                && qoff + 1 < fullDecoded.size() && fullDecoded[qoff + 1].isLowSurrogate())
                                qoff += 2;
                            else
                                ++qoff;
                            pos += sl;
                        }
                    }

                    if (!iso2022jpDirect) {
                        // --- Assign result entries from fullDecoded ---
                        for (int i = 0; i < len; ++i) {
                            const int qcOff = byteToQChar[i];
                            if (qcOff == -2) {
                                // continuation: result[i] stays null (already null from QVector init)
                            } else if (qcOff == -1) {
                                result[i] = QStringLiteral(""); // non-printable control char
                            } else if (qcOff >= 0 && qcOff < fullDecoded.size()) {
                                const QChar c = fullDecoded[qcOff];
                                if (c == QChar(0xFFFD)) {
                                    result[i] = QStringLiteral(""); // substituted invalid byte
                                } else if (c.isHighSurrogate() && qcOff + 1 < fullDecoded.size()
                                           && fullDecoded[qcOff + 1].isLowSurrogate()) {
                                    const QString sym = fullDecoded.mid(qcOff, 2);
                                    result[i] = sym[0].isPrint() ? sym : QStringLiteral("");
                                } else {
                                    result[i] = c.isPrint() ? QString(c) : QStringLiteral("");
                                }
                            } else {
                                result[i] = QStringLiteral(""); // beyond decoded output range
                            }
                        }
                    }
                }
            }
        }
        return result;
    }

    void decodeBufferWithTable(const QByteArray &data,
                               const TranslationTable *tb,
                               QVector<QString> &outChars,
                               QVector<int> &outSpan)
    {
        const int len = data.size();
        outChars = QVector<QString>(len);
        outSpan = QVector<int>(len, 0);

        if (!tb || len <= 0)
            return;

        int pos = 0;
        while (pos < len)
        {
            int consumed = 0;
            const QString sym = tb->encodeBytes(data, pos, consumed);
            if (consumed <= 0)
                consumed = 1;

            outChars[pos] = sym;
            outSpan[pos] = consumed;

            for (int j = 1; j < consumed && (pos + j) < len; ++j)
                outChars[pos + j] = QString(); // continuation byte

            pos += consumed;
        }
    }

    struct PointerState
    {
        qint64 pointerOffset = -1;
        bool hasTarget = false;
        qint64 targetOffset = -1;
        int ptrSize = 4;
    };

    class PointerEditCommand : public QUndoCommand
    {
    public:
        PointerEditCommand(PointerListModel *model,
                           const QVector<PointerState> &before,
                           const QVector<PointerState> &after,
                           const QString &text,
                           QUndoCommand *parent = nullptr)
            : QUndoCommand(text, parent)
            , _model(model)
            , _before(before)
            , _after(after)
        {
        }

        void undo() override
        {
            apply(_before);
        }

        void redo() override
        {
            apply(_after);
        }

    private:
        void apply(const QVector<PointerState> &states)
        {
            if (!_model)
                return;

            for (const PointerState &state : states)
            {
                if (state.hasTarget)
                    _model->addPointer(state.pointerOffset, state.targetOffset, state.ptrSize);
                else
                    _model->dropPointer(state.pointerOffset);
            }
        }

        PointerListModel *_model = nullptr;
        QVector<PointerState> _before;
        QVector<PointerState> _after;
    };

    PointerState capturePointerState(PointerListModel *model, qint64 pointerOffset)
    {
        PointerState state;
        state.pointerOffset = pointerOffset;
        if (!model)
            return state;

        const qint64 target = model->getOffset(pointerOffset);
        if (target >= 0)
        {
            state.hasTarget = true;
            state.targetOffset = target;
            state.ptrSize = model->getPointerSize(pointerOffset);
        }

        return state;
    }
}

// ********************************************************************** Constructor, destructor

QHexEdit::QHexEdit(QWidget *parent) : QAbstractScrollArea(parent), _addressArea(true), _addressWidth(4), _asciiArea(true), _bytesPerLine(32), _hexCharsInLine(47), _highlighting(true), _overwriteMode(true), _showHexGrid(true), _readOnly(false), _showPointers(true), _hexCaps(true), _dynamicBytesPerLine(true), _editAreaIsAscii(true), _chunks(new Chunks(this)), _cursorPosition(0), _lastEventSize(0), _undoStack(new UndoStack(_chunks, this))
{
    setFont(QFont("Courier New", 14));

    auto font = QFont("Courier New", 16, 10);
    QToolTip::setFont(font);

    setAddressAreaColor(this->palette().alternateBase().color());
    setHighlightingColor(QColor(0xff, 0xff, 0x99, 0xff));
    setPointersColor(QColor(0xff, 0x99, 0x00, 0xff));
    setPointedColor(QColor(0x99, 0xff, 0x00, 0xff));
    setPointerFontColor(this->palette().color(QPalette::WindowText));
    setPointedFontColor(this->palette().color(QPalette::WindowText));
    setPointerFrameColor(QColor(0, 0, 0xff));
    setPointerFrameBackgroundColor(QColor(0, 0xFF, 0, 0x80));
    setMultibyteFrameColor(QColor(0x20, 0x20, 0x20));
    setSelectionColor(this->palette().highlight().color());
    setAddressFontColor(QPalette::WindowText);
    setAsciiAreaColor(this->palette().alternateBase().color());
    setAsciiFontColor(QPalette::WindowText);
    setHexAreaBackgroundColor(Qt::white);
    setHexAreaGridColor(QColor(0x99, 0x99, 0x99));
    _cursorCharColor = QColor(0x00, 0x60, 0xFF, 0x80);
    _zeroByteFontColor = QColor(0xCC, 0xCC, 0xCC);

    connect(&_cursorTimer, SIGNAL(timeout()), this, SLOT(updateCursor()));
    connect(verticalScrollBar(), SIGNAL(valueChanged(int)), this, SLOT(adjust()));
    connect(horizontalScrollBar(), SIGNAL(valueChanged(int)), this, SLOT(adjust()));
    connect(_undoStack, SIGNAL(indexChanged(int)), this, SLOT(dataChangedPrivate(int)));

//    _cursorTimer.setInterval(500);
//    _cursorTimer.start();

    // not the best idea though
    _pointers.setHexEdit(this);

    // Scroll map — two thin strips to the right of the viewport:
    //   _scrollMapPtr    (orange)    — pointer storage locations
    //   _scrollMapTarget (sky-blue)  — pointer target locations
    _scrollMapPtr = new HexScrollMap(this);
    _scrollMapPtr->setColor(pointersColor());
    _scrollMapPtr->hide();  // shown only when pointers are present

    _scrollMapTarget = new HexScrollMap(this);
    _scrollMapTarget->setColor(QColor(0x40, 0xbf, 0xff));  // sky-blue — pointer targets
    _scrollMapTarget->hide();

    setViewportMargins(0, 0, 0, 0);  // margins set dynamically by updateScrollMapMargins()

    // Debounce timer: coalesces rapid per-pointer model signals into one batch recompute
    _scrollMapTimer = new QTimer(this);
    _scrollMapTimer->setSingleShot(true);
    _scrollMapTimer->setInterval(200);  // ms — waits for pointer search to complete
    connect(_scrollMapTimer, &QTimer::timeout,
            this, &QHexEdit::scheduleScrollMapCompute);

    // Background computation result handler
    _scrollMapWatcher = new QFutureWatcher<ScrollMapMarkers>(this);
    connect(_scrollMapWatcher, &QFutureWatcher<ScrollMapMarkers>::finished,
            this, [this]() {
        if (_scrollMapWatcher->isCanceled()) return;
        const auto &r = _scrollMapWatcher->result();
        if (_scrollMapPtr) {
            _scrollMapPtr->setTickOffsets(r.ptrYToOff);
            _scrollMapPtr->setTicks(r.ptrYs);
        }
        if (_scrollMapTarget) {
            _scrollMapTarget->setTickOffsets(r.targetYToOff);
            _scrollMapTarget->setTicks(r.targetYs);
        }
    });

    // Recompute when either strip height changes (Y-mapping depends on height).
    // Goes through debounce timer to avoid rapid recomputes during window resize.
    connect(_scrollMapPtr,    &HexScrollMap::heightChanged, this, &QHexEdit::updateScrollMap);
    connect(_scrollMapTarget, &HexScrollMap::heightChanged, this, &QHexEdit::updateScrollMap);

    // Click on a tick → jump to that exact byte offset (centered in viewport)
    auto scrollMapJump = [this](qint64 off) {
        const int bpl = qMax(1, _bytesPerLine);
        const int targetLine = static_cast<int>(off / bpl);
        const int targetValue = qBound(0,
                                       targetLine - verticalScrollBar()->pageStep() / 2,
                                       verticalScrollBar()->maximum());
        verticalScrollBar()->setValue(targetValue);
        setCursorPosition(off * 2);
        resetSelection(off * 2);
        ensureVisible();
    };
    connect(_scrollMapPtr,    &HexScrollMap::tickClicked, this, scrollMapJump);
    connect(_scrollMapTarget, &HexScrollMap::tickClicked, this, scrollMapJump);

    // Kick debounce timer on any pointer model change
    auto onModelChanged = [this]() { updateScrollMap(); };
    connect(&_pointers, &QAbstractTableModel::modelReset,   this, onModelChanged);
    connect(&_pointers, &QAbstractTableModel::rowsInserted, this, onModelChanged);
    connect(&_pointers, &QAbstractTableModel::rowsRemoved,  this, onModelChanged);
    connect(&_pointers, &QAbstractTableModel::dataChanged,  this, onModelChanged);
    connect(&_pointers, &PointerListModel::pointersChanged, this, onModelChanged);

    // Enable mouse tracking to get mouseMoveEvent() on hover (not just on drag)
    viewport()->setMouseTracking(true);

    init();
}

QHexEdit::~QHexEdit()
{
}

// ********************************************************************** Properties

void QHexEdit::setAddressArea(bool addressArea)
{
    _addressArea = addressArea;
    
    if (_dynamicBytesPerLine)
        resizeEvent(nullptr);
    else
        adjust();
    
    setCursorPosition(_cursorPosition);
    viewport()->update();
}

bool QHexEdit::addressArea()
{
    return _addressArea;
}

void QHexEdit::setAddressAreaColor(const QColor &color)
{
    _addressAreaColor = color;
    viewport()->update();
}

QColor QHexEdit::addressAreaColor()
{
    return _addressAreaColor;
}

void QHexEdit::setAddressFontColor(const QColor &color)
{
    _addressFontColor = color;
    viewport()->update();
}

QColor QHexEdit::addressFontColor()
{
    return _addressFontColor;
}

void QHexEdit::setAsciiAreaColor(const QColor &color)
{
    _asciiAreaColor = color;
    viewport()->update();
}

QColor QHexEdit::asciiAreaColor()
{
    return _asciiAreaColor;
}

void QHexEdit::setAsciiFontColor(const QColor &color)
{
    _asciiFontColor = color;
    viewport()->update();
}

QColor QHexEdit::asciiFontColor()
{
    return _asciiFontColor;
}

QChar QHexEdit::nonPrintableNoTableChar() const
{
    return _nonPrintableNoTableChar;
}

void QHexEdit::setNonPrintableNoTableChar(const QChar &ch)
{
    _nonPrintableNoTableChar = ch.isNull() ? QChar(0x25AA) : ch;
    invalidateAsciiAreaWidthCache();
    updateAsciiAreaMaxWidth();
    viewport()->update();
}

QChar QHexEdit::notInTableChar() const
{
    return _notInTableChar;
}

void QHexEdit::setNotInTableChar(const QChar &ch)
{
    _notInTableChar = ch.isNull() ? QChar(0x25A1) : ch;
    invalidateAsciiAreaWidthCache();
    updateAsciiAreaMaxWidth();
    viewport()->update();
}

QColor QHexEdit::cursorCharColor()
{
    return _cursorCharColor;
}

void QHexEdit::setCursorCharColor(const QColor &color)
{
    _cursorCharColor = color;
    viewport()->update();
}

QColor QHexEdit::cursorFrameColor()
{
    return _cursorFrameColor;
}

void QHexEdit::setCursorFrameColor(const QColor &color)
{
    _cursorFrameColor = color;
    viewport()->update();
}

void QHexEdit::setHexFontColor(const QColor &color)
{
    _hexFontColor = color;
    viewport()->update();
}

QColor QHexEdit::hexFontColor()
{
    return _hexFontColor;
}

QColor QHexEdit::zeroByteFontColor()
{
    return _zeroByteFontColor;
}

void QHexEdit::setZeroByteFontColor(const QColor &color)
{
    _zeroByteFontColor = color;
    viewport()->update();
}

void QHexEdit::setAddressOffset(qint64 addressOffset)
{
    _addressOffset = addressOffset;
    adjust();
    setCursorPosition(_cursorPosition);
    viewport()->update();
}

qint64 QHexEdit::addressOffset()
{
    return _addressOffset;
}

void QHexEdit::setAddressWidth(int addressWidth)
{
    _addressWidth = addressWidth;
    adjust();
    setCursorPosition(_cursorPosition);
    viewport()->update();
}

int QHexEdit::addressWidth()
{
    auto size = _chunks->size();

    int n = 1;

    if (size > Q_INT64_C(0x100000000))
    {
        n += 8;
        size /= Q_INT64_C(0x100000000);
    }
    else if (size > 0x10000)
    {
        n += 4;
        size /= 0x10000;
    }
    else if (size > 0x100)
    {
        n += 2;
        size /= 0x100;
    }
    else if (size > 0x10)
    {
        n += 1;
    }
    else if (n > _addressWidth)
    {
        return n;
    }

    return _addressWidth;
}

void QHexEdit::setAsciiArea(bool asciiArea)
{
    if (!asciiArea)
        _editAreaIsAscii = false;
    _asciiArea = asciiArea;
    invalidateAsciiAreaWidthCache();
    updateAsciiAreaMaxWidth();
    
    if (_dynamicBytesPerLine)
        resizeEvent(nullptr);
    else
        adjust();
    
    setCursorPosition(_cursorPosition);
    viewport()->update();
}

bool QHexEdit::asciiArea()
{
    return _asciiArea;
}

void QHexEdit::setBytesPerLine(int count)
{
    _bytesPerLine = count;
    _hexCharsInLine = count * 3 - 1;
    updateAsciiAreaMaxWidth();

    adjust();
    setCursorPosition(_cursorPosition);
    viewport()->update();
}

int QHexEdit::bytesPerLine()
{
    return _bytesPerLine;
}

void QHexEdit::setCursorPosition(qint64 position)
{
    // 1. Check, if cursor in range?
    if (position > (_chunks->size() * 2 - 1))
        position = _chunks->size() * 2 - (_overwriteMode ? 1 : 0);

    if (position < 0)
        position = 0;

    // 2. Calc new position of cursor
    _bPosCurrent = position / 2;
    auto line = ((_bPosCurrent - _bPosFirst) / _bytesPerLine + 1);
    const int rowStridePx = _pxCharHeight + kHexRowExtraGapPx;
    _pxCursorY = line * rowStridePx;
    auto x = (position % (2 * _bytesPerLine));

    _cursorPosition = position;

    // ascii area cursor
    const int byteInLine = x / 2;
    int asciiOffsetPx = 0;
    int asciiCursorWidthPx = _pxCharWidth;

    if (_asciiArea)
    {
        ensureTableDisplayCache();
        ensureEncodingDisplayCache();

        const auto slotGapPx = [this](int baseWidth) {
            return (baseWidth > _pxCharWidth) ? kAsciiColumnGapWidePx : kAsciiColumnGapSinglePx;
        };

        asciiCursorWidthPx = _pxCharWidth + 2;
        asciiOffsetPx = 0;

        if (_tb && !_tbDisplayChars.isEmpty())
        {
            const QFontMetrics fm(font());
            const int bufRowStart = (int)((_bPosCurrent - _bPosFirst) / _bytesPerLine * _bytesPerLine);
            int lastLeadOffsetPx = 0;
            int lastLeadWidth = _pxCharWidth + 2;

            for (int col = 0; col < byteInLine; ++col)
            {
                const int idx = bufRowStart + col;
                if (idx < 0 || idx >= _tbDisplayChars.size()) break;
                if (_tbDisplayChars[idx].isNull()) continue;

                lastLeadOffsetPx = asciiOffsetPx;
                const int baseW = qMax(_pxCharWidth, fm.horizontalAdvance(_tbDisplayChars[idx]));
                const int w = baseW + slotGapPx(baseW);
                lastLeadWidth = baseW + 2;
                asciiOffsetPx += w;
            }

            const int curIdx = bufRowStart + byteInLine;
            if (curIdx >= 0 && curIdx < _tbDisplayChars.size())
            {
                if (_tbDisplayChars[curIdx].isNull())
                {
                    asciiOffsetPx = lastLeadOffsetPx;
                    asciiCursorWidthPx = lastLeadWidth;
                }
                else
                {
                    const int baseW = qMax(_pxCharWidth, fm.horizontalAdvance(_tbDisplayChars[curIdx]));
                    asciiCursorWidthPx = baseW + 2;
                }
            }
        }
        else if (!_encodingChars.isEmpty())
        {
            const QFontMetrics fm(font());
            const int bufRowStart = (int)((_bPosCurrent - _bPosFirst) / _bytesPerLine * _bytesPerLine);
            int lastLeadOffsetPx = 0;
            int lastLeadWidth = _pxCharWidth + 2;

            for (int col = 0; col < byteInLine; ++col)
            {
                const int idx = bufRowStart + col;
                if (idx < 0 || idx >= _encodingChars.size()) break;
                if (_encodingChars[idx].isNull()) continue;
                lastLeadOffsetPx = asciiOffsetPx;
                const int baseW = qMax(_pxCharWidth, fm.horizontalAdvance(_encodingChars[idx]));
                const int w = baseW + slotGapPx(baseW);
                lastLeadWidth = baseW + 2;
                asciiOffsetPx += w;
            }

            const int curIdx = bufRowStart + byteInLine;
            if (curIdx >= 0 && curIdx < _encodingChars.size())
            {
                if (_encodingChars[curIdx].isNull())
                {
                    asciiOffsetPx = lastLeadOffsetPx;
                    asciiCursorWidthPx = lastLeadWidth;
                }
                else
                {
                    const int baseW = qMax(_pxCharWidth, fm.horizontalAdvance(_encodingChars[curIdx]));
                    asciiCursorWidthPx = baseW + 2;
                }
            }
        }
        else
        {
            for (int col = 0; col < byteInLine; ++col)
            {
                const qint64 bytePos = (_bPosCurrent / _bytesPerLine) * _bytesPerLine + col;
                const uint8_t bv = (bytePos < _dataShown.size())
                    ? static_cast<uint8_t>(_dataShown.at(bytePos))
                    : 0;
                const int baseW = (_tb && !_tbSymbolWidthPxCache.isEmpty()) ? _tbSymbolWidthPxCache[bv] : _pxCharWidth;
                asciiOffsetPx += baseW + slotGapPx(baseW);
            }

            const qint64 curBytePos = (_bPosCurrent / _bytesPerLine) * _bytesPerLine + byteInLine;
            const uint8_t curBv = (curBytePos < _dataShown.size())
                ? static_cast<uint8_t>(_dataShown.at(curBytePos))
                : 0;
            const int baseW = (_tb && !_tbSymbolWidthPxCache.isEmpty()) ? _tbSymbolWidthPxCache[curBv] : _pxCharWidth;
            asciiCursorWidthPx = baseW + 2;
        }
    }

    // Save old rects so both old and new positions get repainted (clears stale cursor artifacts)
    const QRect oldAsciiCursorRect = _asciiCursorRect;
    const QRect oldHexCursorRect   = _hexCursorRect;

    _pxCursorX = _pxPosAsciiX + kAsciiAreaLeftPaddingPx + asciiOffsetPx;

    _asciiCursorRect = QRect(_pxCursorX - horizontalScrollBar()->value() - 2, _pxCursorY - _pxCharHeight + _pxSelectionSub - 4, asciiCursorWidthPx, _pxCharHeight + 2);

    // hex area cursor
    const int hexStridePx = 3 * _pxCharWidth + kHexColumnExtraGapPx;
    _pxCursorX = (x / 2) * hexStridePx + _pxPosHexX;

    {
        const int scrollX = horizontalScrollBar()->value();
        const int bufIdx  = (int)(_bPosCurrent - _bPosFirst);
        int leadBufIdx = bufIdx;
        int span = 1;
        if (!_tbDisplayChars.isEmpty() && bufIdx >= 0 && bufIdx < _tbDisplayChars.size()) {
            int li = bufIdx;
            while (li > 0 && _tbDisplayChars[li].isNull()) --li;
            if (li >= 0 && li < _tbDisplaySpan.size() && _tbDisplaySpan[li] > 1)
                { leadBufIdx = li; span = _tbDisplaySpan[li]; }
        } else if (!_encodingChars.isEmpty() && bufIdx >= 0 && bufIdx < _encodingChars.size()) {
            int li = bufIdx;
            while (li > 0 && _encodingChars[li].isNull()) --li;
            if (li >= 0 && li < _encodingSpan.size() && _encodingSpan[li] > 1)
                { leadBufIdx = li; span = _encodingSpan[li]; }
        }
        int hexCursorWidthPx = _pxCharWidth * 2 + 4;
        int hexCursorStartPx = _pxCursorX;
        if (span > 1 && _bytesPerLine > 0) {
            const int curRowStart = (bufIdx / _bytesPerLine) * _bytesPerLine;
            const int segStart    = qMax(leadBufIdx, curRowStart);
            const int segEnd      = qMin(leadBufIdx + span, curRowStart + _bytesPerLine);
            const int bytesOnRow  = segEnd - segStart;
            if (bytesOnRow > 0) {
                hexCursorWidthPx = (bytesOnRow - 1) * hexStridePx + _pxCharWidth * 2 + 4;
                hexCursorStartPx = (segStart % _bytesPerLine) * hexStridePx + _pxPosHexX;
            }
        }
        _hexCursorRect = QRect(hexCursorStartPx - scrollX - 2,
                               _pxCursorY - _pxCharHeight + _pxSelectionSub - 4,
                               hexCursorWidthPx, _pxCharHeight + 2);
        _cursorMultiByteSpan = span;
    }

    // 3. Immediately draw new cursor (also repaint old positions to clear stale frames)
    viewport()->update(oldAsciiCursorRect);
    viewport()->update(oldHexCursorRect);
    viewport()->update(_asciiCursorRect);
    viewport()->update(_hexCursorRect);
    // For multi-byte groups, the cursor spans multiple rows / slots; a full repaint
    // is needed to correctly clear old highlights and fill both row segments.
    {
        const int bufIdx = (int)(_bPosCurrent - _bPosFirst);
        bool isMultiByte = false;
        if (!_tbDisplayChars.isEmpty() && bufIdx >= 0 && bufIdx < _tbDisplayChars.size()) {
            int li = bufIdx;
            while (li > 0 && _tbDisplayChars[li].isNull()) --li;
            if (li >= 0 && li < _tbDisplaySpan.size() && _tbDisplaySpan[li] > 1)
                isMultiByte = true;
        } else if (!_encodingChars.isEmpty() && bufIdx >= 0 && bufIdx < _encodingChars.size()) {
            int li = bufIdx;
            while (li > 0 && _encodingChars[li].isNull()) --li;
            if (li >= 0 && li < _encodingSpan.size() && _encodingSpan[li] > 1)
                isMultiByte = true;
        }
        if (isMultiByte)
            viewport()->update();
    }

    emit currentAddressChanged(_bPosCurrent);
}

qint64 QHexEdit::cursorPosition(QPoint pos)
{
    // Calc cursor position depending on a graphical position
    qint64 result = -1;

    auto posX = pos.x() + horizontalScrollBar()->value();
    auto posY = pos.y() - 3;
    const int rowStridePx = _pxCharHeight + kHexRowExtraGapPx;

    const int hexStridePx = 3 * _pxCharWidth + kHexColumnExtraGapPx;
    const int hexAreaWidthPx = (_bytesPerLine > 0) ? ((_bytesPerLine - 1) * hexStridePx + 2 * _pxCharWidth) : 0;

    if ((posX >= _pxPosHexX) && (posX < (_pxPosHexX + hexAreaWidthPx)))
    {
        _editAreaIsAscii = false;
        const int relX = posX - _pxPosHexX;
        const int byteIndex = relX / hexStridePx;

        if (byteIndex < 0 || byteIndex >= _bytesPerLine)
            return -1;

        const int inByteX = relX - byteIndex * hexStridePx;
        const int nibble = (inByteX >= _pxCharWidth) ? 1 : 0;
        int x = byteIndex * 2 + nibble;
        int y = (posY / rowStridePx) * 2 * _bytesPerLine;

        result = _bPosFirst * 2 + x + y;
    }
    else if (_asciiArea && (posX >= _pxPosAsciiX) && (posX < (_pxPosAsciiX + kAsciiAreaLeftPaddingPx + static_cast<int>(_asciiAreaMaxWidth))))
    {
        _editAreaIsAscii = true;
        ensureTableDisplayCache();
        ensureEncodingDisplayCache();

        const auto slotGapPx = [this](int baseWidth) {
            return (baseWidth > _pxCharWidth) ? kAsciiColumnGapWidePx : kAsciiColumnGapSinglePx;
        };

        const int row = posY / rowStridePx;
        if (row < 0)
            return -1;

        const int xPx = qMax(0, posX - (_pxPosAsciiX + kAsciiAreaLeftPaddingPx));
        auto y = row * 2 * _bytesPerLine;
        const qint64 rowStart = static_cast<qint64>(row) * _bytesPerLine;
        const qint64 rowEnd = qMin(rowStart + _bytesPerLine, static_cast<qint64>(_dataShown.size()));

        if (rowStart >= rowEnd)
            return -1;

        // Walk slots left-to-right accumulating pixel widths until we hit the click X.
        // Multi-byte TBL/encoding entries have zero-width continuation bytes; clicks snap to the lead byte.
        int accumulated = 0;
        int byteCol = 0; // lead byte of the entry being hit

        if (_tb && !_tbDisplayChars.isEmpty())
        {
            const QFontMetrics fm(font());
            for (int col = 0; col < static_cast<int>(rowEnd - rowStart); ++col)
            {
                const int idx = (int)rowStart + col;
                if (idx < 0 || idx >= _tbDisplayChars.size()) break;
                if (_tbDisplayChars[idx].isNull()) continue;
                const int baseW = qMax(_pxCharWidth, fm.horizontalAdvance(_tbDisplayChars[idx]));
                const int slotW = baseW + slotGapPx(baseW);
                byteCol = col;
                if (xPx < accumulated + slotW)
                    break;
                accumulated += slotW;
            }
        }
        else if (!_encodingChars.isEmpty())
        {
            // Encoding mode: continuation bytes have zero width
            const QFontMetrics fm(font());
            for (int col = 0; col < static_cast<int>(rowEnd - rowStart); ++col)
            {
                const int idx = (int)rowStart + col;
                if (idx < 0 || idx >= _encodingChars.size()) break;
                if (_encodingChars[idx].isNull()) continue; // continuation: zero width
                const int baseW = qMax(_pxCharWidth, fm.horizontalAdvance(_encodingChars[idx]));
                const int slotW = baseW + slotGapPx(baseW);
                byteCol = col;
                if (xPx < accumulated + slotW)
                    break;
                accumulated += slotW;
            }
        }
        else
        {
            byteCol = static_cast<int>(rowEnd - rowStart) - 1; // default: last byte
            for (int col = 0; col < static_cast<int>(rowEnd - rowStart); ++col)
            {
                const uint8_t bv = static_cast<uint8_t>(_dataShown.at(rowStart + col));
                const int baseW = (_tb && !_tbSymbolWidthPxCache.isEmpty()) ? _tbSymbolWidthPxCache[bv] : _pxCharWidth;
                const int slotW = baseW + slotGapPx(baseW);
                if (xPx < accumulated + slotW)
                {
                    byteCol = col;
                    break;
                }
                accumulated += slotW;
            }
        }

        result = _bPosFirst * 2 + byteCol * 2 + y;
    }

    return result;
}

qint64 QHexEdit::cursorPosition()
{
    return _cursorPosition;
}

void QHexEdit::setData(const QByteArray &ba)
{
    _data = ba;
    _bData.setData(_data);
    setData(_bData);
}

QByteArray QHexEdit::data()
{
    return _chunks->data(0, -1);
}

QByteArray QHexEdit::getRawSelection()
{
    return _chunks->data(getSelectionBegin(), getSelectionEnd() - getSelectionBegin());
}

Datas QHexEdit::getValue(qint64 offset)
{
    Datas value;

    value.leDword = qFromLittleEndian<quint32_le>(_chunks->data(offset, 4));

    return value;
}

qint64 QHexEdit::getCurrentOffset()
{
    return _bPosCurrent;
}

qint64 QHexEdit::pointerStartAt(qint64 bytePos, int /*pointerSize*/)
{
    if (bytePos < 0)
        return -1;

    // Search back up to the maximum possible pointer size (4 bytes)
    const qint64 startMin = qMax(static_cast<qint64>(0), bytePos - 4 + 1);

    for (qint64 candidate = bytePos; candidate >= startMin; --candidate)
    {
        if (_pointers.isPointer(candidate))
        {
            const int storedSize = _pointers.getPointerSize(candidate);
            if (bytePos < candidate + storedSize)
                return candidate;
        }
    }

    return -1;
}

qint64 QHexEdit::pointerTargetAt(qint64 bytePos, int pointerSize)
{
    const qint64 ptrStart = pointerStartAt(bytePos, pointerSize);
    return (ptrStart >= 0) ? _pointers.getOffset(ptrStart) : -1;
}

void QHexEdit::setHighlighting(bool highlighting)
{
    _highlighting = highlighting;
    viewport()->update();
}

bool QHexEdit::highlighting()
{
    return _highlighting;
}

void QHexEdit::setHighlightingColor(const QColor &color)
{
    _brushHighlighted = QBrush(color);
    _penHighlighted = QPen(viewport()->palette().color(QPalette::WindowText));
    viewport()->update();
}

QColor QHexEdit::highlightingColor()
{
    return _brushHighlighted.color();
}

void QHexEdit::setShowPointers(bool show)
{
    _showPointers = show;
    viewport()->update();
}

bool QHexEdit::showPointers()
{
    return _showPointers;
}

void QHexEdit::setPointersColor(const QColor &color)
{
    _brushPointers = QBrush(color);
    _penPointers = QPen(_pointerFontColor);
    if (_scrollMapPtr)
        _scrollMapPtr->setColor(color);
    viewport()->update();
}

QColor QHexEdit::pointersColor()
{
    return _brushPointers.color();
}

void QHexEdit::setPointedColor(const QColor &color)
{
    _brushPointed = QBrush(color);
    _penPointed = QPen(_pointedFontColor);
    if (_scrollMapTarget)
        _scrollMapTarget->setColor(color);
    viewport()->update();
}

QColor QHexEdit::pointedColor()
{
    return _brushPointed.color();
}

void QHexEdit::setPointedFontColor(const QColor &color)
{
    _pointedFontColor = color;
    _penPointed = QPen(_pointedFontColor);
    viewport()->update();
}

QColor QHexEdit::pointedFontColor()
{
    return _pointedFontColor;
}

void QHexEdit::setPointerFontColor(const QColor &color)
{
    _pointerFontColor = color;
    _penPointers = QPen(_pointerFontColor);
    viewport()->update();
}

QColor QHexEdit::pointerFontColor()
{
    return _pointerFontColor;
}

void QHexEdit::setPointerFrameColor(const QColor &color)
{
    _pointerFrameColor = color;
    viewport()->update();
}

QColor QHexEdit::pointerFrameColor()
{
    return _pointerFrameColor;
}

void QHexEdit::setPointerFrameBackgroundColor(const QColor &color)
{
    _pointerFrameBackgroundColor = color;
    viewport()->update();
}

QColor QHexEdit::pointerFrameBackgroundColor()
{
    return _pointerFrameBackgroundColor;
}

void QHexEdit::setMultibyteFrameColor(const QColor &color)
{
    _multibyteFrameColor = color;
    viewport()->update();
}

QColor QHexEdit::multibyteFrameColor()
{
    return _multibyteFrameColor;
}

bool QHexEdit::showMultibyteFrame() const
{
    return _showMultibyteFrame;
}

void QHexEdit::setShowMultibyteFrame(bool show)
{
    _showMultibyteFrame = show;
    viewport()->update();
}

void QHexEdit::setOverwriteMode(bool overwriteMode)
{
    _overwriteMode = overwriteMode;
    emit overwriteModeChanged(overwriteMode);
}

bool QHexEdit::overwriteMode()
{
    return _overwriteMode;
}

void QHexEdit::setSelectionColor(const QColor &color)
{
    _brushSelection = QBrush(color);
    _penSelection = QPen(Qt::white);
    viewport()->update();
}

QColor QHexEdit::selectionColor()
{
    return _brushSelection.color();
}

bool QHexEdit::showHexGrid()
{
    return _showHexGrid;
}

void QHexEdit::setShowHexGrid(bool mode)
{
    _showHexGrid = mode;
    viewport()->update();
}

QColor QHexEdit::hexAreaBackgroundColor()
{
    return _hexAreaBackgroundColor;
}

void QHexEdit::setHexAreaBackgroundColor(const QColor &color)
{
    _hexAreaBackgroundColor = color;
    viewport()->update();
}

QColor QHexEdit::hexAreaGridColor()
{
    return _hexAreaGridColor;
}

void QHexEdit::setHexAreaGridColor(const QColor &color)
{
    _hexAreaGridColor = color;
    viewport()->update();
}

bool QHexEdit::isReadOnly()
{
    return _readOnly;
}

void QHexEdit::setReadOnly(bool readOnly)
{
    _readOnly = readOnly;
}

void QHexEdit::setHexCaps(const bool isCaps)
{
    if (_hexCaps != isCaps)
    {
        _hexCaps = isCaps;
        viewport()->update();
    }
}

bool QHexEdit::hexCaps()
{
    return _hexCaps;
}

void QHexEdit::setDynamicBytesPerLine(const bool isDynamic)
{
    _dynamicBytesPerLine = isDynamic;
    resizeEvent(NULL);
}

bool QHexEdit::dynamicBytesPerLine()
{
    return _dynamicBytesPerLine;
}

// ********************************************************************** Access to data of qhexedit
bool QHexEdit::setData(QIODevice &iODevice)
{
    bool ok = _chunks->setIODevice(iODevice);

    init();

    dataChangedPrivate();

    return ok;
}

QByteArray QHexEdit::dataAt(qint64 pos, qint64 count)
{
    return _chunks->data(pos, count);
}

bool QHexEdit::write(QIODevice &iODevice, qint64 pos, qint64 count)
{
    return _chunks->write(iODevice, pos, count);
}

// ********************************************************************** Char handling
void QHexEdit::insert(qint64 index, char ch)
{
    _undoStack->insert(index, ch);
    refresh();
}

void QHexEdit::remove(qint64 index, qint64 len)
{
    _undoStack->removeAt(index, len);
    refresh();
}

void QHexEdit::replace(qint64 index, char ch)
{
    _undoStack->overwrite(index, ch);
    refresh();
}

// ********************************************************************** ByteArray handling
void QHexEdit::insert(qint64 pos, const QByteArray &ba)
{
    _undoStack->insert(pos, ba);
    refresh();
}

void QHexEdit::replace(qint64 pos, qint64 len, const QByteArray &ba)
{
    _undoStack->overwrite(pos, len, ba);
    refresh();
}

// ********************************************************************** Utility functions
void QHexEdit::ensureVisible()
{
    if (_cursorPosition < (_bPosFirst * 2))
        verticalScrollBar()->setValue((int)(_cursorPosition / 2 / _bytesPerLine));

    if (_cursorPosition > ((_bPosFirst + (_rowsShown - 1) * _bytesPerLine) * 2))
        verticalScrollBar()->setValue((int)(_cursorPosition / 2 / _bytesPerLine) - _rowsShown + 1);

    if (_pxCursorX < horizontalScrollBar()->value())
        horizontalScrollBar()->setValue(_pxCursorX);

    if ((_pxCursorX + _pxCharWidth) > (horizontalScrollBar()->value() + viewport()->width()))
        horizontalScrollBar()->setValue(_pxCursorX + _pxCharWidth - viewport()->width());

    viewport()->update();
}

qint64 QHexEdit::indexOf(const QByteArray &ba, qint64 from)
{
    qint64 pos = _chunks->indexOf(ba, from);

    if (pos > -1)
    {
        qint64 curPos = pos * 2;

        setCursorPosition(curPos + ba.length() * 2);
        resetSelection(curPos);
        setSelection(curPos + ba.length() * 2);
        ensureVisible();
    }

    return pos;
}

qint64 QHexEdit::relativeSearch(const QByteArray &ba, qint64 from)
{
    auto buf = data().constData();
    uint8_t coin;
    QByteArray relNeedle;
    auto searchLen = ba.size();

    relNeedle.append('\0');

    for (uint8_t j = 1; j < searchLen; j++)
    {
        relNeedle.append(ba[0] - ba[j]);
    }

    auto maxOffset = data().size() - searchLen;

    for (qint64 i = from; i < maxOffset; i++)
    {
        coin = 1;

        for (uint8_t j = 1; j < searchLen; j++)
        {
            if ((buf[i] - buf[i + j]) != relNeedle[j])
            {
                break;
            }

            coin++;
        }

        if (coin == searchLen)
        {
            qint64 curPos = i * 2;

            setCursorPosition(curPos + searchLen * 2);
            resetSelection(curPos);
            setSelection(curPos + searchLen * 2);
            ensureVisible();

            /*if (!_tb)
                _tb = new TranslationTable();

            _tb->generateTable(QString::fromLatin1(_chunks->data(i, searchLen), searchLen), ba);*/

            return i;
        }
    }

    return -1;
}

void QHexEdit::jumpTo(qint64 offset, bool relative)
{
    auto newPos = qBound(0LL, (relative ? (_cursorPosition / 2) + offset : offset), static_cast<qint64>(data().size()));

    setCursorPosition(newPos * 2);
    resetSelection(_cursorPosition);
    ensureVisible();
}

bool QHexEdit::isModified()
{
    return _modified;
}

bool QHexEdit::canUndo()
{
    return _undoStack->canUndo();
}

bool QHexEdit::canRedo()
{
    return _undoStack->canRedo();
}

qint64 QHexEdit::lastIndexOf(const QByteArray &ba, qint64 from)
{
    qint64 pos = _chunks->lastIndexOf(ba, from);

    if (pos > -1)
    {
        qint64 curPos = pos * 2;
        setCursorPosition(curPos - 1);
        resetSelection(curPos);
        setSelection(curPos + ba.length() * 2);
        ensureVisible();
    }

    return pos;
}

void QHexEdit::redo()
{
    _undoStack->redo();
    setCursorPosition(_chunks->pos() * (_editAreaIsAscii ? 1 : 2));
    refresh();
}

QString QHexEdit::selectionToReadableString()
{
    QByteArray ba = _chunks->data(getSelectionBegin(), getSelectionEnd() - getSelectionBegin());
    return toReadable(ba);
}

QString QHexEdit::selectedData()
{
    QByteArray ba = _chunks->data(getSelectionBegin(), getSelectionEnd() - getSelectionBegin()).toHex();
    return ba;
}

void QHexEdit::setFont(const QFont &font)
{
    QFont theFont(font);
    theFont.setStyleHint(QFont::Monospace);
    QWidget::setFont(theFont);
    QFontMetrics metrics = fontMetrics();
    _pxCharWidth = metrics.horizontalAdvance(QLatin1Char('2'));
    _pxCharHeight = metrics.height();
    _pxGapAdr = _pxCharWidth / 2;
    _pxGapAdrHex = _pxCharWidth * 2;
    _pxGapHexAscii = 2 * _pxCharWidth;
    _pxCursorWidth = _pxCharHeight / 7;
    _pxSelectionSub = _pxCharHeight / 5;
    invalidateAsciiAreaWidthCache();
    updateAsciiAreaMaxWidth();
    viewport()->update();
}

QString QHexEdit::toReadableString()
{
    QByteArray ba = _chunks->data();
    return toReadable(ba);
}

void QHexEdit::undo()
{
    _undoStack->undo();
    setCursorPosition(_chunks->pos() * (_editAreaIsAscii ? 1 : 2));
    refresh();
}

TranslationTable *QHexEdit::getTranslationTable()
{
    return _tb;
}

void QHexEdit::setTranslationTable(TranslationTable *tb)
{
    // Save viewport position so toggling the table/autosize doesn't jump vertically
    const qint64 savedTopByte = static_cast<qint64>(verticalScrollBar()->value()) * qMax(1, _bytesPerLine);
    const qint64 savedCursorPos = _cursorPosition;
    const int savedHorizontal = horizontalScrollBar()->value();

    _tb = tb;
    _tbDisplayCacheValid = false;
    invalidateAsciiAreaWidthCache();
    ensureAsciiAreaWidthCache();
    updateAsciiAreaMaxWidth();

    if (_dynamicBytesPerLine)
        resizeEvent(nullptr);
    else
        adjust();

    restoreTopVisibleByte(savedTopByte);
    horizontalScrollBar()->setValue(savedHorizontal);
    setCursorPosition(savedCursorPos);
    viewport()->update();
}

void QHexEdit::removeTranslationTable()
{
    setTranslationTable(); // with no parameters removes translation table
}

QString QHexEdit::currentEncoding() const
{
    return _currentEncoding;
}

void QHexEdit::setCurrentEncoding(const QString &encoding)
{
    if (_currentEncoding == encoding) return;
    _currentEncoding = encoding;
    _encodingCacheValid = false;
    invalidateAsciiAreaWidthCache();
    viewport()->update();
}

QString QHexEdit::decodeTextForCurrentEncoding(const QByteArray &bytes) const
{
    if (_tb)
        return _tb->encode(bytes, true);
    return decodeTextWithEncoding(bytes, _currentEncoding);
}

QByteArray QHexEdit::encodeTextForCurrentEncoding(const QString &text) const
{
    if (_tb)
        return _tb->decode(text.toUtf8());
    return encodeTextWithEncoding(text, _currentEncoding);
}

QVector<QString> QHexEdit::decodeBufferForCurrentEncoding(const QByteArray &data) const
{
    if (_tb) {
        QVector<QString> chars;
        QVector<int> span;
        decodeBufferWithTable(data, _tb, chars, span);
        return chars;
    }
    return decodeBufferWithEncoding(data, _currentEncoding);
}

// ********************************************************************** Handle events
void QHexEdit::keyPressEvent(QKeyEvent *event)
{
    // Pure modifier keys must not trigger ensureVisible()/refresh() because that
    // can unexpectedly jump vertical scroll position.
    if (event->key() == Qt::Key_Shift
        || event->key() == Qt::Key_Control
        || event->key() == Qt::Key_Alt
        || event->key() == Qt::Key_Meta)
    {
        event->accept();
        return;
    }

    // Cursor movements
    if (event->matches(QKeySequence::MoveToNextChar))
    {
        qint64 pos = _cursorPosition + 1;

        if (_editAreaIsAscii)
            pos += 1;

        setCursorPosition(pos);
        resetSelection(pos);
    }

    if (event->matches(QKeySequence::MoveToPreviousChar))
    {
        qint64 pos = _cursorPosition - 1;

        if (_editAreaIsAscii)
            pos -= 1;

        setCursorPosition(pos);
        resetSelection(pos);
    }

    if (event->matches(QKeySequence::MoveToEndOfLine))
    {
        qint64 pos = _cursorPosition - (_cursorPosition % (2 * _bytesPerLine)) + (2 * _bytesPerLine) - 1;
        setCursorPosition(pos);
        resetSelection(_cursorPosition);
    }

    if (event->matches(QKeySequence::MoveToStartOfLine))
    {
        qint64 pos = _cursorPosition - (_cursorPosition % (2 * _bytesPerLine));
        setCursorPosition(pos);
        resetSelection(_cursorPosition);
    }

    if (event->matches(QKeySequence::MoveToPreviousLine))
    {
        setCursorPosition(_cursorPosition - (2 * _bytesPerLine));
        resetSelection(_cursorPosition);
    }

    if (event->matches(QKeySequence::MoveToNextLine))
    {
        setCursorPosition(_cursorPosition + (2 * _bytesPerLine));
        resetSelection(_cursorPosition);
    }

    if (event->matches(QKeySequence::MoveToNextPage))
    {
        setCursorPosition(_cursorPosition + (((_rowsShown - 1) * 2 * _bytesPerLine)));
        resetSelection(_cursorPosition);
    }

    if (event->matches(QKeySequence::MoveToPreviousPage))
    {
        setCursorPosition(_cursorPosition - (((_rowsShown - 1) * 2 * _bytesPerLine)));
        resetSelection(_cursorPosition);
    }

    if (event->matches(QKeySequence::MoveToEndOfDocument))
    {
        setCursorPosition(_chunks->size() * 2);
        resetSelection(_cursorPosition);
    }

    if (event->matches(QKeySequence::MoveToStartOfDocument))
    {
        setCursorPosition(0);
        resetSelection(_cursorPosition);
    }

    // Select commands
    if (event->matches(QKeySequence::SelectAll))
    {
        resetSelection(0);
        setSelection(2 * _chunks->size() + 1);
    }

    if (event->matches(QKeySequence::SelectNextChar))
    {
        qint64 pos = _cursorPosition + 1;
        if (_editAreaIsAscii)
            pos += 1;
        else
            pos = (_cursorPosition | 1) + 1; // snap to next byte boundary
        setCursorPosition(pos);
        setSelection(pos);
    }

    if (event->matches(QKeySequence::SelectPreviousChar))
    {
        qint64 pos = _cursorPosition - 1;
        if (_editAreaIsAscii)
            pos -= 1;
        else
            pos = (_cursorPosition & ~1) - 2; // snap to previous byte boundary
        setSelection(pos);
        setCursorPosition(pos);
    }

    if (event->matches(QKeySequence::SelectEndOfLine))
    {
        qint64 pos = _cursorPosition - (_cursorPosition % (2 * _bytesPerLine)) + (2 * _bytesPerLine) - 1;
        setCursorPosition(pos);
        setSelection(pos);
    }

    if (event->matches(QKeySequence::SelectStartOfLine))
    {
        qint64 pos = _cursorPosition - (_cursorPosition % (2 * _bytesPerLine));
        setCursorPosition(pos);
        setSelection(pos);
    }

    if (event->matches(QKeySequence::SelectPreviousLine))
    {
        qint64 pos = _cursorPosition - (2 * _bytesPerLine);
        setCursorPosition(pos);
        setSelection(pos);
    }

    if (event->matches(QKeySequence::SelectNextLine))
    {
        qint64 pos = _cursorPosition + (2 * _bytesPerLine);
        setCursorPosition(pos);
        setSelection(pos);
    }

    if (event->matches(QKeySequence::SelectNextPage))
    {
        qint64 pos = _cursorPosition + ((_rowsShown - 1) * 2 * _bytesPerLine);
        setCursorPosition(pos);
        setSelection(pos);
    }

    if (event->matches(QKeySequence::SelectPreviousPage))
    {
        qint64 pos = _cursorPosition - ((_rowsShown - 1) * 2 * _bytesPerLine);
        setCursorPosition(pos);
        setSelection(pos);
    }

    if (event->matches(QKeySequence::SelectEndOfDocument))
    {
        qint64 pos = _chunks->size() * 2;
        setCursorPosition(pos);
        setSelection(pos);
    }

    if (event->matches(QKeySequence::SelectStartOfDocument))
    {
        qint64 pos = 0;
        setCursorPosition(pos);
        setSelection(pos);
    }

    // Edit Commands
    if (!_readOnly)
    {
        /* Cut */
        if (event->matches(QKeySequence::Cut))
        {
            const qint64 selBegin = getSelectionBegin();
            const qint64 selEnd = getSelectionEnd();
            const QByteArray raw = _chunks->data(selBegin, selEnd - selBegin);

            if (_editAreaIsAscii)
            {
                QApplication::clipboard()->setText(decodeTextForCurrentEncoding(raw));
            }
            else
            {
                QApplication::clipboard()->setText(QString::fromLatin1(raw.toHex(' ')).toUpper());
            }

            // In REPLACE mode, cut only copies without deleting
            if (!_overwriteMode)
            {
                remove(selBegin, selEnd - selBegin);
                setCursorPosition(2 * selBegin);
                resetSelection(2 * selBegin);
            }
        }
        else

            /* Paste */
            if (event->matches(QKeySequence::Paste))
            {
                QClipboard *clipboard = QApplication::clipboard();
                QByteArray ba;
                if (_editAreaIsAscii)
                {
                    ba = encodeTextForCurrentEncoding(clipboard->text());
                }
                else
                {
                    const QString stripped = clipboard->text()
                                                .remove(' ').remove('\t').remove('\n').remove('\r');
                    ba = QByteArray::fromHex(stripped.toLatin1());
                }

                const qint64 selBegin = getSelectionBegin();
                const qint64 selEnd = getSelectionEnd();
                const bool hasSelection = (selBegin != selEnd);

                if (_overwriteMode)
                {
                    if (hasSelection)
                    {
                        // REPLACE mode with selection: truncate paste to selection size, paste at selection beginning
                        const qint64 selLen = selEnd - selBegin;
                        ba = ba.left(static_cast<int>(selLen));
                        replace(selBegin, ba.size(), ba);
                        setCursorPosition(2 * (selBegin + ba.size()));
                    }
                    else
                    {
                        // REPLACE mode without selection: paste at cursor position
                        ba = ba.left(static_cast<int>(std::min<qint64>(ba.size(), (_chunks->size() - _bPosCurrent))));
                        replace(_bPosCurrent, ba.size(), ba);
                        setCursorPosition(_cursorPosition + 2 * ba.size());
                    }
                }
                else
                {
                    // INSERT mode
                    if (hasSelection)
                    {
                        // INSERT mode with selection: delete entire selection, then insert paste at selection beginning
                        const qint64 selLen = selEnd - selBegin;
                        remove(selBegin, static_cast<int>(selLen));
                        insert(selBegin, ba);
                        setCursorPosition(2 * (selBegin + ba.size()));
                    }
                    else
                    {
                        // INSERT mode without selection: insert at cursor position
                        insert(_bPosCurrent, ba);
                        setCursorPosition(_cursorPosition + 2 * ba.size());
                    }
                }
                resetSelection(getSelectionBegin());
            }
            else

                /* Delete char */
                if (event->matches(QKeySequence::Delete))
                {
                    if (getSelectionEnd() - getSelectionBegin() > 1)
                    {
                        _bPosCurrent = getSelectionBegin();
                        if (_overwriteMode)
                        {
                            QByteArray ba = QByteArray(getSelectionEnd() - getSelectionBegin(), char(0));
                            replace(_bPosCurrent, ba.size(), ba);
                        }
                        else
                        {
                            remove(_bPosCurrent, getSelectionEnd() - getSelectionBegin());
                        }
                    }
                    else
                    {
                        if (_overwriteMode)
                            replace(_bPosCurrent, char(0));
                        else
                            remove(_bPosCurrent, 1);
                    }
                    setCursorPosition(2 * _bPosCurrent);
                    resetSelection(2 * _bPosCurrent);
                }
                else

                    /* Backspace */
                    if ((event->key() == Qt::Key_Backspace) && (event->modifiers() == Qt::NoModifier))
                    {
                        if (getSelectionEnd() - getSelectionBegin() > 1)
                        {
                            _bPosCurrent = getSelectionBegin();
                            setCursorPosition(2 * _bPosCurrent);

                            if (_overwriteMode)
                            {
                                QByteArray ba = QByteArray(getSelectionEnd() - getSelectionBegin(), char(0));
                                replace(_bPosCurrent, ba.size(), ba);
                            }
                            else
                            {
                                remove(_bPosCurrent, getSelectionEnd() - getSelectionBegin());
                            }
                            resetSelection(2 * _bPosCurrent);
                        }
                        else
                        {
                            bool behindLastByte = false;
                            if ((_cursorPosition / 2) == _chunks->size())
                                behindLastByte = true;

                            _bPosCurrent -= 1;
                            if (_overwriteMode)
                                replace(_bPosCurrent, char(0));
                            else
                                remove(_bPosCurrent, 1);

                            if (!behindLastByte)
                                _bPosCurrent -= 1;

                            setCursorPosition(2 * _bPosCurrent);
                            resetSelection(2 * _bPosCurrent);
                        }
                    }
                    else

                        if (event->matches(QKeySequence::Undo)) // UNDO
                    {
                        undo();
                    }
                    else if (event->matches(QKeySequence::Redo)) // REDO
                    {
                        redo();
                    }
                    else if (event->text() != "")
                    {
                        /* Hex and ascii input */
                        auto key = _editAreaIsAscii ? event->text().at(0) : event->text().at(0).toLower().toLatin1();

                        // Filter hex input
                        if ((((key >= '0' && key <= '9') || (key >= 'a' && key <= 'f')) && _editAreaIsAscii == false) || (key >= ' ' && _editAreaIsAscii))
                        {
                            if (hasSelection())
                            {
                                if (_overwriteMode)
                                {
                                    qint64 len = getSelectionEnd() - getSelectionBegin();
                                    replace(getSelectionBegin(), (int)len, QByteArray((int)len, char(0)));
                                }
                                else
                                {
                                    remove(getSelectionBegin(), getSelectionEnd() - getSelectionBegin());
                                    _bPosCurrent = getSelectionBegin();
                                }

                                setCursorPosition(2 * _bPosCurrent);
                                resetSelection(2 * _bPosCurrent);
                            }

                            // If insert mode, then insert a byte
                            if (!_overwriteMode && !(_cursorPosition % 2))
                                insert(_bPosCurrent, char(0));

                            // Change content
                            if (_chunks->size() > 0)
                            {
                                if (!_editAreaIsAscii)
                                {
                                    QByteArray hexValue = _chunks->data(_bPosCurrent, 1).toHex();

                                    if ((_cursorPosition % 2) == 0)
                                        hexValue[0] = key.toLatin1();
                                    else
                                        hexValue[1] = key.toLatin1();

                                    char ch = QByteArray().fromHex(hexValue)[0];
                                    replace(_bPosCurrent, ch);
                                    setCursorPosition(_cursorPosition + 1);
                                }
                                else
                                {
                                    // ASCII edit mode: try multi-byte TBL lookup first
                                    QByteArray bytesToWrite;
                                    if (_tb)
                                    {
                                        bytesToWrite = _tb->decodeToBytes(QString(key));
                                        if (bytesToWrite.isEmpty())
                                        {
                                            // Not in any table entry — keep as raw byte
                                            bytesToWrite = QByteArray(1, key.toLatin1());
                                        }
                                    }
                                    else
                                    {
                                        // Use encoding-aware conversion so non-ASCII characters
                                        // (e.g. Cyrillic typed via keyboard IME) are stored correctly.
                                        bytesToWrite = encodeTextForCurrentEncoding(QString(key));
                                        if (bytesToWrite.isEmpty())
                                            bytesToWrite = QByteArray(1, key.toLatin1());
                                    }

                                    const int bytesLen = bytesToWrite.size();

                                    // In insert mode, the outer block already inserted 1 null byte.
                                    // For multi-byte entries we need bytesLen total, so insert bytesLen-1 more.
                                    if (!_overwriteMode && !(_cursorPosition % 2) && bytesLen > 1)
                                        insert(_bPosCurrent + 1, QByteArray(bytesLen - 1, char(0)));

                                    if (bytesLen == 1)
                                        replace(_bPosCurrent, bytesToWrite[0]);
                                    else
                                        replace(_bPosCurrent, bytesLen, bytesToWrite);

                                    setCursorPosition(_cursorPosition + 2 * bytesLen);
                                }

                                resetSelection(_cursorPosition);
                            }
                        }
                    }
    }

    /* Copy */
    if (event->matches(QKeySequence::Copy))
    {
        const qint64 selBegin = getSelectionBegin();
        const qint64 selEnd = getSelectionEnd();
        const QByteArray raw = _chunks->data(selBegin, selEnd - selBegin);

        if (_editAreaIsAscii)
            QApplication::clipboard()->setText(decodeTextForCurrentEncoding(raw));
        else
            QApplication::clipboard()->setText(QString::fromLatin1(raw.toHex(' ')).toUpper());
    }

    // Switch between insert/overwrite mode
    if ((event->key() == Qt::Key_Insert) && (event->modifiers() == Qt::NoModifier))
    {
        setOverwriteMode(!overwriteMode());
        setCursorPosition(_cursorPosition);
    }

    // switch from hex to ascii edit
    if (event->key() == Qt::Key_Tab && !_editAreaIsAscii)
    {
        _editAreaIsAscii = true;
        setCursorPosition(_cursorPosition);
    }

    // switch from ascii to hex edit
    if (event->key() == Qt::Key_Backtab && _editAreaIsAscii)
    {
        _editAreaIsAscii = false;
        setCursorPosition(_cursorPosition);
    }

    refresh();
    QAbstractScrollArea::keyPressEvent(event);
}

void QHexEdit::mouseMoveEvent(QMouseEvent *event)
{
    _blink = false;
    viewport()->update();

    // Handle separator dragging (only when autosize is OFF and ASCII area visible)
    if (_separatorDragging && !_dynamicBytesPerLine && _asciiArea)
    {
        int pixelDelta = event->x() - _separatorDragStartX;
        
        // Convert pixel delta to bytes per line delta.
        // Each byte takes 3 characters in hex area (2 hex digits + 1 space),
        // plus extra gaps between columns.
        int pixelsPerByte = 3 * _pxCharWidth + kHexColumnExtraGapPx;
        
        // Subtract the gap for the last column to get realistic byte changes
        pixelsPerByte = 3 * _pxCharWidth + kHexColumnExtraGapPx - kHexColumnExtraGapPx / _bytesPerLine;
        
        // Use 3 chars width + gap as the primary measure
        int byteDelta = pixelDelta / (3 * _pxCharWidth);
        
        // Calculate new bytes per line
        int newBytesPerLine = _bytesPerLine + byteDelta;
        
        // Clamp to reasonable range: 4-64 bytes per line
        if (newBytesPerLine < 4)
            newBytesPerLine = 4;
        else if (newBytesPerLine > 64)
            newBytesPerLine = 64;
        
        if (newBytesPerLine != _bytesPerLine)
        {
            _separatorDragStartX = event->x();
            setBytesPerLine(newBytesPerLine);
        }
        
        viewport()->setCursor(Qt::SizeHorCursor);
        return;
    }

    // Determine cursor based on autosize mode and hover position
    if (_dynamicBytesPerLine)
    {
        // Autosize is ON: always use arrow cursor
        viewport()->setCursor(Qt::ArrowCursor);
    }
    else if (_asciiArea)
    {
        // Autosize is OFF and ASCII area visible: check if hovering over separator
        int pxOfsX = horizontalScrollBar()->value();
        int separatorScreenX = _pxPosAsciiX - (_pxGapHexAscii / 2) - pxOfsX;
        
        if (abs(event->x() - separatorScreenX) < 8)
        {
            viewport()->setCursor(Qt::SizeHorCursor);
        }
        else
        {
            viewport()->setCursor(Qt::ArrowCursor);
        }
    }
    else
    {
        // ASCII area hidden: always use arrow cursor
        viewport()->setCursor(Qt::ArrowCursor);
    }

    // Only update selection if mouse button is pressed (not on pure hover)
    if (event->buttons() != Qt::NoButton)
    {
        qint64 actPos = cursorPosition(event->pos());

        if (actPos >= 0)
        {
            setCursorPosition(actPos);
            setSelection(actPos);
        }
    }
}

bool QHexEdit::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::ToolTip && _showPointers)
    {
        QHelpEvent *helpEvent = static_cast<QHelpEvent *>(event);
        const qint64 nibblePos = cursorPosition(helpEvent->pos());
        const qint64 bytePos = nibblePos / 2;

        if (bytePos >= 0)
        {
            const qint64 ptrStart = pointerStartAt(bytePos, kPointerByteSize);
            if (ptrStart >= 0)
            {
                QToolTip::showText(helpEvent->globalPos(), _pointers.getOffsetText(ptrStart), viewport());
                return true;
            }
            else if (_pointers.hasOffset(bytePos))
            {
                const auto ptrs = _pointers.getPointers(bytePos);
                const QString tip = (ptrs.size() == 1)
                    ? QStringLiteral("0x%1").arg(ptrs[0], 8, 16, QChar('0')).toUpper()
                    : tr("%1 pointers").arg(ptrs.size());
                QToolTip::showText(helpEvent->globalPos(), tip, viewport());
                return true;
            }
        }
        QToolTip::hideText();
        event->ignore();
        return true;
    }
    return QAbstractScrollArea::viewportEvent(event);
}

void QHexEdit::mousePressEvent(QMouseEvent *event)
{
    _blink = false;
    viewport()->update();

    // Check if separator drag is starting (only if not in dynamic/autosize mode)
    if (!_dynamicBytesPerLine && _asciiArea)
    {
        int pxOfsX = horizontalScrollBar()->value();
        int separatorScreenX = _pxPosAsciiX - (_pxGapHexAscii / 2) - pxOfsX;
        
        // If click is within 8 pixels of separator, start drag
        if (abs(event->x() - separatorScreenX) < 8)
        {
            _separatorDragging = true;
            _separatorDragStartX = event->x();
            viewport()->setCursor(Qt::SizeHorCursor);
            return;
        }
    }

    qint64 cPos = cursorPosition(event->pos());

    if (cPos >= 0)
    {
        if (event->button() == Qt::RightButton)
        {
            // On right-click: only update cursor position, do NOT reset or
            // change the selection so it is preserved for the context menu.
            setCursorPosition(cPos);
        }
        else
        {
            if (event->modifiers() != Qt::ShiftModifier)
                resetSelection(cPos);

            setCursorPosition(cPos);
            setSelection(cPos);

            if (_showPointers)
            {
                const qint64 ptrStart = pointerStartAt(_bPosCurrent, kPointerByteSize);

                if (ptrStart >= 0)
                {
                    QToolTip::showText(mapToGlobal(event->pos()), _pointers.getOffsetText(ptrStart));
                }
                else if (_pointers.hasOffset(_bPosCurrent))
                {
                    auto ptrs = _pointers.getPointers(_bPosCurrent);

                    if (ptrs.size() == 1)
                    {
                        QToolTip::showText(mapToGlobal(event->pos()), QString("0x%1").arg(ptrs[0], 8, 16, QChar('0')));
                    }
                    else
                    {
                        QToolTip::showText(mapToGlobal(event->pos()), tr("%1 pointers").arg(ptrs.size()));
                    }
                }
            }
        }
    }
}

void QHexEdit::mouseReleaseEvent(QMouseEvent *event)
{
    // End separator dragging
    if (_separatorDragging)
    {
        _separatorDragging = false;
        viewport()->setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }
    
    QAbstractScrollArea::mouseReleaseEvent(event);
}

void QHexEdit::mouseDoubleClickEvent(QMouseEvent *event)
{
    Q_UNUSED(event)

    if (_showPointers)
    {
        const qint64 ptrStart = pointerStartAt(_bPosCurrent, kPointerByteSize);

        // jump to pointed offset
        if (ptrStart >= 0)
        {
            _editAreaIsAscii = true;
            setCursorPosition(_pointers.getOffset(ptrStart) * 2);
            ensureVisible();
        }
        // jump to pointer
        else if (_pointers.hasOffset(_bPosCurrent))
        {
            _editAreaIsAscii = false;

            auto ptrs = _pointers.getPointers(_bPosCurrent);

            if (ptrs.size() == 1)
            {
                setCursorPosition(ptrs[0] * 2);
                ensureVisible();
            }
            else
            {
                // TODO: display context menu/listbox with pointers list
            }
        }
    }
}

void QHexEdit::contextMenuEvent(QContextMenuEvent *event)
{
    emit contextMenuRequested(event->globalPos(), _bPosCurrent);
}

void QHexEdit::paintEvent(QPaintEvent *event)
{
    QPainter painter(viewport());
    auto pxOfsX = horizontalScrollBar()->value();

    if (event->rect() != _asciiCursorRect && event->rect() != _hexCursorRect)
    {
        const int rowStridePx = _pxCharHeight + kHexRowExtraGapPx;
        int pxPosStartY = _pxCharHeight;

        // draw some patterns if needed
        painter.fillRect(event->rect(), viewport()->palette().color(QPalette::Base));

        // Fill hex area with background color
        if (_asciiArea)
        {
            // Stop at the separator line (half-gap before ASCII area)
            const int hexAreaWidth = _pxPosAsciiX - _pxPosHexX - (_pxGapHexAscii / 2);
            painter.fillRect(QRect(_pxPosHexX - pxOfsX, event->rect().top(), hexAreaWidth, height()), _hexAreaBackgroundColor);
        }
        else
        {
            // No ASCII area: hex background extends to the right edge of the viewport
            painter.fillRect(QRect(_pxPosHexX - pxOfsX, event->rect().top(), viewport()->width(), height()), _hexAreaBackgroundColor);
        }

        painter.setPen(viewport()->palette().color(QPalette::WindowText));

        // paint address area
        if (_addressArea)
        {
            QString address;

            auto rowsCount = (_dataShown.size() / _bytesPerLine);

            painter.fillRect(QRect(-pxOfsX, event->rect().top(), _pxPosHexX - _pxGapAdrHex, height()), _addressAreaColor);

            const QFont originalFont = painter.font();
            QFont boldFont = originalFont;
            boldFont.setBold(true);
            painter.setFont(boldFont);

            for (int row = 0, pxPosY = _pxCharHeight; row <= rowsCount; row++, pxPosY += rowStridePx)
            {
                address = QString("%1").arg(_bPosFirst + row * _bytesPerLine + _addressOffset, _addrDigits, 16, QChar('0'));
                painter.setPen(QPen(_addressFontColor));
                painter.drawText(_pxPosAdrX - pxOfsX, pxPosY, hexCaps() ? address.toUpper() : address);
            }

            painter.setFont(originalFont);
        }

        // paint hex and ascii area
        painter.setBackgroundMode(Qt::TransparentMode);

        if (_asciiArea)
        {
            // ASCII area starts flush with the separator line (no gap between hex and ascii backgrounds)
            const int asciiAreaStartX = _pxPosAsciiX - (_pxGapHexAscii / 2);
            painter.fillRect(QRect(asciiAreaStartX - pxOfsX, event->rect().top(), width(), height()), _asciiAreaColor);

            // Draw the separator line on top of the ASCII background
            painter.setPen(Qt::gray);
            painter.drawLine(asciiAreaStartX - pxOfsX, event->rect().top(), asciiAreaStartX - pxOfsX, height());

            ensureAsciiAreaWidthCache();
        }

        const int hexStridePx = 3 * _pxCharWidth + kHexColumnExtraGapPx;

        // Build display caches once for the whole viewport (handles cross-row sequences)
        ensureTableDisplayCache();
        ensureEncodingDisplayCache();
        const bool useTbDisplayCache = !_tbDisplayChars.isEmpty();
        const bool useEncodingDecoder = !_encodingChars.isEmpty();
        const QFontMetrics paintFm = QFontMetrics(font());
        const auto slotGapPx = [this](int baseWidth) {
            return (baseWidth > _pxCharWidth) ? kAsciiColumnGapWidePx : kAsciiColumnGapSinglePx;
        };

        // Pre-compute which buffer group the cursor belongs to (for multi-byte cursor highlight)
        const qint64 cursorBufIdxGlobal = _cursorPosition / 2 - _bPosFirst;
        qint64 cursorLeadBufIdx = cursorBufIdxGlobal;
        int cursorMultiByteSpan = 1;
        if (useTbDisplayCache && cursorBufIdxGlobal >= 0 && cursorBufIdxGlobal < _tbDisplayChars.size()) {
            qint64 li = cursorBufIdxGlobal;
            while (li > 0 && li < _tbDisplayChars.size() && _tbDisplayChars[(int)li].isNull())
                --li;
            if (li >= 0 && li < _tbDisplaySpan.size() && _tbDisplaySpan[(int)li] > 1) {
                cursorLeadBufIdx = li;
                cursorMultiByteSpan = _tbDisplaySpan[(int)li];
            }
        } else if (useEncodingDecoder && cursorBufIdxGlobal >= 0 && cursorBufIdxGlobal < _encodingChars.size()) {
            qint64 li = cursorBufIdxGlobal;
            while (li > 0 && li < _encodingChars.size() && _encodingChars[(int)li].isNull())
                --li;
            if (li >= 0 && li < _encodingSpan.size() && _encodingSpan[(int)li] > 1) {
                cursorLeadBufIdx = li;
                cursorMultiByteSpan = _encodingSpan[(int)li];
            }
        }

        for (int row = 0; row < _rowsShown; row++)
        {
            QByteArray hex;
            int pxPosY = pxPosStartY + row * rowStridePx;
            int pxPosX = _pxPosHexX - pxOfsX;
            int pxPosAsciiX2 = _pxPosAsciiX + kAsciiAreaLeftPaddingPx - pxOfsX;
            qint64 bPosLine = row * _bytesPerLine;

            const bool useTbMultiByte = useTbDisplayCache;
            const qint64 rowStart = bPosLine;
            const qint64 rowEnd = qMin(bPosLine + _bytesPerLine, (qint64)_dataShown.size());

            // can be slow here
            for (int colIdx = 0; ((bPosLine + colIdx) < _dataShown.size() && (colIdx < _bytesPerLine)); colIdx++)
            {
                QColor c = viewport()->palette().color(QPalette::Base);

                const char rawByte = _dataShown.at(bPosLine + colIdx);
                const bool isZeroByte = (rawByte == 0);
                painter.setPen(QPen(isZeroByte ? _zeroByteFontColor : _hexFontColor));

                qint64 posBa = _bPosFirst + bPosLine + colIdx;
                const qint64 pointerStart = _showPointers ? pointerStartAt(posBa, kPointerByteSize) : -1;
                const bool isPointerByte = pointerStart >= 0;
                const int actualPtrSize = isPointerByte ? _pointers.getPointerSize(pointerStart) : kPointerByteSize;
                const bool isPointedByte = _showPointers && _pointers.hasOffset(posBa);
                const bool isSelectedByte = (getSelectionEnd() - getSelectionBegin() > 1)
                                         && (getSelectionBegin() <= posBa) && (getSelectionEnd() > posBa);
                const bool isHighlightedByte = _highlighting && _markedShown.at((int)(posBa - _bPosFirst));

                if (isSelectedByte)
                {
                    c = _brushSelection.color();
                    painter.setPen(_penSelection);
                }
                else
                {
                    if (isHighlightedByte)
                    {
                        c = _brushHighlighted.color();
                        painter.setPen(_penHighlighted);
                    }
                }

                // POINTERS
                if (_showPointers)
                {
                    // cursor image for pointed data
                    if (isPointedByte)
                    {
                        QImage ptrIcon = QImage(":/images/pointer.png");

                        if (!isSelectedByte && !isHighlightedByte)
                            c = _brushPointed.color();

                        if (!isSelectedByte)
                            painter.setPen(_penPointed);

                        painter.drawImage(pxPosX - _pxCharWidth - 2, pxPosY - (_pxCharHeight / 2), ptrIcon, 0, 0, 10, 10);
                    }

                    if (isPointerByte && !isSelectedByte && !isHighlightedByte && !isPointedByte)
                        painter.setPen(_penPointers);
                    
                    if (isPointerByte)
                    {
                        // Draw pointer frame only at the first byte of the pointer on each row
                        // The frame is clipped to the current line so it doesn't bleed into ASCII area
                        const int colInLine = static_cast<int>(posBa % _bytesPerLine);
                        const int ptrStartCol = static_cast<int>(pointerStart % _bytesPerLine);

                        // We draw a partial frame segment on every row that the pointer occupies
                        // Determine how many bytes of this pointer are on the current row starting from colIdx
                        const int ptrEndByteExcl = static_cast<int>((pointerStart - _bPosFirst) + actualPtrSize);
                        const int rowEndCol = _bytesPerLine; // exclusive

                        if (posBa == pointerStart || colInLine == 0)
                        {
                            // First pointer byte on this row — draw frame segment
                            const int bytesOnThisRow = qMin(ptrEndByteExcl - static_cast<int>(bPosLine + colIdx), rowEndCol - colInLine);

                            if (bytesOnThisRow > 0)
                            {
                                QPen pen;
                                pen.setColor(_pointerFrameColor);
                                pen.setWidth(1);
                                painter.setPen(pen);

                                auto frame = QRect(pxPosX - 6, pxPosY - _pxCharHeight + _pxSelectionSub + 1,
                                                    (3 * bytesOnThisRow) * _pxCharWidth + (bytesOnThisRow - 1) * kHexColumnExtraGapPx,
                                                    _pxCharHeight - _pxSelectionSub + 4);

                                painter.drawRect(frame);
                                painter.fillRect(frame, _pointerFrameBackgroundColor);

                                if (_asciiArea)
                                {
                                    const int asciiStartX = pxPosAsciiX2;
                                    int asciiFrameWidth = 0;
                                    for (int k = 0; k < bytesOnThisRow; ++k)
                                    {
                                        const qint64 rowBytePos = bPosLine + colIdx + k;
                                        if (rowBytePos >= _dataShown.size())
                                            break;

                                        const uint8_t rowByte = static_cast<uint8_t>(_dataShown.at(rowBytePos));
                                        const int baseW = (_tb && !_tbSymbolWidthPxCache.isEmpty())
                                            ? _tbSymbolWidthPxCache[rowByte]
                                            : _pxCharWidth;
                                        const int slotW = baseW + slotGapPx(baseW);
                                        asciiFrameWidth += slotW;
                                    }

                                    if (asciiFrameWidth > 0)
                                    {
                                        auto asciiFrame = QRect(asciiStartX - 4,
                                                                pxPosY - _pxCharHeight + _pxSelectionSub - 3,
                                                                asciiFrameWidth + 2,
                                                                _pxCharHeight - _pxSelectionSub + 4);
                                        painter.drawRect(asciiFrame);
                                        painter.fillRect(asciiFrame, _pointerFrameBackgroundColor);
                                    }
                                }
                            }
                        }
                    }
                }

                // render hex value
                auto r = QRect(pxPosX - 1, pxPosY - _pxCharHeight + _pxSelectionSub, 2 * _pxCharWidth + 2, _pxCharHeight + 1);

                // Only fill background if there's actual highlighting/selection (not just base color)
                if (c != viewport()->palette().color(QPalette::Base))
                    painter.fillRect(r, c);

                // Overlay cursor-char highlight: single-byte cursor fills here; multi-byte handled below.
                const bool isCursorByte = (bPosLine + colIdx) == (_cursorPosition / 2 - _bPosFirst);
                const qint64 byteInBuf = bPosLine + colIdx;
                const bool isCursorGroupByte = (byteInBuf >= cursorLeadBufIdx)
                                            && (byteInBuf < cursorLeadBufIdx + cursorMultiByteSpan);

                if (cursorMultiByteSpan == 1 && isCursorByte && _cursorCharColor.alpha() > 0)
                    painter.fillRect(r, _cursorCharColor);

                hex = _hexDataShown.mid((bPosLine + colIdx) * 2, 2);

                // In hex area: draw the active nibble of the cursor byte in bold
                if (isCursorByte && !_editAreaIsAscii)
                {
                    const int activeNibble = _cursorPosition % 2; // 0 = high, 1 = low
                    const QString ch0 = (hexCaps() ? hex.toUpper() : hex).mid(0, 1);
                    const QString ch1 = (hexCaps() ? hex.toUpper() : hex).mid(1, 1);

                    QFont boldFont = painter.font();
                    boldFont.setBold(true);
                    QFont normalFont = painter.font();

                    if (activeNibble == 0)
                    {
                        painter.setFont(boldFont);
                        painter.drawText(pxPosX, pxPosY, ch0);
                        painter.setFont(normalFont);
                        painter.drawText(pxPosX + _pxCharWidth, pxPosY, ch1);
                    }
                    else
                    {
                        painter.drawText(pxPosX, pxPosY, ch0);
                        painter.setFont(boldFont);
                        painter.drawText(pxPosX + _pxCharWidth, pxPosY, ch1);
                        painter.setFont(normalFont);
                    }
                }
                else
                {
                    painter.drawText(pxPosX, pxPosY, hexCaps() ? hex.toUpper() : hex);
                }
                // Multi-byte TBL entry frame in hex area; draw per-row segment so wrapped entries are framed too.
                if (useTbMultiByte && _showMultibyteFrame)
                {
                    const qint64 globalIdx = bPosLine + colIdx;
                    if (globalIdx < _tbDisplayChars.size())
                    {
                        qint64 leadIdx = globalIdx;
                        while (leadIdx > 0
                               && leadIdx < _tbDisplayChars.size()
                               && _tbDisplayChars[(int)leadIdx].isNull())
                            --leadIdx;

                        if (leadIdx >= 0 && leadIdx < _tbDisplaySpan.size())
                        {
                            const int span = _tbDisplaySpan[(int)leadIdx];
                            if (span > 1)
                            {
                                const qint64 entryEnd = leadIdx + span;
                                const qint64 segmentStart = qMax(leadIdx, rowStart);
                                const qint64 segmentEnd = qMin(entryEnd, rowEnd);
                                const int bytesOnThisRow = (int)qMax<qint64>(0, segmentEnd - segmentStart);
                                if (bytesOnThisRow > 0 && globalIdx == segmentStart)
                                {
                                    const int fW = (bytesOnThisRow - 1) * hexStridePx + 2 * _pxCharWidth + 2;
                                    const int x = pxPosX - 1;
                                    const int y = pxPosY - _pxCharHeight + _pxSelectionSub;
                                    const int h = _pxCharHeight + 1;

                                    const bool drawLeft = (segmentStart == leadIdx);
                                    const bool drawRight = (segmentEnd == entryEnd);

                                    QPen fPen(_multibyteFrameColor, 1, Qt::DashLine);
                                    const QPen savedPen = painter.pen();
                                    painter.setPen(fPen);
                                    painter.drawLine(x, y, x + fW - 1, y);
                                    painter.drawLine(x, y + h, x + fW - 1, y + h);
                                    if (drawLeft)
                                        painter.drawLine(x, y, x, y + h);
                                    if (drawRight)
                                        painter.drawLine(x + fW - 1, y, x + fW - 1, y + h);
                                    painter.setPen(savedPen);
                                }
                            }
                        }
                    }
                }
                // Multi-byte encoding frame in hex area (analogous to TBL multi-byte frame)
                else if (useEncodingDecoder && _showMultibyteFrame)
                {
                    const qint64 globalIdx = bPosLine + colIdx;
                    if (globalIdx < _encodingChars.size())
                    {
                        qint64 leadIdx = globalIdx;
                        while (leadIdx > 0
                               && leadIdx < _encodingChars.size()
                               && _encodingChars[(int)leadIdx].isNull())
                            --leadIdx;

                        if (leadIdx >= 0 && leadIdx < _encodingSpan.size())
                        {
                            const int span = _encodingSpan[(int)leadIdx];
                            if (span > 1)
                            {
                                const qint64 entryEnd = leadIdx + span;
                                const qint64 segmentStart = qMax(leadIdx, rowStart);
                                const qint64 segmentEnd = qMin(entryEnd, rowEnd);
                                const int bytesOnThisRow = (int)qMax<qint64>(0, segmentEnd - segmentStart);
                                if (bytesOnThisRow > 0 && globalIdx == segmentStart)
                                {
                                    const int fW = (bytesOnThisRow - 1) * hexStridePx + 2 * _pxCharWidth + 2;
                                    const int x = pxPosX - 1;
                                    const int y = pxPosY - _pxCharHeight + _pxSelectionSub;
                                    const int h = _pxCharHeight + 1;

                                    const bool drawLeft = (segmentStart == leadIdx);
                                    const bool drawRight = (segmentEnd == entryEnd);

                                    QPen fPen(_multibyteFrameColor, 1, Qt::DashLine);
                                    const QPen savedPen = painter.pen();
                                    painter.setPen(fPen);
                                    painter.drawLine(x, y, x + fW - 1, y);
                                    painter.drawLine(x, y + h, x + fW - 1, y + h);
                                    if (drawLeft)
                                        painter.drawLine(x, y, x, y + h);
                                    if (drawRight)
                                        painter.drawLine(x + fW - 1, y, x + fW - 1, y + h);
                                    painter.setPen(savedPen);
                                }
                            }
                        }
                    }
                }

                // Multi-byte cursor group: unconditional fill + frame for this row segment.
                // Uses per-byte walkback identical to the TBL/encoding frame blocks above.
                // Runs regardless of _showMultibyteFrame so the cursor is always visible.
                if (cursorMultiByteSpan > 1)
                {
                    const qint64 globalIdx = bPosLine + colIdx;
                    qint64 leadIdx = cursorLeadBufIdx; // already pre-computed
                    const int entryEnd = (int)(leadIdx + cursorMultiByteSpan);
                    if (globalIdx >= leadIdx && globalIdx < entryEnd)
                    {
                        const qint64 segmentStart = qMax(leadIdx, rowStart);
                        const qint64 segmentEnd   = qMin((qint64)entryEnd, rowEnd);
                        const int bytesOnThisRow   = (int)qMax<qint64>(0, segmentEnd - segmentStart);
                        if (bytesOnThisRow > 0 && globalIdx == segmentStart)
                        {
                            const int fW = (bytesOnThisRow - 1) * hexStridePx + 2 * _pxCharWidth + 2;
                            const int fx = pxPosX - 1;
                            const int fy = pxPosY - _pxCharHeight + _pxSelectionSub;
                            const int fh = _pxCharHeight + 1;
                            const bool drawLeft  = (segmentStart == leadIdx);
                            const bool drawRight = (segmentEnd == (qint64)entryEnd);

                            // Wide fill covering all segment bytes on this row
                            if (_cursorCharColor.alpha() > 0)
                            {
                                painter.fillRect(QRect(fx, fy, fW, fh), _cursorCharColor);
                                // Redraw the hex text of all bytes in the segment (fill covered them)
                                for (int k = 0; k < bytesOnThisRow; ++k)
                                {
                                    const qint64 kBufIdx = segmentStart + k;
                                    const QByteArray kHex = _hexDataShown.mid((int)kBufIdx * 2, 2);
                                    const int kX = pxPosX + k * hexStridePx;
                                    if ((kBufIdx + _bPosFirst) == (qint64)_bPosCurrent && !_editAreaIsAscii)
                                    {
                                        const int activeNibble = _cursorPosition % 2;
                                        const QString ch0 = hexCaps() ? kHex.mid(0,1).toUpper() : QString(kHex.mid(0,1));
                                        const QString ch1 = hexCaps() ? kHex.mid(1,1).toUpper() : QString(kHex.mid(1,1));
                                        QFont boldFont = painter.font(); boldFont.setBold(true);
                                        const QFont normalFont = painter.font();
                                        if (activeNibble == 0) {
                                            painter.setFont(boldFont); painter.drawText(kX, pxPosY, ch0);
                                            painter.setFont(normalFont); painter.drawText(kX + _pxCharWidth, pxPosY, ch1);
                                        } else {
                                            painter.drawText(kX, pxPosY, ch0);
                                            painter.setFont(boldFont); painter.drawText(kX + _pxCharWidth, pxPosY, ch1);
                                            painter.setFont(normalFont);
                                        }
                                    }
                                    else
                                    {
                                        painter.drawText(kX, pxPosY, hexCaps() ? kHex.toUpper() : QString(kHex));
                                    }
                                }
                            }
                            // Draw cursor-color solid frame around this segment (pen width 2 to match single-byte cursor)
                            if (!_readOnly)
                            {
                                const QPen savedPen = painter.pen();
                                painter.setPen(QPen(_cursorFrameColor, 2, Qt::SolidLine));
                                painter.drawLine(fx, fy, fx + fW - 1, fy);
                                painter.drawLine(fx, fy + fh, fx + fW - 1, fy + fh);
                                if (drawLeft)  painter.drawLine(fx, fy, fx, fy + fh);
                                if (drawRight) painter.drawLine(fx + fW - 1, fy, fx + fW - 1, fy + fh);
                                painter.setPen(savedPen);
                            }
                        }
                    }
                }

                pxPosX += hexStridePx;

                // render ascii value
                if (_asciiArea)
                {
                    if (c == viewport()->palette().color(QPalette::Base))
                        c = _asciiAreaColor;

                    QChar ch = QChar::fromLatin1(rawByte);

                    QString sym;
                    // For multi-byte table mode: continuation bytes are null entries in cache.
                    const int encBufIdx = (int)(bPosLine + colIdx);
                    const bool isTbContinuation = useTbMultiByte
                        && encBufIdx < _tbDisplayChars.size()
                        && _tbDisplayChars[encBufIdx].isNull();

                    // For encoding mode: look up from the buffer-level cache
                    const bool isEncodingContinuation = useEncodingDecoder
                        && encBufIdx < _encodingChars.size()
                        && _encodingChars[encBufIdx].isNull();
                    const bool isContinuationByte = isTbContinuation || isEncodingContinuation;

                    if (_tb) {
                        if (useTbMultiByte && encBufIdx < _tbDisplayChars.size()) {
                            sym = _tbDisplayChars[encBufIdx];
                        } else {
                            sym = _tb->encodeSymbol(rawByte);
                        }
                    } else if (useEncodingDecoder && encBufIdx < _encodingChars.size()) {
                        sym = _encodingChars[encBufIdx];
                    } else {
                        sym = ch;
                    }

                    if (_tb)
                    {
                        if (!sym.size() && !isTbContinuation)
                            sym = QString(_notInTableChar); // □ for unmapped single-byte
                        // TBL continuation: leave sym null — zero-width slot, no drawing
                    }
                    else if (!isContinuationByte)
                    {
                        if (sym.isEmpty() || (sym.size() == 1 && !sym[0].isPrint()))
                            sym = QString(_nonPrintableNoTableChar);
                    }
                    // Encoding/TBL continuation: sym is null/empty and isContinuationByte=true → zero-width, no draw

                    const uint8_t byteValue = static_cast<uint8_t>(rawByte);

                    // Slot width: continuation=0; multi-byte TBL uses actual advance; single-byte TBL uses cache;
                    // encoding uses actual advance; otherwise fixed
                    int baseSymWidthPx;
                    if (isContinuationByte)
                        baseSymWidthPx = 0;
                    else if (useTbMultiByte && !sym.isEmpty())
                        baseSymWidthPx = qMax(_pxCharWidth, paintFm.horizontalAdvance(sym));
                    else if (_tb && !_tbSymbolWidthPxCache.isEmpty())
                        baseSymWidthPx = _tbSymbolWidthPxCache[byteValue];
                    else if (useEncodingDecoder && !sym.isEmpty())
                        baseSymWidthPx = qMax(_pxCharWidth, paintFm.horizontalAdvance(sym));
                    else
                        baseSymWidthPx = _pxCharWidth;
                    const int symWidthPx = isContinuationByte ? 0 : (baseSymWidthPx + slotGapPx(baseSymWidthPx));

                    r.setRect(pxPosAsciiX2 - 1, pxPosY - _pxCharHeight + _pxSelectionSub + 2,
                              qMax(1, symWidthPx), _pxCharHeight);

                    if (!isContinuationByte) {
                        if (c != _asciiAreaColor)
                            painter.fillRect(r, c);

                        if (isCursorGroupByte && _cursorCharColor.alpha() > 0)
                            painter.fillRect(QRect(r.x(), r.y() - 2, baseSymWidthPx, r.height() + 2), _cursorCharColor);

                        if (isSelectedByte)
                            painter.setPen(_penSelection);
                        else if (isHighlightedByte)
                            painter.setPen(_penHighlighted);
                        else if (isPointedByte)
                            painter.setPen(_penPointed);
                        else if (isPointerByte)
                            painter.setPen(_penPointers);
                        else
                            painter.setPen(QPen((isZeroByte && !useTbMultiByte) ? _zeroByteFontColor : _asciiFontColor));
                        painter.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, sym);
                    }

                    if (!isContinuationByte && (bPosLine + colIdx) == cursorLeadBufIdx)
                        _asciiCursorRect = QRect(r.x() - 1, r.y() - 2, baseSymWidthPx + 2, r.height() + 2);

                    pxPosAsciiX2 += symWidthPx;
                }
            }
        }

        painter.setBackgroundMode(Qt::TransparentMode);
        painter.setPen(viewport()->palette().color(QPalette::WindowText));
    }

    // Draw hex area grid (vertical lines every 4 bytes)
    if (_showHexGrid && event->rect() != _asciiCursorRect && event->rect() != _hexCursorRect)
    {
        painter.setPen(QPen(_hexAreaGridColor, 1));

        const int rowStridePx = _pxCharHeight + kHexRowExtraGapPx;
        int pxPosStartY = _pxCharHeight;
        int pxPosEndY = pxPosStartY + (_rowsShown + 1) * rowStridePx;

        // Draw vertical grid lines every 4 bytes (between groups)
        const int hexStridePx = 3 * _pxCharWidth + kHexColumnExtraGapPx;
        for (int col = 4; col < _bytesPerLine; col += 4)
        {
            int pxPosX = _pxPosHexX + col * hexStridePx - ((_pxCharWidth + kHexColumnExtraGapPx) / 2) - pxOfsX;
            painter.drawLine(pxPosX, pxPosStartY - _pxCharHeight, pxPosX, pxPosEndY);
        }
    }

    // _cursorPosition counts in 2, _bPosFirst counts in 1
    int hexPositionInShowData = _cursorPosition - 2 * _bPosFirst;

    // due to scrolling the cursor can go out of the currently displayed data
    if ((hexPositionInShowData >= 0) && (hexPositionInShowData < _hexDataShown.size()))
    {
        // paint cursor
        if (_readOnly)
        {
            QColor color = viewport()->palette().dark().color();
            painter.fillRect(QRect(_pxCursorX - pxOfsX, _pxCursorY - _pxCharHeight + _pxSelectionSub, _pxCharWidth, _pxCharHeight), color);
        }
        else
        {
            QPen pen;
            pen.setColor(_cursorFrameColor);
            pen.setWidth(2);
            painter.setPen(pen);

            // For multi-byte cursor, the in-loop block already draws the per-row frame segments
            // (with correct open sides at row-wrap points). Only use drawRect for single-byte cursor.
            if (_cursorMultiByteSpan <= 1)
                painter.drawRect(_hexCursorRect);

            // draw cursor rect
            if (_asciiArea)
                painter.drawRect(_asciiCursorRect);

            painter.setPen(QPen(_hexFontColor));
        }
    }

    // emit event, if size has changed
    if (_lastEventSize != _chunks->size())
    {
        _lastEventSize = _chunks->size();
        emit currentSizeChanged(_lastEventSize);
    }

}

void QHexEdit::resizeEvent(QResizeEvent *)
{
    if (_dynamicBytesPerLine)
    {
        int selectedBpl = 4;
        const int viewportWidthPx = viewport()->width();

        for (int candidateBpl = 64; candidateBpl >= 4; candidateBpl -= 4)
        {
            const int addrDigits = _addressArea ? addressWidth() : 0;
            const int pxPosHexX = _addressArea
                                      ? (_pxGapAdr + addrDigits * _pxCharWidth + _pxGapAdrHex + kAddressRightPaddingPx)
                                      : _pxGapAdrHex;
            const int candidateHexCharsInLine = candidateBpl * 3 - 1;
            const int pxHexEndX = pxPosHexX + candidateHexCharsInLine * _pxCharWidth + (candidateBpl - 1) * kHexColumnExtraGapPx;
            const int pxPosAsciiX = pxHexEndX + _pxGapHexAscii;
            const int asciiWidthPx = _asciiArea ? static_cast<int>(computeAsciiAreaMaxWidthForBytesPerLine(candidateBpl)) : 0;
            const int requiredWidthPx = _asciiArea ? (pxPosAsciiX + kAsciiAreaLeftPaddingPx + asciiWidthPx) : pxHexEndX;

            if (requiredWidthPx <= viewportWidthPx)
            {
                selectedBpl = candidateBpl;
                break;
            }
        }

        if (selectedBpl != _bytesPerLine)
            setBytesPerLine(selectedBpl);
        else
            updateAsciiAreaMaxWidth();
    }

    adjust();
}

bool QHexEdit::focusNextPrevChild(bool next)
{
    if (_addressArea)
    {
        if ((next && _editAreaIsAscii) || (!next && !_editAreaIsAscii))
            return QWidget::focusNextPrevChild(next);
        else
            return false;
    }
    else
    {
        return QWidget::focusNextPrevChild(next);
    }
}

// ********************************************************************** Handle selections
bool QHexEdit::hasSelection()
{
    return _bSelectionEnd - _bSelectionBegin > 1;
}

void QHexEdit::resetSelection()
{
    _bSelectionBegin = _bSelectionInit;
    _bSelectionEnd = _bSelectionInit;

    emit selectionChanged(_bSelectionBegin, _bSelectionEnd);
}

void QHexEdit::resetSelection(qint64 pos)
{
    pos = pos / 2;
    if (pos < 0)
        pos = 0;
    if (pos > _chunks->size())
        pos = _chunks->size();

    _bSelectionInit = pos;
    _bSelectionBegin = pos;
    _bSelectionEnd = pos;

    emit selectionChanged(_bSelectionBegin, _bSelectionEnd);
}

void QHexEdit::setSelection(qint64 pos)
{
    pos = pos / 2;

    if (pos < 0)
        pos = 0;

    if (pos > _chunks->size())
        pos = _chunks->size();

    if (pos >= _bSelectionInit)
    {
        // Include the cursor byte: end is exclusive, so +1
        _bSelectionEnd = qMin(pos + 1, _chunks->size());
        _bSelectionBegin = _bSelectionInit;
    }
    else
    {
        // Cursor is before init: include init byte too
        _bSelectionBegin = pos;
        _bSelectionEnd = qMin(_bSelectionInit + 1, _chunks->size());
    }

    emit selectionChanged(_bSelectionBegin, _bSelectionEnd);
}

qint64 QHexEdit::getSelectionBegin()
{
    return _bSelectionBegin;
}

qint64 QHexEdit::getSelectionEnd()
{
    return _bSelectionEnd;
}

// ********************************************************************** Private utility functions
void QHexEdit::init()
{
    _undoStack->clear();
    setAddressOffset(0);
    resetSelection(0);
    setCursorPosition(0);
    verticalScrollBar()->setValue(0);

    _modified = false;
}

void QHexEdit::adjust()
{
    // recalc Graphics
    if (_addressArea)
    {
        _addrDigits = addressWidth();
        _pxPosHexX = _pxGapAdr + _addrDigits * _pxCharWidth + _pxGapAdrHex + kAddressRightPaddingPx;
    }
    else
        _pxPosHexX = _pxGapAdrHex;

    _pxPosAdrX = _pxGapAdr;
    _pxPosAsciiX = _pxPosHexX + _hexCharsInLine * _pxCharWidth + (_bytesPerLine - 1) * kHexColumnExtraGapPx + _pxGapHexAscii;

    // set horizontalScrollBar()
    int pxWidth;
    if (_asciiArea)
        pxWidth = _pxPosAsciiX + kAsciiAreaLeftPaddingPx + static_cast<int>(_asciiAreaMaxWidth);
    else
        pxWidth = _pxPosAsciiX - _pxGapHexAscii; // no gap wasted when ASCII area is hidden

    horizontalScrollBar()->setRange(0, pxWidth - viewport()->width());
    horizontalScrollBar()->setPageStep(viewport()->width());

    // set verticalScrollbar()
    const int rowStridePx = _pxCharHeight + kHexRowExtraGapPx;
    _rowsShown = ((viewport()->height() - 4) / rowStridePx);

    auto lineCount = (_chunks->size() / _bytesPerLine) + 1;

    verticalScrollBar()->setRange(0, lineCount - _rowsShown);
    verticalScrollBar()->setPageStep(_rowsShown);

    auto value = verticalScrollBar()->value();

    _bPosFirst = value * _bytesPerLine;
    _bPosLast = _bPosFirst + (_rowsShown * _bytesPerLine) - 1;

    if (_bPosLast >= _chunks->size())
        _bPosLast = _chunks->size() - 1;

    readBuffers();
    setCursorPosition(_cursorPosition);

    // Reposition scroll map strips to the right margin reserved by setViewportMargins().
    // Always use viewport geometry — scrollbar geometry is unreliable on macOS
    // overlay scrollbars (often zero/transient).
    {
        const QRect vp = viewport()->geometry();
        if (vp.isValid() && vp.height() > 0)
        {
            int x = vp.right() + 1;
            if (_scrollMapPtr && _scrollMapPtr->isVisible())
            {
                _scrollMapPtr->setGeometry(x, vp.top(), kScrollMapWidth, vp.height());
                x += kScrollMapWidth;
            }
            if (_scrollMapTarget && _scrollMapTarget->isVisible())
            {
                _scrollMapTarget->setGeometry(x, vp.top(), kScrollMapWidth, vp.height());
            }
        }
    }
}

void QHexEdit::dataChangedPrivate(int)
{
    _modified = _undoStack->index() != 0;

    invalidateAsciiAreaWidthCache();

    updateAsciiAreaMaxWidth();

    adjust();

    emit dataChanged();

    emit undoAvailable(_undoStack->canUndo());
    emit redoAvailable(_undoStack->canRedo());
}

void QHexEdit::refresh()
{
    ensureVisible();
    readBuffers();
}

uint32_t QHexEdit::computeAsciiAreaMaxWidthForBytesPerLine(int bytesPerLine)
{
    if (!_asciiArea || bytesPerLine <= 0)
        return 0;

    // When a table is loaded use its max symbol width, otherwise one char per byte.
    const int slotWidthPx = (_tb && _tbMaxSymbolWidthPx > 0)
        ? (_tbMaxSymbolWidthPx + ((_tbMaxSymbolWidthPx > _pxCharWidth) ? kAsciiColumnGapWidePx : kAsciiColumnGapSinglePx))
        : (_pxCharWidth + kAsciiColumnGapSinglePx);

    return static_cast<uint32_t>(bytesPerLine * slotWidthPx);
}

void QHexEdit::updateAsciiAreaMaxWidth()
{
    if (_tb)
        ensureAsciiAreaWidthCache();

    _asciiAreaMaxWidth = computeAsciiAreaMaxWidthForBytesPerLine(_bytesPerLine);
}

void QHexEdit::restoreTopVisibleByte(qint64 topByte)
{
    if (_bytesPerLine <= 0)
        return;

    const int topLine = static_cast<int>(qMax<qint64>(0, topByte) / _bytesPerLine);
    verticalScrollBar()->setValue(topLine);
    adjust();
}

void QHexEdit::invalidateAsciiAreaWidthCache()
{
    _asciiAreaWidthCacheValid = false;
    _tbMaxSymbolWidthPx = 0;
    _asciiAreaWidthCacheTable = nullptr;
    _asciiAreaWidthCacheDataSize = -1;
    _asciiAreaWidthCacheCharWidth = 0;
}

void QHexEdit::ensureAsciiAreaWidthCache()
{
    if (!_tb)
        return;

    if (_asciiAreaWidthCacheValid && _asciiAreaWidthCacheTable == _tb && _asciiAreaWidthCacheCharWidth == _pxCharWidth)
    {
        return;
    }

    _tbSymbolWidthPxCache = QVector<int>(256, _pxCharWidth);
    _asciiAreaMaxWidthByBpl = QVector<uint32_t>(65, 0);
    _tbMaxSymbolWidthPx = _pxCharWidth;

    const QFontMetrics fm(font());

    // Compute actual rendered width for each possible byte value using the table
    for (int value = 0; value <= 0xFF; ++value)
    {
        char rawByte = static_cast<char>(value);
        QString sym;
        
        if (_tb)
        {
            sym = _tb->encodeSymbol(rawByte);
            // Replace "not in table" placeholder with actual placeholder char
            if (!sym.size())
                sym = QString(_notInTableChar);
        }
        else
        {
            QChar ch = QChar::fromLatin1(rawByte);
            sym = ch;
            // Replace non-printable chars with placeholder
            if (sym.size() == 1 && !sym[0].isPrint())
                sym = QString(_nonPrintableNoTableChar);
        }
        
        // Measure actual font width rather than just counting characters
        int widthPx = qMax(_pxCharWidth, fm.horizontalAdvance(sym));
        _tbSymbolWidthPxCache[value] = widthPx;
        if (widthPx > _tbMaxSymbolWidthPx)
            _tbMaxSymbolWidthPx = widthPx;
    }

    for (int bpl = 4; bpl <= 64; bpl += 4) {
        const int gap = (_tbMaxSymbolWidthPx > _pxCharWidth) ? kAsciiColumnGapWidePx : kAsciiColumnGapSinglePx;
        _asciiAreaMaxWidthByBpl[bpl] = static_cast<uint32_t>(bpl * (_tbMaxSymbolWidthPx + gap));
    }

    // Also scan multi-byte entries to get the true maximum symbol width (e.g. kanji)
    if (_tb->hasMultiByteEntries())
    {
        for (const QString &val : _tb->getMultiByteItems())
        {
            const int widthPx = qMax(_pxCharWidth, fm.horizontalAdvance(val));
            if (widthPx > _tbMaxSymbolWidthPx)
                _tbMaxSymbolWidthPx = widthPx;
        }
        // Recompute per-bpl widths with updated max
        for (int bpl = 4; bpl <= 64; bpl += 4) {
            const int gap = (_tbMaxSymbolWidthPx > _pxCharWidth) ? kAsciiColumnGapWidePx : kAsciiColumnGapSinglePx;
            _asciiAreaMaxWidthByBpl[bpl] = static_cast<uint32_t>(bpl * (_tbMaxSymbolWidthPx + gap));
        }
    }

    _asciiAreaWidthCacheTable = _tb;
    _asciiAreaWidthCacheDataSize = -1;
    _asciiAreaWidthCacheCharWidth = _pxCharWidth;
    _asciiAreaWidthCacheValid = true;
}

void QHexEdit::readBuffers()
{
    _dataShown = _chunks->data(_bPosFirst, _bPosLast - _bPosFirst + _bytesPerLine + 1, &_markedShown);
    _hexDataShown = QByteArray(_dataShown.toHex());
    _encodingCacheValid = false;
    _tbDisplayCacheValid = false;
}

void QHexEdit::ensureEncodingDisplayCache()
{
    if (_encodingCacheValid) return;
    _encodingCacheValid = true;
    _encodingChars.clear();
    _encodingSpan.clear();
    if (_tb || _currentEncoding == QLatin1String("ASCII"))
        return; // not applicable: TBL active or plain ASCII
    _encodingChars = decodeBufferWithEncoding(_dataShown, _currentEncoding);

    // Derive span from _encodingChars: lead byte gets count of bytes in its sequence;
    // continuation bytes get 0 (same convention as _tbDisplaySpan).
    const int n = _encodingChars.size();
    _encodingSpan = QVector<int>(n, 0);
    int i = 0;
    while (i < n) {
        if (_encodingChars[i].isNull()) {
            // Orphaned continuation (shouldn't happen with valid data, but handle gracefully)
            _encodingSpan[i] = 0;
            ++i;
        } else {
            // Lead byte: span = 1 + number of consecutive null (continuation) entries
            int span = 1;
            while ((i + span) < n && _encodingChars[i + span].isNull())
                ++span;
            _encodingSpan[i] = span;
            i += span;
        }
    }
}

void QHexEdit::ensureTableDisplayCache()
{
    if (_tbDisplayCacheValid) return;
    _tbDisplayCacheValid = true;
    _tbDisplayChars.clear();
    _tbDisplaySpan.clear();
    if (!_tb || !_tb->hasMultiByteEntries())
        return;
    decodeBufferWithTable(_dataShown, _tb, _tbDisplayChars, _tbDisplaySpan);
}

QString QHexEdit::toReadable(const QByteArray &ba)
{
    QString result;

    auto baLen = ba.size();

    for (int i = 0; i < baLen; i += 16)
    {
        QString addrStr = QString("%1").arg(_addressOffset + i, addressWidth(), 16, QChar('0'));
        QString hexStr;
        const QByteArray rowBytes = ba.mid(i, qMin(16, baLen - i));
        QString ascStr = decodeTextForCurrentEncoding(rowBytes);

        for (int j = 0; j < 16; j++)
        {
            if ((i + j) < baLen)
            {
                hexStr.append(" ").append(ba.mid(i + j, 1).toHex());
            }
        }

        for (int k = 0; k < ascStr.size(); ++k)
            if (!ascStr[k].isPrint())
                ascStr[k] = QChar('.');

        result += addrStr + " " + QString("%1").arg(hexStr, -48) + "  " + QString("%1").arg(ascStr, -17) + "\n";
    }
    return result;
}

void QHexEdit::updateCursor()
{
    _blink = !_blink;

    viewport()->update(_asciiCursorRect);
    viewport()->update(_hexCursorRect);
}

// ---- Scroll map visibility API -------------------------------------------------

bool QHexEdit::scrollMapPtrVisible() const
{
    return _scrollMapPtrEnabled;
}

void QHexEdit::setScrollMapPtrVisible(bool visible)
{
    if (_scrollMapPtrEnabled == visible) return;
    _scrollMapPtrEnabled = visible;
    updateScrollMapMargins();
}

bool QHexEdit::scrollMapTargetVisible() const
{
    return _scrollMapTargetEnabled;
}

void QHexEdit::setScrollMapTargetVisible(bool visible)
{
    if (_scrollMapTargetEnabled == visible) return;
    _scrollMapTargetEnabled = visible;
    updateScrollMapMargins();
}

void QHexEdit::setScrollMapPtrBgColor(const QColor &c)
{
    if (_scrollMapPtr) _scrollMapPtr->setBgColor(c);
}

void QHexEdit::setScrollMapTargetBgColor(const QColor &c)
{
    if (_scrollMapTarget) _scrollMapTarget->setBgColor(c);
}

void QHexEdit::updateScrollMapMargins()
{
    // Immediate (non-debounced) re-evaluation of visibility + margins.
    // Also kicks the debounce timer for future model changes.
    scheduleScrollMapCompute();
    updateScrollMap();
}

// ---- Scroll map computation ---------------------------------------------------

void QHexEdit::updateScrollMap()
{
    // Debounce: restart timer on every rapid model change.
    // scheduleScrollMapCompute() fires once things settle.
    if (_scrollMapTimer)
        _scrollMapTimer->start();
}

void QHexEdit::scheduleScrollMapCompute()
{
    if (!_scrollMapWatcher || !_chunks)
        return;

    const bool hasPointers = !_pointers.empty();
    const bool wantPtr    = _scrollMapPtrEnabled    && hasPointers && _scrollMapPtr;
    const bool wantTarget = _scrollMapTargetEnabled && hasPointers && _scrollMapTarget;

    // Update strip visibility
    if (_scrollMapPtr)    _scrollMapPtr->setVisible(wantPtr);
    if (_scrollMapTarget) _scrollMapTarget->setVisible(wantTarget);
    if (wantPtr)    _scrollMapPtr->raise();
    if (wantTarget) _scrollMapTarget->raise();

    // Update viewport right margin only when it actually changes (avoids recursive relayout)
    const int newMargin = (wantPtr ? kScrollMapWidth : 0) + (wantTarget ? kScrollMapWidth : 0);
    if (newMargin != _scrollMapCurrentMargin)
    {
        _scrollMapCurrentMargin = newMargin;
        setViewportMargins(0, 0, newMargin, 0);
        // setViewportMargins relayouts the viewport synchronously in Qt6,
        // so we call adjust() immediately to size the strip before reading mapH.
    }
    adjust();  // always reposition strips so height() is up-to-date

    if (!wantPtr && !wantTarget)
    {
        if (_scrollMapPtr)    _scrollMapPtr->setTicks({});
        if (_scrollMapTarget) _scrollMapTarget->setTicks({});
        return;
    }

    // If previous background task still running, retry after debounce
    if (_scrollMapWatcher->isRunning())
    {
        _scrollMapTimer->start();
        return;
    }

    // Both strips share the same height (set by adjust()).
    // Read from viewport() directly — adjust() has just positioned the strips,
    // but reading viewport()->height() is always authoritative and avoids any
    // stale-geometry edge cases the first time the strips are shown.
    const int mapH = viewport()->height();
    const qint64 totalBytes = _chunks->size();

    if (mapH <= 0 || totalBytes <= 0)
        return;

    // Get real groove geometry via QStyleOptionSlider (initFrom is public).
    auto *vbar = verticalScrollBar();
    const int sbMin  = vbar->minimum();
    const int sbMax  = vbar->maximum();
    const int sbPage = vbar->pageStep();
    const int bytesPerLine = qMax(1, _bytesPerLine);

    // Build QStyleOptionSlider from public scrollbar API — no initStyleOption needed.
    // Use strip dimensions as the rect so subControlRect returns groove/thumb in strip coords.
    QStyleOptionSlider vbarOpt;
    vbarOpt.initFrom(vbar);                         // sets palette, state from the widget
    vbarOpt.rect          = QRect(0, 0, kScrollMapWidth, mapH);  // ← strip size, not vbar size
    vbarOpt.minimum       = sbMin;
    vbarOpt.maximum       = sbMax;
    vbarOpt.pageStep      = sbPage;
    vbarOpt.singleStep    = vbar->singleStep();
    vbarOpt.sliderValue   = vbar->value();
    vbarOpt.sliderPosition = vbar->sliderPosition();
    vbarOpt.orientation   = Qt::Vertical;
    vbarOpt.upsideDown    = vbar->invertedAppearance();
    vbarOpt.subControls   = QStyle::SC_All;
    vbarOpt.activeSubControls = QStyle::SC_None;

    // Groove rect — in strip coords because we set opt.rect to strip size.
    const QRect grooveRect = vbar->style()->subControlRect(
        QStyle::CC_ScrollBar, &vbarOpt, QStyle::SC_ScrollBarGroove, vbar);

    // Slider (thumb) rect — also in strip coords.
    const QRect sliderRect = vbar->style()->subControlRect(
        QStyle::CC_ScrollBar, &vbarOpt, QStyle::SC_ScrollBarSlider, vbar);

    const int grooveTop = grooveRect.isValid() ? grooveRect.top() : 0;
    const int grooveH   = qMax(1, grooveRect.isValid() ? grooveRect.height() : mapH);
    const int mThumbH   = qMax(1, sliderRect.isValid() ? sliderRect.height() : 1);

    // mGrooveTop/mGrooveH are already in strip (mapH) coords — no scaling needed.
    const int mGrooveTop = grooveTop;
    const int mGrooveH   = grooveH;

    QList<qint64> ptrKeys    = wantPtr    ? _pointers.pointerKeys() : QList<qint64>{};
    QList<qint64> targetKeys = wantTarget ? _pointers.offsetKeys()  : QList<qint64>{};

    auto future = QtConcurrent::run(
        [mapH, totalBytes,
         sbMin, sbMax, sbPage, bytesPerLine,
         mGrooveTop, mGrooveH, mThumbH,
         wantPtr, wantTarget,
         ptrKeys    = std::move(ptrKeys),
         targetKeys = std::move(targetKeys)]() -> ScrollMapMarkers
        {
            ScrollMapMarkers result;

            // Map byte offset → Y that matches the scrollbar thumb CENTER,
            // using real groove geometry obtained from QStyle on the main thread.
            auto offToY = [mapH, totalBytes,
                           sbMin, sbMax, sbPage, bytesPerLine,
                           mGrooveTop, mGrooveH, mThumbH](qint64 off) -> int
            {
                if (totalBytes <= 1 || mapH <= 1)
                    return 0;

                const qint64 offClamped = qBound<qint64>(0, off, totalBytes - 1);
                const double line = static_cast<double>(offClamped) / static_cast<double>(bytesPerLine);

                // Scrollbar value that centers this offset in the viewport.
                const double value = qBound(static_cast<double>(sbMin),
                                            line - static_cast<double>(sbPage) * 0.5,
                                            static_cast<double>(sbMax));

                // Thumb travels within groove: from mGrooveTop to mGrooveTop+mGrooveH-mThumbH.
                const double travel = qMax(0.0, static_cast<double>(mGrooveH - mThumbH));
                const double valueRange = (sbMax > sbMin) ? static_cast<double>(sbMax - sbMin) : 1.0;
                const double normVal = (value - sbMin) / valueRange;

                // Y of the thumb CENTER in strip coordinates.
                const double thumbTopInStrip = mGrooveTop + normVal * travel;
                const double thumbCenterY = thumbTopInStrip + static_cast<double>(mThumbH) * 0.5;

                return qBound(0, static_cast<int>(std::round(thumbCenterY)), mapH - 1);
            };

            if (wantPtr)
            {
                QVector<bool> px(mapH, false);
                for (qint64 off : ptrKeys) {
                    int y = offToY(off);
                    px[y] = true;
                    if (!result.ptrYToOff.contains(y))
                        result.ptrYToOff.insert(y, off);
                }
                for (int i = 0; i < mapH; ++i)
                    if (px[i]) result.ptrYs.push_back(i);
            }

            if (wantTarget)
            {
                QVector<bool> px(mapH, false);
                for (qint64 off : targetKeys) {
                    int y = offToY(off);
                    px[y] = true;
                    if (!result.targetYToOff.contains(y))
                        result.targetYToOff.insert(y, off);
                }
                for (int i = 0; i < mapH; ++i)
                    if (px[i]) result.targetYs.push_back(i);
            }

            return result;
        });

    _scrollMapWatcher->setFuture(future);
}

PointerListModel *QHexEdit::pointers()
{
    return &_pointers;
}

void QHexEdit::clearPointers()
{
    _pointers.clear();

    viewport()->update();
}

/**
 * The function searches for pointers in the specified direction(s) and adds them to the pointers list.
 *
 * @param order - byte order: LittleEndian, BigEndian or SwappedBytes
 * @param searchBefore - whether to search for pointers before the current selection
 * @param searchAfter - whether to search for pointers after the current selection
 * @param firstPrintable - if set, only consider values as pointers if the previous byte is not a printable character (between firstPrintable and lastPrintable)
 * @param lastPrintable - see firstPrintable
 * @param stopChar - if set, skip offsets where the first byte is equal to stopChar
 * @param excludeSelection - whether to exclude the current selection from search (only applicable if both searchBefore and searchAfter are true)
 * @return number of pointers found
 */
qint64 QHexEdit::findPointers(int pointerSize, ByteOrder order, bool searchBefore, bool searchAfter, const char *firstPrintable, const char *lastPrintable, char stopChar, bool excludeSelection)
{
    // Validate pointer size
    if (pointerSize < 2 || pointerSize > 4)
        pointerSize = 4;

    const QByteArray fileData = data();
    const char *buf = fileData.constData();
    const qint64 fileSize = fileData.size();

    qint64 selBegin = _bSelectionBegin;
    qint64 selEnd = _bSelectionEnd;
    if (selBegin >= selEnd)
    {
        selBegin = 0;
        selEnd = fileSize;
    }

    struct SearchRange
    {
        qint64 startOffset;
        qint64 endOffsetExclusive;
    };

    const qint64 maxDecodedStartExclusive = fileSize - pointerSize + 1;

    QVector<SearchRange> ranges;
    auto addRange = [&ranges](qint64 startOffset, qint64 endOffsetExclusive)
    {
        if (startOffset < endOffsetExclusive)
        {
            SearchRange range;
            range.startOffset = startOffset;
            range.endOffsetExclusive = endOffsetExclusive;
            ranges.push_back(range);
        }
    };

    if (searchBefore)
        addRange(0, selBegin);
    if (searchAfter)
        addRange(selEnd, fileSize);
    if (searchBefore && searchAfter && !excludeSelection)
    {
        ranges.clear();
        addRange(0, fileSize);
    }

    auto isCandidateOffset = [&](quint64 value) -> bool
    {
        const qint64 targetOffset = static_cast<qint64>(value);

        if (targetOffset < selBegin || targetOffset >= selEnd)
            return false;

        if (!firstPrintable || !lastPrintable)
            return true;

        if (targetOffset == 0)
            return true;

        const char prevChar = buf[targetOffset - 1];
        return !(prevChar >= *firstPrintable && prevChar <= *lastPrintable);
    };

    qint64 found = 0;
    qint64 its = 0;
    QElapsedTimer timer;
    timer.start();

    for (const auto &range : ranges)
    {
        const qint64 startOffset = qBound(static_cast<qint64>(0), range.startOffset, fileSize);
        const qint64 endOffsetExclusive = qBound(static_cast<qint64>(0), range.endOffsetExclusive, fileSize);
        const qint64 decodeEndExclusive = qMin(endOffsetExclusive, maxDecodedStartExclusive);

        for (qint64 j = startOffset; j < decodeEndExclusive; ++j)
        {
            if (stopChar && buf[j] == stopChar)
            {
                ++its;
                continue;
            }

            quint64 value = 0;
            const uchar *ptr = reinterpret_cast<const uchar *>(buf + j);
            value = decodePointer(ptr, pointerSize, order);

            if (isCandidateOffset(value))
            {
                _pointers.addPointer(j, static_cast<qint64>(value));
                ++found;
            }
            ++its;
        }
    }

    return found;
}

bool QHexEdit::addPointerUndoable(qint64 pointerOffset, qint64 targetOffset, int ptrSize)
{
    if (pointerOffset < 0 || targetOffset < 0)
        return false;

    const PointerState before = capturePointerState(&_pointers, pointerOffset);

    if (before.hasTarget && before.targetOffset == targetOffset && before.ptrSize == ptrSize)
        return false;

    PointerState after;
    after.pointerOffset = pointerOffset;
    after.hasTarget = true;
    after.targetOffset = targetOffset;
    after.ptrSize = ptrSize;

    _undoStack->push(new PointerEditCommand(&_pointers,
                                            QVector<PointerState>{before},
                                            QVector<PointerState>{after},
                                            tr("Add pointer")));
    refresh();
    return true;
}

bool QHexEdit::removePointerUndoable(qint64 pointerOffset)
{
    const PointerState before = capturePointerState(&_pointers, pointerOffset);
    if (!before.hasTarget)
        return false;

    PointerState after;
    after.pointerOffset = pointerOffset;
    after.hasTarget = false;

    _undoStack->push(new PointerEditCommand(&_pointers,
                                            QVector<PointerState>{before},
                                            QVector<PointerState>{after},
                                            tr("Drop pointer")));
    refresh();
    return true;
}

int QHexEdit::removePointersUndoable(const QVector<qint64> &pointerOffsets)
{
    if (pointerOffsets.isEmpty())
        return 0;

    QVector<PointerState> before;
    QVector<PointerState> after;
    QSet<qint64> uniqueOffsets;
    uniqueOffsets.reserve(pointerOffsets.size());

    for (qint64 pointerOffset : pointerOffsets)
    {
        if (uniqueOffsets.contains(pointerOffset))
            continue;
        uniqueOffsets.insert(pointerOffset);

        const PointerState stateBefore = capturePointerState(&_pointers, pointerOffset);
        if (!stateBefore.hasTarget)
            continue;

        before.append(stateBefore);

        PointerState stateAfter;
        stateAfter.pointerOffset = pointerOffset;
        stateAfter.hasTarget = false;
        after.append(stateAfter);
    }

    if (before.isEmpty())
        return 0;

    _undoStack->push(new PointerEditCommand(&_pointers, before, after, tr("Drop pointer")));
    refresh();
    return before.size();
}

int QHexEdit::removePointersToOffsetUndoable(qint64 targetOffset)
{
    const QList<qint64> pointersAtOffset = _pointers.getPointers(targetOffset);
    if (pointersAtOffset.isEmpty())
        return 0;

    QVector<PointerState> before;
    QVector<PointerState> after;
    before.reserve(pointersAtOffset.size());
    after.reserve(pointersAtOffset.size());

    for (qint64 pointerOffset : pointersAtOffset)
    {
        const PointerState stateBefore = capturePointerState(&_pointers, pointerOffset);
        if (!stateBefore.hasTarget)
            continue;

        before.append(stateBefore);

        PointerState stateAfter;
        stateAfter.pointerOffset = pointerOffset;
        stateAfter.hasTarget = false;
        after.append(stateAfter);
    }

    if (before.isEmpty())
        return 0;

    _undoStack->push(new PointerEditCommand(&_pointers, before, after, tr("Drop all")));
    refresh();
    return before.size();
}

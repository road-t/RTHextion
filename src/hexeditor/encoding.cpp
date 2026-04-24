#include "internal.h"
#include "encoding.h"
#include "translationtable.h"

// ═══════════════════════════════════════════════════════════════════════════
// Encoding codec tables, iconv helpers, and text decode/encode functions.
// Extracted from the anonymous namespace of the original hexeditor.cpp.
// ═══════════════════════════════════════════════════════════════════════════

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

// ── Common iconv wrapper ──
static QByteArray runIconv(const char *toCodec, const char *fromCodec,
                           const char *inData, size_t inLen, int outMultiplier = 4)
{
    iconv_t cd = iconv_open(toCodec, fromCodec);
    if (cd == (iconv_t)-1)
        return {};
    size_t inLeft = inLen;
    char *inBuf = const_cast<char *>(inData);
    QByteArray outBuf(static_cast<qsizetype>(inLen) * outMultiplier + 16, '\0');
    size_t outLeft = outBuf.size();
    char *outPtr = outBuf.data();
    iconv(cd, &inBuf, &inLeft, &outPtr, &outLeft);
    iconv_close(cd);
    outBuf.resize(outBuf.size() - static_cast<qsizetype>(outLeft));
    return outBuf;
}

QString decodeWithIconv(const QByteArray &data, const QString &encoding)
{
    const char *fromCodec = iconvCodecName(encoding);
    QByteArray encBytes;
    if (!fromCodec) {
        encBytes = encoding.toLatin1();
        fromCodec = encBytes.constData();
    }
    QByteArray utf8 = runIconv("UTF-8", fromCodec, data.constData(), data.size());
    return utf8.isEmpty() ? QString() : QString::fromUtf8(utf8);
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
    QByteArray result = runIconv(toCodec, "UTF-8", utf8.constData(), utf8.size(), 2);
    return result.isEmpty() ? text.toLatin1() : result;
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

    for (const QByteArray &codecName : codecCandidates(encoding)) {
        auto dec = QStringDecoder(codecName.constData());
        if (dec.isValid())
            return dec(data);
    }
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

    for (const QByteArray &codecName : codecCandidates(encoding)) {
        auto enc = QStringEncoder(codecName.constData());
        if (!enc.isValid())
            continue;
        const QByteArray out = enc(text);
        if (!out.isEmpty() || text.isEmpty())
            return out;
    }
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
    // Try QStringDecoder first, then iconv as fallback
    {
        bool usedQtDecoder = false;
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



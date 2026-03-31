#ifndef ENCODINGDETECT_H
#define ENCODINGDETECT_H

#include <QString>
#include <QByteArray>
#include <iconv.h>

/// Detect text encoding from file data.
/// Uses BOM signatures and statistical heuristics.
/// @param data  First chunk of file data (ideally 4KB+).
/// @return Detected encoding name matching the Encoding menu names, or "ASCII".
inline QString detectEncoding(const QByteArray &data)
{
    if (data.isEmpty())
        return QStringLiteral("ASCII");

    const int sz = qMin(data.size(), 4096);
    auto ub = [&](int i) -> unsigned char { return static_cast<unsigned char>(data[i]); };

    // ---- Step 1: BOM signatures (definitive) ----
    if (sz >= 3 && ub(0) == 0xEF && ub(1) == 0xBB && ub(2) == 0xBF)
        return QStringLiteral("UTF-8");
    if (sz >= 2 && ub(0) == 0xFF && ub(1) == 0xFE)
        return QStringLiteral("UTF-16 LE");
    if (sz >= 2 && ub(0) == 0xFE && ub(1) == 0xFF)
        return QStringLiteral("UTF-16 BE");

    // ---- Step 2: UTF-16 without BOM ----
    //
    // In UTF-16, characters are stored as 2-byte pairs. The "high byte" (page byte)
    // of each pair is the same for all characters in the same Unicode block.
    // e.g. ASCII in UTF-16 LE → odd bytes are all 0x00
    //      Cyrillic in UTF-16 LE → odd bytes are all 0x04
    //      Hiragana in UTF-16 LE → odd bytes are all 0x30
    //
    // Method A: null-byte position test (works for ASCII/Latin UTF-16)
    {
        int nullOdd = 0, nullEven = 0, nullTotal = 0;
        for (int i = 0; i < sz; ++i) {
            if (ub(i) == 0x00) {
                nullTotal++;
                if (i & 1) nullOdd++; else nullEven++;
            }
        }
        if (nullTotal * 4 >= sz) {
            if (nullOdd  > nullEven * 3) return QStringLiteral("UTF-16 LE");
            if (nullEven > nullOdd  * 3) return QStringLiteral("UTF-16 BE");
        }
    }
    // Method B: page-byte clustering (works for non-ASCII like Cyrillic)
    // Split bytes into even/odd lanes and see if one lane has >60% of a single value.
    {
        int freqEven[256] = {}, freqOdd[256] = {};
        int cntEven = 0, cntOdd = 0;
        for (int i = 0; i + 1 < sz; i += 2) {
            freqEven[ub(i)]++;   cntEven++;
            freqOdd[ub(i+1)]++;  cntOdd++;
        }
        if (cntOdd >= 8) {
            int maxOdd = 0, domOdd = 0, maxEven = 0, domEven = 0;
            for (int v = 0; v < 256; ++v) {
                if (freqOdd[v]  > maxOdd)  { maxOdd  = freqOdd[v];  domOdd  = v; }
                if (freqEven[v] > maxEven) { maxEven = freqEven[v]; domEven = v; }
            }
            // One lane concentrated >60%, other lane <30%, dominant byte != 0xFF (padding)
            bool oddPage  = (domOdd  != 0xFF) && (maxOdd  * 10 > cntOdd  * 6)
                         && (maxOdd  * 10 > maxEven * 20);
            bool evenPage = (domEven != 0xFF) && (maxEven * 10 > cntEven * 6)
                         && (maxEven * 10 > maxOdd  * 20);
            if (oddPage)  return QStringLiteral("UTF-16 LE");
            if (evenPage) return QStringLiteral("UTF-16 BE");
        }
    }

    // ---- Step 3: High byte count ----
    int highBytes = 0;
    for (int i = 0; i < sz; ++i)
        if (ub(i) > 0x7F) highBytes++;

    if (highBytes == 0)
        return QStringLiteral("ASCII");

    // ---- Step 4: UTF-8 validation ----
    {
        int valid = 0, invalid = 0;
        for (int i = 0; i < sz; ) {
            unsigned char b = ub(i);
            if (b < 0x80) { i++; continue; }
            int seqLen = 0;
            if      ((b & 0xE0) == 0xC0 && b >= 0xC2) seqLen = 2;
            else if ((b & 0xF0) == 0xE0)               seqLen = 3;
            else if ((b & 0xF8) == 0xF0 && b <= 0xF4)  seqLen = 4;
            else { invalid++; i++; continue; }
            if (i + seqLen > sz) { i++; continue; }
            bool ok = true;
            for (int j = 1; j < seqLen && ok; j++)
                if ((ub(i+j) & 0xC0) != 0x80) ok = false;
            if (ok) valid++; else invalid++;
            i += seqLen;
        }
        if (valid > 0 && invalid == 0)
            return QStringLiteral("UTF-8");
    }

    // ---- Step 5: Cyrillic single-byte encodings ----
    //
    // Byte range layout for each Cyrillic encoding:
    //   Range        Win-1251       KOI8-R/U      CP-866         Mac Cyrillic
    //   0x80-0x9F    specials       box-drawing   UPPERCASE А-Я  UPPERCASE А-Я
    //   0xA0-0xBF    specials       specials      LOWERCASE а-п   specials (†°etc)
    //   0xC0-0xDF    UPPERCASE А-Я  lowercase а-я box-drawing     punctuation/symbols
    //   0xE0-0xFF    lowercase а-я  UPPERCASE А-Я lowercase р-я   lowercase а-я + €
    {
        int c80 = 0, cA0 = 0, cC0 = 0, cE0 = 0;
        int cDF = 0, cFF = 0;
        for (int i = 0; i < sz; ++i) {
            unsigned char b = ub(i);
            if      (b >= 0x80 && b <= 0x9F) c80++;
            else if (b >= 0xA0 && b <= 0xBF) cA0++;
            else if (b >= 0xC0 && b <= 0xDF) cC0++;
            else if (b >= 0xE0 && b <= 0xFF) cE0++;
            if (b == 0xDF) cDF++;
            if (b == 0xFF) cFF++;
        }
        int cyrTotal = c80 + cA0 + cC0 + cE0;

        if (cyrTotal * 10 > sz) {
            // CP-866: uppercase in 0x80-0x9F AND lowercase in 0xA0-0xBF
            if (c80 > 0 && cA0 * 2 > c80)
                return QStringLiteral("CP-866");
            // CP-866 all-lowercase: lowercase split 0xA0-0xBF (а-п) + 0xE0-0xFF (р-я),
            // so cA0 ≈ cE0. In Win-1251/KOI8 cA0 ≈ 0.
            if (cA0 * 3 > (cE0 + c80) && cA0 > 5)
                return QStringLiteral("CP-866");
            // Mac Cyrillic: lowercase is mostly in 0xE0-0xFE and letter 'я' is 0xDF
            // (while in Win-1251 'я' is 0xFF). On real Russian prose this gives a clear
            // marker: DF appears, FF is often absent/rare, and 0xE0-0xFF dominates.
            if (cE0 * 10 > cyrTotal * 7 && cDF > cFF * 3 && cFF == 0)
                return QStringLiteral("Mac Cyrillic");
            // Fallback Mac signal for mixed text where 0xFF can appear (e.g. euro sign).
            if (cE0 * 10 > cyrTotal * 7 && cDF > cFF * 4 && c80 > 0)
                return QStringLiteral("Mac Cyrillic");
            // KOI8-R/U: lowercase in 0xC0-0xDF, uppercase in 0xE0-0xFF
            // Win-1251:  lowercase in 0xE0-0xFF, uppercase in 0xC0-0xDF
            if (cC0 > cE0)
                return QStringLiteral("KOI8-R");
            return QStringLiteral("Windows-1251");
        }
    }

    // ---- Step 6: Asian multi-byte encodings ----

    {
        struct Candidate { const char *name; const char *iconvName; };
        const Candidate candidates[] = {
            { "ISO-2022-JP", "ISO-2022-JP" },
            { "Shift-JIS",   "CP932" },
            { "EUC-JP",      "EUC-JP" },
            { "GB18030",     "GB18030" },
            { "GBK",         "GBK" },
            { "EUC-KR",      "EUC-KR" }
        };

        auto scoreDecoded = [](const QString &s) -> int {
            int cjk = 0, hira = 0, kata = 0, hangul = 0, repl = 0;
            for (const QChar &ch : s) {
                const uint u = ch.unicode();
                if (u == 0xFFFD) { repl++; continue; }
                if (u >= 0x4E00 && u <= 0x9FFF) cjk++;
                else if (u >= 0x3040 && u <= 0x309F) hira++;
                else if (u >= 0x30A0 && u <= 0x30FF) kata++;
                else if (u >= 0xAC00 && u <= 0xD7AF) hangul++;
            }
            return cjk * 2 + hira * 4 + kata * 3 + hangul * 4 - repl * 6;
        };

        auto iconvDecode = [](const QByteArray &input, const char *fromCodec) -> QString {
            iconv_t cd = iconv_open("UTF-8", fromCodec);
            if (cd == (iconv_t)-1)
                return QString();
            size_t inLeft = input.size();
            char *inBuf = const_cast<char *>(input.constData());
            QByteArray outBuf(input.size() * 4 + 16, '\0');
            size_t outLeft = outBuf.size();
            char *outPtr = outBuf.data();
            iconv(cd, &inBuf, &inLeft, &outPtr, &outLeft);
            iconv_close(cd);
            return QString::fromUtf8(outBuf.constData(), outBuf.size() - outLeft);
        };

        int bestScore = -999999;
        QString bestName;
        const QByteArray sample = data.left(sz);
        for (const Candidate &c : candidates) {
            const QString out = iconvDecode(sample, c.iconvName);
            if (out.isEmpty())
                continue;
            const int score = scoreDecoded(out);
            if (score > bestScore) {
                bestScore = score;
                bestName = QString::fromLatin1(c.name);
            }
        }
        if (bestScore >= 24)
            return bestName;
    }

    auto scoreShiftJIS = [&]() -> int {
        int valid = 0, invalid = 0;
        for (int i = 0; i < sz; ++i) {
            const unsigned char b = ub(i);
            if ((b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC)) {
                if (i + 1 >= sz) { invalid++; break; }
                const unsigned char t = ub(i + 1);
                if ((t >= 0x40 && t <= 0x7E) || (t >= 0x80 && t <= 0xFC)) {
                    valid++;
                    i++;
                } else {
                    invalid++;
                }
            } else if (b >= 0xA1 && b <= 0xDF) {
                // half-width katakana
                valid++;
            }
        }
        return valid * 3 - invalid * 4;
    };

    auto scoreEUCJP = [&]() -> int {
        int valid = 0, invalid = 0;
        for (int i = 0; i < sz; ++i) {
            const unsigned char b = ub(i);
            if (b == 0x8E) {
                if (i + 1 < sz && ub(i + 1) >= 0xA1 && ub(i + 1) <= 0xDF) {
                    valid++;
                    i++;
                } else {
                    invalid++;
                }
            } else if (b == 0x8F) {
                if (i + 2 < sz && ub(i + 1) >= 0xA1 && ub(i + 1) <= 0xFE
                        && ub(i + 2) >= 0xA1 && ub(i + 2) <= 0xFE) {
                    valid++;
                    i += 2;
                } else {
                    invalid++;
                }
            } else if (b >= 0xA1 && b <= 0xFE) {
                if (i + 1 < sz && ub(i + 1) >= 0xA1 && ub(i + 1) <= 0xFE) {
                    valid++;
                    i++;
                } else {
                    invalid++;
                }
            }
        }
        return valid * 3 - invalid * 4;
    };

    auto scoreEUCKR = [&]() -> int {
        int valid = 0, invalid = 0;
        for (int i = 0; i < sz; ++i) {
            const unsigned char b = ub(i);
            if (b >= 0x81 && b <= 0xFE) {
                if (i + 1 < sz && ub(i + 1) >= 0x41 && ub(i + 1) <= 0xFE && ub(i + 1) != 0x7F) {
                    valid++;
                    i++;
                } else {
                    invalid++;
                }
            }
        }
        return valid * 3 - invalid * 4;
    };

    auto scoreGBK = [&]() -> int {
        int valid = 0, invalid = 0;
        for (int i = 0; i < sz; ++i) {
            const unsigned char b = ub(i);
            if (b >= 0x81 && b <= 0xFE) {
                if (i + 1 < sz && ub(i + 1) >= 0x40 && ub(i + 1) <= 0xFE && ub(i + 1) != 0x7F) {
                    valid++;
                    i++;
                } else {
                    invalid++;
                }
            }
        }
        return valid * 3 - invalid * 4;
    };

    auto scoreGB18030 = [&]() -> int {
        int valid2 = 0, valid4 = 0, invalid = 0;
        for (int i = 0; i < sz; ++i) {
            const unsigned char b = ub(i);
            if (b >= 0x81 && b <= 0xFE) {
                if (i + 3 < sz && ub(i + 1) >= 0x30 && ub(i + 1) <= 0x39
                        && ub(i + 2) >= 0x81 && ub(i + 2) <= 0xFE
                        && ub(i + 3) >= 0x30 && ub(i + 3) <= 0x39) {
                    valid4++;
                    i += 3;
                } else if (i + 1 < sz && ub(i + 1) >= 0x40 && ub(i + 1) <= 0xFE && ub(i + 1) != 0x7F) {
                    valid2++;
                    i += 1;
                } else {
                    invalid++;
                }
            }
        }
        return valid2 * 2 + valid4 * 5 - invalid * 4;
    };

    auto scoreISO2022JP = [&]() -> int {
        int esc = 0;
        for (int i = 0; i + 2 < sz; ++i) {
            if (ub(i) != 0x1B)
                continue;
            const unsigned char b1 = ub(i + 1);
            const unsigned char b2 = ub(i + 2);
            // ESC $ @, ESC $ B, ESC ( B, ESC ( J, ESC ( I
            if ((b1 == 0x24 && (b2 == 0x40 || b2 == 0x42))
                || (b1 == 0x28 && (b2 == 0x42 || b2 == 0x4A || b2 == 0x49))) {
                esc++;
                continue;
            }
            // ESC $ ( D
            if (i + 3 < sz && b1 == 0x24 && b2 == 0x28 && ub(i + 3) == 0x44) {
                esc++;
                continue;
            }
        }
        return esc * 20;
    };

    struct ScoredEnc { const char *name; int score; };
    const ScoredEnc asianScores[] = {
        { "ISO-2022-JP", scoreISO2022JP() },
        { "Shift-JIS", scoreShiftJIS() },
        { "EUC-JP", scoreEUCJP() },
        { "GB18030", scoreGB18030() },
        { "GBK", scoreGBK() },
        { "EUC-KR", scoreEUCKR() }
    };

    const ScoredEnc *best = nullptr;
    for (const ScoredEnc &s : asianScores) {
        if (!best || s.score > best->score)
            best = &s;
    }
    if (best && best->score >= 18)
        return QString::fromLatin1(best->name);

    if (highBytes * 10 > sz)
        return QStringLiteral("ISO-8859-1");

    return QStringLiteral("ASCII");
}

#endif // ENCODINGDETECT_H

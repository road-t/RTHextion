#include <QIODevice>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QStringConverter>
#include <QDebug>
#include <algorithm>

#include "translationtable.h"

namespace {
QString normalizeImportEncodingName(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return trimmed;

    QString compact = trimmed.toUpper();
    compact.remove('-');
    compact.remove('_');
    compact.remove(' ');

    if (compact == QLatin1String("CP1251")
        || compact == QLatin1String("WINDOWS1251")
        || compact == QLatin1String("WIN1251"))
        return QStringLiteral("Windows-1251");
    if (compact == QLatin1String("CP866"))
        return QStringLiteral("CP-866");
    if (compact == QLatin1String("UTF16LE"))
        return QStringLiteral("UTF-16LE");
    if (compact == QLatin1String("UTF16BE"))
        return QStringLiteral("UTF-16BE");
    if (compact == QLatin1String("UTF8"))
        return QStringLiteral("UTF-8");

    return trimmed;
}

QString decodeWindows1251(const QByteArray &raw)
{
    static const ushort kCp1251Ext[64] = {
        0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
        0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
        0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0xFFFD, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
        0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
        0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
        0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
        0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457
    };

    QString out;
    out.reserve(raw.size());

    for (unsigned char b : raw) {
        if (b < 0x80) {
            out.append(QChar(b));
        } else if (b >= 0xC0) {
            out.append(QChar(0x0410 + (b - 0xC0))); // А..я
        } else if (b == 0xA8) {
            out.append(QChar(0x0401)); // Ё
        } else if (b == 0xB8) {
            out.append(QChar(0x0451)); // ё
        } else {
            out.append(QChar(kCp1251Ext[b - 0x80]));
        }
    }

    return out;
}

bool tryDecodeWithEncoding(const QByteArray &raw, const QString &encodingName, QString *outText)
{
    if (!outText)
        return false;

    const auto encoding = QStringConverter::encodingForName(encodingName.toUtf8());
    if (!encoding.has_value())
        return false;

    QStringDecoder decoder(*encoding);
    const QString decoded = decoder.decode(raw);
    if (decoder.hasError())
        return false;

    *outText = decoded;
    return true;
}

void removeDecodeEntriesForKey(QMap<QString, char> &decodeTable, char key)
{
    for (auto it = decodeTable.begin(); it != decodeTable.end(); ) {
        if (it.value() == key)
            it = decodeTable.erase(it);
        else
            ++it;
    }
}
}

TranslationTable::TranslationTable()
{
    reset();
}

TranslationTable::TranslationTable(QString fileName, const QString &textEncoding) : TranslationTable()
{
    loadFromFile(fileName, textEncoding);
}

QStringList TranslationTable::supportedImportEncodings()
{
    return {
        QStringLiteral("ASCII"),
        QStringLiteral("UTF-8"),
        QStringLiteral("UTF-16LE"),
        QStringLiteral("UTF-16BE"),
        QStringLiteral("Windows-1251"),
        QStringLiteral("CP-1251"),
        QStringLiteral("WIN-1251"),
        QStringLiteral("KOI8-R"),
        QStringLiteral("KOI8-U"),
        QStringLiteral("CP-866"),
        QStringLiteral("ISO-8859-1"),
        QStringLiteral("Windows-1252"),
        QStringLiteral("Shift-JIS"),
        QStringLiteral("EUC-JP"),
        QStringLiteral("ISO-2022-JP"),
        QStringLiteral("GB2312"),
        QStringLiteral("Big5"),
        QStringLiteral("EUC-KR"),
        QStringLiteral("Mac Cyrillic")
    };
}

QString TranslationTable::guessImportEncoding(const QByteArray &raw)
{
    if (raw.startsWith("\xEF\xBB\xBF"))
        return QStringLiteral("UTF-8");
    if (raw.startsWith("\xFF\xFE"))
        return QStringLiteral("UTF-16LE");
    if (raw.startsWith("\xFE\xFF"))
        return QStringLiteral("UTF-16BE");

    QStringDecoder utf8Decoder("UTF-8");
    utf8Decoder.decode(raw);
    if (!utf8Decoder.hasError())
        return QStringLiteral("UTF-8");

    return QStringLiteral("Windows-1251");
}

bool TranslationTable::hasNonAsciiValueBytes(const QByteArray &raw)
{
    int lineStart = 0;
    for (int i = 0; i <= raw.size(); ++i) {
        if (i == raw.size() || raw[i] == '\n') {
            const int lineLen = i - lineStart;
            if (lineLen > 0) {
                const QByteArray line = raw.mid(lineStart, lineLen);
                const int eqPos = line.indexOf('=');
                if (eqPos > 0) {
                    for (int j = eqPos + 1; j < line.size(); ++j) {
                        if (static_cast<unsigned char>(line[j]) > 0x7F)
                            return true;
                    }
                }
            }
            lineStart = i + 1;
        }
    }
    return false;
}

bool TranslationTable::loadFromFile(const QString &fileName, const QString &textEncoding)
{
    clearItems();
    reset();

    QFile inputFile(fileName);

    if (!inputFile.open(QIODevice::ReadOnly))
        return false;

    const QByteArray raw = inputFile.readAll();
    inputFile.close();

    const QString effectiveEncoding = textEncoding.trimmed().isEmpty()
            ? guessImportEncoding(raw)
            : textEncoding.trimmed();
    const QString normalizedEncoding = normalizeImportEncodingName(effectiveEncoding);

    QString content;
    if (normalizedEncoding == QLatin1String("ASCII")) {
        content = QString::fromLatin1(raw);
    } else if (normalizedEncoding == QLatin1String("Windows-1251")) {
        content = decodeWindows1251(raw);
    } else {
        if (!tryDecodeWithEncoding(raw, normalizedEncoding, &content)
            && !tryDecodeWithEncoding(raw, QStringLiteral("Windows-1251"), &content)) {
            content = decodeWindows1251(raw);
        }
    }

    const QStringList lines = content.split(QRegularExpression(QStringLiteral("\\r?\\n")), Qt::SkipEmptyParts);
    for (const QString &rawLine : lines)
    {
        auto eqPos = rawLine.indexOf('=');

        // skip lines without '=' or with nothing before it
        if (eqPos <= 0)
            continue;

        QString hexPart = rawLine.left(eqPos);
        auto value = rawLine.mid(eqPos + 1);

            // Validate: all hex chars
            bool allHex = true;
            for (int i = 0; i < hexPart.size(); ++i) {
                QChar ch = hexPart[i];
                if (!ch.isDigit() && !(ch >= 'A' && ch <= 'F') && !(ch >= 'a' && ch <= 'f')) {
                    allHex = false;
                    break;
                }
            }
        if (!allHex || hexPart.isEmpty())
            continue;

        if (hexPart.size() <= 2) {
            // Single-byte entry: XX=text
            bool success;
            auto val = hexPart.toUInt(&success, 16);
            if (success) {
                const uint8_t key = static_cast<uint8_t>(val);
                if (!encodeTable.contains(key))
                    setItem(key, value);
                else
                    addDecodeAlias(key, value);
            }
        } else {
            // Multi-byte entry: XXYY...=text (must be even number of hex digits)
            if (hexPart.size() % 2 != 0)
                continue;
            QByteArray key;
            bool ok = true;
            for (int i = 0; i < hexPart.size(); i += 2) {
                uint val = hexPart.mid(i, 2).toUInt(&ok, 16);
                if (!ok) break;
                key.append(static_cast<char>(val));
            }
            if (ok && !key.isEmpty()) {
                multiByteEncodeTable.insert(key, value);
                multiByteDecodeTable[value] = key;
                if (key.size() > _maxKeyLen)
                    _maxKeyLen = key.size();
            }
        }
    }

    buildFallbackDecodeEntries();
    return true;
}

void TranslationTable::buildFallbackDecodeEntries()
{
    for (uint16_t i = 0; i < 0x100; i++)
    {
        if (!encodeTable.contains(i))
        {
            auto btSequence = QString("{%1}").arg(i, 2, 16, QChar('0'));
            decodeTable[btSequence] = i;
            if (i > 0x9F || (i & 0xF) > 9)
                decodeTable[btSequence.toUpper()] = i;
        }
    }
}

uint32_t TranslationTable::size() const
{
    return encodeTable.size() + multiByteEncodeTable.size();
}

QByteArray TranslationTable::decode(QByteArray src)
{
    QByteArray result;
    QString text = QString(src);

    // Build multi-byte keys sorted by length descending (longest match first)
    QList<QString> mbKeys = multiByteDecodeTable.keys();
    std::sort(mbKeys.begin(), mbKeys.end(), [](const QString &a, const QString &b) {
        return a.size() > b.size();
    });

    // Build single-byte keys sorted by length descending (longest match first).
    // This includes {XX} fallback entries (4 chars) and 1-char table entries.
    QVector<QPair<QString, char>> sbEntries;
    sbEntries.reserve(decodeTable.size());
    for (auto it = decodeTable.begin(); it != decodeTable.end(); ++it)
        sbEntries.append({it.key(), static_cast<char>(it.value())});
    std::sort(sbEntries.begin(), sbEntries.end(), [](const auto &a, const auto &b) {
        return a.first.size() > b.first.size();
    });

    // Single-pass left-to-right scan to avoid cascading replacements
    int i = 0;
    while (i < text.size()) {
        bool matched = false;

        // Try multi-byte decode entries first (longest match)
        for (const auto &key : mbKeys) {
            if (i + key.size() <= text.size() && QStringView(text).mid(i, key.size()) == key) {
                result.append(multiByteDecodeTable[key]);
                i += key.size();
                matched = true;
                break;
            }
        }
        if (matched) continue;

        // Try single-byte decode entries (longest key first)
        for (const auto &entry : sbEntries) {
            if (i + entry.first.size() <= text.size()
                && QStringView(text).mid(i, entry.first.size()) == entry.first) {
                result.append(entry.second);
                i += entry.first.size();
                matched = true;
                break;
            }
        }
        if (matched) continue;

        // No match — pass through as Latin1
        result.append(text[i].toLatin1());
        ++i;
    }

    return result;
}

// WILL WORK ONLY FOR SYMBOLS
char TranslationTable::decodeSymbol(QString src) const
{
    return encodeTable.key(src, -1);
}

QByteArray TranslationTable::decodeToBytes(const QString &text) const
{
    // Check multi-byte decode table first
    auto it = multiByteDecodeTable.constFind(text);
    if (it != multiByteDecodeTable.constEnd())
        return it.value();

    // Fall back to single-byte
    for (auto it2 = encodeTable.constBegin(); it2 != encodeTable.constEnd(); ++it2) {
        if (it2.value() == text)
            return QByteArray(1, it2.key());

        const auto aliasIt = decodeAliases.constFind(it2.key());
        if (aliasIt != decodeAliases.constEnd() && aliasIt.value().contains(text))
            return QByteArray(1, it2.key());
    }

    return QByteArray(); // not found
}

QString TranslationTable::encode(QByteArray src, bool keepCodes)
{
    auto result = QString();

    int i = 0;
    while (i < src.size())
    {
        int consumed = 0;
        QString sym = encodeBytes(src, i, consumed, keepCodes);
        result += sym;
        i += consumed;
    }

    return result;
}

QString TranslationTable::encodeBytes(const QByteArray &src, int pos, int &bytesConsumed, bool keepCode) const
{
    // Try multi-byte sequences first (longest match)
    if (!multiByteEncodeTable.isEmpty()) {
        for (int len = qMin(_maxKeyLen, (int)(src.size() - pos)); len >= 2; --len) {
            QByteArray key = src.mid(pos, len);
            auto it = multiByteEncodeTable.find(key);
            if (it != multiByteEncodeTable.end()) {
                bytesConsumed = len;
                return it.value();
            }
        }
    }

    // Fall back to single-byte lookup
    bytesConsumed = 1;
    return encodeSymbol(src[pos], keepCode);
}

QString TranslationTable::encodeSymbol(const char symbol, bool keepCode) const
{
    if (encodeTable.contains(static_cast<uint8_t>(symbol)))
    {
        QString code = encodeTable.value(static_cast<uint8_t>(symbol));

        // Если code начинается с '\' и далее идет hex
        if (keepCode && code.length() > 1 && code[0] == '\\')
        {
            bool ok = false;

            int hexVal = code.mid(1).toInt(&ok, 16);
            if (ok && hexVal >= 0 && hexVal <= 0xFF)
                return QString(QChar(hexVal));
            else
                return code;
        }
        else
            return code;
    }
    else
        return keepCode ? charToHex(symbol) : "";
}

QString TranslationTable::escapeNonPrintable(QByteArray src)
{
    auto result = QString();

    for (auto i = 0; i < src.size(); i++)
    {
        auto ch = QChar(src[i]);
        result += ch.isPrint() ? ch : charToHex(src[i]);
    }

    return result;
}

QString TranslationTable::charToHex(const char symbol)
{
    return QString("{%1}").arg(QString::number(static_cast<uint8_t>(symbol), 16), 2, '0').toUpper();
}

QMap<char, QString> *TranslationTable::getItems()
{
    return &encodeTable;
}

QPair<char, QString> TranslationTable::item(QMap<char, QString>::iterator it)
{
    return QPair<int32_t, QString>(it.key(), it.value());
}

QPair<char, QString> TranslationTable::next()
{
    if (++it == encodeTable.end())
        it = encodeTable.begin();

    return item(it);
}

QPair<char, QString> TranslationTable::prev()
{
    if (--it == encodeTable.begin())
        it = encodeTable.end();

    return item(it);
}

void TranslationTable::reset()
{
    it = encodeTable.begin();
}

void TranslationTable::setItem(uint8_t key, const QString &value)
{
    removeDecodeEntriesForKey(decodeTable, static_cast<char>(key));
    encodeTable[key] = value;
    decodeTable[value] = key;

    auto &aliases = decodeAliases[static_cast<char>(key)];
    aliases.removeAll(value);
    for (const QString &alias : aliases)
        decodeTable[alias] = static_cast<char>(key);
}

void TranslationTable::addDecodeAlias(uint8_t key, const QString &value)
{
    if (value.isEmpty())
        return;

    if (!encodeTable.contains(key)) {
        setItem(key, value);
        return;
    }

    if (encodeTable.value(key) == value)
        return;

    auto &aliases = decodeAliases[static_cast<char>(key)];
    if (!aliases.contains(value))
        aliases.append(value);

    decodeTable[value] = static_cast<char>(key);
}

void TranslationTable::setMultiByteItem(const QByteArray &key, const QString &value)
{
    if (multiByteEncodeTable.contains(key))
        multiByteDecodeTable.remove(multiByteEncodeTable.value(key));

    multiByteEncodeTable[key] = value;
    multiByteDecodeTable[value] = key;
    if (key.size() > _maxKeyLen)
        _maxKeyLen = key.size();
}

void TranslationTable::removeItem(uint8_t key)
{
    if (encodeTable.contains(key))
    {
        removeDecodeEntriesForKey(decodeTable, static_cast<char>(key));
        decodeAliases.remove(static_cast<char>(key));
        encodeTable.remove(key);
    }
}

void TranslationTable::removeMultiByteItem(const QByteArray &key)
{
    if (multiByteEncodeTable.contains(key))
    {
        multiByteDecodeTable.remove(multiByteEncodeTable.value(key));
        multiByteEncodeTable.remove(key);
    }
}

void TranslationTable::clearItems()
{
    encodeTable.clear();
    decodeTable.clear();
    multiByteEncodeTable.clear();
    multiByteDecodeTable.clear();
    decodeAliases.clear();
    _maxKeyLen = 1;
}

const QStringList &TranslationTable::decodeAliasesForKey(uint8_t key) const
{
    static const QStringList kEmpty;
    auto it = decodeAliases.constFind(static_cast<char>(key));
    return (it == decodeAliases.constEnd()) ? kEmpty : it.value();
}

bool TranslationTable::save(const QString &fileName) const
{
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);

    // Save single-byte entries
    for (auto it = encodeTable.cbegin(); it != encodeTable.cend(); ++it)
    {
        out << QString("%1=%2").arg(
            QString::number(static_cast<uint8_t>(it.key()), 16).toUpper().rightJustified(2, '0'),
            it.value()) << "\n";

        const auto aliasIt = decodeAliases.constFind(it.key());
        if (aliasIt != decodeAliases.constEnd()) {
            for (const QString &alias : aliasIt.value()) {
                if (alias == it.value())
                    continue;
                out << QString("%1=%2").arg(
                    QString::number(static_cast<uint8_t>(it.key()), 16).toUpper().rightJustified(2, '0'),
                    alias) << "\n";
            }
        }
    }

    // Save multi-byte entries
    for (auto it = multiByteEncodeTable.cbegin(); it != multiByteEncodeTable.cend(); ++it)
    {
        QString hexKey;
        for (int i = 0; i < it.key().size(); ++i)
            hexKey += QString::number(static_cast<uint8_t>(it.key()[i]), 16).toUpper().rightJustified(2, '0');
        out << QString("%1=%2").arg(hexKey, it.value()) << "\n";
    }

    file.close();
    return true;
}

uint32_t TranslationTable::generateTable(QString input, QString value)
{
    static const auto ucRE = QRegularExpression("([A-Z]+)");
    static const auto lcRE = QRegularExpression("([a-z]+)");
    static const auto dRE = QRegularExpression("([0-9]+)");

    auto capitals = value.indexOf(ucRE, 0);
    auto lowerCased = value.indexOf(lcRE, 0);
    auto digits = value.indexOf(dRE, 0);

    encodeTable.clear();

    if (capitals != -1)
    {
        uint8_t capitalsDistanceToASCII = input[capitals].toLatin1() - value[capitals].toLatin1();

        for (uint8_t c = ('A' + capitalsDistanceToASCII); c <= 'Z' + capitalsDistanceToASCII; c++)
        {
            encodeTable.insert(c, QChar::fromLatin1(c - capitalsDistanceToASCII));
        }
    }

    if (lowerCased != -1)
    {
        int16_t lcDistanceToASCII = input[lowerCased].toLatin1() - value[lowerCased].toLatin1();

        for (char c = ('a' + lcDistanceToASCII); c <= 'z' + lcDistanceToASCII; c++)
        {
            encodeTable.insert(c, QChar::fromLatin1(c - lcDistanceToASCII));
        }
    }

    if (digits != -1)
    {
        int16_t digitsDistanceToASCII = input[digits].toLatin1() - value[digits].toLatin1();

        for (char c = ('0' + digitsDistanceToASCII); c <= '9' + digitsDistanceToASCII; c++)
        {
            encodeTable.insert(c, QChar::fromLatin1(c - digitsDistanceToASCII));
        }
    }

    return encodeTable.size();
}

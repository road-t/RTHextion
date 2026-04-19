#include "appsettings.h"

#include <QColor>
#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QKeySequence>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

// Forward declaration of free function used by both AppSettings and the YAML parser.
static QVariant yamlScalarToVariant(const QString &raw, bool wasQuoted);

static const QLatin1String kConfigFileName("settings.yaml");

// =========================================================================
//  Singleton
// =========================================================================

AppSettings &AppSettings::instance()
{
    static AppSettings s;
    return s;
}

AppSettings::AppSettings()
{
    // 1) If there is a config in the working directory, use it.
    const QString localPath = QDir::currentPath() + QLatin1Char('/') + kConfigFileName;
    if (QFile::exists(localPath)) {
        m_filePath = localPath;
        m_usingLocalConfig = true;
    } else {
        // 2) Otherwise use the system config directory.
#ifdef Q_OS_MACOS
        QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#else
        QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
#endif
        QDir().mkpath(configDir);
        m_filePath = configDir + QLatin1Char('/') + kConfigFileName;
    }

    if (QCoreApplication::instance()) {
        QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                         QCoreApplication::instance(), [this]() { sync(); }, Qt::DirectConnection);
    }

    load();
}

AppSettings::~AppSettings()
{
    if (QCoreApplication::closingDown())
        return;

    if (m_dirty)
        save();
}

// =========================================================================
//  Public API (QSettings-compatible)
// =========================================================================

QVariant AppSettings::value(const QString &key, const QVariant &defaultValue) const
{
    QMutexLocker lock(&m_mutex);
    const QString rk = resolvedKey(key);
    auto it = m_data.constFind(rk);
    return (it != m_data.constEnd()) ? it.value() : defaultValue;
}

void AppSettings::setValue(const QString &key, const QVariant &value)
{
    QMutexLocker lock(&m_mutex);
    m_data[resolvedKey(key)] = value;
    m_dirty = true;
}

void AppSettings::remove(const QString &key)
{
    QMutexLocker lock(&m_mutex);
    const QString rk = resolvedKey(key);
    m_data.remove(rk);
    // Also remove all sub-keys (behaves like QSettings group removal).
    const QString prefix = rk + QLatin1Char('/');
    for (auto it = m_data.begin(); it != m_data.end(); ) {
        if (it.key().startsWith(prefix))
            it = m_data.erase(it);
        else
            ++it;
    }
    m_dirty = true;
}

bool AppSettings::contains(const QString &key) const
{
    QMutexLocker lock(&m_mutex);
    return m_data.contains(resolvedKey(key));
}

QStringList AppSettings::allKeys() const
{
    QMutexLocker lock(&m_mutex);
    return m_data.keys();
}

QStringList AppSettings::childGroups() const
{
    QMutexLocker lock(&m_mutex);
    const QString pfx = m_groupStack.isEmpty()
                            ? QString()
                            : (m_groupStack.join(QLatin1Char('/')) + QLatin1Char('/'));
    QSet<QString> groups;
    for (auto it = m_data.constBegin(); it != m_data.constEnd(); ++it) {
        const QString &k = it.key();
        if (!pfx.isEmpty() && !k.startsWith(pfx))
            continue;
        const QStringView rest = QStringView(k).mid(pfx.size());
        const qsizetype slash = rest.indexOf(QLatin1Char('/'));
        if (slash > 0)
            groups.insert(rest.left(slash).toString());
    }
    QStringList result(groups.begin(), groups.end());
    result.sort();
    return result;
}

QStringList AppSettings::childKeys() const
{
    QMutexLocker lock(&m_mutex);
    const QString pfx = m_groupStack.isEmpty()
                            ? QString()
                            : (m_groupStack.join(QLatin1Char('/')) + QLatin1Char('/'));
    QStringList keys;
    for (auto it = m_data.constBegin(); it != m_data.constEnd(); ++it) {
        const QString &k = it.key();
        if (!pfx.isEmpty() && !k.startsWith(pfx))
            continue;
        const QStringView rest = QStringView(k).mid(pfx.size());
        if (!rest.contains(QLatin1Char('/')))
            keys.append(rest.toString());
    }
    keys.sort();
    return keys;
}

void AppSettings::beginGroup(const QString &prefix)
{
    m_groupStack.append(prefix);
}

void AppSettings::endGroup()
{
    if (!m_groupStack.isEmpty())
        m_groupStack.removeLast();
}

QString AppSettings::group() const
{
    return m_groupStack.join(QLatin1Char('/'));
}

void AppSettings::sync()
{
    QMutexLocker lock(&m_mutex);
    if (m_dirty)
        save();
}

QString AppSettings::filePath() const { return m_filePath; }
bool AppSettings::isUsingLocalConfig() const { return m_usingLocalConfig; }

// =========================================================================
//  Load / Save / Migrate
// =========================================================================

void AppSettings::load()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // No YAML file yet — try one-time migration from QSettings.
        migrateFromQSettings();
        return;
    }
    const QString text = QString::fromUtf8(file.readAll());
    file.close();

    QVariantMap tree = parseYaml(text);
    m_data.clear();
    flattenTree(tree, QString(), m_data);
}

void AppSettings::save() const
{
    const QVariantMap tree = buildTree(m_data);
    const QString yaml = QStringLiteral("# RTHextion settings — auto-generated, "
                                        "safe to edit by hand\n\n")
                         + emitYaml(tree);

    QSaveFile file(m_filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(yaml.toUtf8());
        file.commit();
    }
    m_dirty = false;
}

void AppSettings::migrateFromQSettings()
{
    QSettings old;
    const QStringList keys = old.allKeys();
    if (keys.isEmpty())
        return;

    for (const QString &key : keys)
        m_data[key] = old.value(key);

    m_dirty = true;
    save();
}

QString AppSettings::resolvedKey(const QString &key) const
{
    if (m_groupStack.isEmpty())
        return key;
    return m_groupStack.join(QLatin1Char('/')) + QLatin1Char('/') + key;
}

// =========================================================================
//  QVariant ↔ YAML scalar
// =========================================================================

static bool scalarNeedsQuoting(const QString &s)
{
    if (s.isEmpty())
        return true;
    if (s == QLatin1String("true") || s == QLatin1String("false") ||
        s == QLatin1String("null") || s == QLatin1String("~"))
        return true;
    bool ok;
    s.toLongLong(&ok);
    if (ok) return true;
    s.toDouble(&ok);
    if (ok) return true;
    static const QLatin1String special(":#[]{},%&*!|>'\"`@\n\r\t");
    for (QChar c : s)
        if (special.indexOf(c) >= 0)
            return true;
    return false;
}

static bool keyNeedsQuoting(const QString &s)
{
    if (s.isEmpty())
        return true;
    static const QLatin1String special(":#[]{},%&*!|>'\"`@\n\r\t ");
    for (QChar c : s)
        if (special.indexOf(c) >= 0)
            return true;
    return false;
}

static QString quoteYaml(const QString &s)
{
    QString escaped;
    escaped.reserve(s.size() + 8);
    escaped += QLatin1Char('"');
    for (QChar c : s) {
        if (c == QLatin1Char('\\'))      escaped += QLatin1String("\\\\");
        else if (c == QLatin1Char('"'))   escaped += QLatin1String("\\\"");
        else if (c == QLatin1Char('\n'))  escaped += QLatin1String("\\n");
        else if (c == QLatin1Char('\r'))  escaped += QLatin1String("\\r");
        else if (c == QLatin1Char('\t'))  escaped += QLatin1String("\\t");
        else                              escaped += c;
    }
    escaped += QLatin1Char('"');
    return escaped;
}

static QString unquoteYaml(const QString &s)
{
    if (s.size() < 2)
        return s;
    const QChar first = s.front(), last = s.back();
    if ((first == QLatin1Char('"') && last == QLatin1Char('"')) ||
        (first == QLatin1Char('\'') && last == QLatin1Char('\''))) {
        QString inner = s.mid(1, s.size() - 2);
        if (first == QLatin1Char('"')) {
            // Process escape sequences (order matters: \\\\ last)
            inner.replace(QLatin1String("\\n"), QLatin1String("\n"));
            inner.replace(QLatin1String("\\r"), QLatin1String("\r"));
            inner.replace(QLatin1String("\\t"), QLatin1String("\t"));
            inner.replace(QLatin1String("\\\""), QLatin1String("\""));
            inner.replace(QLatin1String("\\\\"), QLatin1String("\\"));
        }
        return inner;
    }
    return s;
}

static QString variantToYamlScalar(const QVariant &v)
{
    if (!v.isValid())
        return QStringLiteral("null");

    switch (v.typeId()) {
    case QMetaType::Bool:
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QMetaType::Int:
        return QString::number(v.toInt());
    case QMetaType::UInt:
        return QString::number(v.toUInt());
    case QMetaType::LongLong:
        return QString::number(v.toLongLong());
    case QMetaType::ULongLong:
        return QString::number(v.toULongLong());
    case QMetaType::Double:
    case QMetaType::Float:
        return QString::number(v.toDouble(), 'g', 15);
    case QMetaType::QByteArray:
        return quoteYaml(QStringLiteral("@ByteArray(") +
                         QString::fromLatin1(v.toByteArray().toBase64()) +
                         QLatin1Char(')'));
    case QMetaType::QColor:
        return quoteYaml(QStringLiteral("@Color(") +
                         v.value<QColor>().name(QColor::HexArgb) +
                         QLatin1Char(')'));
    case QMetaType::QFont:
        return quoteYaml(QStringLiteral("@Font(") +
                         v.value<QFont>().toString() +
                         QLatin1Char(')'));
    case QMetaType::QDateTime:
        return quoteYaml(QStringLiteral("@DateTime(") +
                         v.toDateTime().toString(Qt::ISODate) +
                         QLatin1Char(')'));
    case QMetaType::QKeySequence:
        return quoteYaml(QStringLiteral("@KeySequence(") +
                         v.value<QKeySequence>().toString() +
                         QLatin1Char(')'));
    case QMetaType::QStringList:
        // QStringList is handled at tree level; fallback here
        return quoteYaml(v.toStringList().join(QStringLiteral(", ")));
    default:
        break;
    }

    // QString scalar
    if (v.typeId() == QMetaType::QString) {
        const QString s = v.toString();
        return scalarNeedsQuoting(s) ? quoteYaml(s) : s;
    }

    // Ultimate fallback — binary-encode via QDataStream
    QByteArray ba;
    {
        QDataStream ds(&ba, QIODevice::WriteOnly);
        ds << v;
    }
    return quoteYaml(QStringLiteral("@Variant(") +
                     QString::fromLatin1(ba.toBase64()) +
                     QLatin1Char(')'));
}

static QVariant yamlScalarToVariant(const QString &raw, bool wasQuoted)
{
    if (!wasQuoted) {
        if (raw == QLatin1String("true"))  return QVariant(true);
        if (raw == QLatin1String("false")) return QVariant(false);
        if (raw == QLatin1String("null") || raw == QLatin1String("~"))
            return QVariant();

        bool ok;
        const qint64 ll = raw.toLongLong(&ok);
        if (ok) {
            if (ll >= INT_MIN && ll <= INT_MAX)
                return QVariant(static_cast<int>(ll));
            return QVariant(ll);
        }
        const double d = raw.toDouble(&ok);
        if (ok) return QVariant(d);
    }

    const QString s = wasQuoted ? unquoteYaml(raw) : raw;

    // Tagged Qt types: @Type(payload)
    if (s.startsWith(QLatin1String("@ByteArray(")) && s.endsWith(QLatin1Char(')')))
        return QVariant(QByteArray::fromBase64(s.mid(11, s.size() - 12).toLatin1()));

    if (s.startsWith(QLatin1String("@Color(")) && s.endsWith(QLatin1Char(')')))
        return QVariant::fromValue(QColor(s.mid(7, s.size() - 8)));

    if (s.startsWith(QLatin1String("@Font(")) && s.endsWith(QLatin1Char(')'))) {
        QFont f;
        f.fromString(s.mid(6, s.size() - 7));
        return QVariant::fromValue(f);
    }

    if (s.startsWith(QLatin1String("@DateTime(")) && s.endsWith(QLatin1Char(')')))
        return QVariant(QDateTime::fromString(s.mid(10, s.size() - 11), Qt::ISODate));

    if (s.startsWith(QLatin1String("@KeySequence(")) && s.endsWith(QLatin1Char(')')))
        return QVariant::fromValue(QKeySequence(s.mid(13, s.size() - 14)));

    if (s.startsWith(QLatin1String("@Variant(")) && s.endsWith(QLatin1Char(')'))) {
        QByteArray ba = QByteArray::fromBase64(s.mid(9, s.size() - 10).toLatin1());
        QDataStream ds(&ba, QIODevice::ReadOnly);
        QVariant v;
        ds >> v;
        return v;
    }

    return QVariant(s);
}

// =========================================================================
//  Flat map ↔ nested tree
// =========================================================================

static void setNestedValue(QVariantMap &root, const QStringList &path,
                           const QVariant &value)
{
    if (path.size() == 1) {
        root[path.first()] = value;
        return;
    }
    QVariantMap child;
    if (root.contains(path.first()) &&
        root[path.first()].typeId() == QMetaType::QVariantMap)
        child = root[path.first()].toMap();
    setNestedValue(child, path.mid(1), value);
    root[path.first()] = child;
}

QVariantMap AppSettings::buildTree(const QMap<QString, QVariant> &flat)
{
    QVariantMap root;
    for (auto it = flat.constBegin(); it != flat.constEnd(); ++it)
        setNestedValue(root, it.key().split(QLatin1Char('/')), it.value());
    return root;
}

void AppSettings::flattenTree(const QVariantMap &tree, const QString &prefix,
                              QMap<QString, QVariant> &out)
{
    for (auto it = tree.constBegin(); it != tree.constEnd(); ++it) {
        const QString key = prefix.isEmpty()
                                ? it.key()
                                : (prefix + QLatin1Char('/') + it.key());
        if (it.value().typeId() == QMetaType::QVariantMap)
            flattenTree(it.value().toMap(), key, out);
        else
            out[key] = it.value();
    }
}

// =========================================================================
//  YAML emitter
// =========================================================================

QString AppSettings::emitYaml(const QVariantMap &tree, int indent)
{
    QString result;
    const QString pad(indent, QLatin1Char(' '));

    for (auto it = tree.constBegin(); it != tree.constEnd(); ++it) {
        const QString key = keyNeedsQuoting(it.key())
                                ? quoteYaml(it.key()) : it.key();

        if (it.value().typeId() == QMetaType::QVariantMap) {
            result += pad + key + QStringLiteral(":\n");
            result += emitYaml(it.value().toMap(), indent + 2);
        } else if (it.value().typeId() == QMetaType::QStringList) {
            const QStringList list = it.value().toStringList();
            result += pad + key + QStringLiteral(":\n");
            for (const QString &item : list)
                result += pad + QStringLiteral("  - ") + quoteYaml(item) + QLatin1Char('\n');
        } else {
            result += pad + key + QStringLiteral(": ")
                    + variantToYamlScalar(it.value()) + QLatin1Char('\n');
        }
    }
    return result;
}

// =========================================================================
//  YAML parser  (covers the subset we emit: maps, scalars, sequences)
// =========================================================================

namespace {

struct YamlLine {
    int indent = 0;
    enum Type { Empty, KeyValue, MapKey, ListItem } type = Empty;
    QString key;       // for KeyValue / MapKey
    QString rawValue;  // for KeyValue / ListItem — includes quotes if present
    bool valueQuoted = false;
};

// Strip trailing comment (outside quotes) and return trimmed content.
static QString stripComment(const QString &line)
{
    bool inQuote = false;
    QChar qc;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line[i];
        if (inQuote) {
            if (c == QLatin1Char('\\')) { ++i; continue; }
            if (c == qc) inQuote = false;
        } else {
            if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
                inQuote = true; qc = c;
            } else if (c == QLatin1Char('#') &&
                       (i == 0 || line[i - 1] == QLatin1Char(' '))) {
                return line.left(i).trimmed();
            }
        }
    }
    return line.trimmed();
}

// Find the first unquoted colon followed by space or end-of-string.
static int findColon(const QString &line)
{
    bool inQuote = false;
    QChar qc;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line[i];
        if (inQuote) {
            if (c == QLatin1Char('\\')) { ++i; continue; }
            if (c == qc) inQuote = false;
        } else {
            if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
                inQuote = true; qc = c;
            } else if (c == QLatin1Char(':')) {
                if (i + 1 >= line.size() || line[i + 1] == QLatin1Char(' '))
                    return i;
            }
        }
    }
    return -1;
}

static QVector<YamlLine> tokeniseYaml(const QString &text)
{
    QVector<YamlLine> out;
    const QStringList rawLines = text.split(QLatin1Char('\n'));
    for (const QString &raw : rawLines) {
        YamlLine yl;

        // Calculate indent (number of leading spaces).
        int i = 0;
        while (i < raw.size() && raw[i] == QLatin1Char(' '))
            ++i;
        yl.indent = i;

        QString body = stripComment(raw.mid(i));
        if (body.isEmpty()) {
            out.append(yl);        // Empty
            continue;
        }

        // List item: "- <value>"
        if (body.startsWith(QLatin1String("- "))) {
            yl.type = YamlLine::ListItem;
            yl.rawValue = body.mid(2).trimmed();
            yl.valueQuoted = !yl.rawValue.isEmpty() &&
                (yl.rawValue.front() == QLatin1Char('"') ||
                 yl.rawValue.front() == QLatin1Char('\''));
            out.append(yl);
            continue;
        }

        // Key: extract key, detect "key: value" vs "key:"
        const int colon = findColon(body);
        if (colon < 0) {
            // Bare scalar — treat as a list item with no dash (shouldn't happen
            // in well-formed YAML from our emitter, but handle gracefully).
            yl.type = YamlLine::ListItem;
            yl.rawValue = body;
            out.append(yl);
            continue;
        }

        QString rawKey = body.left(colon).trimmed();
        yl.key = (rawKey.startsWith(QLatin1Char('"')) || rawKey.startsWith(QLatin1Char('\'')))
                     ? unquoteYaml(rawKey) : rawKey;

        const QString afterColon = body.mid(colon + 1).trimmed();
        if (afterColon.isEmpty()) {
            yl.type = YamlLine::MapKey;
        } else {
            yl.type = YamlLine::KeyValue;
            yl.rawValue = afterColon;
            yl.valueQuoted = afterColon.front() == QLatin1Char('"') ||
                             afterColon.front() == QLatin1Char('\'');
        }
        out.append(yl);
    }
    return out;
}

// Recursive-descent parser over tokenised lines.
static QVariantMap parseBlock(const QVector<YamlLine> &lines, int &pos, int parentIndent);

static QVariantMap parseBlock(const QVector<YamlLine> &lines, int &pos, int parentIndent)
{
    QVariantMap result;
    while (pos < lines.size()) {
        const YamlLine &ln = lines[pos];

        // Skip empty lines.
        if (ln.type == YamlLine::Empty) { ++pos; continue; }

        // Back to or above parent indent → done.
        if (ln.indent <= parentIndent)
            break;

        if (ln.type == YamlLine::KeyValue) {
            result[ln.key] = yamlScalarToVariant(ln.rawValue, ln.valueQuoted);
            ++pos;
        } else if (ln.type == YamlLine::MapKey) {
            const int keyIndent = ln.indent;
            const QString key = ln.key;
            ++pos;

            // Peek ahead past empty lines to see what follows.
            int peek = pos;
            while (peek < lines.size() && lines[peek].type == YamlLine::Empty)
                ++peek;

            if (peek >= lines.size() || lines[peek].indent <= keyIndent) {
                // Nothing nested — store null.
                result[key] = QVariant();
            } else if (lines[peek].type == YamlLine::ListItem) {
                // Collect sequence.
                QStringList list;
                while (pos < lines.size()) {
                    const YamlLine &l = lines[pos];
                    if (l.type == YamlLine::Empty) { ++pos; continue; }
                    if (l.indent <= keyIndent) break;
                    if (l.type == YamlLine::ListItem) {
                        list.append(l.valueQuoted ? unquoteYaml(l.rawValue)
                                                  : l.rawValue);
                        ++pos;
                    } else {
                        break;
                    }
                }
                result[key] = list;
            } else {
                // Nested map.
                result[key] = parseBlock(lines, pos, keyIndent);
            }
        } else {
            // Unexpected list item at map level — skip.
            ++pos;
        }
    }
    return result;
}

} // anonymous namespace

QVariantMap AppSettings::parseYaml(const QString &text)
{
    QVector<YamlLine> lines = tokeniseYaml(text);
    int pos = 0;
    return parseBlock(lines, pos, -1);
}

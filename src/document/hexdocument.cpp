#include "hexdocument.h"
#include "PointerListModel.h"
#include "translationtable.h"
#include "romdetect.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

HexDocument::HexDocument() = default;

HexDocument::~HexDocument()
{
    delete translationTable;
    translationTable = nullptr;
    for (auto &entry : tables)
        delete entry.table;
    tables.clear();
}

// ---------------------------------------------------------------------------
// Pointer snapshot helpers
// ---------------------------------------------------------------------------

void HexDocument::snapshotPointers(PointerListModel *model)
{
    pointerSnapshot.clear();
    if (!model)
        return;

    const QList<qint64> keys = model->pointerKeys();
    pointerSnapshot.reserve(keys.size());
    for (qint64 ptrOfs : keys) {
        const qint64 target = model->getOffset(ptrOfs);
        const int size = model->getPointerSize(ptrOfs);
        pointerSnapshot.append({ptrOfs, PointerListModel::encodePtrValue(target, size)});
    }
}

void HexDocument::restorePointers(PointerListModel *model) const
{
    if (!model)
        return;
    model->clear();
    if (!pointerSnapshot.isEmpty())
        model->addPointersBatch(pointerSnapshot);
}

// ---------------------------------------------------------------------------
// YAML helpers
// ---------------------------------------------------------------------------

QString HexDocument::yamlEscape(const QString &s)
{
    if (s.isEmpty())
        return QStringLiteral("\"\"");
    // If the string contains special chars, quote it
    bool needsQuote = false;
    for (const QChar &ch : s) {
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\\') ||
            ch == QLatin1Char(':') || ch == QLatin1Char('#') ||
            ch == QLatin1Char('\n') || ch == QLatin1Char('\r')) {
            needsQuote = true;
            break;
        }
    }
    if (!needsQuote && (s.startsWith(QLatin1Char(' ')) || s.endsWith(QLatin1Char(' '))))
        needsQuote = true;

    if (!needsQuote)
        return s;

    QString out;
    out.reserve(s.size() + 4);
    out += QLatin1Char('"');
    for (const QChar &ch : s) {
        if (ch == QLatin1Char('"'))
            out += QLatin1String("\\\"");
        else if (ch == QLatin1Char('\\'))
            out += QLatin1String("\\\\");
        else if (ch == QLatin1Char('\n'))
            out += QLatin1String("\\n");
        else if (ch == QLatin1Char('\r'))
            out += QLatin1String("\\r");
        else
            out += ch;
    }
    out += QLatin1Char('"');
    return out;
}

QString HexDocument::yamlUnescape(const QString &s)
{
    if (s.isEmpty())
        return s;

    QString v = s;
    // Strip surrounding quotes if present
    if (v.size() >= 2 && v.startsWith(QLatin1Char('"')) && v.endsWith(QLatin1Char('"'))) {
        v = v.mid(1, v.size() - 2);
        // Process escape sequences
        QString out;
        out.reserve(v.size());
        for (int i = 0; i < v.size(); ++i) {
            if (v[i] == QLatin1Char('\\') && i + 1 < v.size()) {
                QChar next = v[i + 1];
                if (next == QLatin1Char('"'))       { out += QLatin1Char('"'); ++i; }
                else if (next == QLatin1Char('\\')) { out += QLatin1Char('\\'); ++i; }
                else if (next == QLatin1Char('n'))  { out += QLatin1Char('\n'); ++i; }
                else if (next == QLatin1Char('r'))  { out += QLatin1Char('\r'); ++i; }
                else out += v[i];
            } else {
                out += v[i];
            }
        }
        return out;
    }
    return v;
}

// ---------------------------------------------------------------------------
// Save project (.rthp)
// ---------------------------------------------------------------------------

// Helper: write table entries to stream
static void writeTableEntries(QTextStream &out, const TranslationTable *table,
                               const QString &indent = QStringLiteral("  "))
{
    if (!table || table->size() == 0) return;

    // Single-byte entries
    const auto *items = const_cast<TranslationTable *>(table)->getItems();
    for (auto it = items->cbegin(); it != items->cend(); ++it) {
        out << indent << "- "
            << QString::number(static_cast<uint8_t>(it.key()), 16).toUpper().rightJustified(2, '0')
            << "=" << it.value() << "\n";
    }
    // Multi-byte entries
    const auto &mbItems = table->getMultiByteItems();
    for (auto it = mbItems.cbegin(); it != mbItems.cend(); ++it) {
        QString hexKey;
        for (int i = 0; i < it.key().size(); ++i)
            hexKey += QString::number(static_cast<uint8_t>(it.key()[i]), 16).toUpper().rightJustified(2, '0');
        out << indent << "- " << hexKey << "=" << it.value() << "\n";
    }
}

bool HexDocument::saveProject(const QString &path,
                               const QVector<DocTableEntry> &tablesVec,
                               int activeIdx)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    const QDir projectDir = QFileInfo(path).absoluteDir();

    QTextStream out(&f);
    out << "# RTHextion project file\n";
    out << "version: 3\n\n";

    // Project name
    if (!projectName.isEmpty())
        out << "title: " << yamlEscape(projectName) << "\n";

    // File path
    if (!filePath.isEmpty()) {
        const QString rel = projectDir.relativeFilePath(filePath);
        out << "file: " << yamlEscape(rel) << "\n";
    }

    // Multi-table section
    if (!tablesVec.isEmpty()) {
        out << "\ntables:\n";
        for (int i = 0; i < tablesVec.size(); ++i) {
            const auto &entry = tablesVec[i];
            out << "  - name: " << yamlEscape(entry.name) << "\n";
            out << "    original: " << (entry.isOriginal ? "true" : "false") << "\n";
            if (entry.table && entry.table->size() > 0) {
                out << "    entries:\n";
                writeTableEntries(out, entry.table, QStringLiteral("      "));
            }
        }
        out << "active_table: " << activeIdx << "\n";
    }

    // Encoding
    out << "\nencoding: " << yamlEscape(currentEncoding) << "\n";

    // ROM type & byte order
    out << "rom_type: " << romTypeName(romType) << "\n";
    switch (byteOrder) {
    case ByteOrder::BigEndian:    out << "byte_order: BE\n"; break;
    case ByteOrder::SwappedBytes: out << "byte_order: BS\n"; break;
    default:                      out << "byte_order: LE\n"; break;
    }

    // Pointers
    if (!pointerSnapshot.isEmpty()) {
        out << "\npointers:\n";
        for (const auto &p : pointerSnapshot) {
            out << "  - offset: 0x" << QString::number(p.first, 16).toUpper() << "\n";
            out << "    target: 0x" << QString::number(PointerListModel::decodePtrTarget(p.second), 16).toUpper() << "\n";
            out << "    size: " << PointerListModel::decodePtrSize(p.second) << "\n";
        }
    }

    // Navigation history
    if (!navigationHistory.isEmpty()) {
        out << "\nnavigation_history:\n";
        for (qint64 pos : navigationHistory)
            out << "  - 0x" << QString::number(pos, 16).toUpper() << "\n";
        out << "navigation_index: " << navigationHistoryIndex << "\n";
    }

    // Cursor position
    out << "\ncursor_position: 0x" << QString::number(cursorPosition, 16).toUpper() << "\n";

    // Display settings
    out << "show_pointers: " << (showPointers ? "true" : "false") << "\n";
    out << "show_changes: "  << (showChanges  ? "true" : "false") << "\n";
    out << "changes_hex_mode: " << (changesHexMode ? "true" : "false") << "\n";

    // Original bytes
    if (!originalBytes.isEmpty()) {
        out << "\noriginal:\n";
        for (const auto &entry : originalBytes) {
            out << "  - " << QString::number(entry.first, 16).toUpper()
                << ": " << entry.second.toHex().toUpper() << "\n";
        }
    }

    f.close();
    projectFilePath = path;
    return true;
}

bool HexDocument::saveProject(const QString &path, const TranslationTable *table)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    const QDir projectDir = QFileInfo(path).absoluteDir();

    QTextStream out(&f);
    out << "# RTHextion project file\n";
    out << "version: 2\n\n";

    // Project name
    if (!projectName.isEmpty())
        out << "title: " << yamlEscape(projectName) << "\n";

    // File path — store relative to the project file when possible
    if (!filePath.isEmpty()) {
        const QString rel = projectDir.relativeFilePath(filePath);
        out << "file: " << yamlEscape(rel) << "\n";
    }

    // Translation table path (as reference)
    if (!tableFilePath.isEmpty()) {
        const QString rel = projectDir.relativeFilePath(tableFilePath);
        out << "table: " << yamlEscape(rel) << "\n";
    }
    out << "use_table: " << (useTable ? "true" : "false") << "\n";

    // Embedded table entries (self-contained — .tbl format inline)
    if (table && table->size() > 0) {
        out << "\ntable_entries:\n";
        // Single-byte entries
        const auto *items = const_cast<TranslationTable *>(table)->getItems();
        for (auto it = items->cbegin(); it != items->cend(); ++it) {
            out << "  - "
                << QString::number(static_cast<uint8_t>(it.key()), 16).toUpper().rightJustified(2, '0')
                << "=" << it.value() << "\n";
        }
        // Multi-byte entries
        const auto &mbItems = table->getMultiByteItems();
        for (auto it = mbItems.cbegin(); it != mbItems.cend(); ++it) {
            QString hexKey;
            for (int i = 0; i < it.key().size(); ++i)
                hexKey += QString::number(static_cast<uint8_t>(it.key()[i]), 16).toUpper().rightJustified(2, '0');
            out << "  - " << hexKey << "=" << it.value() << "\n";
        }
    }

    // Encoding
    out << "\nencoding: " << yamlEscape(currentEncoding) << "\n";

    // ROM type & byte order
    out << "rom_type: " << romTypeName(romType) << "\n";
    switch (byteOrder) {
    case ByteOrder::BigEndian:    out << "byte_order: BE\n"; break;
    case ByteOrder::SwappedBytes: out << "byte_order: BS\n"; break;
    default:                      out << "byte_order: LE\n"; break;
    }

    // Pointers
    if (!pointerSnapshot.isEmpty()) {
        out << "\npointers:\n";
        for (const auto &p : pointerSnapshot) {
            out << "  - offset: 0x" << QString::number(p.first, 16).toUpper() << "\n";
            out << "    target: 0x" << QString::number(PointerListModel::decodePtrTarget(p.second), 16).toUpper() << "\n";
            out << "    size: " << PointerListModel::decodePtrSize(p.second) << "\n";
        }
    }

    // Navigation history
    if (!navigationHistory.isEmpty()) {
        out << "\nnavigation_history:\n";
        for (qint64 pos : navigationHistory)
            out << "  - 0x" << QString::number(pos, 16).toUpper() << "\n";
        out << "navigation_index: " << navigationHistoryIndex << "\n";
    }

    // Cursor position
    out << "\ncursor_position: 0x" << QString::number(cursorPosition, 16).toUpper() << "\n";

    // Display settings
    out << "show_pointers: " << (showPointers ? "true" : "false") << "\n";
    out << "show_changes: "  << (showChanges  ? "true" : "false") << "\n";
    out << "changes_hex_mode: " << (changesHexMode ? "true" : "false") << "\n";

    // Original bytes (pre-modification data for IPS/diff)
    if (!originalBytes.isEmpty()) {
        out << "\noriginal:\n";
        for (const auto &entry : originalBytes) {
            out << "  - " << QString::number(entry.first, 16).toUpper()
                << ": " << entry.second.toHex().toUpper() << "\n";
        }
    }

    f.close();
    projectFilePath = path;
    return true;
}

// ---------------------------------------------------------------------------
// Load project (.rthp)
// ---------------------------------------------------------------------------

bool HexDocument::loadProject(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    const QDir projectDir = QFileInfo(path).absoluteDir();

    // Reset state
    filePath.clear();
    tableFilePath.clear();
    projectName.clear();
    delete translationTable;
    translationTable = nullptr;
    useTable = false;
    for (auto &entry : tables)
        delete entry.table;
    tables.clear();
    activeTableIndex = -1;
    currentEncoding = QStringLiteral("ASCII");
    romType = RomType::Unknown;
    byteOrder = ByteOrder::LittleEndian;
    pointerSnapshot.clear();
    navigationHistory.clear();
    navigationHistoryIndex = -1;
    cursorPosition = 0;
    changesHexMode = false;
    originalBytes.clear();

    enum class Section { Root, Pointers, NavHistory, TableEntries, Original, Tables, TablesEntries };
    Section section = Section::Root;

    // Temp for building a pointer entry
    qint64 ptrOffset = -1;
    qint64 ptrTarget = -1;
    int    ptrSize   = 4;

    // Temp for table entries
    bool hasEmbeddedTable = false;

    auto flushPointer = [&]() {
        if (ptrOffset >= 0 && ptrTarget >= 0) {
            pointerSnapshot.append({ptrOffset, PointerListModel::encodePtrValue(ptrTarget, ptrSize)});
        }
        ptrOffset = -1;
        ptrTarget = -1;
        ptrSize = 4;
    };

    auto switchSection = [&](Section newSection) {
        if (section == Section::Pointers)
            flushPointer();
        section = newSection;
    };

    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine();

        // Skip comments and empty lines
        const QString stripped = line.trimmed();
        if (stripped.isEmpty() || stripped.startsWith(QLatin1Char('#')))
            continue;

        // Detect section headers
        if (stripped == QLatin1String("pointers:")) {
            switchSection(Section::Pointers);
            continue;
        }
        if (stripped == QLatin1String("navigation_history:")) {
            switchSection(Section::NavHistory);
            continue;
        }
        if (stripped == QLatin1String("table_entries:")) {
            switchSection(Section::TableEntries);
            hasEmbeddedTable = true;
            continue;
        }
        if (stripped == QLatin1String("original:")) {
            switchSection(Section::Original);
            continue;
        }
        if (stripped == QLatin1String("tables:")) {
            switchSection(Section::Tables);
            continue;
        }

        // --- Tables section (v3 multi-table): parse "  - name: ..." and sub-entries ---
        if (section == Section::Tables || section == Section::TablesEntries) {
            // Detect "  - name: <tableName>" which starts a new table
            if (stripped.startsWith(QLatin1String("- name:"))) {
                QString tName = stripped.mid(7).trimmed();
                tName = yamlUnescape(tName);
                DocTableEntry dte;
                dte.name = tName;
                dte.table = new TranslationTable();
                tables.append(dte);
                section = Section::Tables;
                continue;
            }
            // Detect "    entries:" sub-header within a table
            if (stripped == QLatin1String("entries:")) {
                section = Section::TablesEntries;
                continue;
            }
            // Parse "    original: true/false"
            if (stripped.startsWith(QLatin1String("original:")) && !tables.isEmpty()) {
                const QString val = stripped.mid(9).trimmed();
                tables.last().isOriginal = (val == QLatin1String("true"));
                continue;
            }
            // Parse entry lines "      - HH=text"
            if (section == Section::TablesEntries && stripped.startsWith(QLatin1String("- "))) {
                if (tables.isEmpty()) continue;
                TranslationTable *t = tables.last().table;
                const int markerPos = line.indexOf(QLatin1String("- "));
                if (markerPos < 0) continue;
                QString entry = line.mid(markerPos + 2);
                int eqPos = entry.indexOf(QLatin1Char('='));
                if (eqPos <= 0) continue;

                QString hexPart = entry.left(eqPos);
                QString value = entry.mid(eqPos + 1);

                bool allHex = true;
                for (int i = 0; i < hexPart.size(); ++i) {
                    QChar ch = hexPart[i];
                    if (!ch.isDigit() && !(ch >= 'A' && ch <= 'F') && !(ch >= 'a' && ch <= 'f')) {
                        allHex = false;
                        break;
                    }
                }
                if (!allHex || hexPart.isEmpty()) continue;

                if (hexPart.size() <= 2) {
                    bool ok;
                    uint val = hexPart.toUInt(&ok, 16);
                    if (ok) t->setItem(static_cast<uint8_t>(val), value);
                } else if (hexPart.size() % 2 == 0) {
                    QByteArray key;
                    bool ok = true;
                    for (int i = 0; i < hexPart.size(); i += 2) {
                        uint val = hexPart.mid(i, 2).toUInt(&ok, 16);
                        if (!ok) break;
                        key.append(static_cast<char>(val));
                    }
                    if (ok && !key.isEmpty())
                        t->setMultiByteItem(key, value);
                }
                continue;
            }
            // Any non-list line exits the tables section
            if (!stripped.startsWith(QLatin1Char('-')) && !stripped.startsWith(QLatin1String("entries"))) {
                section = Section::Root;
                // Fall through to root parsing below
            } else {
                continue;
            }
        }

        // --- TableEntries section: each line is "  - HH=text" (TBL format) ---
        if (section == Section::TableEntries) {
            if (!stripped.startsWith(QLatin1String("- "))) {
                section = Section::Root;
            } else {
            const int markerPos = line.indexOf(QLatin1String("- "));
            if (markerPos < 0)
                continue;
            QString entry = line.mid(markerPos + 2);

            int eqPos = entry.indexOf(QLatin1Char('='));
            if (eqPos <= 0)
                continue; // not a valid table entry

            QString hexPart = entry.left(eqPos);
            QString value = entry.mid(eqPos + 1);

            // Validate hex
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

            if (!translationTable)
                translationTable = new TranslationTable();

            if (hexPart.size() <= 2) {
                bool ok;
                uint val = hexPart.toUInt(&ok, 16);
                if (ok)
                    translationTable->setItem(static_cast<uint8_t>(val), value);
            } else if (hexPart.size() % 2 == 0) {
                QByteArray key;
                bool ok = true;
                for (int i = 0; i < hexPart.size(); i += 2) {
                    uint val = hexPart.mid(i, 2).toUInt(&ok, 16);
                    if (!ok) break;
                    key.append(static_cast<char>(val));
                }
                if (ok && !key.isEmpty())
                    translationTable->setMultiByteItem(key, value);
            }
            continue;
            }
        }

        // --- Original section: each line is "  - HH: HHHH..." ---
        if (section == Section::Original) {
            if (!stripped.startsWith(QLatin1String("- "))) {
                section = Section::Root;
            } else {
            QString entry = stripped;
            if (entry.startsWith(QLatin1String("- ")))
                entry = entry.mid(2);

            int colonPos = entry.indexOf(QLatin1Char(':'));
            if (colonPos <= 0)
                continue;

            QString offsetHex = entry.left(colonPos).trimmed();
            QString bytesHex = entry.mid(colonPos + 1).trimmed();

            bool ok = false;
            qint64 offset = offsetHex.toLongLong(&ok, 16);
            if (!ok)
                continue;

            QByteArray bytes = QByteArray::fromHex(bytesHex.toLatin1());
            if (!bytes.isEmpty())
                originalBytes.append({offset, bytes});
            continue;
            }
        }

        // Handle NavHistory list items (format "- 0xHEX" has no colon)
        if (section == Section::NavHistory && stripped.startsWith(QLatin1String("- "))) {
            QString navVal = stripped.mid(2).trimmed();
            bool ok = false;
            qint64 pos = navVal.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)
                ? navVal.mid(2).toLongLong(&ok, 16) : navVal.toLongLong(&ok);
            if (ok)
                navigationHistory.append(pos);
            continue;
        }

        // Parse key: value
        int colonPos = stripped.indexOf(QLatin1Char(':'));
        if (colonPos < 0)
            continue;

        QString key = stripped.left(colonPos).trimmed();
        QString val = stripped.mid(colonPos + 1).trimmed();

        // Remove list marker "- " from key if present
        if (key.startsWith(QLatin1String("- ")))
            key = key.mid(2).trimmed();

        if (section == Section::Pointers) {
            if (key == QLatin1String("- offset") || key == QLatin1String("offset")) {
                if (key == QLatin1String("- offset"))
                    flushPointer();
                else if (ptrOffset >= 0)
                    flushPointer();
                bool ok = false;
                ptrOffset = val.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)
                    ? val.mid(2).toLongLong(&ok, 16) : val.toLongLong(&ok);
            } else if (key == QLatin1String("target")) {
                bool ok = false;
                ptrTarget = val.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)
                    ? val.mid(2).toLongLong(&ok, 16) : val.toLongLong(&ok);
            } else if (key == QLatin1String("size")) {
                ptrSize = val.toInt();
                if (ptrSize < 2 || ptrSize > 4) ptrSize = 4;
            }
            continue;
        }

        if (section == Section::NavHistory) {
            if (key == QLatin1String("navigation_index")) {
                navigationHistoryIndex = val.toInt();
            } else {
                section = Section::Root;
            }
            if (section == Section::NavHistory)
                continue;
        }

        // Root section
        if (key == QLatin1String("title")) {
            projectName = yamlUnescape(val);
        } else if (key == QLatin1String("file")) {
            const QString rel = yamlUnescape(val);
            filePath = QFileInfo(projectDir.filePath(rel)).canonicalFilePath();
            if (filePath.isEmpty())
                filePath = projectDir.filePath(rel);
        } else if (key == QLatin1String("table")) {
            const QString rel = yamlUnescape(val);
            tableFilePath = QFileInfo(projectDir.filePath(rel)).canonicalFilePath();
            if (tableFilePath.isEmpty())
                tableFilePath = projectDir.filePath(rel);
        } else if (key == QLatin1String("use_table")) {
            useTable = (val == QLatin1String("true"));
        } else if (key == QLatin1String("encoding")) {
            currentEncoding = yamlUnescape(val);
        } else if (key == QLatin1String("rom_type")) {
            // Support both legacy integer and mnemonic string
            bool ok = false;
            const int rt = val.toInt(&ok);
            if (ok) {
                if (rt >= 0 && rt < kRomTypeCount)
                    romType = static_cast<RomType>(rt);
            } else {
                // Search by mnemonic
                for (int i = 0; i < kRomTypeCount; ++i) {
                    if (romTypeName(static_cast<RomType>(i)) == val) {
                        romType = static_cast<RomType>(i);
                        break;
                    }
                }
            }
        } else if (key == QLatin1String("byte_order")) {
            // Support both legacy integer and LE/BE/BS string
            if (val == QLatin1String("BE"))
                byteOrder = ByteOrder::BigEndian;
            else if (val == QLatin1String("BS"))
                byteOrder = ByteOrder::SwappedBytes;
            else if (val == QLatin1String("LE"))
                byteOrder = ByteOrder::LittleEndian;
            else {
                const int bo = val.toInt();
                if (bo >= 0 && bo <= 2)
                    byteOrder = static_cast<ByteOrder>(bo);
            }
        } else if (key == QLatin1String("navigation_index")) {
            navigationHistoryIndex = val.toInt();
        } else if (key == QLatin1String("active_table")) {
            activeTableIndex = val.toInt();
        } else if (key == QLatin1String("cursor_position")) {
            bool ok = false;
            cursorPosition = val.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)
                ? val.mid(2).toLongLong(&ok, 16) : val.toLongLong(&ok);
        } else if (key == QLatin1String("show_pointers")) {
            showPointers = (val == QLatin1String("true"));
        } else if (key == QLatin1String("show_changes")) {
            showChanges = (val == QLatin1String("true"));
        } else if (key == QLatin1String("changes_hex_mode")) {
            changesHexMode = (val == QLatin1String("true"));
        }
    }

    // Flush last pointer if any
    if (section == Section::Pointers)
        flushPointer();

    // Build fallback decode entries for embedded table (legacy)
    if (hasEmbeddedTable && translationTable)
        translationTable->buildFallbackDecodeEntries();

    // Build fallback decode entries for multi-tables
    for (auto &entry : tables) {
        if (entry.table && entry.table->size() > 0)
            entry.table->buildFallbackDecodeEntries();
    }

    // If we loaded a legacy single table but no multi-tables, migrate it
    if (tables.isEmpty() && translationTable && translationTable->size() > 0) {
        DocTableEntry dte;
        dte.name = QStringLiteral("Table 1");
        dte.table = translationTable;
        translationTable = nullptr;  // ownership transferred
        tables.append(dte);
        activeTableIndex = useTable ? 0 : -1;
    }

    f.close();
    projectFilePath = path;
    isUntitled = false;
    return true;
}

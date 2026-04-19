#include "hexdocument.h"
#include "PointerListModel.h"
#include "SectionListModel.h"
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
// Dirty tracking
// ---------------------------------------------------------------------------
void HexDocument::markDirty()
{
    if (!m_dirty)
    {
        m_dirty = true;
        
        if (m_onDirtyChanged)
            m_onDirtyChanged();
    }
}

void HexDocument::clearDirty()
{
    m_dirty = false;
}

// ---------------------------------------------------------------------------
// Setters (dirty-tracked)
// ---------------------------------------------------------------------------

void HexDocument::setCurrentEncoding(const QString &v)
{
    if (m_currentEncoding != v) { m_currentEncoding = v; markDirty(); }
}

void HexDocument::setRomType(RomType v)
{
    if (m_romType != v) { m_romType = v; markDirty(); }
}

void HexDocument::setByteOrder(ByteOrder v)
{
    if (m_byteOrder != v) { m_byteOrder = v; markDirty(); }
}

void HexDocument::setPointerOffset(qint64 v)
{
    if (m_pointerOffset != v) { m_pointerOffset = v; markDirty(); }
}

void HexDocument::setPointerSize(int v)
{
    if (m_pointerSize != v) { m_pointerSize = v; markDirty(); }
}

void HexDocument::setPointerSnapshot(const QVector<QPair<qint64, qint64>> &v)
{
    m_pointerSnapshot = v;
    m_pointerNameSnapshot.clear();
    markDirty();
}

void HexDocument::setAlignmentOffsets(const QVector<qint64> &v)
{
    if (m_alignmentOffsets != v) { m_alignmentOffsets = v; markDirty(); }
}

// ---------------------------------------------------------------------------
// Pointer snapshot helpers
// ---------------------------------------------------------------------------

void HexDocument::snapshotPointers(PointerListModel *model)
{
    m_pointerSnapshot.clear();
    m_pointerNameSnapshot.clear();

    if (!model)
        return;

    const QList<qint64> keys = model->pointerKeys();

    m_pointerSnapshot.reserve(keys.size());
    
    for (qint64 ptrOfs : keys)
    {
        const qint64 target = model->getOffset(ptrOfs);
        const int size = model->getPointerSize(ptrOfs);
        m_pointerSnapshot.append({ptrOfs, PointerListModel::encodePtrValue(target, size)});
        const QString targetName = model->offsetName(target);
        if (!targetName.isEmpty())
            m_pointerNameSnapshot.insert(target, targetName);
    }
}

void HexDocument::restorePointers(PointerListModel *model) const
{
    if (!model)
        return;

    model->clear();

    if (!m_pointerSnapshot.isEmpty())
        model->addPointersBatch(m_pointerSnapshot);
    if (!m_pointerNameSnapshot.isEmpty())
        model->setOffsetNames(m_pointerNameSnapshot);
}

void HexDocument::snapshotSections(SectionListModel *model)
{
    sectionSnapshot.clear();
    groupSnapshot.clear();
    if (!model)
        return;
    sectionSnapshot = model->sections();
    groupSnapshot = model->groups();
}

void HexDocument::restoreSections(SectionListModel *model) const
{
    if (!model)
        return;
    model->setSectionsAndGroups(sectionSnapshot, groupSnapshot);
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

    for (auto it = items->cbegin(); it != items->cend(); ++it)
    {
        out << indent << "- "
            << QString::number(static_cast<uint8_t>(it.key()), 16).toUpper().rightJustified(2, '0')
            << "=" << it.value() << "\n";

        const QStringList aliases = table->decodeAliasesForKey(static_cast<uint8_t>(it.key()));
        for (const QString &alias : aliases)
        {
            if (alias == it.value())
                continue;

            out << indent << "- "
                << QString::number(static_cast<uint8_t>(it.key()), 16).toUpper().rightJustified(2, '0')
                << "=" << alias << "\n";
        }
    }

    // Multi-byte entries
    const auto &mbItems = table->getMultiByteItems();

    for (auto it = mbItems.cbegin(); it != mbItems.cend(); ++it)
    {
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
    if (!filePath.isEmpty())
    {
        const QString rel = projectDir.relativeFilePath(filePath);
        out << "file: " << yamlEscape(rel) << "\n";
    }

    // Multi-table section
    if (!tablesVec.isEmpty())
    {
        out << "\ntables:\n";
        
        for (int i = 0; i < tablesVec.size(); ++i)
        {
            const auto &entry = tablesVec[i];
            out << "  - name: " << yamlEscape(entry.name) << "\n";
            out << "    original: " << (entry.isOriginal ? "true" : "false") << "\n";
            
            if (entry.table && entry.table->size() > 0)
            {
                out << "    entries:\n";
                writeTableEntries(out, entry.table, QStringLiteral("      "));
            }
        }

        out << "active_table: " << activeIdx << "\n";
    }

    out << "use_table: " << (useTable ? "true" : "false") << "\n";

    // Encoding
    out << "\nencoding: " << yamlEscape(m_currentEncoding) << "\n";

    // ROM type & byte order
    out << "rom_type: " << romTypeName(m_romType) << "\n";
    
    if (m_pointerOffset < 0)
        out << "pointer_offset: -0x" << QString::number(-m_pointerOffset, 16).toUpper() << "\n";
    else
        out << "pointer_offset: 0x" << QString::number(m_pointerOffset, 16).toUpper() << "\n";

    out << "pointer_size: " << m_pointerSize << "\n";

    switch (m_byteOrder)
    {
        case ByteOrder::BigEndian:    out << "byte_order: BE\n"; break;
        case ByteOrder::SwappedBytes: out << "byte_order: BS\n"; break;
        default:                      out << "byte_order: LE\n"; break;
    }

    // Pointers
    if (!m_pointerSnapshot.isEmpty())
    {
        out << "\npointers:\n";

        for (const auto &p : m_pointerSnapshot)
        {
            out << "  - offset: 0x" << QString::number(p.first, 16).toUpper() << "\n";
            const qint64 target = PointerListModel::decodePtrTarget(p.second);
            out << "    target: 0x" << QString::number(target, 16).toUpper() << "\n";
            out << "    size: " << PointerListModel::decodePtrSize(p.second) << "\n";
            const QString name = m_pointerNameSnapshot.value(target);
            if (!name.isEmpty())
                out << "    name: " << yamlEscape(name) << "\n";
        }
    }

    // cursor_position, dock_layout_state, and per-tab UI toggles are stored in
    // app settings, not in the project file.

    if (!tablesColumnsState.isEmpty())
        out << "tables_columns_state: " << QString::fromLatin1(tablesColumnsState.toBase64()) << "\n";

    // Original bytes
    if (!originalBytes.isEmpty())
    {
        out << "\noriginal:\n";

        for (const auto &entry : originalBytes)
        {
            out << "  - " << QString::number(entry.first, 16).toUpper()
                << ": " << entry.second.toHex().toUpper() << "\n";
        }
    }

    if (originalFileSize >= 0)
        out << "original_file_size: " << originalFileSize << "\n";

    // Alignment (virtual line breaks)
    if (!m_alignmentOffsets.isEmpty())
    {
        out << "\nalignment:\n";
        for (qint64 off : m_alignmentOffsets)
            out << "  - 0x" << QString::number(off, 16).toUpper() << "\n";
    }

    // Sections
    out << "\nshow_sections: " << (showSections ? "true" : "false") << "\n";
    if (!groupSnapshot.isEmpty())
    {
        out << "\nsection_groups:\n";
        for (const auto &g : groupSnapshot)
        {
            out << "  - name: " << yamlEscape(g.name) << "\n";
            if (g.color.isValid())
                out << "    color: " << g.color.name(QColor::HexArgb) << "\n";
            out << "    tree_order: " << g.treeOrder << "\n";
            if (g.parentGroupId >= 0)
                out << "    parent_group: " << g.parentGroupId << "\n";
        }
    }

    if (!sectionSnapshot.isEmpty())
    {
        out << "\nsections:\n";
        for (const auto &s : sectionSnapshot)
        {
            out << "  - name: " << yamlEscape(s.name) << "\n";
            out << "    start: 0x" << QString::number(s.startOffset, 16).toUpper() << "\n";
            out << "    color: " << s.color.name(QColor::HexArgb) << "\n";
            out << "    display: " << s.displayMode << "\n";
            if (s.displayMode == SectionDisplay_Disasm && s.disasmCpu != RomType::Unknown)
                out << "    disasm_cpu: " << static_cast<int>(s.disasmCpu) << "\n";
            if (s.displayMode == SectionDisplay_Graphics) {
                out << "    tile_codec: " << static_cast<int>(s.tileCodec) << "\n";
                out << "    tile_cols: " << s.tileCols << "\n";
                if (!s.palette.isEmpty()) {
                    out << "    palette:";
                    for (const QRgb c : s.palette)
                        out << " " << QString::number(c & 0xFFFFFF, 16).rightJustified(6, QLatin1Char('0'));
                    out << "\n";
                }
            }
            if (s.groupId >= 0)
                out << "    group: " << s.groupId << "\n";
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
    m_currentEncoding = QStringLiteral("ASCII");
    m_romType = RomType::Unknown;
    m_byteOrder = ByteOrder::LittleEndian;
    m_pointerOffset = defaultPointerOffset(RomType::Unknown);
    m_pointerSnapshot.clear();
    m_pointerNameSnapshot.clear();
    dockLayoutState.clear();
    tablesColumnsState.clear();
    cursorPosition = 0;
    showSections = true;
    sectionSnapshot.clear();
    originalBytes.clear();
    m_alignmentOffsets.clear();
    originalFileSize = -1;

    enum class Section { Root, Pointers, TableEntries, Original, Tables, TablesEntries, Alignment, Sections, SectionGroups };
    Section section = Section::Root;

    // Temp for building a pointer entry
    qint64 ptrOffset = -1;
    qint64 ptrTarget = -1;
    int    ptrSize   = 4;
    QString ptrName;

    // Temp for table entries
    bool hasEmbeddedTable = false;
    bool pointerOffsetExplicit = false;

    auto flushPointer = [&]()
    {
        if (ptrOffset >= 0 && ptrTarget >= 0)
        {
            m_pointerSnapshot.append({ptrOffset, PointerListModel::encodePtrValue(ptrTarget, ptrSize)});
            if (!ptrName.trimmed().isEmpty())
                m_pointerNameSnapshot.insert(ptrTarget, ptrName.trimmed());
        }

        ptrOffset = -1;
        ptrTarget = -1;
        ptrSize = 4;
        ptrName.clear();
    };

    auto switchSection = [&](Section newSection)
    {
        if (section == Section::Pointers)
            flushPointer();
        section = newSection;
    };

    QTextStream in(&f);

    while (!in.atEnd())
    {
        QString line = in.readLine();

        // Skip comments and empty lines
        const QString stripped = line.trimmed();

        if (stripped.isEmpty() || stripped.startsWith(QLatin1Char('#')))
            continue;

        // Detect section headers
        if (stripped == QLatin1String("pointers:"))
        {
            switchSection(Section::Pointers);
            continue;
        }

        if (stripped == QLatin1String("table_entries:"))
        {
            switchSection(Section::TableEntries);
            hasEmbeddedTable = true;
            continue;
        }

        if (stripped == QLatin1String("original:"))
        {
            switchSection(Section::Original);
            continue;
        }

        if (stripped == QLatin1String("tables:"))
        {
            switchSection(Section::Tables);
            continue;
        }

        if (stripped == QLatin1String("alignment:"))
        {
            switchSection(Section::Alignment);
            continue;
        }

        if (stripped == QLatin1String("section_groups:"))
        {
            switchSection(Section::SectionGroups);
            continue;
        }

        if (stripped == QLatin1String("sections:"))
        {
            switchSection(Section::Sections);
            continue;
        }

        // --- Tables section (v3 multi-table): parse "  - name: ..." and sub-entries ---
        if (section == Section::Tables || section == Section::TablesEntries)
        {
            // Detect "  - name: <tableName>" which starts a new table
            if (stripped.startsWith(QLatin1String("- name:")))
            {
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
            if (stripped == QLatin1String("entries:"))
            {
                section = Section::TablesEntries;
                continue;
            }

            // Parse "    original: true/false"
            if (stripped.startsWith(QLatin1String("original:")) && !tables.isEmpty())
            {
                const QString val = stripped.mid(9).trimmed();
                tables.last().isOriginal = (val == QLatin1String("true"));
                continue;
            }

            // Parse entry lines "      - HH=text"
            if (section == Section::TablesEntries && stripped.startsWith(QLatin1String("- ")))
            {
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

                for (int i = 0; i < hexPart.size(); ++i)
                {
                    QChar ch = hexPart[i];
                    if (!ch.isDigit() && !(ch >= 'A' && ch <= 'F') && !(ch >= 'a' && ch <= 'f')) {
                        allHex = false;
                        break;
                    }
                }

                if (!allHex || hexPart.isEmpty()) continue;

                if (hexPart.size() <= 2)
                {
                    bool ok;
                    uint val = hexPart.toUInt(&ok, 16);
                    if (ok) {
                        const uint8_t key = static_cast<uint8_t>(val);
                        if (!t->getItems()->contains(static_cast<char>(key)))
                            t->setItem(key, value);
                        else
                            t->addDecodeAlias(key, value);
                    }
                }
                else if (hexPart.size() % 2 == 0)
                {
                    QByteArray key;
                    bool ok = true;

                    for (int i = 0; i < hexPart.size(); i += 2)
                    {
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
            if (!stripped.startsWith(QLatin1Char('-')) && !stripped.startsWith(QLatin1String("entries")))
            {
                section = Section::Root;
                // Fall through to root parsing below
            }
            else
            {
                continue;
            }
        }

        // --- TableEntries section: each line is "  - HH=text" (TBL format) ---
        if (section == Section::TableEntries)
        {
            if (!stripped.startsWith(QLatin1String("- ")))
            {
                section = Section::Root;
            }
            else
            {
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

                for (int i = 0; i < hexPart.size(); ++i)
                {
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

                if (hexPart.size() <= 2)
                {
                    bool ok;
                    uint val = hexPart.toUInt(&ok, 16);
                    if (ok) {
                        const uint8_t key = static_cast<uint8_t>(val);
                        if (!translationTable->getItems()->contains(static_cast<char>(key)))
                            translationTable->setItem(key, value);
                        else
                            translationTable->addDecodeAlias(key, value);
                    }
                }
                else if (hexPart.size() % 2 == 0)
                {
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
        if (section == Section::Alignment)
        {
            if (!stripped.startsWith(QLatin1String("- ")))
            {
                section = Section::Root;
            }
            else
            {
                QString entry = stripped.mid(2).trimmed();
                bool ok = false;

                qint64 off = entry.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)
                    ? entry.mid(2).toLongLong(&ok, 16) : entry.toLongLong(&ok);

                if (ok && off >= 0)
                    m_alignmentOffsets.append(off);

                continue;
            }
        }

        if (section == Section::Original) 
        {
            if (!stripped.startsWith(QLatin1String("- ")))
            {
                section = Section::Root;
            }
            else
            {
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

        // --- Sections section: each entry is "  - name: ...\n    start: ...\n    end: ...\n    color: ..." ---
        // --- Section groups ---
        if (section == Section::SectionGroups)
        {
            if (stripped.startsWith(QLatin1String("- name:")))
            {
                ::SectionGroup g;
                g.name = yamlUnescape(stripped.mid(7).trimmed());
                groupSnapshot.append(g);
                continue;
            }
            if (!groupSnapshot.isEmpty())
            {
                auto &cur = groupSnapshot.last();
                if (stripped.startsWith(QLatin1String("color:")))
                {
                    cur.color = QColor(stripped.mid(6).trimmed());
                    continue;
                }
                if (stripped.startsWith(QLatin1String("tree_order:")))
                {
                    bool ok = false;
                    const int to = stripped.mid(11).trimmed().toInt(&ok);
                    if (ok) cur.treeOrder = to;
                    continue;
                }
                if (stripped.startsWith(QLatin1String("parent_group:")))
                {
                    bool ok = false;
                    const int pg = stripped.mid(13).trimmed().toInt(&ok);
                    if (ok && pg >= 0) cur.parentGroupId = pg;
                    continue;
                }
            }
            if (!stripped.startsWith(QLatin1Char('-')) && !stripped.startsWith(QLatin1String("color"))
                && !stripped.startsWith(QLatin1String("tree_order"))
                && !stripped.startsWith(QLatin1String("parent_group")))
            {
                section = Section::Root;
            }
            else
            {
                continue;
            }
        }

        if (section == Section::Sections)
        {
            if (stripped.startsWith(QLatin1String("- name:")))
            {
                ::Section sec;
                sec.name = yamlUnescape(stripped.mid(7).trimmed());
                sectionSnapshot.append(sec);
                continue;
            }

            if (!sectionSnapshot.isEmpty())
            {
                auto &cur = sectionSnapshot.last();
                if (stripped.startsWith(QLatin1String("start:")))
                {
                    QString v = stripped.mid(6).trimmed();
                    bool ok = false;
                    cur.startOffset = v.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)
                        ? v.mid(2).toLongLong(&ok, 16) : v.toLongLong(&ok);
                    continue;
                }
                if (stripped.startsWith(QLatin1String("end:")))
                {
                    // Legacy: "end:" is ignored in the new model (section ends at next start)
                    continue;
                }
                if (stripped.startsWith(QLatin1String("color:")))
                {
                    cur.color = QColor(stripped.mid(6).trimmed());
                    continue;
                }
                if (stripped.startsWith(QLatin1String("parent:")))
                {
                    // Legacy: "parent:" is ignored in the new model
                    continue;
                }
                if (stripped.startsWith(QLatin1String("group:")))
                {
                    bool ok = false;
                    const int gi = stripped.mid(6).trimmed().toInt(&ok);
                    if (ok) cur.groupId = gi;
                    continue;
                }
                if (stripped.startsWith(QLatin1String("display:")))
                {
                    bool ok = false;
                    const int dm = stripped.mid(8).trimmed().toInt(&ok);
                    if (ok) cur.displayMode = dm;
                    continue;
                }
                if (stripped.startsWith(QLatin1String("disasm_cpu:")))
                {
                    bool ok = false;
                    const int dc = stripped.mid(11).trimmed().toInt(&ok);
                    if (ok) cur.disasmCpu = static_cast<RomType>(dc);
                    continue;
                }
                if (stripped.startsWith(QLatin1String("tile_codec:")))
                {
                    bool ok = false;
                    const int tc = stripped.mid(11).trimmed().toInt(&ok);
                    if (ok) cur.tileCodec = static_cast<TileCodec>(tc);
                    continue;
                }
                if (stripped.startsWith(QLatin1String("tile_cols:")))
                {
                    bool ok = false;
                    const int tc = stripped.mid(10).trimmed().toInt(&ok);
                    if (ok && tc > 0) cur.tileCols = tc;
                    continue;
                }
                if (stripped.startsWith(QLatin1String("palette:")))
                {
                    const QStringList colors = stripped.mid(8).trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
                    cur.palette.clear();
                    for (const QString &c : colors) {
                        bool ok;
                        const uint v = c.toUInt(&ok, 16);
                        if (ok) cur.palette.append(qRgb((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF));
                    }
                    continue;
                }
            }

            if (!stripped.startsWith(QLatin1Char('-')) && !stripped.startsWith(QLatin1String("start"))
                && !stripped.startsWith(QLatin1String("end")) && !stripped.startsWith(QLatin1String("color"))
                && !stripped.startsWith(QLatin1String("parent")) && !stripped.startsWith(QLatin1String("group"))
                && !stripped.startsWith(QLatin1String("display"))
                && !stripped.startsWith(QLatin1String("disasm_cpu"))
                && !stripped.startsWith(QLatin1String("tile_codec"))
                && !stripped.startsWith(QLatin1String("tile_cols"))
                && !stripped.startsWith(QLatin1String("palette")))
            {
                section = Section::Root;
            }
            else
            {
                continue;
            }
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

        if (section == Section::Pointers)
        {
            if (key == QLatin1String("- offset") || key == QLatin1String("offset"))
            {
                if (key == QLatin1String("- offset"))
                    flushPointer();
                else if (ptrOffset >= 0)
                    flushPointer();
                bool ok = false;
                ptrOffset = val.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)
                    ? val.mid(2).toLongLong(&ok, 16) : val.toLongLong(&ok);
                continue;
            }
            else if (key == QLatin1String("target"))
            {
                bool ok = false;
                ptrTarget = val.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)
                    ? val.mid(2).toLongLong(&ok, 16) : val.toLongLong(&ok);
                continue;
            }
            else if (key == QLatin1String("size"))
            {
                ptrSize = val.toInt();
                if (ptrSize < 2 || ptrSize > 4) ptrSize = 4;
                continue;
            }
            else if (key == QLatin1String("name"))
            {
                ptrName = yamlUnescape(val);
                continue;
            }

            // Non-pointer key: exit pointers section, fall through to root parsing
            switchSection(Section::Root);
        }

        // Root section
        if (key == QLatin1String("title"))
        {
            projectName = yamlUnescape(val);
        }
        else if (key == QLatin1String("file"))
        {
            const QString rel = yamlUnescape(val);

            filePath = QFileInfo(projectDir.filePath(rel)).canonicalFilePath();

            if (filePath.isEmpty())
                filePath = projectDir.filePath(rel);
        }
        else if (key == QLatin1String("table"))
        {
            const QString rel = yamlUnescape(val);
            tableFilePath = QFileInfo(projectDir.filePath(rel)).canonicalFilePath();
            if (tableFilePath.isEmpty())
                tableFilePath = projectDir.filePath(rel);
        }
        else if (key == QLatin1String("use_table"))
        {
            useTable = (val == QLatin1String("true"));
        }
        else if (key == QLatin1String("encoding"))
        {
            m_currentEncoding = yamlUnescape(val);
        }
        else if (key == QLatin1String("rom_type"))
        {
            // Support both legacy integer and mnemonic string
            bool ok = false;
            const int rt = val.toInt(&ok);
            if (ok) {
                if (rt >= 0 && rt < kRomTypeCount)
                    m_romType = static_cast<RomType>(rt);
            } else {
                // Search by mnemonic
                for (int i = 0; i < kRomTypeCount; ++i) {
                    if (romTypeName(static_cast<RomType>(i)) == val) {
                        m_romType = static_cast<RomType>(i);
                        break;
                    }
                }
            }
        }
        else if (key == QLatin1String("byte_order"))
        {
            // Support both legacy integer and LE/BE/BS string
            if (val == QLatin1String("BE"))
                m_byteOrder = ByteOrder::BigEndian;
            else if (val == QLatin1String("BS"))
                m_byteOrder = ByteOrder::SwappedBytes;
            else if (val == QLatin1String("LE"))
                m_byteOrder = ByteOrder::LittleEndian;
            else
            {
                const int bo = val.toInt();

                if (bo >= 0 && bo <= 2)
                    m_byteOrder = static_cast<ByteOrder>(bo);
            }
        }
        else if (key == QLatin1String("pointer_offset"))
        {
            pointerOffsetExplicit = true;
            bool negative = false;
            QString off = val.trimmed();

            if (off.startsWith('-'))
            {
                negative = true;
                off.remove(0, 1);
            }
            else if (off.startsWith('+'))
            {
                off.remove(0, 1);
            }

            bool ok = false;
            qint64 parsed = 0;

            if (off.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
                parsed = off.mid(2).toLongLong(&ok, 16);
            else
                parsed = off.toLongLong(&ok, 16);

            if (ok)
                m_pointerOffset = negative ? -parsed : parsed;
        }
        else if (key == QLatin1String("pointer_size"))
        {
            const int sz = val.toInt();
            if (sz == 2 || sz == 4)
                m_pointerSize = sz;
        }
        else if (key == QLatin1String("active_table"))
        {
            activeTableIndex = val.toInt();
        }
        else if (key == QLatin1String("cursor_position"))
        {
            bool ok = false;

            cursorPosition = val.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)
                ? val.mid(2).toLongLong(&ok, 16) : val.toLongLong(&ok);
        }
        else if (key == QLatin1String("show_sections"))
        {
            showSections = (val == QLatin1String("true"));
        }
        else if (key == QLatin1String("dock_layout_state"))
        {
            dockLayoutState = QByteArray::fromBase64(val.toLatin1());
        }
        else if (key == QLatin1String("tables_columns_state"))
        {
            tablesColumnsState = QByteArray::fromBase64(val.toLatin1());
        }
        else if (key == QLatin1String("original_file_size"))
        {
            bool ok = false;
            qint64 v = val.toLongLong(&ok);

            if (ok && v >= 0)
                originalFileSize = v;
        }
    }

    // Flush last pointer if any
    if (section == Section::Pointers)
        flushPointer();

    if (!pointerOffsetExplicit)
        m_pointerOffset = defaultPointerOffset(m_romType);

    // Build fallback decode entries for embedded table (legacy)
    if (hasEmbeddedTable && translationTable)
        translationTable->buildFallbackDecodeEntries();

    // Build fallback decode entries for multi-tables
    for (auto &entry : tables)
    {
        if (entry.table && entry.table->size() > 0)
            entry.table->buildFallbackDecodeEntries();
    }

    // Fix legacy audio/graphics section colors: old projects may have saved without
    // proper alpha channel.  Restore semi-transparent color.
    for (auto &s : sectionSnapshot) {
        if (s.displayMode == SectionDisplay_Audio && s.color.alpha() > 200)
            s.color.setAlpha(0x40);
        if (s.displayMode == SectionDisplay_Graphics && s.color.alpha() > 200)
            s.color.setAlpha(0x40);
    }

    // If we loaded a legacy single table but no multi-tables, migrate it
    if (tables.isEmpty() && translationTable && translationTable->size() > 0)
    {
        DocTableEntry dte;
        dte.name = QStringLiteral("Table 1");
        dte.table = translationTable;
        translationTable = nullptr;  // ownership transferred
        tables.append(dte);
        activeTableIndex = useTable ? 0 : -1;
    }

    // If treeOrder values are all zero (old project or not set), assign defaults:
    // groups in definition order (0, 1, 2, ...).
    {
        bool anyNonZero = false;
        for (const auto &g : groupSnapshot) if (g.treeOrder != 0) { anyNonZero = true; break; }
        if (!anyNonZero && !groupSnapshot.isEmpty()) {
            for (int i = 0; i < groupSnapshot.size(); ++i)
                groupSnapshot[i].treeOrder = i;
        }
    }

    f.close();
    projectFilePath = path;
    isUntitled = false;
    m_dirty = false;  // freshly loaded — not dirty

    return true;
}

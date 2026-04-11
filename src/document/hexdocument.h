#ifndef HEXDOCUMENT_H
#define HEXDOCUMENT_H

#include <QString>
#include <QVector>
#include <QPair>
#include <QMap>
#include <QByteArray>
#include <functional>

#include "Datas.h"
#include "romdetect.h"
#include "SectionListModel.h"

class TranslationTable;
class PointerListModel;

// A named translation table entry for multi-table support.
struct DocTableEntry
{
    QString name;
    TranslationTable *table = nullptr;  // owned by HexDocument when loaded
    bool isOriginal = false;            // true if this is the "original" encoding table
};

// Per-file document state: everything that belongs to a single opened file
// and should be saved/restored as part of a project.
//
// Properties that are serialised into the .rthp project file are tracked for
// changes: calling a setter with a value different from the current one marks
// the document as "dirty" (project needs saving).
class HexDocument
{
public:
    HexDocument();
    ~HexDocument();

    // ---- Dirty (project-modified) tracking ----

    // True when any project-relevant property has changed since the last
    // clearDirty() call (i.e. since the project was saved).
    bool isDirty() const { return m_dirty; }

    // Explicitly mark the document as dirty (e.g. after external changes to
    // tables, pointers, or virtual formatting that the document owns).
    void markDirty();

    // Clear the dirty flag (called after a successful project save).
    void clearDirty();

    // Install a callback that is invoked whenever the dirty flag transitions
    // from clean → dirty.  Used by MainWindow to update tab title / star.
    void setDirtyChangedCallback(std::function<void()> cb) { m_onDirtyChanged = std::move(cb); }

    // ---- File (not dirty-tracked — metadata only) ----

    QString filePath;                       // canonical path to the data file
    bool    isUntitled = true;              // true when no file has been opened

    // ---- Project file (not dirty-tracked — save path only) ----

    QString projectFilePath;                // path to the .rthp file (empty = unsaved)

    // ---- Project name ----

    QString projectName;

    // ---- Translation tables (multi-table) ----

    QVector<DocTableEntry> tables;
    int activeTableIndex = -1;

    // Legacy single-table fields (populated when loading old projects)
    QString            tableFilePath;       // path to .tbl file (empty = none)
    TranslationTable  *translationTable = nullptr;  // in-memory table (owned)

    bool useTable = false;

    // ---- Encoding ----

    const QString &currentEncoding() const { return m_currentEncoding; }
    void setCurrentEncoding(const QString &v);

    // ---- ROM / endianness ----

    RomType romType() const { return m_romType; }
    void setRomType(RomType v);

    ByteOrder byteOrder() const { return m_byteOrder; }
    void setByteOrder(ByteOrder v);

    qint64 pointerOffset() const { return m_pointerOffset; }
    void setPointerOffset(qint64 v);

    int  pointerSize() const { return m_pointerSize; }
    void setPointerSize(int v);

    // ---- Pointers ----

    const QVector<QPair<qint64, qint64>> &pointerSnapshot() const { return m_pointerSnapshot; }
    void setPointerSnapshot(const QVector<QPair<qint64, qint64>> &v);

    // ---- Cursor position (saved but does NOT dirty the project) ----

    qint64 cursorPosition = 0;

    // ---- Display settings ----

    // Per-tab pointer/change toggles live in EditorSession and app settings.
    bool showSections = true;

    // ---- Sections ----

    QVector<Section> sectionSnapshot;

    // Dock layout / columns state — saved to project but NOT dirty-tracked
    // (pure UI geometry; losing it is harmless).
    QByteArray dockLayoutState;
    QByteArray tablesColumnsState;

    // ---- Original bytes (pre-modification snapshots) ----

    QVector<QPair<qint64, QByteArray>> originalBytes;
    qint64 originalFileSize = -1;

    // ---- Alignment (virtual line breaks) ----

    const QVector<qint64> &alignmentOffsets() const { return m_alignmentOffsets; }
    void setAlignmentOffsets(const QVector<qint64> &v);

    // ---- Serialisation (YAML .rthp) ----

    bool saveProject(const QString &path,
                     const QVector<DocTableEntry> &tables,
                     int activeTableIndex);

    bool loadProject(const QString &path);

    void snapshotPointers(PointerListModel *model);
    void restorePointers(PointerListModel *model) const;

    void snapshotSections(SectionListModel *model);
    void restoreSections(SectionListModel *model) const;

private:
    // Dirty tracking
    bool m_dirty = false;
    std::function<void()> m_onDirtyChanged;

    // Private data

    QString m_currentEncoding = QStringLiteral("ASCII");

    RomType   m_romType   = RomType::Unknown;
    ByteOrder m_byteOrder = ByteOrder::LittleEndian;
    qint64    m_pointerOffset = 0;
    int       m_pointerSize = 4;

    QVector<QPair<qint64, qint64>> m_pointerSnapshot;

    QVector<qint64> m_alignmentOffsets;

    // Simple YAML helpers
    static QString yamlEscape(const QString &s);
    static QString yamlUnescape(const QString &s);
};

#endif // HEXDOCUMENT_H

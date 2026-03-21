#ifndef HEXDOCUMENT_H
#define HEXDOCUMENT_H

#include <QString>
#include <QVector>
#include <QPair>
#include <QMap>

#include "Datas.h"
#include "romdetect.h"

class TranslationTable;
class PointerListModel;

/// A named translation table entry for multi-table support.
struct DocTableEntry
{
    QString name;
    TranslationTable *table = nullptr;  // owned by HexDocument when loaded
    bool isOriginal = false;            // true if this is the "original" encoding table
};

/// Per-file document state: everything that belongs to a single opened file
/// and should be saved/restored as part of a project.
class HexDocument
{
public:
    HexDocument();
    ~HexDocument();

    // --- File ---
    QString filePath;                       // canonical path to the data file
    bool    isUntitled = true;              // true when no file has been opened

    // --- Translation tables (multi-table) ---
    QVector<DocTableEntry> tables;          // all embedded tables (owned)
    int activeTableIndex = -1;              // -1 = no active table

    // Legacy single-table fields (populated when loading old projects)
    QString            tableFilePath;       // path to .tbl file (empty = none)
    TranslationTable  *translationTable = nullptr;  // in-memory table (owned)
    bool               useTable = false;    // whether the table is active

    // --- Encoding ---
    QString currentEncoding = QStringLiteral("ASCII");

    // --- ROM / endianness ---
    RomType   romType   = RomType::Unknown;
    ByteOrder byteOrder = ByteOrder::LittleEndian;

    // --- Pointers ---
    /// Serialisable snapshot of the pointer list.
    /// Each entry: (pointerOffset, encodedValue) where encodedValue has
    /// target address + pointer byte-size encoded in bits 61-62
    /// (see PointerListModel::encodePtrValue).
    QVector<QPair<qint64, qint64>> pointerSnapshot;

    // --- Navigation history ---
    QVector<qint64> navigationHistory;
    int  navigationHistoryIndex = -1;

    // --- Cursor position ---
    qint64 cursorPosition = 0;              // last active cursor offset in the editor

    // --- Display settings ---
    bool showPointers = true;               // whether pointer highlighting is visible
    bool showChanges  = false;              // whether change highlighting is visible
    bool changesHexMode = false;            // true = changes list shows hex, false = text
    bool tablesDockVisible = true;          // tables dock visibility in UI
    bool pointersDockVisible = false;       // pointers dock visibility in UI
    bool changesDockVisible = false;        // changes dock visibility in UI

    // --- Original bytes (pre-modification snapshots) ---
    /// Groups of original bytes before user modifications.
    /// Each entry: (offset, contiguousOriginalBytes).
    /// Used for IPS patch generation and diff display.
    QVector<QPair<qint64, QByteArray>> originalBytes;

    // --- Project file ---
    QString projectFilePath;                // path to the .rthp file (empty = unsaved)
    QString projectName;                    // human-readable project name (from title: key)

    // ---- Serialisation (YAML .rthp) ----

    /// Save document metadata to a .rthp YAML file.
    /// Pass the list of tables and active index for multi-table support.
    bool saveProject(const QString &path,
                     const QVector<DocTableEntry> &tables,
                     int activeTableIndex);

    /// Legacy overload: save with a single table (backward compat).
    bool saveProject(const QString &path, const TranslationTable *table = nullptr);

    /// Load document metadata from a .rthp YAML file.
    /// Returns true on success.  After loading, the caller must:
    ///   1. Open `filePath` in HexEditor
    ///   2. Load `tableFilePath` into a TranslationTable if non-empty
    ///   3. Restore pointers from `pointerSnapshot`
    bool loadProject(const QString &path);

    /// Take a snapshot of pointers from the live PointerListModel
    /// so they can be serialised later.
    void snapshotPointers(PointerListModel *model);

    /// Restore pointers from `pointerSnapshot` into a live PointerListModel.
    void restorePointers(PointerListModel *model) const;

private:
    // Simple YAML helpers (flat key-value, no nesting beyond pointer list)
    static QString yamlEscape(const QString &s);
    static QString yamlUnescape(const QString &s);
};

#endif // HEXDOCUMENT_H

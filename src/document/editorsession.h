#ifndef EDITORSESSION_H
#define EDITORSESSION_H

#include <QFile>
#include <QByteArray>
#include <QVector>
#include <QString>
#include "romdetect.h"
#include "TablesDockWidget.h"

class HexEditor;
class HexDocument;

/// Per-tab state: everything that changes when the user switches between open files.
class EditorSession
{
public:
    EditorSession();
    ~EditorSession();

    HexEditor *editor = nullptr;       // owned by QTabWidget (Qt parent)
    HexDocument *document = nullptr;   // owned by this session
    QFile file;                        // keep alive for Chunks lazy I/O

    QString curFile;
    QString tableFilePath;
    bool isUntitled = true;
    qint64 curOffset = 0;

    /// Non-owning pointer to the active translation table in TablesDockWidget.
    TranslationTable *table = nullptr;

    /// Per-tab snapshot of the entire TablesDockWidget content (deep copy).
    QVector<TableTab> tableSnapshot;
    int tableActiveIndex = -1;

    QByteArray changeTrackingSnapshot;

    RomType detectedRomType = RomType::Unknown;
    qint64 pointerOffset = defaultPointerOffset(RomType::Unknown);
    int pointerSize = 4;
    QString currentEncoding = QStringLiteral("ASCII");

    QVector<qint64> navigationHistory;
    int navigationHistoryIndex = -1;
    bool navigationJumpInProgress = false;

    bool tablesDockVisible = false;
    bool tablesDockVisibilityInitialized = false;

    // Per-tab Find/Replace dialog state
    QString searchFindText;
    int     searchFindFormat    = -1;  // table combo data: -1 = Raw, -2 = Hex, >=0 = table index
    QString searchReplaceText;
    int     searchReplaceFormat = -1;
    bool    searchRelative      = false;

    // Per-tab Find Pointers dialog state (options not covered by ROM profile)
    int  ptrSearchDir        = 0;     // 0 = before, 1 = after, 2 = both
    bool ptrExcludeSelection = false;
    bool ptrAlignedOnly      = false;
    bool ptrOptimize         = false;

private:
    EditorSession(const EditorSession &) = delete;
    EditorSession &operator=(const EditorSession &) = delete;
};

#endif // EDITORSESSION_H

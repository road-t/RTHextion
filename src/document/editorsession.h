#ifndef EDITORSESSION_H
#define EDITORSESSION_H

#include <QFile>
#include <QByteArray>
#include <QVector>
#include <QString>
#include "romdetect.h"
#include "audiodetector.h"
#include "TablesDockWidget.h"
#include "palettedetector.h"

class HexEditor;
class HexDocument;

// Per-tab state: everything that changes when the user switches between open files.
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

    // Non-owning pointer to the active translation table in TablesDockWidget.
    TranslationTable *table = nullptr;

    // Per-tab snapshot of the entire TablesDockWidget content (deep copy).
    // Used for project save/load and initial session creation.
    QVector<TableTab> tableSnapshot;
    int tableActiveIndex = -1;

    // Live table-dock widgets detached during tab switch (avoids widget
    // recreation).  Populated by TablesDockWidget::detachTabs(), consumed
    // by TablesDockWidget::attachTabs().
    TablesDockWidget::LiveTabState liveTableState;

    QByteArray changeTrackingSnapshot;

    RomType detectedRomType = RomType::Unknown;
    qint64 pointerOffset = defaultPointerOffset(RomType::Unknown);
    int pointerSize = 4;
    QString currentEncoding = QStringLiteral("ASCII");

    // Per-tab/UI-only visibility state (persisted in app settings, not .rthp).
    bool showPointers = true;
    bool showChanges = false;
    bool changesHexMode = false;
    QString defaultViewMode = QStringLiteral("text");
    PaletteStorageFormat defaultViewPaletteFormat = PaletteStorageFormat::Unknown;
    TileCodec defaultViewTileCodec = TileCodec::Linear1bpp;
    AudioSampleFormat defaultViewAudioFormat = AudioSampleFormat::Unknown;
    RomType defaultViewDisasmCpu = RomType::Unknown;

    QVector<qint64> navigationHistory;
    int navigationHistoryIndex = -1;
    bool navigationJumpInProgress = false;

    bool dockTablesVisible = true;
    bool dockPointersVisible = true;
    bool dockChangesVisible = true;
    bool dockSectionsVisible = true;
    bool dockAudioVisible = false;
    bool dockGraphicsVisible = false;

    // Per-tab audio dock parameters
    int  audioFormatIndex = 0;     // combo index in AudioDockWidget (0 = Auto)
    QString audioSampleRate;       // sample rate text (e.g. "8000")
    double audioPlaybackSpeed = 1.0;

    bool dockVisibilityInitialized = false;

    // Per-tab dock collapse/size state (captured on tab switch, applied on tab switch)
    bool dockTablesCollapsed      = false;
    int  dockTablesExpandedWidth  = -1;
    int  dockTablesExpandedHeight = -1;
    bool dockPointersCollapsed      = false;
    int  dockPointersExpandedWidth  = -1;
    int  dockPointersExpandedHeight = -1;
    bool dockChangesCollapsed      = false;
    int  dockChangesExpandedWidth  = -1;
    int  dockChangesExpandedHeight = -1;
    bool dockSectionsCollapsed      = false;
    int  dockSectionsExpandedWidth  = -1;
    int  dockSectionsExpandedHeight = -1;
    bool dockStateInitialized = false;

    // Per-tab expansion state of section tree groups.
    QVector<int> sectionsExpandedGroupIds;

    /// Set during session restore from settings to defer cursor centering
    /// until the tab's editor becomes visible.
    bool scrollPending = false;

    // Per-tab Find/Replace dialog state
    QString searchFindText;
    int     searchFindFormat    = -1;  // table combo data: -1 = Raw, -2 = Hex, >=0 = table index
    QString searchReplaceText;
    int     searchReplaceFormat = -1;
    bool    searchRelative      = false;
    int     searchSectionScope  = 0;  // 0=All, 1=Text, 2=Code, 3=Sound, 4=Graphics

    // Per-tab Jump To dialog state
    QString jumpToText;

    // Per-tab Find Pointers dialog state (options not covered by ROM profile)
    int  ptrSearchDir        = 2;     // 0 = before, 1 = after, 2 = both (whole file)
    bool ptrExcludeSelection = false;
    bool ptrAlignedOnly      = false;
    bool ptrOptimize         = false;

private:
    EditorSession(const EditorSession &) = delete;
    EditorSession &operator=(const EditorSession &) = delete;
};

#endif // EDITORSESSION_H

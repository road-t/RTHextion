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
    bool tablesDockVisible = false;

    bool projectModified = false;
    QByteArray changeTrackingSnapshot;

    RomType detectedRomType = RomType::Unknown;
    QString currentEncoding = QStringLiteral("ASCII");

    QVector<qint64> navigationHistory;
    int navigationHistoryIndex = -1;
    bool navigationJumpInProgress = false;

private:
    EditorSession(const EditorSession &) = delete;
    EditorSession &operator=(const EditorSession &) = delete;
};

#endif // EDITORSESSION_H

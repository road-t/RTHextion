// Auto-extracted from mainwindow.cpp
#include "mainwindow.h"
#include "internal.h"
using namespace MainWindowInternal;
#include <QSettings>   // kept for QSettings::NativeFormat (Windows dark-mode detection)
#include "appsettings.h"
#include <QApplication>
#include <QStyleFactory>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QAction>
#include <QFileDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QScrollBar>
#include <QMessageBox>
#ifdef Q_OS_MAC
#include "macostheme.h"
#endif
#include "theme.h"
#include "SectionListModel.h"

void MainWindow::readSettings()
{
    auto &settings = AppSettings::instance();
    const QByteArray geom = settings.value("WindowGeometry").toByteArray();
    if (!geom.isEmpty())
        restoreGeometry(geom);

    bool darkTheme;
    if (settings.contains(QStringLiteral("DarkTheme"))) {
        darkTheme = settings.value(QStringLiteral("DarkTheme")).toBool();
    } else {
        // First launch: detect system dark mode
#ifdef Q_OS_WIN
        QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
                      QSettings::NativeFormat);
        darkTheme = reg.value(QStringLiteral("AppsUseLightTheme"), 1).toInt() == 0;
#elif defined(Q_OS_MAC)
        // On macOS Qt inherits the system appearance in the default palette:
        // dark background means dark mode.
        darkTheme = qApp->palette().color(QPalette::Window).lightness() < 128;
#else
        darkTheme = false;
#endif
        if (darkTheme) {
            // Apply dark editor color defaults on first launch
            EditorTheme::defaultDark().applyToSettings();
        }
        settings.setValue(QStringLiteral("DarkTheme"), darkTheme);
    }
    if (showDarkThemeAct)
        showDarkThemeAct->setChecked(darkTheme);
    else
        applyDarkTheme(darkTheme);

    // If dark theme is active but the editor color settings are still from a
    // light theme (e.g. the user enabled system dark mode after the first
    // launch, or DarkTheme was set without migrating editor colors), apply the
    // dark editor defaults now so text is readable.  The heuristic is simple:
    // if dark mode is on and HexAreaBackgroundColor is either absent or has a
    // high lightness value (> 180), the settings are light-themed.
    if (darkTheme) {
        const QColor hexBg = settings.value(QStringLiteral("HexAreaBackgroundColor")).value<QColor>();
        if (!hexBg.isValid() || hexBg.lightness() > 180)
            EditorTheme::defaultDark().applyToSettings();
    }

    hexEdit->setAddressArea(settings.value("AddressArea", true).toBool());
    // Panel mode: backward-compatible with old "AsciiArea" bool setting
    {
        const QString pm = settings.value("PanelMode").toString();
        if (pm == QLatin1String("disasm")) {
            hexEdit->setAsciiArea(true);
            hexEdit->setShowDisasm(true);
        } else if (pm == QLatin1String("graphics") || pm == QLatin1String("sound")) {
            hexEdit->setAsciiArea(true);
            hexEdit->setShowDisasm(false);
        } else {
            // "text" or legacy bool
            hexEdit->setAsciiArea(settings.value("AsciiArea", true).toBool());
            hexEdit->setShowDisasm(false);
        }
    }
    hexEdit->setHighlighting(true);
    hexEdit->setOverwriteMode(settings.value("OverwriteMode", true).toBool());


    // Set color values with proper defaults for first-launch initialization
    hexEdit->setHighlightingColor(settings.value("HighlightingColor", QColor(0xff, 0xff, 0x99, 0xff)).value<QColor>());
    hexEdit->setPointedColor(settings.value("PointedColor", QColor(0xc0, 0x80, 0x00, 0xff)).value<QColor>());
    hexEdit->setPointedFontColor(settings.value("PointedFontColor", QColor(Qt::black)).value<QColor>());
    hexEdit->setPointerFontColor(settings.value("PointerFontColor", QColor(Qt::black)).value<QColor>());
    hexEdit->setPointerFrameColor(settings.value("PointerFrameColor", QColor(0x00, 0x00, 0xFF)).value<QColor>());
    hexEdit->setPointerFrameBackgroundColor(settings.value("PointerFrameBgColor", QColor(0x00, 0xFF, 0x00, 0x80)).value<QColor>());
    hexEdit->setAddressAreaColor(settings.value("AddressAreaColor", palette().alternateBase().color()).value<QColor>());
    hexEdit->setSelectionColor(settings.value("SelectionColor", palette().highlight().color()).value<QColor>());
    hexEdit->setFont(settings.value("WidgetFont", QFont("Courier New", 14)).value<QFont>());
    hexEdit->setAddressFontColor(settings.value("AddressFontColor", palette().color(QPalette::WindowText)).value<QColor>());
    hexEdit->setAddressZeroByteFontColor(settings.value("AddressZeroByteFontColor", QColor(0xCC, 0xCC, 0xCC)).value<QColor>());
    hexEdit->setAsciiAreaColor(settings.value("AsciiAreaColor", palette().alternateBase().color()).value<QColor>());
    hexEdit->setAsciiFontColor(settings.value("AsciiFontColor", palette().color(QPalette::WindowText)).value<QColor>());
    hexEdit->setHexFontColor(settings.value("HexFontColor", palette().color(QPalette::WindowText)).value<QColor>());
    hexEdit->setNonPrintableNoTableChar(readSingleCharSetting(settings, "NonPrintableNoTableChar", QChar(0x25AA)));
    hexEdit->setNotInTableChar(readSingleCharSetting(settings, "NotInTableChar", QChar(0x25A1)));


    hexEdit->setAddressWidth(settings.value("AddressAreaWidth", 8).toInt());
    hexEdit->setBytesPerLine(settings.value("BytesPerLine", 32).toInt());
    hexEdit->setDynamicBytesPerLine(settings.value("Autosize", true).toBool());
    hexEdit->setHexCaps(settings.value("HexCaps", true).toBool());
    hexEdit->setShowHexGrid(settings.value("ShowHexGrid", true).toBool());
    hexEdit->setShowMultibyteFrame(settings.value("ShowMultibyteFrame", true).toBool());
    hexEdit->setHexAreaBackgroundColor(settings.value("HexAreaBackgroundColor", QColor(Qt::white)).value<QColor>());
    hexEdit->setHexAreaGridColor(settings.value("HexAreaGridColor", QColor(0x99, 0x99, 0x99)).value<QColor>());
    hexEdit->setMultibyteFrameColor(settings.value("MultibyteFrameColor", QColor(0x20, 0x20, 0x20)).value<QColor>());
    hexEdit->setCursorCharColor(settings.value("CursorCharColor", QColor(0x00, 0x60, 0xFF, 0x80)).value<QColor>());
    hexEdit->setCursorFrameColor(settings.value("CursorFrameColor", QColor(Qt::black)).value<QColor>());
    hexEdit->setZeroByteFontColor(settings.value("ZeroByteFontColor", QColor(0xCC, 0xCC, 0xCC)).value<QColor>());
    hexEdit->setChangesColor(settings.value("ChangesColor", QColor(0x99, 0xff, 0x99, 0xff)).value<QColor>());
    hexEdit->setSectionHeaderFontColor(settings.value("SectionHeaderFontColor", palette().color(QPalette::WindowText)).value<QColor>());
    hexEdit->setSectionHeaderBackgroundColor(settings.value("SectionHeaderBgColor", QColor(0xD8, 0xD8, 0xD8, 0x90)).value<QColor>());
    {
        QFont sectionHeaderFont = settings.value("SectionHeaderFont", hexEdit->font()).value<QFont>();
        if (!sectionHeaderFont.bold())
            sectionHeaderFont.setBold(true);
        hexEdit->setSectionHeaderFont(sectionHeaderFont);
    }

    if (showAddressAreaAct)
        showAddressAreaAct->setChecked(hexEdit->addressArea());
    // Sync panel mode radio buttons
    if (panelModeTextAct) {
        const QString pm = settings.value("PanelMode").toString();
        if (pm == QLatin1String("disasm"))
            panelModeDisasmAct->setChecked(true);
        else if (pm == QLatin1String("graphics")) {
            panelModeGraphicsAct->setChecked(true);
            hexEdit->setShowGraphicsPanel(true);
        }
        else
            panelModeTextAct->setChecked(true);
    }
    if (showAddressGridAct)
        showAddressGridAct->setChecked(hexEdit->showHexGrid());


    const QByteArray windowState = settings.value(kMainWindowStateKey).toByteArray();
    if (!windowState.isEmpty())
        restoreState(windowState);

    // Always show all dock panels regardless of saved state
    if (m_tablesDock) m_tablesDock->show();
    if (m_pointersDock) m_pointersDock->show();
    if (m_changesDock) m_changesDock->show();

    // Restore dock column states
    if (m_pointersDock)
        m_pointersDock->restoreColumnsState(settings.value(QStringLiteral("PointersDockColumns")).toByteArray());
    if (m_tablesDock)
        m_tablesDock->restoreColumnsState(settings.value(QStringLiteral("TablesDockColumns")).toByteArray());
    if (m_changesDock)
        m_changesDock->restoreColumnsState(settings.value(QStringLiteral("ChangesDockColumns")).toByteArray());

    updateRecentFileMenu();

    updateRecentTableMenu();

    updateRecentProjectMenu();

    const bool autoLoadRecentFile = settings.value("AutoLoadRecentFile", true).toBool();

    if (autoLoadRecentFile)
    {
        const int tabCount  = settings.value(QStringLiteral("Session/TabCount"), 0).toInt();
        const int activeTab = settings.value(QStringLiteral("Session/ActiveTab"), 0).toInt();

        if (tabCount > 0)
        {
            // ---- New per-tab array format ----
            struct TabData {
                bool    isProject          = false;
                QString path;
                RomType romType            = RomType::Unknown;  // Preserved ROM type from last session
                qint64  cursor             = 0;
                bool    showPointers       = true;
                bool    showChanges        = false;
                bool    changesHexMode     = false;
                int     tablesActiveIndex  = -1;
                bool    dockTablesVisible       = true;
                bool    dockPointersVisible     = true;
                bool    dockChangesVisible      = true;
                bool    dockSectionsVisible     = true;
                bool    dockAudioVisible        = false;
                int     audioFormatIndex        = 0;
                QString audioSampleRate;
                double  audioPlaybackSpeed      = 1.0;
                bool    dockTablesCollapsed     = false;
                int     dockTablesExpW          = -1;
                int     dockTablesExpH          = -1;
                bool    dockPointersCollapsed   = false;
                int     dockPointersExpW        = -1;
                int     dockPointersExpH        = -1;
                bool    dockChangesCollapsed    = false;
                int     dockChangesExpW         = -1;
                int     dockChangesExpH         = -1;
                int     ptrSearchDir            = 2;
                bool    ptrExcludeSelection     = false;
                bool    ptrAlignedOnly          = false;
                bool    ptrOptimize             = false;
                QVector<int> sectionsExpandedGroupIds;
            };
            QVector<TabData> tabs;
            for (int i = 0; i < tabCount; ++i) {
                const QString pfx = QStringLiteral("Session/Tabs/") + QString::number(i);
                const QString type = settings.value(pfx + QStringLiteral("/type")).toString();
                const QString path = settings.value(pfx + QStringLiteral("/path")).toString();
                if (path.isEmpty() || !QFile::exists(path))
                    continue;
                TabData t;
                t.isProject          = (type == QStringLiteral("project"));
                t.path               = path;
                t.cursor             = settings.value(pfx + QStringLiteral("/cursor"), 0LL).toLongLong();
                t.showPointers       = settings.value(pfx + QStringLiteral("/showPointers"),   true).toBool();
                t.showChanges        = settings.value(pfx + QStringLiteral("/showChanges"),    false).toBool();
                t.changesHexMode     = settings.value(pfx + QStringLiteral("/changesHexMode"), false).toBool();
                t.tablesActiveIndex  = settings.value(pfx + QStringLiteral("/tablesActiveIndex"), -1).toInt();
                const int savedRomType = settings.value(pfx + QStringLiteral("/romType"), static_cast<int>(RomType::Unknown)).toInt();
                t.romType            = (savedRomType >= 0 && savedRomType < kRomTypeCount) ? static_cast<RomType>(savedRomType) : RomType::Unknown;
                t.dockTablesVisible    = settings.value(pfx + QStringLiteral("/dockTablesVisible"),    true).toBool();
                t.dockPointersVisible  = settings.value(pfx + QStringLiteral("/dockPointersVisible"),  true).toBool();
                t.dockChangesVisible   = settings.value(pfx + QStringLiteral("/dockChangesVisible"),   true).toBool();
                t.dockSectionsVisible  = settings.value(pfx + QStringLiteral("/dockSectionsVisible"),  true).toBool();
                t.dockAudioVisible     = settings.value(pfx + QStringLiteral("/dockAudioVisible"),     false).toBool();
                t.audioFormatIndex     = settings.value(pfx + QStringLiteral("/audioFormatIndex"),     0).toInt();
                t.audioSampleRate      = settings.value(pfx + QStringLiteral("/audioSampleRate")).toString();
                t.audioPlaybackSpeed   = settings.value(pfx + QStringLiteral("/audioPlaybackSpeed"),   1.0).toDouble();
                t.dockTablesCollapsed   = settings.value(pfx + QStringLiteral("/dockTablesCollapsed"),       false).toBool();
                t.dockTablesExpW        = settings.value(pfx + QStringLiteral("/dockTablesExpandedWidth"),   -1).toInt();
                t.dockTablesExpH        = settings.value(pfx + QStringLiteral("/dockTablesExpandedHeight"),  -1).toInt();
                t.dockPointersCollapsed = settings.value(pfx + QStringLiteral("/dockPointersCollapsed"),     false).toBool();
                t.dockPointersExpW      = settings.value(pfx + QStringLiteral("/dockPointersExpandedWidth"), -1).toInt();
                t.dockPointersExpH      = settings.value(pfx + QStringLiteral("/dockPointersExpandedHeight"),-1).toInt();
                t.dockChangesCollapsed  = settings.value(pfx + QStringLiteral("/dockChangesCollapsed"),      false).toBool();
                t.dockChangesExpW       = settings.value(pfx + QStringLiteral("/dockChangesExpandedWidth"),  -1).toInt();
                t.dockChangesExpH       = settings.value(pfx + QStringLiteral("/dockChangesExpandedHeight"), -1).toInt();
                t.ptrSearchDir          = settings.value(pfx + QStringLiteral("/ptrSearchDir"), 2).toInt();
                t.ptrExcludeSelection   = settings.value(pfx + QStringLiteral("/ptrExcludeSelection"), false).toBool();
                t.ptrAlignedOnly        = settings.value(pfx + QStringLiteral("/ptrAlignedOnly"), false).toBool();
                t.ptrOptimize           = settings.value(pfx + QStringLiteral("/ptrOptimize"), false).toBool();
                const QStringList expandedIds = settings.value(pfx + QStringLiteral("/sectionsExpandedGroups")).toStringList();
                for (const QString &id : expandedIds) {
                    bool ok = false;
                    const int gid = id.toInt(&ok);
                    if (ok)
                        t.sectionsExpandedGroupIds.append(gid);
                }
                tabs.append(t);
            }

            // Open all tabs; skip restoreProjectStateFromSettings during this loop.
            m_restoringSession = true;
            bool anyOpened = false;
            for (const TabData &t : std::as_const(tabs)) {
                if (t.isProject) {
                    if (!anyOpened) openProjectFile(t.path);
                    else            { createSession(); openProjectFile(t.path); }
                } else {
                    if (!anyOpened) loadFile(t.path, t.romType);
                    else            loadFileInNewTab(t.path, t.romType);
                }
                anyOpened = true;
            }
            m_restoringSession = false;

            // Apply saved per-tab state to each session in open order.
            int si = 0;
            for (int ti = 0; ti < tabs.size() && si < m_sessions.size(); ++ti, ++si) {
                EditorSession *s = m_sessions[si];
                const TabData &t = tabs[ti];

                // Cursor — defer scrolling until the tab's editor is shown
                if (t.cursor > 0) {
                    s->curOffset = t.cursor;
                    if (s->editor)
                        s->editor->setCursorPosition(t.cursor * 2);
                    if (s == m_currentSession)
                        curOffset = t.cursor;
                }
                s->scrollPending = true;

                // Per-tab display flags
                s->showPointers = t.showPointers;
                s->showChanges = t.showChanges;
                s->changesHexMode = t.changesHexMode;

                // Active table index
                if (t.tablesActiveIndex >= 0)
                    s->tableActiveIndex = t.tablesActiveIndex;

                // Dock visibility/state
                s->dockTablesVisible        = t.dockTablesVisible;
                s->dockPointersVisible      = t.dockPointersVisible;
                s->dockChangesVisible       = t.dockChangesVisible;
                s->dockSectionsVisible      = t.dockSectionsVisible;
                s->dockAudioVisible         = t.dockAudioVisible;
                s->audioFormatIndex         = t.audioFormatIndex;
                s->audioSampleRate          = t.audioSampleRate;
                s->audioPlaybackSpeed       = t.audioPlaybackSpeed;
                s->dockVisibilityInitialized = true;
                s->dockTablesCollapsed      = t.dockTablesCollapsed;
                s->dockTablesExpandedWidth  = t.dockTablesExpW;
                s->dockTablesExpandedHeight = t.dockTablesExpH;
                s->dockPointersCollapsed      = t.dockPointersCollapsed;
                s->dockPointersExpandedWidth  = t.dockPointersExpW;
                s->dockPointersExpandedHeight = t.dockPointersExpH;
                s->dockChangesCollapsed      = t.dockChangesCollapsed;
                s->dockChangesExpandedWidth  = t.dockChangesExpW;
                s->dockChangesExpandedHeight = t.dockChangesExpH;
                s->ptrSearchDir              = t.ptrSearchDir;
                s->ptrExcludeSelection       = t.ptrExcludeSelection;
                s->ptrAlignedOnly            = t.ptrAlignedOnly;
                s->ptrOptimize               = t.ptrOptimize;
                s->sectionsExpandedGroupIds  = t.sectionsExpandedGroupIds;
                s->dockStateInitialized = true;
            }

            // After the apply loop the EditorSession fields for the CURRENT session
            // hold the restored values, but the live dock widgets still reflect the
            // default state set during the last loadFile/loadFileInNewTab call.
            // Sync the live dock widgets to the current session's restored state NOW
            // so that the saveCurrentSession() call inside onTabChanged() (triggered
            // by setCurrentIndex below) captures the correct restored values instead
            // of overwriting them with the stale live state.
            if (m_currentSession && m_currentSession->dockStateInitialized) {
                auto fixLiveDock = [](BaseDockWidget *d, bool collapsed, int expW, int expH) {
                    if (!d) return;
                    if (expW > 0 || expH > 0) d->setExpandedSize(expW, expH);
                    if (collapsed != d->isCollapsed()) d->setCollapsed(collapsed);
                };
                fixLiveDock(m_tablesDock,
                            m_currentSession->dockTablesCollapsed,
                            m_currentSession->dockTablesExpandedWidth,
                            m_currentSession->dockTablesExpandedHeight);
                fixLiveDock(m_pointersDock,
                            m_currentSession->dockPointersCollapsed,
                            m_currentSession->dockPointersExpandedWidth,
                            m_currentSession->dockPointersExpandedHeight);
                fixLiveDock(m_changesDock,
                            m_currentSession->dockChangesCollapsed,
                            m_currentSession->dockChangesExpandedWidth,
                            m_currentSession->dockChangesExpandedHeight);
                fixLiveDock(m_sectionsDock,
                            m_currentSession->dockSectionsCollapsed,
                            m_currentSession->dockSectionsExpandedWidth,
                            m_currentSession->dockSectionsExpandedHeight);

                m_tablesDock->setVisible(m_currentSession->dockTablesVisible);
                m_pointersDock->setVisible(m_currentSession->dockPointersVisible);
                m_changesDock->setVisible(m_currentSession->dockChangesVisible);
                m_sectionsDock->setVisible(m_currentSession->dockSectionsVisible);
                if (m_audioDock) {
                    m_audioDock->setVisible(m_currentSession->dockAudioVisible);
                    m_audioDock->setFormatIndex(m_currentSession->audioFormatIndex);
                    if (!m_currentSession->audioSampleRate.isEmpty())
                        m_audioDock->setSampleRateText(m_currentSession->audioSampleRate);
                    m_audioDock->setPlaybackSpeed(m_currentSession->audioPlaybackSpeed);
                }

                if (showPointersAct)
                    showPointersAct->setChecked(m_currentSession->showPointers);
                if (hexEdit)
                    hexEdit->setShowPointers(m_currentSession->showPointers);
                if (showChangesAct)
                    showChangesAct->setChecked(m_currentSession->showChanges);
                if (m_changesDock) {
                    m_changesDock->setShowChangesChecked(m_currentSession->showChanges);
                    m_changesDock->setHexMode(m_currentSession->changesHexMode);
                }
                if (hexEdit)
                    hexEdit->setShowChanges(m_currentSession->showChanges);
            }

            // Switch to previously active tab and apply its dock state.
            if (anyOpened) {
                const int target = qBound(0, activeTab, m_tabWidget->count() - 1);
                if (m_tabWidget->currentIndex() != target)
                    m_tabWidget->setCurrentIndex(target);  // triggers onTabChanged → restoreSession
                else if (target < m_sessions.size())
                    restoreSession(m_sessions[target]);    // already on this tab; force apply
            }
        }
        else
        {
            // ---- Legacy fallback (old Session/Tabs QStringList format or last file) ----
            const QStringList sessionTabs = settings.value(QStringLiteral("Session/Tabs")).toStringList();
            const int legacyActiveTab = settings.value(QStringLiteral("Session/ActiveTab"), 0).toInt();

            if (!sessionTabs.isEmpty()) {
                bool anyOpened = false;
                for (const QString &entry : sessionTabs) {
                    if (entry.startsWith(QStringLiteral("project:"))) {
                        const QString path = entry.mid(8);
                        if (!path.isEmpty() && QFile::exists(path)) {
                            if (!anyOpened) openProjectFile(path);
                            else { createSession(); openProjectFile(path); }
                            anyOpened = true;
                        }
                    } else if (entry.startsWith(QStringLiteral("file:"))) {
                        const QString path = entry.mid(5);
                        if (!path.isEmpty() && QFile::exists(path)) {
                            if (!anyOpened) loadFile(path);
                            else            loadFileInNewTab(path);
                            anyOpened = true;
                        }
                    }
                }
                if (anyOpened && legacyActiveTab >= 0 && legacyActiveTab < m_tabWidget->count())
                    m_tabWidget->setCurrentIndex(legacyActiveTab);
            } else {
                // Last resort: single most-recently-used file/project
                const QString lastProjectFile = settings.value("LastProjectFile").toString();
                const QString fileName        = settings.value("RecentFile0").toString();
                const int recentRomType = settings.value("RecentFile0RomType", static_cast<int>(RomType::Unknown)).toInt();
                const RomType suggestedRecentRomType =
                    (recentRomType >= 0 && recentRomType < kRomTypeCount)
                        ? static_cast<RomType>(recentRomType)
                        : RomType::Unknown;
                if (!lastProjectFile.isEmpty() && QFile::exists(lastProjectFile))
                    openProjectFile(lastProjectFile);
                else if (!fileName.isEmpty() && QFile::exists(fileName))
                    loadFile(fileName, suggestedRecentRomType);
            }
        }
    }

    applyShortcutsFromSettings();
    
    // Load and apply the language translator
    auto &settingsForLang = AppSettings::instance();
    const QString language = settingsForLang.value("Language", QStringLiteral("en")).toString();
    applyLanguage(language);
}


void MainWindow::applyShortcutsFromSettings()
{
    auto &s = AppSettings::instance();
    openAct->setShortcut(s.value("hotkey_Open",         QKeySequence(QKeySequence::Open)).value<QKeySequence>());
    saveAct->setShortcut(s.value("hotkey_Save",          QKeySequence(QKeySequence::Save)).value<QKeySequence>());
    saveAsAct->setShortcut(s.value("hotkey_SaveAs",      QKeySequence(QKeySequence::SaveAs)).value<QKeySequence>());
    closeAct->setShortcut(s.value("hotkey_Close",        QKeySequence(QKeySequence::Close)).value<QKeySequence>());
    undoAct->setShortcut(s.value("hotkey_Undo",          QKeySequence(QKeySequence::Undo)).value<QKeySequence>());
    redoAct->setShortcut(s.value("hotkey_Redo",          QKeySequence(QKeySequence::Redo)).value<QKeySequence>());
    cutAct->setShortcut(s.value("hotkey_Cut",            QKeySequence(QKeySequence::Cut)).value<QKeySequence>());
    copyAct->setShortcut(s.value("hotkey_Copy",          QKeySequence(QKeySequence::Copy)).value<QKeySequence>());
    pasteAct->setShortcut(s.value("hotkey_Paste",        QKeySequence(QKeySequence::Paste)).value<QKeySequence>());
    findAct->setShortcut(s.value("hotkey_Find",          QKeySequence(QKeySequence::Find)).value<QKeySequence>());
    gotoAct->setShortcut(s.value("hotkey_Goto",          QKeySequence(QKeySequence::FindNext)).value<QKeySequence>());
    newAct->setShortcut(s.value("hotkey_New",             QKeySequence(Qt::CTRL | Qt::Key_T)).value<QKeySequence>());
    useTableAct->setShortcut(QKeySequence()); // no shortcut — Ctrl+T is reserved for new file
    findPointersAct->setShortcut(s.value("hotkey_FindPointers", QKeySequence(QKeySequence::New)).value<QKeySequence>());
    dumpScriptAct->setShortcut(s.value("hotkey_EditScript", QKeySequence(Qt::CTRL | Qt::Key_E)).value<QKeySequence>());
    if (previousPositionAct)
        previousPositionAct->setShortcut(s.value("hotkey_PrevPos",
            QKeySequence(Qt::CTRL | Qt::Key_BracketLeft)).value<QKeySequence>());
    if (nextPositionAct)
        nextPositionAct->setShortcut(s.value("hotkey_NextPos",
            QKeySequence(Qt::CTRL | Qt::Key_BracketRight)).value<QKeySequence>());
}

void MainWindow::switchShowPointers()
{
    if (!hexEdit || !showPointersAct)
        return;

    const bool show = showPointersAct->isChecked();
    hexEdit->setShowPointers(show);
    if (m_currentSession)
        m_currentSession->showPointers = show;
}

void MainWindow::pushNavigationPosition(qint64 position)
{
    if (navigationJumpInProgress || position < 0)
        return;

    if (navigationHistoryIndex >= 0
        && navigationHistoryIndex < navigationHistory.size()
        && navigationHistory[navigationHistoryIndex] == position)
    {
        return;
    }

    if (navigationHistoryIndex + 1 < navigationHistory.size())
        navigationHistory.resize(navigationHistoryIndex + 1);

    navigationHistory.append(position);

    if (navigationHistory.size() > 1024)
        navigationHistory.remove(0, navigationHistory.size() - 1024);

    navigationHistoryIndex = navigationHistory.size() - 1;
    updateNavigationActions();
}

void MainWindow::resetNavigationHistory()
{
    navigationHistory.clear();
    navigationHistoryIndex = -1;
    pushNavigationPosition(hexEdit ? hexEdit->getCurrentOffset() : 0);
}

void MainWindow::navigateToHistoryIndex(int index)
{
    if (index < 0 || index >= navigationHistory.size())
        return;

    navigationHistoryIndex = index;
    navigationJumpInProgress = true;
    hexEdit->jumpTo(navigationHistory[index]);
    navigationJumpInProgress = false;
    updateNavigationActions();
}

void MainWindow::updateNavigationActions()
{
    const bool hasHistory = !navigationHistory.isEmpty();
    const bool hasPrev = hasHistory && navigationHistoryIndex > 0;
    const bool hasNext = hasHistory && navigationHistoryIndex >= 0 && navigationHistoryIndex < navigationHistory.size() - 1;

    if (previousPositionAct)
        previousPositionAct->setEnabled(hasPrev);
    if (nextPositionAct)
        nextPositionAct->setEnabled(hasNext);
    if (firstPositionAct)
        firstPositionAct->setEnabled(hasPrev);
    if (lastPositionAct)
        lastPositionAct->setEnabled(hasNext);
    if (toolbarPreviousPositionAct)
        toolbarPreviousPositionAct->setEnabled(hasPrev);
    if (toolbarNextPositionAct)
        toolbarNextPositionAct->setEnabled(hasNext);
    if (toolbarFirstPositionAct)
        toolbarFirstPositionAct->setEnabled(hasPrev);
    if (toolbarLastPositionAct)
        toolbarLastPositionAct->setEnabled(hasNext);
    if (toFileBeginningAct)
        toFileBeginningAct->setEnabled(hexEdit && hexEdit->dataSize() > 0);
    if (toFileEndAct)
        toFileEndAct->setEnabled(hexEdit && hexEdit->dataSize() > 0);
}


void MainWindow::toggleDarkTheme(bool enabled)
{
    applyDarkTheme(enabled);
    auto &settings = AppSettings::instance();
    settings.setValue("DarkTheme", enabled);
}

void MainWindow::applyDarkTheme(bool enabled)
{
#ifdef Q_OS_MAC
    setMacOSDarkMode(enabled);
#else
    if (!m_lightPaletteCaptured) {
        m_lightPalette = qApp->palette();
        m_lightStyleName = qApp->style()->objectName();
        m_lightPaletteCaptured = true;
    }

    if (enabled) {
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        QPalette dark;
        dark.setColor(QPalette::Window, QColor(53, 53, 53));
        dark.setColor(QPalette::WindowText, Qt::white);
        dark.setColor(QPalette::Base, QColor(35, 35, 35));
        dark.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
        dark.setColor(QPalette::ToolTipBase, QColor(53, 53, 53));
        dark.setColor(QPalette::ToolTipText, Qt::white);
        dark.setColor(QPalette::Text, Qt::white);
        dark.setColor(QPalette::Button, QColor(53, 53, 53));
        dark.setColor(QPalette::ButtonText, Qt::white);
        dark.setColor(QPalette::BrightText, Qt::red);
        dark.setColor(QPalette::Light, QColor(80, 80, 80));
        dark.setColor(QPalette::Midlight, QColor(70, 70, 70));
        dark.setColor(QPalette::Mid, QColor(50, 50, 50));
        dark.setColor(QPalette::Dark, QColor(35, 35, 35));
        dark.setColor(QPalette::Shadow, QColor(20, 20, 20));
        dark.setColor(QPalette::Highlight, QColor(42, 130, 218));
        dark.setColor(QPalette::HighlightedText, Qt::black);
        qApp->setPalette(dark);
        qApp->setStyleSheet(QStringLiteral(
            "QToolTip { color: #ffffff; background-color: #353535; border: 1px solid #555555; }"
        ));
    } else {
        if (!m_lightStyleName.isEmpty())
            QApplication::setStyle(QStyleFactory::create(m_lightStyleName));
        qApp->setPalette(m_lightPalette);
        qApp->setStyleSheet(QString());
    }
#endif
    if (m_tabWidget)
        m_tabWidget->update();
    if (statusBar())
        statusBar()->update();
    if (hexEdit)
        hexEdit->update();
}


void MainWindow::updateHexEditorSettings()
{
    // Apply settings to all open editors
    for (EditorSession *session : m_sessions) {
        HexEditor *editor = session->editor;
        if (!editor)
            continue;

        const qint64 savedCursorPos = editor->cursorPosition();
        const int savedBytesPerLine = qMax(1, editor->bytesPerLine());
        const qint64 savedTopByte = static_cast<qint64>(editor->verticalScrollBar()->value()) * savedBytesPerLine;
        const int savedHorizontal = editor->horizontalScrollBar()->value();

        auto &settings = AppSettings::instance();

        editor->setAddressArea(settings.value("AddressArea", true).toBool());
        {
            const QString pm = settings.value("PanelMode").toString();
            if (pm == QLatin1String("disasm")) {
                editor->setAsciiArea(true);
                editor->setShowDisasm(true);
            } else if (pm == QLatin1String("graphics") || pm == QLatin1String("sound")) {
                editor->setAsciiArea(true);
                editor->setShowDisasm(false);
            } else {
                editor->setAsciiArea(settings.value("AsciiArea", true).toBool());
                editor->setShowDisasm(false);
            }
        }
        editor->setHighlighting(true);
        editor->setHighlightingColor(settings.value("HighlightingColor").value<QColor>());
        editor->setPointedColor(settings.value("PointedColor").value<QColor>());
        editor->setPointedFontColor(settings.value("PointedFontColor", QColor(Qt::black)).value<QColor>());
        editor->setPointerFontColor(settings.value("PointerFontColor", QColor(Qt::black)).value<QColor>());
        editor->setPointerFrameColor(settings.value("PointerFrameColor", QColor(0x00, 0x00, 0xFF)).value<QColor>());
        editor->setPointerFrameBackgroundColor(settings.value("PointerFrameBgColor", QColor(0x00, 0xFF, 0x00, 0x80)).value<QColor>());
        editor->setAddressAreaColor(settings.value("AddressAreaColor").value<QColor>());
        editor->setSelectionColor(settings.value("SelectionColor").value<QColor>());
        editor->setFont(settings.value("WidgetFont").value<QFont>());
        editor->setAddressFontColor(settings.value("AddressFontColor").value<QColor>());
        editor->setAddressZeroByteFontColor(settings.value("AddressZeroByteFontColor", settings.value("AddressFontColor").value<QColor>()).value<QColor>());
        editor->setAsciiAreaColor(settings.value("AsciiAreaColor").value<QColor>());
        editor->setAsciiFontColor(settings.value("AsciiFontColor").value<QColor>());
        editor->setHexFontColor(settings.value("HexFontColor").value<QColor>());
        editor->setNonPrintableNoTableChar(readSingleCharSetting(settings, "NonPrintableNoTableChar", QChar(0x25AA)));
        editor->setNotInTableChar(readSingleCharSetting(settings, "NotInTableChar", QChar(0x25A1)));
        editor->setAddressWidth(settings.value("AddressAreaWidth").toInt());
        editor->setBytesPerLine(settings.value("BytesPerLine", 32).toInt());
        editor->setDynamicBytesPerLine(settings.value("Autosize", true).toBool());
        editor->setShowHexGrid(settings.value("ShowHexGrid", true).toBool());
        editor->setShowMultibyteFrame(settings.value("ShowMultibyteFrame", true).toBool());
        editor->setHexAreaBackgroundColor(settings.value("HexAreaBackgroundColor", QColor(Qt::white)).value<QColor>());
        editor->setHexAreaGridColor(settings.value("HexAreaGridColor", QColor(0x99, 0x99, 0x99)).value<QColor>());
        editor->setMultibyteFrameColor(settings.value("MultibyteFrameColor", QColor(0x20, 0x20, 0x20)).value<QColor>());
        editor->setCursorCharColor(settings.value("CursorCharColor", QColor(0x00, 0x60, 0xFF, 0x80)).value<QColor>());
        editor->setCursorFrameColor(settings.value("CursorFrameColor", QColor(Qt::black)).value<QColor>());
        editor->setZeroByteFontColor(settings.value("ZeroByteFontColor", QColor(0xCC, 0xCC, 0xCC)).value<QColor>());
        editor->setChangesColor(settings.value("ChangesColor", QColor(0x99, 0xff, 0x99, 0xff)).value<QColor>());
        editor->setSectionHeaderFontColor(settings.value("SectionHeaderFontColor", qApp->palette().color(QPalette::WindowText)).value<QColor>());
        editor->setSectionHeaderBackgroundColor(settings.value("SectionHeaderBgColor", QColor(0xD8, 0xD8, 0xD8, 0x90)).value<QColor>());
        {
            QFont sectionHeaderFont = settings.value("SectionHeaderFont", editor->font()).value<QFont>();
            if (!sectionHeaderFont.bold())
                sectionHeaderFont.setBold(true);
            editor->setSectionHeaderFont(sectionHeaderFont);
        }
        editor->setScrollMapChangesBgColor(settings.value("ScrollMapPtrBgColor", QColor(0xd0, 0xd0, 0xd0)).value<QColor>());
        editor->setScrollMapTargetBgColor(settings.value("ScrollMapTargetBgColor", QColor(0xd0, 0xd0, 0xd0)).value<QColor>());

        const int newBytesPerLine = qMax(1, editor->bytesPerLine());
        editor->verticalScrollBar()->setValue(static_cast<int>(savedTopByte / newBytesPerLine));
        editor->horizontalScrollBar()->setValue(savedHorizontal);
        editor->setCursorPosition(savedCursorPos);
        editor->viewport()->update();
    }

    if (showAddressAreaAct && hexEdit)
        showAddressAreaAct->setChecked(hexEdit->addressArea());
    if (panelModeTextAct && hexEdit) {
        auto &s = AppSettings::instance();
        const QString pm = s.value("PanelMode").toString();
        if (pm == QLatin1String("disasm"))
            panelModeDisasmAct->setChecked(true);
        else if (pm == QLatin1String("graphics")) {
            panelModeGraphicsAct->setChecked(true);
            hexEdit->setShowGraphicsPanel(true);
        }
        else
            panelModeTextAct->setChecked(true);
    }
    if (showAddressGridAct && hexEdit)
        showAddressGridAct->setChecked(hexEdit->showHexGrid());
}


void MainWindow::writeSettings()
{
    auto &settings = AppSettings::instance();

    // Save window geometry
    settings.setValue("WindowGeometry", saveGeometry());
    settings.setValue(kMainWindowStateKey, saveState());

    // Save dock column states
    if (m_pointersDock)
        settings.setValue(QStringLiteral("PointersDockColumns"), m_pointersDock->saveColumnsState());
    if (m_tablesDock)
        settings.setValue(QStringLiteral("TablesDockColumns"), m_tablesDock->saveColumnsState());
    if (m_changesDock)
        settings.setValue(QStringLiteral("ChangesDockColumns"), m_changesDock->saveColumnsState());

    // Save hex editor settings to ensure persisted state
    if (hexEdit)
    {
        settings.setValue("AddressArea", hexEdit->addressArea());
        settings.setValue("AsciiArea", hexEdit->asciiArea());
        settings.setValue("OverwriteMode", hexEdit->overwriteMode());
        settings.setValue("ShowHexGrid", hexEdit->showHexGrid());
        settings.setValue("Autosize", hexEdit->dynamicBytesPerLine());
        settings.setValue("HexCaps", hexEdit->hexCaps());
        settings.setValue("AddressAreaWidth", hexEdit->addressWidth());
        settings.setValue("BytesPerLine", hexEdit->bytesPerLine());
    }

    // Save all open tabs as a per-tab state array.
    // Flush the current session's live state (cursor, dock collapse, etc.) first.
    saveCurrentSession();

    // Clear stale tab data from the previous run, then write fresh.
    settings.remove(QStringLiteral("Session"));

    int savedTabCount = 0;
    for (int i = 0; i < m_sessions.size(); ++i) {
        const EditorSession *s = m_sessions[i];
        const QString projPath = s->document ? s->document->projectFilePath : QString();
        const bool isProject = !projPath.isEmpty() && QFile::exists(projPath);
        const bool isFile    = !isProject && !s->curFile.isEmpty() && !s->isUntitled
                               && QFile::exists(s->curFile);
        if (!isProject && !isFile)
            continue;

        const QString pfx = QStringLiteral("Session/Tabs/") + QString::number(savedTabCount);
        settings.setValue(pfx + QStringLiteral("/type"),   isProject ? QStringLiteral("project")
                                                                      : QStringLiteral("file"));
        settings.setValue(pfx + QStringLiteral("/path"),   isProject ? projPath : s->curFile);
        settings.setValue(pfx + QStringLiteral("/cursor"), s->curOffset);

        settings.setValue(pfx + QStringLiteral("/showPointers"),   s->showPointers);
        settings.setValue(pfx + QStringLiteral("/showChanges"),    s->showChanges);
        settings.setValue(pfx + QStringLiteral("/changesHexMode"), s->changesHexMode);
        settings.setValue(pfx + QStringLiteral("/tablesActiveIndex"), s->tableActiveIndex);
        settings.setValue(pfx + QStringLiteral("/romType"), static_cast<int>(s->detectedRomType));

        settings.setValue(pfx + QStringLiteral("/dockTablesVisible"),   s->dockTablesVisible);
        settings.setValue(pfx + QStringLiteral("/dockPointersVisible"), s->dockPointersVisible);
        settings.setValue(pfx + QStringLiteral("/dockChangesVisible"),  s->dockChangesVisible);
        settings.setValue(pfx + QStringLiteral("/dockSectionsVisible"), s->dockSectionsVisible);
        settings.setValue(pfx + QStringLiteral("/dockAudioVisible"),    s->dockAudioVisible);
        settings.setValue(pfx + QStringLiteral("/audioFormatIndex"),    s->audioFormatIndex);
        settings.setValue(pfx + QStringLiteral("/audioSampleRate"),     s->audioSampleRate);
        settings.setValue(pfx + QStringLiteral("/audioPlaybackSpeed"),  s->audioPlaybackSpeed);
        settings.setValue(pfx + QStringLiteral("/dockTablesCollapsed"),       s->dockTablesCollapsed);

        if (isProject) {
            const QString projectPfx = projectUiSettingsPrefix(projPath);
            settings.setValue(projectPfx + QStringLiteral("/dockTablesVisible"), s->dockTablesVisible);
            settings.setValue(projectPfx + QStringLiteral("/dockPointersVisible"), s->dockPointersVisible);
            settings.setValue(projectPfx + QStringLiteral("/dockChangesVisible"), s->dockChangesVisible);
            settings.setValue(projectPfx + QStringLiteral("/dockSectionsVisible"), s->dockSectionsVisible);
            settings.setValue(projectPfx + QStringLiteral("/dockAudioVisible"), s->dockAudioVisible);
        }
        settings.setValue(pfx + QStringLiteral("/dockTablesExpandedWidth"),   s->dockTablesExpandedWidth);
        settings.setValue(pfx + QStringLiteral("/dockTablesExpandedHeight"),  s->dockTablesExpandedHeight);
        settings.setValue(pfx + QStringLiteral("/dockPointersCollapsed"),     s->dockPointersCollapsed);
        settings.setValue(pfx + QStringLiteral("/dockPointersExpandedWidth"), s->dockPointersExpandedWidth);
        settings.setValue(pfx + QStringLiteral("/dockPointersExpandedHeight"),s->dockPointersExpandedHeight);
        settings.setValue(pfx + QStringLiteral("/dockChangesCollapsed"),      s->dockChangesCollapsed);
        settings.setValue(pfx + QStringLiteral("/dockChangesExpandedWidth"),  s->dockChangesExpandedWidth);
        settings.setValue(pfx + QStringLiteral("/dockChangesExpandedHeight"), s->dockChangesExpandedHeight);
        settings.setValue(pfx + QStringLiteral("/ptrSearchDir"), s->ptrSearchDir);
        settings.setValue(pfx + QStringLiteral("/ptrExcludeSelection"), s->ptrExcludeSelection);
        settings.setValue(pfx + QStringLiteral("/ptrAlignedOnly"), s->ptrAlignedOnly);
        settings.setValue(pfx + QStringLiteral("/ptrOptimize"), s->ptrOptimize);
        QStringList expandedIds;
        expandedIds.reserve(s->sectionsExpandedGroupIds.size());
        for (int gid : s->sectionsExpandedGroupIds)
            expandedIds.append(QString::number(gid));
        settings.setValue(pfx + QStringLiteral("/sectionsExpandedGroups"), expandedIds);

        ++savedTabCount;
    }
    settings.setValue(QStringLiteral("Session/TabCount"),
                      savedTabCount);
    // Use m_closingActiveTab when called from closeEvent (where the tab-switch
    // loop changes currentIndex), otherwise fall back to the live current index.
    const int activeTabToSave = (m_closingActiveTab >= 0)
                                ? m_closingActiveTab
                                : (m_tabWidget->isVisible() ? m_tabWidget->currentIndex() : 0);
    settings.setValue(QStringLiteral("Session/ActiveTab"), activeTabToSave);

    settings.sync();
}

QString MainWindow::lastDirectory(const QString &settingsKey) const
{
    auto &settings = AppSettings::instance();
    const QString dir = settings.value(settingsKey).toString();
    return dir.isEmpty() ? QDir::homePath() : dir;
}

void MainWindow::rememberDirectory(const QString &settingsKey, const QString &filePath)
{
    if (filePath.isEmpty())
        return;

    const QString dirPath = QFileInfo(filePath).absolutePath();
    if (dirPath.isEmpty())
        return;

    auto &settings = AppSettings::instance();
    settings.setValue(settingsKey, dirPath);
}


void MainWindow::saveProjectDockVisibilityState() const
{
    if (!m_document || m_document->projectFilePath.isEmpty())
        return;

    auto &settings = AppSettings::instance();
    const QString pfx = projectUiSettingsPrefix(m_document->projectFilePath);
    settings.setValue(pfx + QStringLiteral("/dockTablesVisible"), m_tablesDock ? m_tablesDock->isVisible() : true);
    settings.setValue(pfx + QStringLiteral("/dockPointersVisible"), m_pointersDock ? m_pointersDock->isVisible() : true);
    settings.setValue(pfx + QStringLiteral("/dockChangesVisible"), m_changesDock ? m_changesDock->isVisible() : true);
    settings.setValue(pfx + QStringLiteral("/dockSectionsVisible"), m_sectionsDock ? m_sectionsDock->isVisible() : true);
    settings.setValue(pfx + QStringLiteral("/dockAudioVisible"), m_audioDock ? m_audioDock->isVisible() : false);
}

void MainWindow::restoreProjectDockVisibilityState(const QString &projectPath)
{
    if (projectPath.isEmpty())
        return;

    auto &settings = AppSettings::instance();
    const QString pfx = projectUiSettingsPrefix(projectPath);
    const bool tablesVisible = settings.value(pfx + QStringLiteral("/dockTablesVisible"), true).toBool();
    const bool pointersVisible = settings.value(pfx + QStringLiteral("/dockPointersVisible"), true).toBool();
    const bool changesVisible = settings.value(pfx + QStringLiteral("/dockChangesVisible"), true).toBool();
    const bool sectionsVisible = settings.value(pfx + QStringLiteral("/dockSectionsVisible"), true).toBool();
    const bool audioVisible = settings.value(pfx + QStringLiteral("/dockAudioVisible"), false).toBool();

    if (m_tablesDock)
        m_tablesDock->setVisible(tablesVisible);
    if (m_pointersDock)
        m_pointersDock->setVisible(pointersVisible);
    if (m_changesDock)
        m_changesDock->setVisible(changesVisible);
    if (m_sectionsDock)
        m_sectionsDock->setVisible(sectionsVisible);
    if (m_audioDock)
        m_audioDock->setVisible(audioVisible);

    if (m_currentSession) {
        m_currentSession->dockTablesVisible = tablesVisible;
        m_currentSession->dockPointersVisible = pointersVisible;
        m_currentSession->dockChangesVisible = changesVisible;
        m_currentSession->dockSectionsVisible = sectionsVisible;
        m_currentSession->dockAudioVisible = audioVisible;
        m_currentSession->dockVisibilityInitialized = true;
    }
}


void MainWindow::addToRecentFiles(const QString &fileName)
{
    if (fileName.isEmpty())
        return;

    auto &settings = AppSettings::instance();
    QStringList files = settings.value(kRecentFilesKey).toStringList();

    files.removeAll(fileName);
    files.prepend(fileName);

    while (files.size() > kMaxRecentFiles)
        files.removeLast();

    settings.setValue(kRecentFilesKey, files);
    updateRecentFileMenu();
}

void MainWindow::addToRecentTables(const QString &fileName)
{
    if (fileName.isEmpty())
        return;

    auto &settings = AppSettings::instance();
    QStringList files = settings.value(kRecentTablesKey).toStringList();

    files.removeAll(fileName);
    files.prepend(fileName);

    while (files.size() > kMaxRecentTables)
        files.removeLast();

    settings.setValue(kRecentTablesKey, files);
    updateRecentTableMenu();
}

void MainWindow::updateRecentFileMenu()
{
    auto &settings = AppSettings::instance();
    QStringList files = settings.value(kRecentFilesKey).toStringList();

    recentFileMenu->clear();

    if (files.isEmpty())
    {
        recentFileMenu->setEnabled(false);
        return;
    }

    recentFileMenu->setEnabled(true);

    for (int i = 0; i < files.size(); ++i)
    {
        const QString fileName = files[i];
        const QString text = QStringLiteral("&%1 %2").arg(i + 1).arg(QFileInfo(fileName).fileName());

        QAction *action = recentFileMenu->addAction(text);
        action->setData(fileName);
        connect(action, &QAction::triggered, this, &MainWindow::openRecentFile);
    }
}

void MainWindow::updateRecentTableMenu()
{
    auto &settings = AppSettings::instance();
    QStringList files = settings.value(kRecentTablesKey).toStringList();

    recentTableMenu->clear();

    if (files.isEmpty())
    {
        recentTableMenu->setEnabled(false);
        return;
    }

    recentTableMenu->setEnabled(true);

    for (int i = 0; i < files.size(); ++i)
    {
        const QString fileName = files[i];
        const QString text = QStringLiteral("&%1 %2").arg(i + 1).arg(QFileInfo(fileName).fileName());

        QAction *action = recentTableMenu->addAction(text);
        action->setData(fileName);
        connect(action, &QAction::triggered, this, &MainWindow::openRecentTable);
    }
}

void MainWindow::openRecentFile()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action)
        return;

    const QString fileName = action->data().toString();
    if (!fileName.isEmpty()) {
        if (m_sessions.isEmpty() || !isUntitled || hexEdit->isModified()) {
            loadFileInNewTab(fileName);
        } else {
            loadFile(fileName);
        }
    }
}

void MainWindow::openRecentTable()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action)
        return;

    const QString fileName = action->data().toString();
    if (!fileName.isEmpty())
    {
        // Check if file still exists
        if (!QFile::exists(fileName))
        {
            QMessageBox::warning(this, tr("Error"), tr("File not found: %1").arg(fileName));
            return;
        }

        try
        {
            bool encodingAccepted = true;
            const QString importEncoding = chooseTableImportEncoding(this, fileName, &encodingAccepted);
            if (!encodingAccepted)
                return;

            const TranslationTable newTable(fileName, importEncoding);
            m_tablesDock->addTable(QFileInfo(fileName).completeBaseName(), &newTable);
            m_tablesDock->show();
            tb = m_tablesDock->currentTable();
            useTableAct->setEnabled(true);
            useTableAct->setChecked(true);
            editTableAct->setEnabled(true);
            saveTableAct->setEnabled(true);
            saveTableAsAct->setEnabled(true);
            hexEdit->setTranslationTable(tb);
            tableFilePath = fileName;
            statusBar()->showMessage(tr("Table loaded"), 2000);
        }
        catch (const std::exception &e)
        {
            QMessageBox::warning(this, tr("Error"), tr("Failed to load table: %1").arg(QString::fromStdString(e.what())));
        }
    }
}

void MainWindow::addToRecentProjects(const QString &fileName)
{
    if (fileName.isEmpty())
        return;

    auto &settings = AppSettings::instance();
    QStringList files = settings.value(kRecentProjectsKey).toStringList();

    files.removeAll(fileName);
    files.prepend(fileName);

    while (files.size() > kMaxRecentProjects)
        files.removeLast();

    settings.setValue(kRecentProjectsKey, files);
    updateRecentProjectMenu();
}

void MainWindow::updateRecentProjectMenu()
{
    auto &settings = AppSettings::instance();
    QStringList files = settings.value(kRecentProjectsKey).toStringList();

    // Filter out the currently open project
    const QString currentProject = m_document ? m_document->projectFilePath : QString();

    recentProjectMenu->clear();

    // Build filtered list (exclude current project)
    QStringList displayFiles;
    for (const QString &f : files) {
        if (!f.isEmpty() && f != currentProject)
            displayFiles.append(f);
    }

    if (displayFiles.isEmpty())
    {
        recentProjectMenu->setEnabled(false);
        return;
    }

    recentProjectMenu->setEnabled(true);

    for (int i = 0; i < displayFiles.size(); ++i)
    {
        const QString fileName = displayFiles[i];
        const QString text = QStringLiteral("&%1 %2").arg(i + 1).arg(QFileInfo(fileName).fileName());

        QAction *action = recentProjectMenu->addAction(text);
        action->setData(fileName);
        connect(action, &QAction::triggered, this, &MainWindow::openRecentProject);
    }
}

void MainWindow::openRecentProject()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action)
        return;

    const QString fileName = action->data().toString();
    if (!fileName.isEmpty())
    {
        if (!QFile::exists(fileName))
        {
            QMessageBox::warning(this, tr("Error"), tr("File not found: %1").arg(fileName));
            return;
        }
        // Open in new tab if current has content
        if (m_sessions.isEmpty() || !isUntitled || (hexEdit && hexEdit->isModified()))
            createSession();
        openProjectFile(fileName);
    }
}


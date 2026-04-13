#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QByteArray>
#include <QVector>
#include <QPalette>

#include "langtranslator.h"
#include "translationtable.h"
#include "hexeditor/hexeditor.h"
#include "romdetect.h"
#include "optionsdialog.h"
#include "searchdialog.h"
#include "pointersdialog.h"
#include "JumpToDialog.h"
#include "TablesDockWidget.h"
#include "PointersDockWidget.h"
#include "ChangesDockWidget.h"
#include "SectionsDockWidget.h"
#include "SemiAutoTableDialog.h"
#include "DumpScriptdialog.h"
#include "InsertScriptDialog.h"
#include "hexdocument.h"
#include "editorsession.h"

QT_BEGIN_NAMESPACE
class QAction;
class QActionGroup;
class QMenu;
class QUndoStack;
class QComboBox;
class QLabel;
class QPushButton;
class QTimer;
class QDragEnterEvent;
class QDropEvent;
class QTabWidget;
QT_END_NAMESPACE

class SectionListModel;
class UpdateChecker;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool event(QEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void about();
    void checkForUpdates();
    void onUpdateAvailable(const QString &version, const QString &url, const QString &notes);
    void onUpToDate();
    void onUpdateCheckFailed(const QString &error);
    void dataChanged();
    void onHexDataChangedAt(qint64 offset);
    void flushChangesUiUpdate();
    void newFile();
    void closeFile();
    void open();
    void optionsAccepted();
    void findNext();
    void showJumpToDialog();
    bool save();
    bool saveAs();
    void revert();
    void saveSelectionToReadableFile();
    void saveToReadableFile();
    void setSelection(qint64 start, qint64 end);
    void setAddress(qint64 address);
    void setOverwriteMode(bool mode);
    void setSize(qint64 size);
    void showOptionsDialog();
    void showSearchDialog();
    void showVirtualFormatDialog(qint64 rangeFrom = -1, qint64 rangeTo = -1);
    void removeVirtualFormatting(qint64 rangeFrom = -1, qint64 rangeTo = -1);
    void addSectionFromSelection(int parentIdx = -1);
    void selectRangeInEditor(qint64 start, qint64 end, bool focus = true);
    void showPointersDialog();
    void pointersUpdated();
    void hexEditContextMenu(const QPoint &globalPos, qint64 bytePos);
    void onQuickSearchCompleted(int found);
    void goToPreviousPosition();
    void goToNextPosition();
    void goToFirstPosition();
    void goToLastPosition();
    void goToFileBeginning();
    void goToFileEnd();
    bool loadTable();
    void switchShowPointers();
    void switchUseTable();
    void updateScriptMenuState(bool enabled = false);
    void editTable();
    void onTranslationTableChanged();
    void createEmptyTable();
    void showSemiAutoTableDialog();
    void onSemiAutoTableGenerated();
    void saveTable();
    void saveTableAs();
    void dumpScript();
    void insertScript();
    void updateEndiannes();
    void toggleOverwriteMode();
    void onRomTypeChanged(int index);
    void onMenuRomTypeTriggered(QAction *action);
    void onEncodingTriggered(QAction *action);
    void setLanguage();
    void openRecentFile();
    void openRecentTable();
    void openRecentProject();
    void openProject();
    void openProjectFile(const QString &path);
    void restoreDockLayout();
    bool saveProject();
    bool saveProjectAs();
    void toggleShowChanges();
    void createIpsPatch();
    void loadIpsPatch();
    void loadOriginal();
    void onDockTableChanged(TranslationTable *table);
    void onDockTableContentChanged();
    void startCopyTableToTab();
    void newTab();
    void closeTab();
    void nextTab();
    void previousTab();
    void onTabChanged(int index);
    void onTabCloseRequested(int index);

public slots:
    void toggleDarkTheme(bool enabled);

public:
    void loadFile(const QString &fileName);
    void loadFileInNewTab(const QString &fileName);
    void updateHexEditorSettings();
    void applyShortcutsFromSettings();
    RomType currentRomType() const { return m_detectedRomType; }
    int currentPointerSize() const { return m_pointerSize; }
    qint64 currentPointerOffset() const { return m_pointerOffset; }
    void setCurrentPointerOffset(qint64 offset);
    void setCurrentPointerSize(int size);

private:
    void init();
    EditorSession *createSession();
    void saveCurrentSession();
    void restoreSession(EditorSession *session);
    void connectEditorSignals(HexEditor *editor);
    void disconnectEditorSignals(HexEditor *editor);
    void updateTabTitle(int index);
    void createActions();
    void createMenus();
    void createStatusBar();
    void createToolBars();
    void retranslateUi();
    bool maybeSave();
    bool maybeSaveProject();
    bool hasProjectData() const;
    void updateWindowTitle();
    void updateActionStates();
    void readSettings();
    bool saveFile(const QString &fileName);
    void setCurrentFile(const QString &fileName);
    QString strippedName(const QString &fullFileName);
    void writeSettings();
    QString lastDirectory(const QString &settingsKey) const;
    void rememberDirectory(const QString &settingsKey, const QString &filePath);
    void saveProjectDockVisibilityState() const;
    void restoreProjectDockVisibilityState(const QString &projectPath);
    void updateRecentFileMenu();
    void updateRecentTableMenu();
    void updateRecentProjectMenu();
    void addToRecentFiles(const QString &fileName);
    void addToRecentTables(const QString &fileName);
    void addToRecentProjects(const QString &fileName);
    void updateChangedBytesHighlight();
    void refreshChangesView();
    TranslationTable *selectedTable() const;
    int tableIndexForViewMode(bool showOriginal) const;
    TranslationTable *tableForViewMode(bool showOriginal) const;
    void applySelectedTable();
    void applyTranslationTableForViewMode();
    bool shouldSwitchTableOnViewModeChange() const;
    void enforceBottomDockEqualWidth();
    void updateStatusBarVisibility();
    void updateValuePanels();
    void updateEndiannesLabel();
    void repopulateRomTypeCombo();
    void syncRomTypeMenu(int index);
    void syncEncodingMenu();
    void openEncodingSelectionDialog();
    void pushNavigationPosition(qint64 position);
    void resetNavigationHistory();
    void navigateToHistoryIndex(int index);
    void updateNavigationActions();
    QString detectSystemLanguage();
    void applyLanguage(const QString &language);
    bool saveProjectImpl(const QString &path);
    void applyDarkTheme(bool enabled);
    void setDockAreaCollapsed(Qt::DockWidgetArea area, bool collapsed);
    bool isDockAreaCollapsed(Qt::DockWidgetArea area) const;
    void updateDockAreaActions();
    void installSeparatorEventFilters();
    void setupDockTitleBarCallbacks();
    Qt::DockWidgetArea separatorDockArea(QWidget *separator) const;

    // ---- Tab / session management ----
    QTabWidget *m_tabWidget = nullptr;
    QVector<EditorSession*> m_sessions;
    EditorSession *m_currentSession = nullptr;

    // ---- Per-session working copies (swapped on tab switch) ----
    QString curFile;
    QString tableFilePath;
    bool isUntitled;
    qint64 curOffset = 0;
    TranslationTable* tb = nullptr;

    HexDocument *m_document = nullptr;
    bool m_closing = false;
    int  m_closingActiveTab = -1;
    bool m_restoringSession = false;
    bool m_restoringTableDockState = false;
    bool m_restoringProjectUi = false;
    QWidget *m_tabPickerOverlay = nullptr;
    QByteArray m_changeTrackingSnapshot;
    QTimer *m_changesUiUpdateTimer = nullptr;

    QMenu *fileMenu;
    QMenu *editMenu;
    QMenu *changesMenu;
    QMenu *goMenu;
    QMenu *tableMenu;
    QMenu *pointersMenu;
    QMenu *scriptMenu;
    QMenu *viewMenu;
    QMenu *languageMenu;
    QMenu *helpMenu;
    QMenu *toolbarMenu;
    QMenu *statusBarMenu;
    QMenu *panelsMenu;
    QMenu *mapsMenu;
    QMenu *dockMenu = nullptr;
    QMenu *recentFileMenu;
    QMenu *recentTableMenu;
    QMenu *recentProjectMenu;
    QMenu *romTypeMenu;
    QMenu *encodingMenu;
    QActionGroup *romTypeMenuGroup = nullptr;
    QActionGroup *encodingGroup = nullptr;

    QToolBar *fileToolBar;
    QToolBar *editToolBar;
    QToolBar *searchToolBar;
    QToolBar *navigationToolBar = nullptr;
    QToolBar *scriptToolBar = nullptr;
    QToolBar *profileToolBar = nullptr;
    QByteArray defaultWindowState;

    QAction *openAct;
    QAction *saveAct;
    QAction *saveAsAct;
    QAction *saveReadable;
    QAction *revertAct;
    QAction *newAct;
    QAction *closeAct;
    QAction *exitAct;

    QAction *newTabAct = nullptr;
    QAction *closeTabAct = nullptr;
    QAction *nextTabAct = nullptr;
    QAction *prevTabAct = nullptr;

    QAction *openProjectAct = nullptr;
    QAction *saveProjectAct = nullptr;
    QAction *saveProjectAsAct = nullptr;

    QAction *showChangesAct = nullptr;
    QAction *createIpsPatchAct = nullptr;
    QAction *loadIpsPatchAct = nullptr;
    QAction *loadOriginalAct = nullptr;

    QAction *undoAct;
    QAction *redoAct;
    QAction *copyAddressAct;
    QAction *cutAct;
    QAction *copyAct;
    QAction *pasteAct;
    QAction *saveSelectionReadable;

    QAction *loadTableAct;
    QAction *useTableAct;
    QAction *scanForEncodingAct;
    QAction *editTableAct;
    QAction *saveTableAct;
    QAction *saveTableAsAct;
    QAction *createEmptyTableAct;
    QAction *semiAutoTableAct;
    QMenu *createTableMenu;

    QAction *insertScriptAct;
    QAction *dumpScriptAct;

    QAction *findPointersAct;
    QAction *listPointersAct;
    QAction *showPointersAct;
    QAction *pointersSettingsAct;

    QAction *aboutAct;
    QAction *checkUpdatesAct = nullptr;
    QAction *optionsAct;
    QAction *langEnglishAct;
    QAction *langFrenchAct;
    QAction *langGermanAct;
    QAction *langSpanishAct;
    QAction *langPortugueseAct;
    QAction *langJapaneseAct;
    QAction *langChineseSimplifiedAct;
    QAction *langRussianAct;
    QActionGroup *languageGroup;

    QAction *findAct;
    QAction *findNextAct;
    QAction *virtualFormatAct = nullptr;
    QAction *removeVirtualFormattingAct = nullptr;
    QAction *gotoAct;
    QAction *previousPositionAct = nullptr;
    QAction *nextPositionAct = nullptr;
    QAction *firstPositionAct = nullptr;
    QAction *lastPositionAct = nullptr;
    QAction *toolbarPreviousPositionAct = nullptr;
    QAction *toolbarNextPositionAct = nullptr;
    QAction *toolbarFirstPositionAct = nullptr;
    QAction *toolbarLastPositionAct = nullptr;
    QAction *toolbarDumpScriptAct = nullptr;
    QAction *toolbarInsertScriptAct = nullptr;
    QAction *toFileBeginningAct = nullptr;
    QAction *toFileEndAct = nullptr;
    QAction *resetToolbarsAct;
    QAction *showStatusBarAct = nullptr;
    QAction *showStatusEndianAct;
    QAction *showStatusByteAct;
    QAction *showStatusWordAct;
    QAction *showStatusDwordAct;
    QAction *showStatusSelectionAct;
    QAction *showStatusAddressAct;
    QAction *showStatusSizeAct;
    QAction *showStatusModeAct;
    QAction *showStatusEncodingAct = nullptr;
    QAction *showSignedValuesAct;
    QAction *showAddressAreaAct;
    QMenu   *asciiAreaMenu = nullptr;
    QActionGroup *asciiAreaGroup = nullptr;
    QAction *panelModeTextAct = nullptr;
    QAction *panelModeGraphicsAct = nullptr;
    QAction *panelModeSoundAct = nullptr;
    QAction *panelModeDisasmAct = nullptr;
    QAction *showAddressGridAct;
    QAction *showDarkThemeAct = nullptr;
    QAction *showMapPointersAct;
    QAction *showMapTargetsAct;
    QAction *restoreDockLayoutAct = nullptr;
    QAction *collapseLeftDockAreaAct = nullptr;
    QAction *collapseRightDockAreaAct = nullptr;
    QAction *collapseBottomDockAreaAct = nullptr;

    // Dock visibility toggle actions
    QAction *tablesDockToggleAct = nullptr;
    QAction *pointersDockToggleAct = nullptr;
    QAction *changesDockToggleAct = nullptr;
    QAction *sectionsDockToggleAct = nullptr;

    HexEditor *hexEdit;
    OptionsDialog *optionsDialog;
    SearchDialog *searchDialog;
    JumpToDialog *jumpToDialog;
    PointersDialog *pointersDialog;
    SemiAutoTableDialog *semiAutoTableDialog;
    DumpScriptDialog *dumpScriptDialog;
    InsertScriptDialog *insertScriptDialog;
    TablesDockWidget   *m_tablesDock   = nullptr;
    PointersDockWidget *m_pointersDock  = nullptr;
    ChangesDockWidget  *m_changesDock   = nullptr;
    SectionsDockWidget *m_sectionsDock  = nullptr;
    SectionListModel   *m_sectionModel  = nullptr;

    QComboBox *cbRomType;
    QPushButton *lbEndiannes;
    QLabel *lbValueByte;
    QLabel *lbValueWord;
    QLabel *lbValueDword;
    QLabel *lbSelection;
    QLabel *lbAddress;
    QPushButton *lbOverwriteMode;
    QLabel *lbSize, *lbSizeName;
    QLabel *lbEncoding = nullptr;

    RomType m_detectedRomType = RomType::Unknown;
    qint64 m_pointerOffset = defaultPointerOffset(RomType::Unknown);
    int m_pointerSize = 4;

    QPalette m_lightPalette;
    QString m_lightStyleName;
    bool m_lightPaletteCaptured = false;
    QString m_currentEncoding = QStringLiteral("ASCII");
    QString m_readyText;
    UpdateChecker *m_updateChecker = nullptr;
    QVector<qint64> navigationHistory;
    int navigationHistoryIndex = -1;
    bool navigationJumpInProgress = false;
};

#endif

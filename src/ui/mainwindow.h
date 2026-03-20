#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QByteArray>
#include <QVector>

#include "langtranslator.h"
#include "translationtable.h"
#include "hexeditor/hexeditor.h"
#include "romdetect.h"
#include "optionsdialog.h"
#include "searchdialog.h"
#include "pointersdialog.h"
#include "JumpToDialog.h"
#include "TableEditDialog.h"
#include "TablesDockWidget.h"
#include "PointersDockWidget.h"
#include "ChangesDockWidget.h"
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

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void about();
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
    bool saveProject();
    bool saveProjectAs();
    void toggleShowChanges();
    void createIpsPatch();
    void loadIpsPatch();
    void loadOriginal();
    void onDockTableChanged(TranslationTable *table);
    void onDockTableContentChanged();
    void newTab();
    void closeTab();
    void nextTab();
    void previousTab();
    void onTabChanged(int index);
    void onTabCloseRequested(int index);

public:
    void loadFile(const QString &fileName);
    void loadFileInNewTab(const QString &fileName);
    void updateHexEditorSettings();
    void applyShortcutsFromSettings();
    RomType currentRomType() const { return m_detectedRomType; }
    int currentPointerSize() const { return defaultPointerSize(m_detectedRomType); }
    qint64 currentPointerOffset() const { return defaultPointerOffset(m_detectedRomType); }

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
    void updateWindowTitle();
    void updateActionStates();
    void readSettings();
    bool saveFile(const QString &fileName);
    void setCurrentFile(const QString &fileName);
    QString strippedName(const QString &fullFileName);
    void writeSettings();
    QString lastDirectory(const QString &settingsKey) const;
    void rememberDirectory(const QString &settingsKey, const QString &filePath);
    void updateRecentFileMenu();
    void updateRecentTableMenu();
    void updateRecentProjectMenu();
    void addToRecentFiles(const QString &fileName);
    void addToRecentTables(const QString &fileName);
    void addToRecentProjects(const QString &fileName);
    void updateChangedBytesHighlight();
    void refreshChangesView();
    TranslationTable *tableForViewMode(bool showOriginal) const;
    void applyTranslationTableForViewMode();
    bool shouldSwitchTableOnViewModeChange() const;
    void enforceBottomDockEqualWidth();
    void updateStatusBarVisibility();
    void updateValuePanels();
    void updateEndiannesLabel();
    void repopulateRomTypeCombo();
    void syncRomTypeMenu(int index);
    void syncEncodingMenu();
    void pushNavigationPosition(qint64 position);
    void resetNavigationHistory();
    void navigateToHistoryIndex(int index);
    void updateNavigationActions();
    QString detectSystemLanguage();
    void applyLanguage(const QString &language);
    bool saveProjectImpl(const QString &path);

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
    bool m_projectModified = false;
    bool m_closing = false;
    bool m_restoringTableDockState = false;
    bool m_restoringProjectUi = false;
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
    QAction *showAsciiAreaAct;
    QAction *showAddressGridAct;
    QAction *showMapPointersAct;
    QAction *showMapTargetsAct;

    HexEditor *hexEdit;
    OptionsDialog *optionsDialog;
    SearchDialog *searchDialog;
    JumpToDialog *jumpToDialog;
    PointersDialog *pointersDialog;
    TableEditDialog *tableEditDialog;
    SemiAutoTableDialog *semiAutoTableDialog;
    DumpScriptDialog *dumpScriptDialog;
    InsertScriptDialog *insertScriptDialog;
    TablesDockWidget   *m_tablesDock   = nullptr;
    PointersDockWidget *m_pointersDock  = nullptr;
    ChangesDockWidget  *m_changesDock   = nullptr;

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
    QString m_currentEncoding = QStringLiteral("ASCII");
    QString m_readyText;
    QVector<qint64> navigationHistory;
    int navigationHistoryIndex = -1;
    bool navigationJumpInProgress = false;
};

#endif

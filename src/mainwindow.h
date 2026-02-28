#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QByteArray>

#include "langtranslator.h"
#include "translationtable.h"
#include "qhexedit/qhexedit.h"
#include "optionsdialog.h"
#include "searchdialog.h"
#include "pointersdialog.h"
#include "JumpToDialog.h"
#include "TableEditDialog.h"
#include "SemiAutoTableDialog.h"
#include "DumpScriptdialog.h"
#include "InsertScriptDialog.h"

QT_BEGIN_NAMESPACE
class QAction;
class QActionGroup;
class QMenu;
class QUndoStack;
class QLabel;
class QPushButton;
class QDragEnterEvent;
class QDropEvent;
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow();

protected:
    void closeEvent(QCloseEvent *event);
    void changeEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event);
    void dropEvent(QDropEvent *event);

private slots:
    void about();
    void dataChanged();
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
    bool loadTable();
    void switchShowPointers();
    void switchUseTable();
    void updateScriptMenuState();
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
    void setLanguage();

public:
    void loadFile(const QString &fileName);

private:
    void init();
    void createActions();
    void createMenus();
    void createStatusBar();
    void createToolBars();
    void retranslateUi();
    bool maybeSave();
    void updateActionStates();
    void readSettings();
    bool saveFile(const QString &fileName);
    void setCurrentFile(const QString &fileName);
    QString strippedName(const QString &fullFileName);
    void writeSettings();
    QString lastDirectory(const QString &settingsKey) const;
    void rememberDirectory(const QString &settingsKey, const QString &filePath);

    QString curFile;
    QString tableFilePath;
    QFile file;
    bool isUntitled;
    qint64 curOffset;
    TranslationTable* tb = nullptr;

    QMenu *fileMenu;
    QMenu *editMenu;
    QMenu *tableMenu;
    QMenu *pointersMenu;
    QMenu *scriptMenu;
    QMenu *viewMenu;
    QMenu *languageMenu;
    QMenu *helpMenu;
    QMenu *toolbarMenu;

    QToolBar *fileToolBar;
    QToolBar *editToolBar;
    QToolBar *searchToolBar;
    QByteArray defaultWindowState;

    QAction *openAct;
    QAction *saveAct;
    QAction *saveAsAct;
    QAction *saveReadable;
    QAction *revertAct;
    QAction *newAct;
    QAction *closeAct;
    QAction *exitAct;

    QAction *undoAct;
    QAction *redoAct;
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
    QAction *resetToolbarsAct;

    QHexEdit *hexEdit;
    OptionsDialog *optionsDialog;
    SearchDialog *searchDialog;
    JumpToDialog *jumpToDialog;
    PointersDialog *pointersDialog;
    TableEditDialog *tableEditDialog;
    SemiAutoTableDialog *semiAutoTableDialog;
    DumpScriptDialog *dumpScriptDialog;
    InsertScriptDialog *insertScriptDialog;

    QPushButton *lbEndiannes;
    QLabel *lbValue;
    QLabel *lbSelection;
    QLabel *lbAddress, *lbAddressName;
    QPushButton *lbOverwriteMode;
    QLabel *lbOverwriteModeName;
    QLabel *lbSize, *lbSizeName;
};

#endif

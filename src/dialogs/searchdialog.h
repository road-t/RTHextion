#ifndef SEARCHDIALOG_H
#define SEARCHDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QtCore>
#include "hexeditor/hexeditor.h"
#include "TablesDockWidget.h"

class SearchDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SearchDialog(HexEditor *hexEdit, QWidget *parent = nullptr);
    ~SearchDialog();
    qint64 findNext();
    qint64 findPrevious();
    void setHexEdit(HexEditor *hexEdit);
    void setAvailableTables(const QVector<TableTab> &tables, int activeIndex, bool useTable);

    /// Snapshot of the dialog's editable state (find/replace text, formats, relative flag).
    struct State {
        QString findText;
        int     findFormat    = -1;   // table combo data: -1 = Raw, -2 = Hex, >=0 = table index
        QString replaceText;
        int     replaceFormat = -1;
        bool    relative      = false;
        int     sectionScope  = 0;    // 0=All, 1=Text, 2=Code, 3=Sound, 4=Graphics
    };
    State dialogState() const;
    void  setDialogState(const State &s);

private slots:
    void on_pbFind_clicked();
    void on_pbFindPrev_clicked();
    void on_pbReplace_clicked();
    void on_pbReplaceAll_clicked();

    void onFindTableChanged(int index);
    void onReplaceTableChanged(int index);

protected:
    void showEvent(QShowEvent *ev) override;
    void changeEvent(QEvent *event) override;

private:
    struct SearchResult {
        qint64 index = -1;
        int matchNumber = 0;
        int totalMatches = 0;
        bool wrapped = false;
    };

    SearchResult findOccurrence(bool forward, bool allowWrap = true);
    bool offsetMatchesScope(qint64 offset) const;
    qint64 findMatchingOccurrence(bool forward, qint64 from, const QByteArray &needle,
                                  bool relativeMode, bool allowWrap, bool *wrappedOut = nullptr) const;
    bool selectionMatchesPattern(const QByteArray &needle, bool relativeMode) const;
    void updateMatchStatus(const SearchResult &result, bool hasPattern, bool found);
    void clearMatchStatus();
    QByteArray getContent(int comboIndex, const QString &input, TranslationTable *table);
    qint64 replaceOccurrence(qint64 idx, const QByteArray &replaceBa);

    HexEditor *_hexEdit;
    QByteArray _findBa;

    // Table data
    QVector<TableTab> m_availableTables;
    int m_activeTableIndex = -1;
    bool m_useTable = false;
    TranslationTable *m_findTable = nullptr;
    TranslationTable *m_replaceTable = nullptr;

    // UI widgets
    QComboBox *cbFind;
    QComboBox *cmbFindTable;
    QCheckBox *cbRelative;
    QComboBox *cmbSectionScope;
    QLabel *lbMatchStatus;
    QPushButton *pbFind;
    QPushButton *pbFindPrev;

    QComboBox *cbReplace;
    QComboBox *cmbReplaceTable;
    QPushButton *pbReplace;
    QPushButton *pbReplaceAll;

    QPushButton *pbCancel;

    State m_pendingState;
    bool m_hasPendingState = false;
};

#endif // SEARCHDIALOG_H

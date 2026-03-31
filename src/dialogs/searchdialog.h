#ifndef SEARCHDIALOG_H
#define SEARCHDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QCheckBox>
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
        int     findFormat    = 0;
        QString replaceText;
        int     replaceFormat = 0;
        bool    relative      = false;
    };
    State dialogState() const;
    void  setDialogState(const State &s);

private slots:
    void on_pbFind_clicked();
    void on_pbFindPrev_clicked();
    void on_pbReplace_clicked();
    void on_pbReplaceAll_clicked();

    void on_cbFindFormat_currentIndexChanged(int index);
    void on_cbReplaceFormat_currentIndexChanged(int index);

    void onFindTableChanged(int index);
    void onReplaceTableChanged(int index);

protected:
    void showEvent(QShowEvent *ev) override;
    void changeEvent(QEvent *event) override;

private:
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
    QComboBox *cbFindFormat;
    QComboBox *cbFind;
    QComboBox *cmbFindTable;
    QCheckBox *cbRelative;
    QPushButton *pbFind;
    QPushButton *pbFindPrev;

    QComboBox *cbReplaceFormat;
    QComboBox *cbReplace;
    QComboBox *cmbReplaceTable;
    QPushButton *pbReplace;
    QPushButton *pbReplaceAll;

    QPushButton *pbCancel;
};

#endif // SEARCHDIALOG_H

#ifndef DUMPSCRIPTDIALOG_H
#define DUMPSCRIPTDIALOG_H

#include <QDialog>
#include <QCloseEvent>
#include "hexeditor/hexeditor.h"
#include "translationtable.h"
#include "TablesDockWidget.h"

namespace Ui {
class DumpScriptDialog;
}

class DumpScriptDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DumpScriptDialog(HexEditor *hexEdit, QWidget *parent = nullptr);
    ~DumpScriptDialog();
    void updateText();
    void setRomProfile(int pointerSize, qint64 pointerOffset);
    void setAvailableTables(const QVector<TableTab> &tables, int activeIndex, bool useTable);

protected:
    void showEvent(QShowEvent *ev) override;
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void populateStopCharCmb();

private slots:
    void on_cbSplitByCharacter_stateChanged(int arg1);

    void on_cmbTable_currentIndexChanged(int index);

    void on_cmbSplitCharacter_currentIndexChanged(int index);

    void on_cbUsePointers_stateChanged(int arg1);

    void on_cbSplitByPointers_stateChanged(int arg1);

    void on_pbCancel_clicked();
    void on_pbExport_clicked();
    void on_pbInsert_clicked();

    QByteArray decodeScriptText(const QString &text, bool useTable) const;

private:
    Ui::DumpScriptDialog *ui;
    HexEditor *hexEdit;
    TranslationTable* tb;
    QVector<TableTab> m_availableTables;
    int m_activeTableIndex = -1;
    bool m_useTable = false;
    int _pointerSize = 4;
    qint64 _pointerOffset = 0;
};

#endif // DUMPSCRIPTDIALOG_H

#ifndef DUMPSCRIPTDIALOG_H
#define DUMPSCRIPTDIALOG_H

#include <QDialog>
#include "qhexedit/qhexedit.h"
#include "translationtable.h"

namespace Ui {
class DumpScriptDialog;
}

class DumpScriptDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DumpScriptDialog(QHexEdit *hexEdit, QWidget *parent = nullptr);
    ~DumpScriptDialog();
    void updateText();

protected:
    void showEvent(QShowEvent *ev);
    void populateStopCharCmb();

private slots:
    void on_cbSplitByCharacter_stateChanged(int arg1);

    void on_cbUseTable_stateChanged(int arg1);

    void on_cmbSplitCharacter_currentIndexChanged(int index);

    void on_cbSplitByPointers_stateChanged(int arg1);

    void on_buttonBox_accepted();

private:
    Ui::DumpScriptDialog *ui;
    QHexEdit *hexEdit;
    TranslationTable* tb;
};

#endif // DUMPSCRIPTDIALOG_H

#ifndef INSERTSCRIPTDIALOG_H
#define INSERTSCRIPTDIALOG_H

#include <QDialog>
#include <QAbstractButton>

#include "qhexedit/qhexedit.h"
#include "QtWidgets/qabstractbutton.h"
#include "translationtable.h"

namespace Ui {
class InsertScriptDialog;
}

class InsertScriptDialog : public QDialog
{
    Q_OBJECT

public:
    explicit InsertScriptDialog(QHexEdit *hexEdit, QWidget *parent = nullptr);
    ~InsertScriptDialog();
    void updateText();

protected:
    void showEvent(QShowEvent *ev);

private slots:
    void on_bbControls_accepted();

    void on_bbControls_clicked(QAbstractButton *button);

    void on_cbUpdatePointers_stateChanged(int arg1);

    void on_rbLittleEndian_toggled(bool checked);

    void on_rbBigEndian_toggled(bool checked);

private:
    Ui::InsertScriptDialog *ui;
    QHexEdit *hexEdit;
    TranslationTable* tb;
    bool bigEndian = false;

};

#endif // INSERTSCRIPTDIALOG_H

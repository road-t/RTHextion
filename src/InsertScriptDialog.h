#ifndef INSERTSCRIPTDIALOG_H
#define INSERTSCRIPTDIALOG_H

#include <QDialog>
#include <QAbstractButton>
#include <QFileDialog>

#include "qhexedit/qhexedit.h"
#include "QtWidgets/qabstractbutton.h"
#include "translationtable.h"
#include "romdetect.h"

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
    void setRomProfile(int pointerSize, qint64 pointerOffset);

protected:
    void showEvent(QShowEvent *ev) override;
    void changeEvent(QEvent *event) override;

private slots:
    void on_bbControls_accepted();

    void on_bbControls_clicked(QAbstractButton *button);

    void on_cbUpdatePointers_stateChanged(int arg1);

private:
    Ui::InsertScriptDialog *ui;
    QHexEdit *hexEdit;
    TranslationTable* tb;
    int _pointerSize = 4;
    qint64 _pointerOffset = 0;

};

#endif // INSERTSCRIPTDIALOG_H

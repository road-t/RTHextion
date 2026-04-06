#ifndef INSERTSCRIPTDIALOG_H
#define INSERTSCRIPTDIALOG_H

#include <QDialog>
#include <QAbstractButton>
#include <QFileDialog>

#include "hexeditor/hexeditor.h"
#include "QtWidgets/qabstractbutton.h"
#include "translationtable.h"
#include "romdetect.h"
#include "TablesDockWidget.h"

namespace Ui {
class InsertScriptDialog;
}

class InsertScriptDialog : public QDialog
{
    Q_OBJECT

public:
    explicit InsertScriptDialog(HexEditor *hexEdit, QWidget *parent = nullptr);
    ~InsertScriptDialog();
    void updateText();
    void setRomProfile(int pointerSize, qint64 pointerOffset);
    void setAvailableTables(const QVector<TableTab> &tables);

protected:
    void showEvent(QShowEvent *ev) override;
    void changeEvent(QEvent *event) override;

private slots:
    void on_bbControls_accepted();

    void on_bbControls_clicked(QAbstractButton *button);

    void on_cbUpdatePointers_stateChanged(int arg1);

private:
    Ui::InsertScriptDialog *ui;
    HexEditor *hexEdit;
    TranslationTable* tb;
    QVector<TableTab> m_tables;
    int _pointerSize = 4;
    qint64 _pointerOffset = 0;

};

#endif // INSERTSCRIPTDIALOG_H

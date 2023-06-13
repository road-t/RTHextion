#ifndef TABLEEDITDIALOG_H
#define TABLEEDITDIALOG_H

#include <QDialog>
#include <QTableWidgetItem>
#include "translationtable.h"

namespace Ui {
class TableEditDialog;
}

class TableEditDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TableEditDialog(TranslationTable** tb, QWidget *parent = nullptr);
    ~TableEditDialog();

private:
    void showEvent(QShowEvent *ev);
    Ui::TableEditDialog *ui;
    TranslationTable** _tb = nullptr;
    QVector<QTableWidgetItem> items;
};

#endif // TABLEEDITDIALOG_H

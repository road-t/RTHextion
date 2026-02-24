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

signals:
    void tableChanged();

private slots:
    void on_pbAdd_clicked();
    void on_pbRemove_clicked();
    void on_pbClear_clicked();
    void accept() override;

private:
    void showEvent(QShowEvent *ev) override;
    void addRow(uint8_t hex, const QString &value);

    Ui::TableEditDialog *ui;
    TranslationTable** _tb = nullptr;
};

#endif // TABLEEDITDIALOG_H

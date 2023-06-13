#include "TableEditDialog.h"
#include "ui_TableEditDialog.h"

TableEditDialog::TableEditDialog(TranslationTable** tb, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::TableEditDialog),
    _tb(tb)
{
    ui->setupUi(this);

    ui->twTable->setHorizontalHeaderLabels(QStringList() << "HEX" << tr("Value"));
}

void TableEditDialog::showEvent(QShowEvent *ev)
{
    items.clear();

    auto tb = *_tb;

    if (tb)
    {
        ui->twTable->setRowCount(tb->size());
        auto begin = tb->getItems()->begin();
        auto end = tb->getItems()->end();

        uint32_t row = 0;

        for (auto it = begin; it != end; it++)
        {
            items.append(QTableWidgetItem(QString::number(tb->item(it).first, 16).toUpper()));
            ui->twTable->setItem(row, 0, &items.last());

            items.append(QTableWidgetItem(tb->item(it).second));
            ui->twTable->setItem(row, 1, &items.last());

            row++;
        }
    }
}

TableEditDialog::~TableEditDialog()
{
    delete ui;
}

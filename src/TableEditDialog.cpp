#include "TableEditDialog.h"
#include "ui_TableEditDialog.h"
#include <QMessageBox>
#include <QHeaderView>
#include <QSet>
#include <QInputDialog>
#include <QLineEdit>

TableEditDialog::TableEditDialog(TranslationTable** tb, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::TableEditDialog),
    _tb(tb)
{
    ui->setupUi(this);

    ui->twTable->setHorizontalHeaderLabels(QStringList() << "HEX" << tr("Value"));

    // Stretch both columns
    ui->twTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->twTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
}

TableEditDialog::~TableEditDialog()
{
    delete ui;
}

void TableEditDialog::showEvent(QShowEvent *ev)
{
    Q_UNUSED(ev);

    ui->twTable->setRowCount(0);

    auto tb = *_tb;

    if (tb)
    {
        for (auto it = tb->getItems()->cbegin(); it != tb->getItems()->cend(); ++it)
        {
            addRow(static_cast<uint8_t>(it.key()), it.value());
        }
    }
}

void TableEditDialog::addRow(uint8_t hex, const QString &value)
{
    int row = ui->twTable->rowCount();
    ui->twTable->insertRow(row);

    auto *hexItem = new QTableWidgetItem(QString::number(hex, 16).toUpper().rightJustified(2, '0'));
    hexItem->setTextAlignment(Qt::AlignCenter);
    // HEX column: not editable
    hexItem->setFlags(hexItem->flags() & ~Qt::ItemIsEditable);
    ui->twTable->setItem(row, 0, hexItem);

    auto *valItem = new QTableWidgetItem(value);
    ui->twTable->setItem(row, 1, valItem);
}

void TableEditDialog::on_pbAdd_clicked()
{
    // Find first free byte not already in table
    QSet<int> usedKeys;
    for (int r = 0; r < ui->twTable->rowCount(); ++r)
    {
        bool ok;
        int key = ui->twTable->item(r, 0)->text().toInt(&ok, 16);
        if (ok) usedKeys.insert(key);
    }

    // Suggest the first free byte
    int suggestedByte = 0;
    for (int i = 0; i <= 0xFF; ++i)
    {
        if (!usedKeys.contains(i))
        {
            suggestedByte = i;
            break;
        }
    }

    bool ok;
    QString input = QInputDialog::getText(this, tr("Add Entry"),
                                          tr("Enter hex byte value (0x00-0xFF):"),
                                          QLineEdit::Normal,
                                          QString::number(suggestedByte, 16).rightJustified(2, '0').toUpper(),
                                          &ok);
    if (!ok || input.isEmpty())
        return;

    bool hexOk;
    int byteVal = input.toInt(&hexOk, 16);
    if (!hexOk || byteVal < 0 || byteVal > 0xFF)
    {
        QMessageBox::warning(this, tr("Invalid Input"),
                           tr("Please enter a valid hex byte value (0x00-0xFF)"));
        return;
    }

    if (usedKeys.contains(byteVal))
    {
        QMessageBox::warning(this, tr("Duplicate"),
                           tr("This byte value is already in the table"));
        return;
    }

    addRow(static_cast<uint8_t>(byteVal), "");
    ui->twTable->scrollToBottom();
    // Set focus to the value cell for immediate editing
    ui->twTable->setCurrentCell(ui->twTable->rowCount() - 1, 1);
    ui->twTable->editItem(ui->twTable->item(ui->twTable->rowCount() - 1, 1));
}

void TableEditDialog::on_pbRemove_clicked()
{
    auto selected = ui->twTable->selectedItems();
    QSet<int> rows;
    for (auto *item : selected)
        rows.insert(item->row());

    QList<int> rowList = rows.values();
    std::sort(rowList.rbegin(), rowList.rend()); // remove from bottom up
    for (int r : rowList)
        ui->twTable->removeRow(r);
}

void TableEditDialog::on_pbClear_clicked()
{
    auto res = QMessageBox::warning(this, tr("Clear table"),
                                    tr("Are you sure you want to clear the entire table?"),
                                    QMessageBox::Yes | QMessageBox::No);
    if (res == QMessageBox::Yes)
        ui->twTable->setRowCount(0);
}

void TableEditDialog::accept()
{
    if (!_tb || !*_tb)
    {
        QDialog::accept();
        return;
    }

    auto tb = *_tb;
    tb->clearItems();

    for (int r = 0; r < ui->twTable->rowCount(); ++r)
    {
        auto *hexItem = ui->twTable->item(r, 0);
        auto *valItem = ui->twTable->item(r, 1);
        if (!hexItem || !valItem) continue;

        bool ok;
        uint8_t key = static_cast<uint8_t>(hexItem->text().toInt(&ok, 16));
        if (!ok) continue;

        tb->setItem(key, valItem->text());
    }

    QDialog::accept();
}

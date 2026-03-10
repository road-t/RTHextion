#include "TableEditDialog.h"
#include "ui_TableEditDialog.h"
#include <QMessageBox>
#include <QHeaderView>
#include <QSet>
#include <QInputDialog>
#include <QLineEdit>
#include <QEvent>
#include <QRegularExpressionValidator>

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

        const auto &mbItems = tb->getMultiByteItems();
        for (auto it = mbItems.cbegin(); it != mbItems.cend(); ++it)
            addRow(it.key(), it.value());
    }
}

void TableEditDialog::addRow(uint8_t hex, const QString &value)
{
    addRow(QByteArray(1, static_cast<char>(hex)), value);
}

void TableEditDialog::addRow(const QByteArray &key, const QString &value)
{
    if (key.isEmpty())
        return;

    int row = ui->twTable->rowCount();
    ui->twTable->insertRow(row);

    QString hexText;
    for (int i = 0; i < key.size(); ++i)
        hexText += QString::number(static_cast<uint8_t>(key[i]), 16).toUpper().rightJustified(2, '0');

    auto *hexItem = new QTableWidgetItem(hexText);
    hexItem->setTextAlignment(Qt::AlignCenter);
    ui->twTable->setItem(row, 0, hexItem);

    auto *valItem = new QTableWidgetItem(value);
    ui->twTable->setItem(row, 1, valItem);
}

void TableEditDialog::on_pbAdd_clicked()
{
    // Suggest first free single-byte key, but allow multi-byte sequences too.
    QSet<QString> usedKeys;
    for (int r = 0; r < ui->twTable->rowCount(); ++r)
    {
        auto *item = ui->twTable->item(r, 0);
        if (!item)
            continue;
        const QString key = item->text().trimmed().toUpper();
        if (!key.isEmpty())
            usedKeys.insert(key);
    }

    // Suggest the first free byte
    int suggestedByte = -1;
    for (int i = 0; i <= 0xFF; ++i)
    {
        const QString key = QString::number(i, 16).toUpper().rightJustified(2, '0');
        if (!usedKeys.contains(key))
        {
            suggestedByte = i;
            break;
        }
    }
    if (suggestedByte < 0)
        suggestedByte = 0;

    bool ok;
    QInputDialog hexDialog(this);
    hexDialog.setWindowTitle(tr("Add entry"));
    hexDialog.setLabelText(tr("Enter hex byte sequence (00-FF, even number of digits):"));
    hexDialog.setInputMode(QInputDialog::TextInput);
    hexDialog.setTextValue(QString::number(suggestedByte, 16).rightJustified(2, '0').toUpper());

    if (auto lineEdit = hexDialog.findChild<QLineEdit*>()) {
        auto *validator = new QRegularExpressionValidator(QRegularExpression(QStringLiteral("^[0-9A-Fa-f]{2,}$")), lineEdit);
        lineEdit->setValidator(validator);
    }

    ok = hexDialog.exec() == QDialog::Accepted;
    QString input = hexDialog.textValue();
    if (!ok || input.isEmpty())
        return;

    input = input.trimmed().toUpper();
    if (input.size() % 2 != 0)
    {
        QMessageBox::warning(this, tr("Invalid Input"),
                           tr("Please enter an even number of hex digits (e.g. 12 or 00E8)."));
        return;
    }

    bool parseOk = true;
    QByteArray key;
    for (int i = 0; i < input.size(); i += 2)
    {
        bool okPart = false;
        const int v = input.mid(i, 2).toInt(&okPart, 16);
        if (!okPart)
        {
            parseOk = false;
            break;
        }
        key.append(static_cast<char>(v));
    }
    if (!parseOk || key.isEmpty())
    {
        QMessageBox::warning(this, tr("Invalid Input"),
                             tr("Please enter a valid hex byte sequence."));
        return;
    }

    if (usedKeys.contains(input))
    {
        QMessageBox::warning(this, tr("Duplicate"),
                           tr("This key is already in the table"));
        return;
    }

    // Now prompt for the translation value
    QString valueStr = QInputDialog::getText(this, tr("Add entry"),
                                             tr("Enter translation text for key 0x%1:").arg(input),
                                             QLineEdit::Normal,
                                             QString(),
                                             &ok);
    if (!ok)
        return;

    addRow(key, valueStr);
    ui->twTable->scrollToBottom();
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

        const QString hexText = hexItem->text().trimmed();
        if (hexText.isEmpty() || (hexText.size() % 2) != 0)
            continue;

        QByteArray key;
        bool ok = true;
        for (int i = 0; i < hexText.size(); i += 2)
        {
            bool okPart = false;
            const int v = hexText.mid(i, 2).toInt(&okPart, 16);
            if (!okPart)
            {
                ok = false;
                break;
            }
            key.append(static_cast<char>(v));
        }
        if (!ok || key.isEmpty())
            continue;

        const QString value = valItem->text();
        // Skip empty values
        if (value.isEmpty()) continue;

        if (key.size() == 1)
            tb->setItem(static_cast<uint8_t>(key[0]), value);
        else
            tb->setMultiByteItem(key, value);
    }

    emit tableChanged();

    QDialog::accept();
}

void TableEditDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        ui->retranslateUi(this);
    QDialog::changeEvent(event);
}

#include <QMessageBox>
#include <QFileDialog>

#include "DumpScriptdialog.h"
#include "ui_DumpScriptdialog.h"

DumpScriptDialog::DumpScriptDialog(QHexEdit *hexEdit, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DumpScriptDialog),
    hexEdit(hexEdit)
{
    ui->setupUi(this);
}

void DumpScriptDialog::showEvent(QShowEvent *ev)
{
    Q_UNUSED(ev);

    auto _tb = hexEdit->getTranslationTable();

    tb = _tb;

    ui->cbUseTable->setDisabled(!tb);
    ui->cbUseTable->setChecked(tb);

    updateText();
    populateStopCharCmb();
}

DumpScriptDialog::~DumpScriptDialog()
{
    delete ui;
}

void DumpScriptDialog::on_cbSplitByCharacter_stateChanged(int arg1)
{
    ui->cmbSplitCharacter->setDisabled(!arg1);
    updateText();
}

void DumpScriptDialog::updateText()
{
    auto useTable = (tb && ui->cbUseTable->isChecked());
    auto data = hexEdit->getRawSelection();

    QChar stopChar;
    bool stopCharActive = false;

    if (ui->cbSplitByCharacter->isChecked())
    {
        stopChar = ui->cmbSplitCharacter->itemData(ui->cmbSplitCharacter->currentIndex()).toChar();
        stopCharActive = true;
    }

    QString dump;

    auto selectionOffset = hexEdit->getSelectionBegin();

    for (auto i = 0; i < data.size(); i++)
    {
        auto offset = i + selectionOffset;

        if (hexEdit->pointers()->hasOffset(offset))
        {
            // split by pointers if requested
            if (ui->cbSplitByPointers->isChecked() && i)
                dump += '\n';

            auto ptrs = hexEdit->pointers()->getPointers(offset);
            QString ptrsString;

            if (ptrs.size() > 1)
            {
                QStringList ptrsList;

                for (auto it = ptrs.begin(); it != ptrs.end(); ++it)
                {
                    ptrsList.push_back(QString::number(*it, 16));
                }

                ptrsString = ptrsList.join(',');
            }
            else
                ptrsString = QString::number(ptrs[0], 16);

            // add pointer to dump
            dump += QString("{|%1|}:\n").arg(ptrsString);
        }

        QChar ch = data[i];

        if (useTable)
            dump += tb->encodeSymbol(ch.toLatin1(), true);
        else
            dump += ch.isPrint() ? ch : TranslationTable::charToHex(ch.toLatin1());

        if (stopCharActive && ch == stopChar)
            dump += '\n';
    }

    ui->pteScript->setPlainText(dump);
}

void DumpScriptDialog::on_cbUseTable_stateChanged(int arg1)
{
    Q_UNUSED(arg1);

    updateText();
    populateStopCharCmb();
}

void DumpScriptDialog::populateStopCharCmb()
{
    ui->cmbSplitCharacter->clear();

    // populate range comboboxes with translation table values...
    if (ui->cbUseTable->isChecked() && tb)
    {
        size_t i = 0;
        tb->reset();

        while (i++ < tb->size())
        {
            auto el = tb->next();

            ui->cmbSplitCharacter->addItem(el.second, el.first);
        }
    }
    else // ...or with standard ASCII printable characters
    {
        for (uint16_t c = 0; c < 0x100; c++)
        {
            if (c > 0x20)
                ui->cmbSplitCharacter->addItem(QString("{%1} - %2").arg(QString::number(c, 16).toUpper(), 2, '0').arg(QChar(c)), c);
            else
                ui->cmbSplitCharacter->addItem(QString("{%1}").arg(QString::number(c, 16).toUpper(), 2, '0'), c);
        }
    }

    ui->cmbSplitCharacter->setCurrentIndex(0);
}

void DumpScriptDialog::on_cmbSplitCharacter_currentIndexChanged(int index)
{
    Q_UNUSED(index);

    updateText();
}


void DumpScriptDialog::on_cbSplitByPointers_stateChanged(int arg1)
{
    Q_UNUSED(arg1);

    updateText();
}


void DumpScriptDialog::on_buttonBox_accepted()
{
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save To Readable File"));

    if (!fileName.isEmpty())
    {
        QFile file(fileName);

        if (!file.open(QFile::WriteOnly | QFile::Text)) {
            QMessageBox::warning(this, tr("Save dump"),
                                 tr("Cannot write file %1:\n%2.")
                                 .arg(fileName)
                                 .arg(file.errorString()));
            return;
        }

        QApplication::setOverrideCursor(Qt::WaitCursor);

        file.write(ui->pteScript->toPlainText().toUtf8());

        QApplication::restoreOverrideCursor();
    }

    close();
}


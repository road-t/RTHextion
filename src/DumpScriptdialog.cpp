#include <QMessageBox>
#include <QFileDialog>
#include <QSettings>
#include <QDir>
#include <QFileInfo>
#include <QEvent>

#include "DumpScriptdialog.h"
#include "ui_DumpScriptdialog.h"

namespace
{
    const char *kLastDumpDirKey = "Paths/LastDumpDir";

    bool hasIncomingPointersInSelection(QHexEdit *hexEdit)
    {
        if (!hexEdit)
            return false;

        const QByteArray selection = hexEdit->getRawSelection();
        if (selection.size() <= 1)
            return false;

        const qint64 begin = hexEdit->getSelectionBegin();
        PointerListModel *model = hexEdit->pointers();
        if (!model)
            return false;

        for (int i = 0; i < selection.size(); ++i)
        {
            if (model->hasOffset(begin + i))
                return true;
        }

        return false;
    }
}

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

    const bool canUsePointers = hasIncomingPointersInSelection(hexEdit);
    ui->cbUsePointers->setEnabled(canUsePointers);
    ui->cbUsePointers->setChecked(canUsePointers);
    ui->cbSplitByPointers->setEnabled(canUsePointers && ui->cbUsePointers->isChecked());
    if (!canUsePointers)
        ui->cbSplitByPointers->setChecked(false);

    updateText();
    populateStopCharCmb();
}

DumpScriptDialog::~DumpScriptDialog()
{
    delete ui;
}

void DumpScriptDialog::setRomProfile(int pointerSize, qint64 pointerOffset)
{
    _pointerSize = pointerSize;
    _pointerOffset = pointerOffset;
}

void DumpScriptDialog::on_cbSplitByCharacter_stateChanged(int arg1)
{
    ui->cmbSplitCharacter->setDisabled(!arg1);
    updateText();
}

void DumpScriptDialog::updateText()
{
    auto useTable = (tb && ui->cbUseTable->isChecked());
    const bool usePointers = ui->cbUsePointers->isChecked();
    auto data = hexEdit->getRawSelection();

    QByteArray stopBytes;
    bool stopCharActive = false;

    if (ui->cbSplitByCharacter->isChecked())
    {
        stopBytes = ui->cmbSplitCharacter->currentData().toByteArray();
        stopCharActive = !stopBytes.isEmpty();
    }

    QString dump;

    auto selectionOffset = hexEdit->getSelectionBegin();

    int i = 0;
    while (i < data.size())
    {
        const auto offset = i + selectionOffset;

        if (usePointers && hexEdit->pointers()->hasOffset(offset))
        {
            // split by pointers if requested
            if (ui->cbSplitByPointers->isChecked() && i)
                dump += '\n';

            auto ptrs = hexEdit->pointers()->getPointers(offset);
            QString ptrsString;

            {
                QStringList ptrsList;
                for (const qint64 ptrKey : ptrs)
                {
                    const int sz = hexEdit->pointers()->getPointerSize(ptrKey);
                    ptrsList.push_back(QString::number(ptrKey, 16) + ":" + QString::number(sz));
                }
                ptrsString = ptrsList.join(',');
            }

            // add pointer to dump
            dump += QString("{|%1|}:\n").arg(ptrsString);
        }

        if (useTable)
        {
            int consumed = 0;
            dump += tb->encodeBytes(data, i, consumed, true);
            if (stopCharActive && data.mid(i, stopBytes.size()) == stopBytes)
                dump += '\n';
            i += consumed;
        }
        else
        {
            QChar ch = QChar::fromLatin1(data[i]);
            dump += ch.isPrint() ? ch : TranslationTable::charToHex(ch.toLatin1());
            if (stopCharActive && data.mid(i, stopBytes.size()) == stopBytes)
                dump += '\n';
            ++i;
        }
    }

    ui->pteScript->setPlainText(dump);
}

void DumpScriptDialog::on_cbUseTable_stateChanged(int arg1)
{
    Q_UNUSED(arg1);

    updateText();
    populateStopCharCmb();
}

void DumpScriptDialog::on_cbUsePointers_stateChanged(int arg1)
{
    ui->cbSplitByPointers->setEnabled(arg1 != 0);
    if (!arg1)
        ui->cbSplitByPointers->setChecked(false);

    updateText();
}

void DumpScriptDialog::populateStopCharCmb()
{
    ui->cmbSplitCharacter->clear();

    // populate range comboboxes with translation table values...
    if (ui->cbUseTable->isChecked() && tb)
    {
        const auto *items = tb->getItems();
        for (auto it = items->cbegin(); it != items->cend(); ++it)
            ui->cmbSplitCharacter->addItem(it.value(), QByteArray(1, it.key()));
        const auto &mbItems = tb->getMultiByteItems();
        for (auto it = mbItems.cbegin(); it != mbItems.cend(); ++it)
            ui->cmbSplitCharacter->addItem(it.value(), it.key());
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
    QSettings settings;
    const QString defaultDir = settings.value(kLastDumpDirKey, QDir::homePath()).toString();
    QString fileName = QFileDialog::getSaveFileName(this,
                                                    tr("Save to readable file"),
                                                    defaultDir,
                                                    tr("Text files (*.txt);;All files (*)"));

    if (!fileName.isEmpty())
    {
        if (QFileInfo(fileName).suffix().isEmpty())
            fileName += ".txt";

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

        const QString dirPath = QFileInfo(fileName).absolutePath();
        if (!dirPath.isEmpty())
            settings.setValue(kLastDumpDirKey, dirPath);

        QApplication::restoreOverrideCursor();
    }

    close();
}

void DumpScriptDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        ui->retranslateUi(this);
    QDialog::changeEvent(event);
}


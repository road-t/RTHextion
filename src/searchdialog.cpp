#include "searchdialog.h"
#include "ui_searchdialog.h"

#include <QMessageBox>
#include <QLineEdit>

SearchDialog::SearchDialog(QHexEdit *hexEdit, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SearchDialog),
    _hexEdit(hexEdit)
{
  ui->setupUi(this);
  setWindowFlags(Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowStaysOnTopHint | Qt::WindowCloseButtonHint | Qt::Window);
}

SearchDialog::~SearchDialog()
{
  delete ui;
}

qint64 SearchDialog::findNext()
{
    qint64 from = _hexEdit->cursorPosition() / 2;
    _findBa = getContent(ui->cbFindFormat->currentIndex(), ui->cbFind->currentText());
    qint64 idx = -1;

    if (_findBa.length() > 0)
    {
        if (ui->cbUseTable->isChecked())
        {
            auto tb = _hexEdit->getTranslationTable();

            if (tb)
            {
                _findBa = tb->decode(_findBa);
            }
        }

        if (ui->cbRelative->isChecked())
        {
            idx = _hexEdit->relativeSearch(_findBa, from);
        }
        else
        {
            if (ui->cbBackwards->isChecked())
                idx = _hexEdit->lastIndexOf(_findBa, from);
            else
                idx = _hexEdit->indexOf(_findBa, from);
        }
    }

    return idx;
}

void SearchDialog::on_pbFind_clicked()
{
    findNext();
}

void SearchDialog::on_pbReplace_clicked()
{
    int idx = findNext();
    if (idx >= 0)
    {
        QByteArray replaceBa = getContent(ui->cbReplaceFormat->currentIndex(), ui->cbReplace->currentText());
        replaceOccurrence(idx, replaceBa);
    }
}

void SearchDialog::on_pbReplaceAll_clicked()
{
    int replaceCounter = 0;
    int idx = 0;
    int goOn = QMessageBox::Yes;

    while ((idx >= 0) && (goOn == QMessageBox::Yes))
    {
        idx = findNext();
        if (idx >= 0)
        {
            QByteArray replaceBa = getContent(ui->cbReplaceFormat->currentIndex(), ui->cbReplace->currentText());
            int result = replaceOccurrence(idx, replaceBa);

            if (result == QMessageBox::Yes)
                replaceCounter += 1;

            if (result == QMessageBox::Cancel)
                goOn = result;
        }
    }

    if (replaceCounter > 0)
        QMessageBox::information(this, tr("QHexEdit"), QString(tr("%1 occurrences replaced.")).arg(replaceCounter));
}


QByteArray SearchDialog::getContent(int comboIndex, const QString &input)
{
    QByteArray findBa;
    switch (comboIndex)
    {
        case 0:     // text
            findBa = input.toUtf8();
            break;

        case 1:     // hex
            findBa = QByteArray::fromHex(input.toLatin1());
            break;
    }

    return findBa;
}

qint64 SearchDialog::replaceOccurrence(qint64 idx, const QByteArray &replaceBa)
{
    int result = QMessageBox::Yes;
    if (replaceBa.length() >= 0)
    {
        if (ui->cbPrompt->isChecked())
        {
            result = QMessageBox::question(this, tr("QHexEdit"),
                     tr("Replace occurrence?"),
                     QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

            if (result == QMessageBox::Yes)
            {
                _hexEdit->replace(idx, replaceBa.length(), replaceBa);
                _hexEdit->update();
            }
        }
        else
        {
            _hexEdit->replace(idx, _findBa.length(), replaceBa);
        }
    }
    return result;
}

void SearchDialog::on_cbFindFormat_currentIndexChanged(int index)
{
    ui->cbFind->lineEdit()->setInputMask(index ? ">hhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhh" : "");
}


void SearchDialog::on_cbReplaceFormat_currentIndexChanged(int index)
{
    ui->cbReplace->lineEdit()->setInputMask(index ? ">hhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhh" : "");
}


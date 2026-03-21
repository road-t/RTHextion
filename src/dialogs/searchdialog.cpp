#include "searchdialog.h"
#include "ui_searchdialog.h"

#include <QEvent>
#include <QMessageBox>
#include <QLineEdit>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QStringDecoder>
#include <QStringEncoder>

SearchDialog::SearchDialog(HexEditor *hexEdit, QWidget *parent) :
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

void SearchDialog::setHexEdit(HexEditor *hexEdit)
{
        _hexEdit = hexEdit;
}

void SearchDialog::setUseTableChecked(bool checked)
{
    if (ui && ui->cbUseTable)
    ui->cbUseTable->setChecked(checked);
}

qint64 SearchDialog::findNext()
{
    if (!_hexEdit)
        return -1;

    qint64 from = _hexEdit->cursorPosition() / 2;
    _findBa = getContent(ui->cbFindFormat->currentIndex(), ui->cbFind->currentText());
    qint64 idx = -1;

    if (_findBa.length() > 0)
    {
        if (ui->cbRelative->isChecked())
        {
            idx = _hexEdit->relativeSearch(_findBa, from);
        }
        else
        {
            idx = _hexEdit->indexOf(_findBa, from);
        }
    }

    return idx;
}

qint64 SearchDialog::findPrevious()
{
    if (!_hexEdit)
        return -1;

    qint64 from = _hexEdit->cursorPosition() / 2;
    if (from > 0)
        --from;

    _findBa = getContent(ui->cbFindFormat->currentIndex(), ui->cbFind->currentText());
    if (_findBa.isEmpty())
        return -1;

    // Relative backward search isn't defined in current HexEditor API,
    // so Find prev always performs regular backward search.
    return _hexEdit->lastIndexOf(_findBa, from);
}

void SearchDialog::on_pbFind_clicked()
{
    findNext();
}

void SearchDialog::on_pbFindPrev_clicked()
{
    findPrevious();
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
        QMessageBox::information(this, tr("Replace"), QString(tr("%1 occurrences replaced")).arg(replaceCounter));
}


QByteArray SearchDialog::getContent(int comboIndex, const QString &input)
{
    QByteArray findBa;
    switch (comboIndex)
    {
        case 0:     // text
            if (!_hexEdit)
            {
                findBa = input.toUtf8();
            }
            else if (ui->cbUseTable->isChecked() && _hexEdit->getTranslationTable())
            {
                findBa = _hexEdit->getTranslationTable()->decode(input.toUtf8());
            }
            else
            {
                const QString encoding = _hexEdit->currentEncoding();
                if (encoding.isEmpty() || encoding == QLatin1String("ASCII"))
                {
                    const QByteArray encoded = input.toLatin1();
                    findBa = (QString::fromLatin1(encoded) == input) ? encoded : QByteArray();
                }
                else
                {
                    QStringEncoder enc(encoding.toUtf8().constData());
                    if (enc.isValid()) {
                        const QByteArray encoded = enc.encode(input);
                        QStringDecoder dec(encoding.toUtf8().constData());
                        findBa = (dec.isValid() && dec.decode(encoded) == input) ? encoded : QByteArray();
                    } else {
                        // Do not silently fall back to another encoding for text search.
                        // Lossy conversion creates false positives (e.g. 0x3F sequences).
                        findBa.clear();
                    }
                }
            }
            break;

        case 1:     // hex
            findBa = QByteArray::fromHex(input.simplified().remove(' ').toLatin1());
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
            result = QMessageBox::question(this, tr("Replace"),
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
    auto *le = ui->cbFind->lineEdit();
    le->setInputMask("");
    le->setValidator(index ? new QRegularExpressionValidator(
        QRegularExpression("[0-9A-Fa-f ]*"), le) : nullptr);
}


void SearchDialog::on_cbReplaceFormat_currentIndexChanged(int index)
{
    auto *le = ui->cbReplace->lineEdit();
    le->setInputMask("");
    le->setValidator(index ? new QRegularExpressionValidator(
        QRegularExpression("[0-9A-Fa-f ]*"), le) : nullptr);
}

void SearchDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        ui->retranslateUi(this);
    QDialog::changeEvent(event);
}

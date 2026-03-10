#include "InsertScriptDialog.h"
#include "ui_InsertScriptDialog.h"
#include "Datas.h"
#include <QtEndian>
#include <QEvent>
#include <QFile>
#include <QMessageBox>
#include <algorithm>

InsertScriptDialog::InsertScriptDialog(QHexEdit *hexEdit, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::InsertScriptDialog),
    hexEdit(hexEdit)
{
    ui->setupUi(this);
}

void InsertScriptDialog::showEvent(QShowEvent *ev)
{
    Q_UNUSED(ev);

    auto _tb = hexEdit->getTranslationTable();

    tb = _tb;

    ui->cbUseTable->setDisabled(!tb);
    ui->cbUseTable->setChecked(tb);

    updateText();
}

InsertScriptDialog::~InsertScriptDialog()
{
    delete ui;
}

void InsertScriptDialog::on_bbControls_accepted()
{

}

void InsertScriptDialog::updateText()
{

}

void InsertScriptDialog::setRomProfile(int pointerSize, qint64 pointerOffset)
{
    _pointerSize = pointerSize;
    _pointerOffset = pointerOffset;
}

void InsertScriptDialog::on_bbControls_clicked(QAbstractButton *button)
{
    if (ui->bbControls->standardButton(button) == QDialogButtonBox::Open)
    {
        QSettings settings;
        const QString defaultDir = settings.value("Paths/LastDumpDir", QDir::homePath()).toString();
        const QString fileName = QFileDialog::getOpenFileName(this, tr("Open script"), defaultDir, tr("Text files (*.txt *.script *.scr);;All files (*)"));
        if (fileName.isEmpty())
            return;

        QFile file(fileName);
        if (!file.open(QFile::ReadOnly | QFile::Text))
        {
            QMessageBox::warning(this, tr("Open script"),
                                 tr("Cannot read file %1:\n%2.")
                                 .arg(fileName)
                                 .arg(file.errorString()));
            return;
        }

        ui->pteScript->setPlainText(QString::fromUtf8(file.readAll()));
        const QString dirPath = QFileInfo(fileName).absolutePath();
        if (!dirPath.isEmpty())
            settings.setValue("Paths/LastDumpDir", dirPath);
        return;
    }

    if (ui->bbControls->buttonRole(button) == QDialogButtonBox::ButtonRole::AcceptRole)
    {
        /* regex to match dumps like:

         {|abc0123|}:<any space character>
         dump chunk
         <any space character>
         and so on

        */
        static QRegularExpression re(
                    "\\{\\|([a-f0-9:,]+)\\|\\}:\\s*(.*)(?=(?:\\{\\|)|$)\\s*",
                    QRegularExpression::CaseInsensitiveOption |
                    QRegularExpression::DotMatchesEverythingOption |
                    QRegularExpression::InvertedGreedinessOption
                    );

        static QRegularExpression reNoNewlines("[\r\n]+");

        qDebug() << "Capture groups: " << re.captureCount();

        auto offset = hexEdit->getCurrentOffset();

    //    if (ui->cbUseTable->isChecked())
    //    {
            auto script = ui->pteScript->toPlainText();

            QRegularExpressionMatchIterator it = re.globalMatch(script, 0, QRegularExpression::PartialPreferCompleteMatch);

            while (it.hasNext())
            {
                QRegularExpressionMatch match = it.next();

                if (match.hasMatch())
                {
                     qDebug() << ": [1] " << match.captured(1) << "[2] " << match.captured(2);

                     // insert pointed chunk
                     auto line = tb->decode(match.captured(2).remove(reNoNewlines).toUtf8());
                     auto length = line.length();

                     hexEdit->replace(offset, length, line);

                     // update pointers to it
                     auto pointers = match.captured(1).split(',');

                     for (const auto& i : pointers)
                     {
                         const QStringList parts = i.trimmed().split(':');
                         const quint64 ptrOffset = parts[0].trimmed().toULongLong(nullptr, 16);
                         const int perPtrSize = (parts.size() > 1) ? parts[1].trimmed().toInt() : _pointerSize;
                         const int effectivePtrSize = (perPtrSize == 2 || perPtrSize == 3 || perPtrSize == 4)
                                                          ? perPtrSize : _pointerSize;

                         // Reverse the offset: raw_pointer = file_offset - pointerOffset
                         const qint64 rawPointerValue = static_cast<qint64>(offset) - _pointerOffset;

                         QByteArray data(effectivePtrSize, 0);
                         uchar *raw = reinterpret_cast<uchar *>(data.data());

                         if (effectivePtrSize == 2)
                         {
                             const quint16 val16 = static_cast<quint16>(rawPointerValue);
                             if (hexEdit->byteOrder == ByteOrder::BigEndian)
                                 qToBigEndian<quint16>(val16, raw);
                             else
                             {
                                 qToLittleEndian<quint16>(val16, raw);
                                 if (hexEdit->byteOrder == ByteOrder::SwappedBytes)
                                     std::swap(data[0], data[1]);
                             }
                         }
                         else
                         {
                             const quint32 val32 = static_cast<quint32>(rawPointerValue);
                             if (hexEdit->byteOrder == ByteOrder::BigEndian)
                                 qToBigEndian<quint32>(val32, raw);
                             else
                             {
                                 qToLittleEndian<quint32>(val32, raw);
                                 if (hexEdit->byteOrder == ByteOrder::SwappedBytes)
                                 {
                                     std::swap(data[0], data[1]);
                                     std::swap(data[2], data[3]);
                                 }
                             }
                         }

                         hexEdit->replace(ptrOffset, effectivePtrSize, data);
                     }


                     offset += length;
                }
            }
    //    }
    }
}


void InsertScriptDialog::on_cbUpdatePointers_stateChanged(int arg1)
{
    Q_UNUSED(arg1);
}

void InsertScriptDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        ui->retranslateUi(this);
    QDialog::changeEvent(event);
}


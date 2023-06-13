#include "InsertScriptDialog.h"
#include "ui_InsertScriptDialog.h"
#include "Datas.h"

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

    bigEndian = hexEdit->bigEndian;
    bigEndian ? ui->rbBigEndian->setChecked(true) : ui->rbLittleEndian->setChecked(true);

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

void InsertScriptDialog::on_bbControls_clicked(QAbstractButton *button)
{
    if (ui->bbControls->buttonRole(button) == QDialogButtonBox::ButtonRole::AcceptRole)
    {
        /* regex to match dumps like:

         {|abc0123|}:<any space character>
         dump chunk
         <any space character>
         and so on

        */
        static QRegularExpression re(
                    "\\{\\|([a-f0-9,]+)\\|\\}:\\s*(.*)(?=(?:\\{\\|)|$)\\s*",
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
                         auto ptrOffset = i.toUInt(nullptr, 16);

                         Datas newPtr;

                         if (bigEndian)
                            newPtr.beDword = offset;
                         else
                             newPtr.leDword = offset;

                         auto data = QByteArray(newPtr.chr, 4);

                         hexEdit->replace(ptrOffset, 4, data); // TODO: store offset size in dump and use endiannes
                     }


                     offset += length;
                }
            }
    //    }
    }
}


void InsertScriptDialog::on_cbUpdatePointers_stateChanged(int arg1)
{
    ui->rbLayout->setEnabled(arg1);
}


void InsertScriptDialog::on_rbLittleEndian_toggled(bool checked)
{
    bigEndian = !checked;
}


void InsertScriptDialog::on_rbBigEndian_toggled(bool checked)
{
    bigEndian = checked;
}


#include <QPushButton>
#include <QEvent>
#include <QRegularExpression>
#include <QRegularExpressionValidator>

#include "JumpToDialog.h"
#include "ui_JumpToDialog.h"

JumpToDialog::JumpToDialog(HexEditor *hexEdit, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::JumpToDialog),
    _hexEdit(hexEdit)
{
    ui->setupUi(this);
    ui->bbControls->button(QDialogButtonBox::Ok)->setText(tr("Go"));

    ui->leOffset->setInputMask("");
    ui->leOffset->setValidator(new QRegularExpressionValidator(
        QRegularExpression("[+-]?[0-9A-Fa-f]*"), ui->leOffset));

    setWindowFlags(Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowStaysOnTopHint | Qt::WindowCloseButtonHint | Qt::Window);
}

JumpToDialog::~JumpToDialog()
{
    delete ui;
}

void JumpToDialog::setHexEdit(HexEditor *hexEdit)
{
    _hexEdit = hexEdit;
}

QString JumpToDialog::offsetText() const
{
    return ui->leOffset->text();
}

void JumpToDialog::setOffsetText(const QString &text)
{
    ui->leOffset->setText(text);
}

void JumpToDialog::on_bbControls_accepted()
{
    if (!_hexEdit)
        return;

    auto relative = ui->leOffset->text().startsWith('+') || ui->leOffset->text().startsWith('-');
    auto offset = ui->leOffset->text().toLong(nullptr, 16);

    _hexEdit->jumpTo(offset, relative);
}

void JumpToDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        ui->retranslateUi(this);
        ui->bbControls->button(QDialogButtonBox::Ok)->setText(tr("Go"));
    }
    QDialog::changeEvent(event);
}


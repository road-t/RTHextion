#include <QPushButton>

#include "JumpToDialog.h"
#include "ui_JumpToDialog.h"

JumpToDialog::JumpToDialog(QHexEdit *hexEdit, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::JumpToDialog),
    _hexEdit(hexEdit)
{
    ui->setupUi(this);
    ui->bbControls->button(QDialogButtonBox::Ok)->setText("Go");

    setWindowFlags(Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowStaysOnTopHint | Qt::WindowCloseButtonHint | Qt::Window);
}

JumpToDialog::~JumpToDialog()
{
    delete ui;
}

void JumpToDialog::on_bbControls_accepted()
{
    auto relative = ui->leOffset->text().startsWith('+') || ui->leOffset->text().startsWith('-');
    auto offset = ui->leOffset->text().toLong(nullptr, 16);

    _hexEdit->jumpTo(offset, relative);
}


#ifndef JUMPTODIALOG_H
#define JUMPTODIALOG_H

#include <QDialog>
#include "hexeditor/hexeditor.h"

namespace Ui {
class JumpToDialog;
}

class JumpToDialog : public QDialog
{
    Q_OBJECT

public:
    explicit JumpToDialog(HexEditor *hexEdit, QWidget *parent = nullptr);
    ~JumpToDialog();

    void setHexEdit(HexEditor *hexEdit);
    QString offsetText() const;
    void setOffsetText(const QString &text);

protected:
    void changeEvent(QEvent *event) override;

private slots:
    void on_bbControls_accepted();

private:
    Ui::JumpToDialog *ui;
    HexEditor *_hexEdit;
};

#endif // JUMPTODIALOG_H

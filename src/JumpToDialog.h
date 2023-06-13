#ifndef JUMPTODIALOG_H
#define JUMPTODIALOG_H

#include <QDialog>
#include "qhexedit/qhexedit.h"

namespace Ui {
class JumpToDialog;
}

class JumpToDialog : public QDialog
{
    Q_OBJECT

public:
    explicit JumpToDialog(QHexEdit *hexEdit, QWidget *parent = nullptr);
    ~JumpToDialog();

private slots:
    void on_bbControls_accepted();

private:
    Ui::JumpToDialog *ui;
    QHexEdit *_hexEdit;
};

#endif // JUMPTODIALOG_H

#ifndef POINTERSDIALOG_H
#define POINTERSDIALOG_H

#include <QDialog>
#include <QAbstractButton>
#include "qhexedit/qhexedit.h"
#include "PointerListModel.h"

namespace Ui {
class PointersDialog;
}

class PointersDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PointersDialog(QHexEdit *hexEdit, QWidget *parent = nullptr);
    ~PointersDialog();

private slots:
    void on_bbControls_accepted();

    void on_bbControls_rejected();

    void on_bbControls_clicked(QAbstractButton *button);

    void on_cbOptimize_stateChanged(int arg1);

    void on_cbRangeStart_currentIndexChanged(int index);

    void on_cbRangeEnd_currentIndexChanged(int index);

    void on_tvPointers_doubleClicked(const QModelIndex &index);

    void keyPressEvent(QKeyEvent *event);

protected:
    void showEvent(QShowEvent *ev);
    PointerListModel* plModel = nullptr;

private:
    Ui::PointersDialog *ui;
    QHexEdit *_hexEdit;
    TranslationTable* tb;
};

#endif // POINTERSDIALOG_H

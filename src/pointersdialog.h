#ifndef POINTERSDIALOG_H
#define POINTERSDIALOG_H

#include <QDialog>
#include <QAbstractButton>
#include <QFuture>
#include <atomic>
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
    void refreshFromTable();

private slots:
    void on_bbControls_accepted();
    void on_bbControls_rejected();
    void on_bbControls_clicked(QAbstractButton *button);
    void on_cbOptimize_stateChanged(int arg1);
    void on_cbRangeStart_currentIndexChanged(int index);
    void on_cbRangeEnd_currentIndexChanged(int index);
    void on_tvPointers_doubleClicked(const QModelIndex &index);
    void keyPressEvent(QKeyEvent *event);
    void updateProgress(int percent, int found);
    void on_btnStop_clicked();
    void on_btnAddPointer_clicked();
    void on_btnDeletePointer_clicked();
    void on_btnCleanAll_clicked();
    void on_leRangeBegin_textChanged(const QString &text);
    void on_leRangeEnd_textChanged(const QString &text);
    void finishSearchUi(bool cancelled, int found, qint64 elapsedMs);

protected:
    void showEvent(QShowEvent *ev);
    void changeEvent(QEvent *event) override;
    PointerListModel* plModel = nullptr;
    QFuture<void> searchFuture;
    std::atomic<bool> cancelRequested{false};
    bool searchActive = false;

    qint64 parseHexField(const QString &text, bool *ok) const;
    bool validateRangeInputs();

private:
    Ui::PointersDialog *ui;
    QHexEdit *_hexEdit;
    TranslationTable* tb;
};

#endif // POINTERSDIALOG_H

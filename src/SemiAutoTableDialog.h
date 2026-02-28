#ifndef SEMIAUTOTABLEDIALOG_H
#define SEMIAUTOTABLEDIALOG_H

#include <QDialog>

class QLineEdit;
class QPushButton;
class QHexEdit;
class TranslationTable;

class SemiAutoTableDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SemiAutoTableDialog(QHexEdit *hexEdit, TranslationTable **tb, QWidget *parent = nullptr);

signals:
    void tableGenerated();

private slots:
    void onFind();

private:
    void changeEvent(QEvent *event) override;
    void retranslateUi();

    QHexEdit *_hexEdit;
    TranslationTable **_tb;
    QLineEdit *_leSearch;
    QPushButton *_pbFind;
    QPushButton *_pbCancel;
};

#endif // SEMIAUTOTABLEDIALOG_H

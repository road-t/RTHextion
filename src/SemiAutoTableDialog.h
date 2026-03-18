#ifndef SEMIAUTOTABLEDIALOG_H
#define SEMIAUTOTABLEDIALOG_H

#include <QDialog>

#include "translationtable.h"

class QLineEdit;
class QPushButton;
class QHexEdit;

class SemiAutoTableDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SemiAutoTableDialog(QHexEdit *hexEdit, QWidget *parent = nullptr);
    bool hasGeneratedTable() const { return _hasGeneratedTable; }
    const TranslationTable &generatedTable() const { return _generatedTable; }

signals:
    void tableGenerated();

private slots:
    void onFind();

private:
    void changeEvent(QEvent *event) override;
    void retranslateUi();

    QHexEdit *_hexEdit;
    TranslationTable _generatedTable;
    bool _hasGeneratedTable = false;
    QLineEdit *_leSearch;
    QPushButton *_pbFind;
    QPushButton *_pbCancel;
};

#endif // SEMIAUTOTABLEDIALOG_H

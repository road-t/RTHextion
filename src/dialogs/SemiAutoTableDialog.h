#ifndef SEMIAUTOTABLEDIALOG_H
#define SEMIAUTOTABLEDIALOG_H

#include <QDialog>

#include "translationtable.h"

class QLineEdit;
class QPushButton;
class QLabel;
class HexEditor;

class SemiAutoTableDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SemiAutoTableDialog(HexEditor *hexEdit, QWidget *parent = nullptr);
    bool hasGeneratedTable() const { return _hasGeneratedTable; }
    const TranslationTable &generatedTable() const { return _generatedTable; }

signals:
    void tableGenerated();

private slots:
    void onFind();
    void updateFindButtonState();

private:
    void changeEvent(QEvent *event) override;
    void retranslateUi();
    bool hasEnoughUniqueCharacters(const QString &text) const;

    HexEditor *_hexEdit;
    TranslationTable _generatedTable;
    bool _hasGeneratedTable = false;
    QLineEdit *_leSearch;
    QPushButton *_pbFind;
    QPushButton *_pbCancel;
    QLabel *_lbHint;
};

#endif // SEMIAUTOTABLEDIALOG_H

#ifndef FILLWITHDIALOG_H
#define FILLWITHDIALOG_H

#include <QDialog>
#include <QVector>

class QComboBox;
class QLineEdit;
class QSpinBox;
class QStackedWidget;
class TranslationTable;
struct TableTab;

class FillWithDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FillWithDialog(qint64 selectionLength,
                            const QVector<TableTab> &tables,
                            QWidget *parent = nullptr);

    QByteArray fillByte() const;
    int fillLength() const;

private slots:
    void onTableChanged(int index);
    void onReplace();

private:
    void changeEvent(QEvent *event) override;
    void retranslateUi();
    void buildSymbolCombo();

    QComboBox      *_cbTable;
    QStackedWidget *_symbolStack;
    QLineEdit      *_leSymbol;       // page 0: Hex / Raw input
    QComboBox      *_cbSymbol;       // page 1: table symbol picker
    QSpinBox       *_spLength;
    QPushButton    *_pbReplace;
    QPushButton    *_pbCancel;

    const QVector<TableTab> &_tables;
    QByteArray _result;
};

#endif // FILLWITHDIALOG_H

#ifndef VIRTUALFORMATDIALOG_H
#define VIRTUALFORMATDIALOG_H

#include <QDialog>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QStackedWidget;
class QPushButton;
class TranslationTable;
struct TableTab;

class VirtualFormatDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VirtualFormatDialog(const QVector<TableTab> &tables,
                                 QWidget *parent = nullptr);

    QByteArray character() const;
    int lines() const;
    bool ignoreRepeated() const;

private slots:
    void onTableChanged(int index);
    void onOk();
    void updateOkEnabled();

private:
    void changeEvent(QEvent *event) override;
    void retranslateUi();
    void buildSymbolCombo();

    QComboBox      *_cbTable;
    QStackedWidget *_symbolStack;
    QLineEdit      *_leSymbol;       // page 0: Hex / Raw input
    QComboBox      *_cbSymbol;       // page 1: table symbol picker
    QSpinBox       *_spLines;
    QCheckBox      *_cbIgnoreRepeated;
    QPushButton    *_pbOk;
    QPushButton    *_pbCancel;

    // Labels for retranslation
    class QLabel *_lblTable;
    class QLabel *_lblCharacter;
    class QLabel *_lblLines;

    const QVector<TableTab> &_tables;
    QByteArray _result;
};

#endif // VIRTUALFORMATDIALOG_H

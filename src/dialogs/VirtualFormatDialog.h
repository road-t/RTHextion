#ifndef VIRTUALFORMATDIALOG_H
#define VIRTUALFORMATDIALOG_H

#include <QDialog>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QRadioButton;
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
                                 int activeTableIndex = -1,
                                 QWidget *parent = nullptr);

    bool splitByCount() const;       // true = every N bytes; false = by character
    int  countValue() const;         // N (only when splitByCount)
    QByteArray character() const;    // needle bytes (only when !splitByCount)
    int lines() const;
    bool ignoreRepeated() const;

private slots:
    void onTableChanged(int index);
    void onModeChanged();
    void onOk();
    void updateOkEnabled();

private:
    void changeEvent(QEvent *event) override;
    void retranslateUi();
    void buildSymbolCombo();

    QRadioButton   *_rbByChar;
    QRadioButton   *_rbByCount;
    QComboBox      *_cbTable;
    QStackedWidget *_symbolStack;
    QLineEdit      *_leSymbol;       // page 0: Hex / Raw input
    QComboBox      *_cbSymbol;       // page 1: table symbol picker
    QSpinBox       *_spCount;        // byte count for "split by count"
    QSpinBox       *_spLines;
    QCheckBox      *_cbIgnoreRepeated;
    QPushButton    *_pbOk;
    QPushButton    *_pbCancel;

    // Labels for retranslation
    class QLabel *_lblTable;
    class QLabel *_lblCharacter;
    class QLabel *_lblCount;
    class QLabel *_lblLines;

    const QVector<TableTab> &_tables;
    QByteArray _result;
    bool _isSplitByCount = false;
    int  _countResult = 1;
};

#endif // VIRTUALFORMATDIALOG_H

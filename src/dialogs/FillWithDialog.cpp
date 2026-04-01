#include "FillWithDialog.h"
#include "TablesDockWidget.h"
#include "translationtable.h"

#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

static int s_lastTableIndex = 0;
static QString s_lastSymbolText;
static int s_lastSymbolComboIndex = -1;

FillWithDialog::FillWithDialog(qint64 selectionLength,
                               const QVector<TableTab> &tables,
                               QWidget *parent)
    : QDialog(parent)
    , _tables(tables)
{
    setWindowTitle(tr("Fill with"));
    setModal(true);

    auto *form = new QFormLayout;

    // --- Table combo ---
    _cbTable = new QComboBox;
    _cbTable->addItem(QStringLiteral("Hex"));
    _cbTable->addItem(QStringLiteral("Raw"));
    for (const TableTab &tab : _tables)
        _cbTable->addItem(tab.name);
    form->addRow(tr("Table"), _cbTable);

    // --- Symbol (stacked: line edit / combo) ---
    _symbolStack = new QStackedWidget;

    _leSymbol = new QLineEdit;
    _symbolStack->addWidget(_leSymbol);           // page 0

    _cbSymbol = new QComboBox;
    _symbolStack->addWidget(_cbSymbol);            // page 1

    _symbolStack->setCurrentIndex(0);
    form->addRow(tr("Character"), _symbolStack);

    // --- Length spin ---
    _spLength = new QSpinBox;
    _spLength->setMinimum(1);
    _spLength->setMaximum(static_cast<int>(qMin<qint64>(selectionLength, INT_MAX)));
    _spLength->setValue(static_cast<int>(qMin<qint64>(selectionLength, INT_MAX)));
    form->addRow(tr("Length"), _spLength);

    // --- Buttons ---
    auto *btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    _pbReplace = new QPushButton(tr("Replace"));
    _pbReplace->setDefault(true);
    _pbCancel = new QPushButton(tr("Cancel"));
    btnLayout->addWidget(_pbReplace);
    btnLayout->addWidget(_pbCancel);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(form);
    mainLayout->addLayout(btnLayout);

    // Init symbol field for default (Hex)
    onTableChanged(0);

    connect(_cbTable, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FillWithDialog::onTableChanged);
    connect(_pbReplace, &QPushButton::clicked, this, &FillWithDialog::onReplace);
    connect(_pbCancel,  &QPushButton::clicked, this, &QDialog::reject);

    // Restore last used table selection
    if (s_lastTableIndex >= 0 && s_lastTableIndex < _cbTable->count())
        _cbTable->setCurrentIndex(s_lastTableIndex);

    // Restore last used symbol
    if (_symbolStack->currentIndex() == 0) {
        _leSymbol->setText(s_lastSymbolText);
    } else {
        if (s_lastSymbolComboIndex >= 0 && s_lastSymbolComboIndex < _cbSymbol->count())
            _cbSymbol->setCurrentIndex(s_lastSymbolComboIndex);
    }

    mainLayout->setSizeConstraint(QLayout::SetFixedSize);
}

void FillWithDialog::onTableChanged(int index)
{
    if (index == 0) {
        // Hex: exactly 2 hex chars
        _symbolStack->setCurrentIndex(0);
        _leSymbol->clear();
        _leSymbol->setMaxLength(2);
        _leSymbol->setValidator(
            new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9A-Fa-f]{0,2}")), _leSymbol));
        _leSymbol->setPlaceholderText(QStringLiteral("FF"));
    } else if (index == 1) {
        // Raw: single character
        _symbolStack->setCurrentIndex(0);
        _leSymbol->clear();
        _leSymbol->setMaxLength(1);
        _leSymbol->setValidator(nullptr);
        _leSymbol->setPlaceholderText(QStringLiteral("A"));
    } else {
        // Table: show combo with all table values
        _symbolStack->setCurrentIndex(1);
        buildSymbolCombo();
    }
    _symbolStack->setFixedHeight(_symbolStack->currentWidget()->sizeHint().height());
}

void FillWithDialog::buildSymbolCombo()
{
    _cbSymbol->clear();
    const int tableIdx = _cbTable->currentIndex() - 2;
    if (tableIdx < 0 || tableIdx >= _tables.size())
        return;

    const TranslationTable &tbl = _tables[tableIdx].table;

    // Single-byte entries
    auto *items = const_cast<TranslationTable &>(tbl).getItems();
    for (auto it = items->constBegin(); it != items->constEnd(); ++it) {
        const uint8_t byte = static_cast<uint8_t>(it.key());
        const QString &symbol = it.value();
        const QString hex = QString::number(byte, 16).toUpper().rightJustified(2, QLatin1Char('0'));
        _cbSymbol->addItem(QStringLiteral("%1  [%2]").arg(symbol, hex),
                           QByteArray(1, static_cast<char>(byte)));
    }

    // Multi-byte entries
    const auto &mbItems = tbl.getMultiByteItems();
    for (auto it = mbItems.constBegin(); it != mbItems.constEnd(); ++it) {
        const QByteArray &key = it.key();
        const QString &symbol = it.value();
        const QString hex = QString::fromLatin1(key.toHex(' ')).toUpper();
        _cbSymbol->addItem(QStringLiteral("%1  [%2]").arg(symbol, hex),
                           key);
    }
}

void FillWithDialog::onReplace()
{
    const int index = _cbTable->currentIndex();

    if (index == 0) {
        // Hex
        const QString text = _leSymbol->text();
        if (text.length() != 2)
            return;
        bool ok = false;
        const int val = text.toInt(&ok, 16);
        if (!ok)
            return;
        _result = QByteArray(1, static_cast<char>(val));
    } else if (index == 1) {
        // Raw
        const QString text = _leSymbol->text();
        if (text.isEmpty())
            return;
        _result = QByteArray(1, text.at(0).toLatin1());
    } else {
        // Table
        if (_cbSymbol->currentIndex() < 0)
            return;
        _result = _cbSymbol->currentData().toByteArray();
    }

    if (_result.isEmpty())
        return;

    // Remember selections for next invocation
    s_lastTableIndex = _cbTable->currentIndex();
    if (_symbolStack->currentIndex() == 0) {
        s_lastSymbolText = _leSymbol->text();
        s_lastSymbolComboIndex = -1;
    } else {
        s_lastSymbolText.clear();
        s_lastSymbolComboIndex = _cbSymbol->currentIndex();
    }

    accept();
}

QByteArray FillWithDialog::fillByte() const
{
    return _result;
}

int FillWithDialog::fillLength() const
{
    return _spLength->value();
}

void FillWithDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(event);
}

void FillWithDialog::retranslateUi()
{
    setWindowTitle(tr("Fill with"));
    _pbReplace->setText(tr("Replace"));
    _pbCancel->setText(tr("Cancel"));
}

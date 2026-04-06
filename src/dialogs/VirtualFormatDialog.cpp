#include "VirtualFormatDialog.h"
#include "TablesDockWidget.h"
#include "translationtable.h"

#include <QComboBox>
#include <QCheckBox>
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

static int s_vfLastTableIndex = 0;
static QString s_vfLastSymbolText;
static int s_vfLastSymbolComboIndex = -1;
static int s_vfLastLines = 1;
static bool s_vfLastIgnoreRepeated = false;

VirtualFormatDialog::VirtualFormatDialog(const QVector<TableTab> &tables,
                                         int activeTableIndex,
                                         QWidget *parent)
    : QDialog(parent)
    , _tables(tables)
{
    setWindowTitle(tr("Virtual formatting"));
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

    // --- Lines spin ---
    _spLines = new QSpinBox;
    _spLines->setMinimum(1);
    _spLines->setMaximum(100);
    _spLines->setValue(s_vfLastLines);
    form->addRow(tr("Lines"), _spLines);

    // --- Ignore repeated checkbox ---
    _cbIgnoreRepeated = new QCheckBox(tr("Ignore repeated"));
    _cbIgnoreRepeated->setChecked(s_vfLastIgnoreRepeated);
    form->addRow(QString(), _cbIgnoreRepeated);

    // --- Buttons ---
    auto *btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    _pbOk = new QPushButton(tr("OK"));
    _pbOk->setDefault(true);
    _pbCancel = new QPushButton(tr("Cancel"));
    btnLayout->addWidget(_pbOk);
    btnLayout->addWidget(_pbCancel);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(form);
    mainLayout->addLayout(btnLayout);

    // Store label ptrs for retranslation
    // QFormLayout stores labels as items; retrieve them
    _lblTable     = qobject_cast<QLabel *>(form->labelForField(_cbTable));
    _lblCharacter = qobject_cast<QLabel *>(form->labelForField(_symbolStack));
    _lblLines     = qobject_cast<QLabel *>(form->labelForField(_spLines));

    // Init symbol field for default (Hex)
    onTableChanged(0);

    connect(_cbTable, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VirtualFormatDialog::onTableChanged);
    connect(_pbOk,     &QPushButton::clicked, this, &VirtualFormatDialog::onOk);
    connect(_pbCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(_leSymbol, &QLineEdit::textChanged, this, &VirtualFormatDialog::updateOkEnabled);
    connect(_cbSymbol, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VirtualFormatDialog::updateOkEnabled);

    // Preselect: active table if given, otherwise last used selection
    const int comboTarget = (activeTableIndex >= 0 && activeTableIndex < _tables.size())
                                ? activeTableIndex + 2  // +2 for Hex/Raw
                                : s_vfLastTableIndex;
    if (comboTarget >= 0 && comboTarget < _cbTable->count())
        _cbTable->setCurrentIndex(comboTarget);

    // Restore last used symbol
    if (_symbolStack->currentIndex() == 0) {
        _leSymbol->setText(s_vfLastSymbolText);
    } else {
        if (s_vfLastSymbolComboIndex >= 0 && s_vfLastSymbolComboIndex < _cbSymbol->count())
            _cbSymbol->setCurrentIndex(s_vfLastSymbolComboIndex);
    }

    updateOkEnabled();

    mainLayout->setSizeConstraint(QLayout::SetFixedSize);
}

void VirtualFormatDialog::onTableChanged(int index)
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
    updateOkEnabled();
}

void VirtualFormatDialog::buildSymbolCombo()
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

void VirtualFormatDialog::updateOkEnabled()
{
    bool hasChar = false;
    const int index = _cbTable->currentIndex();
    if (index == 0) {
        // Hex: need exactly 2 chars
        hasChar = _leSymbol->text().length() == 2;
    } else if (index == 1) {
        // Raw: need 1 char
        hasChar = !_leSymbol->text().isEmpty();
    } else {
        // Table: need valid selection
        hasChar = _cbSymbol->currentIndex() >= 0 && _cbSymbol->count() > 0;
    }
    _pbOk->setEnabled(hasChar);
}

void VirtualFormatDialog::onOk()
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
    s_vfLastTableIndex = _cbTable->currentIndex();
    s_vfLastLines = _spLines->value();
    s_vfLastIgnoreRepeated = _cbIgnoreRepeated->isChecked();
    if (_symbolStack->currentIndex() == 0) {
        s_vfLastSymbolText = _leSymbol->text();
        s_vfLastSymbolComboIndex = -1;
    } else {
        s_vfLastSymbolText.clear();
        s_vfLastSymbolComboIndex = _cbSymbol->currentIndex();
    }

    accept();
}

QByteArray VirtualFormatDialog::character() const
{
    return _result;
}

int VirtualFormatDialog::lines() const
{
    return _spLines->value();
}

bool VirtualFormatDialog::ignoreRepeated() const
{
    return _cbIgnoreRepeated->isChecked();
}

void VirtualFormatDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(event);
}

void VirtualFormatDialog::retranslateUi()
{
    setWindowTitle(tr("Virtual formatting"));
    _pbOk->setText(tr("OK"));
    _pbCancel->setText(tr("Cancel"));
    if (_lblTable)     _lblTable->setText(tr("Table"));
    if (_lblCharacter) _lblCharacter->setText(tr("Character"));
    if (_lblLines)     _lblLines->setText(tr("Lines"));
    if (_cbIgnoreRepeated) _cbIgnoreRepeated->setText(tr("Ignore repeated"));
}

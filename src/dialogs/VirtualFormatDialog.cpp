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
#include <QRadioButton>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

static int s_vfLastTableIndex = 0;
static QString s_vfLastSymbolText;
static int s_vfLastSymbolComboIndex = -1;
static int s_vfLastLines = 1;
static bool s_vfLastIgnoreRepeated = false;
static bool s_vfLastSplitByCount = false;
static int s_vfLastCount = 16;

VirtualFormatDialog::VirtualFormatDialog(const QVector<TableTab> &tables,
                                         int activeTableIndex,
                                         QWidget *parent)
    : QDialog(parent)
    , _tables(tables)
{
    setWindowTitle(tr("Virtual formatting"));
    setModal(true);

    auto *mainLayout = new QVBoxLayout(this);

    // --- Split mode radio buttons ---
    _rbByChar  = new QRadioButton(tr("Split by character sequence"));
    _rbByCount = new QRadioButton(tr("Split by byte count"));
    _rbByChar->setChecked(!s_vfLastSplitByCount);
    _rbByCount->setChecked(s_vfLastSplitByCount);
    mainLayout->addWidget(_rbByChar);
    mainLayout->addWidget(_rbByCount);

    auto *form = new QFormLayout;

    // --- Table combo (for character mode) ---
    _cbTable = new QComboBox;
    _cbTable->addItem(QStringLiteral("Hex"));
    _cbTable->addItem(QStringLiteral("Raw"));
    for (const TableTab &tab : _tables)
        _cbTable->addItem(tab.name);
    _lblTable = new QLabel(tr("Table"));
    form->addRow(_lblTable, _cbTable);

    // --- Symbol (stacked: line edit / combo) ---
    _symbolStack = new QStackedWidget;

    _leSymbol = new QLineEdit;
    _symbolStack->addWidget(_leSymbol);           // page 0

    _cbSymbol = new QComboBox;
    _symbolStack->addWidget(_cbSymbol);            // page 1

    _symbolStack->setCurrentIndex(0);
    _lblCharacter = new QLabel(tr("Character"));
    form->addRow(_lblCharacter, _symbolStack);

    // --- Count spin (for count mode) ---
    _spCount = new QSpinBox;
    _spCount->setMinimum(1);
    _spCount->setMaximum(999999);
    _spCount->setValue(s_vfLastCount);
    _lblCount = new QLabel(tr("Byte count"));
    form->addRow(_lblCount, _spCount);

    // --- Line feeds spin ---
    _spLines = new QSpinBox;
    _spLines->setMinimum(1);
    _spLines->setMaximum(100);
    _spLines->setValue(s_vfLastLines);
    _lblLines = new QLabel(tr("Line feeds"));
    form->addRow(_lblLines, _spLines);

    // --- Ignore repeated checkbox ---
    _cbIgnoreRepeated = new QCheckBox(tr("Ignore repeated"));
    _cbIgnoreRepeated->setChecked(s_vfLastIgnoreRepeated);
    form->addRow(QString(), _cbIgnoreRepeated);

    mainLayout->addLayout(form);

    // --- Buttons ---
    auto *btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    _pbOk = new QPushButton(tr("OK"));
    _pbOk->setDefault(true);
    _pbCancel = new QPushButton(tr("Cancel"));
    btnLayout->addWidget(_pbOk);
    btnLayout->addWidget(_pbCancel);
    mainLayout->addLayout(btnLayout);

    // Init symbol field for default (Hex)
    onTableChanged(0);

    // Initial mode setup
    onModeChanged();

    connect(_rbByChar,  &QRadioButton::toggled, this, &VirtualFormatDialog::onModeChanged);
    connect(_cbTable, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VirtualFormatDialog::onTableChanged);
    connect(_pbOk,     &QPushButton::clicked, this, &VirtualFormatDialog::onOk);
    connect(_pbCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(_leSymbol, &QLineEdit::textChanged, this, &VirtualFormatDialog::updateOkEnabled);
    connect(_cbSymbol, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &VirtualFormatDialog::updateOkEnabled);
    connect(_spCount, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { updateOkEnabled(); });

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

void VirtualFormatDialog::onModeChanged()
{
    const bool byChar = _rbByChar->isChecked();
    _lblTable->setVisible(byChar);
    _cbTable->setVisible(byChar);
    _lblCharacter->setVisible(byChar);
    _symbolStack->setVisible(byChar);
    _cbIgnoreRepeated->setVisible(byChar);
    _lblCount->setVisible(!byChar);
    _spCount->setVisible(!byChar);
    updateOkEnabled();
}

void VirtualFormatDialog::onTableChanged(int index)
{
    if (index == 0) {
        // Hex: any number of hex char pairs
        _symbolStack->setCurrentIndex(0);
        _leSymbol->clear();
        _leSymbol->setMaxLength(512);
        _leSymbol->setValidator(
            new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9A-Fa-f ]*")), _leSymbol));
        _leSymbol->setPlaceholderText(QStringLiteral("FF 00 AB"));
    } else if (index == 1) {
        // Raw: any text
        _symbolStack->setCurrentIndex(0);
        _leSymbol->clear();
        _leSymbol->setMaxLength(256);
        _leSymbol->setValidator(nullptr);
        _leSymbol->setPlaceholderText(QStringLiteral("text"));
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
    if (_rbByCount->isChecked()) {
        _pbOk->setEnabled(_spCount->value() > 0);
        return;
    }

    bool hasChar = false;
    const int index = _cbTable->currentIndex();
    if (index == 0) {
        // Hex: need at least one complete byte pair (2 hex chars, ignoring spaces)
        const QString stripped = _leSymbol->text().remove(' ');
        hasChar = stripped.length() >= 2 && (stripped.length() % 2 == 0);
    } else if (index == 1) {
        // Raw: need at least 1 char
        hasChar = !_leSymbol->text().isEmpty();
    } else {
        // Table: need valid selection
        hasChar = _cbSymbol->currentIndex() >= 0 && _cbSymbol->count() > 0;
    }
    _pbOk->setEnabled(hasChar);
}

void VirtualFormatDialog::onOk()
{
    _isSplitByCount = _rbByCount->isChecked();

    if (_isSplitByCount) {
        _countResult = _spCount->value();
        if (_countResult <= 0)
            return;
    } else {
        const int index = _cbTable->currentIndex();

        if (index == 0) {
            // Hex: arbitrary length, strip spaces and decode
            const QString stripped = _leSymbol->text().remove(' ');
            if (stripped.length() < 2 || (stripped.length() % 2 != 0))
                return;
            _result = QByteArray::fromHex(stripped.toLatin1());
        } else if (index == 1) {
            // Raw: use the full text as Latin-1 bytes
            const QString text = _leSymbol->text();
            if (text.isEmpty())
                return;
            _result = text.toLatin1();
        } else {
            // Table
            if (_cbSymbol->currentIndex() < 0)
                return;
            _result = _cbSymbol->currentData().toByteArray();
        }

        if (_result.isEmpty())
            return;
    }

    // Remember selections for next invocation
    s_vfLastTableIndex = _cbTable->currentIndex();
    s_vfLastLines = _spLines->value();
    s_vfLastIgnoreRepeated = _cbIgnoreRepeated->isChecked();
    s_vfLastSplitByCount = _isSplitByCount;
    s_vfLastCount = _spCount->value();
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

bool VirtualFormatDialog::splitByCount() const
{
    return _isSplitByCount;
}

int VirtualFormatDialog::countValue() const
{
    return _countResult;
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
    _rbByChar->setText(tr("Split by character sequence"));
    _rbByCount->setText(tr("Split by byte count"));
    _pbOk->setText(tr("OK"));
    _pbCancel->setText(tr("Cancel"));
    if (_lblTable)     _lblTable->setText(tr("Table"));
    if (_lblCharacter) _lblCharacter->setText(tr("Character"));
    if (_lblCount)     _lblCount->setText(tr("Byte count"));
    if (_lblLines)     _lblLines->setText(tr("Line feeds"));
    if (_cbIgnoreRepeated) _cbIgnoreRepeated->setText(tr("Ignore repeated"));
}

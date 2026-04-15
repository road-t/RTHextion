#include "searchdialog.h"

#include <QEvent>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QStringDecoder>
#include <QStringEncoder>

SearchDialog::SearchDialog(HexEditor *hexEdit, QWidget *parent) :
    QDialog(parent),
    _hexEdit(hexEdit)
{
    setWindowTitle(tr("Find/Replace"));
    setWindowFlags(Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowStaysOnTopHint | Qt::WindowCloseButtonHint | Qt::Window);

    auto *mainLayout = new QVBoxLayout(this);

    // ── Find group ──────────────────────────────────
    auto *gbFind = new QGroupBox(tr("Find"), this);
    auto *findGrid = new QGridLayout(gbFind);

    cbFind = new QComboBox(this);
    cbFind->setEditable(true);
    cbFind->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    pbFind = new QPushButton(tr("Find next"), this);
    pbFind->setDefault(true);
    pbFind->setShortcut(QKeySequence(Qt::Key_F3));

    pbFindPrev = new QPushButton(tr("Find prev"), this);
    pbFindPrev->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3));

    cmbFindTable = new QComboBox(this);
    cmbFindTable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    cbRelative = new QCheckBox(tr("Relative search"), this);
    lbMatchStatus = new QLabel(tr("No active match"), this);
    lbMatchStatus->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lbMatchStatus->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    // Row 0: input + find next
    findGrid->addWidget(cbFind, 0, 0, 1, 3);
    findGrid->addWidget(pbFind, 0, 3);

    // Row 1:  + find prev
    findGrid->addWidget(cmbFindTable, 1, 0, 1, 3);
    findGrid->addWidget(pbFindPrev, 1, 3);

    // Row 2: relative checkbox
    findGrid->addWidget(cbRelative, 2, 0, 1, 2);
    findGrid->addWidget(lbMatchStatus, 2, 2, 1, 2);

    findGrid->setColumnStretch(1, 1);

    mainLayout->addWidget(gbFind);

    // ── Replace group ───────────────────────────────
    auto *gbReplace = new QGroupBox(tr("Replace"), this);
    auto *replaceGrid = new QGridLayout(gbReplace);

    cbReplace = new QComboBox(this);
    cbReplace->setEditable(true);
    cbReplace->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    pbReplace = new QPushButton(tr("Replace"), this);
    pbReplaceAll = new QPushButton(tr("Replace All"), this);

    cmbReplaceTable = new QComboBox(this);
    cmbReplaceTable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // Row 0: input + replace
    replaceGrid->addWidget(cbReplace, 0, 0, 1, 3);
    replaceGrid->addWidget(pbReplace, 0, 3);

    // Row 1: table combo + replace all
    replaceGrid->addWidget(cmbReplaceTable, 1, 0, 1, 3);
    replaceGrid->addWidget(pbReplaceAll, 1, 3);

    replaceGrid->setColumnStretch(1, 1);

    mainLayout->addWidget(gbReplace);

    // ── Close button at bottom-right ────────────────
    mainLayout->addStretch();
    auto *bottomRow = new QHBoxLayout();
    bottomRow->addStretch();
    pbCancel = new QPushButton(tr("Close"), this);
    bottomRow->addWidget(pbCancel);
    mainLayout->addLayout(bottomRow);

    // ── Connections ─────────────────────────────────
    connect(pbFind, &QPushButton::clicked, this, &SearchDialog::on_pbFind_clicked);
    connect(pbFindPrev, &QPushButton::clicked, this, &SearchDialog::on_pbFindPrev_clicked);
    connect(pbReplace, &QPushButton::clicked, this, &SearchDialog::on_pbReplace_clicked);
    connect(pbReplaceAll, &QPushButton::clicked, this, &SearchDialog::on_pbReplaceAll_clicked);
    connect(pbCancel, &QPushButton::clicked, this, &QDialog::hide);
    connect(cmbFindTable, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SearchDialog::onFindTableChanged);
    connect(cmbReplaceTable, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SearchDialog::onReplaceTableChanged);
        connect(cbFind->lineEdit(), &QLineEdit::textChanged, this, [this]() { clearMatchStatus(); });
        connect(cbRelative, &QCheckBox::toggled, this, [this]() { clearMatchStatus(); });

    resize(480, 0);
}

SearchDialog::~SearchDialog()
{
}

void SearchDialog::setHexEdit(HexEditor *hexEdit)
{
    _hexEdit = hexEdit;
}

SearchDialog::State SearchDialog::dialogState() const
{
    return {cbFind->currentText(),
            cmbFindTable->currentData().toInt(),
            cbReplace->currentText(),
            cmbReplaceTable->currentData().toInt(),
            cbRelative->isChecked()};
}

void SearchDialog::setDialogState(const State &s)
{
    cbFind->setEditText(s.findText);
    {
        // Select matching table combo item by data value
        for (int i = 0; i < cmbFindTable->count(); ++i) {
            if (cmbFindTable->itemData(i).toInt() == s.findFormat) {
                const QSignalBlocker blk(cmbFindTable);
                cmbFindTable->setCurrentIndex(i);
                break;
            }
        }
        // Apply validator matching the restored mode
        bool isHex = (s.findFormat == -2);
        auto *le = cbFind->lineEdit();
        le->setInputMask("");
        le->setValidator(isHex ? new QRegularExpressionValidator(
            QRegularExpression("[0-9A-Fa-f ]*"), le) : nullptr);
    }
    cbReplace->setEditText(s.replaceText);
    {
        for (int i = 0; i < cmbReplaceTable->count(); ++i) {
            if (cmbReplaceTable->itemData(i).toInt() == s.replaceFormat) {
                const QSignalBlocker blk(cmbReplaceTable);
                cmbReplaceTable->setCurrentIndex(i);
                break;
            }
        }
        bool isHex = (s.replaceFormat == -2);
        auto *le = cbReplace->lineEdit();
        le->setInputMask("");
        le->setValidator(isHex ? new QRegularExpressionValidator(
            QRegularExpression("[0-9A-Fa-f ]*"), le) : nullptr);
    }
    cbRelative->setChecked(s.relative);
}

void SearchDialog::setAvailableTables(const QVector<TableTab> &tables, int activeIndex, bool useTable)
{
    m_availableTables = tables;
    m_activeTableIndex = activeIndex;
    m_useTable = useTable;
}

void SearchDialog::showEvent(QShowEvent *ev)
{
    QDialog::showEvent(ev);

    // Populate both table comboboxes
    auto populateTableCombo = [this](QComboBox *combo) {
        combo->blockSignals(true);
        combo->clear();
        combo->addItem(tr("Raw"), -1);
        combo->addItem(tr("Hex"), -2);
        for (int i = 0; i < m_availableTables.size(); ++i)
            combo->addItem(m_availableTables[i].name, i);

        if (m_useTable && m_activeTableIndex >= 0 && m_activeTableIndex < m_availableTables.size())
            combo->setCurrentIndex(m_activeTableIndex + 2);
        else
            combo->setCurrentIndex(0);
        combo->blockSignals(false);
    };

    populateTableCombo(cmbFindTable);
    populateTableCombo(cmbReplaceTable);

    // Update table pointers from combobox state
    auto tableFromCombo = [this](QComboBox *combo) -> TranslationTable * {
        const int idx = combo->currentData().toInt();
        return (idx >= 0 && idx < m_availableTables.size())
                   ? &m_availableTables[idx].table : nullptr;
    };
    m_findTable = tableFromCombo(cmbFindTable);
    m_replaceTable = tableFromCombo(cmbReplaceTable);
    clearMatchStatus();
}

void SearchDialog::onFindTableChanged(int /*index*/)
{
    const int data = cmbFindTable->currentData().toInt();
    m_findTable = (data >= 0 && data < m_availableTables.size())
                      ? &m_availableTables[data].table : nullptr;

    auto *le = cbFind->lineEdit();
    le->setInputMask("");
    le->setValidator(data == -2 ? new QRegularExpressionValidator(
        QRegularExpression("[0-9A-Fa-f ]*"), le) : nullptr);
    if (data == -2)
        cbFind->setEditText(QString());
    clearMatchStatus();
}

void SearchDialog::onReplaceTableChanged(int /*index*/)
{
    const int data = cmbReplaceTable->currentData().toInt();
    m_replaceTable = (data >= 0 && data < m_availableTables.size())
                         ? &m_availableTables[data].table : nullptr;

    auto *le = cbReplace->lineEdit();
    le->setInputMask("");
    le->setValidator(data == -2 ? new QRegularExpressionValidator(
        QRegularExpression("[0-9A-Fa-f ]*"), le) : nullptr);
    if (data == -2)
        cbReplace->setEditText(QString());
}

void SearchDialog::clearMatchStatus()
{
    lbMatchStatus->setText(tr("No active match"));
}

bool SearchDialog::selectionMatchesPattern(const QByteArray &needle, bool relativeMode) const
{
    if (!_hexEdit || needle.isEmpty())
        return false;

    const qint64 selBegin = _hexEdit->getSelectionBegin();
    const qint64 selEnd = _hexEdit->getSelectionEnd();
    const qint64 selLen = selEnd - selBegin;
    if (selLen != needle.size())
        return false;

    const QByteArray selection = _hexEdit->dataAt(selBegin, selLen);
    if (!relativeMode)
        return selection == needle;

    if (selection.size() != needle.size())
        return false;

    for (int i = 1; i < needle.size(); ++i) {
        if ((selection[0] - selection[i]) != (needle[0] - needle[i]))
            return false;
    }

    return true;
}

void SearchDialog::updateMatchStatus(const SearchResult &result, bool hasPattern, bool found)
{
    if (!hasPattern) {
        clearMatchStatus();
        return;
    }

    if (!found || result.totalMatches <= 0) {
        lbMatchStatus->setText(tr("No matches"));
        return;
    }

    QString text = tr("Match %1 of %2").arg(result.matchNumber).arg(result.totalMatches);
    if (result.wrapped)
        text += tr(" (wrapped)");
    lbMatchStatus->setText(text);
}

SearchDialog::SearchResult SearchDialog::findOccurrence(bool forward, bool allowWrap)
{
    SearchResult result;
    if (!_hexEdit)
        return result;

    int comboIndex = (cmbFindTable->currentData().toInt() == -2) ? 1 : 0;
    _findBa = getContent(comboIndex, cbFind->currentText(), m_findTable);
    const bool hasPattern = !cbFind->currentText().isEmpty();
    if (_findBa.isEmpty()) {
        updateMatchStatus(result, hasPattern, false);
        return result;
    }

    const bool relativeMode = cbRelative->isChecked();
    const bool selectionMatches = selectionMatchesPattern(_findBa, relativeMode);
    const qint64 selectionBegin = _hexEdit->getSelectionBegin();
    const qint64 fileSize = _hexEdit->dataSize();
    const qint64 lastPossibleStart = qMax<qint64>(0, fileSize - _findBa.size());

    qint64 from = _hexEdit->cursorPosition() / 2;
    if (selectionMatches)
        from = forward ? (selectionBegin + 1) : (selectionBegin - 1);

    if (forward) {
        result.index = _hexEdit->findNextIndex(_findBa, from, relativeMode);
        if (result.index < 0 && allowWrap) {
            result.index = _hexEdit->findNextIndex(_findBa, 0, relativeMode);
            result.wrapped = result.index >= 0;
        }
    } else {
        result.index = _hexEdit->findPreviousIndex(_findBa, from, relativeMode);
        if (result.index < 0 && allowWrap) {
            result.index = _hexEdit->findPreviousIndex(_findBa, lastPossibleStart, relativeMode);
            result.wrapped = result.index >= 0;
        }
    }

    if (result.index >= 0) {
        _hexEdit->highlightMatch(result.index, _findBa.size());

        for (qint64 scanFrom = 0; scanFrom <= lastPossibleStart; ) {
            const qint64 matchIndex = _hexEdit->findNextIndex(_findBa, scanFrom, relativeMode);
            if (matchIndex < 0)
                break;

            ++result.totalMatches;
            if (matchIndex == result.index && result.matchNumber == 0)
                result.matchNumber = result.totalMatches;

            scanFrom = matchIndex + 1;
        }
    }

    updateMatchStatus(result, hasPattern, result.index >= 0);
    return result;
}

qint64 SearchDialog::findNext()
{
    return findOccurrence(true).index;
}

qint64 SearchDialog::findPrevious()
{
    return findOccurrence(false).index;
}

void SearchDialog::on_pbFind_clicked()
{
    findNext();
}

void SearchDialog::on_pbFindPrev_clicked()
{
    findPrevious();
}

void SearchDialog::on_pbReplace_clicked()
{
    int idx = findNext();
    if (idx >= 0)
    {
        int replaceComboIndex = (cmbReplaceTable->currentData().toInt() == -2) ? 1 : 0;
        QByteArray replaceBa = getContent(replaceComboIndex, cbReplace->currentText(), m_replaceTable);
        replaceOccurrence(idx, replaceBa);
    }
}

void SearchDialog::on_pbReplaceAll_clicked()
{
    if (!_hexEdit)
        return;

    int findComboIndex = (cmbFindTable->currentData().toInt() == -2) ? 1 : 0;
    _findBa = getContent(findComboIndex, cbFind->currentText(), m_findTable);
    if (_findBa.isEmpty()) {
        updateMatchStatus(SearchResult{}, !cbFind->currentText().isEmpty(), false);
        return;
    }

    int replaceComboIndex = (cmbReplaceTable->currentData().toInt() == -2) ? 1 : 0;
    QByteArray replaceBa = getContent(replaceComboIndex, cbReplace->currentText(), m_replaceTable);
    const bool relativeMode = cbRelative->isChecked();

    int replaceCounter = 0;
    qint64 searchFrom = 0;

    while (searchFrom <= qMax<qint64>(0, _hexEdit->dataSize() - _findBa.size())) {
        const qint64 idx = _hexEdit->findNextIndex(_findBa, searchFrom, relativeMode);
        if (idx < 0)
            break;

        _hexEdit->highlightMatch(idx, _findBa.size());
        replaceOccurrence(idx, replaceBa);
        ++replaceCounter;

        searchFrom = idx + qMax<qint64>(replaceBa.size(), 1);
    }

    if (replaceCounter > 0)
        QMessageBox::information(this, tr("Replace"), QString(tr("%1 occurrences replaced")).arg(replaceCounter));
}


QByteArray SearchDialog::getContent(int comboIndex, const QString &input, TranslationTable *table)
{
    QByteArray findBa;
    switch (comboIndex)
    {
        case 0:     // text
            if (!_hexEdit)
            {
                findBa = input.toUtf8();
            }
            else if (table && table->size() > 0)
            {
                findBa = table->decode(input.toUtf8());
            }
            else
            {
                const QString encoding = _hexEdit->currentEncoding();
                if (encoding.isEmpty() || encoding == QLatin1String("ASCII"))
                {
                    const QByteArray encoded = input.toLatin1();
                    findBa = (QString::fromLatin1(encoded) == input) ? encoded : QByteArray();
                }
                else
                {
                    QStringEncoder enc(encoding.toUtf8().constData());
                    if (enc.isValid()) {
                        const QByteArray encoded = enc.encode(input);
                        QStringDecoder dec(encoding.toUtf8().constData());
                        findBa = (dec.isValid() && dec.decode(encoded) == input) ? encoded : QByteArray();
                    } else {
                        findBa.clear();
                    }
                }
            }
            break;

        case 1:     // hex
            findBa = QByteArray::fromHex(input.simplified().remove(' ').toLatin1());
            break;
    }

    return findBa;
}

qint64 SearchDialog::replaceOccurrence(qint64 idx, const QByteArray &replaceBa)
{
    if (replaceBa.length() >= 0)
    {
        _hexEdit->replace(idx, _findBa.length(), replaceBa);
    }
    return QMessageBox::Yes;
}

void SearchDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        setWindowTitle(tr("Find/Replace"));
    }
    QDialog::changeEvent(event);
}

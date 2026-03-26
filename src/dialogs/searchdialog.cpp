#include "searchdialog.h"

#include <QEvent>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
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

    cbFindFormat = new QComboBox(this);
    cbFindFormat->addItem(tr("Text"));
    cbFindFormat->addItem(tr("Hex"));

    cbFind = new QComboBox(this);
    cbFind->setEditable(true);
    cbFind->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    pbFind = new QPushButton(tr("Find next"), this);
    pbFind->setDefault(true);
    pbFind->setShortcut(QKeySequence(Qt::Key_F3));

    pbFindPrev = new QPushButton(tr("Find prev"), this);
    pbFindPrev->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3));

    auto *lblFindTable = new QLabel(tr("Table") + ":", this);
    cmbFindTable = new QComboBox(this);
    cmbFindTable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    cbRelative = new QCheckBox(tr("Relative search"), this);

    // Row 0: format + input + find next
    findGrid->addWidget(cbFindFormat, 0, 0);
    findGrid->addWidget(cbFind, 0, 1, 1, 2);
    findGrid->addWidget(pbFind, 0, 3);

    // Row 1: table label + combo + find prev
    findGrid->addWidget(lblFindTable, 1, 0);
    findGrid->addWidget(cmbFindTable, 1, 1);
    findGrid->addWidget(cbRelative, 1, 2);
    findGrid->addWidget(pbFindPrev, 1, 3);

    findGrid->setColumnStretch(1, 1);
    findGrid->setColumnStretch(2, 0);

    mainLayout->addWidget(gbFind);

    // ── Replace group ───────────────────────────────
    auto *gbReplace = new QGroupBox(tr("Replace"), this);
    auto *replaceGrid = new QGridLayout(gbReplace);

    cbReplaceFormat = new QComboBox(this);
    cbReplaceFormat->addItem(tr("Text"));
    cbReplaceFormat->addItem(tr("Hex"));

    cbReplace = new QComboBox(this);
    cbReplace->setEditable(true);
    cbReplace->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    pbReplace = new QPushButton(tr("Replace"), this);
    pbReplaceAll = new QPushButton(tr("Replace All"), this);

    auto *lblReplaceTable = new QLabel(tr("Table") + ":", this);
    cmbReplaceTable = new QComboBox(this);
    cmbReplaceTable->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // Row 0: format + input + replace
    replaceGrid->addWidget(cbReplaceFormat, 0, 0);
    replaceGrid->addWidget(cbReplace, 0, 1, 1, 2);
    replaceGrid->addWidget(pbReplace, 0, 3);

    // Row 1: table label + combo + replace all
    replaceGrid->addWidget(lblReplaceTable, 1, 0);
    replaceGrid->addWidget(cmbReplaceTable, 1, 1, 1, 2);
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
    connect(cbFindFormat, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SearchDialog::on_cbFindFormat_currentIndexChanged);
    connect(cbReplaceFormat, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SearchDialog::on_cbReplaceFormat_currentIndexChanged);
    connect(cmbFindTable, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SearchDialog::onFindTableChanged);
    connect(cmbReplaceTable, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SearchDialog::onReplaceTableChanged);

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
            cbFindFormat->currentIndex(),
            cbReplace->currentText(),
            cbReplaceFormat->currentIndex(),
            cbRelative->isChecked()};
}

void SearchDialog::setDialogState(const State &s)
{
    // Always restore text unconditionally so switching to a "fresh" tab
    // correctly clears any text left by a different tab.
    cbFind->setEditText(s.findText);
    {
        const QSignalBlocker blk(cbFindFormat);
        cbFindFormat->setCurrentIndex(s.findFormat);
        // Apply validator matching the restored format
        auto *le = cbFind->lineEdit();
        le->setInputMask("");
        le->setValidator(s.findFormat ? new QRegularExpressionValidator(
            QRegularExpression("[0-9A-Fa-f ]*"), le) : nullptr);
    }
    cbReplace->setEditText(s.replaceText);
    {
        const QSignalBlocker blk(cbReplaceFormat);
        cbReplaceFormat->setCurrentIndex(s.replaceFormat);
        auto *le = cbReplace->lineEdit();
        le->setInputMask("");
        le->setValidator(s.replaceFormat ? new QRegularExpressionValidator(
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
        for (int i = 0; i < m_availableTables.size(); ++i)
            combo->addItem(m_availableTables[i].name, i);

        if (m_useTable && m_activeTableIndex >= 0 && m_activeTableIndex < m_availableTables.size())
            combo->setCurrentIndex(m_activeTableIndex + 1);
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
}

void SearchDialog::onFindTableChanged(int /*index*/)
{
    const int idx = cmbFindTable->currentData().toInt();
    m_findTable = (idx >= 0 && idx < m_availableTables.size())
                      ? &m_availableTables[idx].table : nullptr;
}

void SearchDialog::onReplaceTableChanged(int /*index*/)
{
    const int idx = cmbReplaceTable->currentData().toInt();
    m_replaceTable = (idx >= 0 && idx < m_availableTables.size())
                         ? &m_availableTables[idx].table : nullptr;
}

qint64 SearchDialog::findNext()
{
    if (!_hexEdit)
        return -1;

    qint64 from = _hexEdit->cursorPosition() / 2;
    _findBa = getContent(cbFindFormat->currentIndex(), cbFind->currentText(), m_findTable);
    qint64 idx = -1;

    if (_findBa.length() > 0)
    {
        if (cbRelative->isChecked())
            idx = _hexEdit->relativeSearch(_findBa, from);
        else
            idx = _hexEdit->indexOf(_findBa, from);
    }

    return idx;
}

qint64 SearchDialog::findPrevious()
{
    if (!_hexEdit)
        return -1;

    qint64 from = _hexEdit->cursorPosition() / 2;
    if (from > 0)
        --from;

    _findBa = getContent(cbFindFormat->currentIndex(), cbFind->currentText(), m_findTable);
    if (_findBa.isEmpty())
        return -1;

    return _hexEdit->lastIndexOf(_findBa, from);
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
        QByteArray replaceBa = getContent(cbReplaceFormat->currentIndex(), cbReplace->currentText(), m_replaceTable);
        replaceOccurrence(idx, replaceBa);
    }
}

void SearchDialog::on_pbReplaceAll_clicked()
{
    int replaceCounter = 0;
    int idx = 0;

    while (idx >= 0)
    {
        idx = findNext();
        if (idx >= 0)
        {
            QByteArray replaceBa = getContent(cbReplaceFormat->currentIndex(), cbReplace->currentText(), m_replaceTable);
            replaceOccurrence(idx, replaceBa);
            replaceCounter += 1;
        }
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

void SearchDialog::on_cbFindFormat_currentIndexChanged(int index)
{
    auto *le = cbFind->lineEdit();
    le->setInputMask("");
    le->setValidator(index ? new QRegularExpressionValidator(
        QRegularExpression("[0-9A-Fa-f ]*"), le) : nullptr);
    if (index == 1)  // switching to Hex — clear non-hex text
        cbFind->setEditText(QString());
}


void SearchDialog::on_cbReplaceFormat_currentIndexChanged(int index)
{
    auto *le = cbReplace->lineEdit();
    le->setInputMask("");
    le->setValidator(index ? new QRegularExpressionValidator(
        QRegularExpression("[0-9A-Fa-f ]*"), le) : nullptr);
    if (index == 1)  // switching to Hex — clear non-hex text
        cbReplace->setEditText(QString());
}

void SearchDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        setWindowTitle(tr("Find/Replace"));
    }
    QDialog::changeEvent(event);
}

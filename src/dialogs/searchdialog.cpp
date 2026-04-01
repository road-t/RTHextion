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

    // Row 0: input + find next
    findGrid->addWidget(cbFind, 0, 0, 1, 3);
    findGrid->addWidget(pbFind, 0, 3);

    // Row 1:  + find prev
    findGrid->addWidget(cmbFindTable, 1, 0, 1, 3);
    findGrid->addWidget(pbFindPrev, 1, 3);

    // Row 2: relative checkbox
    findGrid->addWidget(cbRelative, 2, 0, 1, 3);

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

qint64 SearchDialog::findNext()
{
    if (!_hexEdit)
        return -1;

    qint64 from = _hexEdit->cursorPosition() / 2;
    int comboIndex = (cmbFindTable->currentData().toInt() == -2) ? 1 : 0;
    _findBa = getContent(comboIndex, cbFind->currentText(), m_findTable);
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

    int comboIndex = (cmbFindTable->currentData().toInt() == -2) ? 1 : 0;
    _findBa = getContent(comboIndex, cbFind->currentText(), m_findTable);
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
        int replaceComboIndex = (cmbReplaceTable->currentData().toInt() == -2) ? 1 : 0;
        QByteArray replaceBa = getContent(replaceComboIndex, cbReplace->currentText(), m_replaceTable);
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
            int replaceComboIndex = (cmbReplaceTable->currentData().toInt() == -2) ? 1 : 0;
            QByteArray replaceBa = getContent(replaceComboIndex, cbReplace->currentText(), m_replaceTable);
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

void SearchDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        setWindowTitle(tr("Find/Replace"));
    }
    QDialog::changeEvent(event);
}

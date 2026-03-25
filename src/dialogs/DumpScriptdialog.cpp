#include <QMessageBox>
#include <QFileDialog>
#include <QSettings>
#include <QDir>
#include <QFileInfo>
#include <QEvent>
#include <QCloseEvent>
#include <QPushButton>
#include <QAbstractButton>
#include <QRegularExpression>
#include <algorithm>
#include <QtEndian>

#include <QRegularExpressionValidator>

#include "DumpScriptdialog.h"
#include "ui_DumpScriptdialog.h"

namespace
{
    const char *kLastDumpDirKey = "Paths/LastDumpDir";

    bool hasIncomingPointersInSelection(HexEditor *hexEdit)
    {
        if (!hexEdit)
            return false;

        const QByteArray selection = hexEdit->getRawSelection();
        if (selection.size() <= 1)
            return false;

        const qint64 begin = hexEdit->getSelectionBegin();
        PointerListModel *model = hexEdit->pointers();
        if (!model)
            return false;

        for (int i = 0; i < selection.size(); ++i)
        {
            if (model->hasOffset(begin + i))
                return true;
        }

        return false;
    }
}

DumpScriptDialog::DumpScriptDialog(HexEditor *hexEdit, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DumpScriptDialog),
    hexEdit(hexEdit)
{
    ui->setupUi(this);

    // Hex-only validator for pointer offset (optional leading '-', hex digits)
    static const QRegularExpression hexOffsetRe(QStringLiteral("^-?[0-9A-Fa-f]*$"));
    ui->lePointerOffset->setValidator(new QRegularExpressionValidator(hexOffsetRe, ui->lePointerOffset));
}

void DumpScriptDialog::setAvailableTables(const QVector<TableTab> &tables, int activeIndex, bool useTable)
{
    m_availableTables = tables;
    m_activeTableIndex = activeIndex;
    m_useTable = useTable;
}

void DumpScriptDialog::showEvent(QShowEvent *ev)
{
    Q_UNUSED(ev);

    // Populate the table combobox
    ui->cmbTable->blockSignals(true);
    ui->cmbTable->clear();
    ui->cmbTable->addItem(tr("Raw"), -1);
    for (int i = 0; i < m_availableTables.size(); ++i)
        ui->cmbTable->addItem(m_availableTables[i].name, i);

    // Select the table that was active, or Raw if use_table is off or no tables
    if (m_useTable && m_activeTableIndex >= 0 && m_activeTableIndex < m_availableTables.size())
        ui->cmbTable->setCurrentIndex(m_activeTableIndex + 1); // +1 because index 0 is "Raw"
    else
        ui->cmbTable->setCurrentIndex(0);
    ui->cmbTable->blockSignals(false);

    // Update tb from combobox
    const int tableIdx = ui->cmbTable->currentData().toInt();
    tb = (tableIdx >= 0 && tableIdx < m_availableTables.size())
             ? &m_availableTables[tableIdx].table : nullptr;

    const bool canUsePointers = hasIncomingPointersInSelection(hexEdit);
    ui->cbUsePointers->setEnabled(canUsePointers);
    ui->cbUsePointers->setChecked(canUsePointers);
    ui->cbSplitByPointers->setEnabled(canUsePointers && ui->cbUsePointers->isChecked());
    if (!canUsePointers)
        ui->cbSplitByPointers->setChecked(false);

    // Init pointer offset edit (plain hex, no 0x prefix)
    const QString prefix = (_pointerOffset < 0) ? QStringLiteral("-") : QString();
    ui->lePointerOffset->setText(prefix + QString::number(qAbs(_pointerOffset), 16).toUpper());

    updateText();
    populateStopCharCmb();
}

DumpScriptDialog::~DumpScriptDialog()
{
    delete ui;
}

void DumpScriptDialog::setRomProfile(int pointerSize, qint64 pointerOffset)
{
    _pointerSize = pointerSize;
    _pointerOffset = pointerOffset;
}

void DumpScriptDialog::on_cbSplitByCharacter_stateChanged(int arg1)
{
    ui->cmbSplitCharacter->setDisabled(!arg1);
    updateText();
}

void DumpScriptDialog::updateText()
{
    auto useTable = (tb != nullptr);
    const bool usePointers = ui->cbUsePointers->isChecked();
    auto data = hexEdit->getRawSelection();

    QByteArray stopBytes;
    bool stopCharActive = false;

    if (ui->cbSplitByCharacter->isChecked())
    {
        stopBytes = ui->cmbSplitCharacter->currentData().toByteArray();
        stopCharActive = !stopBytes.isEmpty();
    }

    QString dump;

    auto selectionOffset = hexEdit->getSelectionBegin();

    // Pre-decode per-byte character mapping for non-table mode
    QVector<QString> chars;
    if (!useTable)
        chars = hexEdit->decodeBufferForCurrentEncoding(data);

    int i = 0;
    while (i < data.size())
    {
        const auto offset = i + selectionOffset;

        if (usePointers && hexEdit->pointers()->hasOffset(offset))
        {
            // split by pointers if requested
            if (ui->cbSplitByPointers->isChecked() && i)
                dump += '\n';

            auto ptrs = hexEdit->pointers()->getPointers(offset);
            QString ptrsString;

            {
                QStringList ptrsList;
                for (const qint64 ptrKey : ptrs)
                {
                    const int sz = hexEdit->pointers()->getPointerSize(ptrKey);
                    ptrsList.push_back(QString::number(ptrKey, 16) + ":" + QString::number(sz));
                }
                ptrsString = ptrsList.join(',');
            }

            // add pointer to dump
            dump += QString("{|%1|}:\n").arg(ptrsString);
        }

        if (useTable)
        {
            int consumed = 0;
            dump += tb->encodeBytes(data, i, consumed, true);
            if (stopCharActive && data.mid(i, stopBytes.size()) == stopBytes)
                dump += '\n';
            i += consumed;
        }
        else
        {
            // Use per-byte decoded buffer from hex editor (encoding-aware)
            if (chars[i].isNull()) {
                // Continuation byte of a multi-byte character — skip
                ++i;
                continue;
            }
            if (chars[i].isEmpty()) {
                // Non-printable byte
                dump += TranslationTable::charToHex(data[i]);
            } else {
                dump += chars[i];
            }
            if (stopCharActive && data.mid(i, stopBytes.size()) == stopBytes)
                dump += '\n';
            ++i;
        }
    }

    ui->pteScript->setPlainText(dump);
    ui->pteScript->document()->setModified(false);
}

void DumpScriptDialog::on_cmbTable_currentIndexChanged(int index)
{
    Q_UNUSED(index);

    const int tableIdx = ui->cmbTable->currentData().toInt();
    tb = (tableIdx >= 0 && tableIdx < m_availableTables.size())
             ? &m_availableTables[tableIdx].table : nullptr;

    updateText();
    populateStopCharCmb();
}

void DumpScriptDialog::on_cbUsePointers_stateChanged(int arg1)
{
    ui->cbSplitByPointers->setEnabled(arg1 != 0);
    if (!arg1)
        ui->cbSplitByPointers->setChecked(false);

    updateText();
}

void DumpScriptDialog::populateStopCharCmb()
{
    ui->cmbSplitCharacter->clear();

    // populate range comboboxes with translation table values...
    if (tb)
    {
        const auto *items = tb->getItems();
        for (auto it = items->cbegin(); it != items->cend(); ++it)
            ui->cmbSplitCharacter->addItem(it.value(), QByteArray(1, it.key()));
        const auto &mbItems = tb->getMultiByteItems();
        for (auto it = mbItems.cbegin(); it != mbItems.cend(); ++it)
            ui->cmbSplitCharacter->addItem(it.value(), it.key());
    }
    else // ...or with standard ASCII printable characters
    {
        for (uint16_t c = 0; c < 0x100; c++)
        {
            if (c > 0x20)
                ui->cmbSplitCharacter->addItem(QString("{%1} - %2").arg(QString::number(c, 16).toUpper(), 2, '0').arg(QChar(c)), c);
            else
                ui->cmbSplitCharacter->addItem(QString("{%1}").arg(QString::number(c, 16).toUpper(), 2, '0'), c);
        }
    }

    ui->cmbSplitCharacter->setCurrentIndex(0);
}

void DumpScriptDialog::on_cmbSplitCharacter_currentIndexChanged(int index)
{
    Q_UNUSED(index);

    updateText();
}


void DumpScriptDialog::on_cbSplitByPointers_stateChanged(int arg1)
{
    Q_UNUSED(arg1);

    updateText();
}


void DumpScriptDialog::on_pbCancel_clicked()
{
    close();
}

void DumpScriptDialog::on_pbExport_clicked()
{
    QSettings settings;
    const QString defaultDir = settings.value(kLastDumpDirKey, QDir::homePath()).toString();
    QString fileName = QFileDialog::getSaveFileName(this,
                                                    tr("Export script"),
                                                    defaultDir,
                                                    tr("Text files (*.txt);;All files (*)"));

    if (!fileName.isEmpty())
    {
        if (QFileInfo(fileName).suffix().isEmpty())
            fileName += ".txt";

        QFile file(fileName);

        if (!file.open(QFile::WriteOnly | QFile::Text)) {
            QMessageBox::warning(this, tr("Export script"),
                                 tr("Cannot write file %1:\n%2.")
                                 .arg(fileName)
                                 .arg(file.errorString()));
            return;
        }

        QApplication::setOverrideCursor(Qt::WaitCursor);
        file.write(ui->pteScript->toPlainText().toUtf8());
        QApplication::restoreOverrideCursor();

        const QString dirPath = QFileInfo(fileName).absolutePath();
        if (!dirPath.isEmpty())
            settings.setValue(kLastDumpDirKey, dirPath);

        ui->pteScript->document()->setModified(false);
    }
}

QByteArray DumpScriptDialog::decodeScriptText(const QString &text, bool useTable) const
{
    // Process escape sequences and {XX} hex byte markers.
    // Escape: backslash-{ and backslash-} produce literal braces,
    // double-backslash produces a single backslash.
    // {XX} (hex pairs) become raw bytes.
    // Everything else is plain text, encoded via table or current encoding.

    QByteArray result;
    QString plainBuf;

    auto flushPlain = [&]() {
        if (plainBuf.isEmpty()) return;
        if (useTable && tb)
            result.append(tb->decode(plainBuf.toUtf8()));
        else
            result.append(hexEdit->encodeTextForCurrentEncoding(plainBuf));
        plainBuf.clear();
    };

    int i = 0;
    const int len = text.size();
    while (i < len) {
        QChar ch = text[i];

        // Escape sequences: backslash + { or } or backslash
        if (ch == QLatin1Char('\\') && i + 1 < len) {
            QChar next = text[i + 1];
            if (next == QLatin1Char('{') || next == QLatin1Char('}') || next == QLatin1Char('\\')) {
                plainBuf += next;
                i += 2;
                continue;
            }
        }

        // {XX} hex byte sequences
        if (ch == QLatin1Char('{')) {
            int closePos = text.indexOf(QLatin1Char('}'), i + 1);
            if (closePos > i + 1) {
                const QString hexStr = text.mid(i + 1, closePos - i - 1);
                static const QRegularExpression hexRe(QStringLiteral("^[0-9A-Fa-f]+$"));
                if (hexStr.size() % 2 == 0 && hexRe.match(hexStr).hasMatch()) {
                    flushPlain();
                    result.append(QByteArray::fromHex(hexStr.toLatin1()));
                    i = closePos + 1;
                    continue;
                }
            }
        }

        plainBuf += ch;
        ++i;
    }

    flushPlain();
    return result;
}

void DumpScriptDialog::on_pbInsert_clicked()
{
    // Parse pointer offset from the edit field (plain hex, no 0x prefix)
    {
        QString text = ui->lePointerOffset->text().trimmed();
        bool negative = false;
        if (text.startsWith(QLatin1Char('-'))) {
            negative = true;
            text = text.mid(1);
        }
        bool ok = false;
        qint64 val = text.toLongLong(&ok, 16);
        if (ok)
            _pointerOffset = negative ? -val : val;
    }

    const bool useTable = (tb != nullptr);
    const QString script = ui->pteScript->toPlainText();

    const qint64 selBegin = hexEdit->getSelectionBegin();
    const qint64 selEnd   = hexEdit->getSelectionEnd();
    const qint64 selSize  = selEnd - selBegin;

    static QRegularExpression re(
        "\\{\\|([a-f0-9:,]+)\\|\\}:\\s*(.*)(?=(?:\\{\\|)|$)\\s*",
        QRegularExpression::CaseInsensitiveOption |
        QRegularExpression::DotMatchesEverythingOption |
        QRegularExpression::InvertedGreedinessOption
    );
    static QRegularExpression reNoNewlines("[\r\n]+");

    struct Chunk {
        QByteArray data;
        QMap<qint64, int> pointers; // ptrFileOffset -> effectivePtrSize
    };

    QVector<Chunk> chunks;

    QRegularExpressionMatchIterator it = re.globalMatch(script, 0, QRegularExpression::PartialPreferCompleteMatch);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        if (!match.hasMatch())
            continue;

        Chunk chunk;
        const QString textContent = match.captured(2).remove(reNoNewlines);
        chunk.data = decodeScriptText(textContent, useTable);

        const QStringList ptrParts = match.captured(1).split(QLatin1Char(','));
        for (const QString &ptrStr : ptrParts) {
            const QStringList parts = ptrStr.trimmed().split(QLatin1Char(':'));
            const qint64 ptrFileOffset = static_cast<qint64>(parts[0].trimmed().toULongLong(nullptr, 16));
            const int perPtrSize       = (parts.size() > 1) ? parts[1].trimmed().toInt() : _pointerSize;
            const int effectivePtrSize = (perPtrSize == 2 || perPtrSize == 3 || perPtrSize == 4)
                                             ? perPtrSize : _pointerSize;
            chunk.pointers.insert(ptrFileOffset, effectivePtrSize);
        }

        chunks.append(chunk);
    }

    // Fallback: no pointer markers — treat whole script as one chunk starting at selBegin
    if (chunks.isEmpty()) {
        Chunk chunk;
        chunk.data = decodeScriptText(script, useTable);
        chunks.append(chunk);
    }

    qint64 totalBytes = 0;
    for (const auto &ch : chunks)
        totalBytes += ch.data.size();

    bool shouldTrim = false;
    if (selSize > 0 && totalBytes > selSize) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Insert script"));
        box.setText(tr("The decoded script (%1 bytes) is larger than the selection (%2 bytes).")
                        .arg(totalBytes).arg(selSize));
        QPushButton *overwriteBtn   = box.addButton(tr("Overwrite"),          QMessageBox::AcceptRole);
        QPushButton *trimBtn        = box.addButton(tr("Trim to selection"),  QMessageBox::AcceptRole);
        QPushButton *cancelBtn      = box.addButton(tr("Return to editing"),  QMessageBox::RejectRole);
        Q_UNUSED(overwriteBtn);
        box.setDefaultButton(cancelBtn);
        box.exec();

        if (box.clickedButton() == cancelBtn)
            return;

        shouldTrim = (box.clickedButton() == trimBtn);
    }

    qint64 offset = selBegin;
    const qint64 limitOffset = selBegin + selSize;

    for (const auto &chunk : chunks) {
        QByteArray data = chunk.data;

        if (shouldTrim && selSize > 0 && offset + data.size() > limitOffset) {
            const qint64 remaining = limitOffset - offset;
            if (remaining <= 0)
                break;
            data = data.left(static_cast<int>(remaining));
        }

        if (data.isEmpty())
            continue;

        hexEdit->replace(offset, data.size(), data);

        // Update each pointer that refers to this chunk
        for (auto ptrIt = chunk.pointers.cbegin(); ptrIt != chunk.pointers.cend(); ++ptrIt) {
            const qint64 ptrFileOffset  = ptrIt.key();
            const int effectivePtrSize  = ptrIt.value();
            const qint64 rawPointerValue = offset - _pointerOffset;

            QByteArray ptrData(effectivePtrSize, 0);
            uchar *raw = reinterpret_cast<uchar *>(ptrData.data());

            if (effectivePtrSize == 2) {
                const quint16 val16 = static_cast<quint16>(rawPointerValue);
                if (hexEdit->byteOrder == ByteOrder::BigEndian)
                    qToBigEndian<quint16>(val16, raw);
                else {
                    qToLittleEndian<quint16>(val16, raw);
                    if (hexEdit->byteOrder == ByteOrder::SwappedBytes)
                        std::swap(ptrData[0], ptrData[1]);
                }
            } else {
                const quint32 val32 = static_cast<quint32>(rawPointerValue);
                if (hexEdit->byteOrder == ByteOrder::BigEndian)
                    qToBigEndian<quint32>(val32, raw);
                else {
                    qToLittleEndian<quint32>(val32, raw);
                    if (hexEdit->byteOrder == ByteOrder::SwappedBytes) {
                        std::swap(ptrData[0], ptrData[1]);
                        std::swap(ptrData[2], ptrData[3]);
                    }
                }
            }

            hexEdit->replace(ptrFileOffset, effectivePtrSize, ptrData);
        }

        offset += chunk.data.size(); // advance by full (untruncated) chunk size
    }

    ui->pteScript->document()->setModified(false);
    close();
}

void DumpScriptDialog::closeEvent(QCloseEvent *event)
{
    if (ui->pteScript->document()->isModified()) {
        QMessageBox box(QMessageBox::Question,
                        tr("Edit script"),
                        tr("The script has been modified but not exported. Close anyway?"),
                        QMessageBox::Yes | QMessageBox::No,
                        this);
        if (box.exec() != QMessageBox::Yes) {
            event->ignore();
            return;
        }
    }
    QDialog::closeEvent(event);
}

void DumpScriptDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        ui->retranslateUi(this);
    QDialog::changeEvent(event);
}


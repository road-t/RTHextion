#include "InsertScriptDialog.h"
#include "ui_InsertScriptDialog.h"
#include "appsettings.h"
#include "Datas.h"
#include <QtEndian>
#include <QEvent>
#include <QFile>
#include <QMessageBox>
#include <QProgressDialog>
#include <QRegularExpression>
#include <algorithm>

namespace
{
    int normalizedScriptPointerSize(int explicitSize, int defaultSize)
    {
        if (explicitSize == 2 || explicitSize == 4)
            return explicitSize;
        return (defaultSize == 2) ? 2 : 4;
    }

    bool parsePointerSpec(const QString &text, qint64 *outOffset, int *outSize)
    {
        if (!outOffset || !outSize)
            return false;

        static const QRegularExpression re(
            QStringLiteral("^\\s*([0-9A-Fa-f]+)(?:\\s*:\\s*([24]))?\\s*$"));
        const QRegularExpressionMatch m = re.match(text);
        if (!m.hasMatch())
            return false;

        bool ok = false;
        const qint64 ofs = static_cast<qint64>(m.captured(1).toULongLong(&ok, 16));
        if (!ok)
            return false;

        const int sz = m.captured(2).isEmpty() ? 0 : m.captured(2).toInt();
        *outOffset = ofs;
        *outSize = sz;
        return true;
    }

    QByteArray decodeScriptText(const QString &text, TranslationTable *tb)
    {
        QByteArray result;
        QString plainBuf;

        auto flushPlain = [&]() {
            if (plainBuf.isEmpty())
                return;
            if (tb)
                result.append(tb->decode(plainBuf.toUtf8()));
            plainBuf.clear();
        };

        int i = 0;
        const int len = text.size();
        while (i < len) {
            const QChar ch = text[i];

            if (ch == QLatin1Char('\\') && i + 1 < len) {
                const QChar next = text[i + 1];
                if (next == QLatin1Char('{') || next == QLatin1Char('}') || next == QLatin1Char('\\')) {
                    plainBuf += next;
                    i += 2;
                    continue;
                }
            }

            if (ch == QLatin1Char('{')) {
                const int closePos = text.indexOf(QLatin1Char('}'), i + 1);
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
}

InsertScriptDialog::InsertScriptDialog(HexEditor *hexEdit, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::InsertScriptDialog),
    hexEdit(hexEdit)
{
    ui->setupUi(this);
}

void InsertScriptDialog::showEvent(QShowEvent *ev)
{
    Q_UNUSED(ev);

    // Repopulate the table combobox
    ui->cmbTable->blockSignals(true);
    ui->cmbTable->clear();
    for (int i = 0; i < m_tables.size(); ++i)
        ui->cmbTable->addItem(m_tables[i].name, i);
    ui->cmbTable->blockSignals(false);

    const bool hasTables = !m_tables.isEmpty();
    ui->cmbTable->setEnabled(hasTables);

    // Set tb from the currently selected table
    const int idx = hasTables ? ui->cmbTable->currentData().toInt() : -1;
    tb = (idx >= 0 && idx < m_tables.size()) ? &m_tables[idx].table : nullptr;

    updateText();
}

InsertScriptDialog::~InsertScriptDialog()
{
    delete ui;
}

void InsertScriptDialog::on_bbControls_accepted()
{

}

void InsertScriptDialog::updateText()
{

}

void InsertScriptDialog::setRomProfile(int pointerSize, qint64 pointerOffset)
{
    _pointerSize = pointerSize;
    _pointerOffset = pointerOffset;
}

void InsertScriptDialog::setAvailableTables(const QVector<TableTab> &tables)
{
    m_tables = tables;
}

void InsertScriptDialog::on_bbControls_clicked(QAbstractButton *button)
{
    if (ui->bbControls->standardButton(button) == QDialogButtonBox::Open)
    {
        auto &settings = AppSettings::instance();
        const QString defaultDir = settings.value("Paths/LastDumpDir", QDir::homePath()).toString();
        const QString fileName = QFileDialog::getOpenFileName(this, tr("Open script"), defaultDir, tr("Text files (*.txt *.script *.scr);;All files (*)"));
        if (fileName.isEmpty())
            return;

        QFile file(fileName);
        if (!file.open(QFile::ReadOnly | QFile::Text))
        {
            QMessageBox::warning(this, tr("Open script"),
                                 tr("Cannot read file %1:\n%2.")
                                 .arg(fileName)
                                 .arg(file.errorString()));
            return;
        }

        ui->pteScript->setPlainText(QString::fromUtf8(file.readAll()));
        const QString dirPath = QFileInfo(fileName).absolutePath();
        if (!dirPath.isEmpty())
            settings.setValue("Paths/LastDumpDir", dirPath);
        return;
    }

    if (ui->bbControls->buttonRole(button) == QDialogButtonBox::ButtonRole::AcceptRole)
    {
        // Update tb from the currently selected combobox item
        const int tableIdx = ui->cmbTable->currentData().toInt();
        tb = (tableIdx >= 0 && tableIdx < m_tables.size()) ? &m_tables[tableIdx].table : nullptr;
        if (!tb)
            return;

        /* regex to match dumps like:

         {|abc0123|}:<any space character>
         dump chunk
         <any space character>
         and so on

        */
        static QRegularExpression re(
            "\\{\\|([^|]+)\\|\\}:(?:\\r?\\n)*(.*?)(?=(?:\\{\\|)|\\z)",
                QRegularExpression::CaseInsensitiveOption |
                QRegularExpression::DotMatchesEverythingOption
                );

        static QRegularExpression reNoNewlines("[\r\n]+");

        qDebug() << "Capture groups: " << re.captureCount();

        auto offset = hexEdit->getCurrentOffset();

    //    if (ui->cbUseTable->isChecked())
    //    {
            auto script = ui->pteScript->toPlainText();

            QVector<QRegularExpressionMatch> matches;
            QRegularExpressionMatchIterator it = re.globalMatch(script);
            while (it.hasNext()) {
                const QRegularExpressionMatch match = it.next();
                if (match.hasMatch())
                    matches.append(match);
            }

            const bool updatePointers = ui->cbUpdatePointers->isChecked();
            QVector<QPair<qint64, qint64>> pointerBatch;
            const bool hadChunks = !matches.isEmpty();

            QProgressDialog progress(tr("Importing script..."), QString(), 0, hadChunks ? matches.size() : 1, this);
            progress.setWindowModality(Qt::ApplicationModal);
            progress.setMinimumDuration(0);
            progress.setCancelButton(nullptr);
            progress.setAutoClose(true);
            progress.setAutoReset(true);
            progress.show();

            QUndoStack *stack = hexEdit->undoStack();
            if (stack)
                stack->beginMacro(tr("Import script"));

            hexEdit->setUpdatesEnabled(false);

            int progressStep = 0;
            for (const QRegularExpressionMatch &match : matches)
            {
                if (match.hasMatch())
                {
                     qDebug() << ": [1] " << match.captured(1) << "[2] " << match.captured(2);

                     // insert pointed chunk
                     auto line = decodeScriptText(match.captured(2).remove(reNoNewlines), tb);
                     auto length = line.length();

                     hexEdit->replace(offset, length, line);

                     // update pointers to it
                     auto pointers = match.captured(1).split(',', Qt::SkipEmptyParts);

                     for (const auto& i : pointers)
                     {
                         qint64 ptrOffset = -1;
                         int parsedSize = 0;
                         if (!parsePointerSpec(i, &ptrOffset, &parsedSize))
                             continue;

                         const int effectivePtrSize = normalizedScriptPointerSize(parsedSize, _pointerSize);

                         // Reverse the offset: raw_pointer = file_offset - pointerOffset
                         const qint64 rawPointerValue = static_cast<qint64>(offset) - _pointerOffset;

                         QByteArray data(effectivePtrSize, 0);
                         uchar *raw = reinterpret_cast<uchar *>(data.data());

                         if (effectivePtrSize == 2)
                         {
                             const quint16 val16 = static_cast<quint16>(rawPointerValue);
                             if (hexEdit->byteOrder == ByteOrder::BigEndian)
                                 qToBigEndian<quint16>(val16, raw);
                             else
                             {
                                 qToLittleEndian<quint16>(val16, raw);
                                 if (hexEdit->byteOrder == ByteOrder::SwappedBytes)
                                     std::swap(data[0], data[1]);
                             }
                         }
                         else
                         {
                             const quint32 val32 = static_cast<quint32>(rawPointerValue);
                             if (hexEdit->byteOrder == ByteOrder::BigEndian)
                                 qToBigEndian<quint32>(val32, raw);
                             else
                             {
                                 qToLittleEndian<quint32>(val32, raw);
                                 if (hexEdit->byteOrder == ByteOrder::SwappedBytes)
                                 {
                                     std::swap(data[0], data[1]);
                                     std::swap(data[2], data[3]);
                                 }
                             }
                         }

                         if (updatePointers)
                         {
                             hexEdit->replace(ptrOffset, effectivePtrSize, data);
                             pointerBatch.append({
                                 ptrOffset,
                                 PointerListModel::encodePtrValue(offset, effectivePtrSize)
                             });
                         }
                     }


                     offset += length;
                }

                progress.setValue(++progressStep);
                QApplication::processEvents();
            }

            if (!hadChunks) {
                const QByteArray data = decodeScriptText(script.remove(reNoNewlines), tb);
                if (!data.isEmpty())
                    hexEdit->replace(offset, data.size(), data);
                progress.setValue(1);
                QApplication::processEvents();
            }

            if (!pointerBatch.isEmpty())
                hexEdit->pointers()->addPointersBatch(pointerBatch);

            hexEdit->setUpdatesEnabled(true);
            hexEdit->viewport()->update();

            if (stack)
                stack->endMacro();
    //    }
    }
}


void InsertScriptDialog::on_cbUpdatePointers_stateChanged(int arg1)
{
    Q_UNUSED(arg1);
}

void InsertScriptDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        ui->retranslateUi(this);
    QDialog::changeEvent(event);
}


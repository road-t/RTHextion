#include "QtWidgets/qpushbutton.h"
#include <QMessageBox>
#include <QKeyEvent>
#include <QInputDialog>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QtEndian>
#include <numeric>

#include "pointersdialog.h"
#include "ui_pointersdialog.h"
#include "PointerListModel.h"
#include <QtConcurrent/QtConcurrentRun>

PointersDialog::PointersDialog(QHexEdit *hexEdit, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::PointersDialog),
    _hexEdit(hexEdit)
{
    ui->setupUi(this);

    setWindowModality(Qt::NonModal);
    setWindowFlags(Qt::CustomizeWindowHint | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint | Qt::Window | Qt::WindowStaysOnTopHint);

    plModel = _hexEdit->pointers();

    plModel->setSectionNames(QStringList() << tr("Ptr offset") << tr("Points to") << tr("Data"));

    ui->tvPointers->setModel(plModel);
    ui->tvPointers->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tvPointers->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->tvPointers->verticalHeader()->setVisible(false);
    ui->tvPointers->sortByColumn(0, Qt::AscendingOrder);
    ui->tvPointers->setColumnWidth(0, 78);
    ui->tvPointers->setColumnWidth(1, 78);

    ui->leRangeBegin->setMaxLength(5);
    ui->leRangeEnd->setMaxLength(5);

    const int fieldWidth = fontMetrics().horizontalAdvance(QLatin1Char('0')) * 5 + 16;
    ui->leRangeBegin->setFixedWidth(fieldWidth);
    ui->leRangeEnd->setFixedWidth(fieldWidth);

    ui->bbControls->button(QDialogButtonBox::Ok)->setText("Find");

    ui->btnDeletePointer->setEnabled(false);
    connect(ui->tvPointers->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this](const QItemSelection &, const QItemSelection &)
            { ui->btnDeletePointer->setEnabled(
                  !ui->tvPointers->selectionModel()->selectedRows().isEmpty()); });
}

PointersDialog::~PointersDialog()
{
    delete ui;
}

void PointersDialog::refreshFromTable()
{
    tb = nullptr;
    if (plModel)
        plModel->refreshData();

    ui->tvPointers->viewport()->update();
    ui->tvPointers->resizeColumnsToContents();
}

qint64 PointersDialog::parseHexField(const QString &text, bool *ok) const
{
    QString clean = text;
    clean.remove(' ');

    if (clean.isEmpty())
    {
        if (ok)
            *ok = false;
        return 0;
    }

    return clean.toLongLong(ok, 16);
}

bool PointersDialog::validateRangeInputs(bool showMessage)
{
    bool beginOk = false;
    bool endOk = false;

    const qint64 begin = parseHexField(ui->leRangeBegin->text(), &beginOk);
    const qint64 end = parseHexField(ui->leRangeEnd->text(), &endOk);
    const qint64 fileSize = _hexEdit->data().size();

    const bool relationValid = beginOk && endOk && begin >= 0 && end >= 0 && begin <= end && begin <= fileSize && end <= fileSize;
    const bool valid = relationValid;
    const QString badStyle("QLineEdit { color: red; }");

    const bool beginFieldValid = beginOk && begin >= 0 && begin <= fileSize;
    const bool endFieldValid = endOk && end >= 0 && end <= fileSize;

    ui->leRangeBegin->setStyleSheet((beginFieldValid && relationValid) ? QString() : badStyle);
    ui->leRangeEnd->setStyleSheet((endFieldValid && relationValid) ? QString() : badStyle);
    ui->bbControls->button(QDialogButtonBox::Ok)->setEnabled(valid && !searchActive);

    if (!valid && showMessage)
        QMessageBox::warning(this, QString(), tr("Invalid range. Use hex values within file size and ensure begin <= end."));

    return valid;
}

void PointersDialog::showEvent(QShowEvent *ev)
{
    Q_UNUSED(ev);

    ui->btnCleanAll->setEnabled(!_hexEdit->pointers()->empty());
    //ui->bbControls->button(QDialogButtonBox::Ok)->setEnabled(_hexEdit->hasSelection());

    ui->tvPointers->setSortingEnabled(false);

    auto _tb = _hexEdit->getTranslationTable();

    // if table was added/changed/deleted
    if (_tb != tb)
    {
        tb = _tb;
        ui->cbRangeStart->clear();
        ui->cbRangeEnd->clear();

        // populate range comboboxes with translation table values...
        if (tb)
        {
            size_t i = 0;
            tb->reset();

            while (i++ < tb->size())
            {
                auto el = tb->next();

                ui->cbRangeStart->addItem(el.second, el.first);
                ui->cbRangeEnd->addItem(el.second, el.first);
                ui->cbStopChar->addItem(el.second, el.first);
            }

            ui->cbRangeStart->setCurrentIndex(0);
            ui->cbRangeEnd->setCurrentIndex(i - 1);
            ui->cbStopChar->setCurrentIndex(0);

            ui->cbStopChar->setDisabled(false);
            ui->lbStopChar->setDisabled(false);
        }
        else // ...or with standard ASCII printable characters
        {
            for (uint8_t c = 0x20; c < 0x80; c++)
            {
                ui->cbRangeStart->addItem(QChar(c), c);
                ui->cbRangeEnd->addItem(QChar(c), c);
            }

            ui->cbRangeStart->setCurrentIndex(0);
            ui->cbRangeEnd->setCurrentIndex(0x5E);

            ui->cbStopChar->setDisabled(true);
            ui->lbStopChar->setDisabled(true);
        }
    }

    if (plModel->rowCount())
        ui->tvPointers->resizeColumnsToContents();

    ui->tvPointers->setSortingEnabled(true);
    ui->tvPointers->sortByColumn(0, Qt::AscendingOrder);

    qint64 selBegin = _hexEdit->getSelectionBegin();
    qint64 selEnd = _hexEdit->getSelectionEnd();
    if (selBegin >= selEnd)
    {
        selBegin = _hexEdit->getCurrentOffset();
        selEnd = _hexEdit->data().size();
    }

    ui->leRangeBegin->setText(QString::number(selBegin, 16).toUpper());
    ui->leRangeEnd->setText(QString::number(selEnd, 16).toUpper());
    validateRangeInputs(false);

    ui->rbLE->setChecked(!_hexEdit->bigEndian);
    ui->rbBE->setChecked(_hexEdit->bigEndian);

    ui->progressBar->setValue(0);
    ui->progressBar->setFormat(tr("Ready"));
}

void PointersDialog::on_bbControls_accepted()
{
    if (searchActive)
    {
        cancelRequested.store(true);
        return;
    }

    // Get pointer size early for validation
    const int pointerSize = ui->rb2Byte->isChecked() ? 2 : (ui->rb3Byte->isChecked() ? 3 : 4);

    // Parse range
    bool rangeOkBegin = false;
    bool rangeOkEnd = false;
    qint64 selBegin = parseHexField(ui->leRangeBegin->text(), &rangeOkBegin);
    qint64 selEnd = parseHexField(ui->leRangeEnd->text(), &rangeOkEnd);

    if (!rangeOkBegin || !rangeOkEnd)
    {
        QMessageBox::warning(this, QString(), tr("Invalid range values."));
        return;
    }

    const qint64 rangeSize = selEnd - selBegin;

    // Warn if 2-byte pointers and range > 0x1000
    if (pointerSize == 2 && rangeSize > 0x1000)
    {
        QMessageBox msgBox(QMessageBox::Warning, QString(),
                           tr("Warning: 2-byte pointer search in large range (> 0x1000) can take a very long time.\n\nContinue anyway?"),
                           QMessageBox::Yes | QMessageBox::No, this);
        if (msgBox.exec() != QMessageBox::Yes)
            return;
    }

    if (!validateRangeInputs(true))
        return;

    bool before = ui->rbBefore->isChecked();
    bool after = ui->rbAfter->isChecked();

    if (ui->rbBoth->isChecked())
    {
        before = true;
        after = true;
    }

    // Initialize UI for search
    cancelRequested.store(false);
    searchActive = true;
    ui->progressBar->setValue(0);
    ui->progressBar->setFormat(tr("0% | Found: 0"));
    ui->progressBar->setVisible(true);
    ui->btnStop->setEnabled(true);
    ui->bbControls->button(QDialogButtonBox::Ok)->setText("Stop");
    ui->bbControls->button(QDialogButtonBox::Close)->setEnabled(false);
    ui->tvPointers->setSortingEnabled(false);

    // Get search parameters
    const bool optimizeForText = ui->cbOptimize->isChecked();
    const char firstPrintable = ui->cbRangeStart->currentData().toChar().toLatin1();
    const char lastPrintable = ui->cbRangeEnd->currentData().toChar().toLatin1();
    auto stopChar = ui->cbStopChar->isEnabled() ? ui->cbStopChar->currentData().toChar().toLatin1() : 0;
    const bool bigEndian = ui->rbBE->isChecked();
    const bool excludeSelection = ui->cbExcludeSelection->isChecked();
    const QByteArray fileData = _hexEdit->data();
    const qint64 fileSize = fileData.size();

    selBegin = qBound(static_cast<qint64>(0), selBegin, fileSize);
    selEnd = qBound(static_cast<qint64>(0), selEnd, fileSize);

    if (selBegin >= selEnd)
    {
        selBegin = 0;
        selEnd = fileSize;
    }

    searchFuture = QtConcurrent::run([=]()
                                     {
        struct SearchRange
        {
            qint64 start = 0;
            qint64 end = 0;
        };

        auto toIterations = [pointerSize](const SearchRange &r) -> qint64
        {
            const qint64 len = r.end - r.start;
            return len >= pointerSize ? (len - (pointerSize - 1)) : 0;
        };

        QVector<SearchRange> ranges;
        auto addRange = [&ranges, pointerSize](qint64 start, qint64 end)
        {
            if (end - start >= pointerSize)
            {
                SearchRange range;
                range.start = start;
                range.end = end;
                ranges.push_back(range);
            }
        };

        if (before)
            addRange(0, selBegin);
        if (after)
            addRange(selEnd, fileSize);

        if (before && after && !excludeSelection)
        {
            ranges.clear();
            addRange(0, fileSize);
        }

        if (ranges.isEmpty())
        {
            QMetaObject::invokeMethod(this, [=]() { finishSearchUi(false, 0, 0); }, Qt::QueuedConnection);
            return;
        }

        const char *buf = fileData.constData();
        auto isCandidateOffset = [&](quint32 value) -> bool
        {
            if (value < static_cast<quint32>(selBegin) || value >= static_cast<quint32>(selEnd))
                return false;

            if (!optimizeForText)
                return true;

            if (value == 0)
                return true;

            const char prevChar = buf[value - 1];
            return !(prevChar >= firstPrintable && prevChar <= lastPrintable);
        };

        const qint64 totalIterations = std::accumulate(ranges.cbegin(), ranges.cend(), static_cast<qint64>(0),
            [&](qint64 total, const SearchRange &range)
        {
            return total + toIterations(range);
        });

        if (totalIterations <= 0)
        {
            QMetaObject::invokeMethod(this, [=]() { finishSearchUi(false, 0, 0); }, Qt::QueuedConnection);
            return;
        }

        QElapsedTimer timer;
        timer.start();

        qint64 found = 0;
        qint64 processed = 0;
        QVector<QPair<qint64, qint64>> batch;
        batch.reserve(512);

        auto flushBatch = [&](int percent)
        {
            QVector<QPair<qint64, qint64>> out;
            out.swap(batch);
            QMetaObject::invokeMethod(this, [=]() {
                if (!out.isEmpty())
                    plModel->addPointersBatch(out);
                updateProgress(percent, static_cast<int>(found));
            }, Qt::QueuedConnection);
        };

        for (const auto &range : ranges)
        {
            for (qint64 j = range.start; j <= range.end - pointerSize; ++j)
            {
                if (cancelRequested.load())
                    break;

                if (stopChar && buf[j] == stopChar)
                {
                    ++processed;
                    continue;
                }

                quint64 value = 0;
                const uchar *ptr = reinterpret_cast<const uchar *>(buf + j);

                if (pointerSize == 2)
                {
                    const quint16 val16 = bigEndian ? qFromBigEndian<quint16>(ptr) : qFromLittleEndian<quint16>(ptr);
                    value = static_cast<quint64>(val16);
                }
                else if (pointerSize == 3)
                {
                    if (bigEndian)
                    {
                        value = (static_cast<quint64>(ptr[0]) << 16) | (static_cast<quint64>(ptr[1]) << 8) | static_cast<quint64>(ptr[2]);
                    }
                    else
                    {
                        value = static_cast<quint64>(ptr[0]) | (static_cast<quint64>(ptr[1]) << 8) | (static_cast<quint64>(ptr[2]) << 16);
                    }
                }
                else // 4 bytes
                {
                    const quint32 val32 = bigEndian ? qFromBigEndian<quint32>(ptr) : qFromLittleEndian<quint32>(ptr);
                    value = static_cast<quint64>(val32);
                }

                if (isCandidateOffset(static_cast<quint32>(value)))
                {
                    batch.append(qMakePair(j, static_cast<qint64>(value)));
                    ++found;
                }

                ++processed;

                if (batch.size() >= 512 || (processed % 8192 == 0))
                {
                    const int percent = static_cast<int>((processed * 100) / totalIterations);
                    flushBatch(percent);
                }
            }
            if (cancelRequested.load())
                break;
        }

        if (!batch.isEmpty())
        {
            const int percent = static_cast<int>((processed * 100) / totalIterations);
            flushBatch(percent);
        }

        const bool cancelled = cancelRequested.load();
        const qint64 elapsed = timer.elapsed();
        QMetaObject::invokeMethod(this, [=]() {
            finishSearchUi(cancelled, static_cast<int>(found), elapsed);
        }, Qt::QueuedConnection); });
}

void PointersDialog::updateProgress(int percent, int found)
{
    const int normalizedPercent = qBound(0, percent, 100);
    ui->progressBar->setValue(normalizedPercent);
    ui->progressBar->setFormat(tr("%1% | Found: %2").arg(normalizedPercent).arg(found));
}

void PointersDialog::on_btnStop_clicked()
{
    cancelRequested.store(true);
    ui->btnStop->setEnabled(false);
}

void PointersDialog::finishSearchUi(bool cancelled, int found, qint64 elapsedMs)
{
    searchActive = false;
    updateProgress(100, found);
    ui->progressBar->setVisible(false);
    ui->btnStop->setEnabled(false);
    ui->bbControls->button(QDialogButtonBox::Ok)->setText("Find");
    ui->bbControls->button(QDialogButtonBox::Ok)->setEnabled(true);
    ui->bbControls->button(QDialogButtonBox::Close)->setEnabled(true);
    ui->tvPointers->resizeColumnsToContents();
    ui->tvPointers->setSortingEnabled(true);
    ui->btnCleanAll->setEnabled(!plModel->empty());

    if (cancelled)
    {
        QMessageBox::information(this, QString(), tr("Search stopped.\nPointers found: %1\nTime elapsed %2ms").arg(found).arg(elapsedMs));
        return;
    }

    QMessageBox::information(this, QString(), tr("Pointers found: %1\nTime elapsed %2ms").arg(found).arg(elapsedMs));
}

void PointersDialog::on_bbControls_rejected()
{
    if (searchActive)
    {
        cancelRequested.store(true);
        return;
    }

    emit accepted();
    close();
}

void PointersDialog::on_bbControls_clicked(QAbstractButton *button)
{
    Q_UNUSED(button)
}

void PointersDialog::on_cbOptimize_stateChanged(int arg1)
{
    bool disabled = !arg1;

    ui->cbRangeStart->setDisabled(disabled);
    ui->lbMinus->setDisabled(disabled);
    ui->cbRangeEnd->setDisabled(disabled);
    ui->lbFirstChar->setDisabled(disabled);

    // enable only when translation table is o
    auto tb = _hexEdit->getTranslationTable();

    ui->cbStopChar->setDisabled(disabled || !tb);
    ui->lbStopChar->setDisabled(disabled || !tb);
}


void PointersDialog::on_cbRangeStart_currentIndexChanged(int index)
{
    if (index > ui->cbRangeEnd->currentIndex())
        ui->cbRangeEnd->setCurrentIndex(index);
}


void PointersDialog::on_cbRangeEnd_currentIndexChanged(int index)
{
    if (index < ui->cbRangeStart->currentIndex())
        ui->cbRangeStart->setCurrentIndex(index);
}


void PointersDialog::on_tvPointers_doubleClicked(const QModelIndex &index)
{
    auto newindex = index.sibling(index.row(), qMin(index.column(), 1)); // click at found string = click at it's offset

    auto selectedOffset = ui->tvPointers->model()->itemData(newindex).value(0).toString().toUInt(nullptr, 16);

    _hexEdit->setCursorPosition(selectedOffset * 2);
    _hexEdit->ensureVisible();
}

void PointersDialog::on_btnAddPointer_clicked()
{
    bool ok = false;
    const QString pointerText = QInputDialog::getText(this,
                                                      tr("Add pointer"),
                                                      tr("Pointer offset (hex/dec, e.g. 0x1234):"),
                                                      QLineEdit::Normal,
                                                      QString(),
                                                      &ok);
    if (!ok || pointerText.isEmpty())
        return;

    const qint64 pointerOffset = pointerText.toLongLong(&ok, 0);
    if (!ok || pointerOffset < 0)
    {
        QMessageBox::warning(this, QString(), tr("Invalid pointer offset."));
        return;
    }

    const QString valueText = QInputDialog::getText(this,
                                                    tr("Add pointer"),
                                                    tr("Pointer value (hex/dec, e.g. 0x5678):"),
                                                    QLineEdit::Normal,
                                                    QString(),
                                                    &ok);
    if (!ok || valueText.isEmpty())
        return;

    const qint64 pointedOffset = valueText.toLongLong(&ok, 0);
    if (!ok || pointedOffset < 0)
    {
        QMessageBox::warning(this, QString(), tr("Invalid pointer value."));
        return;
    }

    plModel->addPointer(pointerOffset, pointedOffset);
    ui->tvPointers->resizeColumnsToContents();
    ui->btnCleanAll->setEnabled(!plModel->empty());
}

void PointersDialog::on_btnDeletePointer_clicked()
{
    const auto selectedRows = ui->tvPointers->selectionModel()->selectedRows(0);
    if (selectedRows.isEmpty())
        return;

    QMessageBox msg(QMessageBox::Warning, nullptr, tr("Are you sure want to delete pointer from list?"), QMessageBox::Yes | QMessageBox::No, this);
    if (msg.exec() != QMessageBox::Yes)
        return;

    QVector<qint64> pointersToDelete;
    for (const QModelIndex &offsetIndex : selectedRows)
    {
        const qint64 pointer = ui->tvPointers->model()->itemData(offsetIndex).value(0).toString().toLongLong(nullptr, 16);
        pointersToDelete.append(pointer);
    }

    plModel->dropPointersBatch(pointersToDelete);
    ui->btnCleanAll->setEnabled(!plModel->empty());
}

void PointersDialog::on_btnCleanAll_clicked()
{
    QMessageBox msg(QMessageBox::Warning, nullptr, tr("Are you sure want to clear pointers list?"), QMessageBox::Yes | QMessageBox::No, this);
    if (msg.exec() != QMessageBox::Yes)
        return;

    _hexEdit->clearPointers();
    ui->tvPointers->reset();
    ui->btnCleanAll->setEnabled(false);
}

void PointersDialog::on_leRangeBegin_textChanged(const QString &text)
{
    Q_UNUSED(text)
    validateRangeInputs(false);
}

void PointersDialog::on_leRangeEnd_textChanged(const QString &text)
{
    Q_UNUSED(text)
    validateRangeInputs(false);
}

void PointersDialog::keyPressEvent(QKeyEvent *event)
{
    if (ui->tvPointers->hasFocus())
    {
        if (event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete)
        {
            on_btnDeletePointer_clicked();
        }
    }
}

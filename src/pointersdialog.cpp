#include "QtWidgets/qpushbutton.h"
#include <QApplication>
#include <QCoreApplication>
#include <QMessageBox>
#include <QKeyEvent>
#include <QShowEvent>
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
#include <QRegularExpression>
#include <QRegularExpressionValidator>

PointersDialog::PointersDialog(QHexEdit *hexEdit, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::PointersDialog),
    _hexEdit(hexEdit)
{
    ui->setupUi(this);

    setWindowModality(Qt::NonModal);
    setWindowFlags(Qt::CustomizeWindowHint | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint | Qt::Window | Qt::WindowStaysOnTopHint);

    plModel = _hexEdit->pointers();

    plModel->setSectionNames(QStringList() << tr("offset") << tr("Pointer") << tr("Data"));

    ui->tvPointers->setModel(plModel);
    ui->tvPointers->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tvPointers->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->tvPointers->verticalHeader()->setVisible(false);
    ui->tvPointers->sortByColumn(0, Qt::AscendingOrder);
    ui->tvPointers->setColumnWidth(0, 78);
    ui->tvPointers->setColumnWidth(1, 78);

    ui->leRangeBegin->setInputMask("");
    ui->leRangeEnd->setInputMask("");
    ui->leRangeBegin->setMaxLength(16);
    ui->leRangeEnd->setMaxLength(16);
    ui->leRangeBegin->setValidator(new QRegularExpressionValidator(
        QRegularExpression("[0-9A-Fa-f]*"), this));
    ui->leRangeEnd->setValidator(new QRegularExpressionValidator(
        QRegularExpression("[0-9A-Fa-f]*"), this));

    const int fieldWidth = fontMetrics().horizontalAdvance(QLatin1Char('0')) * 10 + 16;
    ui->leRangeBegin->setFixedWidth(fieldWidth);
    ui->leRangeEnd->setFixedWidth(fieldWidth);
    ui->rb4Byte->setChecked(true);
    ui->gbPointerSize->setEnabled(false);

    ui->bbControls->button(QDialogButtonBox::Ok)->setText(tr("Find"));

    ui->btnDeletePointer->setEnabled(false);
    connect(ui->tvPointers->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this](const QItemSelection &, const QItemSelection &)
            { ui->btnDeletePointer->setEnabled(
                  !ui->tvPointers->selectionModel()->selectedRows().isEmpty()); });

    // Timer-based UI drain: the background search thread pushes results to a
    // mutex-protected buffer; the timer flushes it to the model at ~10 fps so
    // the event queue is never flooded and the Stop button stays responsive.
    _uiUpdateTimer = new QTimer(this);
    _uiUpdateTimer->setInterval(100);
    connect(_uiUpdateTimer, &QTimer::timeout, this, &PointersDialog::drainPendingResults);

    // When the background future completes, finalize the UI on the main thread.
    connect(&_futureWatcher, &QFutureWatcher<void>::finished,
            this, &PointersDialog::onSearchFinished);
}

PointersDialog::~PointersDialog()
{
    // Stop the search cleanly before any members are destroyed.
    if (searchActive)
    {
        cancelRequested.store(true);
        _futureWatcher.disconnect(); // prevent onSearchFinished from running after destruction
        searchFuture.waitForFinished();
        // Drain any queued events the thread might have posted.
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents
                                        | QEventLoop::ExcludeSocketNotifiers);
    }
    if (_uiUpdateTimer)
        _uiUpdateTimer->stop();
    if (_quickSearchBusyCursor)
    {
        QApplication::restoreOverrideCursor();
        _quickSearchBusyCursor = false;
    }
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

void PointersDialog::quickSearch(qint64 clickBytePos)
{
    if (searchActive)
        return;

    // If combo boxes are not yet populated (dialog was never shown), initialize the UI first.
    if (ui->cbRangeStart->count() == 0)
    {
        QShowEvent fakeEv;
        showEvent(&fakeEv);
    }

    // Determine the range to use:
    //  - If there is a selection AND the click was inside it → use the selection.
    //  - Otherwise → use the clicked byte address as a single-byte range.
    const qint64 selBegin = _hexEdit->getSelectionBegin();
    const qint64 selEnd   = _hexEdit->getSelectionEnd();
    const bool hasSelection = selBegin < selEnd;

    if (hasSelection && clickBytePos >= selBegin && clickBytePos < selEnd)
    {
        ui->leRangeBegin->setText(QString::number(selBegin, 16).toUpper());
        ui->leRangeEnd->setText(QString::number(selEnd,   16).toUpper());
    }
    else if (clickBytePos >= 0)
    {
        // Single-byte range: begin = clickBytePos, end = clickBytePos + 1
        ui->leRangeBegin->setText(QString::number(clickBytePos,     16).toUpper());
        ui->leRangeEnd->setText(QString::number(clickBytePos + 1, 16).toUpper());
    }
    // else: leave the fields as they were (e.g. launched without a click)

    validateRangeInputs();

    // Match the editor's current endianness, search the whole file, and skip
    // text-optimization heuristics — they are irrelevant for a direct address search.
    switch (_hexEdit->byteOrder) {
    case ByteOrder::BigEndian:
        ui->rbBE->setChecked(true);
        break;
    case ByteOrder::SwappedBytes:
        ui->rbSW->setChecked(true);
        break;
    default:
        ui->rbLE->setChecked(true);
        break;
    }
    ui->rbBoth->setChecked(true);           // search pointers in the entire file
    ui->cbOptimize->setChecked(false);      // no text optimization
    ui->cbExcludeSelection->setChecked(false);

    _quickSearchMode = true;
    if (!_quickSearchBusyCursor)
    {
        QApplication::setOverrideCursor(Qt::BusyCursor);
        _quickSearchBusyCursor = true;
    }
    on_bbControls_accepted();

    // If the search did not actually start (e.g. invalid range), reset the flag.
    if (!searchActive)
    {
        _quickSearchMode = false;
        if (_quickSearchBusyCursor)
        {
            QApplication::restoreOverrideCursor();
            _quickSearchBusyCursor = false;
        }
    }
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

bool PointersDialog::validateRangeInputs()
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
        selEnd = selBegin + 1;
    }

    ui->leRangeBegin->setText(QString::number(selBegin, 16).toUpper());
    ui->leRangeEnd->setText(QString::number(selEnd, 16).toUpper());
    validateRangeInputs();

    ui->rbLE->setChecked(_hexEdit->byteOrder == ByteOrder::LittleEndian);
    ui->rbBE->setChecked(_hexEdit->byteOrder == ByteOrder::BigEndian);
    ui->rbSW->setChecked(_hexEdit->byteOrder == ByteOrder::SwappedBytes);
    ui->rb4Byte->setChecked(true);

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

    if (!validateRangeInputs())
        return;

    const int pointerSize = 4;

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
    _pendingFound.store(0);
    _pendingPercent.store(0);
    _searchWasCancelled.store(false);
    _searchElapsedMs.store(0);
    {
        QMutexLocker lock(&_pendingMutex);
        _pendingResults.clear();
    }
    ui->progressBar->setValue(0);
    ui->progressBar->setVisible(true);
    ui->btnStop->setEnabled(true);
    ui->bbControls->button(QDialogButtonBox::Ok)->setText(tr("Stop"));
    ui->bbControls->button(QDialogButtonBox::Close)->setEnabled(false);
    ui->tvPointers->setSortingEnabled(false);

    // Get search parameters
    const bool optimizeForText = ui->cbOptimize->isChecked();
    const char firstPrintable = ui->cbRangeStart->currentData().toChar().toLatin1();
    const char lastPrintable = ui->cbRangeEnd->currentData().toChar().toLatin1();
    auto stopChar = ui->cbStopChar->isEnabled() ? ui->cbStopChar->currentData().toChar().toLatin1() : 0;
    const ByteOrder searchOrder = ui->rbBE->isChecked() ? ByteOrder::BigEndian
                                : ui->rbSW->isChecked() ? ByteOrder::SwappedBytes
                                                        : ByteOrder::LittleEndian;
    const bool excludeSelection = ui->cbExcludeSelection->isChecked();
    const QByteArray fileData = _hexEdit->data();
    const qint64 fileSize = fileData.size();

    selBegin = qBound(static_cast<qint64>(0), selBegin, fileSize);
    selEnd = qBound(static_cast<qint64>(0), selEnd, fileSize);

    // Treat begin==end as a single-byte inclusive range; only fall back to
    // the whole file when begin is strictly greater than end (invalid input).
    if (selBegin == selEnd)
        selEnd = qMin(selBegin + 1, fileSize);
    else if (selBegin > selEnd)
    {
        selBegin = 0;
        selEnd = fileSize;
    }

    searchFuture = QtConcurrent::run([=]()
    {
        struct SearchRange
        {
            qint64 startOffset = 0;
            qint64 endOffsetExclusive = 0;
        };

        const qint64 maxDecodedStartExclusive = fileSize - pointerSize + 1;

        auto toIterations = [fileSize, maxDecodedStartExclusive](const SearchRange &r) -> qint64
        {
            if (maxDecodedStartExclusive <= 0)
                return 0;

            const qint64 start = qBound(static_cast<qint64>(0), r.startOffset, fileSize);
            const qint64 end = qBound(static_cast<qint64>(0), r.endOffsetExclusive, fileSize);
            const qint64 decodeEnd = qMin(end, maxDecodedStartExclusive);
            return qMax(static_cast<qint64>(0), decodeEnd - start);
        };

        QVector<SearchRange> ranges;
        auto addRange = [&ranges](qint64 startOffset, qint64 endOffsetExclusive)
        {
            if (startOffset < endOffsetExclusive)
            {
                SearchRange range;
                range.startOffset = startOffset;
                range.endOffsetExclusive = endOffsetExclusive;
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
            _searchWasCancelled.store(false);
            _searchElapsedMs.store(0);
            return;
        }

        const char *buf = fileData.constData();
        auto isCandidateOffset = [&](quint64 value) -> bool
        {
            const qint64 targetOffset = static_cast<qint64>(value);

            if (targetOffset < selBegin || targetOffset >= selEnd)
                return false;

            if (!optimizeForText)
                return true;

            if (targetOffset == 0)
                return true;

            const char prevChar = buf[targetOffset - 1];
            return !(prevChar >= firstPrintable && prevChar <= lastPrintable);
        };

        const qint64 totalIterations = std::accumulate(ranges.cbegin(), ranges.cend(), static_cast<qint64>(0),
            [&](qint64 total, const SearchRange &range)
        {
            return total + toIterations(range);
        });

        if (totalIterations <= 0)
        {
            _searchWasCancelled.store(false);
            _searchElapsedMs.store(0);
            return;
        }

        QElapsedTimer timer;
        timer.start();

        qint64 found = 0;
        qint64 processed = 0;
        QVector<QPair<qint64, qint64>> batch;
        batch.reserve(512);

        // Push accumulated batch to the shared buffer (no UI calls from this thread).
        auto flushBatch = [&](int percent)
        {
            if (!batch.isEmpty())
            {
                QMutexLocker lock(&_pendingMutex);
                _pendingResults.append(batch);
                batch.clear();
            }
            _pendingFound.store(found);
            _pendingPercent.store(percent);
        };

        for (const auto &range : ranges)
        {
            const qint64 startOffset = qBound(static_cast<qint64>(0), range.startOffset, fileSize);
            const qint64 endOffsetExclusive = qBound(static_cast<qint64>(0), range.endOffsetExclusive, fileSize);
            const qint64 decodeEndExclusive = qMin(endOffsetExclusive, maxDecodedStartExclusive);

            for (qint64 j = startOffset; j < decodeEndExclusive; ++j)
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
                value = decodePointer(ptr, pointerSize, searchOrder);

                if (isCandidateOffset(value))
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

        // Store final state for onSearchFinished() to read on the main thread.
        _searchWasCancelled.store(cancelRequested.load());
        _searchElapsedMs.store(timer.elapsed());
        _pendingFound.store(found);
        _pendingPercent.store(100);
    });

    _futureWatcher.setFuture(searchFuture);
    _uiUpdateTimer->start();
}

void PointersDialog::drainPendingResults()
{
    // Cap the amount of work done per timer tick so the main thread stays
    // responsive and the Stop button can be pressed even during large searches.
    constexpr int kMaxPerDrain = 4096;

    QVector<QPair<qint64, qint64>> results;
    bool bufferEmpty = false;
    {
        QMutexLocker lock(&_pendingMutex);
        if (_pendingResults.size() <= kMaxPerDrain)
        {
            results.swap(_pendingResults);
            bufferEmpty = true;
        }
        else
        {
            results = _pendingResults.mid(0, kMaxPerDrain);
            _pendingResults.remove(0, kMaxPerDrain);
        }
    }
    if (!results.isEmpty())
        plModel->addPointersBatch(results);

    updateProgress(_pendingPercent.load(), static_cast<int>(_pendingFound.load()));

    // Finalize once the background thread has completed AND the buffer is empty.
    if (_searchFinishPending && bufferEmpty)
    {
        _uiUpdateTimer->stop();
        _searchFinishPending = false;
        finishSearchUi(_searchWasCancelled.load(),
                       static_cast<int>(_pendingFound.load()),
                       _searchElapsedMs.load());
    }
}

void PointersDialog::onSearchFinished()
{
    // Signal that the background thread is done. drainPendingResults() will
    // continue ticking via the timer, consuming remaining buffered results in
    // small chunks, and will call finishSearchUi() once the buffer is empty.
    _searchFinishPending = true;
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
    if (_quickSearchBusyCursor)
    {
        QApplication::restoreOverrideCursor();
        _quickSearchBusyCursor = false;
    }
    updateProgress(100, found);
    ui->progressBar->setVisible(false);
    ui->btnStop->setEnabled(false);
    ui->bbControls->button(QDialogButtonBox::Ok)->setText(tr("Find"));
    ui->bbControls->button(QDialogButtonBox::Ok)->setEnabled(true);
    ui->bbControls->button(QDialogButtonBox::Close)->setEnabled(true);
    if (plModel->rowCount() <= 10000)
        ui->tvPointers->resizeColumnsToContents();
    ui->tvPointers->setSortingEnabled(true);
    ui->btnCleanAll->setEnabled(!plModel->empty());

    if (_quickSearchMode)
    {
        _quickSearchMode = false;
        emit searchCompleted(found);
        return;
    }

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
    const qint64 selectedOffset = index.data(PointerListModel::ValueRole).toLongLong();

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

    _hexEdit->addPointerUndoable(pointerOffset, pointedOffset);
    ui->tvPointers->resizeColumnsToContents();
    ui->btnCleanAll->setEnabled(!plModel->empty());
}

void PointersDialog::on_btnDeletePointer_clicked()
{
    const auto selectedRows = ui->tvPointers->selectionModel()->selectedRows();
    if (selectedRows.isEmpty())
        return;

    QMessageBox msg(QMessageBox::Warning, nullptr, tr("Are you sure want to delete pointer from list?"), QMessageBox::Yes | QMessageBox::No, this);
    if (msg.exec() != QMessageBox::Yes)
        return;

    QVector<qint64> pointersToDelete;
    pointersToDelete.reserve(selectedRows.size());
    for (const QModelIndex &rowIndex : selectedRows)
    {
        const qint64 pointer = rowIndex.data(PointerListModel::KeyRole).toLongLong();
        pointersToDelete.append(pointer);
    }

    _hexEdit->removePointersUndoable(pointersToDelete);
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
    validateRangeInputs();
}

void PointersDialog::on_leRangeEnd_textChanged(const QString &text)
{
    Q_UNUSED(text)
    validateRangeInputs();
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

void PointersDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        ui->retranslateUi(this);
        plModel->setSectionNames(QStringList() << tr("offset") << tr("Pointer") << tr("Data"));
        if (!searchActive)
            ui->bbControls->button(QDialogButtonBox::Ok)->setText(tr("Find"));
    }
    QDialog::changeEvent(event);
}

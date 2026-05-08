#include "SemiAutoTableDialog.h"
#include "hexeditor/hexeditor.h"
#include "translationtable.h"

#include <QLineEdit>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QEvent>
#include <QProgressBar>
#include <QTimer>
#include <QApplication>
#include <QSet>

#include <QtConcurrent/QtConcurrentRun>

#include <limits>

namespace {

constexpr qint64 kCancelledSearchPos = std::numeric_limits<qint64>::min();

qint64 findRelativeMatchWithProgress(const QByteArray &haystack,
                                    const QByteArray &needle,
                                    qint64 from,
                                    std::atomic<bool> *cancelRequested,
                                    std::atomic<int> *progressPercent)
{
    if (needle.isEmpty())
        return -1;

    const char *buf = haystack.constData();
    const int searchLen = needle.size();
    const qint64 maxOffset = haystack.size() - searchLen;
    if (maxOffset < 0)
        return -1;

    QByteArray relNeedle;
    relNeedle.reserve(searchLen);
    relNeedle.append('\0');

    for (int j = 1; j < searchLen; ++j)
        relNeedle.append(needle[0] - needle[j]);

    const qint64 start = qBound<qint64>(0, from, maxOffset);
    const qint64 total = qMax<qint64>(1, maxOffset - start + 1);

    if (progressPercent)
        progressPercent->store(0);

    for (qint64 i = start; i <= maxOffset; ++i)
    {
        if (cancelRequested && cancelRequested->load())
            return kCancelledSearchPos;

        int coin = 1;
        for (int j = 1; j < searchLen; ++j)
        {
            if ((buf[i] - buf[i + j]) != relNeedle[j])
                break;
            ++coin;
        }

        if (coin == searchLen)
        {
            if (progressPercent)
                progressPercent->store(95);
            return i;
        }

        if (progressPercent && (((i - start) % 4096) == 0 || i == maxOffset))
            progressPercent->store(static_cast<int>(((i - start + 1) * 95) / total));
    }

    if (progressPercent)
        progressPercent->store(95);
    return -1;
}

} // namespace

SemiAutoTableDialog::SemiAutoTableDialog(HexEditor *hexEdit, QWidget *parent)
    : QDialog(parent), _hexEdit(hexEdit)
{
    setWindowTitle(tr("Semi-auto table generation"));
    setModal(true);

    auto *mainLayout = new QVBoxLayout(this);

    auto *label = new QLabel(tr("Enter a known text fragment (Latin letters):"));
    mainLayout->addWidget(label);

    _leSearch = new QLineEdit;
    _leSearch->setMaxLength(20);
    _leSearch->setPlaceholderText(tr("At least 3 different characters"));
    // Width for ~20 characters
    QFontMetrics fm(_leSearch->font());
    _leSearch->setFixedWidth(fm.averageCharWidth() * 24 + 16);
    mainLayout->addWidget(_leSearch);

    _lbHint = new QLabel(tr("Enter at least 3 different characters to start search"));
    _lbHint->setWordWrap(true);
    mainLayout->addWidget(_lbHint);

    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch();

    _pbFind = new QPushButton(tr("Find"));
    _pbFind->setDefault(true);
    _pbFind->setEnabled(false);
    buttonLayout->addWidget(_pbFind);

    _pbCancel = new QPushButton(tr("Cancel"));
    buttonLayout->addWidget(_pbCancel);

    mainLayout->addLayout(buttonLayout);

    setLayout(mainLayout);
    setFixedSize(sizeHint());

    _generationProgressTimer = new QTimer(this);
    _generationProgressTimer->setInterval(50);

    connect(_pbFind, &QPushButton::clicked, this, &SemiAutoTableDialog::onFind);
    connect(_pbCancel, &QPushButton::clicked, this, [this]() {
        if (_generationWatcher.isRunning())
            requestGenerationCancel();
        else
            reject();
    });
    connect(_leSearch, &QLineEdit::textChanged, this, &SemiAutoTableDialog::updateFindButtonState);
    connect(_generationProgressTimer, &QTimer::timeout, this, &SemiAutoTableDialog::updateGenerationProgress);
    connect(&_generationWatcher, &QFutureWatcher<void>::finished,
            this, &SemiAutoTableDialog::handleGenerationFinished);

    updateFindButtonState();
}

void SemiAutoTableDialog::onFind()
{
    auto text = _leSearch->text();

    if (!hasEnoughUniqueCharacters(text))
        return;

    startGeneration(text);
}

void SemiAutoTableDialog::startGeneration(const QString &text)
{
    if (_generationWatcher.isRunning())
        return;

    QByteArray needle = text.toLatin1();
    if (needle.isEmpty())
        return;

    createProgressDialog();
    if (_progressDialog)
        _progressDialog->show();
    qApp->processEvents(QEventLoop::ExcludeUserInputEvents);

    const QByteArray fileData = _hexEdit ? _hexEdit->data() : QByteArray();

    _generationSearchText = text;
    _generationCancelRequested.store(false);
    _generationProgressPercent.store(0);
    {
        QMutexLocker lock(&_generationResultMutex);
        _generationOutcome = GenerationOutcome::None;
        _generatedMatchPosition = -1;
        _pendingGeneratedTable = TranslationTable();
    }

    setGenerationControlsEnabled(false);
    _generationProgressTimer->start();

    _generationFuture = QtConcurrent::run([this, fileData, needle, text]() {
        TranslationTable generated;
        GenerationOutcome outcome = GenerationOutcome::Failed;
        qint64 matchPos = -1;

        const qint64 pos = findRelativeMatchWithProgress(
            fileData, needle, 0, &_generationCancelRequested, &_generationProgressPercent);

        if (pos == kCancelledSearchPos)
        {
            outcome = GenerationOutcome::Cancelled;
        }
        else if (pos < 0)
        {
            outcome = GenerationOutcome::NotFound;
        }
        else
        {
            matchPos = pos;
            _generationProgressPercent.store(97);

            const QString input = QString::fromLatin1(fileData.constData() + pos, needle.size());
            generated.generateTable(input, text);

            if (_generationCancelRequested.load())
                outcome = GenerationOutcome::Cancelled;
            else if (generated.size() == 0)
                outcome = GenerationOutcome::Failed;
            else
                outcome = GenerationOutcome::Success;
        }

        _generationProgressPercent.store(100);

        QMutexLocker lock(&_generationResultMutex);
        _generationOutcome = outcome;
        _generatedMatchPosition = matchPos;
        _pendingGeneratedTable = generated;
    });

    _generationWatcher.setFuture(_generationFuture);
}

void SemiAutoTableDialog::updateFindButtonState()
{
    _pbFind->setEnabled(hasEnoughUniqueCharacters(_leSearch->text()));
}

bool SemiAutoTableDialog::hasEnoughUniqueCharacters(const QString &text) const
{
    QSet<QChar> uniqueChars;
    for (const QChar ch : text)
        uniqueChars.insert(ch);
    return uniqueChars.size() >= 3;
}

void SemiAutoTableDialog::createProgressDialog()
{
    destroyProgressDialog();

    _progressDialog = new QDialog(this, Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
    _progressDialog->setModal(true);
    _progressDialog->setWindowTitle(tr("Generating table"));

    auto *layout = new QVBoxLayout(_progressDialog);

    _progressLabel = new QLabel(tr("Searching for relative match..."), _progressDialog);
    _progressLabel->setWordWrap(true);
    layout->addWidget(_progressLabel);

    _progressBar = new QProgressBar(_progressDialog);
    _progressBar->setRange(0, 100);
    _progressBar->setValue(0);
    layout->addWidget(_progressBar);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->addStretch();
    _progressCancelButton = new QPushButton(tr("Cancel"), _progressDialog);
    buttonRow->addWidget(_progressCancelButton);
    layout->addLayout(buttonRow);

    connect(_progressCancelButton, &QPushButton::clicked,
            this, &SemiAutoTableDialog::requestGenerationCancel);

    _progressDialog->setFixedSize(_progressDialog->sizeHint());
}

void SemiAutoTableDialog::destroyProgressDialog()
{
    if (!_progressDialog)
        return;

    _progressDialog->close();
    _progressDialog->deleteLater();
    _progressDialog = nullptr;
    _progressLabel = nullptr;
    _progressBar = nullptr;
    _progressCancelButton = nullptr;
}

void SemiAutoTableDialog::setGenerationControlsEnabled(bool enabled)
{
    _leSearch->setEnabled(enabled);
    _pbCancel->setEnabled(enabled);
    if (enabled)
        updateFindButtonState();
    else
        _pbFind->setEnabled(false);
}

void SemiAutoTableDialog::updateGenerationProgress()
{
    if (_progressBar)
        _progressBar->setValue(_generationProgressPercent.load());

    if (_progressLabel) {
        if (_generationCancelRequested.load())
            _progressLabel->setText(tr("Cancelling..."));
        else if (_generationProgressPercent.load() < 95)
            _progressLabel->setText(tr("Searching for relative match..."));
        else
            _progressLabel->setText(tr("Generating table entries..."));
    }
}

void SemiAutoTableDialog::requestGenerationCancel()
{
    if (!_generationWatcher.isRunning())
        return;

    _generationCancelRequested.store(true);
    if (_progressCancelButton)
        _progressCancelButton->setEnabled(false);
    if (_progressLabel)
        _progressLabel->setText(tr("Cancelling..."));
}

void SemiAutoTableDialog::handleGenerationFinished()
{
    _generationProgressTimer->stop();
    updateGenerationProgress();
    destroyProgressDialog();
    setGenerationControlsEnabled(true);

    GenerationOutcome outcome = GenerationOutcome::Failed;
    qint64 matchPos = -1;
    TranslationTable generated;
    {
        QMutexLocker lock(&_generationResultMutex);
        outcome = _generationOutcome;
        matchPos = _generatedMatchPosition;
        generated = _pendingGeneratedTable;
    }

    if (outcome == GenerationOutcome::Cancelled)
        return;

    if (outcome == GenerationOutcome::NotFound)
    {
        QMessageBox::information(this, tr("Not found"),
            tr("No relative match found for \"%1\".\nTry a different string, possibly shorter.").arg(_generationSearchText));
        return;
    }

    if (outcome == GenerationOutcome::Failed)
    {
        QMessageBox::warning(
            this,
            tr("Generation failed"),
            tr("Could not generate any table entries from the found match.\nTry a different string with more variety of letters."));
        return;
    }

    _generatedTable = generated;
    _hasGeneratedTable = true;

    if (_hexEdit && matchPos >= 0)
        _hexEdit->highlightMatch(matchPos, _generationSearchText.toLatin1().size());

    QMessageBox::information(
        this,
        tr("Table generated"),
        tr("Generated %1 table entries.\nThe table editor will open so you can review and adjust.").arg(_generatedTable.size()));

    emit tableGenerated();
    accept();
}

void SemiAutoTableDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(event);
}

void SemiAutoTableDialog::retranslateUi()
{
    setWindowTitle(tr("Semi-auto table generation"));
    _leSearch->setPlaceholderText(tr("At least 3 different characters"));
    _lbHint->setText(tr("Enter at least 3 different characters to start search"));
    _pbFind->setText(tr("Find"));
    _pbCancel->setText(tr("Cancel"));
    if (_progressDialog)
        _progressDialog->setWindowTitle(tr("Generating table"));
    if (_progressCancelButton)
        _progressCancelButton->setText(tr("Cancel"));
    updateGenerationProgress();
}

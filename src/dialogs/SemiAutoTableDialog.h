#ifndef SEMIAUTOTABLEDIALOG_H
#define SEMIAUTOTABLEDIALOG_H

#include <QDialog>
#include <QFuture>
#include <QFutureWatcher>
#include <QMutex>

#include <atomic>

#include "translationtable.h"

class QLineEdit;
class QPushButton;
class QLabel;
class QDialog;
class QProgressBar;
class QTimer;
class HexEditor;

class SemiAutoTableDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SemiAutoTableDialog(HexEditor *hexEdit, QWidget *parent = nullptr);
    bool hasGeneratedTable() const { return _hasGeneratedTable; }
    const TranslationTable &generatedTable() const { return _generatedTable; }

signals:
    void tableGenerated();

private slots:
    void onFind();
    void updateFindButtonState();
    void updateGenerationProgress();
    void handleGenerationFinished();
    void requestGenerationCancel();

private:
    enum class GenerationOutcome {
        None,
        Success,
        NotFound,
        Failed,
        Cancelled
    };

    void changeEvent(QEvent *event) override;
    void retranslateUi();
    bool hasEnoughUniqueCharacters(const QString &text) const;
    void startGeneration(const QString &text);
    void createProgressDialog();
    void destroyProgressDialog();
    void setGenerationControlsEnabled(bool enabled);

    HexEditor *_hexEdit;
    TranslationTable _generatedTable;
    bool _hasGeneratedTable = false;
    QLineEdit *_leSearch;
    QPushButton *_pbFind;
    QPushButton *_pbCancel;
    QLabel *_lbHint;

    QFuture<void> _generationFuture;
    QFutureWatcher<void> _generationWatcher;
    QTimer *_generationProgressTimer = nullptr;
    QDialog *_progressDialog = nullptr;
    QLabel *_progressLabel = nullptr;
    QProgressBar *_progressBar = nullptr;
    QPushButton *_progressCancelButton = nullptr;
    std::atomic<bool> _generationCancelRequested{false};
    std::atomic<int> _generationProgressPercent{0};
    QMutex _generationResultMutex;
    GenerationOutcome _generationOutcome = GenerationOutcome::None;
    qint64 _generatedMatchPosition = -1;
    TranslationTable _pendingGeneratedTable;
    QString _generationSearchText;
};

#endif // SEMIAUTOTABLEDIALOG_H

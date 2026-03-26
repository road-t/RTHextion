#ifndef POINTERSDIALOG_H
#define POINTERSDIALOG_H

#include <QDialog>
#include <QAbstractButton>
#include <QFuture>
#include <QFutureWatcher>
#include <QMutex>
#include <QTimer>
#include <atomic>
#include "hexeditor/hexeditor.h"
#include "PointerListModel.h"
#include "romdetect.h"

class QComboBox;
class QLineEdit;
class QGroupBox;
class PointersDockWidget;

namespace Ui {
class PointersDialog;
}

class PointersDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PointersDialog(HexEditor *hexEdit, QWidget *parent = nullptr);
    ~PointersDialog();
    void refreshFromTable();
    void quickSearch(qint64 clickBytePos = -1);
    void setHexEdit(HexEditor *he);
    void setRomProfile(RomType type, qint64 offset);

    /// Per-tab snapshot of user-controlled options (excluding ROM profile and range, which
    /// are derived from the current session and selection respectively).
    struct State {
        int  searchDir        = 0;  // 0=before, 1=after, 2=both
        bool excludeSelection = false;
        bool alignedOnly      = false;
        bool optimize         = false;
    };
    State dialogState() const;
    void  setDialogState(const State &s);
    void setDock(PointersDockWidget *dock) { m_dock = dock; }

signals:
    void searchCompleted(int found);
    void searchStarted();
    void searchFinished();

private slots:
    void on_bbControls_accepted();
    void on_bbControls_rejected();
    void on_bbControls_clicked(QAbstractButton *button);
    void on_cbOptimize_stateChanged(int arg1);
    void on_cbRangeStart_currentIndexChanged(int index);
    void on_cbRangeEnd_currentIndexChanged(int index);
    void updateProgress(int percent, int found);
    void on_btnStop_clicked();
    void on_leRangeBegin_textChanged(const QString &text);
    void on_leRangeEnd_textChanged(const QString &text);
    void finishSearchUi(bool cancelled, int found, qint64 elapsedMs);
    void drainPendingResults();
    void onSearchFinished();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *ev) override;
    void changeEvent(QEvent *event) override;
    PointerListModel* plModel = nullptr;
    QFuture<void> searchFuture;
    std::atomic<bool> cancelRequested{false};
    bool searchActive = false;
    bool _quickSearchMode = false;
    bool _quickSearchBusyCursor = false;

    qint64 parseHexField(const QString &text, bool *ok) const;
    bool validateRangeInputs();

private:
    Ui::PointersDialog *ui;
    HexEditor *_hexEdit;
    TranslationTable* tb;
    PointersDockWidget *m_dock = nullptr;

    // Thread-safe result buffer: background thread pushes here, timer drains on UI thread
    QMutex _pendingMutex;
    QVector<QPair<qint64, qint64>> _pendingResults;
    std::atomic<qint64> _pendingFound{0};
    std::atomic<int>    _pendingPercent{0};
    std::atomic<bool>   _searchWasCancelled{false};
    std::atomic<qint64> _searchElapsedMs{0};

    QTimer *_uiUpdateTimer = nullptr;
    QFutureWatcher<void> _futureWatcher;
    bool _searchFinishPending = false;
    bool _profileInitialized = false;

    // ROM profile widgets
    QGroupBox  *_gbProfile = nullptr;
    QComboBox  *_cbProfileRomType = nullptr;
    QLineEdit  *_leProfileOffset = nullptr;
};

#endif // POINTERSDIALOG_H

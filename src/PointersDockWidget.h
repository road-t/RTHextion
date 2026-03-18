#ifndef POINTERSDOCKWIDGET_H
#define POINTERSDOCKWIDGET_H

#include <QDockWidget>
#include <QTableView>
#include <QToolBar>
#include <QAction>

class QHexEdit;
class PointerListModel;

class PointersDockWidget : public QDockWidget
{
    Q_OBJECT

public:
    explicit PointersDockWidget(QWidget *parent = nullptr);

    /// Bind to the hex editor. Must be called after construction.
    void setHexEdit(QHexEdit *hexEdit);

    /// Add the show-pointers toggle action (from MainWindow) to the toolbar.
    void addShowPointersAction(QAction *act);

    /// Called by PointersDialog when a search begins (disables sorting for performance).
    void beginSearch();
    /// Called by PointersDialog when a search ends (re-enables sorting and resizes columns).
    void endSearch();
    /// Refresh the view (e.g. after external model changes).
    void refreshView();

    void retranslateUi();

signals:
    void findPointersRequested();

private slots:
    void onHexSelectionChanged(qint64 start, qint64 end);
    void onListSelectionChanged();
    void onDoubleClicked(const QModelIndex &index);
    void addPointer();
    void deleteSelectedPointers();
    void cleanAllPointers();

private:
    void updateButtonStates();

    QHexEdit           *m_hexEdit    = nullptr;
    PointerListModel   *m_model      = nullptr;
    QTableView         *m_view       = nullptr;
    QToolBar           *m_toolbar    = nullptr;
    QAction            *m_findAct    = nullptr;
    QAction            *m_addAct     = nullptr;
    QAction            *m_deleteAct  = nullptr;
    QAction            *m_cleanAllAct = nullptr;
};

#endif // POINTERSDOCKWIDGET_H

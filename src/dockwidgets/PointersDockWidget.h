#ifndef POINTERSDOCKWIDGET_H
#define POINTERSDOCKWIDGET_H

#include <QDockWidget>
#include <QTableView>
#include <QToolBar>
#include <QAction>
#include <QToolButton>
#include <QPointer>

class HexEditor;
class PointerListModel;

class PointersDockWidget : public QDockWidget
{
    Q_OBJECT

public:
    explicit PointersDockWidget(QWidget *parent = nullptr);

    /// Bind to the hex editor. Must be called after construction.
    void setHexEdit(HexEditor *hexEdit);

    /// Add the show-pointers toggle action (from MainWindow) to the toolbar.
    void addShowPointersAction(QAction *act);

    /// Called by PointersDialog when a search begins (disables sorting for performance).
    void beginSearch();
    /// Called by PointersDialog when a search ends (re-enables sorting and resizes columns).
    void endSearch();
    /// Refresh the view (e.g. after external model changes).
    void refreshView();

    void setCollapsed(bool collapsed);
    bool isCollapsed() const;
    void resetCollapse();

    void retranslateUi();

signals:
    void findPointersRequested();
    void showPointersToggled(bool checked);

public:
    void setShowPointersChecked(bool checked);
    void setShowPointersEnabled(bool enabled);

    QByteArray saveColumnsState() const;
    void restoreColumnsState(const QByteArray &state);
private slots:
    void onHexSelectionChanged(qint64 start, qint64 end);
    void onListSelectionChanged();
    void onDoubleClicked(const QModelIndex &index);
    void addPointer();
    void deleteSelectedPointers();
    void cleanAllPointers();
    void showContextMenu(const QPoint &pos);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateButtonStates();

    QPointer<HexEditor>      m_hexEdit;
    QPointer<PointerListModel> m_model;
    QWidget            *m_contentWidget = nullptr;
    QTableView         *m_view       = nullptr;
    QToolBar           *m_toolbar    = nullptr;
    QAction            *m_findAct    = nullptr;
    QAction            *m_addAct     = nullptr;
    QAction            *m_deleteAct  = nullptr;
    QAction            *m_cleanAllAct = nullptr;
    QToolButton        *m_showPointersBtn = nullptr;
    bool                m_collapsed = false;
    int                 m_savedExpandedWidth = -1;
    int                 m_savedExpandedHeight = -1;
};

#endif // POINTERSDOCKWIDGET_H

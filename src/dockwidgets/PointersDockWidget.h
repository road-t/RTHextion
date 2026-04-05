#ifndef POINTERSDOCKWIDGET_H
#define POINTERSDOCKWIDGET_H

#include <QDockWidget>
#include <QTableView>
#include <QToolBar>
#include <QAction>
#include <QToolButton>
#include <QPointer>

#include "BaseDockWidget.h"

class HexEditor;
class PointerListModel;

class PointersDockWidget : public BaseDockWidget
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

    void retranslateUi() override;

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
    void onPaletteChanged() override;

private:
    void updateButtonStates();

    QPointer<HexEditor>      m_hexEdit;
    QPointer<PointerListModel> m_model;
    QTableView         *m_view       = nullptr;
    QToolBar           *m_toolbar    = nullptr;
    QAction            *m_findAct    = nullptr;
    QAction            *m_addAct     = nullptr;
    QAction            *m_deleteAct  = nullptr;
    QAction            *m_cleanAllAct = nullptr;
    QToolButton        *m_showPointersBtn = nullptr;
};

#endif // POINTERSDOCKWIDGET_H

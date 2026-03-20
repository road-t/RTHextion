#ifndef TABLESDOCKWIDGET_H
#define TABLESDOCKWIDGET_H

#include <QDockWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QLineEdit>
#include <QVector>
#include <QUndoStack>
#include <QLabel>
#include <QToolButton>

#include "translationtable.h"

class HexEditor;

/// A single tab holding a named translation table and its editor grid.
struct TableTab
{
    QString name;             // user-editable display name
    TranslationTable table;   // value-owned, copied by value for snapshots/undo
    bool isOriginal = false;  // true = this is the "original" encoding table
};

/// Dock widget that contains a QTabWidget with one tab per translation table.
/// Provides add / duplicate / remove / export actions and Ctrl+N shortcuts.
class TablesDockWidget : public QDockWidget
{
    Q_OBJECT

public:
    explicit TablesDockWidget(QWidget *parent = nullptr);
    ~TablesDockWidget() override;

    // --- Table management ---
    /// Add a new empty table and switch to it.
    int addTable(const QString &name = QString(), const TranslationTable *table = nullptr);

    /// Duplicate the currently active table into a new tab.
    void duplicateCurrentTable();

    /// Remove the currently active tab (and its table) — asks for confirmation.
    void removeCurrentTable();

    /// Export the currently active table to a .tbl file.
    void exportCurrentTable();

    /// Import a table from a .tbl file into a new tab.
    void importTable();

    /// Remove all tabs and delete all tables.
    void clearAll();

    /// Number of tables.
    int count() const;

    /// Currently active table (may be nullptr if no tabs).
    TranslationTable *currentTable() const;

    /// Index of the active tab.
    int currentIndex() const;

    /// Set the active tab by index.
    void setCurrentIndex(int index);

    /// Get all tables for serialization.
    const QVector<TableTab> &allTables() const;

    /// Set the current project name (used for export filename suggestions).
    void setProjectName(const QString &name) { m_projectName = name; }

    /// Returns whether the table at index is marked as original.
    bool isTableOriginal(int index) const;
    /// Set or clear the original flag for the given index
    /// (only one table can be original at a time).
    void setTableOriginal(int index, bool original);

    /// Retranslate UI strings.
    void retranslateUi();

    /// Undo stack (exposed so main window can connect undo/redo actions).
    QUndoStack *undoStack() const { return m_undoStack; }

    /// Sync the eye (use-table) button state from outside (e.g. from MainWindow).
    void setUseTableChecked(bool checked);
    void setUseTableEnabled(bool enabled);

    /// Apply a full state snapshot (used by undo/redo and per-tab session restore).
    void applySnapshot(const QVector<TableTab> &snapshot, int activeIndex);

    /// Capture current state as a snapshot (deep copy).
    QVector<TableTab> takeSnapshot() const;

    /// Save/restore grid column widths for all tabs.
    QByteArray saveColumnsState() const;
    void restoreColumnsState(const QByteArray &state);

signals:
    /// Emitted when the currently selected table changes (switched tab or edited).
    void activeTableChanged(TranslationTable *table);

    /// Emitted when any table's content is modified (entry added/removed/edited).
    void tableContentChanged();

    /// Emitted when the eye (use-table) toggle button is clicked.
    void useTableToggled(bool checked);

private slots:
    void onTabChanged(int index);
    void onTabDoubleClicked(int index);
    void onCellChanged(int row, int col);
    void onTabContextMenu(const QPoint &pos);

private:
    void populateGrid(QTableWidget *grid, TranslationTable *table);
    void syncTableFromGrid(int tabIndex);
    void updateButtonStates();
    QTableWidget *currentGrid() const;
    QTableWidget *gridAt(int index) const;
    QString defaultTabName(int number) const;

    /// Push a snapshot-based undo command.
    void pushUndoSnapshot(const QString &description);

    QWidget *m_contentWidget = nullptr;
    QTabWidget *m_tabs = nullptr;
    QToolBar *m_toolbar = nullptr;
    QUndoStack *m_undoStack = nullptr;
    QString m_projectName;
    QLabel *m_titleLabel = nullptr;
    QToolButton *m_collapseBtn = nullptr;
    QToolButton *m_useTableBtn = nullptr;

    QAction *m_addAct = nullptr;
    QAction *m_duplicateAct = nullptr;
    QAction *m_removeAct = nullptr;
    QAction *m_exportAct = nullptr;
    QAction *m_importAct = nullptr;
    QAction *m_undoAct = nullptr;
    QAction *m_redoAct = nullptr;

    QVector<TableTab> m_tables;
    int m_nextTableNumber = 1;
    bool m_ignoreChanges = false;  // guard for programmatic edits

    // Snapshot stored before a destructive operation so undo can restore it
    QVector<TableTab> m_pendingSnapshot;

    friend class TableSnapshotCommand;
};

#endif // TABLESDOCKWIDGET_H

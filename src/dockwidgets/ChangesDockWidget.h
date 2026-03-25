#ifndef CHANGESDOCKWIDGET_H
#define CHANGESDOCKWIDGET_H

#include <QDockWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QButtonGroup>
#include <QVector>
#include <QPair>
#include <QByteArray>
#include <QString>

class TranslationTable;

class ChangesDockWidget : public QDockWidget
{
    Q_OBJECT

public:
    explicit ChangesDockWidget(QWidget *parent = nullptr);

    /// Repopulate the list by comparing originals against currentData.
    /// Pass originalTable (the isOriginal=true table) for Original column encoding,
    /// and activeTable (the current non-original table) for Current column encoding.
    /// useTable=true → use tables; false → decode with encoding (like ASCII area).
    /// Null table = hex fallback when useTable is true.
    void refresh(const QVector<QPair<qint64, QByteArray>> &originals,
                 const QByteArray &currentData,
                 TranslationTable *originalTable = nullptr,
                 TranslationTable *activeTable = nullptr,
                 bool useTable = false,
                 const QString &encoding = QString());

    /// Clear all rows.
    void clear();

    /// Sync the eye button state from outside (e.g. from MainWindow).
    void setShowChangesChecked(bool checked);
    void setShowChangesEnabled(bool enabled);
    void setHexMode(bool hexMode);
    bool hexMode() const { return m_hexMode; }

    void setShowOriginalChecked(bool checked);
    void setShowOriginalEnabled(bool enabled);

    void setCollapsed(bool collapsed);
    bool isCollapsed() const;

    QByteArray saveColumnsState() const;
    void restoreColumnsState(const QByteArray &state);

    void retranslateUi();

signals:
    void showChangesToggled(bool checked);
    void showOriginalToggled(bool show);
    void jumpToOffset(qint64 offset);

private slots:
    void onRowDoubleClicked(const QModelIndex &index);

private:
    QWidget     *m_contentWidget  = nullptr;
    QTableWidget *m_table         = nullptr;
    QToolButton  *m_showChangesBtn = nullptr;
    QToolButton  *m_currentBtn    = nullptr;
    QToolButton  *m_originalBtn   = nullptr;
    QButtonGroup *m_viewModeGroup = nullptr;
    QToolButton  *m_textBtn       = nullptr;
    QToolButton  *m_hexBtn        = nullptr;
    QButtonGroup *m_displayModeGroup = nullptr;
    bool          m_hexMode       = false;
    bool          m_collapsed     = false;
    int           m_savedExpandedWidth = -1;
    int           m_savedExpandedHeight = -1;

    // Stored parameters for re-rendering when display mode is toggled
    QVector<QPair<qint64, QByteArray>> m_lastOriginals;
    QByteArray     m_lastCurrentData;
    TranslationTable *m_lastOrigTable   = nullptr;
    TranslationTable *m_lastActiveTable = nullptr;
    bool           m_lastUseTable = false;
    QString        m_lastEncoding;
};

#endif // CHANGESDOCKWIDGET_H

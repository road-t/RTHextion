#ifndef BASEDOCKWIDGET_H
#define BASEDOCKWIDGET_H

#include <QDockWidget>
#include <QIcon>
#include <QToolButton>

class DockTitleBar;

/// Base class for all RTHextion dock widgets.
/// Provides: collapse/expand, DockTitleBar setup, theme-aware eye icon,
/// and the common palette-change handler.
class BaseDockWidget : public QDockWidget
{
    Q_OBJECT

public:
    explicit BaseDockWidget(const QString &title, QWidget *parent = nullptr);

    void setCollapsed(bool collapsed);
    bool isCollapsed() const { return m_collapsed; }
    void resetCollapse();

    /// Override in subclasses to retranslate UI strings.
    virtual void retranslateUi() {}

    /// Paint a 16×16 eye icon using the given color.
    static QIcon makeEyeIcon(const QColor &col);
    /// Paint a 16×16 "+" add icon.
    static QIcon makeAddIcon(const QColor &col);
    /// Paint a 16×16 "×" remove icon.
    static QIcon makeRemoveIcon(const QColor &col);
    /// Paint a 16×16 "→□" copy-to-tab icon.
    static QIcon makeCopyToIcon(const QColor &col);
    /// Paint a 16×16 export (↓ arrow + baseline) icon.
    static QIcon makeExportIcon(const QColor &col);

protected:
    /// Call from subclass constructor after creating the content widget.
    void initTitleBar();

    /// Called when the palette changes. Subclasses override to refresh icons.
    /// Default implementation does nothing.
    virtual void onPaletteChanged() {}

    void changeEvent(QEvent *event) override;

    QWidget *m_contentWidget = nullptr;
    int      m_defaultExpandedWidth = 320;

private:
    bool m_collapsed = false;
    int  m_savedExpandedWidth = -1;
    int  m_savedExpandedHeight = -1;
};

#endif // BASEDOCKWIDGET_H

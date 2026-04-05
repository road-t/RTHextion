#include "BaseDockWidget.h"
#include "DockTitleBar.h"

#include <QEvent>
#include <QMainWindow>
#include <QPainter>
#include <QPainterPath>

BaseDockWidget::BaseDockWidget(const QString &title, QWidget *parent)
    : QDockWidget(title, parent)
{
    setFeatures(QDockWidget::DockWidgetClosable
                | QDockWidget::DockWidgetFloatable
                | QDockWidget::DockWidgetMovable);
}

void BaseDockWidget::initTitleBar()
{
    auto *titleBar = new DockTitleBar(windowTitle(), this);
    setTitleBarWidget(titleBar);
    connect(this, &QDockWidget::dockLocationChanged,
            titleBar, &DockTitleBar::onDockLocationChanged);
    connect(this, &QDockWidget::dockLocationChanged, this, [this](Qt::DockWidgetArea) {
        resetCollapse();
    });
}

// -----------------------------------------------------------------------
// Collapse / expand
// -----------------------------------------------------------------------

void BaseDockWidget::setCollapsed(bool collapsed)
{
    if (m_collapsed == collapsed)
        return;
    m_collapsed = collapsed;

    Qt::DockWidgetArea area = Qt::NoDockWidgetArea;
    QMainWindow *mw = qobject_cast<QMainWindow *>(parentWidget());
    if (mw)
        area = mw->dockWidgetArea(this);
    const bool sideArea = (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea);

    auto *titleBar = static_cast<DockTitleBar *>(titleBarWidget());
    const int collapsedExtent = titleBar ? titleBar->collapsedExtent(sideArea) : 30;

    if (collapsed) {
        if (m_contentWidget)
            m_contentWidget->setVisible(false);
        if (titleBar)
            titleBar->setCollapsed(true);

        if (sideArea) {
            m_savedExpandedWidth = width();
            m_savedExpandedHeight = height();
            setMinimumHeight(0);
            setMaximumHeight(QWIDGETSIZE_MAX);
            setMinimumWidth(collapsedExtent);
            setMaximumWidth(collapsedExtent);
            if (mw) mw->resizeDocks({this}, {collapsedExtent}, Qt::Horizontal);
        } else {
            m_savedExpandedHeight = height();
            m_savedExpandedWidth = width();
            setMinimumWidth(m_savedExpandedWidth);
            setMinimumHeight(collapsedExtent);
            setMaximumHeight(collapsedExtent);
            if (mw) mw->resizeDocks({this}, {collapsedExtent}, Qt::Vertical);
        }
    } else {
        if (titleBar)
            titleBar->setCollapsed(false);

        setMinimumWidth(0);
        setMaximumWidth(QWIDGETSIZE_MAX);
        setMinimumHeight(0);
        setMaximumHeight(QWIDGETSIZE_MAX);

        if (m_contentWidget)
            m_contentWidget->setVisible(true);

        if (sideArea) {
            const int target = m_savedExpandedWidth > 0 ? m_savedExpandedWidth : m_defaultExpandedWidth;
            if (mw) mw->resizeDocks({this}, {target}, Qt::Horizontal);
        } else {
            const int targetH = m_savedExpandedHeight > 0 ? m_savedExpandedHeight : 220;
            const int targetW = m_savedExpandedWidth > 0 ? m_savedExpandedWidth : -1;
            if (mw) {
                mw->resizeDocks({this}, {targetH}, Qt::Vertical);
                if (targetW > 0)
                    mw->resizeDocks({this}, {targetW}, Qt::Horizontal);
            }
        }
    }
}

void BaseDockWidget::resetCollapse()
{
    if (!m_collapsed)
        return;
    m_collapsed = false;
    m_savedExpandedWidth = -1;
    m_savedExpandedHeight = -1;
    setMinimumWidth(0);
    setMaximumWidth(QWIDGETSIZE_MAX);
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);
    if (m_contentWidget)
        m_contentWidget->setVisible(true);
    if (auto *tb = static_cast<DockTitleBar *>(titleBarWidget()))
        tb->setCollapsed(false);
}

// -----------------------------------------------------------------------
// Theme-aware eye icon
// -----------------------------------------------------------------------

QIcon BaseDockWidget::makeEyeIcon(const QColor &col)
{
    auto paint = [&col](bool filled) -> QPixmap {
        const int sz = 16;
        QPixmap pm(sz, sz);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(col, 1.3));
        QPainterPath lens;
        const float cx = sz * 0.5f, cy = sz * 0.5f;
        const float rx = sz * 0.44f, ry = sz * 0.27f;
        lens.moveTo(cx - rx, cy);
        lens.cubicTo(cx - rx * 0.4f, cy - ry * 2.0f,
                     cx + rx * 0.4f, cy - ry * 2.0f, cx + rx, cy);
        lens.cubicTo(cx + rx * 0.4f, cy + ry * 2.0f,
                     cx - rx * 0.4f, cy + ry * 2.0f, cx - rx, cy);
        p.drawPath(lens);
        const float pr = ry * 0.72f;
        if (filled) { p.setBrush(col); p.setPen(Qt::NoPen); }
        p.drawEllipse(QPointF(cx, cy), pr, pr);
        return pm;
    };
    QIcon icon;
    icon.addPixmap(paint(true),  QIcon::Normal,  QIcon::On);
    icon.addPixmap(paint(false), QIcon::Normal,  QIcon::Off);
    icon.addPixmap(paint(false), QIcon::Disabled, QIcon::Off);
    return icon;
}

// -----------------------------------------------------------------------
// Palette change
// -----------------------------------------------------------------------

void BaseDockWidget::changeEvent(QEvent *event)
{
    QDockWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        onPaletteChanged();
}

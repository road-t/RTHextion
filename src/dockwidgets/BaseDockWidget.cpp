#include "BaseDockWidget.h"
#include "DockTitleBar.h"

#include <QEvent>
#include <QMainWindow>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

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
            // Only capture the current size as fallback; don't overwrite an
            // already-set value (e.g. from setExpandedSize called just before).
            const int liveW = width();
            const int liveH = height();
            if (m_savedExpandedWidth  <= 0 && liveW > collapsedExtent) m_savedExpandedWidth  = liveW;
            if (m_savedExpandedHeight <= 0 && liveH > 0)               m_savedExpandedHeight = liveH;
            setMinimumHeight(0);
            setMaximumHeight(QWIDGETSIZE_MAX);
            setMinimumWidth(collapsedExtent);
            setMaximumWidth(collapsedExtent);
            if (mw) mw->resizeDocks({this}, {collapsedExtent}, Qt::Horizontal);
        } else {
            const int liveH = height();
            const int liveW = width();
            if (m_savedExpandedHeight <= 0 && liveH > collapsedExtent) m_savedExpandedHeight = liveH;
            if (m_savedExpandedWidth  <= 0 && liveW > 0)               m_savedExpandedWidth  = liveW;
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
    emit collapsedChanged(collapsed);
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
    emit collapsedChanged(false);
}

void BaseDockWidget::setExpandedSize(int w, int h)
{
    if (w > 0) {
        m_savedExpandedWidth  = w;
        m_defaultExpandedWidth = w;
    }
    if (h > 0)
        m_savedExpandedHeight = h;
}

void BaseDockWidget::resizeEvent(QResizeEvent *event)
{
    QDockWidget::resizeEvent(event);
    emit dockResized();
}

// -----------------------------------------------------------------------
// Theme-aware icons
// -----------------------------------------------------------------------

QIcon BaseDockWidget::makeEyeIcon(const QColor &col)
{
    // On state: filled pupil. Off state: diagonal slash through pupil area.
    auto paint = [&col](bool filled) -> QPixmap {
        const int sz = 16;
        QPixmap pm(sz, sz);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(col, 1.3));

        // Lens shape — 30 % less tall than original (ry reduced by 0.3)
        const float cx = sz * 0.5f, cy = sz * 0.5f;
        const float rx = sz * 0.44f, ry = sz * 0.189f; // was 0.27f

        QPainterPath lens;
        lens.moveTo(cx - rx, cy);
        lens.cubicTo(cx - rx * 0.4f, cy - ry * 2.0f,
                     cx + rx * 0.4f, cy - ry * 2.0f, cx + rx, cy);
        lens.cubicTo(cx + rx * 0.4f, cy + ry * 2.0f,
                     cx - rx * 0.4f, cy + ry * 2.0f, cx - rx, cy);
        p.drawPath(lens);

        const float pr = ry * 0.72f;
        if (filled) {
            // Filled pupil (On state — table active)
            p.setBrush(col);
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(cx, cy), pr, pr);
        } else {
            // Off state: unfilled pupil with diagonal slash from corner to corner
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(col, 1.3));
            p.drawEllipse(QPointF(cx, cy), pr, pr);
            // Diagonal slash from lower-left corner to upper-right corner of entire icon
            p.drawLine(QPointF(3.5, sz - 3.5), QPointF(sz - 3.5, 3.5));
        }
        return pm;
    };
    QIcon icon;
    icon.addPixmap(paint(true),  QIcon::Normal,  QIcon::On);
    icon.addPixmap(paint(false), QIcon::Normal,  QIcon::Off);
    icon.addPixmap(paint(false), QIcon::Disabled, QIcon::Off);
    return icon;
}

QIcon BaseDockWidget::makeAddIcon(const QColor &col)
{
    const int sz = 16;
    QPixmap pm(sz, sz);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(col, 1.5, Qt::SolidLine, Qt::RoundCap));
    // "+" — horizontal and vertical bars
    p.drawLine(QPointF(3.5, 8),  QPointF(12.5, 8));
    p.drawLine(QPointF(8,   3.5), QPointF(8,   12.5));
    return QIcon(pm);
}

QIcon BaseDockWidget::makeRemoveIcon(const QColor &col)
{
    const int sz = 16;
    QPixmap pm(sz, sz);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(col, 1.5, Qt::SolidLine, Qt::RoundCap));
    // "−" (minus sign) — horizontal bar only
    p.drawLine(QPointF(3.5, 8), QPointF(12.5, 8));
    return QIcon(pm);
}

QIcon BaseDockWidget::makeCopyToIcon(const QColor &col)
{
    // Arrow (→) pointing into a small rectangle (target tab)
    const int sz = 16;
    QPixmap pm(sz, sz);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(col, 1.3, Qt::SolidLine, Qt::RoundCap));

    // Arrow shaft: left side → into the rectangle
    p.drawLine(QPointF(1.5, 8), QPointF(9, 8));
    // Arrowhead
    p.drawLine(QPointF(7, 5.5), QPointF(9, 8));
    p.drawLine(QPointF(7, 10.5), QPointF(9, 8));
    // Target rectangle (tab)
    p.setPen(QPen(col, 1.3, Qt::SolidLine, Qt::SquareCap));
    p.drawRect(QRectF(10, 3.5, 4.5, 9));
    return QIcon(pm);
}

QIcon BaseDockWidget::makeExportIcon(const QColor &col)
{
    // Down arrow + baseline (export / save to file)
    const int sz = 16;
    QPixmap pm(sz, sz);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(col, 1.5, Qt::SolidLine, Qt::RoundCap));
    // Shaft
    p.drawLine(QPointF(8, 2),  QPointF(8, 11));
    // Arrowhead
    p.drawLine(QPointF(5, 8.5), QPointF(8, 11));
    p.drawLine(QPointF(11, 8.5), QPointF(8, 11));
    // Baseline
    p.setPen(QPen(col, 1.5, Qt::SolidLine, Qt::SquareCap));
    p.drawLine(QPointF(3, 13.5), QPointF(13, 13.5));
    return QIcon(pm);
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

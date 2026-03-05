#include "hexscrollmap.h"

#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QToolTip>
#include <QHelpEvent>

HexScrollMap::HexScrollMap(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);  // we fill the whole rect ourselves
    setMouseTracking(true);                 // for hover tooltips
}

void HexScrollMap::setTicks(const QVector<int> &ys)
{
    _ticks = ys;
    update();
}

void HexScrollMap::setTickOffsets(const QMap<int, qint64> &yToOff)
{
    _yToOff = yToOff;
}

void HexScrollMap::setColor(const QColor &c)
{
    _color = c;
    update();
}

void HexScrollMap::setBgColor(const QColor &c)
{
    _bgColor = c;
    update();
}

void HexScrollMap::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // Only emit when height actually changes — avoids spurious recomputes from
    // adjust() calling setGeometry() every scroll tick.
    if (event->oldSize().height() != event->size().height())
        emit heightChanged();
}

void HexScrollMap::paintEvent(QPaintEvent *)
{
    QPainter p(this);

    // Fill background
    p.fillRect(rect(), _bgColor);

    const int w = width();
    if (w <= 0) return;

    for (int y : qAsConst(_ticks))
        p.fillRect(0, y, w, 1, _color);
}

void HexScrollMap::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        qint64 off = offsetNearY(event->pos().y());
        if (off >= 0)
            emit tickClicked(off);
    }
    QWidget::mousePressEvent(event);
}

void HexScrollMap::mouseMoveEvent(QMouseEvent *event)
{
    qint64 off = offsetNearY(event->pos().y());
    setCursor(off >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    QWidget::mouseMoveEvent(event);
}

bool HexScrollMap::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        auto *helpEvent = static_cast<QHelpEvent *>(event);
        qint64 off = offsetNearY(helpEvent->pos().y());
        if (off >= 0) {
            const QString digits = QString::number(off, 16).toUpper();
            QString text = QStringLiteral("0x%1").arg(digits);
            QToolTip::showText(helpEvent->globalPos(), text, this);
        } else {
            QToolTip::hideText();
            event->ignore();
        }
        return true;
    }
    return QWidget::event(event);
}

qint64 HexScrollMap::offsetNearY(int y) const
{
    if (_yToOff.isEmpty())
        return -1;

    // Find closest tick within ±kHitRadius
    auto it = _yToOff.lowerBound(y - kHitRadius);
    qint64 bestOff = -1;
    int bestDist = kHitRadius + 1;

    while (it != _yToOff.end() && it.key() <= y + kHitRadius) {
        int dist = qAbs(it.key() - y);
        if (dist < bestDist) {
            bestDist = dist;
            bestOff = it.value();
        }
        ++it;
    }
    return bestOff;
}

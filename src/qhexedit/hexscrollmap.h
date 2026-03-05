#ifndef HEXSCROLLMAP_H
#define HEXSCROLLMAP_H

#include <QWidget>
#include <QVector>
#include <QColor>
#include <QMap>

/**
 * Pre-computed Y-coordinates for both map strips.
 * Built in a background thread, then distributed to each HexScrollMap.
 */
struct ScrollMapMarkers
{
    QVector<int> ptrYs;         ///< Y-positions for pointer-storage strip
    QVector<int> targetYs;      ///< Y-positions for pointer-target strip
    QMap<int, qint64> ptrYToOff;    ///< Y → nearest exact byte offset (ptr)
    QMap<int, qint64> targetYToOff; ///< Y → nearest exact byte offset (target)
};

/**
 * Thin vertical strip showing tick marks for one category of offsets.
 *
 * Each HexScrollMap instance draws a single color from a pre-computed
 * Y-position list — no expensive iteration in paintEvent.
 * Two instances are placed side-by-side in QHexEdit:
 *   - pointer storage locations (orange)
 *   - pointer target locations  (sky-blue)
 */
class HexScrollMap : public QWidget
{
    Q_OBJECT
public:
    explicit HexScrollMap(QWidget *parent = nullptr);

    /** Replace tick set and repaint. */
    void setTicks(const QVector<int> &ys);

    /** Set Y→offset mapping for tooltip & click navigation. */
    void setTickOffsets(const QMap<int, qint64> &yToOff);

    /** Set the tick color (typically called once at construction). */
    void setColor(const QColor &c);

    /** Set the strip background color. */
    void setBgColor(const QColor &c);

signals:
    /** Emitted when widget height changes so QHexEdit can recompute ticks. */
    void heightChanged();

    /** Emitted when the user clicks on or near a tick. */
    void tickClicked(qint64 byteOffset);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    bool event(QEvent *event) override;

private:
    /** Find the nearest tick offset within ±kHitRadius pixels of y, or -1. */
    qint64 offsetNearY(int y) const;

    static constexpr int kHitRadius = 3;  ///< click/hover tolerance in pixels

    QVector<int>        _ticks;
    QMap<int, qint64>   _yToOff;   ///< Y → exact byte offset
    QColor              _color   { 0xff, 0x99, 0x00 };   // default: orange
    QColor              _bgColor { 0xd0, 0xd0, 0xd0 };   // default: light grey
};

#endif // HEXSCROLLMAP_H

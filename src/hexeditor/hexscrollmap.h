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
    QVector<int> changesYs;           ///< Y-positions for changes strip
    QVector<int> pointerYs;           ///< Y-positions for pointer storage locations
    QVector<int> targetYs;            ///< Y-positions for pointed-to target addresses
    QMap<int, qint64> changesYToOff;  ///< Y → nearest exact byte offset (changed)
    QMap<int, qint64> pointerYToOff;  ///< Y → nearest exact byte offset (pointer storage)
    QMap<int, qint64> targetYToOff;   ///< Y → nearest exact byte offset (target address)
};

/**
 * Thin vertical strip showing tick marks for byte-offset categories.
 *
 * Supports two overlapping tick layers (primary + secondary) each with their
 * own color, so pointer-storage and pointer-target locations can be shown
 * together in a single strip without wasting horizontal space.
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

    /** Secondary tick layer — drawn on top of primary ticks with a different color. */
    void setSecondaryTicks(const QVector<int> &ys);
    void setSecondaryTickOffsets(const QMap<int, qint64> &yToOff);
    void setSecondaryColor(const QColor &c);

signals:
    /** Emitted when widget height changes so HexEditor can recompute ticks. */
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

    QVector<int>        _secondaryTicks;
    QMap<int, qint64>   _secondaryYToOff;
    QColor              _secondaryColor { 0x40, 0xbf, 0xff };   // default: sky-blue
};

#endif // HEXSCROLLMAP_H

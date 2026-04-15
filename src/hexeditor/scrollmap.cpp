#include "internal.h"

// ═══════════════════════════════════════════════════════════════════════════
// HexEditor scroll map strip computation
// ═══════════════════════════════════════════════════════════════════════════

bool HexEditor::scrollMapChangesVisible() const
{
    return _scrollMapChangesEnabled;
}


void HexEditor::setScrollMapChangesVisible(bool visible)
{
    if (_scrollMapChangesEnabled == visible) return;
    _scrollMapChangesEnabled = visible;
    updateScrollMapMargins();
}


bool HexEditor::scrollMapTargetVisible() const
{
    return _scrollMapTargetEnabled;
}


void HexEditor::setScrollMapTargetVisible(bool visible)
{
    if (_scrollMapTargetEnabled == visible) return;
    _scrollMapTargetEnabled = visible;
    updateScrollMapMargins();
}


void HexEditor::setScrollMapChangesBgColor(const QColor &c)
{
    if (_scrollMapChanges) _scrollMapChanges->setBgColor(c);
}


void HexEditor::setScrollMapTargetBgColor(const QColor &c)
{
    if (_scrollMapTarget) _scrollMapTarget->setBgColor(c);
}


void HexEditor::updateScrollMapMargins()
{
    // Immediate (non-debounced) re-evaluation of visibility + margins.
    // Also kicks the debounce timer for future model changes.
    scheduleScrollMapCompute();
    updateScrollMap();
}

// ---- Scroll map computation ---------------------------------------------------


void HexEditor::updateScrollMap()
{
    // Debounce: restart timer on every rapid model change.
    // scheduleScrollMapCompute() fires once things settle.
    if (_scrollMapTimer)
        _scrollMapTimer->start();
}


void HexEditor::scheduleScrollMapCompute()
{
    if (!_scrollMapWatcher || !_chunks)
        return;

    const bool hasPointers = !_pointers.empty();
    const bool hasChanges  = !_changedPositions.isEmpty();
    const bool wantChanges = _scrollMapChangesEnabled && hasChanges  && _scrollMapChanges;
    const bool wantTarget  = _scrollMapTargetEnabled  && hasPointers && _scrollMapTarget;

    // Update strip visibility
    if (_scrollMapChanges) _scrollMapChanges->setVisible(wantChanges);
    if (_scrollMapTarget)  _scrollMapTarget->setVisible(wantTarget);
    if (wantChanges) _scrollMapChanges->raise();
    if (wantTarget)  _scrollMapTarget->raise();

    // Update viewport right margin only when it actually changes (avoids recursive relayout)
    const int newMargin = (wantChanges ? kScrollMapWidth : 0) + (wantTarget ? kScrollMapWidth : 0);
    if (newMargin != _scrollMapCurrentMargin)
    {
        _scrollMapCurrentMargin = newMargin;
        setViewportMargins(0, 0, newMargin, 0);
        // setViewportMargins relayouts the viewport synchronously in Qt6,
        // so we call adjust() immediately to size the strip before reading mapH.
    }
    adjust();  // always reposition strips so height() is up-to-date

    if (!wantChanges && !wantTarget)
    {
        if (_scrollMapChanges)  _scrollMapChanges->setTicks({});
        if (_scrollMapTarget)   { _scrollMapTarget->setTicks({}); _scrollMapTarget->setSecondaryTicks({}); }
        return;
    }

    // If previous background task still running, retry after debounce
    if (_scrollMapWatcher->isRunning())
    {
        _scrollMapTimer->start();
        return;
    }

    // Both strips share the same height (set by adjust()).
    // Read from viewport() directly — adjust() has just positioned the strips,
    // but reading viewport()->height() is always authoritative and avoids any
    // stale-geometry edge cases the first time the strips are shown.
    const int mapH = viewport()->height();
    const qint64 totalBytes = _chunks->size();

    if (mapH <= 0 || totalBytes <= 0)
        return;

    // Get real groove geometry via QStyleOptionSlider (initFrom is public).
    auto *vbar = verticalScrollBar();
    const int sbMin  = vbar->minimum();
    const int sbMax  = vbar->maximum();
    const int sbPage = vbar->pageStep();
    const int bytesPerLine = qMax(1, _bytesPerLine);

    // Build QStyleOptionSlider from public scrollbar API — no initStyleOption needed.
    // Use strip dimensions as the rect so subControlRect returns groove/thumb in strip coords.
    QStyleOptionSlider vbarOpt;
    vbarOpt.initFrom(vbar);                         // sets palette, state from the widget
    vbarOpt.rect          = QRect(0, 0, kScrollMapWidth, mapH);  // ← strip size, not vbar size
    vbarOpt.minimum       = sbMin;
    vbarOpt.maximum       = sbMax;
    vbarOpt.pageStep      = sbPage;
    vbarOpt.singleStep    = vbar->singleStep();
    vbarOpt.sliderValue   = vbar->value();
    vbarOpt.sliderPosition = vbar->sliderPosition();
    vbarOpt.orientation   = Qt::Vertical;
    vbarOpt.upsideDown    = vbar->invertedAppearance();
    vbarOpt.subControls   = QStyle::SC_All;
    vbarOpt.activeSubControls = QStyle::SC_None;

    // Groove rect — in strip coords because we set opt.rect to strip size.
    const QRect grooveRect = vbar->style()->subControlRect(
        QStyle::CC_ScrollBar, &vbarOpt, QStyle::SC_ScrollBarGroove, vbar);

    // Slider (thumb) rect — also in strip coords.
    const QRect sliderRect = vbar->style()->subControlRect(
        QStyle::CC_ScrollBar, &vbarOpt, QStyle::SC_ScrollBarSlider, vbar);

    const int grooveTop = grooveRect.isValid() ? grooveRect.top() : 0;
    const int grooveH   = qMax(1, grooveRect.isValid() ? grooveRect.height() : mapH);
    const int mThumbH   = qMax(1, sliderRect.isValid() ? sliderRect.height() : 1);

    // mGrooveTop/mGrooveH are already in strip (mapH) coords — no scaling needed.
    const int mGrooveTop = grooveTop;
    const int mGrooveH   = grooveH;

    QList<qint64> changedKeys = wantChanges ? _changedPositions.values() : QList<qint64>{};
    QList<qint64> ptrKeys     = wantTarget  ? _pointers.pointerKeys()    : QList<qint64>{};
    QList<qint64> targetKeys  = wantTarget  ? _pointers.offsetKeys()     : QList<qint64>{};

    const qint64 changedRangeStart = _changedRangeStart;
    const qint64 changedRangeEnd = _changedRangeEnd;

    auto future = QtConcurrent::run(
        [mapH, totalBytes,
         sbMin, sbMax, sbPage, bytesPerLine,
         mGrooveTop, mGrooveH, mThumbH,
         wantChanges, wantTarget,
         changedRangeStart, changedRangeEnd,
         changedKeys = std::move(changedKeys),
         ptrKeys     = std::move(ptrKeys),
         targetKeys  = std::move(targetKeys)]() -> ScrollMapMarkers
        {
            ScrollMapMarkers result;

            // Map byte offset → Y that matches the scrollbar thumb CENTER,
            // using real groove geometry obtained from QStyle on the main thread.
            auto offToY = [mapH, totalBytes,
                           sbMin, sbMax, sbPage, bytesPerLine,
                           mGrooveTop, mGrooveH, mThumbH](qint64 off) -> int
            {
                if (totalBytes <= 1 || mapH <= 1)
                    return 0;

                const qint64 offClamped = qBound<qint64>(0, off, totalBytes - 1);
                const double line = static_cast<double>(offClamped) / static_cast<double>(bytesPerLine);

                // Scrollbar value that centers this offset in the viewport.
                const double value = qBound(static_cast<double>(sbMin),
                                            line - static_cast<double>(sbPage) * 0.5,
                                            static_cast<double>(sbMax));

                // Thumb travels within groove: from mGrooveTop to mGrooveTop+mGrooveH-mThumbH.
                const double travel = qMax(0.0, static_cast<double>(mGrooveH - mThumbH));
                const double valueRange = (sbMax > sbMin) ? static_cast<double>(sbMax - sbMin) : 1.0;
                const double normVal = (value - sbMin) / valueRange;

                // Y of the thumb CENTER in strip coordinates.
                const double thumbTopInStrip = mGrooveTop + normVal * travel;
                const double thumbCenterY = thumbTopInStrip + static_cast<double>(mThumbH) * 0.5;

                return qBound(0, static_cast<int>(std::round(thumbCenterY)), mapH - 1);
            };

            if (wantChanges)
            {
                QVector<bool> px(mapH, false);

                // Extended tail of file (inserted bytes beyond original) should show as continuous block
                if (changedRangeStart >= 0 && changedRangeEnd > changedRangeStart)
                {
                    int y0 = offToY(changedRangeStart);
                    int y1 = offToY(changedRangeEnd - 1);
                    if (y0 > y1)
                        std::swap(y0, y1);
                    for (int y = y0; y <= y1; ++y)
                    {
                        px[y] = true;
                        if (!result.changesYToOff.contains(y))
                            result.changesYToOff.insert(y, changedRangeStart);
                    }
                }

                for (qint64 off : changedKeys) {
                    int y = offToY(off);
                    px[y] = true;
                    if (!result.changesYToOff.contains(y))
                        result.changesYToOff.insert(y, off);
                }
                for (int i = 0; i < mapH; ++i)
                    if (px[i]) result.changesYs.push_back(i);
            }

            if (wantTarget)
            {
                QVector<bool> ptrPx(mapH, false);
                for (qint64 off : ptrKeys) {
                    int y = offToY(off);
                    ptrPx[y] = true;
                    if (!result.pointerYToOff.contains(y))
                        result.pointerYToOff.insert(y, off);
                }
                for (int i = 0; i < mapH; ++i)
                    if (ptrPx[i]) result.pointerYs.push_back(i);

                QVector<bool> targetPx(mapH, false);
                for (qint64 off : targetKeys) {
                    int y = offToY(off);
                    targetPx[y] = true;
                    if (!result.targetYToOff.contains(y))
                        result.targetYToOff.insert(y, off);
                }
                for (int i = 0; i < mapH; ++i)
                    if (targetPx[i]) result.targetYs.push_back(i);
            }

            return result;
        });

    _scrollMapWatcher->setFuture(future);
}


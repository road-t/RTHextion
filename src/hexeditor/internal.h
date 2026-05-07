// internal.h — Shared header for HexEditor split files.
#ifndef HEXEDITOR_INTERNAL_H
#define HEXEDITOR_INTERNAL_H

#include "hexeditor.h"
#include "hexscrollmap.h"
#include "SectionListModel.h"
#include "disassembler.h"
#include "romdetect.h"
#include "PointerListModel.h"

#include <QtGlobal>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStyleOptionSlider>
#include <QToolTip>
#include <QHelpEvent>
#include <QProgressDialog>
#include <QTimer>
#include <QSet>
#include <QInputDialog>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QUndoCommand>
#include <QStringDecoder>
#include <QStringEncoder>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <cerrno>
#include <iconv.h>

#include "appsettings.h"

// ── Anonymous-namespace constants used by multiple translation units ──
namespace hexeditor_detail {
    inline constexpr int kHexColumnExtraGapPx   = 4;
    inline constexpr int kHexRowExtraGapPx      = 4;
    inline constexpr int kAddressRightPaddingPx = 4;
    inline constexpr int kAsciiAreaLeftPaddingPx = 4;
    inline constexpr int kAsciiColumnGapSinglePx = 2;
    inline constexpr int kAsciiColumnGapWidePx   = 3;
    inline constexpr int kPointerByteSize        = 4;
    inline constexpr int kDefaultScrollMapWidth  = 12;
    inline constexpr int kLineBreakCmdId         = 5678;
}
using namespace hexeditor_detail;

#endif // HEXEDITOR_INTERNAL_H

#include "GraphicsDockWidget.h"
#include "DockTitleBar.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QColorDialog>
#include <QSignalBlocker>
#include <QApplication>
#include <QStyledItemDelegate>
#include <QStylePainter>
#include <QStyleOptionComboBox>
#include <cmath>
#include <random>

namespace {

constexpr int kPaletteCellSize = 24;
constexpr int kPaletteComboCellSize = 12;
constexpr int kPaletteComboCellSpacing = 1;
constexpr int kPaletteSectionStartRole = Qt::UserRole;
constexpr int kPaletteSectionColorsRole = Qt::UserRole + 1;

QVariantList paletteVariantList(const QVector<QRgb> &colors)
{
    QVariantList values;
    values.reserve(colors.size());
    for (QRgb color : colors)
        values.append(static_cast<uint>(color));
    return values;
}

QVector<QRgb> paletteFromVariant(const QVariant &value)
{
    QVector<QRgb> colors;
    const QVariantList values = value.toList();
    colors.reserve(values.size());
    for (const QVariant &item : values)
        colors.append(static_cast<QRgb>(item.toUInt()));
    return colors;
}

void paintPaletteRow(QPainter *painter,
                     const QRect &rect,
                     const QVector<QRgb> &colors,
                     const QPalette &palette,
                     bool enabled)
{
    if (!painter || !rect.isValid())
        return;

    painter->save();
    painter->setClipRect(rect);

    if (colors.isEmpty()) {
        painter->setPen(enabled ? palette.color(QPalette::Mid) : palette.color(QPalette::Disabled, QPalette::Mid));
        painter->drawRect(rect.adjusted(0, 0, -1, -1));
        painter->restore();
        return;
    }

    const int cellSize = qMax(6, qMin(kPaletteComboCellSize, rect.height()));
    const int cellStep = cellSize + kPaletteComboCellSpacing;
    const int visibleCount = qMax(1, (rect.width() + kPaletteComboCellSpacing) / cellStep);
    const int top = rect.top() + qMax(0, (rect.height() - cellSize) / 2);
    const QColor border = enabled
        ? palette.color(QPalette::Shadow).lighter(125)
        : palette.color(QPalette::Disabled, QPalette::Mid);

    for (int i = 0; i < visibleCount; ++i) {
        const int colorIndex = (i * colors.size()) / visibleCount;
        const int x = rect.left() + i * cellStep;
        if (x + cellSize > rect.right() + 1)
            break;

        const QRect cellRect(x, top, cellSize, cellSize);
        painter->fillRect(cellRect, QColor::fromRgb(colors[qBound(0, colorIndex, colors.size() - 1)]));
        painter->setPen(border);
        painter->drawRect(cellRect.adjusted(0, 0, -1, -1));
    }

    painter->restore();
}

class PaletteSectionItemDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter,
               const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        opt.text.clear();
        opt.icon = QIcon();

        const QWidget *widget = opt.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();

        painter->save();
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, painter, widget);
        paintPaletteRow(painter,
                        opt.rect.adjusted(6, 2, -6, -2),
                        paletteFromVariant(index.data(kPaletteSectionColorsRole)),
                        opt.palette,
                        opt.state.testFlag(QStyle::State_Enabled));
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override
    {
        Q_UNUSED(option);
        Q_UNUSED(index);
        return QSize(140, kPaletteComboCellSize + 8);
    }
};

class PaletteSectionComboBox final : public QComboBox
{
public:
    using QComboBox::QComboBox;

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QStylePainter painter(this);
        QStyleOptionComboBox opt;
        initStyleOption(&opt);
        painter.drawComplexControl(QStyle::CC_ComboBox, opt);

        const QVector<QRgb> colors = paletteFromVariant(currentData(kPaletteSectionColorsRole));
        if (colors.isEmpty()) {
            painter.drawControl(QStyle::CE_ComboBoxLabel, opt);
            return;
        }

        const QRect editRect = style()->subControlRect(QStyle::CC_ComboBox,
                                                       &opt,
                                                       QStyle::SC_ComboBoxEditField,
                                                       this).adjusted(2, 2, -2, -2);
        paintPaletteRow(&painter, editRect, colors, opt.palette, isEnabled());
    }
};

QVector<TileCodec> allTileCodecs()
{
    QVector<TileCodec> codecs;
    for (int ci = 0; ci <= static_cast<int>(TileCodec::Linear8bpp); ++ci)
        codecs.append(static_cast<TileCodec>(ci));
    return codecs;
}

QVector<TileCodec> preferredTileCodecsForRom(RomType rom)
{
    switch (rom) {
    case RomType::NES:
        return {TileCodec::Linear2bpp};
    case RomType::GB:
    case RomType::GBC:
        return {TileCodec::Interleaved2bpp};
    case RomType::SNES:
    case RomType::SNES_SMC:
    case RomType::SNES_HIROM:
    case RomType::SNES_HIROM_SMC:
        return {TileCodec::Interleaved4bpp, TileCodec::Planar3bpp, TileCodec::Interleaved2bpp};
    case RomType::GBA:
        return {TileCodec::Linear4bpp, TileCodec::Linear8bpp};
    case RomType::MD:
    case RomType::X32:
        return {TileCodec::SegaMD4bpp};
    case RomType::SMS:
    case RomType::GG:
    case RomType::SG1000:
    case RomType::ColecoVision:
        return {TileCodec::SegaSMS4bpp};
    default:
        return {};
    }
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// PalettePreview
// ═══════════════════════════════════════════════════════════════════════════

PalettePreview::PalettePreview(QWidget *parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setMinimumHeight(kPaletteCellSize);
    setMaximumHeight(kPaletteCellSize);
    setCursor(Qt::PointingHandCursor);
}

void PalettePreview::setPalette(const QVector<QRgb> &colors)
{
    m_colors = colors;
    updateHeightForCurrentWidth();
    updateGeometry();
    update();
}

void PalettePreview::setVisibleColorCount(int count)
{
    const int normalized = qMax(0, count);
    if (m_visibleColorCount == normalized)
        return;
    m_visibleColorCount = normalized;
    updateHeightForCurrentWidth();
    updateGeometry();
    update();
}

int PalettePreview::visibleColorCount() const
{
    if (m_visibleColorCount > 0)
        return qMin(m_colors.size(), m_visibleColorCount);
    return m_colors.size();
}

void PalettePreview::setDimmed(bool dimmed)
{
    if (m_dimmed == dimmed)
        return;
    m_dimmed = dimmed;
    update();
}

QSize PalettePreview::sizeHint() const
{
    const int count = visibleColorCount();
    const int cols = qMax(1, qMin(count, 8));
    const int rows = qMax(1, (count + cols - 1) / cols);
    return QSize(cols * kPaletteCellSize, rows * kPaletteCellSize);
}

QSize PalettePreview::minimumSizeHint() const
{
    return QSize(kPaletteCellSize, kPaletteCellSize);
}

void PalettePreview::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateHeightForCurrentWidth();
}

int PalettePreview::columnCountForWidth(int availWidth) const
{
    const int n = visibleColorCount();
    if (n <= 0)
        return 1;
    const int maxColsByWidth = qMax(1, availWidth / kPaletteCellSize);
    return qMax(1, qMin(n, maxColsByWidth));
}

void PalettePreview::updateHeightForCurrentWidth()
{
    const int cols = columnCountForWidth(width());
    const int rows = qMax(1, (visibleColorCount() + cols - 1) / cols);
    const int h = rows * kPaletteCellSize;
    setMinimumHeight(h);
    setMaximumHeight(h);
}

void PalettePreview::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int n = visibleColorCount();
    if (n == 0) {
        p.fillRect(rect(), Qt::black);
        return;
    }

    const int cols = columnCountForWidth(width());
    const int rows = (n + cols - 1) / cols;
    const int cellW = kPaletteCellSize;
    const int cellH = kPaletteCellSize;
    const int gridW = cols * cellW;
    const int gridH = rows * cellH;

    p.fillRect(0, 0, width(), height(), Qt::black);

    for (int i = 0; i < n; ++i) {
        const int cx = (i % cols) * cellW;
        const int cy = (i / cols) * cellH;
        p.fillRect(cx, cy, cellW, cellH, QColor::fromRgba(m_colors[i]));
    }

    p.setPen(QPen(QColor(255, 255, 255, 60), 1));
    for (int c = 1; c < cols; ++c)
        p.drawLine(c * cellW, 0, c * cellW, gridH);
    for (int r = 1; r < rows; ++r)
        p.drawLine(0, r * cellH, gridW, r * cellH);
    p.drawRect(0, 0, gridW, gridH);

    // Highlight left-button color (solid cyan frame)
    if (m_leftIndex >= 0 && m_leftIndex < n) {
        const int lx = (m_leftIndex % cols) * cellW;
        const int ly = (m_leftIndex / cols) * cellH;
        p.setPen(QPen(QColor(0, 255, 255), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(lx + 1, ly + 1, cellW - 2, cellH - 2);
    }
    // Highlight right-button color (solid magenta frame)
    if (m_rightIndex >= 0 && m_rightIndex < n) {
        const int rx = (m_rightIndex % cols) * cellW;
        const int ry = (m_rightIndex / cols) * cellH;
        p.setPen(QPen(QColor(255, 0, 255), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rx + 1, ry + 1, cellW - 2, cellH - 2);
    }

    if (m_dimmed)
        p.fillRect(rect(), QColor(120, 120, 120, 140));
}

void PalettePreview::mousePressEvent(QMouseEvent *event)
{
    if (visibleColorCount() <= 0)
        return;

    const int cols = columnCountForWidth(width());
    const int rows = (visibleColorCount() + cols - 1) / cols;
    const int cellW = kPaletteCellSize;
    const int cellH = kPaletteCellSize;
    if (event->pos().x() < 0 || event->pos().x() >= cols * cellW
        || event->pos().y() < 0 || event->pos().y() >= rows * cellH)
        return;
    const int col = event->pos().x() / cellW;
    const int row = event->pos().y() / cellH;
    const int idx = row * cols + col;
    if (idx < 0 || idx >= visibleColorCount())
        return;

    if (event->button() == Qt::LeftButton) {
        m_leftIndex = idx;
        update();
        emit leftColorSelected(idx);
    } else if (event->button() == Qt::RightButton) {
        m_rightIndex = idx;
        update();
        emit rightColorSelected(idx);
    }
}

void PalettePreview::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (visibleColorCount() <= 0 || event->button() != Qt::LeftButton)
        return;

    const int cols = columnCountForWidth(width());
    const int rows = (visibleColorCount() + cols - 1) / cols;
    const int cellW = kPaletteCellSize;
    const int cellH = kPaletteCellSize;
    if (event->pos().x() < 0 || event->pos().x() >= cols * cellW
        || event->pos().y() < 0 || event->pos().y() >= rows * cellH)
        return;
    const int col = event->pos().x() / cellW;
    const int row = event->pos().y() / cellH;
    const int idx = row * cols + col;
    if (idx < 0 || idx >= visibleColorCount())
        return;

    const QColor initial = QColor::fromRgba(m_colors[idx]);
    const QColor chosen = QColorDialog::getColor(initial, this, tr("Edit color"));
    if (chosen.isValid()) {
        m_colors[idx] = chosen.rgb();
        update();
        emit colorEdited(idx, chosen.rgb());
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// GraphicsDockWidget
// ═══════════════════════════════════════════════════════════════════════════

GraphicsDockWidget::GraphicsDockWidget(QWidget *parent)
    : BaseDockWidget(tr("Graphics"), parent)
{
    setWindowTitle(tr("Graphics"));
    setObjectName(QStringLiteral("GraphicsDockWidget"));

    auto *container = new QWidget(this);
    m_contentWidget = container;
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    auto *form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(4);

    // ── Codec ──
    m_codecLabel = new QLabel(tr("Codec") + QStringLiteral(":"), this);
    m_codecCombo = new QComboBox(this);
    populateCodecs(m_currentRomType);
    form->addRow(m_codecLabel, m_codecCombo);

    // ── Tile columns ──
    m_colsLabel = new QLabel(tr("Tile columns") + QStringLiteral(":"), this);
    m_colsSpin = new QSpinBox(this);
    m_colsSpin->setRange(1, 128);
    m_colsSpin->setValue(16);
    form->addRow(m_colsLabel, m_colsSpin);

    // ── Palette sections ──
    m_paletteSectionLabel = new QLabel(tr("Palette section") + QStringLiteral(":"), this);
    m_paletteSectionCombo = new PaletteSectionComboBox(this);
    m_paletteSectionCombo->setEnabled(false);
    m_paletteSectionCombo->setMinimumHeight(kPaletteComboCellSize + 10);
    m_paletteSectionCombo->setItemDelegate(new PaletteSectionItemDelegate(m_paletteSectionCombo));
    m_paletteSectionCombo->setPlaceholderText(tr("No palette sections"));
    form->addRow(m_paletteSectionLabel, m_paletteSectionCombo);

    layout->addLayout(form);

    // ── Palette preview (click to edit) ──
    m_palLabel = new QLabel(tr("Palette (click to edit)") + QStringLiteral(":"), this);
    layout->addWidget(m_palLabel);

    m_palPreview = new PalettePreview(this);
    layout->addWidget(m_palPreview);

    layout->addStretch();
    setWidget(container);
    initTitleBar();
    setSectionActive(false);

    // Signals
    connect(m_codecCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!m_hasCustomPalette) {
            updatePalettePreview();
        } else {
            // Clamp/expand existing custom palette to new codec bpp.
            setPaletteColors(m_palPreview->colors());
        }
        emit codecChanged(selectedCodec());
    });

    connect(m_colsSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &GraphicsDockWidget::tileColsChanged);

    connect(m_paletteSectionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (!m_paletteSectionCombo || index < 0)
            return;

        const QVector<QRgb> paletteColors = paletteFromVariant(
            m_paletteSectionCombo->itemData(index, kPaletteSectionColorsRole));
        if (paletteColors.isEmpty())
            return;

        m_hasCustomPalette = true;
        setPaletteColors(paletteColors);
        emit paletteChanged(m_palPreview->colors());
    });

    connect(m_palPreview, &PalettePreview::colorEdited, this, [this](int, QRgb) {
        m_hasCustomPalette = true;
        syncPaletteSectionSelection(m_palPreview->colors());
        emit paletteChanged(m_palPreview->colors());
    });

    connect(m_palPreview, &PalettePreview::leftColorSelected, this,
            &GraphicsDockWidget::leftPalIndexChanged);
    connect(m_palPreview, &PalettePreview::rightColorSelected, this,
            &GraphicsDockWidget::rightPalIndexChanged);

    updatePalettePreview();
}

GraphicsDockWidget::~GraphicsDockWidget() = default;

TileCodec GraphicsDockWidget::selectedCodec() const
{
    return static_cast<TileCodec>(m_codecCombo->currentData().toInt());
}

int GraphicsDockWidget::selectedTileCols() const
{
    return m_colsSpin ? m_colsSpin->value() : 1;
}

void GraphicsDockWidget::setRomType(RomType romType)
{
    m_currentRomType = romType;
    populateCodecs(romType);
}

void GraphicsDockWidget::setSectionActive(bool active)
{
    if (m_sectionActive == active)
        return;
    m_sectionActive = active;
    if (m_contentWidget)
        m_contentWidget->setEnabled(active);
    if (m_palPreview)
        m_palPreview->setDimmed(!active);
}

void GraphicsDockWidget::setCodec(TileCodec codec)
{
    bool changed = false;
    const QSignalBlocker blocker(m_codecCombo);
    const int wanted = static_cast<int>(codec);
    for (int i = 0; i < m_codecCombo->count(); ++i) {
        if (m_codecCombo->itemData(i).toInt() == wanted) {
            changed = (m_codecCombo->currentIndex() != i);
            m_codecCombo->setCurrentIndex(i);
            break;
        }
    }

    if (changed) {
        m_palPreview->setVisibleColorCount(1 << tileCodecBpp(selectedCodec()));
        if (!m_hasCustomPalette)
            updatePalettePreview();
        else
            setPaletteColors(m_palPreview->colors());
    }
}

void GraphicsDockWidget::setTileColsDisplay(int cols)
{
    if (!m_colsSpin)
        return;
    const QSignalBlocker blocker(m_colsSpin);
    m_colsSpin->setValue(qBound(1, cols, 128));
}

void GraphicsDockWidget::setPaletteColors(const QVector<QRgb> &pal)
{
    m_hasCustomPalette = !pal.isEmpty();
    if (pal.isEmpty()) {
        updatePalettePreview();
        return;
    }
    const QVector<QRgb> normalized = normalizePaletteForCodec(pal, selectedCodec());
    m_palPreview->setVisibleColorCount(normalized.size());
    m_palPreview->setPalette(normalized);
    syncPaletteSectionSelection(normalized);
}

QVector<QRgb> GraphicsDockWidget::paletteColors() const
{
    return m_palPreview->colors();
}

void GraphicsDockWidget::setPaletteSections(const QVector<PaletteSectionPreview> &paletteSections)
{
    if (!m_paletteSectionCombo)
        return;

    const QSignalBlocker blocker(m_paletteSectionCombo);
    m_paletteSectionCombo->clear();

    for (const PaletteSectionPreview &section : paletteSections) {
        m_paletteSectionCombo->addItem(section.name);
        const int itemIndex = m_paletteSectionCombo->count() - 1;
        m_paletteSectionCombo->setItemData(itemIndex, section.startOffset, kPaletteSectionStartRole);
        m_paletteSectionCombo->setItemData(itemIndex, paletteVariantList(section.colors), kPaletteSectionColorsRole);
        m_paletteSectionCombo->setItemData(itemIndex,
                                           QStringLiteral("%1 [0x%2]")
                                               .arg(section.name)
                                               .arg(section.startOffset, 0, 16),
                                           Qt::ToolTipRole);
    }

    const bool hasPaletteSections = m_paletteSectionCombo->count() > 0;
    m_paletteSectionCombo->setEnabled(hasPaletteSections);
    m_paletteSectionCombo->setPlaceholderText(hasPaletteSections
        ? tr("Custom palette")
        : tr("No palette sections"));

    if (hasPaletteSections)
        syncPaletteSectionSelection(m_palPreview->colors());
    else
        clearPaletteSectionSelection();
}

int GraphicsDockWidget::selectedLeftPalIndex() const
{
    return m_palPreview->leftIndex();
}

int GraphicsDockWidget::selectedRightPalIndex() const
{
    return m_palPreview->rightIndex();
}

void GraphicsDockWidget::updatePalettePreview()
{
    const QVector<QRgb> pal = defaultPaletteForCodec(selectedCodec());
    m_palPreview->setVisibleColorCount(pal.size());
    m_palPreview->setPalette(pal);
    clearPaletteSectionSelection();
}

QVector<QRgb> GraphicsDockWidget::defaultPaletteForCodec(TileCodec codec) const
{
    const int bpp = tileCodecBpp(codec);
    const int colors = 1 << bpp;
    QVector<QRgb> pal(colors);

    pal[0] = qRgb(0, 0, 0);
    if (bpp == 1) {
        pal[1] = qRgb(255, 255, 255);
    } else if (bpp == 2) {
        pal[1] = qRgb(96, 96, 210);
        pal[2] = qRgb(180, 80, 80);
        pal[3] = qRgb(240, 240, 240);
    } else {
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> dist(60, 255);
        for (int i = 1; i < colors; ++i)
            pal[i] = qRgb(dist(rng), dist(rng), dist(rng));
    }

    return pal;
}

QVector<QRgb> GraphicsDockWidget::normalizePaletteForCodec(const QVector<QRgb> &pal, TileCodec codec) const
{
    const int colors = 1 << tileCodecBpp(codec);
    QVector<QRgb> normalized = pal;
    if (normalized.size() > colors)
        normalized.resize(colors);

    const QVector<QRgb> fallback = defaultPaletteForCodec(codec);
    for (int i = normalized.size(); i < colors && i < fallback.size(); ++i)
        normalized.append(fallback[i]);

    return normalized;
}

void GraphicsDockWidget::syncPaletteSectionSelection(const QVector<QRgb> &colors)
{
    if (!m_paletteSectionCombo || m_paletteSectionCombo->count() <= 0)
        return;

    const QVector<QRgb> normalizedColors = normalizePaletteForCodec(colors, selectedCodec());
    int matchedIndex = -1;
    for (int i = 0; i < m_paletteSectionCombo->count(); ++i) {
        const QVector<QRgb> itemColors = normalizePaletteForCodec(
            paletteFromVariant(m_paletteSectionCombo->itemData(i, kPaletteSectionColorsRole)),
            selectedCodec());
        if (itemColors == normalizedColors) {
            matchedIndex = i;
            break;
        }
    }

    const QSignalBlocker blocker(m_paletteSectionCombo);
    m_paletteSectionCombo->setCurrentIndex(matchedIndex);
    m_paletteSectionCombo->update();
}

void GraphicsDockWidget::clearPaletteSectionSelection()
{
    if (!m_paletteSectionCombo)
        return;

    const QSignalBlocker blocker(m_paletteSectionCombo);
    m_paletteSectionCombo->setCurrentIndex(-1);
    m_paletteSectionCombo->update();
}

void GraphicsDockWidget::populateCodecs(RomType romType)
{
    const TileCodec current = selectedCodec();
    const QVector<TileCodec> preferred = preferredTileCodecsForRom(romType);
    const QVector<TileCodec> all = allTileCodecs();

    const QSignalBlocker blocker(m_codecCombo);
    m_codecCombo->clear();

    auto appendCodec = [this](TileCodec tc) {
        m_codecCombo->addItem(QString::fromLatin1(tileCodecName(tc)), static_cast<int>(tc));
    };

    for (TileCodec tc : preferred)
        appendCodec(tc);

    if (romType != RomType::Unknown && !preferred.isEmpty())
        m_codecCombo->insertSeparator(m_codecCombo->count());

    for (TileCodec tc : all) {
        if (preferred.contains(tc))
            continue;
        appendCodec(tc);
    }

    int bestIndex = 0;
    for (int i = 0; i < m_codecCombo->count(); ++i) {
        if (m_codecCombo->itemData(i).toInt() == static_cast<int>(current)) {
            bestIndex = i;
            break;
        }
    }
    m_codecCombo->setCurrentIndex(bestIndex);
}

void GraphicsDockWidget::retranslateUi()
{
    setWindowTitle(tr("Graphics"));
    m_codecLabel->setText(tr("Codec") + QStringLiteral(":"));
    m_colsLabel->setText(tr("Tile columns") + QStringLiteral(":"));
    m_paletteSectionLabel->setText(tr("Palette section") + QStringLiteral(":"));
    if (m_paletteSectionCombo) {
        m_paletteSectionCombo->setPlaceholderText(m_paletteSectionCombo->count() > 0
            ? tr("Custom palette")
            : tr("No palette sections"));
    }
    m_palLabel->setText(tr("Palette (click to edit)") + QStringLiteral(":"));
}

void GraphicsDockWidget::onPaletteChanged()
{
    // Nothing special needed
}

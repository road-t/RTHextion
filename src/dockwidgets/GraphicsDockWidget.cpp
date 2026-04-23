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
#include <cmath>
#include <random>

namespace {

constexpr int kPaletteCellSize = 24;

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

void PalettePreview::setDimmed(bool dimmed)
{
    if (m_dimmed == dimmed)
        return;
    m_dimmed = dimmed;
    update();
}

QSize PalettePreview::sizeHint() const
{
    const int cols = qMax(1, qMin(m_colors.size(), 8));
    const int rows = qMax(1, (m_colors.size() + cols - 1) / cols);
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
    const int n = m_colors.size();
    if (n <= 0)
        return 1;
    const int maxColsByWidth = qMax(1, availWidth / kPaletteCellSize);
    return qMax(1, qMin(n, maxColsByWidth));
}

void PalettePreview::updateHeightForCurrentWidth()
{
    const int cols = columnCountForWidth(width());
    const int rows = qMax(1, (m_colors.size() + cols - 1) / cols);
    const int h = rows * kPaletteCellSize;
    setMinimumHeight(h);
    setMaximumHeight(h);
}

void PalettePreview::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int n = m_colors.size();
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
    if (m_colors.isEmpty())
        return;

    const int cols = columnCountForWidth(width());
    const int rows = (m_colors.size() + cols - 1) / cols;
    const int cellW = kPaletteCellSize;
    const int cellH = kPaletteCellSize;
    if (event->pos().x() < 0 || event->pos().x() >= cols * cellW
        || event->pos().y() < 0 || event->pos().y() >= rows * cellH)
        return;
    const int col = event->pos().x() / cellW;
    const int row = event->pos().y() / cellH;
    const int idx = row * cols + col;
    if (idx < 0 || idx >= m_colors.size())
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
    if (m_colors.isEmpty() || event->button() != Qt::LeftButton)
        return;

    const int cols = columnCountForWidth(width());
    const int rows = (m_colors.size() + cols - 1) / cols;
    const int cellW = kPaletteCellSize;
    const int cellH = kPaletteCellSize;
    if (event->pos().x() < 0 || event->pos().x() >= cols * cellW
        || event->pos().y() < 0 || event->pos().y() >= rows * cellH)
        return;
    const int col = event->pos().x() / cellW;
    const int row = event->pos().y() / cellH;
    const int idx = row * cols + col;
    if (idx < 0 || idx >= m_colors.size())
        return;

    const QColor initial = QColor::fromRgba(m_colors[idx]);
    const QColor chosen = QColorDialog::getColor(initial, this, tr("Edit Palette Color"));
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
    m_codecLabel = new QLabel(tr("Codec:"), this);
    m_codecCombo = new QComboBox(this);
    populateCodecs(m_currentRomType);
    form->addRow(m_codecLabel, m_codecCombo);

    // ── Tile columns ──
    m_colsLabel = new QLabel(tr("Tile columns:"), this);
    m_colsSpin = new QSpinBox(this);
    m_colsSpin->setRange(1, 128);
    m_colsSpin->setValue(16);
    form->addRow(m_colsLabel, m_colsSpin);

    layout->addLayout(form);

    // ── Palette preview (click to edit) ──
    m_palLabel = new QLabel(tr("Palette (click to edit):"), this);
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

    connect(m_palPreview, &PalettePreview::colorEdited, this, [this](int, QRgb) {
        m_hasCustomPalette = true;
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
    const QSignalBlocker blocker(m_codecCombo);
    const int wanted = static_cast<int>(codec);
    for (int i = 0; i < m_codecCombo->count(); ++i) {
        if (m_codecCombo->itemData(i).toInt() == wanted) {
            m_codecCombo->setCurrentIndex(i);
            return;
        }
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

    const int colors = 1 << tileCodecBpp(selectedCodec());
    QVector<QRgb> normalized = pal;
    if (normalized.size() > colors)
        normalized.resize(colors);

    if (normalized.size() < colors) {
        QVector<QRgb> fallback;
        fallback.resize(colors);
        fallback[0] = qRgb(0, 0, 0);
        if (colors == 2) {
            fallback[1] = qRgb(255, 255, 255);
        } else if (colors == 4) {
            fallback[1] = qRgb(96, 96, 210);
            fallback[2] = qRgb(180, 80, 80);
            fallback[3] = qRgb(240, 240, 240);
        } else {
            std::mt19937 rng(42);
            std::uniform_int_distribution<int> dist(60, 255);
            for (int i = 1; i < colors; ++i)
                fallback[i] = qRgb(dist(rng), dist(rng), dist(rng));
        }

        for (int i = normalized.size(); i < colors; ++i)
            normalized.append(fallback[i]);
    }

    m_palPreview->setPalette(normalized);
}

QVector<QRgb> GraphicsDockWidget::paletteColors() const
{
    return m_palPreview->colors();
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
    const TileCodec codec = selectedCodec();
    const int bpp = tileCodecBpp(codec);
    const int colors = 1 << bpp;
    QVector<QRgb> pal(colors);

    // Replicate the palette generation from HexEditor::initGraphicsPalette
    pal[0] = qRgb(0, 0, 0);
    if (bpp == 1) {
        pal[1] = qRgb(255, 255, 255);
    } else if (bpp == 2) {
        pal[0] = qRgb(0, 0, 0);
        pal[1] = qRgb(96, 96, 210);
        pal[2] = qRgb(180, 80, 80);
        pal[3] = qRgb(240, 240, 240);
    } else {
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> dist(60, 255);
        for (int i = 1; i < colors; ++i)
            pal[i] = qRgb(dist(rng), dist(rng), dist(rng));
    }

    m_palPreview->setPalette(pal);
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
    m_codecLabel->setText(tr("Codec:"));
    m_colsLabel->setText(tr("Tile columns:"));
    m_palLabel->setText(tr("Palette (click to edit):"));
}

void GraphicsDockWidget::onPaletteChanged()
{
    // Nothing special needed
}

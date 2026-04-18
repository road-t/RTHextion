#include "GraphicsDockWidget.h"
#include "DockTitleBar.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QColorDialog>
#include <cmath>
#include <random>

// ═══════════════════════════════════════════════════════════════════════════
// PalettePreview
// ═══════════════════════════════════════════════════════════════════════════

PalettePreview::PalettePreview(QWidget *parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumHeight(40);
    setCursor(Qt::PointingHandCursor);
}

void PalettePreview::setPalette(const QVector<QRgb> &colors)
{
    m_colors = colors;
    update();
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

    const int cols = qMin(n, 16);
    const int rows = (n + cols - 1) / cols;
    const int cellW = width() / cols;
    const int cellH = qMax(2, height() / rows);

    for (int i = 0; i < n; ++i) {
        const int cx = (i % cols) * cellW;
        const int cy = (i / cols) * cellH;
        p.fillRect(cx, cy, cellW, cellH, QColor::fromRgba(m_colors[i]));
    }

    p.setPen(QPen(QColor(255, 255, 255, 60), 1));
    for (int c = 1; c < cols; ++c)
        p.drawLine(c * cellW, 0, c * cellW, height());
    for (int r = 1; r < rows; ++r)
        p.drawLine(0, r * cellH, width(), r * cellH);

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
}

void PalettePreview::mousePressEvent(QMouseEvent *event)
{
    if (m_colors.isEmpty())
        return;

    const int cols = qMin(m_colors.size(), 16);
    const int rows = (m_colors.size() + cols - 1) / cols;
    const int cellW = width() / cols;
    const int cellH = qMax(2, height() / rows);
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

    const int cols = qMin(m_colors.size(), 16);
    const int rows = (m_colors.size() + cols - 1) / cols;
    const int cellW = width() / cols;
    const int cellH = qMax(2, height() / rows);
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
    for (int i = 0; i <= static_cast<int>(TileCodec::Linear8bpp); ++i)
        m_codecCombo->addItem(QString::fromUtf8(tileCodecName(static_cast<TileCodec>(i))), i);
    m_codecCombo->setCurrentIndex(static_cast<int>(TileCodec::Linear2bpp));
    form->addRow(m_codecLabel, m_codecCombo);

    // ── Tile columns (display-only, auto-computed from hex width) ──
    m_colsLabel = new QLabel(tr("Tile columns:"), this);
    m_colsValue = new QLabel(QStringLiteral("—"), this);
    form->addRow(m_colsLabel, m_colsValue);

    layout->addLayout(form);

    // ── Palette preview (click to edit) ──
    m_palLabel = new QLabel(tr("Palette (click to edit):"), this);
    layout->addWidget(m_palLabel);

    m_palPreview = new PalettePreview(this);
    layout->addWidget(m_palPreview);

    layout->addStretch();
    setWidget(container);
    initTitleBar();

    // Signals
    connect(m_codecCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!m_hasCustomPalette)
            updatePalettePreview();
        emit codecChanged(selectedCodec());
    });

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

void GraphicsDockWidget::setCodec(TileCodec codec)
{
    const QSignalBlocker blocker(m_codecCombo);
    m_codecCombo->setCurrentIndex(static_cast<int>(codec));
}

void GraphicsDockWidget::setTileColsDisplay(int cols)
{
    m_colsValue->setText(QString::number(cols));
}

void GraphicsDockWidget::setPaletteColors(const QVector<QRgb> &pal)
{
    m_hasCustomPalette = !pal.isEmpty();
    if (pal.isEmpty()) {
        updatePalettePreview();
    } else {
        m_palPreview->setPalette(pal);
    }
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

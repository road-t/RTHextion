#ifndef GRAPHICSDOCKWIDGET_H
#define GRAPHICSDOCKWIDGET_H

#include "BaseDockWidget.h"
#include "SectionListModel.h"

#include <QComboBox>
#include <QSpinBox>
#include <QLabel>

class QGridLayout;

/// A small widget that draws the current palette as a grid of colored cells.
/// Clicking a cell opens a QColorDialog to edit that color.
class PalettePreview : public QWidget
{
    Q_OBJECT
public:
    explicit PalettePreview(QWidget *parent = nullptr);
    void setPalette(const QVector<QRgb> &colors);
    void setVisibleColorCount(int count);
    void setDimmed(bool dimmed);
    const QVector<QRgb> &colors() const { return m_colors; }

    int leftIndex() const { return m_leftIndex; }
    int rightIndex() const { return m_rightIndex; }

signals:
    void colorEdited(int index, QRgb newColor);
    void leftColorSelected(int index);
    void rightColorSelected(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
private:
    int visibleColorCount() const;
    int columnCountForWidth(int availWidth) const;
    void updateHeightForCurrentWidth();

    QVector<QRgb> m_colors;
    int m_leftIndex  = 0;
    int m_rightIndex = 1;
    bool m_dimmed = false;
    int m_visibleColorCount = 0;
};

class GraphicsDockWidget : public BaseDockWidget
{
    Q_OBJECT

public:
    explicit GraphicsDockWidget(QWidget *parent = nullptr);
    ~GraphicsDockWidget() override;

    /// Rebuild codec list with platform-preferred options on top.
    void setRomType(RomType romType);

    /// Enable controls only when cursor is in a graphics section.
    void setSectionActive(bool active);

    TileCodec selectedCodec() const;
    int selectedTileCols() const;

    void setCodec(TileCodec codec);
    void setTileColsDisplay(int cols);

    /// Set the palette shown in the preview (and used for rendering).
    void setPaletteColors(const QVector<QRgb> &pal);
    QVector<QRgb> paletteColors() const;

    int selectedLeftPalIndex() const;
    int selectedRightPalIndex() const;

    /// Refresh the palette preview for the currently selected codec (default palette).
    void updatePalettePreview();

    void retranslateUi() override;

signals:
    void codecChanged(TileCodec codec);
    void tileColsChanged(int cols);
    void paletteChanged(const QVector<QRgb> &palette);
    void leftPalIndexChanged(int index);
    void rightPalIndexChanged(int index);

protected:
    void onPaletteChanged() override;

private:
    void populateCodecs(RomType romType);

    QComboBox      *m_codecCombo   = nullptr;
    QSpinBox       *m_colsSpin     = nullptr;
    PalettePreview *m_palPreview   = nullptr;
    QLabel         *m_codecLabel   = nullptr;
    QLabel         *m_colsLabel    = nullptr;
    QLabel         *m_palLabel     = nullptr;
    bool            m_hasCustomPalette = false;
    RomType         m_currentRomType = RomType::Unknown;
    bool            m_sectionActive = false;
};

#endif // GRAPHICSDOCKWIDGET_H

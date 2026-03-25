#pragma once

#include <QWidget>
#include <QToolButton>
#include <QPainter>
#include <QFontMetrics>
#include <QShowEvent>
#include <QResizeEvent>
#include <QStyle>
#include <QStyleOption>
#include <QMainWindow>
#include <QDockWidget>
#include <QCloseEvent>

/// A custom title bar widget for QDockWidget that draws its label vertically
/// when the dock is placed in the left or right dock area.
class DockTitleBar : public QWidget
{
    static constexpr int kThick = 30;  ///< Bar thickness (height in H-mode, width in V-mode)
    static constexpr int kBtnSz = 16;  ///< Collapse button size
    static constexpr int kPad   = 3;   ///< Padding around elements

public:
    explicit DockTitleBar(const QString &title, QWidget *parent = nullptr)
        : QWidget(parent), m_title(title)
    {
        m_font = font();
        m_font.setPointSizeF(m_font.pointSizeF() * 0.8);

        m_collapseBtn = new QToolButton(this);
        m_collapseBtn->setAutoRaise(true);
        m_collapseBtn->setFixedSize(kBtnSz, kBtnSz);
        m_collapseBtn->setToolTip(tr("Collapse / Expand"));

        m_floatBtn = new QToolButton(this);
        m_floatBtn->setAutoRaise(true);
        m_floatBtn->setFixedSize(kBtnSz, kBtnSz);
        m_floatBtn->setToolTip(tr("Detach / Attach"));

        m_closeBtn = new QToolButton(this);
        m_closeBtn->setAutoRaise(true);
        m_closeBtn->setFixedSize(kBtnSz, kBtnSz);
        m_closeBtn->setToolTip(tr("Close"));

        connect(m_collapseBtn, &QToolButton::clicked, this, [this] {
            setCollapsed(!m_collapsed);
        });

        connect(m_floatBtn, &QToolButton::clicked, this, [this] {
            if (auto *dw = qobject_cast<QDockWidget *>(parentWidget()))
                dw->setFloating(!dw->isFloating());
        });

        connect(m_closeBtn, &QToolButton::clicked, this, [this] {
            if (auto *dw = qobject_cast<QDockWidget *>(parentWidget()))
                dw->close();
        });

        applyHorizontalConstraints();
        updateButtonIcons();
        updateCollapseGlyph();
    }

    QToolButton *collapseButton() const { return m_collapseBtn; }
    int collapsedExtent(bool sideArea) const
    {
        const QFontMetrics fm(m_font);
        const int buttonsLen = (3 * kBtnSz) + (4 * kPad);
        if (sideArea)
            return kThick;
        return qMax(buttonsLen + 24, fm.height() + 2 * kPad);
    }

    void setTitle(const QString &t)
    {
        if (m_title != t) {
            m_title = t;
            update();
        }
    }

    bool isCollapsed() const { return m_collapsed; }

public slots:
    void onDockLocationChanged(Qt::DockWidgetArea area)
    {
        m_dockArea = area;
        m_sideArea = (area == Qt::LeftDockWidgetArea ||
                      area == Qt::RightDockWidgetArea);
        updateOrientation();
        updateCollapseGlyph();
    }

    void setCollapsed(bool collapsed)
    {
        if (m_collapsed == collapsed)
            return;
        m_collapsed = collapsed;
        updateOrientation();
        updateCollapseGlyph();
        update();
    }

protected:
    void showEvent(QShowEvent *e) override
    {
        QWidget::showEvent(e);
        refreshAreaFromParentDock();
    }

    void resizeEvent(QResizeEvent *e) override
    {
        QWidget::resizeEvent(e);
        refreshAreaFromParentDock();
        positionButton();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        QStyleOption opt;
        opt.initFrom(this);
        style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);

        p.setFont(m_font);
        p.setPen(palette().color(QPalette::WindowText));
        const QFontMetrics fm(m_font);

        if (m_vertical) {
            // Vertical title is used only in collapsed side-dock state.
            // Buttons are at the top; text occupies remaining lower area.
            const int buttons = (3 * kBtnSz) + (4 * kPad);
            const int avail = height() - buttons;
            if (avail > fm.height()) {
                const QString txt = fm.elidedText(m_title, Qt::ElideRight, avail - kPad);
                p.save();
                // Rotate so x-axis maps from top to bottom in original coordinates.
                p.translate(width(), 0);
                p.rotate(90.0);
                p.drawText(QRect(buttons, 0, avail - kPad, width()),
                           Qt::AlignLeft | Qt::AlignVCenter, txt);
                p.restore();
            }
        } else {
            const int buttons = (3 * kBtnSz) + (4 * kPad);
            const int avail = width() - buttons - kPad;
            if (avail > 0) {
                p.drawText(QRect(kPad, 0, avail, height()),
                           Qt::AlignLeft | Qt::AlignVCenter,
                           fm.elidedText(m_title, Qt::ElideRight, avail));
            }
        }
    }

    QSize sizeHint() const override
    {
        const QFontMetrics fm(m_font);
        const int buttonsLen = (3 * kBtnSz) + (4 * kPad);
        if (m_vertical) {
            const int fullTitleLen = fm.horizontalAdvance(m_title) + (2 * kPad);
            return QSize(kThick, qMax(buttonsLen + 24, buttonsLen + fullTitleLen));
        }
        return QSize(qMax(120, fm.horizontalAdvance(m_title) + buttonsLen + 4 * kPad), kThick);
    }

    QSize minimumSizeHint() const override
    {
        const QFontMetrics fm(m_font);
        const int buttonsLen = (3 * kBtnSz) + (4 * kPad);
        if (m_vertical) {
            const int fullTitleLen = fm.horizontalAdvance(m_title) + (2 * kPad);
            return QSize(kThick, qMax(buttonsLen + 24, buttonsLen + fullTitleLen));
        }
        return QSize(qMax(buttonsLen + 30, fm.horizontalAdvance(m_title) + buttonsLen), kThick);
    }

private:
    void applyHorizontalConstraints()
    {
        const int buttonsLen = (3 * kBtnSz) + (4 * kPad);
        setMinimumSize(buttonsLen, kThick);
        setMaximumSize(QWIDGETSIZE_MAX, kThick);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        positionButton();
        update();
    }

    void applyVerticalConstraints()
    {
        const int buttonsLen = (3 * kBtnSz) + (4 * kPad);
        setMinimumSize(kThick, buttonsLen);
        setMaximumSize(kThick, QWIDGETSIZE_MAX);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
        positionButton();
        update();
    }

    void positionButton()
    {
        if (!m_collapseBtn || !m_floatBtn || !m_closeBtn)
            return;

        if (m_vertical) {
            const int x = (width() - kBtnSz) / 2;
            int y = kPad;
            m_closeBtn->move(x, y);
            y += (kBtnSz + kPad);
            m_floatBtn->move(x, y);
            y += (kBtnSz + kPad);
            m_collapseBtn->move(x, y);
        } else {
            const int y = (height() - kBtnSz) / 2;
            int x = width() - kBtnSz - kPad;
            m_closeBtn->move(x, y);
            x -= (kBtnSz + kPad);
            m_floatBtn->move(x, y);
            x -= (kBtnSz + kPad);
            m_collapseBtn->move(x, y);
        }
    }

    void updateButtonIcons()
    {
        m_floatBtn->setIcon(style()->standardIcon(QStyle::SP_TitleBarNormalButton));
        m_closeBtn->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
        m_floatBtn->setIconSize(QSize(12, 12));
        m_closeBtn->setIconSize(QSize(12, 12));
    }

    void updateCollapseGlyph()
    {
        m_collapseBtn->setIcon(QIcon());
        if (m_vertical) {
            if (m_dockArea == Qt::LeftDockWidgetArea)
                m_collapseBtn->setText(m_collapsed ? QStringLiteral(">") : QStringLiteral("<"));
            else
                m_collapseBtn->setText(m_collapsed ? QStringLiteral("<") : QStringLiteral(">"));
        } else {
            if (m_dockArea == Qt::TopDockWidgetArea)
                m_collapseBtn->setText(m_collapsed ? QStringLiteral("V") : QStringLiteral("^"));
            else if (m_dockArea == Qt::BottomDockWidgetArea)
                m_collapseBtn->setText(m_collapsed ? QStringLiteral("^") : QStringLiteral("V"));
            else
                m_collapseBtn->setText(m_collapsed ? QStringLiteral("^") : QStringLiteral("V"));
        }
    }

    void updateOrientation()
    {
        const bool shouldVertical = m_collapsed && m_sideArea;
        if (shouldVertical == m_vertical)
            return;

        m_vertical = shouldVertical;
        if (m_vertical)
            applyVerticalConstraints();
        else
            applyHorizontalConstraints();
        updateCollapseGlyph();
    }

    void refreshAreaFromParentDock()
    {
        if (auto *dw = qobject_cast<QDockWidget *>(parentWidget())) {
            if (auto *mw = qobject_cast<QMainWindow *>(dw->parentWidget())) {
                const Qt::DockWidgetArea area = mw->dockWidgetArea(dw);
                if (area != Qt::NoDockWidgetArea)
                    onDockLocationChanged(area);
            }
        }
    }

    QString      m_title;
    QFont        m_font;
    QToolButton *m_collapseBtn = nullptr;
    QToolButton *m_floatBtn    = nullptr;
    QToolButton *m_closeBtn    = nullptr;
    bool         m_vertical = false;
    bool         m_collapsed = false;
    bool         m_sideArea = false;
    Qt::DockWidgetArea m_dockArea = Qt::NoDockWidgetArea;
};

#include "SectionsDockWidget.h"
#include "SectionListModel.h"
#include "DockTitleBar.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QColorDialog>
#include <QMenu>
#include <QPainter>

SectionsDockWidget::SectionsDockWidget(QWidget *parent)
    : BaseDockWidget(tr("Sections"), parent)
{
    setWindowTitle(tr("Sections"));
    setObjectName(QStringLiteral("SectionsDockWidget"));

    auto *container = new QWidget(this);
    m_contentWidget = container;
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);

    // Toolbar
    auto *toolRow = new QHBoxLayout();
    toolRow->setContentsMargins(0, 0, 0, 0);
    toolRow->setSpacing(4);

    m_showSectionsBtn = new QToolButton(this);
    m_showSectionsBtn->setCheckable(true);
    m_showSectionsBtn->setChecked(true);
    m_showSectionsBtn->setAutoRaise(true);
    m_showSectionsBtn->setToolTip(tr("Show section colors"));
    m_showSectionsBtn->setIcon(makeEyeIcon(palette().color(QPalette::WindowText)));
    m_showSectionsBtn->setIconSize(QSize(16, 16));
    toolRow->addWidget(m_showSectionsBtn);

    m_addBtn = new QToolButton(this);
    m_addBtn->setAutoRaise(true);
    m_addBtn->setToolTip(tr("Add section"));
    m_addBtn->setIcon(makeAddIcon(palette().color(QPalette::WindowText)));
    m_addBtn->setIconSize(QSize(16, 16));
    toolRow->addWidget(m_addBtn);

    toolRow->addStretch();
    layout->addLayout(toolRow);

    // Tree widget
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({tr("Name"), tr("Offset")});
    m_tree->setColumnCount(2);
    m_tree->header()->setStretchLastSection(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setRootIsDecorated(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_tree);

    setWidget(container);
    initTitleBar();

    connect(m_showSectionsBtn, &QToolButton::toggled,
            this, &SectionsDockWidget::showSectionsToggled);
    connect(m_addBtn, &QToolButton::clicked,
            this, &SectionsDockWidget::addSectionRequested);
    connect(m_tree, &QTreeWidget::itemClicked,
            this, &SectionsDockWidget::onItemClicked);
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &SectionsDockWidget::onTreeContextMenu);
}

void SectionsDockWidget::setModel(SectionListModel *model)
{
    if (m_model)
        disconnect(m_model, nullptr, this, nullptr);

    m_model = model;

    if (m_model)
        connect(m_model, &SectionListModel::sectionsChanged,
                this, &SectionsDockWidget::rebuildTree);

    rebuildTree();
}

void SectionsDockWidget::setRomTypeName(const QString &name)
{
    if (m_romTypeName != name) {
        m_romTypeName = name;
        rebuildTree();
    }
}

void SectionsDockWidget::refresh()
{
    rebuildTree();
}

void SectionsDockWidget::setShowSectionsChecked(bool checked)
{
    m_showSectionsBtn->setChecked(checked);
}

void SectionsDockWidget::retranslateUi()
{
    setWindowTitle(tr("Sections"));
    m_showSectionsBtn->setToolTip(tr("Show section colors"));
    m_addBtn->setToolTip(tr("Add section"));
    m_tree->setHeaderLabels({tr("Name"), tr("Offset")});
}

void SectionsDockWidget::onPaletteChanged()
{
    const QColor fg = palette().color(QPalette::WindowText);
    m_showSectionsBtn->setIcon(makeEyeIcon(fg));
    m_addBtn->setIcon(makeAddIcon(fg));
    rebuildTree();
}

void SectionsDockWidget::onItemClicked(QTreeWidgetItem *item, int /*column*/)
{
    bool ok = false;
    const qint64 offset = item->data(0, Qt::UserRole).toLongLong(&ok);
    if (ok && offset >= 0)
        emit jumpToOffset(offset);
}

void SectionsDockWidget::onTreeContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_tree->itemAt(pos);
    if (!item || !m_model)
        return;

    const int sectionIdx = item->data(0, Qt::UserRole + 1).toInt();
    if (sectionIdx < 0 || sectionIdx >= m_model->count())
        return;

    QMenu menu(this);
    QAction *renameAct  = menu.addAction(tr("Rename"));
    QAction *recolorAct = menu.addAction(tr("Change color"));
    menu.addSeparator();
    QAction *deleteAct  = menu.addAction(tr("Delete"));

    QAction *chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == renameAct) {
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, tr("Rename section"),
            tr("Name:"), QLineEdit::Normal,
            m_model->at(sectionIdx).name, &ok);
        if (ok && !name.isEmpty())
            m_model->renameSection(sectionIdx, name);
    } else if (chosen == recolorAct) {
        const QColor color = QColorDialog::getColor(
            m_model->at(sectionIdx).color, this, tr("Section color"));
        if (color.isValid())
            m_model->recolorSection(sectionIdx, color);
    } else if (chosen == deleteAct) {
        m_model->removeSection(sectionIdx);
    }
}

void SectionsDockWidget::rebuildTree()
{
    m_tree->clear();

    const QString rootLabel = m_romTypeName.isEmpty()
                                  ? QStringLiteral("ROM")
                                  : QStringLiteral("ROM (%1)").arg(m_romTypeName);

    auto *root = new QTreeWidgetItem(m_tree, {rootLabel});
    root->setData(0, Qt::UserRole, qint64(-1));   // no jump
    root->setData(0, Qt::UserRole + 1, -1);       // no section index

    if (m_model) {
        for (int i = 0; i < m_model->count(); ++i) {
            const auto &s = m_model->at(i);
            const QString offsetStr = QStringLiteral("0x%1")
                .arg(s.startOffset, 0, 16, QLatin1Char('0')).toUpper();
            auto *child = new QTreeWidgetItem(root, {s.name, offsetStr});
            child->setData(0, Qt::UserRole, s.startOffset);
            child->setData(0, Qt::UserRole + 1, i);
            child->setIcon(0, colorSwatchIcon(s.color));
        }
    }

    m_tree->expandAll();
    m_tree->resizeColumnToContents(0);
}

QIcon SectionsDockWidget::colorSwatchIcon(const QColor &color) const
{
    QPixmap pix(16, 16);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRoundedRect(2, 2, 12, 12, 2, 2);
    return QIcon(pix);
}

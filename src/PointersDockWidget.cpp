#include "PointersDockWidget.h"
#include "PointerListModel.h"
#include "qhexedit/qhexedit.h"

#include <QPainter>
#include <QPainterPath>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QItemSelectionModel>

PointersDockWidget::PointersDockWidget(QWidget *parent)
    : QDockWidget(parent)
{
    setWindowTitle(tr("Pointers"));
    setObjectName(QStringLiteral("PointersDockWidget"));
    setFeatures(QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetMovable);

    // Custom title bar with collapse button
    auto *titleBar = new QWidget(this);
    titleBar->setObjectName(QStringLiteral("dockTitleBar"));
    titleBar->setFixedHeight(16);
    auto *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(4, 0, 2, 0);
    titleLayout->setSpacing(1);
    m_titleLabel = new QLabel(tr("Pointers"), titleBar);
    QFont smallFont = m_titleLabel->font();
    smallFont.setPointSizeF(smallFont.pointSizeF() * 0.8);
    m_titleLabel->setFont(smallFont);
    titleLayout->addWidget(m_titleLabel);
    titleLayout->addStretch();
    m_collapseBtn = new QToolButton(titleBar);
    m_collapseBtn->setArrowType(Qt::DownArrow);
    m_collapseBtn->setAutoRaise(true);
    m_collapseBtn->setFixedSize(14, 14);
    m_collapseBtn->setToolTip(tr("Collapse / Expand"));
    titleLayout->addWidget(m_collapseBtn);
    setTitleBarWidget(titleBar);

    auto *container = new QWidget(this);
    m_contentWidget = container;
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);

    // Toolbar
    m_toolbar = new QToolBar(this);
    m_toolbar->setIconSize(QSize(16, 16));

    // Eye (show-pointers) toggle button
    m_showPointersBtn = new QToolButton(this);
    m_showPointersBtn->setCheckable(true);
    m_showPointersBtn->setChecked(true);
    m_showPointersBtn->setAutoRaise(true);
    m_showPointersBtn->setToolTip(tr("Show pointers"));
    {
        auto paint = [](bool filled) -> QPixmap {
            const int sz = 16;
            QPixmap pm(sz, sz);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            const QColor col(70, 70, 70);
            p.setPen(QPen(col, 1.3));
            const float cx = sz * 0.5f, cy = sz * 0.5f;
            const float rx = sz * 0.44f, ry = sz * 0.27f;
            QPainterPath lens;
            lens.moveTo(cx - rx, cy);
            lens.cubicTo(cx - rx*0.4f, cy - ry*2.0f, cx + rx*0.4f, cy - ry*2.0f, cx + rx, cy);
            lens.cubicTo(cx + rx*0.4f, cy + ry*2.0f, cx - rx*0.4f, cy + ry*2.0f, cx - rx, cy);
            p.drawPath(lens);
            const float pr = ry * 0.72f;
            if (filled) { p.setBrush(col); p.setPen(Qt::NoPen); }
            p.drawEllipse(QPointF(cx, cy), pr, pr);
            return pm;
        };
        QIcon icon;
        icon.addPixmap(paint(true),  QIcon::Normal,  QIcon::On);
        icon.addPixmap(paint(false), QIcon::Normal,  QIcon::Off);
        icon.addPixmap(paint(false), QIcon::Disabled, QIcon::Off);
        m_showPointersBtn->setIcon(icon);
    }
    m_showPointersBtn->setIconSize(QSize(16, 16));
    m_toolbar->addWidget(m_showPointersBtn);
    m_toolbar->addSeparator();
    connect(m_showPointersBtn, &QToolButton::clicked, this, [this](bool checked) {
        emit showPointersToggled(checked);
    });

    m_findAct = m_toolbar->addAction(tr("Find pointers"), this,
        [this]{ emit findPointersRequested(); });
    m_findAct->setEnabled(false);
    m_toolbar->addSeparator();
    m_addAct     = m_toolbar->addAction(tr("Add"),       this, &PointersDockWidget::addPointer);
    m_deleteAct  = m_toolbar->addAction(tr("Delete"),    this, &PointersDockWidget::deleteSelectedPointers);
    m_cleanAllAct = m_toolbar->addAction(tr("Clean all"), this, &PointersDockWidget::cleanAllPointers);
    layout->addWidget(m_toolbar);

    // Table view
    m_view = new QTableView(this);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->verticalHeader()->setVisible(false);
    m_view->setSortingEnabled(true);
    m_view->setAlternatingRowColors(true);
    m_view->verticalHeader()->setDefaultSectionSize(22);
    connect(m_view, &QTableView::doubleClicked,
            this, &PointersDockWidget::onDoubleClicked);
    layout->addWidget(m_view);

    setWidget(container);
    updateButtonStates();

    connect(m_collapseBtn, &QToolButton::clicked, this, [this]() {
        const bool visible = m_contentWidget->isVisible();
        m_contentWidget->setVisible(!visible);
        m_collapseBtn->setArrowType(visible ? Qt::RightArrow : Qt::DownArrow);
    });
}

void PointersDockWidget::setHexEdit(QHexEdit *hexEdit)
{
    if (m_hexEdit)
        disconnect(m_hexEdit, &QHexEdit::selectionChanged,
                   this, &PointersDockWidget::onHexSelectionChanged);

    m_hexEdit = hexEdit;
    m_model = hexEdit ? hexEdit->pointers() : nullptr;
    if (m_model)
        m_model->setSectionNames(QStringList() << tr("#") << tr("Pointer") << tr("Offset") << tr("Data"));
    m_view->setModel(m_model);

    // Column widths: narrow row# col, two fixed, last stretches
    m_view->setColumnWidth(0, 30);
    m_view->setColumnWidth(1, 80);
    m_view->setColumnWidth(2, 80);
    if (m_model)
        m_view->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);

    if (m_hexEdit)
        connect(m_hexEdit, &QHexEdit::selectionChanged,
                this, &PointersDockWidget::onHexSelectionChanged);

    if (m_view->selectionModel())
        connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
                this, &PointersDockWidget::onListSelectionChanged);

    if (m_model) {
        connect(m_model, &PointerListModel::pointersChanged,
                this, &PointersDockWidget::updateButtonStates);
        m_view->sortByColumn(1, Qt::AscendingOrder);
    }

    updateButtonStates();
}

void PointersDockWidget::addShowPointersAction(QAction * /*act*/)
{
    // The eye button in the toolbar is the dock-side show-pointers toggle;
    // the QAction stays in the menu bar only.
}

void PointersDockWidget::beginSearch()
{
    m_view->setSortingEnabled(false);
}

void PointersDockWidget::endSearch()
{
    if (m_model && m_model->rowCount() <= 10000)
        m_view->resizeColumnsToContents();
    m_view->setSortingEnabled(true);
    m_view->sortByColumn(0, Qt::AscendingOrder);
    updateButtonStates();
}

void PointersDockWidget::refreshView()
{
    m_view->viewport()->update();
    if (m_model && m_model->rowCount() > 0)
        m_view->resizeColumnsToContents();
    updateButtonStates();
}

void PointersDockWidget::retranslateUi()
{
    setWindowTitle(tr("Pointers"));
    if (m_titleLabel)
        m_titleLabel->setText(tr("Pointers"));
    if (m_collapseBtn)
        m_collapseBtn->setToolTip(tr("Collapse / Expand"));
    m_findAct->setText(tr("Find pointers"));
    m_addAct->setText(tr("Add"));
    m_deleteAct->setText(tr("Delete"));
    m_cleanAllAct->setText(tr("Clean all"));
    if (m_model)
        m_model->setSectionNames(QStringList() << tr("#") << tr("Pointer") << tr("Offset") << tr("Data"));
}

void PointersDockWidget::onHexSelectionChanged(qint64 start, qint64 end)
{
    m_findAct->setEnabled(start < end);
}

void PointersDockWidget::onListSelectionChanged()
{
    updateButtonStates();
}

void PointersDockWidget::onDoubleClicked(const QModelIndex &index)
{
    if (!m_hexEdit || !m_model) return;
    const qint64 selectedOffset = index.data(PointerListModel::ValueRole).toLongLong();
    m_hexEdit->setCursorPosition(selectedOffset * 2);
    m_hexEdit->ensureVisible();
}

void PointersDockWidget::addPointer()
{
    if (!m_hexEdit) return;

    bool ok = false;
    const QString pointerText = QInputDialog::getText(this, tr("Add pointer"),
        tr("Pointer offset (hex/dec, e.g. 0x1234):"), QLineEdit::Normal, QString(), &ok);
    if (!ok || pointerText.isEmpty()) return;

    const qint64 pointerOffset = pointerText.toLongLong(&ok, 0);
    if (!ok || pointerOffset < 0) {
        QMessageBox::warning(this, QString(), tr("Invalid pointer offset."));
        return;
    }

    const QString valueText = QInputDialog::getText(this, tr("Add pointer"),
        tr("Pointer value (hex/dec, e.g. 0x5678):"), QLineEdit::Normal, QString(), &ok);
    if (!ok || valueText.isEmpty()) return;

    const qint64 pointedOffset = valueText.toLongLong(&ok, 0);
    if (!ok || pointedOffset < 0) {
        QMessageBox::warning(this, QString(), tr("Invalid pointer value."));
        return;
    }

    m_hexEdit->addPointerUndoable(pointerOffset, pointedOffset, 4);
    updateButtonStates();
}

void PointersDockWidget::deleteSelectedPointers()
{
    if (!m_hexEdit || !m_view->selectionModel()) return;

    const auto selectedRows = m_view->selectionModel()->selectedRows();
    if (selectedRows.isEmpty()) return;

    QMessageBox msg(QMessageBox::Warning, nullptr,
        tr("Are you sure want to delete pointer from list?"),
        QMessageBox::Yes | QMessageBox::No, this);
    if (msg.exec() != QMessageBox::Yes) return;

    QVector<qint64> pointersToDelete;
    pointersToDelete.reserve(selectedRows.size());
    for (const QModelIndex &rowIndex : selectedRows)
        pointersToDelete.append(rowIndex.data(PointerListModel::KeyRole).toLongLong());

    m_hexEdit->removePointersUndoable(pointersToDelete);
    updateButtonStates();
}

void PointersDockWidget::cleanAllPointers()
{
    if (!m_hexEdit) return;

    QMessageBox msg(QMessageBox::Warning, nullptr,
        tr("Are you sure want to clear pointers list?"),
        QMessageBox::Yes | QMessageBox::No, this);
    if (msg.exec() != QMessageBox::Yes) return;

    m_hexEdit->clearPointers();
    m_view->reset();
    updateButtonStates();
}

void PointersDockWidget::updateButtonStates()
{
    const bool hasModel    = m_model != nullptr;
    const bool hasPointers = hasModel && !m_model->empty();
    const bool hasSel      = m_view->selectionModel() &&
                             !m_view->selectionModel()->selectedRows().isEmpty();

    m_addAct->setEnabled(hasModel);
    m_deleteAct->setEnabled(hasSel);
    m_cleanAllAct->setEnabled(hasPointers);

    if (m_hexEdit)
        m_findAct->setEnabled(m_hexEdit->hasSelection());
    else
        m_findAct->setEnabled(false);

    if (m_titleLabel) {
        const int count = hasModel ? m_model->rowCount() : 0;
        const QString title = tr("Pointers") +
            (count > 0 ? QStringLiteral(" \u2013 %1").arg(count) : QString());
        m_titleLabel->setText(title);
    }
}

void PointersDockWidget::setShowPointersChecked(bool checked)
{
    if (m_showPointersBtn)
        m_showPointersBtn->setChecked(checked);
}

void PointersDockWidget::setShowPointersEnabled(bool enabled)
{
    if (m_showPointersBtn)
        m_showPointersBtn->setEnabled(enabled);
}

QByteArray PointersDockWidget::saveColumnsState() const
{
    return m_view->horizontalHeader()->saveState();
}

void PointersDockWidget::restoreColumnsState(const QByteArray &state)
{
    if (!state.isEmpty())
        m_view->horizontalHeader()->restoreState(state);
}

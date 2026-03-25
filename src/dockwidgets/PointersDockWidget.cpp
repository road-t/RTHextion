#include "PointersDockWidget.h"
#include "PointerListModel.h"
#include "hexeditor/hexeditor.h"
#include "DockTitleBar.h"

#include <QPainter>
#include <QPainterPath>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QItemSelectionModel>
#include <QMainWindow>
#include <QMenu>
#include <QKeyEvent>

PointersDockWidget::PointersDockWidget(QWidget *parent)
    : QDockWidget(parent)
{
    setWindowTitle(tr("Pointers"));
    setObjectName(QStringLiteral("PointersDockWidget"));
    setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetMovable);

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
    m_cleanAllAct = m_toolbar->addAction(tr("Drop all"), this, &PointersDockWidget::cleanAllPointers);
    layout->addWidget(m_toolbar);

    // Table view
    m_view = new QTableView(this);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->verticalHeader()->setVisible(false);
    m_view->setSortingEnabled(true);
    m_view->setAlternatingRowColors(true);
    m_view->verticalHeader()->setDefaultSectionSize(22);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->installEventFilter(this);
    connect(m_view, &QTableView::doubleClicked,
            this, &PointersDockWidget::onDoubleClicked);
    connect(m_view, &QTableView::customContextMenuRequested,
            this, &PointersDockWidget::showContextMenu);
    layout->addWidget(m_view);

    setWidget(container);
    updateButtonStates();
}

void PointersDockWidget::setHexEdit(HexEditor *hexEdit)
{
    if (m_hexEdit)
        disconnect(m_hexEdit, &HexEditor::selectionChanged,
                   this, &PointersDockWidget::onHexSelectionChanged);

    if (m_model)
        disconnect(m_model, &PointerListModel::pointersChanged,
                   this, &PointersDockWidget::updateButtonStates);

    if (m_view && m_view->selectionModel())
        disconnect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
                   this, &PointersDockWidget::onListSelectionChanged);

    m_hexEdit = hexEdit;
    m_model = m_hexEdit ? m_hexEdit->pointers() : nullptr;
    if (m_model)
        m_model->setSectionNames(QStringList() << tr("#") << tr("Offset") << tr("Pointer") << tr("Data"));
    m_view->setModel(m_model);

    // Column widths: narrow row# col, two fixed, last stretches
    m_view->setColumnWidth(0, 30);
    m_view->setColumnWidth(1, 80);
    m_view->setColumnWidth(2, 80);
    if (m_model)
        m_view->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);

    if (m_hexEdit)
        connect(m_hexEdit, &HexEditor::selectionChanged,
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

void PointersDockWidget::setCollapsed(bool collapsed)
{
    if (m_collapsed == collapsed)
        return;
    m_collapsed = collapsed;

    Qt::DockWidgetArea area = Qt::NoDockWidgetArea;
    QMainWindow *mw = qobject_cast<QMainWindow *>(parentWidget());
    if (mw)
        area = mw->dockWidgetArea(this);
    const bool sideArea = (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea);

    int collapsedExtent = 30;
    if (auto *titleBar = static_cast<DockTitleBar *>(titleBarWidget())) {
        collapsedExtent = titleBar->collapsedExtent(sideArea);
    }

    if (collapsed) {
        if (sideArea) {
            m_savedExpandedWidth = width();
            setMinimumWidth(collapsedExtent);
            setMaximumWidth(collapsedExtent);
            if (mw)
                mw->resizeDocks({this}, {collapsedExtent}, Qt::Horizontal);
            else
                resize(collapsedExtent, height());
        } else {
            m_savedExpandedHeight = height();
            setMinimumHeight(collapsedExtent);
            setMaximumHeight(collapsedExtent);
            if (mw)
                mw->resizeDocks({this}, {collapsedExtent}, Qt::Vertical);
            else
                resize(width(), collapsedExtent);
        }
    } else {
        if (sideArea) {
            setMinimumWidth(0);
            setMaximumWidth(QWIDGETSIZE_MAX);
            const int target = m_savedExpandedWidth > 0 ? m_savedExpandedWidth : 320;
            if (mw)
                mw->resizeDocks({this}, {target}, Qt::Horizontal);
            else
                resize(target, height());
        } else {
            setMinimumHeight(0);
            setMaximumHeight(QWIDGETSIZE_MAX);
            const int target = m_savedExpandedHeight > 0 ? m_savedExpandedHeight : 220;
            if (mw)
                mw->resizeDocks({this}, {target}, Qt::Vertical);
            else
                resize(width(), target);
        }
    }

    if (m_contentWidget)
        m_contentWidget->setVisible(!collapsed);
    if (auto *titleBar = static_cast<DockTitleBar *>(titleBarWidget()))
        titleBar->setCollapsed(collapsed);
}

bool PointersDockWidget::isCollapsed() const
{
    return m_contentWidget && !m_contentWidget->isVisible();
}

void PointersDockWidget::retranslateUi()
{
    setWindowTitle(tr("Pointers"));
    m_findAct->setText(tr("Find pointers"));
    m_addAct->setText(tr("Add"));
    m_deleteAct->setText(tr("Delete"));
    m_cleanAllAct->setText(tr("Drop all"));
    if (m_model)
        m_model->setSectionNames(QStringList() << tr("#") << tr("Offset") << tr("Pointer") << tr("Data"));
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

    // Column 1 (Offset): go to pointer offset
    if (index.column() == 1) {
        const qint64 pointerOffset = index.data(PointerListModel::KeyRole).toLongLong();
        m_hexEdit->setCursorPosition(pointerOffset * 2);
        m_hexEdit->ensureVisible();
    }
    // Column 2 (Pointer): go to pointer target
    else if (index.column() == 2) {
        const qint64 targetOffset = index.data(PointerListModel::ValueRole).toLongLong();
        m_hexEdit->setCursorPosition(targetOffset * 2);
        m_hexEdit->ensureVisible();
    }
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

    {
        const int count = hasModel ? m_model->rowCount() : 0;
        const QString title = tr("Pointers") +
            (count > 0 ? QStringLiteral(" \u2013 %1").arg(count) : QString());
        setWindowTitle(title);
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

bool PointersDockWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_view && event->type() == QEvent::KeyPress) {
        QKeyEvent *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Delete) {
            deleteSelectedPointers();
            return true;
        }
    }
    return QDockWidget::eventFilter(watched, event);
}

void PointersDockWidget::showContextMenu(const QPoint &pos)
{
    if (!m_hexEdit || !m_model) return;

    const QModelIndex index = m_view->indexAt(pos);
    const bool hasSelection = m_view->selectionModel() &&
                              !m_view->selectionModel()->selectedRows().isEmpty();

    QMenu menu(this);
    QAction *jumpToPointerAct = menu.addAction(tr("Go to pointer"));
    QAction *jumpToDataAct    = menu.addAction(tr("Go to data"));
    menu.addSeparator();
    QAction *removeAct        = menu.addAction(tr("Remove pointer"));

    const bool singleValid = index.isValid();
    jumpToPointerAct->setEnabled(singleValid);
    jumpToDataAct->setEnabled(singleValid);
    removeAct->setEnabled(hasSelection);

    QAction *chosen = menu.exec(m_view->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == jumpToPointerAct && singleValid) {
        const qint64 ptrOffset = index.data(PointerListModel::KeyRole).toLongLong();
        m_hexEdit->setCursorPosition(ptrOffset * 2);
        m_hexEdit->ensureVisible();
    } else if (chosen == jumpToDataAct && singleValid) {
        const qint64 targetOffset = index.data(PointerListModel::ValueRole).toLongLong();
        m_hexEdit->setCursorPosition(targetOffset * 2);
        m_hexEdit->ensureVisible();
    } else if (chosen == removeAct && hasSelection) {
        deleteSelectedPointers();
    }
}

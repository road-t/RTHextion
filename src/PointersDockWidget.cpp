#include "PointersDockWidget.h"
#include "PointerListModel.h"
#include "qhexedit/qhexedit.h"

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
        m_model->setSectionNames(QStringList() << tr("Offset") << tr("Pointer") << tr("Data"));
    m_view->setModel(m_model);

    // Column widths: first two 2×, last stretches
    m_view->setColumnWidth(0, 156);
    m_view->setColumnWidth(1, 156);
    if (m_model)
        m_view->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);

    if (m_hexEdit)
        connect(m_hexEdit, &QHexEdit::selectionChanged,
                this, &PointersDockWidget::onHexSelectionChanged);

    if (m_view->selectionModel())
        connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
                this, &PointersDockWidget::onListSelectionChanged);

    if (m_model) {
        connect(m_model, &PointerListModel::pointersChanged,
                this, &PointersDockWidget::updateButtonStates);
        m_view->sortByColumn(0, Qt::AscendingOrder);
    }

    updateButtonStates();
}

void PointersDockWidget::addShowPointersAction(QAction *act)
{
    m_toolbar->addSeparator();
    m_toolbar->addAction(act);
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
        m_model->setSectionNames(QStringList() << tr("Offset") << tr("Pointer") << tr("Data"));
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
}

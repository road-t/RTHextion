#include "PointersDockWidget.h"
#include "PointerListModel.h"
#include "hexeditor/hexeditor.h"
#include "DockTitleBar.h"
#include "BaseDockWidget.h"

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
    : BaseDockWidget(tr("Pointers"), parent)
{
    setWindowTitle(tr("Pointers"));
    setObjectName(QStringLiteral("PointersDockWidget"));

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
        m_showPointersBtn->setIcon(makeEyeIcon(palette().color(QPalette::WindowText)));
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

    initTitleBar();

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
        m_model->setSectionNames(QStringList() << tr("#") << tr("Offset") << tr("Pointer") << tr("Name") << tr("Data"));
    m_view->setModel(m_model);

    // Column widths: narrow row# col, two fixed, last stretches
    m_view->setColumnWidth(0, 30);
    m_view->setColumnWidth(1, 80);
    m_view->setColumnWidth(2, 80);
    m_view->setColumnWidth(3, 140);
    if (m_model)
        m_view->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);

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
    
    if (!m_suppressResize && m_model && m_model->rowCount() > 0)
        m_view->resizeColumnsToContents();
    updateButtonStates();
}

void PointersDockWidget::retranslateUi()
{
    setWindowTitle(tr("Pointers"));
    if (m_showPointersBtn)
        m_showPointersBtn->setToolTip(tr("Show pointers"));
    m_findAct->setText(tr("Find pointers"));
    m_addAct->setText(tr("Add"));
    m_deleteAct->setText(tr("Delete"));
    m_cleanAllAct->setText(tr("Drop all"));
    if (m_model)
        m_model->setSectionNames(QStringList() << tr("#") << tr("Offset") << tr("Pointer") << tr("Name") << tr("Data"));
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

void PointersDockWidget::onPaletteChanged()
{
    if (m_showPointersBtn)
        m_showPointersBtn->setIcon(makeEyeIcon(palette().color(QPalette::WindowText)));
}

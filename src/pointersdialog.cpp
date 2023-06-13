#include "QtWidgets/qpushbutton.h"
#include <QMessageBox>
#include <QKeyEvent>

#include "pointersdialog.h"
#include "ui_pointersdialog.h"
#include "PointerListModel.h"

PointersDialog::PointersDialog(QHexEdit *hexEdit, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::PointersDialog),
    _hexEdit(hexEdit)
{
    ui->setupUi(this);

    setWindowFlags(Qt::CustomizeWindowHint | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint | Qt::Window);

    plModel = _hexEdit->pointers();

    connect(plModel, SIGNAL(dataChanged()), SLOT(update()));

    plModel->setSectionNames(QStringList() << tr("Offset") << tr("Value") << tr("Data"));

    ui->tvPointers->setModel(plModel);
    ui->tvPointers->setSelectionBehavior(QAbstractItemView::SelectRows);

    ui->bbControls->button(QDialogButtonBox::Ok)->setText("Find");
}

PointersDialog::~PointersDialog()
{
    delete ui;
}

void PointersDialog::showEvent(QShowEvent *ev)
{
    Q_UNUSED(ev);

    ui->bbControls->button(QDialogButtonBox::Reset)->setEnabled(!_hexEdit->pointers()->empty());
    //ui->bbControls->button(QDialogButtonBox::Ok)->setEnabled(_hexEdit->hasSelection());

    ui->tvPointers->setSortingEnabled(false);

    auto _tb = _hexEdit->getTranslationTable();

    // if table was added/changed/deleted
    if (_tb != tb)
    {
        tb = _tb;
        ui->cbRangeStart->clear();
        ui->cbRangeEnd->clear();

        // populate range comboboxes with translation table values...
        if (tb)
        {
            size_t i = 0;
            tb->reset();

            while (i++ < tb->size())
            {
                auto el = tb->next();

                ui->cbRangeStart->addItem(el.second, el.first);
                ui->cbRangeEnd->addItem(el.second, el.first);
                ui->cbStopChar->addItem(el.second, el.first);
            }

            ui->cbRangeStart->setCurrentIndex(0);
            ui->cbRangeEnd->setCurrentIndex(i - 1);
            ui->cbStopChar->setCurrentIndex(0);

            ui->cbStopChar->setDisabled(false);
            ui->lbStopChar->setDisabled(false);
        }
        else // ...or with standard ASCII printable characters
        {
            for (uint8_t c = 0x20; c < 0x80; c++)
            {
                ui->cbRangeStart->addItem(QChar(c), c);
                ui->cbRangeEnd->addItem(QChar(c), c);
            }

            ui->cbRangeStart->setCurrentIndex(0);
            ui->cbRangeEnd->setCurrentIndex(0x5E);

            ui->cbStopChar->setDisabled(true);
            ui->lbStopChar->setDisabled(true);
        }
    }

    if (plModel->rowCount())
        ui->tvPointers->resizeColumnsToContents();

    ui->tvPointers->setSortingEnabled(true);
}

void PointersDialog::on_bbControls_accepted()
{
    bool before = ui->rbBefore->isChecked();
    bool after = ui->rbAfter->isChecked();

    if (ui->rbBoth->isChecked())
    {
        before = true;
        after = true;
    }

    QElapsedTimer timer;

    timer.start();

    hide();

    char *start = nullptr, *end = nullptr;

    if (ui->cbOptimize->isChecked())
    {
        auto startChar = ui->cbRangeStart->currentData().toChar().toLatin1();
        auto endChar = ui->cbRangeEnd->currentData().toChar().toLatin1();

        start = &startChar;
        end = &endChar;
    }

    auto stopChar = ui->cbStopChar->isEnabled() ? ui->cbStopChar->currentData().toChar().toLatin1() : 0;

    auto found = _hexEdit->findPointers(ui->rbBE->isChecked(), before, after, start, end, stopChar);

    if (found)
    {
        ui->bbControls->button(QDialogButtonBox::Reset)->setEnabled(true);
    }

    auto msg = QMessageBox(QMessageBox::Information, nullptr, tr("Pointers found: %1\nTime elapsed %2ms").arg(found).arg(timer.elapsed()), QMessageBox::Ok, this);
    msg.exec();

    ui->tvPointers->resizeColumnsToContents();

    show();
}

void PointersDialog::on_bbControls_rejected()
{
    //_hexEdit->resetSelection();
    emit accepted();
    close();
}

void PointersDialog::on_bbControls_clicked(QAbstractButton *button)
{
    if (ui->bbControls->buttonRole(button) == QDialogButtonBox::ButtonRole::ResetRole)
    {
        auto msg = QMessageBox(QMessageBox::Warning, nullptr, tr("Are you sure want to clear pointers list?"), QMessageBox::Yes | QMessageBox::No, this);
        auto result = msg.exec();

        if (result == QMessageBox::Yes)
        {
            _hexEdit->clearPointers();
            ui->tvPointers->reset();
            ui->bbControls->button(QDialogButtonBox::Reset)->setEnabled(false);
        }
    }
}

void PointersDialog::on_cbOptimize_stateChanged(int arg1)
{
    bool disabled = !arg1;

    ui->cbRangeStart->setDisabled(disabled);
    ui->lbMinus->setDisabled(disabled);
    ui->cbRangeEnd->setDisabled(disabled);
    ui->lbFirstChar->setDisabled(disabled);

    // enable only when translation table is o
    auto tb = _hexEdit->getTranslationTable();

    ui->cbStopChar->setDisabled(disabled || !tb);
    ui->lbStopChar->setDisabled(disabled || !tb);
}


void PointersDialog::on_cbRangeStart_currentIndexChanged(int index)
{
    if (index > ui->cbRangeEnd->currentIndex())
        ui->cbRangeEnd->setCurrentIndex(index);
}


void PointersDialog::on_cbRangeEnd_currentIndexChanged(int index)
{
    if (index < ui->cbRangeStart->currentIndex())
        ui->cbRangeStart->setCurrentIndex(index);
}


void PointersDialog::on_tvPointers_doubleClicked(const QModelIndex &index)
{
    auto newindex = index.sibling(index.row(), qMin(index.column(), 1)); // click at found string = click at it's offset

    auto selectedOffset = ui->tvPointers->model()->itemData(newindex).value(0).toString().toUInt(nullptr, 16);

    _hexEdit->setCursorPosition(selectedOffset * 2);
    _hexEdit->ensureVisible();
}

void PointersDialog::keyPressEvent(QKeyEvent *event)
{
    if (ui->tvPointers->hasFocus())
    {
        if (event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete)
        {
            auto msg = QMessageBox(QMessageBox::Warning, nullptr, tr("Are you sure want to delete pointer from list?"), QMessageBox::Yes | QMessageBox::No, this);
            auto result = msg.exec();

            if (result == QMessageBox::Yes)
            {
                auto index = ui->tvPointers->currentIndex();

                auto newindex = index.sibling(index.row(), 0); // click at found string = click at it's offset

                auto pointer = ui->tvPointers->model()->itemData(newindex).value(0).toString().toLongLong(nullptr, 16);

                _hexEdit->pointers()->dropPointer(pointer);
            }
        }
    }
}

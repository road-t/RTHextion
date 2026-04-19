#include <QApplication>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QTableWidget>
#include <QTest>
#include <QTimer>
#include <QWidget>

#include "../../src/dockwidgets/TablesDockWidget.h"

namespace {

void autoAcceptNextMessageBox()
{
    QTimer::singleShot(0, []() {
        for (QWidget *w : QApplication::topLevelWidgets()) {
            if (auto *mb = qobject_cast<QMessageBox *>(w)) {
                mb->accept();
                break;
            }
        }
    });
}

QTableWidget *gridForDock(TablesDockWidget &dock)
{
    return dock.findChild<QTableWidget *>();
}

int firstDataRow(QTableWidget *grid)
{
    if (!grid)
        return -1;
    for (int r = 0; r < grid->rowCount(); ++r) {
        auto *valueItem = grid->item(r, 1);
        if (!valueItem)
            continue;
        if (!valueItem->data(Qt::UserRole).toBool())
            return r;
    }
    return -1;
}

int placeholderRow(QTableWidget *grid)
{
    if (!grid)
        return -1;
    for (int r = 0; r < grid->rowCount(); ++r) {
        auto *valueItem = grid->item(r, 1);
        if (valueItem && valueItem->data(Qt::UserRole).toBool())
            return r;
    }
    return -1;
}

} // namespace

class TstTablesDockWidget : public QObject
{
    Q_OBJECT

private slots:
    void newRowRequiresValidHexBeforeValueEdit();
    void escCancelsDraftRow();
    void invalidExistingHexDoesNotApply();
    void enterInValueEditorCommitsDraft();
    void focusOutCancelsEmptyDraft();
    void focusOutCommitsNonEmptyDraft();
};

void TstTablesDockWidget::newRowRequiresValidHexBeforeValueEdit()
{
    QMainWindow mw;
    TablesDockWidget dock(&mw);
    mw.addDockWidget(Qt::RightDockWidgetArea, &dock);
    dock.addTable(QStringLiteral("T"));

    QTableWidget *grid = gridForDock(dock);
    QVERIFY(grid);

    const int initialRows = grid->rowCount();
    const int phRow = placeholderRow(grid);
    QVERIFY(phRow >= 0);

    // Trying to edit Value in a fresh row must warn and return focus to HEX.
    autoAcceptNextMessageBox();
    grid->setCurrentCell(phRow, 1);
    QTest::mouseClick(grid->viewport(), Qt::LeftButton,
                      Qt::NoModifier, grid->visualItemRect(grid->item(phRow, 1)).center());

    QVERIFY(grid->rowCount() == initialRows + 1);
    const int draftRow = firstDataRow(grid);
    QVERIFY(draftRow >= 0);
    QCOMPARE(grid->currentRow(), draftRow);
    QCOMPARE(grid->currentColumn(), 0);

    auto *hexItem = grid->item(draftRow, 0);
    auto *valItem = grid->item(draftRow, 1);
    QVERIFY(hexItem);
    QVERIFY(valItem);

    hexItem->setText(QStringLiteral("41"));
    grid->setCurrentCell(draftRow, 0);
    QTest::keyClick(grid, Qt::Key_Return);

    QVERIFY(valItem->flags() & Qt::ItemIsEditable);
    QCOMPARE(grid->currentRow(), draftRow);
    QCOMPARE(grid->currentColumn(), 1);

    valItem->setText(QStringLiteral("A"));
    grid->setCurrentCell(draftRow, 1);
    QTest::keyClick(grid, Qt::Key_Return);

    QCOMPARE(dock.currentTable()->decodeToBytes(QStringLiteral("A")), QByteArray(1, char(0x41)));
}

void TstTablesDockWidget::escCancelsDraftRow()
{
    QMainWindow mw;
    TablesDockWidget dock(&mw);
    mw.addDockWidget(Qt::RightDockWidgetArea, &dock);
    dock.addTable(QStringLiteral("T"));

    QTableWidget *grid = gridForDock(dock);
    QVERIFY(grid);

    const int initialRows = grid->rowCount();
    const int phRow = placeholderRow(grid);
    QVERIFY(phRow >= 0);

    grid->setCurrentCell(phRow, 0);
    QTest::mouseClick(grid->viewport(), Qt::LeftButton,
                      Qt::NoModifier, grid->visualItemRect(grid->item(phRow, 0)).center());

    QVERIFY(grid->rowCount() == initialRows + 1);

    QTest::keyClick(grid, Qt::Key_Escape);

    QCOMPARE(grid->rowCount(), initialRows);
    QCOMPARE(dock.currentTable()->size(), 0);
}

void TstTablesDockWidget::invalidExistingHexDoesNotApply()
{
    QMainWindow mw;
    TablesDockWidget dock(&mw);
    mw.addDockWidget(Qt::RightDockWidgetArea, &dock);

    TranslationTable table;
    table.setItem(0x41, QStringLiteral("A"));
    dock.addTable(QStringLiteral("T"), &table);

    QTableWidget *grid = gridForDock(dock);
    QVERIFY(grid);

    const int row = firstDataRow(grid);
    QVERIFY(row >= 0);

    auto *hexItem = grid->item(row, 0);
    QVERIFY(hexItem);

    autoAcceptNextMessageBox();
    hexItem->setText(QStringLiteral("4"));

    // Invalid HEX must not be applied into the translation model.
    QCOMPARE(dock.currentTable()->decodeToBytes(QStringLiteral("A")), QByteArray(1, char(0x41)));
}

void TstTablesDockWidget::enterInValueEditorCommitsDraft()
{
    QMainWindow mw;
    TablesDockWidget dock(&mw);
    mw.addDockWidget(Qt::RightDockWidgetArea, &dock);
    dock.addTable(QStringLiteral("T"));

    QTableWidget *grid = gridForDock(dock);
    QVERIFY(grid);

    const int phRow = placeholderRow(grid);
    QVERIFY(phRow >= 0);

    grid->setCurrentCell(phRow, 0);
    QTest::mouseClick(grid->viewport(), Qt::LeftButton,
                      Qt::NoModifier, grid->visualItemRect(grid->item(phRow, 0)).center());

    const int draftRow = firstDataRow(grid);
    QVERIFY(draftRow >= 0);

    auto *hexItem = grid->item(draftRow, 0);
    auto *valItem = grid->item(draftRow, 1);
    QVERIFY(hexItem);
    QVERIFY(valItem);

    hexItem->setText(QStringLiteral("41"));
    grid->setCurrentCell(draftRow, 0);
    QTest::keyClick(grid, Qt::Key_Return);

    grid->setCurrentCell(draftRow, 1);
    grid->editItem(valItem);
    QTest::qWait(0);

    auto *editor = qobject_cast<QLineEdit *>(QApplication::focusWidget());
    if (editor) {
        editor->setText(QStringLiteral("A!@#"));
        QTest::keyClick(editor, Qt::Key_Return);
    } else {
        valItem->setText(QStringLiteral("A!@#"));
        grid->setCurrentCell(draftRow, 1);
        QTest::keyClick(grid, Qt::Key_Return);
    }

    QTRY_COMPARE(dock.currentTable()->decodeToBytes(QStringLiteral("A!@#")), QByteArray(1, char(0x41)));
}

void TstTablesDockWidget::focusOutCancelsEmptyDraft()
{
    QMainWindow mw;
    TablesDockWidget dock(&mw);
    mw.addDockWidget(Qt::RightDockWidgetArea, &dock);
    dock.addTable(QStringLiteral("T"));

    QTableWidget *grid = gridForDock(dock);
    QVERIFY(grid);

    const int initialRows = grid->rowCount();
    const int phRow = placeholderRow(grid);
    QVERIFY(phRow >= 0);

    grid->setCurrentCell(phRow, 0);
    QTest::mouseClick(grid->viewport(), Qt::LeftButton,
                      Qt::NoModifier, grid->visualItemRect(grid->item(phRow, 0)).center());

    const int draftRow = firstDataRow(grid);
    QVERIFY(draftRow >= 0);
    auto *hexItem = grid->item(draftRow, 0);
    auto *valItem = grid->item(draftRow, 1);
    QVERIFY(hexItem);
    QVERIFY(valItem);

    hexItem->setText(QStringLiteral("41"));
    grid->setCurrentCell(draftRow, 0);
    QTest::keyClick(grid, Qt::Key_Return);

    grid->setCurrentCell(draftRow, 1);
    grid->editItem(valItem);
    QTest::qWait(0);

    QWidget outside;
    outside.show();
    outside.activateWindow();
    outside.setFocus();

    QTRY_COMPARE(dock.currentTable()->size(), 0);
    QTRY_COMPARE(grid->rowCount(), initialRows);
}

void TstTablesDockWidget::focusOutCommitsNonEmptyDraft()
{
    QMainWindow mw;
    TablesDockWidget dock(&mw);
    mw.addDockWidget(Qt::RightDockWidgetArea, &dock);
    dock.addTable(QStringLiteral("T"));

    QTableWidget *grid = gridForDock(dock);
    QVERIFY(grid);

    const int phRow = placeholderRow(grid);
    QVERIFY(phRow >= 0);

    grid->setCurrentCell(phRow, 0);
    QTest::mouseClick(grid->viewport(), Qt::LeftButton,
                      Qt::NoModifier, grid->visualItemRect(grid->item(phRow, 0)).center());

    const int draftRow = firstDataRow(grid);
    QVERIFY(draftRow >= 0);
    auto *hexItem = grid->item(draftRow, 0);
    auto *valItem = grid->item(draftRow, 1);
    QVERIFY(hexItem);
    QVERIFY(valItem);

    hexItem->setText(QStringLiteral("41"));
    grid->setCurrentCell(draftRow, 0);
    QTest::keyClick(grid, Qt::Key_Return);

    grid->setCurrentCell(draftRow, 1);
    grid->editItem(valItem);
    QTest::qWait(0);

    auto *editor = qobject_cast<QLineEdit *>(QApplication::focusWidget());
    if (editor)
        editor->setText(QStringLiteral("FOCUS"));
    else
        valItem->setText(QStringLiteral("FOCUS"));

    QWidget outside;
    outside.show();
    outside.activateWindow();
    outside.setFocus();

    QTRY_COMPARE(dock.currentTable()->decodeToBytes(QStringLiteral("FOCUS")), QByteArray(1, char(0x41)));
}

QTEST_MAIN(TstTablesDockWidget)
#include "tst_tablesdockwidget.moc"

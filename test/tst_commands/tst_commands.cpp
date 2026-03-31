#include <QTest>
#include <QBuffer>
#include <QSignalSpy>
#include <memory>

#include "../../src/hexeditor/commands.h"

class TstCommands : public QObject
{
    Q_OBJECT

private:
    struct Env {
        QByteArray raw;
        QBuffer buf;
        Chunks *chunks;
        UndoStack *stack;
    };

    std::unique_ptr<Env> makeEnv(const QByteArray &data)
    {
        auto e = std::make_unique<Env>();
        e->raw = data;
        e->buf.setData(e->raw);
        e->chunks = new Chunks(this);
        e->chunks->setIODevice(e->buf);
        e->stack = new UndoStack(e->chunks, this);
        return e;
    }

private slots:

    // ---- Insert ----

    void insertSingleChar()
    {
        auto e = makeEnv(QByteArray(8, 'A'));
        e->stack->insert(0, 'Z');
        QCOMPARE((*e->chunks)[0], 'Z');
        QCOMPARE(e->chunks->size(), 9);
    }

    void insertByteArray()
    {
        auto e = makeEnv(QByteArray(8, 'A'));
        e->stack->insert(4, QByteArray("BCD"));
        QCOMPARE(e->chunks->size(), 11);
        QCOMPARE((*e->chunks)[4], 'B');
        QCOMPARE((*e->chunks)[5], 'C');
        QCOMPARE((*e->chunks)[6], 'D');
    }

    // ---- Overwrite ----

    void overwriteSingleChar()
    {
        auto e = makeEnv(QByteArray(8, 'A'));
        e->stack->overwrite(3, 'X');
        QCOMPARE((*e->chunks)[3], 'X');
        QCOMPARE(e->chunks->size(), 8);
    }

    void overwriteByteArray()
    {
        auto e = makeEnv(QByteArray(8, 'A'));
        e->stack->overwrite(2, 3, QByteArray("XYZ"));
        QCOMPARE((*e->chunks)[2], 'X');
        QCOMPARE((*e->chunks)[3], 'Y');
        QCOMPARE((*e->chunks)[4], 'Z');
        QCOMPARE(e->chunks->size(), 8);
    }

    // ---- Remove ----

    void removeSingleChar()
    {
        QByteArray orig(8, '\0');
        for (int i = 0; i < 8; ++i) orig[i] = char('A' + i);
        auto e = makeEnv(orig);
        e->stack->removeAt(0);
        QCOMPARE(e->chunks->size(), 7);
        QCOMPARE((*e->chunks)[0], 'B');
    }

    void removeMultipleChars()
    {
        QByteArray orig(8, '\0');
        for (int i = 0; i < 8; ++i) orig[i] = char('A' + i);
        auto e = makeEnv(orig);
        e->stack->removeAt(2, 3);  // remove C, D, E
        QCOMPARE(e->chunks->size(), 5);
        QCOMPARE((*e->chunks)[0], 'A');
        QCOMPARE((*e->chunks)[1], 'B');
        QCOMPARE((*e->chunks)[2], 'F');
    }

    // ---- Undo / Redo ----

    void undoInsert()
    {
        auto e = makeEnv(QByteArray(8, 'A'));
        e->stack->insert(0, 'Z');
        QCOMPARE(e->chunks->size(), 9);
        e->stack->undo();
        QCOMPARE(e->chunks->size(), 8);
        QCOMPARE((*e->chunks)[0], 'A');
    }

    void undoOverwrite()
    {
        auto e = makeEnv(QByteArray(8, 'A'));
        e->stack->overwrite(3, 'X');
        QCOMPARE((*e->chunks)[3], 'X');
        e->stack->undo();
        QCOMPARE((*e->chunks)[3], 'A');
    }

    void undoRemove()
    {
        QByteArray orig(8, '\0');
        for (int i = 0; i < 8; ++i) orig[i] = char('A' + i);
        auto e = makeEnv(orig);
        e->stack->removeAt(0);
        QCOMPARE((*e->chunks)[0], 'B');
        e->stack->undo();
        QCOMPARE((*e->chunks)[0], 'A');
        QCOMPARE(e->chunks->size(), 8);
    }

    void redoAfterUndo()
    {
        auto e = makeEnv(QByteArray(8, 'A'));
        e->stack->overwrite(0, 'Z');
        e->stack->undo();
        QCOMPARE((*e->chunks)[0], 'A');
        e->stack->redo();
        QCOMPARE((*e->chunks)[0], 'Z');
    }

    void multipleUndos()
    {
        auto e = makeEnv(QByteArray(8, 'A'));
        e->stack->overwrite(0, 'X');
        e->stack->overwrite(1, 'Y');
        e->stack->overwrite(2, 'Z');

        e->stack->undo();
        QCOMPARE((*e->chunks)[2], 'A');
        e->stack->undo();
        QCOMPARE((*e->chunks)[1], 'A');
        e->stack->undo();
        QCOMPARE((*e->chunks)[0], 'A');
    }

    void undoByteArrayInsert()
    {
        auto e = makeEnv(QByteArray(4, 'A'));
        e->stack->insert(2, QByteArray("XYZ"));
        QCOMPARE(e->chunks->size(), 7);
        e->stack->undo();  // macro undo - one step undoes the whole insertion
        QCOMPARE(e->chunks->size(), 4);
        QByteArray result = e->chunks->data(0, 4);
        QCOMPARE(result, QByteArray(4, 'A'));
    }

    // ---- canUndo / canRedo ----

    void canUndoRedo()
    {
        auto e = makeEnv(QByteArray(8, 'A'));
        QVERIFY(!e->stack->canUndo());
        QVERIFY(!e->stack->canRedo());
        e->stack->overwrite(0, 'X');
        QVERIFY(e->stack->canUndo());
        QVERIFY(!e->stack->canRedo());
        e->stack->undo();
        QVERIFY(!e->stack->canUndo());
        QVERIFY(e->stack->canRedo());
    }

    // ---- Boundary conditions ----

    void insertBeyondSizeIsIgnored()
    {
        auto e = makeEnv(QByteArray(8, 'A'));
        // Insert at size+1 should be silently ignored
        e->stack->insert(100, 'Z');
        QCOMPARE(e->chunks->size(), 8);
    }

    void overwriteBeyondSizeIsIgnored()
    {
        auto e = makeEnv(QByteArray(8, 'A'));
        e->stack->overwrite(100, 'Z');
        QCOMPARE(e->chunks->size(), 8);
    }

    void removeBeyondSizeIsIgnored()
    {
        auto e = makeEnv(QByteArray(8, 'A'));
        e->stack->removeAt(100);
        QCOMPARE(e->chunks->size(), 8);
    }
};

QTEST_MAIN(TstCommands)
#include "tst_commands.moc"

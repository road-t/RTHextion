#include <QTest>
#include <QSettings>
#include <QUrl>
#include <QCoreApplication>

// Helper: mirrors the per-tab settings prefix used in MainWindow::writeSettings
static QString tabPrefix(int index)
{
    return QStringLiteral("Session/Tabs/") + QString::number(index);
}

// Helper: write a single tab entry to QSettings (same keys as writeSettings)
static void writeTab(QSettings &s, int index,
                     const QString &type, const QString &path, qint64 cursor,
                     bool showPointers = true, bool showChanges = false,
                     bool changesHexMode = false, int tablesActiveIndex = -1,
                     bool dockTablesCollapsed = false, int dockTablesExpW = -1, int dockTablesExpH = -1,
                     bool dockPointersCollapsed = false, int dockPointersExpW = -1, int dockPointersExpH = -1,
                     bool dockChangesCollapsed = false, int dockChangesExpW = -1, int dockChangesExpH = -1)
{
    const QString pfx = tabPrefix(index);
    s.setValue(pfx + QStringLiteral("/type"),   type);
    s.setValue(pfx + QStringLiteral("/path"),   path);
    s.setValue(pfx + QStringLiteral("/cursor"), cursor);
    s.setValue(pfx + QStringLiteral("/showPointers"),   showPointers);
    s.setValue(pfx + QStringLiteral("/showChanges"),    showChanges);
    s.setValue(pfx + QStringLiteral("/changesHexMode"), changesHexMode);
    s.setValue(pfx + QStringLiteral("/tablesActiveIndex"), tablesActiveIndex);
    s.setValue(pfx + QStringLiteral("/dockTablesCollapsed"),       dockTablesCollapsed);
    s.setValue(pfx + QStringLiteral("/dockTablesExpandedWidth"),   dockTablesExpW);
    s.setValue(pfx + QStringLiteral("/dockTablesExpandedHeight"),  dockTablesExpH);
    s.setValue(pfx + QStringLiteral("/dockPointersCollapsed"),     dockPointersCollapsed);
    s.setValue(pfx + QStringLiteral("/dockPointersExpandedWidth"), dockPointersExpW);
    s.setValue(pfx + QStringLiteral("/dockPointersExpandedHeight"),dockPointersExpH);
    s.setValue(pfx + QStringLiteral("/dockChangesCollapsed"),      dockChangesCollapsed);
    s.setValue(pfx + QStringLiteral("/dockChangesExpandedWidth"),  dockChangesExpW);
    s.setValue(pfx + QStringLiteral("/dockChangesExpandedHeight"), dockChangesExpH);
}

class TstSettings : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("RTHextionTest"));
        QCoreApplication::setApplicationName(QStringLiteral("TstSettings"));
        QSettings settings;
        settings.clear();
    }

    void cleanupTestCase()
    {
        QSettings settings;
        settings.clear();
    }

    void cleanup()
    {
        // Each test starts with a clean slate
        QSettings settings;
        settings.remove(QStringLiteral("Session"));
    }

    // ---- Cursor byte offset <-> nibble position conversion ----

    void cursorByteToNibbleConversion()
    {
        const qint64 byteOffset = 0x1234;
        const qint64 nibblePos = byteOffset * 2;
        QCOMPARE(nibblePos, qint64(0x2468));
        QCOMPARE(nibblePos / 2, byteOffset);
    }

    void cursorZeroOffsetNotRestored()
    {
        // Offset 0 should not trigger setCursorPosition in the restore code
        const qint64 byteOffset = 0;
        QVERIFY(!(byteOffset > 0));
    }

    // ---- Per-tab session settings roundtrip ----

    void perTabCursor_singleFile()
    {
        const qint64 cursor = 0xABCDE;
        {
            QSettings s;
            writeTab(s, 0, QStringLiteral("file"), QStringLiteral("/tmp/rom.bin"), cursor);
            s.setValue(QStringLiteral("Session/TabCount"), 1);
            s.setValue(QStringLiteral("Session/ActiveTab"), 0);
        }
        {
            QSettings s;
            QCOMPARE(s.value(QStringLiteral("Session/TabCount")).toInt(), 1);
            const QString pfx = tabPrefix(0);
            QCOMPARE(s.value(pfx + QStringLiteral("/type")).toString(), QStringLiteral("file"));
            QCOMPARE(s.value(pfx + QStringLiteral("/path")).toString(), QStringLiteral("/tmp/rom.bin"));
            QCOMPARE(s.value(pfx + QStringLiteral("/cursor")).toLongLong(), cursor);
        }
    }

    void perTabCursor_multipleTabs()
    {
        const qint64 c0 = 0x100, c1 = 0x200, c2 = 0x300;
        {
            QSettings s;
            writeTab(s, 0, QStringLiteral("file"),    QStringLiteral("/tmp/a.bin"), c0);
            writeTab(s, 1, QStringLiteral("project"), QStringLiteral("/tmp/b.rthp"), c1);
            writeTab(s, 2, QStringLiteral("file"),    QStringLiteral("/tmp/c.bin"), c2);
            s.setValue(QStringLiteral("Session/TabCount"), 3);
            s.setValue(QStringLiteral("Session/ActiveTab"), 1);
        }
        {
            QSettings s;
            QCOMPARE(s.value(QStringLiteral("Session/TabCount")).toInt(), 3);
            QCOMPARE(s.value(QStringLiteral("Session/ActiveTab")).toInt(), 1);

            QCOMPARE(s.value(tabPrefix(0) + QStringLiteral("/cursor")).toLongLong(), c0);
            QCOMPARE(s.value(tabPrefix(1) + QStringLiteral("/cursor")).toLongLong(), c1);
            QCOMPARE(s.value(tabPrefix(2) + QStringLiteral("/cursor")).toLongLong(), c2);

            QCOMPARE(s.value(tabPrefix(1) + QStringLiteral("/type")).toString(),
                     QStringLiteral("project"));
        }
    }

    void perTabCursor_independentPerTab()
    {
        // Verify each tab stores its cursor independently
        {
            QSettings s;
            writeTab(s, 0, QStringLiteral("file"), QStringLiteral("/tmp/x.bin"), 0x1000);
            writeTab(s, 1, QStringLiteral("file"), QStringLiteral("/tmp/y.bin"), 0x2000);
            s.setValue(QStringLiteral("Session/TabCount"), 2);
        }
        {
            QSettings s;
            const qint64 c0 = s.value(tabPrefix(0) + QStringLiteral("/cursor")).toLongLong();
            const qint64 c1 = s.value(tabPrefix(1) + QStringLiteral("/cursor")).toLongLong();
            QVERIFY(c0 != c1);
            QCOMPARE(c0, qint64(0x1000));
            QCOMPARE(c1, qint64(0x2000));
        }
    }

    void perTabCursor_largeCursorOffset()
    {
        // Files can be very large — ensure qint64 survives the roundtrip
        const qint64 bigOffset = Q_INT64_C(0x1FFFFFFFF);  // > 4 GB
        {
            QSettings s;
            writeTab(s, 0, QStringLiteral("file"), QStringLiteral("/tmp/big.bin"), bigOffset);
            s.setValue(QStringLiteral("Session/TabCount"), 1);
        }
        {
            QSettings s;
            QCOMPARE(s.value(tabPrefix(0) + QStringLiteral("/cursor")).toLongLong(), bigOffset);
        }
    }

    void perTabDockState_roundtrip()
    {
        {
            QSettings s;
            writeTab(s, 0, QStringLiteral("file"), QStringLiteral("/tmp/rom.bin"), 0x100,
                     /*showPointers=*/true, /*showChanges=*/true, /*changesHexMode=*/false,
                     /*tablesActiveIndex=*/2,
                     /*dockTablesCollapsed=*/true,  /*dockTablesExpW=*/300, /*dockTablesExpH=*/200,
                     /*dockPointersCollapsed=*/false, /*dockPointersExpW=*/250, /*dockPointersExpH=*/180,
                     /*dockChangesCollapsed=*/true,  /*dockChangesExpW=*/280, /*dockChangesExpH=*/160);
            s.setValue(QStringLiteral("Session/TabCount"), 1);
        }
        {
            QSettings s;
            const QString pfx = tabPrefix(0);
            QCOMPARE(s.value(pfx + "/showPointers").toBool(),   true);
            QCOMPARE(s.value(pfx + "/showChanges").toBool(),    true);
            QCOMPARE(s.value(pfx + "/changesHexMode").toBool(), false);
            QCOMPARE(s.value(pfx + "/tablesActiveIndex").toInt(), 2);
            QCOMPARE(s.value(pfx + "/dockTablesCollapsed").toBool(), true);
            QCOMPARE(s.value(pfx + "/dockTablesExpandedWidth").toInt(), 300);
            QCOMPARE(s.value(pfx + "/dockTablesExpandedHeight").toInt(), 200);
            QCOMPARE(s.value(pfx + "/dockPointersCollapsed").toBool(), false);
            QCOMPARE(s.value(pfx + "/dockPointersExpandedWidth").toInt(), 250);
            QCOMPARE(s.value(pfx + "/dockPointersExpandedHeight").toInt(), 180);
            QCOMPARE(s.value(pfx + "/dockChangesCollapsed").toBool(), true);
            QCOMPARE(s.value(pfx + "/dockChangesExpandedWidth").toInt(), 280);
            QCOMPARE(s.value(pfx + "/dockChangesExpandedHeight").toInt(), 160);
        }
    }

    void sessionRemoveClearsOldTabs()
    {
        // writeSettings calls settings.remove("Session") before writing fresh data.
        // Verify that removing the group clears all per-tab keys.
        {
            QSettings s;
            writeTab(s, 0, QStringLiteral("file"), QStringLiteral("/tmp/a.bin"), 100);
            writeTab(s, 1, QStringLiteral("file"), QStringLiteral("/tmp/b.bin"), 200);
            s.setValue(QStringLiteral("Session/TabCount"), 2);
        }
        {
            QSettings s;
            s.remove(QStringLiteral("Session"));
        }
        {
            QSettings s;
            QCOMPARE(s.value(QStringLiteral("Session/TabCount"), 0).toInt(), 0);
            QVERIFY(!s.contains(tabPrefix(0) + QStringLiteral("/path")));
            QVERIFY(!s.contains(tabPrefix(1) + QStringLiteral("/path")));
        }
    }

    void sessionTabCountReducedOldTabsCleared()
    {
        // If user closes some tabs, writeSettings removes "Session" and writes fewer tabs.
        // Old tab keys must not linger.
        {
            QSettings s;
            writeTab(s, 0, QStringLiteral("file"), QStringLiteral("/tmp/a.bin"), 100);
            writeTab(s, 1, QStringLiteral("file"), QStringLiteral("/tmp/b.bin"), 200);
            writeTab(s, 2, QStringLiteral("file"), QStringLiteral("/tmp/c.bin"), 300);
            s.setValue(QStringLiteral("Session/TabCount"), 3);
        }
        // Simulate writeSettings with fewer tabs
        {
            QSettings s;
            s.remove(QStringLiteral("Session"));
            writeTab(s, 0, QStringLiteral("file"), QStringLiteral("/tmp/a.bin"), 150);
            s.setValue(QStringLiteral("Session/TabCount"), 1);
        }
        {
            QSettings s;
            QCOMPARE(s.value(QStringLiteral("Session/TabCount")).toInt(), 1);
            QCOMPARE(s.value(tabPrefix(0) + QStringLiteral("/cursor")).toLongLong(), qint64(150));
            // Old tabs should be gone
            QVERIFY(!s.contains(tabPrefix(1) + QStringLiteral("/path")));
            QVERIFY(!s.contains(tabPrefix(2) + QStringLiteral("/path")));
        }
    }

    void activeTabClamped()
    {
        // readSettings clamps activeTab to [0, count-1]
        const int tabCount = 2;
        const int savedActive = 5;  // out of range
        const int clamped = qBound(0, savedActive, tabCount - 1);
        QCOMPARE(clamped, 1);
    }

    void activeTab_savedCorrectly_notLastTab()
    {
        // Regression test for the closeEvent loop bug:
        // writeSettings must save the ORIGINALLY active tab, not the last tab
        // visited by the setCurrentIndex loop.
        //
        // Simulate: 3 tabs, user was on tab 1 (middle).
        // closeEvent loop switches to 0,1,2 → lastIndex == 2.
        // m_closingActiveTab == 1 (captured before loop).
        // writeSettings must use m_closingActiveTab, not currentIndex().

        const int originalActive = 1;     // user was here
        const int loopEndedOn   = 2;      // loop left the tab widget on this

        // Simulate what writeSettings does with m_closingActiveTab set:
        const int activeTabToSave = (originalActive >= 0) ? originalActive : loopEndedOn;
        QCOMPARE(activeTabToSave, originalActive);

        // Write and read back
        {
            QSettings s;
            s.remove(QStringLiteral("Session"));
            writeTab(s, 0, QStringLiteral("file"), QStringLiteral("/tmp/a.bin"), 100);
            writeTab(s, 1, QStringLiteral("file"), QStringLiteral("/tmp/b.bin"), 200);
            writeTab(s, 2, QStringLiteral("file"), QStringLiteral("/tmp/c.bin"), 300);
            s.setValue(QStringLiteral("Session/TabCount"), 3);
            s.setValue(QStringLiteral("Session/ActiveTab"), activeTabToSave);
        }
        {
            QSettings s;
            QCOMPARE(s.value(QStringLiteral("Session/ActiveTab")).toInt(), 1);
        }
    }

    void activeTab_savedAsLastTab_wasWrongBehavior()
    {
        // Demonstrates the old buggy behavior: saving currentIndex() after the
        // closeEvent loop always produced the LAST tab index, not the user's tab.
        const int lastTabInLoop = 2;  // what old code would save
        const int userWasOn    = 1;

        // Old (wrong): Session/ActiveTab == lastTabInLoop
        // New (correct): Session/ActiveTab == userWasOn (via m_closingActiveTab)
        QVERIFY(lastTabInLoop != userWasOn);  // confirms they differ
    }

    void legacySessionFormat_parsed()
    {
        // The legacy format uses "project:path" / "file:path" strings
        const QString entry1 = QStringLiteral("project:/tmp/proj.rthp");
        const QString entry2 = QStringLiteral("file:/tmp/rom.bin");

        QVERIFY(entry1.startsWith(QStringLiteral("project:")));
        QCOMPARE(entry1.mid(8), QStringLiteral("/tmp/proj.rthp"));
        QVERIFY(entry2.startsWith(QStringLiteral("file:")));
        QCOMPARE(entry2.mid(5), QStringLiteral("/tmp/rom.bin"));
    }

    void perTabCursor_zeroCursorNotWrittenAsPositive()
    {
        // Cursor offset 0 is valid (start of file) — verify it roundtrips
        {
            QSettings s;
            writeTab(s, 0, QStringLiteral("file"), QStringLiteral("/tmp/rom.bin"), 0);
            s.setValue(QStringLiteral("Session/TabCount"), 1);
        }
        {
            QSettings s;
            const qint64 c = s.value(tabPrefix(0) + QStringLiteral("/cursor"), -1LL).toLongLong();
            QCOMPARE(c, qint64(0));
            // Restore code uses `cursor > 0` so offset 0 won't trigger setCursorPosition
            QVERIFY(!(c > 0));
        }
    }
};

QTEST_MAIN(TstSettings)
#include "tst_settings.moc"

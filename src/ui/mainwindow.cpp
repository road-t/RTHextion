#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QClipboard>
#include <QStatusBar>
#include <QLabel>
#include <QPixmap>
#include <QAction>
#include <QActionGroup>
#include <QMenuBar>
#include <QMenu>
#include <QToolBar>
#include <QColorDialog>
#include <QFontDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGridLayout>
#include "appsettings.h"
#include <QScrollBar>
#include <QUrl>
#include <QDesktopServices>
#include <QShortcut>
#include <QSplitter>
#include <QDir>
#include <QComboBox>
#include <QLocale>
#include <QTimer>
#include <QTabWidget>
#include <QTabBar>
#include <QStyleFactory>
#include <QMouseEvent>
#include <QChildEvent>
#include <QFileOpenEvent>
#include <QInputDialog>
#include <QFile>
#include <QPushButton>
#include <QProgressDialog>
#include <QPainter>
#include <QFontMetrics>
#include <QPointer>
#include <QSet>
#include <memory>
#include <functional>
#ifdef Q_OS_MAC
#include "macostheme.h"
#endif
#include "theme.h"
#include <algorithm>

#include "QtWidgets/qpushbutton.h"
#include "appinfo.h"
#include "FillWithDialog.h"
#include "VirtualFormatDialog.h"
#include "langtranslator.h"
#include "mainwindow.h"
#include "DockTitleBar.h"
#include "TablesDockWidget.h"
#include "ChangesDockWidget.h"
#include "SectionsDockWidget.h"
#include "SectionListModel.h"
#include "Datas.h"
#include "disassembler.h"
#include "palettedetector.h"
#include "romdetect.h"
#include "romchecksum.h"
#include "encodingdetect.h"
#include "updatechecker.h"
#include "audioplayer.h"

#include "mainwindow/internal.h"
using namespace MainWindowInternal;

namespace
{
    void mergeRunsAt(QVector<QPair<qint64, QByteArray>> &runs, int idx)
    {
        if (idx < 0 || idx >= runs.size())
            return;

        if (idx > 0) {
            const qint64 prevEnd = runs[idx - 1].first + runs[idx - 1].second.size();
            if (prevEnd == runs[idx].first) {
                runs[idx - 1].second.append(runs[idx].second);
                runs.removeAt(idx);
                idx -= 1;
            }
        }

        if (idx >= 0 && idx + 1 < runs.size()) {
            const qint64 curEnd = runs[idx].first + runs[idx].second.size();
            if (curEnd == runs[idx + 1].first) {
                runs[idx].second.append(runs[idx + 1].second);
                runs.removeAt(idx + 1);
            }
        }
    }

    void removeByteFromRun(QVector<QPair<qint64, QByteArray>> &runs, int runIdx, int byteIdx)
    {
        if (runIdx < 0 || runIdx >= runs.size())
            return;
        QByteArray &bytes = runs[runIdx].second;
        if (byteIdx < 0 || byteIdx >= bytes.size())
            return;

        if (bytes.size() == 1) {
            runs.removeAt(runIdx);
            return;
        }

        if (byteIdx == 0) {
            bytes.remove(0, 1);
            runs[runIdx].first += 1;
            return;
        }

        if (byteIdx == bytes.size() - 1) {
            bytes.chop(1);
            return;
        }

        const qint64 rightStart = runs[runIdx].first + byteIdx + 1;
        QByteArray right = bytes.mid(byteIdx + 1);
        bytes.truncate(byteIdx);
        runs.insert(runIdx + 1, {rightStart, right});
    }

    // Adjust originalBytes run offsets when bytes are inserted or deleted.
    // `changeOffset` is the position where the size change occurred and
    // `delta` is positive for insertion (new bytes added) or negative for
    // deletion (bytes removed).
    void adjustOriginalBytesForSizeChange(QVector<QPair<qint64, QByteArray>> &runs,
                                          qint64 changeOffset, qint64 delta)
    {
        if (delta == 0 || runs.isEmpty())
            return;

        if (delta > 0) {
            // Insertion: shift entries at/after changeOffset by +delta.
            // An entry that spans the insert point is split: the left part
            // stays in place, the right part is shifted.
            for (int i = runs.size() - 1; i >= 0; --i) {
                auto &entry = runs[i];
                if (entry.first >= changeOffset) {
                    entry.first += delta;
                } else if (entry.first + entry.second.size() > changeOffset) {
                    const int splitPos = static_cast<int>(changeOffset - entry.first);
                    QByteArray rightPart = entry.second.mid(splitPos);
                    entry.second.truncate(splitPos);
                    runs.insert(i + 1, {changeOffset + delta, rightPart});
                    if (entry.second.isEmpty())
                        runs.removeAt(i);
                }
            }
        } else {
            const qint64 absDelta = -delta;
            const qint64 delEnd = changeOffset + absDelta;

            for (int i = runs.size() - 1; i >= 0; --i) {
                auto &entry = runs[i];
                const qint64 entryEnd = entry.first + entry.second.size();

                if (entry.first >= delEnd) {
                    // Entirely after deleted range: shift left.
                    entry.first -= absDelta;
                } else if (entry.first >= changeOffset) {
                    if (entryEnd <= delEnd) {
                        // Entirely inside deleted range: remove.
                        runs.removeAt(i);
                    } else {
                        // Starts inside deleted range, extends past it: trim front.
                        const int trimFront = static_cast<int>(delEnd - entry.first);
                        entry.second.remove(0, trimFront);
                        entry.first = changeOffset;
                    }
                } else if (entryEnd > changeOffset) {
                    if (entryEnd <= delEnd) {
                        // Starts before, ends inside deleted range: trim tail.
                        entry.second.truncate(static_cast<int>(changeOffset - entry.first));
                    } else {
                        // Spans entire deleted range: remove middle.
                        const int keepLeft = static_cast<int>(changeOffset - entry.first);
                        const int removeLen = static_cast<int>(absDelta);
                        entry.second.remove(keepLeft, removeLen);
                    }
                    if (entry.second.isEmpty())
                        runs.removeAt(i);
                }
            }
        }
    }

    void applyIncrementalOriginalByteChange(QVector<QPair<qint64, QByteArray>> &runs,
                                            qint64 offset,
                                            char oldByte,
                                            char newByte)
    {
        int nearestIdx = -1;
        for (int i = 0; i < runs.size(); ++i) {
            if (runs[i].first <= offset)
                nearestIdx = i;
            else
                break;
        }

        if (nearestIdx >= 0) {
            const qint64 start = runs[nearestIdx].first;
            const qint64 rel = offset - start;
            QByteArray &bytes = runs[nearestIdx].second;
            if (rel >= 0 && rel < bytes.size()) {
                const char originalByte = bytes.at(static_cast<int>(rel));
                // Byte returned to its original value: remove it from tracked runs.
                if (newByte == originalByte)
                    removeByteFromRun(runs, nearestIdx, static_cast<int>(rel));
                return;
            }
        }

        // No existing tracked run contains this byte.
        // If value did not effectively change, nothing to track.
        if (newByte == oldByte)
            return;

        const int insertPos = nearestIdx + 1;
        const bool appendToNearest = (nearestIdx >= 0) &&
                                     (runs[nearestIdx].first + runs[nearestIdx].second.size() == offset);
        if (appendToNearest) {
            runs[nearestIdx].second.append(oldByte);
            mergeRunsAt(runs, nearestIdx);
            return;
        }

        runs.insert(insertPos, {offset, QByteArray(1, oldByte)});
        mergeRunsAt(runs, insertPos);
    }

    PaletteStorageFormat palettePreviewFormatForSection(const Section &section, RomType romType)
    {
        const PaletteStorageFormat format = paletteStorageFormatFromMnemonic(
            parseSectionOptions(section.options).value(QStringLiteral("format")));
        if (format != PaletteStorageFormat::Unknown)
            return format;

        const QVector<PaletteStorageFormat> preferred = paletteStorageFormatsForRom(romType);
        return preferred.isEmpty() ? PaletteStorageFormat::Unknown : preferred.first();
    }

    QVector<QRgb> decodedPalettePreviewColors(HexEditor *editor,
                                              SectionListModel *model,
                                              int sectionIndex,
                                              const Section &section,
                                              RomType romType)
    {
        if (editor && model && sectionIndex >= 0) {
            const qint64 fileSize = editor->dataSize();
            const qint64 sectionEnd = model->endOffsetOf(sectionIndex, fileSize);
            const qint64 sectionLength = qMax<qint64>(0, sectionEnd - section.startOffset);
            const PaletteStorageFormat format = palettePreviewFormatForSection(section, romType);
            if (format != PaletteStorageFormat::Unknown && sectionLength > 0) {
                const QByteArray paletteBytes = editor->dataAt(section.startOffset, sectionLength);
                const QVector<QRgb> decoded = decodePaletteColors(paletteBytes, format);
                if (!decoded.isEmpty())
                    return decoded;
            }
        }

        return section.palette;
    }
}

/*****************************************************************************/

/* Public methods */
/*****************************************************************************/
MainWindow::MainWindow()
    : hexEdit(nullptr), optionsDialog(nullptr), searchDialog(nullptr), jumpToDialog(nullptr), pointersDialog(nullptr), semiAutoTableDialog(nullptr), dumpScriptDialog(nullptr), insertScriptDialog(nullptr)
{
    setAcceptDrops( true );
    init();
}

/*****************************************************************************/
/* Protected methods */
/*****************************************************************************/
void MainWindow::closeEvent(QCloseEvent *event)
{
    // Stop any audio playback before closing to avoid callbacks into
    // partially-destroyed objects.
    stopAudioPlayback();

    // Capture the currently active tab BEFORE iterating — the setCurrentIndex
    // loop below changes the tab widget's current index and would otherwise
    // cause writeSettings() to save the wrong active tab.
    m_closingActiveTab = m_tabWidget->currentIndex();

    // Check each session for unsaved changes
    for (int i = 0; i < m_sessions.size(); ++i) {
        m_tabWidget->setCurrentIndex(i);

        // Remember whether the file had unsaved hex edits before prompting.
        const bool wasFileModified = isWindowModified();
        if (!maybeSave()) {
            event->ignore();
            m_closingActiveTab = -1;
            return;
        }
        // If the user clicked "Save", save() also saves the project (via
        // saveProjectImpl), clearing the dirty flag — maybeSaveProject() will
        // be a no-op.  If the user clicked "Discard", honour their intent to
        // close without saving by skipping the project prompt entirely.
        // Only ask about the project when the file itself had no unsaved edits
        // (i.e. maybeSave was a no-op) but the project has separate changes.
        if (!wasFileModified) {
            if (!maybeSaveProject()) {
                event->ignore();
                m_closingActiveTab = -1;
                return;
            }
        }
    }

    m_closing = true;
    writeSettings();
    m_closingActiveTab = -1;
    event->accept();
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);

#ifdef Q_OS_MAC
    if (m_pendingInitialMacDockVisibilitySync) {
        m_pendingInitialMacDockVisibilitySync = false;
        QTimer::singleShot(50, this, [this]() {
            if (!m_audioDock || !m_graphicsDock)
                return;

            bool audioVisible = false;
            bool graphicsVisible = false;

            if (m_currentSession && m_currentSession->dockVisibilityInitialized) {
                audioVisible = m_currentSession->dockAudioVisible;
                graphicsVisible = m_currentSession->dockGraphicsVisible;
            } else if (m_document && !m_document->projectFilePath.isEmpty()) {
                auto &settings = AppSettings::instance();
                const QString pfx = projectUiSettingsPrefix(m_document->projectFilePath);
                audioVisible = settings.value(pfx + QStringLiteral("/dockAudioVisible"), false).toBool();
                graphicsVisible = settings.value(pfx + QStringLiteral("/dockGraphicsVisible"), false).toBool();
            }

            m_audioDock->setVisible(audioVisible);
            m_graphicsDock->setVisible(graphicsVisible);
        });
    }
#endif

    // Apply any cursor scrolls that were deferred because the window wasn't
    // visible (and viewport heights were 0) during readSettings().
    // Use a short delay so the event loop has time to fully process the
    // show/resize/layout cascade before we scroll.  A zero-delay timer
    // sometimes fires before dock-widget resizing has settled.
    const bool hasPending = std::any_of(m_sessions.begin(), m_sessions.end(),
                                        [](EditorSession *s){ return s->scrollPending; });
    if (hasPending) {
        QTimer::singleShot(50, this, [this]() {
            for (EditorSession *s : m_sessions) {
                if (!s->scrollPending || !s->editor || s->curOffset <= 0) {
                    s->scrollPending = false;
                    continue;
                }
                if (s == m_currentSession) {
                    // Active tab's viewport is visible — apply scroll now
                    s->scrollPending = false;
                    s->editor->setCursorPosition(s->curOffset * 2);
                    s->editor->ensureVisibleCentered();
                }
                // Non-active tabs: leave scrollPending = true.
                // restoreSession() will apply the scroll when the user
                // switches to that tab and the viewport has a real size.
            }
        });
    }

    // Automatic update check — once per 24 hours, silent (no dialog if up-to-date)
    if (UpdateChecker::shouldAutoCheck()) {
        QTimer::singleShot(3000, this, [this]() {
            if (!m_updateChecker) {
                m_updateChecker = new UpdateChecker(this);
                connect(m_updateChecker, &UpdateChecker::updateAvailable,
                        this, &MainWindow::onUpdateAvailable);
                connect(m_updateChecker, &UpdateChecker::upToDate,
                        this, &MainWindow::onUpToDate);
                connect(m_updateChecker, &UpdateChecker::checkFailed,
                        this, &MainWindow::onUpdateCheckFailed);
            }
            m_updateChecker->check(/*silent=*/true);
        });
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->accept();
}


void MainWindow::dropEvent(QDropEvent *event)
{
    if (event->mimeData()->hasUrls())
    {
        QList<QUrl> urls = event->mimeData()->urls();
        for (int i = 0; i < urls.size(); ++i) {
            const QString filePath = urls.at(i).toLocalFile();
            if (i == 0 && !m_sessions.isEmpty() && isUntitled && !hexEdit->isModified())
                loadFile(filePath);
            else
                loadFileInNewTab(filePath);
        }
        event->accept();
    }
}

bool MainWindow::event(QEvent *e)
{
    if (e && e->type() == QEvent::FileOpen) {
        auto *foe = static_cast<QFileOpenEvent *>(e);
        const QString filePath = foe ? foe->file() : QString();
        if (!filePath.isEmpty()) {
            if (!m_sessions.isEmpty() && isUntitled && hexEdit && !hexEdit->isModified())
                loadFile(filePath);
            else
                loadFileInNewTab(filePath);
            e->accept();
            return true;
        }
    }

    // Track child widgets added to the main window (including internal separators)
    // and install event filters on potential dock area separators.
    if (e->type() == QEvent::ChildAdded) {
        auto *ce = static_cast<QChildEvent *>(e);
        if (QWidget *w = qobject_cast<QWidget *>(ce->child())) {
            // Skip known widget types — only separators remain
            if (!qobject_cast<QDockWidget *>(w) && !qobject_cast<QToolBar *>(w)
                && !qobject_cast<QMenuBar *>(w) && !qobject_cast<QStatusBar *>(w)
                && w != centralWidget() && w != m_tabWidget) {
                w->installEventFilter(this);
            }
        }
    }
    return QMainWindow::event(e);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == lbEncoding && event && event->type() == QEvent::MouseButtonRelease) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent && mouseEvent->button() == Qt::LeftButton) {
            openEncodingSelectionDialog();
            return true;
        }
    }

    // Double-click on dock area separator → toggle area collapse
    if (event && event->type() == QEvent::MouseButtonDblClick) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            QWidget *w = qobject_cast<QWidget *>(watched);
            if (w && w->parentWidget() == this
                && !qobject_cast<QDockWidget *>(w)
                && !qobject_cast<QToolBar *>(w)
                && !qobject_cast<QMenuBar *>(w)
                && !qobject_cast<QStatusBar *>(w)
                && w != centralWidget() && w != m_tabWidget) {
                Qt::DockWidgetArea area = separatorDockArea(w);
                if (area != Qt::NoDockWidgetArea) {
                    setDockAreaCollapsed(area, !isDockAreaCollapsed(area));
                    return true;
                }
            }
        }
    }

    // Single click on dock area separator when already collapsed → expand
    if (event && event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            QWidget *w = qobject_cast<QWidget *>(watched);
            if (w && w->parentWidget() == this
                && !qobject_cast<QDockWidget *>(w)
                && !qobject_cast<QToolBar *>(w)
                && !qobject_cast<QMenuBar *>(w)
                && !qobject_cast<QStatusBar *>(w)
                && w != centralWidget() && w != m_tabWidget) {
                Qt::DockWidgetArea area = separatorDockArea(w);
                if (area != Qt::NoDockWidgetArea && isDockAreaCollapsed(area)) {
                    setDockAreaCollapsed(area, false);
                    return true;
                }
            }
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

/*****************************************************************************/
/* Private Slots */
/*****************************************************************************/
void MainWindow::about()
{
    QMessageBox aboutBox(this);
    aboutBox.setWindowTitle(tr("About"));
    aboutBox.setIconPixmap(QPixmap(":/images/tj.png").scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    // Build translated body parts separately to avoid huge translation strings
    QString versionLine = QStringLiteral("%1 v%2").arg(AppInfo::Name).arg(AppInfo::Version);
    QString descriptionLine = tr("This is a hex editor for retro game translation/ROM hacking.\nA tribute to Translhextion editor made by Brian 'Januschan' Bennewitz in early 00's.");
    QString copyrightLine = tr("Ilya 'Road Tripper' Annikov © 2021-2026. All rights reserved.");
    QString githubLine = QStringLiteral("GitHub: https://github.com/road-t/RTHextion");

    QString rawText = versionLine + "\n\n" + descriptionLine + "\n\n" + copyrightLine + "\n\n" + githubLine;

    static const QString urlPlain = QStringLiteral("https://github.com/road-t/RTHextion");
    static const QString urlLink = QStringLiteral("<a href='https://github.com/road-t/RTHextion'>https://github.com/road-t/RTHextion</a>");
    QString htmlText = rawText.toHtmlEscaped()
                           .replace(QLatin1Char('\n'), QLatin1String("<br>"))
                           .replace(urlPlain, urlLink);

    aboutBox.setTextFormat(Qt::RichText);
    aboutBox.setText(htmlText);
    aboutBox.setTextInteractionFlags(Qt::TextBrowserInteraction);

    const auto labels = aboutBox.findChildren<QLabel *>();
    for (QLabel *label : labels)
    {
        if (label->text().contains(QStringLiteral("https://github.com/road-t/RTHextion")))
        {
            label->setTextInteractionFlags(Qt::TextBrowserInteraction);
            label->setOpenExternalLinks(true);
            break;
        }
    }

    // Widen dialog ~30% via a horizontal spacer in the internal grid layout
    if (auto *layout = qobject_cast<QGridLayout *>(aboutBox.layout()))
    {
        auto *spacer = new QSpacerItem(560, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
        layout->addItem(spacer, layout->rowCount(), 0, 1, layout->columnCount());
    }

    // Add "Check for Updates" button on the left side of the button box
    auto *updateBtn = aboutBox.addButton(tr("Check for Updates..."), QMessageBox::ActionRole);
    // Move it to the leftmost position by re-inserting it at the front
    if (auto *btnLayout = qobject_cast<QGridLayout *>(aboutBox.layout())) {
        // QMessageBox buttons live in last row; we insert our button row before OK
        btnLayout->addWidget(updateBtn, 0, 0, Qt::AlignLeft);
    }

    aboutBox.exec();

    if (aboutBox.clickedButton() == updateBtn)
        checkForUpdates();
}

/*****************************************************************************/
void MainWindow::checkForUpdates()
{
    if (!m_updateChecker)
    {
        m_updateChecker = new UpdateChecker(this);
        connect(m_updateChecker, &UpdateChecker::updateAvailable,
                this, &MainWindow::onUpdateAvailable);
        connect(m_updateChecker, &UpdateChecker::upToDate,
                this, &MainWindow::onUpToDate);
        connect(m_updateChecker, &UpdateChecker::checkFailed,
                this, &MainWindow::onUpdateCheckFailed);
    }
    statusBar()->showMessage(tr("Checking for updates..."), 3000);
    m_updateChecker->check(/*silent=*/false);
}

void MainWindow::onUpdateAvailable(const QString &version, const QString &url,
                                   const QString &notes)
{
    statusBar()->clearMessage();
    QMessageBox box(this);
    box.setWindowTitle(tr("Update available"));
    box.setIcon(QMessageBox::Information);
    box.setTextFormat(Qt::RichText);
    box.setText(tr("A new version of %1 is available: <b>v%2</b><br>"
                   "You are running <b>v%3</b>.")
                    .arg(AppInfo::Name, version,
                         QString::fromLatin1(AppInfo::Version)));
    if (!notes.isEmpty())
    {
        QString info = notes;
        // Trim to a reasonable length for display
        if (info.length() > 800)
            info = info.left(800) + QStringLiteral("...");
        box.setDetailedText(info);
    }
    auto *downloadBtn = box.addButton(tr("Download"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Close);
    box.exec();
    if (box.clickedButton() == downloadBtn)
        QDesktopServices::openUrl(QUrl(url));
}

void MainWindow::onUpToDate()
{
    statusBar()->clearMessage();
    QMessageBox::information(this, tr("No updates"),
                             tr("%1 is up to date (v%2).")
                                 .arg(AppInfo::Name)
                                 .arg(QString::fromLatin1(AppInfo::Version)));
}

void MainWindow::onUpdateCheckFailed(const QString &error)
{
    statusBar()->clearMessage();
    QMessageBox::warning(this, tr("Update check Failed"),
                         tr("Could not check for updates:\n%1").arg(error));
}

void MainWindow::dataChanged()
{
    if (m_closing)
        return;
    if (!hexEdit)
        return;
    setWindowModified(hexEdit->isModified());
    updateActionStates();
    updateTabTitle(m_tabWidget->currentIndex());
}

void MainWindow::onHexDataChangedAt(qint64 offset)
{
    if (m_closing || !m_document)
        return;

    const qint64 newSize = hexEdit->dataSize();
    bool refreshPaletteSectionPreviews = false;

    if (m_changeTrackingSnapshot.isNull()) {
        if (newSize <= 256 * 1024 * 1024)
            m_changeTrackingSnapshot = hexEdit->data();
        else
            m_changeTrackingSnapshot = QByteArray("");
        if (m_sectionModel) {
            const int sectionIndex = m_sectionModel->sectionIndexAtOffset(offset);
            if (sectionIndex >= 0
                && m_sectionModel->at(sectionIndex).displayMode == SectionDisplay_Palette) {
                refreshPaletteSectionUiState();
            }
        }
        return;
    }

    const qint64 oldSize = m_changeTrackingSnapshot.size();
    QVector<qint64> changedOffsets;

    if (oldSize == newSize && offset >= 0 && offset < newSize) {
        // Same size: only a single overwrite. Read just the changed byte(s)
        // from the already-buffered visible data or chunks (not the whole file).
        const char oldByte = m_changeTrackingSnapshot.at(static_cast<int>(offset));
        const QByteArray oneByte = hexEdit->dataAt(offset, 1);
        const char newByte = oneByte.isEmpty() ? oldByte : oneByte.at(0);
        if (oldByte != newByte) {
            applyIncrementalOriginalByteChange(m_document->originalBytes, offset, oldByte, newByte);
            m_changeTrackingSnapshot[static_cast<int>(offset)] = newByte;
            changedOffsets.append(offset);
        }
        // A macro (e.g. multi-byte overwrite from script insert) fires
        // dataChangedAt only once with the last byte's offset.  Detect nearby
        // bytes that may also have changed (scan a small window, not the whole file).
        {
            const qint64 scanStart = qMax<qint64>(0, offset - 64);
            const qint64 scanEnd = qMin(newSize, offset + 65);
            const QByteArray region = hexEdit->dataAt(scanStart, scanEnd - scanStart);
            for (qint64 i = scanStart; i < scanEnd && (i - scanStart) < region.size(); ++i) {
                const char ob = m_changeTrackingSnapshot.at(static_cast<int>(i));
                const char nb = region.at(static_cast<int>(i - scanStart));
                if (ob != nb) {
                    applyIncrementalOriginalByteChange(m_document->originalBytes, i, ob, nb);
                    m_changeTrackingSnapshot[static_cast<int>(i)] = nb;
                    changedOffsets.append(i);
                }
            }
        }

        if (!changedOffsets.isEmpty() && m_sectionModel) {
            for (qint64 changedOffset : changedOffsets) {
                const int sectionIndex = m_sectionModel->sectionIndexAtOffset(changedOffset);
                if (sectionIndex >= 0
                    && m_sectionModel->at(sectionIndex).displayMode == SectionDisplay_Palette) {
                    refreshPaletteSectionPreviews = true;
                    break;
                }
            }
        }

        if (!changedOffsets.isEmpty() && hexEdit && hexEdit->pointers()) {
            QSet<qint64> affectedPointers;
            for (qint64 changedOffset : changedOffsets) {
                const qint64 ptrStart = hexEdit->pointerStartAt(changedOffset, currentPointerSize());
                if (ptrStart >= 0)
                    affectedPointers.insert(ptrStart);
            }

            bool pointerTableChanged = false;
            for (qint64 ptrStart : affectedPointers) {
                const int ptrSize = hexEdit->pointers()->getPointerSize(ptrStart);
                if (ptrSize < 2 || ptrSize > 4)
                    continue;

                const QByteArray rawPtr = hexEdit->dataAt(ptrStart, ptrSize);
                if (rawPtr.size() != ptrSize)
                    continue;

                const quint64 decoded = decodePointer(
                    reinterpret_cast<const uchar *>(rawPtr.constData()),
                    ptrSize,
                    hexEdit->byteOrder);
                const qint64 newTarget = static_cast<qint64>(decoded) + currentPointerOffset();
                if (newTarget < 0)
                    continue;

                if (hexEdit->pointers()->getOffset(ptrStart) != newTarget) {
                    hexEdit->pointers()->addPointer(ptrStart, newTarget, ptrSize);
                    pointerTableChanged = true;
                }
            }

            if (pointerTableChanged) {
                pointersUpdated();
                if (m_pointersDock && m_pointersDock->isVisible())
                    m_pointersDock->refreshView();
                if (pointersDialog)
                    pointersDialog->refreshFromTable();
                hexEdit->viewport()->update();
            }
        }
    } else {
        // Insert/delete: adjust tracked original-byte offsets so they stay
        // aligned with the shifted data, then resync snapshot.
        const qint64 delta = newSize - oldSize;
        if (delta != 0 && offset >= 0 && !m_document->originalBytes.isEmpty())
            adjustOriginalBytesForSizeChange(m_document->originalBytes, offset, delta);
        if (newSize <= 256 * 1024 * 1024)
            m_changeTrackingSnapshot = hexEdit->data();
        else
            m_changeTrackingSnapshot = QByteArray("");
        refreshPaletteSectionPreviews = (delta != 0);
    }

    if (refreshPaletteSectionPreviews)
        refreshPaletteSectionUiState();

    if (m_changesUiUpdateTimer)
        m_changesUiUpdateTimer->start();
}

void MainWindow::flushChangesUiUpdate()
{
    if (m_closing)
        return;

    if (m_changesDock && m_changesDock->isVisible())
        refreshChangesView();

    if (showChangesAct && showChangesAct->isChecked())
        updateChangedBytesHighlight();
}

void MainWindow::closeFile()
{
    if (!maybeSave())
        return;
    if (!maybeSaveProject())
        return;

    const bool hadProject = m_document && !m_document->projectFilePath.isEmpty();
    m_document->projectFilePath.clear();
    m_document->projectName.clear();
    m_document->clearDirty();
    m_document->originalBytes.clear();
    m_document->originalFileSize = -1;
    hexEdit->setData(QByteArray());
    m_changeTrackingSnapshot = QByteArray();
    hexEdit->clearPointers();
    resetNavigationHistory();
    showPointersAct->setEnabled(false);

    // Remove table: always when a project existed, otherwise respect settings
    {
        auto &s = AppSettings::instance();
        if (hadProject || s.value("ResetTableOnClose", false).toBool()) {
            hexEdit->removeTranslationTable();
            tb = nullptr;
            m_tablesDock->clearAll();
            useTableAct->setChecked(false);
            useTableAct->setEnabled(false);
            editTableAct->setEnabled(false);
            saveTableAct->setEnabled(false);
            saveTableAsAct->setEnabled(false);
        }
        if (s.value("ResetEncodingOnClose", false).toBool()) {
            const QString defEnc = s.value("DefaultEncoding", QStringLiteral("ASCII")).toString();
            m_currentEncoding = defEnc;
            hexEdit->setCurrentEncoding(defEnc);
            if (lbEncoding)
                lbEncoding->setText(defEnc);
            syncEncodingMenu();
        }
    }

    setCurrentFile("");
    statusBar()->showMessage(tr("File closed"), 2000);
}

void MainWindow::newFile()
{
    createSession();
    setCurrentFile("");

    // New files start in INSERT mode with UTF-8 encoding
    if (hexEdit) {
        hexEdit->setOverwriteMode(false);
        setOverwriteMode(false);
        m_currentEncoding = QStringLiteral("UTF-8");
        hexEdit->setCurrentEncoding(m_currentEncoding);
        if (lbEncoding)
            lbEncoding->setText(m_currentEncoding);
        syncEncodingMenu();
    }

    statusBar()->showMessage(tr("New file created"), 2000);
}

void MainWindow::open()
{
    const QString fileName = QFileDialog::getOpenFileName(this, tr("Open file"), lastDirectory(kLastFileDirKey));

    if (!fileName.isEmpty()) {
        // Open in new tab if current has content; handle no-tabs state safely
        if (m_sessions.isEmpty() || !isUntitled || hexEdit->isModified()) {
            loadFileInNewTab(fileName);
        } else {
            loadFile(fileName);
        }
    }
}

void MainWindow::revert()
{
    if (isUntitled)
    {
        // For new files, just clear the data
        if (QMessageBox::warning(this, tr("Clear data"), tr("Clear all data and changes?"), QMessageBox::Yes | QMessageBox::Cancel) == QMessageBox::Yes)
        {
            m_document->originalBytes.clear();
            m_document->originalFileSize = -1;
            hexEdit->setData(QByteArray());
            m_changeTrackingSnapshot = QByteArray();
            hexEdit->clearPointers();
            showPointersAct->setEnabled(false);
            statusBar()->showMessage(tr("Data cleared"), 2000);
        }
    }
    else
    {
        // For existing files, reload from disk
        if (QMessageBox::warning(this, tr("Revert"), tr("Reload file from disk and discard all changes?"), QMessageBox::Yes | QMessageBox::Cancel) == QMessageBox::Yes)
        {
            // Reload file directly without triggering maybeSave()
            m_currentSession->file.setFileName(curFile);
            if (hexEdit->setData(m_currentSession->file))
            {
                if (hexEdit->dataSize() <= 256 * 1024 * 1024)
                    m_changeTrackingSnapshot = hexEdit->data();
                else
                    m_changeTrackingSnapshot = QByteArray("");
                resetNavigationHistory();
                statusBar()->showMessage(tr("File reverted"), 2000);
            }
            else
            {
                QMessageBox::warning(this, QString::fromLatin1(AppInfo::Name),
                                     tr("Cannot read file %1:\n%2.")
                                     .arg(curFile)
                                     .arg(m_currentSession->file.errorString()));
            }
        }
    }
}

void MainWindow::optionsAccepted()
{
    updateHexEditorSettings();
}

void MainWindow::pointersUpdated()
{
    showPointersAct->setEnabled(!hexEdit->pointers()->empty());
    if (!m_document || !hexEdit)
        return;

    if (m_restoringSession || m_restoringProjectUi)
        return;

    QVector<QPair<qint64, qint64>> currentPointers;
    PointerListModel *model = hexEdit->pointers();
    if (model) {
        const QList<qint64> keys = model->pointerKeys();
        currentPointers.reserve(keys.size());
        for (qint64 ptrOfs : keys) {
            const qint64 target = model->getOffset(ptrOfs);
            const int size = model->getPointerSize(ptrOfs);
            currentPointers.append({ptrOfs, PointerListModel::encodePtrValue(target, size)});
        }
    }

    if (m_document->pointerSnapshot() != currentPointers)
        m_document->markDirty();
}

void MainWindow::findNext()
{
    if (!searchDialog)
        searchDialog = new SearchDialog(hexEdit, this);
    else
        searchDialog->setHexEdit(hexEdit);

    searchDialog->setAvailableTables(m_tablesDock->allTables(),
                                     m_tablesDock->currentIndex(),
                                     useTableAct && useTableAct->isChecked());
    searchDialog->findNext();
}

void MainWindow::showJumpToDialog()
{
    if (!jumpToDialog)
        jumpToDialog = new JumpToDialog(hexEdit, this);

    jumpToDialog->setHexEdit(hexEdit);
    if (m_currentSession)
        jumpToDialog->setOffsetText(m_currentSession->jumpToText);

    jumpToDialog->show();
    jumpToDialog->raise();
    jumpToDialog->activateWindow();
}

bool MainWindow::save()
{
    bool ok;
    if (isUntitled)
        ok = saveAs();
    else if (m_document
             && !m_document->sourceFilePath.isEmpty()
             && (hasProjectData() || !m_document->projectFilePath.isEmpty())) {
        const QString sourceCanonical = QFileInfo(m_document->sourceFilePath).canonicalFilePath();
        const QString sourcePath = sourceCanonical.isEmpty() ? m_document->sourceFilePath : sourceCanonical;
        const QString curCanonical = QFileInfo(curFile).canonicalFilePath();
        const QString currentPath = curCanonical.isEmpty() ? curFile : curCanonical;

        if (!currentPath.isEmpty() && currentPath == sourcePath) {
            QString initialPath = curFile;
            if (initialPath.isEmpty())
                initialPath = lastDirectory(kLastFileDirKey);

            const QString exportPath = QFileDialog::getSaveFileName(
                this, tr("Save current version as..."), initialPath);
            if (exportPath.isEmpty())
                return false;

            const QString exportCanonical = QFileInfo(exportPath).canonicalFilePath();
            const QString chosenPath = exportCanonical.isEmpty() ? exportPath : exportCanonical;
            if (chosenPath == sourcePath) {
                const auto answer = QMessageBox::warning(
                    this,
                    QString::fromLatin1(AppInfo::Name),
                    tr("The selected file is the original source. "
                       "If you overwrite it, tracking differences against source will no longer be possible.\n\n"
                       "Continue anyway?"),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::No);
                if (answer != QMessageBox::Yes)
                    return false;
            }

            ok = saveFile(exportPath);
        } else {
            ok = saveFile(curFile);
        }
    } else {
        ok = saveFile(curFile);
    }

    if (ok && m_document) {
        if (!m_document->projectFilePath.isEmpty()) {
            // Project already has a file — save it
            saveProjectImpl(m_document->projectFilePath);
        } else if (hasProjectData()) {
            // Project data exists but no project file yet — ask user
            QMessageBox msgBox(this);
            msgBox.setIcon(QMessageBox::Information);
            msgBox.setWindowTitle(QString::fromLatin1(AppInfo::Name));
            msgBox.setText(tr("This file has project data (tables, pointers, formatting) "
                              "that is not saved in the file itself."));
            auto *btnSaveProject = msgBox.addButton(tr("Save Project"), QMessageBox::AcceptRole);
            msgBox.addButton(tr("File Only"), QMessageBox::RejectRole);
            msgBox.setDefaultButton(btnSaveProject);
            msgBox.exec();
            if (msgBox.clickedButton() == btnSaveProject)
                saveProjectAs();
        }
    }

    return ok;
}

bool MainWindow::saveAs()
{
    QString initialPath = curFile;
    if (initialPath.isEmpty())
        initialPath = lastDirectory(kLastFileDirKey);

    QString fileName = QFileDialog::getSaveFileName(this, tr("Save As..."), initialPath);

    if (fileName.isEmpty())
        return false;

    return saveFile(fileName);
}

void MainWindow::saveSelectionToReadableFile()
{
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save dump to file"), lastDirectory(kLastDumpDirKey));

    if (!fileName.isEmpty())
    {
        QFile file(fileName);

        if (!file.open(QFile::WriteOnly | QFile::Text)) {
            QMessageBox::warning(this, QString::fromLatin1(AppInfo::Name),
                                 tr("Cannot write file %1:\n%2.")
                                     .arg(fileName)
                                     .arg(file.errorString()));
            return;
        }

        QApplication::setOverrideCursor(Qt::WaitCursor);

        const QString data = hexEdit->decodeTextForCurrentEncoding(hexEdit->getRawSelection());
        file.write(data.toUtf8());

         //file.write(hexEdit->selectionToReadableString().toLatin1());

        QApplication::restoreOverrideCursor();

        statusBar()->showMessage(tr("File saved"), 2000);
        rememberDirectory(kLastDumpDirKey, fileName);
    }
}

void MainWindow::saveToReadableFile()
{
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save dump to file"), lastDirectory(kLastDumpDirKey));
    if (!fileName.isEmpty())
    {
        QFile file(fileName);
        if (!file.open(QFile::WriteOnly | QFile::Text)) {
            QMessageBox::warning(this, QString::fromLatin1(AppInfo::Name),
                                 tr("Cannot write file %1:\n%2.")
                                     .arg(fileName)
                                     .arg(file.errorString()));
            return;
        }

        QApplication::setOverrideCursor(Qt::WaitCursor);
        file.write(hexEdit->toReadableString().toUtf8());
        QApplication::restoreOverrideCursor();

        statusBar()->showMessage(tr("File saved"), 2000);
        rememberDirectory(kLastDumpDirKey, fileName);
    }
}

void MainWindow::setAddress(qint64 address)
{
    if (!lbAddress)
        return;

    const int digits = (hexEdit ? qMax(1, hexEdit->addressWidth()) : 1);
    lbAddress->setText(QString("0x%1").arg(address, digits, 16, QChar('0')));
    curOffset = address;
    pushNavigationPosition(address);

    updateValuePanels();
}

void MainWindow::updateValuePanels()
{
    if (!hexEdit)
        return;
    auto value = hexEdit->getValue(curOffset);

    const quint8 byteValue = value.uByte;
    const quint16 wordValue = readWord(value, hexEdit->byteOrder);
    const quint32 dwordValue = readDword(value, hexEdit->byteOrder);

    if (showSignedValuesAct && showSignedValuesAct->isChecked())
    {
        lbValueByte->setText(QString("B: %1").arg(static_cast<qint8>(byteValue)));
        lbValueWord->setText(QString("W: %1").arg(static_cast<qint16>(wordValue)));
        lbValueDword->setText(QString("D: %1").arg(static_cast<qint32>(dwordValue)));
    }
    else
    {
        lbValueByte->setText(QString("B: %1").arg(byteValue));
        lbValueWord->setText(QString("W: %1").arg(wordValue));
        lbValueDword->setText(QString("D: %1").arg(dwordValue));
    }
}

void MainWindow::selectRangeInEditor(qint64 start, qint64 end, bool focus)
{
    if (!hexEdit)
        return;

    const qint64 clampedStart = qMax<qint64>(0, start);
    const qint64 clampedEnd = qMin(end, hexEdit->dataSize());
    if (clampedEnd <= clampedStart)
        return;

    hexEdit->selectByteRange(clampedStart, clampedEnd);
    hexEdit->ensureVisibleTop();
    if (focus)
        hexEdit->setFocus();
}

void MainWindow::setSelection(qint64 start, qint64 end)
{
    if (!lbSelection)
        return;

    // end is now exclusive (half-open range); real byte count = end - start.
    // A selection of exactly 1 means only the cursor byte = no real selection.
    auto len = end - start;
    auto selBytes = len > 1 ? len : 0;   // bytes actually selected (excluding mere cursor byte)

    auto text = selBytes ?
        QString("0x%1-0x%2: %3")
            .arg(start, 2, 16, QChar('0'))
            .arg(end - 1, 2, 16, QChar('0'))   // end-1 = last included byte
            .arg(selBytes)
        : tr("No selection");

    lbSelection->setText(text);
    const bool canModify = hexEdit && !hexEdit->isReadOnly() && !hexEdit->showOriginal();
    saveSelectionReadable->setEnabled(selBytes > 0);
    copyAct->setEnabled(selBytes > 0);
    cutAct->setEnabled(selBytes > 0 && !hexEdit->overwriteMode() && canModify);
    pasteAct->setEnabled(canModify && !QApplication::clipboard()->text().isEmpty());

    updateScriptMenuState(selBytes > 0);
}

void MainWindow::setOverwriteMode(bool mode)
{
    if (lbOverwriteMode)
        lbOverwriteMode->setText(mode ? tr("REPLACE") : tr("INSERT"));
}

void MainWindow::setSize(qint64 size)
{
    if (lbSize)
        lbSize->setText(QString("%1").arg(size));
    if (m_sectionsDock)
        m_sectionsDock->setFileSize(size);
}

void MainWindow::updateStatusBarVisibility()
{
    if (lbEncoding)
        lbEncoding->setVisible(showStatusEncodingAct->isChecked());
    if (lbValueByte)
        lbValueByte->setVisible(showStatusByteAct->isChecked());
    if (lbValueWord)
        lbValueWord->setVisible(showStatusWordAct->isChecked());
    if (lbValueDword)
        lbValueDword->setVisible(showStatusDwordAct->isChecked());

    if (lbSelection)
        lbSelection->setVisible(showStatusSelectionAct->isChecked());

    const bool showAddress = showStatusAddressAct->isChecked();
    if (lbAddress)
        lbAddress->setVisible(showAddress);

    const bool showSize = showStatusSizeAct->isChecked();
    if (lbSizeName)
        lbSizeName->setVisible(showSize);
    if (lbSize)
        lbSize->setVisible(showSize);

    const bool showMode = showStatusModeAct->isChecked();

    if (lbOverwriteMode)
        lbOverwriteMode->setVisible(showMode);
}

void MainWindow::showOptionsDialog()
{
    if (!optionsDialog)
    {
        optionsDialog = new OptionsDialog(this);
        connect(optionsDialog, &QDialog::accepted, this, &MainWindow::optionsAccepted);
    }
    optionsDialog->show();
}

void MainWindow::showSearchDialog()
{
    if (!searchDialog)
        searchDialog = new SearchDialog(hexEdit, this);
    else
        searchDialog->setHexEdit(hexEdit);

    searchDialog->setAvailableTables(m_tablesDock->allTables(),
                                     m_tablesDock->currentIndex(),
                                     useTableAct && useTableAct->isChecked());

    // Restore per-tab search state
    if (m_currentSession) {
        SearchDialog::State s;
        s.findText    = m_currentSession->searchFindText;
        s.findFormat  = m_currentSession->searchFindFormat;
        s.replaceText = m_currentSession->searchReplaceText;
        s.replaceFormat = m_currentSession->searchReplaceFormat;
        s.relative    = m_currentSession->searchRelative;
        s.sectionScope = m_currentSession->searchSectionScope;
        searchDialog->setDialogState(s);
    }

    searchDialog->show();
}

void MainWindow::showVirtualFormatDialog(qint64 rangeFrom, qint64 rangeTo)
{
    if (!hexEdit) return;

    const QVector<TableTab> tables = m_tablesDock ? m_tablesDock->allTables() : QVector<TableTab>();
    const int activeTableIdx = (m_tablesDock && useTableAct && useTableAct->isChecked())
                                    ? m_tablesDock->currentIndex() : -1;
    VirtualFormatDialog dlg(tables, activeTableIdx, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const int lines = dlg.lines();
    if (lines <= 0)
        return;

    // Determine search range
    const qint64 searchFrom = (rangeFrom >= 0) ? rangeFrom : 0;
    const qint64 searchTo   = (rangeTo >= 0)   ? rangeTo   : hexEdit->dataSize();

    QVector<qint64> newBreaks = hexEdit->lineBreaks();

    if (dlg.splitByCount()) {
        // Split every N bytes
        const int count = dlg.countValue();
        if (count <= 0)
            return;
        for (qint64 pos = searchFrom + count - 1; pos < searchTo; pos += count) {
            for (int i = 0; i < lines; ++i)
                newBreaks.append(pos);
        }
    } else {
        // Split by character sequence
        const QByteArray needle = dlg.character();
        const bool ignoreRepeated = dlg.ignoreRepeated();
        if (needle.isEmpty())
            return;

        qint64 from = searchFrom;
        while (from < searchTo) {
            qint64 pos = hexEdit->indexOf(needle, from);
            if (pos < 0 || pos + needle.size() > searchTo)
                break;

            qint64 breakOffset = pos + needle.size() - 1;

            if (ignoreRepeated) {
                // Skip consecutive repeats of the needle — advance to the last one
                while (true) {
                    qint64 nextPos = hexEdit->indexOf(needle, pos + needle.size());
                    if (nextPos == pos + needle.size() && nextPos + needle.size() <= searchTo) {
                        pos = nextPos;
                        breakOffset = pos + needle.size() - 1;
                    } else {
                        break;
                    }
                }
            }

            for (int i = 0; i < lines; ++i)
                newBreaks.append(breakOffset);
            from = pos + needle.size();
        }
    }

    hexEdit->setLineBreaks(newBreaks);
}

void MainWindow::removeVirtualFormatting(qint64 rangeFrom, qint64 rangeTo)
{
    if (!hexEdit) return;
    if (hexEdit->lineBreaks().isEmpty()) return;

    const bool hasRange = (rangeFrom >= 0 && rangeTo >= 0);

    // Use addButton(tr(...)) so button labels go through LangTranslator regardless
    // of whether the platform shows a native sheet (e.g. macOS NSAlert).
    QMessageBox box(QMessageBox::Question, QString(),
                    tr("Remove all virtual line breaks?"),
                    QMessageBox::NoButton, this);
    QPushButton *yesBtn = box.addButton(tr("Yes"), QMessageBox::YesRole);
    box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    box.exec();

    if (box.clickedButton() != yesBtn)
        return;

    if (hasRange)
        hexEdit->clearLineBreaksInRange(rangeFrom, rangeTo);
    else
        hexEdit->clearLineBreaks();
}

void MainWindow::onQuickSearchCompleted(int found)
{
    if (found == 0)
    {
        QMessageBox::information(this, QString(), tr("No pointers found"));
    }
    else
    {
        hexEdit->viewport()->update();
    }
}

void MainWindow::goToPreviousPosition()
{
    navigateToHistoryIndex(navigationHistoryIndex - 1);
}

void MainWindow::goToNextPosition()
{
    navigateToHistoryIndex(navigationHistoryIndex + 1);
}

void MainWindow::goToFirstPosition()
{
    navigateToHistoryIndex(0);
}

void MainWindow::goToLastPosition()
{
    navigateToHistoryIndex(navigationHistory.size() - 1);
}

void MainWindow::goToFileBeginning()
{
    hexEdit->jumpTo(0);
}

void MainWindow::goToFileEnd()
{
    hexEdit->jumpTo(hexEdit->dataSize());
}

bool MainWindow::loadTable()
{
    auto fileName = QFileDialog::getOpenFileName(this, tr("Open translation table"), lastDirectory(kLastTableDirKey), "Tables (*.tbl *.tab *.table);;Text files (*.txt)");

    if (!fileName.isEmpty())
    {
        bool encodingAccepted = true;
        const QString importEncoding = chooseTableImportEncoding(this, fileName, &encodingAccepted);
        if (!encodingAccepted)
            return true;

        const TranslationTable newTable(fileName, importEncoding);

        // Add to dock widget
        m_tablesDock->addTable(QFileInfo(fileName).completeBaseName(), &newTable);
        m_tablesDock->show();

        // Also keep tb in sync for backward compat
        tb = m_tablesDock->currentTable();
        hexEdit->setTranslationTable(tb);
        useTableAct->setDisabled(false);
        useTableAct->setChecked(true);
        editTableAct->setDisabled(false);
        saveTableAct->setDisabled(false);
        saveTableAsAct->setDisabled(false);
        updateActionStates();
        rememberDirectory(kLastTableDirKey, fileName);
        tableFilePath = fileName;
        addToRecentTables(fileName);
        if (m_document)
            m_document->markDirty();
        statusBar()->showMessage(tr("Table loaded"), 2000);
    }

    return true;
}

void MainWindow::switchUseTable()
{
    tb = m_tablesDock->currentTable();
    applySelectedTable();
    updateActionStates();
    refreshChangesView();
    
    if (m_pointersDock)
        m_pointersDock->refreshView();
}

void MainWindow::updateScriptMenuState(bool enabled)
{
    const bool canModify = hexEdit && !hexEdit->isReadOnly() && !hexEdit->showOriginal();

    if (dumpScriptAct)
        dumpScriptAct->setEnabled(enabled);
    if (toolbarDumpScriptAct)
        toolbarDumpScriptAct->setEnabled(enabled);

    // Keep import action available in editable mode regardless of selection.
    if (insertScriptAct)
        insertScriptAct->setEnabled(canModify);
    if (toolbarInsertScriptAct)
        toolbarInsertScriptAct->setEnabled(canModify);

    if (scriptMenu)
        scriptMenu->setEnabled(enabled || canModify);
}

void MainWindow::toggleOverwriteMode()
{
    hexEdit->setOverwriteMode(!hexEdit->overwriteMode());
}

void MainWindow::setLanguage()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action)
        return;

    const QString language = action->data().toString();
    
    // Apply language
    applyLanguage(language);

    // Save language preference
    auto &settings = AppSettings::instance();
    settings.setValue("Language", language);
}

QString MainWindow::detectSystemLanguage()
{
    // Get system locale
    const QLocale systemLocale = QLocale::system();
    const QString uiLanguage = systemLocale.uiLanguages().isEmpty() ? 
        systemLocale.name() : systemLocale.uiLanguages().first();

    // Map system language to our supported languages
    const QString langLower = uiLanguage.toLower();
    
    if (langLower.startsWith("ru"))
        return QStringLiteral("ru");
    else if (langLower.startsWith("de"))
        return QStringLiteral("de");
    else if (langLower.startsWith("fr"))
        return QStringLiteral("fr");
    else if (langLower.startsWith("es"))
        return QStringLiteral("es");
    else if (langLower.startsWith("pt"))
        return QStringLiteral("pt");
    else if (langLower.startsWith("ja"))
        return QStringLiteral("ja");
    else if (langLower.startsWith("zh"))
        return QStringLiteral("zh_CN");
    
    // Default to English if no match
    return QStringLiteral("en");
}

void MainWindow::applyLanguage(const QString &language)
{
    // Remove previous custom translator (if any)
    const auto translators = qApp->findChildren<LangTranslator *>();
    for (auto *t : translators)
    {
        qApp->removeTranslator(t);
        t->deleteLater();
    }

    // Load and install new translator
    LangTranslator *translator = new LangTranslator(qApp);
    QStringList candidates;
    candidates << language;
    
    const QString languageShort = language.left(2);
    if (!languageShort.isEmpty() && languageShort != language)
        candidates << languageShort;

    bool loaded = false;
    for (const QString &candidate : candidates)
    {
        const QString path = QStringLiteral(":/translations/") + candidate + QStringLiteral(".lang");
        if (!QFile::exists(path))
            continue;
        if (translator->load(path))
        {
            loaded = true;
            break;
        }
    }

    if (loaded)
    {
        qApp->installTranslator(translator);
    }
    else
    {
        delete translator;
    }

    LangTranslator::setCurrentLanguage(language);

    // Retranslate everything immediately
    retranslateUi();
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QMainWindow::changeEvent(event);
}

void MainWindow::editTable()
{
    // Show and raise the dock widget instead of a separate dialog
    m_tablesDock->show();
    m_tablesDock->raise();
}

void MainWindow::onTranslationTableChanged()
{
    tb = m_tablesDock->currentTable();
    applySelectedTable();

    hexEdit->viewport()->update();
    hexEdit->update();

    if (pointersDialog)
        pointersDialog->refreshFromTable();
    else
        m_pointersDock->refreshView();
    
    refreshChangesView();
}

void MainWindow::onDockTableChanged(TranslationTable *table)
{
    if (m_closing) return;
    tb = table;
    applySelectedTable();

    const bool hasTables = m_tablesDock->count() > 0;
    useTableAct->setEnabled(hasTables);
    editTableAct->setEnabled(hasTables);
    saveTableAct->setEnabled(hasTables);
    saveTableAsAct->setEnabled(hasTables);

    hexEdit->viewport()->update();
    hexEdit->update();

    if (pointersDialog)
        pointersDialog->refreshFromTable();
    else
        m_pointersDock->refreshView();
    
    refreshChangesView();
}

void MainWindow::onDockTableContentChanged()
{
    if (m_closing) return;
    tb = m_tablesDock->currentTable();
    applySelectedTable();

    hexEdit->viewport()->update();
    hexEdit->update();

    if (!m_restoringTableDockState && m_document)
        m_document->markDirty();

    if (pointersDialog)
        pointersDialog->refreshFromTable();
    else
        m_pointersDock->refreshView();
    
    refreshChangesView();
}

void MainWindow::createEmptyTable()
{
    m_tablesDock->addTable();
    m_tablesDock->show();
    tb = m_tablesDock->currentTable();
    hexEdit->setTranslationTable(tb);
    useTableAct->setDisabled(false);
    useTableAct->setChecked(true);
    editTableAct->setDisabled(false);
    saveTableAct->setDisabled(false);
    saveTableAsAct->setDisabled(false);
    updateActionStates();
}

void MainWindow::showSemiAutoTableDialog()
{
    if (!semiAutoTableDialog)
    {
        semiAutoTableDialog = new SemiAutoTableDialog(hexEdit, this);
        connect(semiAutoTableDialog, &SemiAutoTableDialog::tableGenerated, this, &MainWindow::onSemiAutoTableGenerated);
    }
    semiAutoTableDialog->show();
}

void MainWindow::onSemiAutoTableGenerated()
{
    if (semiAutoTableDialog && semiAutoTableDialog->hasGeneratedTable()) {
        const TranslationTable &generated = semiAutoTableDialog->generatedTable();
        m_tablesDock->addTable(QString(), &generated);
        tb = m_tablesDock->currentTable();
        m_tablesDock->show();
    }
    useTableAct->setDisabled(false);
    editTableAct->setDisabled(false);
    saveTableAct->setDisabled(false);
    saveTableAsAct->setDisabled(false);
    useTableAct->setChecked(true);
    hexEdit->setTranslationTable(tb);
    updateActionStates();
}

void MainWindow::saveTable()
{
    tb = m_tablesDock->currentTable();
    if (!tb || tb->size() == 0)
        return;

    if (tableFilePath.isEmpty())
    {
        saveTableAs();
        return;
    }

    if (!tb->save(tableFilePath))
    {
        QMessageBox::warning(this, tr("Error"), tr("Could not save the table file"));
    }
    else
    {
        addToRecentTables(tableFilePath);
        statusBar()->showMessage(tr("Table saved"), 2000);
    }
}

void MainWindow::saveTableAs()
{
    tb = m_tablesDock->currentTable();
    if (!tb || tb->size() == 0)
        return;

    auto fileName = QFileDialog::getSaveFileName(this, tr("Save translation table"),
        lastDirectory(kLastTableDirKey), "Tables (*.tbl);;Text files (*.txt)");

    if (!fileName.isEmpty())
    {
        if (!tb->save(fileName))
        {
            QMessageBox::warning(this, tr("Error"), tr("Could not save the table file"));
            return;
        }

        rememberDirectory(kLastTableDirKey, fileName);
        tableFilePath = fileName;
        addToRecentTables(fileName);
        statusBar()->showMessage(tr("Table saved"), 2000);
    }
}

void MainWindow::dumpScript()
{
    if (!dumpScriptDialog)
        dumpScriptDialog = new DumpScriptDialog(hexEdit, this);

    dumpScriptDialog->setRomProfile(currentPointerSize(), currentPointerOffset());
    dumpScriptDialog->setAvailableTables(m_tablesDock->allTables(),
                                         m_tablesDock->currentIndex(),
                                         useTableAct->isChecked());
    dumpScriptDialog->show();
}

void MainWindow::insertScript()
{
    if (!hexEdit || hexEdit->isReadOnly() || hexEdit->showOriginal())
        return;

    if (!insertScriptDialog)
        insertScriptDialog = new InsertScriptDialog(hexEdit, this);

    insertScriptDialog->setAvailableTables(m_tablesDock ? m_tablesDock->allTables() : QVector<TableTab>());
    insertScriptDialog->setRomProfile(currentPointerSize(), currentPointerOffset());
    insertScriptDialog->show();
}

/*****************************************************************************/
/* Private Methods */
/*****************************************************************************/
void MainWindow::init()
{
    setAttribute(Qt::WA_DeleteOnClose);

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    m_tabWidget->setDocumentMode(true);
    connect(m_tabWidget, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, &MainWindow::onTabCloseRequested);
    setCentralWidget(m_tabWidget);

    // Allow nested dock areas so docks can be stacked:
    // multiple columns in side areas, multiple rows in bottom area.
    setDockNestingEnabled(true);

    // Create the first session
    EditorSession *firstSession = createSession();
    m_currentSession = firstSession;
    hexEdit = firstSession->editor;
    m_document = firstSession->document;
    isUntitled = true;

    m_changesUiUpdateTimer = new QTimer(this);
    m_changesUiUpdateTimer->setSingleShot(true);
    m_changesUiUpdateTimer->setInterval(40);
    connect(m_changesUiUpdateTimer, &QTimer::timeout, this, &MainWindow::flushChangesUiUpdate);

    // Tables dock widget (right side)
    m_tablesDock = new TablesDockWidget(this);
    addDockWidget(Qt::RightDockWidgetArea, m_tablesDock);
    m_tablesDock->show();  // visible by default
    connect(m_tablesDock, &TablesDockWidget::activeTableChanged,
            this, &MainWindow::onDockTableChanged);
    connect(m_tablesDock, &TablesDockWidget::tableContentChanged,
            this, &MainWindow::onDockTableContentChanged);
    connect(m_tablesDock, &TablesDockWidget::generateTableRequested,
            this, &MainWindow::showSemiAutoTableDialog);
    connect(m_tablesDock, &TablesDockWidget::copyToTabRequested,
            this, &MainWindow::startCopyTableToTab);
    connect(m_tablesDock, &TablesDockWidget::useTableToggled, this, [this](bool checked) {
        if (useTableAct->isEnabled()) {
            useTableAct->setChecked(checked);
            switchUseTable();
        }
    });

    // Pointers dock widget (bottom)
    m_pointersDock = new PointersDockWidget(this);
    m_pointersDock->setHexEdit(hexEdit);
    addDockWidget(Qt::BottomDockWidgetArea, m_pointersDock);
    m_pointersDock->show();
    connect(m_pointersDock, &PointersDockWidget::findPointersRequested,
            this, &MainWindow::showPointersDialog);

    // Changes dock widget (to the right of pointers dock, bottom)
    m_changesDock = new ChangesDockWidget(this);
    addDockWidget(Qt::BottomDockWidgetArea, m_changesDock);
    splitDockWidget(m_pointersDock, m_changesDock, Qt::Horizontal);
    // Set 50/50 width split and matching stretch factors
    for (auto *splitter : findChildren<QSplitter*>())
    {
        const int pointersIdx = splitter->indexOf(m_pointersDock);
        const int changesIdx = splitter->indexOf(m_changesDock);
        if (pointersIdx >= 0 && changesIdx >= 0)
        {
            splitter->setStretchFactor(pointersIdx, 1);
            splitter->setStretchFactor(changesIdx, 1);
            splitter->setSizes({400, 400});
            break;
        }
    }
    m_changesDock->show();

    // Sections dock widget (left side)
    m_sectionModel = new SectionListModel(this);
    m_sectionModel->setUndoStack(hexEdit ? hexEdit->undoStack() : nullptr);
    m_sectionsDock = new SectionsDockWidget(this);
    m_sectionsDock->setModel(m_sectionModel);
    addDockWidget(Qt::LeftDockWidgetArea, m_sectionsDock);
    m_sectionsDock->show();
    hexEdit->setSectionModel(m_sectionModel);

    connect(m_sectionsDock, &SectionsDockWidget::jumpToOffset, this, [this](qint64 offset) {
        hexEdit->setCursorPosition(offset * 2);
        hexEdit->ensureVisibleTop();
        if (hexEdit->verticalScrollBar()->value() > 0)
            hexEdit->verticalScrollBar()->setValue(hexEdit->verticalScrollBar()->value() - 1);
        hexEdit->setFocus();
    });
    connect(m_sectionsDock, &SectionsDockWidget::selectRangeRequested, this,
            [this](qint64 start, qint64 end) { selectRangeInEditor(start, end); });
    connect(m_sectionsDock, &SectionsDockWidget::virtualFormattingRequested, this,
            [this](qint64 start, qint64 end) {
                selectRangeInEditor(start, end, false);
                showVirtualFormatDialog(start, end);
            });
    connect(m_sectionsDock, &SectionsDockWidget::removeVirtualFormattingRequested, this,
            [this](qint64 start, qint64 end) {
                selectRangeInEditor(start, end, false);
                removeVirtualFormatting(start, end);
            });
    connect(m_sectionsDock, &SectionsDockWidget::showSectionsToggled, this, [this](bool checked) {
        hexEdit->setShowSections(checked);
    });
    connect(m_sectionsDock, &SectionsDockWidget::parseRequested, this, [this]() {
        parseSections();
    });
    connect(m_sectionsDock, &SectionsDockWidget::detectAudioRequested, this, [this]() {
        detectAudioSamples();
    });
    connect(m_sectionsDock, &SectionsDockWidget::detectFunctionsRequested, this, [this]() {
        detectFunctions();
    });
    connect(m_sectionsDock, &SectionsDockWidget::detectPalettesRequested, this, [this]() {
        detectPalettes();
    });
    connect(m_sectionsDock, &SectionsDockWidget::findSamplesInSectionRequested, this,
            [this](qint64 start, qint64 end) {
                detectAudioSamplesInRange(start, end);
            });
    connect(m_sectionsDock, &SectionsDockWidget::findPalettesInSectionRequested, this,
            [this](qint64 start, qint64 end) {
                detectPalettesInRange(start, end);
            });
    connect(m_sectionsDock, &SectionsDockWidget::applyPaletteToGraphicsSectionRequested, this,
            [this](qint64 paletteSectionStart) {
                applyDetectedPaletteToGraphicsSection(paletteSectionStart);
            });
    connect(m_sectionsDock, &SectionsDockWidget::findFunctionsInSectionRequested, this,
            [this](qint64 start, qint64 end) {
                detectFunctionsInRange(start, end);
            });
    connect(m_sectionsDock, &SectionsDockWidget::splitSectionRequested, this,
            [this](int sectionIdx, const QVector<qint64> &sizes) {
                splitSection(sectionIdx, sizes);
            });
    connect(m_sectionsDock, &SectionsDockWidget::disasmCpuChanged, this,
            [this](int /*sectionIdx*/, RomType /*cpu*/) {
                if (!hexEdit) return;
                // CPU is now resolved per-section inside HexEditor disasm path.
                hexEdit->viewport()->update();
            });
    connect(m_sectionsDock, &SectionsDockWidget::findPointersInSectionRequested, this,
            [this](qint64 start, qint64 end) {
                selectRangeInEditor(start, end, false);
                showPointersDialog();
                if (pointersDialog)
                    pointersDialog->setRange(start, end);
            });
    connect(m_sectionsDock, &SectionsDockWidget::dropPointersInSectionRequested, this,
            [this](qint64 start, qint64 end) {
                dropPointersInRange(start, end);
            });
    connect(m_sectionModel, &SectionListModel::sectionsChanged, this, [this]() {
        const bool restoring = m_restoringSession || m_restoringProjectUi;
        if (!restoring && m_document && m_document->sectionSnapshot != m_sectionModel->sections())
            m_document->markDirty();
        if (hexEdit) {
            refreshPaletteSectionUiState();
            syncDisplayDocksForOffset(hexEdit->cursorPosition() / 2);
            hexEdit->viewport()->update();
        }
    });
    connect(m_sectionModel, &SectionListModel::groupsChanged, this, [this]() {
        const bool restoring = m_restoringSession || m_restoringProjectUi;
        if (!restoring && m_document)
            m_document->markDirty();
    });
    // Auto-select the matching section in the tree as the cursor moves.
    // The actual connection is made per-editor in connectEditorSignals() so
    // it follows tab switches correctly.

    // Start inline section rename on double-click of header row in hex editor
    connect(hexEdit, &HexEditor::sectionHeaderDoubleClicked, this, [this](int sectionIndex) {
        if (m_sectionsDock)
            m_sectionsDock->startRenameSection(sectionIndex);
    });

    // Keep SectionsDockWidget informed about available table names for the
    // "Display mode" submenu (sync on add / remove / rename).
    auto syncTableNames = [this]() {
        if (!m_tablesDock || !m_sectionsDock) return;
        QStringList names;
        QVector<TranslationTable*> ptrs;
        const auto &tabs = m_tablesDock->allTables();
        for (int i = 0; i < tabs.size(); ++i) {
            names << tabs[i].name;
            ptrs << const_cast<TranslationTable*>(&tabs[i].table);
        }
        m_sectionsDock->setTableNames(names);
        hexEdit->setAllTables(ptrs);
    };
    connect(m_tablesDock, &TablesDockWidget::activeTableChanged,
            this, syncTableNames);
    connect(m_tablesDock, &TablesDockWidget::tableContentChanged,
            this, syncTableNames);
    syncTableNames();

    // ── Audio dock ──
    m_audioDock = new AudioDockWidget(this);
    addDockWidget(Qt::RightDockWidgetArea, m_audioDock);
#ifdef Q_OS_MAC
    m_pendingInitialMacDockVisibilitySync = true;
#else
    m_audioDock->hide();  // hidden by default, shown via View → Dock
#endif
    m_audioDock->setRomType(m_detectedRomType);
    hexEdit->setGlobalAudioFormat(m_audioDock->selectedFormat());
    m_audioDock->setSectionActive(false);

    auto syncAudioOptionsToCurrentSection = [this]() {
        if (!hexEdit || !m_sectionModel || !m_audioDock)
            return;
        const int idx = m_sectionModel->sectionIndexAtOffset(hexEdit->cursorPosition() / 2);
        if (idx < 0) {
            if (hexEdit->showAudioPanel()) {
                hexEdit->setGlobalAudioFormat(m_audioDock->selectedFormat());
                hexEdit->viewport()->update();
            }
            return;
        }

        Section sec = m_sectionModel->at(idx);
        if (sec.displayMode != SectionDisplay_Audio) {
            if (hexEdit->showAudioPanel()) {
                hexEdit->setGlobalAudioFormat(m_audioDock->selectedFormat());
                hexEdit->viewport()->update();
            }
            return;
        }

        auto opts = parseSectionOptions(sec.options);
        opts.insert(QStringLiteral("type"), audioSubtypeMnemonicFromFormat(m_audioDock->selectedFormat()));
        opts.insert(QStringLiteral("sample_rate"), QString::number(m_audioDock->selectedSampleRate()));
        opts.insert(QStringLiteral("speed"), QString::number(m_audioDock->playbackSpeed(), 'f', 3));
        const QString optionsText = serializeSectionOptions(opts);

        bool changed = false;
        if (sec.display != QStringLiteral("snd")) {
            sec.display = QStringLiteral("snd");
            changed = true;
        }
        if (sec.options != optionsText) {
            sec.options = optionsText;
            changed = true;
        }
        if (changed) {
            m_sectionModel->updateSection(idx, sec);
        } else if (hexEdit) {
            // Format change must immediately repaint audio histogram.
            hexEdit->viewport()->update();
        }
    };

    connect(m_audioDock, &AudioDockWidget::formatChanged, this,
            [syncAudioOptionsToCurrentSection](AudioSampleFormat) {
                syncAudioOptionsToCurrentSection();
            });
    connect(m_audioDock, &AudioDockWidget::sampleRateChanged, this,
            [syncAudioOptionsToCurrentSection](int) {
                syncAudioOptionsToCurrentSection();
            });
    connect(m_audioDock, &AudioDockWidget::playbackSpeedChanged, this,
            [syncAudioOptionsToCurrentSection](double) {
                syncAudioOptionsToCurrentSection();
            });

    // ── Graphics dock ──
    m_graphicsDock = new GraphicsDockWidget(this);
    addDockWidget(Qt::RightDockWidgetArea, m_graphicsDock);
#ifndef Q_OS_MAC
    m_graphicsDock->hide();
#endif
    m_graphicsDock->setRomType(m_detectedRomType);
    m_graphicsDock->setSectionActive(false);
    refreshPaletteSectionUiState();

    // Codec changed in dock → update global + current section
    connect(m_graphicsDock, &GraphicsDockWidget::codecChanged, this, [this](TileCodec codec) {
        hexEdit->setGlobalTileCodec(codec);
        if (auto *model = hexEdit->sectionModel()) {
            int idx = model->sectionIndexAtOffset(hexEdit->cursorPosition() / 2);
            if (idx >= 0) {
                Section sec = model->at(idx);
                if (sec.displayMode == SectionDisplay_Graphics) {
                    sec.tileCodec = codec;
                    // Reset custom palette when codec changes (bpp may differ)
                    sec.palette.clear();
                    model->updateSection(idx, sec);
                    m_graphicsDock->setPaletteColors(sec.palette);
                }
            }
        }
    });

    connect(m_graphicsDock, &GraphicsDockWidget::tileColsChanged, this, [this](int cols) {
        if (!hexEdit)
            return;

        hexEdit->setGlobalTileCols(cols);

        if (auto *model = hexEdit->sectionModel()) {
            const int idx = model->sectionIndexAtOffset(hexEdit->cursorPosition() / 2);
            if (idx >= 0) {
                Section sec = model->at(idx);
                if (sec.displayMode == SectionDisplay_Graphics && sec.tileCols != cols) {
                    sec.tileCols = cols;
                    model->updateSection(idx, sec);
                }
            }
        }
    });

    // Palette edited in dock → update current section
    connect(m_graphicsDock, &GraphicsDockWidget::paletteChanged, this, [this](const QVector<QRgb> &pal) {
        if (auto *model = hexEdit->sectionModel()) {
            int idx = model->sectionIndexAtOffset(hexEdit->cursorPosition() / 2);
            if (idx >= 0) {
                Section sec = model->at(idx);
                if (sec.displayMode == SectionDisplay_Graphics) {
                    sec.palette = pal;
                    model->updateSection(idx, sec);
                    if (hexEdit) hexEdit->viewport()->update();
                }
            }
        }
    });

    // Palette color selection → forward selected indices to editor
    connect(m_graphicsDock, &GraphicsDockWidget::leftPalIndexChanged, this, [this](int idx) {
        if (hexEdit) hexEdit->setGfxLeftPalIdx(idx);
    });
    connect(m_graphicsDock, &GraphicsDockWidget::rightPalIndexChanged, this, [this](int idx) {
        if (hexEdit) hexEdit->setGfxRightPalIdx(idx);
    });

    if (hexEdit)
        syncDisplayDocksForOffset(hexEdit->cursorPosition() / 2);

    // Ctrl+1..9 shortcuts for switching table tabs
    for (int i = 1; i <= 9; ++i) {
        auto *shortcut = new QShortcut(QKeySequence(static_cast<int>(Qt::CTRL) | (Qt::Key_0 + i)), this);
        connect(shortcut, &QShortcut::activated, this, [this, i]() {
            if (m_tablesDock && m_tablesDock->count() >= i)
                m_tablesDock->setCurrentIndex(i - 1);
        });
    }

    createActions();
    // Give the pointers dock the show-pointers toggle action after it's created
    m_pointersDock->addShowPointersAction(showPointersAct);
    connect(m_pointersDock, &PointersDockWidget::showPointersToggled, this, [this](bool checked) {
        showPointersAct->setChecked(checked);
        switchShowPointers();
    });
    connect(showPointersAct, &QAction::toggled,
            m_pointersDock, &PointersDockWidget::setShowPointersChecked);
    connect(showPointersAct, &QAction::changed, this, [this]() {
        m_pointersDock->setShowPointersEnabled(showPointersAct->isEnabled());
    });

    connect(m_changesDock, &ChangesDockWidget::showChangesToggled, this, [this](bool checked) {
        showChangesAct->setChecked(checked);
        toggleShowChanges();
    });
    connect(m_changesDock, &ChangesDockWidget::hexModeChanged, this, [this](bool hexMode) {
        if (m_currentSession)
            m_currentSession->changesHexMode = hexMode;
    });
    connect(m_changesDock, &ChangesDockWidget::showOriginalToggled, this, [this](bool show) {
        if (!hexEdit || !m_document)
            return;
        if (show) {
            // Reconstruct original file from current data + sparse originalBytes records
            QByteArray original = hexEdit->data();
            for (const auto &entry : m_document->originalBytes) {
                const int off = static_cast<int>(entry.first);
                const QByteArray &origBytes = entry.second;
                if (off >= 0 && off + origBytes.size() <= original.size())
                    original.replace(off, origBytes.size(), origBytes);
            }
            hexEdit->setOriginalData(original);
        }
        hexEdit->setShowOriginal(show);
        
        // Only switch table if there are tables with different isOriginal values
        if (shouldSwitchTableOnViewModeChange()) {
            const int idx = tableIndexForViewMode(show);
            if (idx >= 0 && m_tablesDock && m_tablesDock->currentIndex() != idx)
                m_tablesDock->setCurrentIndex(idx);
            else
                applyTranslationTableForViewMode();
        }
        
        refreshChangesView();
        updateActionStates();
    });
    connect(showChangesAct, &QAction::toggled,
            m_changesDock, &ChangesDockWidget::setShowChangesChecked);
    connect(showChangesAct, &QAction::changed, this, [this]() {
        m_changesDock->setShowChangesEnabled(showChangesAct->isEnabled());
    });
    connect(m_changesDock, &ChangesDockWidget::jumpToOffset, this, [this](qint64 offset) {
        hexEdit->setCursorPosition(offset * 2);
        hexEdit->ensureVisible();
        hexEdit->setFocus();
    });
    connect(m_tablesDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (m_currentSession) {
            m_currentSession->dockTablesVisible = visible;
            m_currentSession->dockVisibilityInitialized = true;
        }
        
        if (!m_restoringProjectUi)
            saveProjectDockVisibilityState();
        updateDockAreaActions();
    });
    connect(m_pointersDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (m_currentSession) {
            m_currentSession->dockPointersVisible = visible;
            m_currentSession->dockVisibilityInitialized = true;
        }
        
        if (!m_restoringProjectUi)
            saveProjectDockVisibilityState();
        updateDockAreaActions();
    });

    connect(m_changesDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (m_currentSession) {
            m_currentSession->dockChangesVisible = visible;
            m_currentSession->dockVisibilityInitialized = true;
        }
        if (visible && m_document)
            refreshChangesView();
        
        if (!m_restoringProjectUi)
            saveProjectDockVisibilityState();
        updateDockAreaActions();
    });
    connect(m_sectionsDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (m_currentSession) {
            m_currentSession->dockSectionsVisible = visible;
            m_currentSession->dockVisibilityInitialized = true;
        }

        if (visible && hexEdit)
            m_sectionsDock->highlightOffset(hexEdit->cursorPosition() / 2);
        
        if (!m_restoringProjectUi)
            saveProjectDockVisibilityState();
        updateDockAreaActions();
    });

    connect(m_audioDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (m_currentSession) {
            m_currentSession->dockAudioVisible = visible;
            m_currentSession->dockVisibilityInitialized = true;
        }

        if (visible && hexEdit)
            syncDisplayDocksForOffset(hexEdit->cursorPosition() / 2);

        if (!m_restoringProjectUi)
            saveProjectDockVisibilityState();
        updateDockAreaActions();
    });

    connect(m_graphicsDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (m_currentSession) {
            m_currentSession->dockGraphicsVisible = visible;
            m_currentSession->dockVisibilityInitialized = true;
        }

        if (visible && hexEdit)
            syncDisplayDocksForOffset(hexEdit->cursorPosition() / 2);

        if (!m_restoringProjectUi)
            saveProjectDockVisibilityState();
        updateDockAreaActions();
    });

    // Wire up dock title bar collapse callbacks and separator event filters
    setupDockTitleBarCallbacks();
    installSeparatorEventFilters();

    createToolBars();
    createMenus();

    createStatusBar();

    defaultWindowState = saveState();

    setCurrentFile("");

    readSettings();

    updateActionStates();
    enforceBottomDockEqualWidth();

#ifdef Q_OS_MAC
    // Avoid Qt/AppKit startup layout crashes seen in the packaged macOS build.
    setUnifiedTitleAndToolBarOnMac(false);
#endif
}

// ---------- Session / tab management ----------

EditorSession *MainWindow::createSession()
{
    auto *session = new EditorSession;
    session->document = new HexDocument;
    session->editor = new HexEditor;
    session->isUntitled = true;

    // When the document's dirty flag changes, refresh the tab title star.
    session->document->setDirtyChangedCallback([this, session] {
        const int tabIdx = m_sessions.indexOf(session);
        if (tabIdx >= 0)
            updateTabTitle(tabIdx);
    });

    if (m_tabWidget->isHidden())
        m_tabWidget->show();

    int idx = m_tabWidget->addTab(session->editor, tr("New file"));
    m_sessions.append(session);
    m_tabWidget->setCurrentIndex(idx);  // triggers onTabChanged → connectEditorSignals

    // Apply current visual settings to the new editor
    updateHexEditorSettings();
    captureDefaultViewState(session);

    // Guarantee signals are connected for the new editor.
    // QTabWidget::addTab may fire currentChanged BEFORE m_sessions.append() runs
    // (for the very first tab), causing onTabChanged to return early without
    // connecting signals. Disconnect + reconnect is idempotent and ensures
    // exactly one connection regardless of Qt's internal signal-emission order.
    disconnectEditorSignals(session->editor);
    connectEditorSignals(session->editor);

    // If Qt already selected this tab during addTab(), setCurrentIndex(idx)
    // may not emit currentChanged again. In that case onTabChanged is skipped,
    // so force the MainWindow active session pointers to this tab.
    // Guard against early startup: during init() the first createSession()
    // runs before dock widgets are created, so restoreSession is not safe yet.
    if (m_tablesDock && m_pointersDock
            && m_currentSession != session
            && m_tabWidget->currentIndex() == idx) {
        if (m_currentSession && m_currentSession->editor)
            disconnectEditorSignals(m_currentSession->editor);
        saveCurrentSession();
        restoreSession(session);
    }

    if (m_sectionsDock) {
        m_sectionsDock->show();
        session->dockSectionsVisible = true;
        session->dockVisibilityInitialized = true;
    }

    return session;
}

void MainWindow::syncDisplayDocksForOffset(qint64 offset)
{
    if (!hexEdit || !m_sectionModel)
        return;

    const int idx = m_sectionModel->sectionIndexAtOffset(offset);
    const bool inGraphicsSection = idx >= 0 && m_sectionModel->at(idx).displayMode == SectionDisplay_Graphics;
    const bool inDefaultGraphicsView = idx >= 0
        && m_sectionModel->at(idx).displayMode == SectionDisplay_Default
        && hexEdit->showGraphicsPanel();
    const bool inAudioSection = idx >= 0 && m_sectionModel->at(idx).displayMode == SectionDisplay_Audio;

    if (m_graphicsDock && m_graphicsDock->isVisible()) {
        m_graphicsDock->setSectionActive(inGraphicsSection || inDefaultGraphicsView);
        if (idx >= 0) {
            const Section &sec = m_sectionModel->at(idx);
            if (sec.displayMode == SectionDisplay_Graphics) {
                m_graphicsDock->setCodec(sec.tileCodec);
                m_graphicsDock->setTileColsDisplay(sec.tileCols);
                m_graphicsDock->setPaletteColors(sec.palette);
            } else if (inDefaultGraphicsView) {
                m_graphicsDock->setCodec(hexEdit->globalTileCodec());
                m_graphicsDock->setTileColsDisplay(hexEdit->globalTileCols());
                m_graphicsDock->setPaletteColors({});
            }
        }
    }

    if (m_audioDock && m_audioDock->isVisible()) {
        m_audioDock->setSectionActive(inAudioSection || hexEdit->showAudioPanel());
        if (idx >= 0) {
            const Section &sec = m_sectionModel->at(idx);
            if (sec.displayMode == SectionDisplay_Audio) {
                const AudioSampleFormat fmt = audioFormatFromSubtypeMnemonic(sectionAudioSubtypeMnemonic(sec, m_detectedRomType));
                m_audioDock->setSelectedFormat(fmt);
                m_audioDock->setSampleRateText(QString::number(sectionAudioSampleRate(sec, m_detectedRomType)));
                m_audioDock->setPlaybackSpeed(sectionAudioSpeed(sec));
            } else if (hexEdit->showAudioPanel()) {
                m_audioDock->setSelectedFormat(hexEdit->globalAudioFormat());
            }
        } else if (hexEdit->showAudioPanel()) {
            m_audioDock->setSelectedFormat(hexEdit->globalAudioFormat());
        }
    }
}

void MainWindow::refreshPaletteSectionUiState()
{
    QVector<PaletteSectionPreview> paletteSections;
    QHash<qint64, QVector<QRgb>> palettePreviewColors;

    if (hexEdit && m_sectionModel) {
        paletteSections.reserve(m_sectionModel->count());
        for (int i = 0; i < m_sectionModel->count(); ++i) {
            const Section &section = m_sectionModel->at(i);
            if (section.displayMode != SectionDisplay_Palette)
                continue;

            const QVector<QRgb> colors = decodedPalettePreviewColors(
                hexEdit,
                m_sectionModel,
                i,
                section,
                m_detectedRomType);
            palettePreviewColors.insert(section.startOffset, colors);

            PaletteSectionPreview preview;
            preview.startOffset = section.startOffset;
            preview.name = section.name;
            preview.colors = colors;
            paletteSections.append(preview);
        }
    }

    if (m_sectionsDock)
        m_sectionsDock->setPalettePreviewColors(palettePreviewColors);
    if (m_graphicsDock)
        m_graphicsDock->setPaletteSections(paletteSections);
}

void MainWindow::connectEditorSignals(HexEditor *editor)
{
    connect(editor, &HexEditor::overwriteModeChanged, this, &MainWindow::setOverwriteMode);
    connect(editor, &HexEditor::dataChanged, this, &MainWindow::dataChanged);
    connect(editor, &HexEditor::dataChangedAt, this, &MainWindow::onHexDataChangedAt);
    connect(editor, &HexEditor::contextMenuRequested, this, &MainWindow::hexEditContextMenu);
    connect(editor, &HexEditor::selectionChanged, this, &MainWindow::setSelection);
    connect(editor, &HexEditor::currentAddressChanged, this, &MainWindow::setAddress);
    connect(editor, &HexEditor::currentAddressChanged,
            m_sectionsDock, &SectionsDockWidget::highlightOffset);
    connect(editor, &HexEditor::currentAddressChanged,
            this, &MainWindow::syncDisplayDocksForOffset);
    connect(editor, &HexEditor::currentSizeChanged, this, &MainWindow::setSize);
    connect(editor, &HexEditor::lineBreaksChanged, this, [this, editor]() {
        const bool restoring = m_restoringSession || m_restoringProjectUi;
        if (!restoring && m_document && editor == hexEdit
            && m_document->alignmentOffsets() != editor->lineBreaks())
        {
            m_document->markDirty();
        }
        if (removeVirtualFormattingAct)
            removeVirtualFormattingAct->setEnabled(editor && !editor->lineBreaks().isEmpty());
        if (m_currentSession)
            updateTabTitle(m_tabWidget->currentIndex());
    });
    connect(editor, &HexEditor::audioPlaybackToggled, this, [this](qint64 /*bytePos*/) {
        if (m_audioPlayer && m_audioPlayer->isPlaying())
            stopAudioPlayback();
        else
            playAudioAtCursor();
    });
}

void MainWindow::disconnectEditorSignals(HexEditor *editor)
{
    disconnect(editor, SIGNAL(overwriteModeChanged(bool)), this, SLOT(setOverwriteMode(bool)));
    disconnect(editor, SIGNAL(dataChanged()), this, SLOT(dataChanged()));
    disconnect(editor, &HexEditor::dataChangedAt, this, &MainWindow::onHexDataChangedAt);
    disconnect(editor, &HexEditor::contextMenuRequested, this, &MainWindow::hexEditContextMenu);
    disconnect(editor, SIGNAL(selectionChanged(qint64, qint64)), this, SLOT(setSelection(qint64, qint64)));
    disconnect(editor, SIGNAL(currentAddressChanged(qint64)), this, SLOT(setAddress(qint64)));
    disconnect(editor, &HexEditor::currentAddressChanged,
              m_sectionsDock, &SectionsDockWidget::highlightOffset);
    disconnect(editor, &HexEditor::currentAddressChanged,
              this, &MainWindow::syncDisplayDocksForOffset);
    disconnect(editor, SIGNAL(currentSizeChanged(qint64)), this, SLOT(setSize(qint64)));
    disconnect(editor, &HexEditor::lineBreaksChanged, this, nullptr);
    disconnect(editor, &HexEditor::audioPlaybackToggled, this, nullptr);
}

bool MainWindow::saveFile(const QString &fileName)
{
    auto &settings = AppSettings::instance();
    const bool autoFixChecksums = settings.value("AutoFixChecksums", false).toBool();

    QString tmpFileName = fileName + ".~tmp";

    QApplication::setOverrideCursor(Qt::WaitCursor);

    // If auto-fix is requested, apply checksum correction to a copy of the data
    // before writing so both the file and the editor reflect the corrected state.
    bool checksumFixed = false;
    if (autoFixChecksums) {
        QByteArray data = hexEdit->data();
        const ChecksumFixResult csResult = tryFixChecksum(data, m_detectedRomType);
        if (csResult.status == ChecksumFixStatus::Fixed) {
            checksumFixed = true;
            // Update only changed spans in editor data without polluting undo/redo history.
            const QByteArray oldData = hexEdit->data();
            const int common = qMin(data.size(), oldData.size());
            int i = 0;
            while (i < common) {
                if (data[i] == oldData[i]) {
                    ++i;
                    continue;
                }
                const int start = i;
                while (i < common && data[i] != oldData[i])
                    ++i;
                hexEdit->replaceNoUndo(start, i - start, data.mid(start, i - start));
            }
            // Refresh changes view to show recalculated checksums
            if (showChangesAct && showChangesAct->isChecked())
                refreshChangesView();
        }
    }

    QFile file(tmpFileName);

    bool ok = hexEdit->write(file);
    if (QFile::exists(fileName))
        ok = QFile::remove(fileName);
    if (ok)
    {
        file.setFileName(tmpFileName);
        ok = file.copy(fileName);
        if (ok)
            ok = QFile::remove(tmpFileName);
    }

    QApplication::restoreOverrideCursor();

    if (!ok) {
        QMessageBox::warning(this, QString::fromLatin1(AppInfo::Name),
                             tr("Cannot write file %1.")
                                 .arg(fileName));
        return false;
    }

    if (hexEdit)
        hexEdit->setModified(false);
    setCurrentFile(fileName);
    rememberDirectory(kLastFileDirKey, fileName);
    if (checksumFixed)
        statusBar()->showMessage(tr("File saved (checksums fixed)"), 3000);
    else
        statusBar()->showMessage(tr("File saved"), 2000);
    return true;
}

void MainWindow::updateWindowTitle()
{
    const bool hasProject = m_document && !m_document->projectName.isEmpty();
    const bool hasFile = !isUntitled && !curFile.isEmpty();

    if (hasProject && hasFile)
        setWindowTitle(QString("%1 — %2[*] - RTHextion")
                           .arg(m_document->projectName, strippedName(curFile)));
    else if (hasProject)
        setWindowTitle(QString("%1[*] - RTHextion").arg(m_document->projectName));
    else if (hasFile)
        setWindowTitle(QString("%1[*] - RTHextion").arg(strippedName(curFile)));
    else
        setWindowTitle(QStringLiteral("RTHextion"));

    // Refresh recent projects menu since filter depends on current project path
    updateRecentProjectMenu();
}

void MainWindow::setCurrentFile(const QString &fileName)
{
    curFile = QFileInfo(fileName).canonicalFilePath();
    if (curFile.isEmpty())
        curFile = fileName;
    isUntitled = fileName.isEmpty();
    setWindowModified(false);

    if (!fileName.isEmpty())
        addToRecentFiles(fileName);

    updateWindowTitle();
    updateActionStates();

    // Sync session and tab title
    if (m_currentSession) {
        m_currentSession->curFile = curFile;
        m_currentSession->isUntitled = isUntitled;
        updateTabTitle(m_tabWidget->currentIndex());
    }
}

bool MainWindow::maybeSaveProject()
{
    if (!m_document || !m_document->isDirty())
        return true;

    // Project data exists but has never been saved to a project file
    if (m_document->projectFilePath.isEmpty() && !hasProjectData())
        return true;

    // Show save dialog
    QMessageBox msg(QMessageBox::Warning, QString::fromLatin1(AppInfo::Name),
                    tr("The project has unsaved changes.\nDo you want to save the project?"), 
                    QMessageBox::NoButton, this);
    msg.addButton(tr("Save"), QMessageBox::AcceptRole);
    msg.addButton(tr("Discard"), QMessageBox::DestructiveRole);
    msg.addButton(tr("Cancel"), QMessageBox::RejectRole);
    msg.setDefaultButton(0);
    int result = msg.exec();

    QMessageBox::StandardButton stdResult = (result == 0) ? QMessageBox::Save : 
                                             (result == 1) ? QMessageBox::Discard : QMessageBox::Cancel;

    if (stdResult == QMessageBox::Save) {
        if (m_document->projectFilePath.isEmpty())
            return saveProjectAs();
        return saveProjectImpl(m_document->projectFilePath);
    }

    return stdResult != QMessageBox::Cancel;
}

bool MainWindow::hasProjectData() const
{
    if (!hexEdit)
        return false;
    if (m_tablesDock && m_tablesDock->count() > 0)
        return true;
    if (hexEdit->pointers() && !hexEdit->pointers()->empty())
        return true;
    if (!hexEdit->lineBreaks().isEmpty())
        return true;
    return false;
}

bool MainWindow::maybeSave()
{
    if (!isWindowModified())
        return true;

    QMessageBox msg(QMessageBox::Warning, QString::fromLatin1(AppInfo::Name),
                    tr("File has been modified.\nDo you want to save your changes?"), 
                    QMessageBox::NoButton, this);
    msg.addButton(tr("Save"), QMessageBox::AcceptRole);
    msg.addButton(tr("Discard"), QMessageBox::DestructiveRole);
    msg.addButton(tr("Cancel"), QMessageBox::RejectRole);
    msg.setDefaultButton(0);
    int result = msg.exec();
    QMessageBox::StandardButton stdResult = (result == 0) ? QMessageBox::Save : 
                                             (result == 1) ? QMessageBox::Discard : QMessageBox::Cancel;

    if (stdResult == QMessageBox::Save)
        return save();

    return stdResult != QMessageBox::Cancel;
}

void MainWindow::updateActionStates()
{
    const bool canModify = hexEdit && !hexEdit->isReadOnly() && !hexEdit->showOriginal();

    // Save is always enabled (for new files it will trigger Save As)
    saveAct->setEnabled(!m_sessions.isEmpty());
    closeAct->setEnabled(!m_sessions.isEmpty());
    undoAct->setEnabled(canModify && hexEdit->canUndo());
    redoAct->setEnabled(canModify && hexEdit->canRedo());

    // Save Project is only enabled after the project has been given a path
    if (saveProjectAct)
        saveProjectAct->setEnabled(m_document && !m_document->projectFilePath.isEmpty());

    if (removeVirtualFormattingAct)
        removeVirtualFormattingAct->setEnabled(hexEdit && !hexEdit->lineBreaks().isEmpty());

    // Create IPS patch only when project has tracked original bytes
    if (createIpsPatchAct)
        createIpsPatchAct->setEnabled(m_document && !m_document->originalBytes.isEmpty());

    // Show-original button enabled whenever the project has recorded original bytes
    if (m_changesDock)
        m_changesDock->setShowOriginalEnabled(m_document && !m_document->originalBytes.isEmpty());

    // Load original is available whenever a file is open
    if (loadOriginalAct)
        loadOriginalAct->setEnabled(!isUntitled && !curFile.isEmpty());
    
    const bool hasSelection = hexEdit && hexEdit->getRawSelection().size() > 1;
    const bool dumpEnabled = hasSelection;
    const bool insertEnabled = canModify;

    if (cutAct)
        cutAct->setEnabled(hasSelection && hexEdit && !hexEdit->overwriteMode() && canModify);
    if (pasteAct)
        pasteAct->setEnabled(canModify && !QApplication::clipboard()->text().isEmpty());
    if (editMenu)
        editMenu->setEnabled(canModify);

    if (scriptMenu)
        scriptMenu->setEnabled(dumpEnabled || insertEnabled);
    if (dumpScriptAct)
        dumpScriptAct->setEnabled(dumpEnabled);
    if (insertScriptAct)
        insertScriptAct->setEnabled(insertEnabled);
    if (toolbarDumpScriptAct)
        toolbarDumpScriptAct->setEnabled(dumpEnabled);
    if (toolbarInsertScriptAct)
        toolbarInsertScriptAct->setEnabled(insertEnabled);
}

QString MainWindow::strippedName(const QString &fullFileName)
{
    return QFileInfo(fullFileName).fileName();
}

bool MainWindow::shouldTrackChangedBytes() const
{
    const bool showInlineChanges = showChangesAct && showChangesAct->isChecked();
    const bool showChangesMap = showMapPointersAct && showMapPointersAct->isChecked();
    return showInlineChanges || showChangesMap;
}

void MainWindow::toggleShowChanges()
{
    if (!hexEdit || !showChangesAct)
        return;

    const bool show = showChangesAct->isChecked();
    hexEdit->setShowChanges(show);
    if (shouldTrackChangedBytes())
        updateChangedBytesHighlight();

    if (m_currentSession) {
        m_currentSession->showChanges = show;
        m_currentSession->changesHexMode = (m_changesDock && m_changesDock->hexMode());
    }
}

void MainWindow::updateChangedBytesHighlight()
{
    QSet<qint64> positions;
    if (!m_document || m_document->originalBytes.isEmpty()) {
        hexEdit->clearChangedRange();
        hexEdit->setChangedPositions(positions);
        return;
    }

    const QByteArray currentData = hexEdit->data();

    // Mark the expansion tail as changed only when the original file size is
    // explicitly known (set by IPS patch / Load Original).  For regular edits
    // originalFileSize stays -1 and no range highlight is needed.
    if (m_document->originalFileSize >= 0 && currentData.size() > m_document->originalFileSize) {
        hexEdit->setChangedRange(m_document->originalFileSize, currentData.size());
    } else {
        hexEdit->clearChangedRange();
    }

    // Compare original bytes with current data for the overlapping portion
    for (const auto &entry : m_document->originalBytes) {
        const qint64 offset = entry.first;
        const QByteArray &origBytes = entry.second;
        for (int i = 0; i < origBytes.size(); ++i) {
            const qint64 pos = offset + i;
            if (pos < currentData.size()) {
                if (currentData.at(pos) != origBytes.at(i))
                    positions.insert(pos);
            }
        }
    }
    hexEdit->setChangedPositions(positions);
}

void MainWindow::refreshChangesView()
{
    if (!m_document || !m_changesDock)
        return;

    TranslationTable *origTable = tableForViewMode(true);
    TranslationTable *activeTable = selectedTable();
    if (!activeTable)
        activeTable = tableForViewMode(false);

    m_changesDock->refresh(m_document->originalBytes, hexEdit->data(), origTable, activeTable,
                           useTableAct && useTableAct->isChecked(), m_currentEncoding);
}

TranslationTable *MainWindow::tableForViewMode(bool showOriginal) const
{
    if (!m_tablesDock)
        return nullptr;

    const auto &tabs = m_tablesDock->allTables();
    if (tabs.isEmpty())
        return nullptr;

    const int curIdx = m_tablesDock->currentIndex();
    TranslationTable *currentTable = nullptr;
    if (curIdx >= 0 && curIdx < tabs.size())
        currentTable = const_cast<TranslationTable *>(&tabs[curIdx].table);

    const int modeIdx = tableIndexForViewMode(showOriginal);
    if (modeIdx >= 0)
        return const_cast<TranslationTable *>(&tabs[modeIdx].table);

    if (showOriginal)
        return currentTable ? currentTable : const_cast<TranslationTable *>(&tabs.first().table);

    if (curIdx >= 0 && curIdx < tabs.size() && !tabs[curIdx].isOriginal)
        return const_cast<TranslationTable *>(&tabs[curIdx].table);

    for (const auto &tt : tabs) {
        if (!tt.isOriginal)
            return const_cast<TranslationTable *>(&tt.table);
    }

    return currentTable ? currentTable : const_cast<TranslationTable *>(&tabs.first().table);
}

int MainWindow::tableIndexForViewMode(bool showOriginal) const
{
    if (!m_tablesDock)
        return -1;

    const auto &tabs = m_tablesDock->allTables();
    if (tabs.isEmpty())
        return -1;

    const int curIdx = m_tablesDock->currentIndex();
    if (curIdx >= 0 && curIdx < tabs.size() && tabs[curIdx].isOriginal == showOriginal)
        return curIdx;

    for (int i = 0; i < tabs.size(); ++i) {
        if (tabs[i].isOriginal == showOriginal)
            return i;
    }

    return -1;
}

TranslationTable *MainWindow::selectedTable() const
{
    if (!m_tablesDock)
        return nullptr;

    const auto &tabs = m_tablesDock->allTables();
    if (tabs.isEmpty())
        return nullptr;

    const int curIdx = m_tablesDock->currentIndex();
    if (curIdx >= 0 && curIdx < tabs.size())
        return const_cast<TranslationTable *>(&tabs[curIdx].table);

    return const_cast<TranslationTable *>(&tabs.first().table);
}

void MainWindow::applySelectedTable()
{
    if (!hexEdit)
        return;

    if (!(useTableAct && useTableAct->isChecked())) {
        hexEdit->removeTranslationTable();
        return;
    }

    TranslationTable *table = selectedTable();
    if (table)
        hexEdit->setTranslationTable(table);
    else
        hexEdit->removeTranslationTable();
}

void MainWindow::applyTranslationTableForViewMode()
{
    if (!hexEdit)
        return;

    if (!(useTableAct && useTableAct->isChecked())) {
        hexEdit->removeTranslationTable();
        return;
    }

    TranslationTable *table = tableForViewMode(hexEdit->showOriginal());
    if (table)
        hexEdit->setTranslationTable(table);
    else
        hexEdit->removeTranslationTable();
}

bool MainWindow::shouldSwitchTableOnViewModeChange() const
{
    if (!m_tablesDock)
        return false;

    const auto &tabs = m_tablesDock->allTables();
    
    // If there's only one table or no tables, no need to switch
    if (tabs.size() <= 1)
        return false;

    // Check if there are tables with different isOriginal values
    bool hasOriginal = false;
    bool hasNonOriginal = false;
    
    for (const auto &tt : tabs) {
        if (tt.isOriginal)
            hasOriginal = true;
        else
            hasNonOriginal = true;
            
        if (hasOriginal && hasNonOriginal)
            return true;
    }
    
    // If we don't have both types, no need to switch
    return false;
}

void MainWindow::enforceBottomDockEqualWidth()
{
    if (!m_pointersDock || !m_changesDock)
        return;
    if (!m_pointersDock->isVisible() || !m_changesDock->isVisible())
        return;
    if (m_pointersDock->isFloating() || m_changesDock->isFloating())
        return;
    if (dockWidgetArea(m_pointersDock) != Qt::BottomDockWidgetArea ||
        dockWidgetArea(m_changesDock) != Qt::BottomDockWidgetArea)
        return;

    const int halfWidth = qMax(200, width() / 2);
    resizeDocks({m_pointersDock, m_changesDock}, {halfWidth, halfWidth}, Qt::Horizontal);
}

void MainWindow::updateEndiannes()
{
    // Cycle: Little-endian → Big-endian → Swapped bytes → Little-endian
    switch (hexEdit->byteOrder) {
    case ByteOrder::LittleEndian:
        hexEdit->byteOrder = ByteOrder::BigEndian;
        break;
    case ByteOrder::BigEndian:
        hexEdit->byteOrder = ByteOrder::SwappedBytes;
        break;
    case ByteOrder::SwappedBytes:
    default:
        hexEdit->byteOrder = ByteOrder::LittleEndian;
        break;
    }

    updateEndiannesLabel();
    setAddress(hexEdit->getCurrentOffset());
}

void MainWindow::updateEndiannesLabel()
{
    switch (hexEdit->byteOrder) {
    case ByteOrder::BigEndian:
        lbEndiannes->setText(tr("Big-endian"));
        break;
    case ByteOrder::SwappedBytes:
        lbEndiannes->setText(tr("Byte-swapped"));
        break;
    default:
        lbEndiannes->setText(tr("Little-endian"));
        break;
    }
}

void MainWindow::onRomTypeChanged(int index)
{
    m_detectedRomType = static_cast<RomType>(cbRomType->itemData(index).toInt());
    m_pointerOffset = defaultPointerOffset(m_detectedRomType);
    m_pointerSize = defaultPointerSize(m_detectedRomType);
    if (m_document) {
        m_document->setPointerOffset(m_pointerOffset);
        m_document->setPointerSize(m_pointerSize);
    }

    // Update byte order to match the new ROM type
    hexEdit->byteOrder = defaultByteOrder(m_detectedRomType);
    updateEndiannesLabel();
    setAddress(hexEdit->getCurrentOffset());
    syncRomTypeMenu(index);

    hexEdit->setDisasmRomType(m_detectedRomType);
    if (m_audioDock)
        m_audioDock->setRomType(m_detectedRomType);
    if (m_graphicsDock)
        m_graphicsDock->setRomType(m_detectedRomType);
}

void MainWindow::repopulateRomTypeCombo()
{
    if (!cbRomType) return;
    const int savedIndex = cbRomType->currentIndex();
    const QSignalBlocker blocker(cbRomType);
    cbRomType->clear();
    cbRomType->addItem(tr("Unknown"), 0);
    for (int i = 1; i < kRomTypeCount; ++i)
        cbRomType->addItem(tr(romTypeName(static_cast<RomType>(i))), i);
    cbRomType->setCurrentIndex(savedIndex >= 0 ? savedIndex : 0);
}

void MainWindow::onMenuRomTypeTriggered(QAction *action)
{
    int index = action->data().toInt();
    const QSignalBlocker blocker(cbRomType);
    cbRomType->setCurrentIndex(index);
    m_detectedRomType = static_cast<RomType>(index);
    m_pointerOffset = defaultPointerOffset(m_detectedRomType);
    m_pointerSize = defaultPointerSize(m_detectedRomType);

    if (m_document)
    {
        m_document->setPointerOffset(m_pointerOffset);
        m_document->setPointerSize(m_pointerSize);
    }

    hexEdit->byteOrder = defaultByteOrder(m_detectedRomType);
    updateEndiannesLabel();
    setAddress(hexEdit->getCurrentOffset());

    hexEdit->setDisasmRomType(m_detectedRomType);
    if (m_audioDock)
        m_audioDock->setRomType(m_detectedRomType);
    if (m_graphicsDock)
        m_graphicsDock->setRomType(m_detectedRomType);
}

void MainWindow::setCurrentPointerOffset(qint64 offset)
{
    m_pointerOffset = offset;
    if (m_document)
        m_document->setPointerOffset(m_pointerOffset);
    if (!m_restoringProjectUi && m_document && !m_document->projectFilePath.isEmpty())
        m_document->markDirty();
}

void MainWindow::setCurrentPointerSize(int size)
{
    if (size != 2 && size != 4)
        return;
    m_pointerSize = size;
    if (m_document)
        m_document->setPointerSize(m_pointerSize);
    if (!m_restoringProjectUi && m_document && !m_document->projectFilePath.isEmpty())
        m_document->markDirty();
}

void MainWindow::syncRomTypeMenu(int index)
{
    if (!romTypeMenuGroup) return;
    const auto actions = romTypeMenuGroup->actions();
    if (index >= 0 && index < actions.size())
        actions[index]->setChecked(true);
}

void MainWindow::onEncodingTriggered(QAction *action)
{
    m_currentEncoding = action->data().toString();
    hexEdit->setCurrentEncoding(m_currentEncoding);
    if (lbEncoding)
        lbEncoding->setText(m_currentEncoding);
}

void MainWindow::syncEncodingMenu()
{
    if (!encodingGroup) return;
    for (QAction *a : encodingGroup->actions()) {
        if (a->data().toString() == m_currentEncoding) {
            a->setChecked(true);
            break;
        }
    }
}

void MainWindow::openEncodingSelectionDialog()
{
    if (!encodingGroup || !hexEdit)
        return;

    const QList<QAction *> actions = encodingGroup->actions();
    if (actions.isEmpty())
        return;

    QStringList values;
    values.reserve(actions.size());
    int currentIndex = 0;

    for (int i = 0; i < actions.size(); ++i) {
        const QString value = actions[i]->data().toString();
        values.append(value);
        if (value == m_currentEncoding)
            currentIndex = i;
    }

    bool ok = false;
    const QString selected = QInputDialog::getItem(
        this,
        tr("Encoding"),
        tr("Select current encoding:"),
        values,
        currentIndex,
        false,
        &ok);

    if (!ok || selected.isEmpty() || selected == m_currentEncoding)
        return;

    for (QAction *action : actions) {
        if (action->data().toString() == selected) {
            onEncodingTriggered(action);
            syncEncodingMenu();
            break;
        }
    }
}

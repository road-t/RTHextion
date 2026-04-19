// Auto-extracted from mainwindow.cpp
#include "mainwindow.h"
#include "internal.h"
using namespace MainWindowInternal;
#include <QMenu>
#include <QAction>
#include <QClipboard>
#include <QApplication>
#include <QMessageBox>
#include <QInputDialog>
#include "FillWithDialog.h"
#include "VirtualFormatDialog.h"
#include "PointerListModel.h"
#include "SectionListModel.h"

void MainWindow::hexEditContextMenu(const QPoint &globalPos, qint64 bytePos)
{
    PointerListModel *model = hexEdit->pointers();
    const qint64 pointerStart = hexEdit->pointerStartAt(bytePos, currentPointerSize());
    const qint64 fileSize = hexEdit->dataSize();

    const bool hasSelection  = hexEdit->getSelectionEnd() - hexEdit->getSelectionBegin() > 1;
    const bool isOverwrite   = hexEdit->overwriteMode();
    const bool isReadOnly    = hexEdit->isReadOnly();
    const bool clickedAscii  = hexEdit->editAreaIsAscii();
    const bool clickedDisasm = hexEdit->showDisasm()
        || (m_sectionModel && m_sectionModel->displayModeAtOffset(bytePos) == SectionDisplay_Disasm);
    const bool canRemoveFromSection = hasSelection && canRemoveSelectionFromSection();

    auto addToSectionWithPrompt = [this, canRemoveFromSection]() {
        if (!canRemoveFromSection)
            return;
        bool ok = false;
        const QString name = QInputDialog::getText(
            this,
            tr("Add to section"),
            tr("New section name:"),
            QLineEdit::Normal,
            tr("Section %1").arg(m_sectionModel ? (m_sectionModel->count() + 1) : 1),
            &ok);
        if (!ok)
            return;
        const QString trimmed = name.trimmed();
        if (trimmed.isEmpty())
            return;
        removeSelectionFromSection(trimmed);
    };

    const qint64 ptrOffset = currentPointerOffset();

    struct PointerLengthOption {
        int size = 0;
        qint64 target = -1;
    };

    QVector<PointerLengthOption> addPointerOptions;
    addPointerOptions.reserve(2);
    for (int size : {2, 4})
    {
        if (bytePos < 0 || bytePos + size > fileSize)
            continue;

        const QByteArray rawPointer = hexEdit->dataAt(bytePos, size);
        if (rawPointer.size() != size)
            continue;

        const quint64 decodedPointer = decodePointer(reinterpret_cast<const uchar *>(rawPointer.constData()), size, hexEdit->byteOrder);
        const qint64 fileTarget = static_cast<qint64>(decodedPointer) + ptrOffset;
        addPointerOptions.append({size, fileTarget});
    }

    auto addPointerLengthMenu = [&](QMenu &menu, bool disableAll = false) -> QMap<QAction *, PointerLengthOption>
    {
        QMap<QAction *, PointerLengthOption> actions;
        QMenu *sub = menu.addMenu(tr("Add as pointer"));
        for (const PointerLengthOption &opt : addPointerOptions)
        {
            const bool valid = (opt.target >= 0 && opt.target <= fileSize);
            const QString targetStr = valid
                ? QStringLiteral("0x") + QString::number(opt.target, 16).toUpper()
                : tr("out of range");
            QAction *act = sub->addAction(tr("%1-byte \u2192 %2").arg(opt.size).arg(targetStr));
            act->setEnabled(valid);
            actions.insert(act, opt);
        }
        sub->setEnabled(!addPointerOptions.isEmpty() && !disableAll);
        return actions;
    };

    auto refreshPointersUi = [this]()
    {
        if (pointersDialog)
            pointersDialog->refreshFromTable();
        pointersUpdated();
        hexEdit->viewport()->update();
    };

    auto renameOffsetWithPrompt = [&](qint64 targetOffset)
    {
        if (!model || targetOffset < 0)
            return;

        bool ok = false;
        const QString currentName = model->offsetName(targetOffset);
        const QString name = QInputDialog::getText(
            this,
            tr("Rename offset"),
            tr("Offset name:"),
            QLineEdit::Normal,
            currentName,
            &ok);
        if (!ok)
            return;

        if (model->setOffsetName(targetOffset, name))
            refreshPointersUi();
    };

    auto selectedPointerOffsetsForDrop = [&]() -> QVector<qint64>
    {
        QVector<qint64> result;
        if (!hasSelection || !model)
            return result;

        const qint64 selBegin = hexEdit->getSelectionBegin();
        const qint64 selEnd = hexEdit->getSelectionEnd();
        if (selEnd <= selBegin)
            return result;

        QSet<qint64> unique;
        const QList<qint64> keys = model->pointerKeys();
        unique.reserve(keys.size());

        for (qint64 ptrOfs : keys)
        {
            const qint64 targetOfs = model->getOffset(ptrOfs);
            const bool pointerInSelection = (ptrOfs >= selBegin && ptrOfs < selEnd);
            const bool targetInSelection = (targetOfs >= selBegin && targetOfs < selEnd);
            if (pointerInSelection || targetInSelection)
                unique.insert(ptrOfs);
        }

        result.reserve(unique.size());
        for (qint64 ptrOfs : unique)
            result.append(ptrOfs);
        return result;
    };

    auto handleFindPtrAction = [&](QAction *chosen, QAction *quickSearch, QAction *findPtr) -> bool
    {
        if (!chosen) return false;
        if (chosen == quickSearch)
        {
            if (!pointersDialog)
            {
                pointersDialog = new PointersDialog(hexEdit, this);
                connect(pointersDialog, &QDialog::accepted, this, &MainWindow::pointersUpdated);
                connect(pointersDialog, &PointersDialog::searchCompleted, this, &MainWindow::onQuickSearchCompleted);
                pointersDialog->setDock(m_pointersDock);
            }
            pointersDialog->setHexEdit(hexEdit);
            if (m_currentSession) {
                PointersDialog::State ps;
                ps.searchDir        = m_currentSession->ptrSearchDir;
                ps.excludeSelection = m_currentSession->ptrExcludeSelection;
                ps.alignedOnly      = m_currentSession->ptrAlignedOnly;
                ps.optimize         = m_currentSession->ptrOptimize;
                pointersDialog->setDialogState(ps);
            }
            m_pointersDock->show();
            pointersDialog->quickSearch(bytePos);
            return true;
        }
        if (chosen == findPtr)
        {
            showPointersDialog();
            return true;
        }
        return false;
    };

    // --- Helper: clipboard has pasteable hex data ---
    const QClipboard *clipboard = QApplication::clipboard();
    const bool canPaste = !isReadOnly && !clipboard->text().isEmpty();

    // --- Helper: add common clipboard + address actions to a menu ---
    // Returns pointers to created actions via output params (nullptr when skipped).
    struct ClipboardActions {
        QAction *copyAddress = nullptr;
        QAction *cut = nullptr;
        QAction *copy = nullptr;
        QAction *copyToNewTab = nullptr;
        QAction *paste = nullptr;
    };

    auto addClipboardActions = [&](QMenu &menu, bool isHexArea = false) -> ClipboardActions
    {
        ClipboardActions acts;

        menu.addSeparator();
        // Copy address is always available
        acts.copyAddress = menu.addAction(tr("Copy address"));

        menu.addSeparator();
        acts.cut  = menu.addAction(tr("Cut"));
        acts.cut->setShortcut(QKeySequence::Cut);
        acts.cut->setEnabled(hasSelection && !isReadOnly && !isOverwrite);

        if (isHexArea && !clickedDisasm)
            acts.copy = menu.addAction(tr("Copy hex values"));
        else
            acts.copy = menu.addAction(tr("Copy"));
        acts.copy->setShortcut(QKeySequence::Copy);
        acts.copyToNewTab = menu.addAction(tr("Copy to a new tab"));
        acts.copyToNewTab->setEnabled(hasSelection);
        if (isHexArea)
            acts.paste = menu.addAction(tr("Paste hex values"));
        else
            acts.paste = menu.addAction(tr("Paste"));
        acts.paste->setShortcut(QKeySequence::Paste);
        acts.paste->setEnabled(!isReadOnly && canPaste);

        return acts;
    };

    // --- Helper: handle clipboard action result ---
    auto handleClipboardAction = [&](QAction *chosen, const ClipboardActions &acts)
    {
        if (!chosen)
            return false;

        if (chosen == acts.copyAddress)
        {
            // Keep "0x" prefix lowercase, uppercase only the hex digits
            const QString addrText = QStringLiteral("0x") + QString("%1").arg(bytePos, 8, 16, QChar('0')).toUpper();
            QApplication::clipboard()->setText(addrText);
            return true;
        }

        if (chosen == acts.cut && acts.cut)
        {
            // Cut: copy selection to clipboard, remove bytes (INSERT) or zero-fill (REPLACE)
            const qint64 selBegin = hexEdit->getSelectionBegin();
            const qint64 selEnd   = hexEdit->getSelectionEnd();
            const QByteArray raw = hexEdit->dataAt(selBegin, selEnd - selBegin);

            if (clickedAscii)
            {
                // ASCII area: use active table, otherwise current text encoding
                QApplication::clipboard()->setText(hexEdit->decodeTextForCurrentEncoding(raw));
            }
            else
            {
                // Hex area: space-separated uppercase hex pairs
                QApplication::clipboard()->setText(QString::fromLatin1(raw.toHex(' ')).toUpper());
            }

            if (isOverwrite)
            {
                // In overwrite mode cut is disabled, but just in case:
                hexEdit->replace(selBegin, selEnd - selBegin, QByteArray(static_cast<int>(selEnd - selBegin), char(0)));
            }
            else
            {
                hexEdit->remove(selBegin, selEnd - selBegin);
            }

            hexEdit->setCursorPosition(2 * selBegin);
            hexEdit->ensureVisible();
            return true;
        }

        if (chosen == acts.copy && acts.copy)
        {
            qint64 selBegin = hexEdit->getSelectionBegin();
            qint64 selEnd   = hexEdit->getSelectionEnd();
            if (selEnd - selBegin < 1) {
                // No selection — copy the single byte at cursor
                selBegin = bytePos;
                selEnd   = bytePos + 1;
            }
            const QByteArray raw = hexEdit->dataAt(selBegin, selEnd - selBegin);

            const qint64 selLast = qMax<qint64>(selBegin, selEnd - 1);
            const bool clickedDisasm = hexEdit->showDisasm()
                || (m_sectionModel && (m_sectionModel->displayModeAtOffset(selBegin) == SectionDisplay_Disasm
                    || m_sectionModel->displayModeAtOffset(selLast) == SectionDisplay_Disasm));

            if (clickedDisasm)
            {
                const QString disasmText = hexEdit->selectedDisasmText();
                if (!disasmText.isEmpty()) {
                    QApplication::clipboard()->setText(disasmText);
                    return true;
                }
                QApplication::clipboard()->setText(hexEdit->decodeTextForCurrentEncoding(raw));
            }
            else if (clickedAscii)
            {
                // ASCII area: use active table, otherwise current text encoding
                QApplication::clipboard()->setText(hexEdit->decodeTextForCurrentEncoding(raw));
            }
            else
            {
                // Hex area: space-separated uppercase hex pairs
                QApplication::clipboard()->setText(QString::fromLatin1(raw.toHex(' ')).toUpper());
            }
            return true;
        }

        if (chosen == acts.copyToNewTab && acts.copyToNewTab)
        {
            const qint64 selBegin = hexEdit->getSelectionBegin();
            const qint64 selEnd   = hexEdit->getSelectionEnd();
            const QByteArray raw = hexEdit->dataAt(selBegin, selEnd - selBegin);

            // Remember source settings before tab switch
            const auto srcByteOrder = hexEdit->byteOrder;
            const auto srcRomType = m_detectedRomType;

            // Create new tab and paste data
            newFile();
            hexEdit->insert(0, raw);
            hexEdit->setCursorPosition(0);

            // Copy settings from source tab
            hexEdit->byteOrder = srcByteOrder;
            m_detectedRomType = srcRomType;
            m_pointerOffset = defaultPointerOffset(srcRomType);
            m_pointerSize = defaultPointerSize(srcRomType);
            if (cbRomType) {
                const QSignalBlocker blocker(cbRomType);
                cbRomType->setCurrentIndex(static_cast<int>(srcRomType));
            }
            syncRomTypeMenu(static_cast<int>(srcRomType));
            updateEndiannesLabel();
            return true;
        }

        if (chosen == acts.paste && acts.paste)
        {
            // Decode clipboard depending on the area that was right-clicked
            QByteArray ba;
            if (clickedAscii)
            {
                // ASCII area: use active table, otherwise current text encoding
                ba = hexEdit->encodeTextForCurrentEncoding(QApplication::clipboard()->text());
            }
            else
            {
                // Hex area: strip whitespace then decode hex pairs (e.g. "AF 00 91" or "AF0091")
                const QString stripped = QApplication::clipboard()->text()
                                             .remove(' ').remove('\t').remove('\n').remove('\r');
                ba = QByteArray::fromHex(stripped.toLatin1());
            }

            if (ba.isEmpty())
                return true;

            if (isOverwrite)
            {
                // REPLACE mode
                if (hasSelection)
                {
                    // REPLACE with selection: truncate paste to selection size, paste at selection beginning
                    const qint64 selBegin = hexEdit->getSelectionBegin();
                    const qint64 selLen   = hexEdit->getSelectionEnd() - selBegin;
                    ba = ba.left(static_cast<int>(selLen));
                    hexEdit->replace(selBegin, static_cast<int>(ba.size()), ba);
                    hexEdit->setCursorPosition(2 * (selBegin + ba.size()));
                }
                else
                {
                    // REPLACE without selection: paste at clicked position
                    ba = ba.left(static_cast<int>(std::min<qint64>(ba.size(), fileSize - bytePos)));
                    hexEdit->replace(bytePos, static_cast<int>(ba.size()), ba);
                    hexEdit->setCursorPosition(2 * (bytePos + ba.size()));
                }
            }
            else
            {
                // INSERT mode
                if (hasSelection)
                {
                    // INSERT with selection: delete entire selection, then insert paste at selection beginning
                    const qint64 selBegin = hexEdit->getSelectionBegin();
                    const qint64 selLen = hexEdit->getSelectionEnd() - selBegin;
                    hexEdit->remove(selBegin, static_cast<int>(selLen));
                    hexEdit->insert(selBegin, ba);
                    hexEdit->setCursorPosition(2 * (selBegin + ba.size()));
                }
                else
                {
                    // INSERT without selection: insert at clicked position
                    hexEdit->insert(bytePos, ba);
                    hexEdit->setCursorPosition(2 * (bytePos + ba.size()));
                }
            }
            hexEdit->ensureVisible();
            return true;
        }

        return false;
    };

    // 0) Original-view mode: minimal read-only context menu.
    if (hexEdit->showOriginal())
    {
        QMenu menu(this);
        const QString addrText = QStringLiteral("0x") + QString("%1").arg(bytePos, 8, 16, QChar('0')).toUpper();

        QAction *copyAddrAct = menu.addAction(tr("Copy address"));

        // Copy selection is always available
        QAction *copySelAct = menu.addAction(tr("Copy"));
        copySelAct->setShortcut(QKeySequence::Copy);
        copySelAct->setEnabled(hasSelection);

        QAction *saveAsDumpAct = nullptr;
        if (clickedAscii)
            saveAsDumpAct = menu.addAction(tr("Save as dump"));

        // Revert to original is still useful even in original view —
        // it writes the original byte back into the current (edited) data.
        QAction *revertOrigAct = nullptr;
        if (bytePos >= 0 && m_document && !m_document->originalBytes.isEmpty())
        {
            for (const auto &entry : m_document->originalBytes)
            {
                const qint64 off = entry.first;
                const QByteArray &orig = entry.second;
                if (bytePos >= off && bytePos < off + orig.size())
                {
                    const uint8_t origByte = static_cast<uint8_t>(orig.at(bytePos - off));
                    menu.addSeparator();
                    revertOrigAct = menu.addAction(
                        tr("Revert to original: %1")
                            .arg(QString::number(origByte, 16).toUpper().rightJustified(2, QLatin1Char('0'))));
                    break;
                }
            }
        }

        QAction *chosen = menu.exec(globalPos);
        if (chosen == copyAddrAct)
        {
            QApplication::clipboard()->setText(addrText);
        }
        else if (chosen == copySelAct && hasSelection)
        {
            const qint64 selBegin = hexEdit->getSelectionBegin();
            const qint64 selEnd   = hexEdit->getSelectionEnd();
            const QByteArray raw = hexEdit->dataAt(selBegin, selEnd - selBegin);
            if (clickedAscii)
                QApplication::clipboard()->setText(hexEdit->decodeTextForCurrentEncoding(raw));
            else
                QApplication::clipboard()->setText(QString::fromLatin1(raw.toHex(' ')).toUpper());
        }
        else if (saveAsDumpAct && chosen == saveAsDumpAct)
        {
            dumpScript();
        }
        else if (revertOrigAct && chosen == revertOrigAct)
        {
            for (int ei = 0; ei < m_document->originalBytes.size(); ++ei)
            {
                auto &entry = m_document->originalBytes[ei];
                const qint64 off = entry.first;
                QByteArray &orig = entry.second;
                if (bytePos >= off && bytePos < off + orig.size())
                {
                    const char origByte = orig.at(static_cast<int>(bytePos - off));
                    hexEdit->replace(bytePos, 1, QByteArray(1, origByte));
                    const int localIdx = static_cast<int>(bytePos - off);
                    if (orig.size() == 1) {
                        m_document->originalBytes.removeAt(ei);
                    } else if (localIdx == 0) {
                        entry.first += 1;
                        orig.remove(0, 1);
                    } else if (localIdx == orig.size() - 1) {
                        orig.chop(1);
                    } else {
                        const QByteArray tail = orig.mid(localIdx + 1);
                        orig.truncate(localIdx);
                        m_document->originalBytes.insert(ei + 1, {bytePos + 1, tail});
                    }
                    break;
                }
            }
            if (showChangesAct->isChecked())
                updateChangedBytesHighlight();
            updateActionStates();
        }
        return;
    }

    // 2) Right click on offset that has incoming pointers (PRIORITY: checked first).
    if (model->hasOffset(bytePos) && hexEdit->showPointers())
    {
        QMenu menu(this);
        QAction *quickSearchAct2 = menu.addAction(tr("Quick pointer search"));
        QAction *findPtrAct2     = menu.addAction(tr("Find pointers") + QString("..."));
        menu.addSeparator();

        const QList<qint64> ptrs = model->getPointers(bytePos);
        QList<QAction *> ptrActs;
        ptrActs.reserve(ptrs.size());
        QAction *jumpToPointerAct = nullptr;

        if (ptrs.size() == 1)
        {
            jumpToPointerAct = menu.addAction(
                tr("Jump to pointer") + QStringLiteral(": 0x%1")
                    .arg(ptrs[0], 8, 16, QChar('0')).toUpper());
        }
        else
        {
            QMenu *subJmp = menu.addMenu(tr("Jump to pointer"));
            for (const qint64 ptr : ptrs)
            {
                QAction *ptrAct = subJmp->addAction(
                    QStringLiteral("0x%1").arg(ptr, 8, 16, QChar('0')).toUpper());
                ptrActs.append(ptrAct);
            }
        }

        menu.addSeparator();
        QAction *dropAllAct = menu.addAction(tr("Drop pointers"));
        QAction *renameOffsetAct = menu.addAction(tr("Rename offset"));
        QAction *dropSelectionPtrsAct = nullptr;
        QVector<qint64> dropSelectionPtrs;
        if (hasSelection)
        {
            dropSelectionPtrs = selectedPointerOffsetsForDrop();
            dropSelectionPtrsAct = menu.addAction(tr("Drop all pointers"));
            dropSelectionPtrsAct->setEnabled(!dropSelectionPtrs.isEmpty());
        }
        QAction *editScriptAct2 = nullptr;
        QMap<QAction *, PointerLengthOption> addPointerActs;
        if (!hasSelection)
            addPointerActs = addPointerLengthMenu(menu);

        auto clipActs = addClipboardActions(menu, !clickedAscii);

        QAction *fillWithAct2 = nullptr;
        QAction *vfFormatAct2 = nullptr;
        QAction *vfRemoveAct2 = nullptr;
        QAction *addToSectionAct2 = nullptr;
        if (hasSelection) {
            menu.addSeparator();
            editScriptAct2 = menu.addAction(tr("Edit script..."));
            if (!isReadOnly)
                fillWithAct2 = menu.addAction(tr("Fill with") + "...");
            vfFormatAct2 = menu.addAction(tr("Virtually format") + "...");
            vfRemoveAct2 = menu.addAction(tr("Remove virtual formatting"));
            addToSectionAct2 = menu.addAction(tr("Add to section"));
            addToSectionAct2->setEnabled(canRemoveFromSection);
        }

        QAction *chosen = menu.exec(globalPos);
        if (!chosen)
            return;

        if (handleClipboardAction(chosen, clipActs))
            return;
        if (handleFindPtrAction(chosen, quickSearchAct2, findPtrAct2))
            return;

        if (editScriptAct2 && chosen == editScriptAct2)
        {
            dumpScript();
            return;
        }

        if (fillWithAct2 && chosen == fillWithAct2)
        {
            const qint64 selBegin = hexEdit->getSelectionBegin();
            const qint64 selLen   = hexEdit->getSelectionEnd() - selBegin;
            const QVector<TableTab> tables = m_tablesDock ? m_tablesDock->allTables() : QVector<TableTab>();
            FillWithDialog dlg(selLen, tables, this);
            if (dlg.exec() == QDialog::Accepted) {
                const QByteArray unit = dlg.fillByte();
                const int len = dlg.fillLength();
                QByteArray fill;
                fill.reserve(len);
                for (int i = 0; i < len; i += unit.size())
                    fill.append(unit.left(qMin(unit.size(), len - i)));
                fill.truncate(len);
                hexEdit->replace(selBegin, len, fill);
                hexEdit->setCursorPosition(2 * (selBegin + len));
                hexEdit->ensureVisible();
            }
            return;
        }

        if (vfFormatAct2 && chosen == vfFormatAct2)
        {
            showVirtualFormatDialog(hexEdit->getSelectionBegin(), hexEdit->getSelectionEnd());
            return;
        }

        if (vfRemoveAct2 && chosen == vfRemoveAct2)
        {
            removeVirtualFormatting(hexEdit->getSelectionBegin(), hexEdit->getSelectionEnd());
            return;
        }

        if (addToSectionAct2 && chosen == addToSectionAct2)
        {
            addToSectionWithPrompt();
            return;
        }

        if (addPointerActs.contains(chosen))
        {
            const PointerLengthOption opt = addPointerActs.value(chosen);
            if (hexEdit->addPointerUndoable(bytePos, opt.target, opt.size))
                refreshPointersUi();
            return;
        }

        const int ptrIdx = ptrActs.indexOf(chosen);
        if (jumpToPointerAct && chosen == jumpToPointerAct)
        {
            hexEdit->setCursorPosition(ptrs[0] * 2);
            hexEdit->ensureVisible();
            return;
        }

        if (ptrIdx >= 0)
        {
            const qint64 ptrOffset = ptrs[ptrIdx];
            hexEdit->setCursorPosition(ptrOffset * 2);
            hexEdit->ensureVisible();
            return;
        }

        if (chosen == dropAllAct)
        {
            QMessageBox confirm(QMessageBox::Question,
                                QString(),
                                tr("Drop all pointers to this offset?"),
                                QMessageBox::Yes | QMessageBox::Cancel,
                                this);
            if (confirm.exec() == QMessageBox::Yes)
            {
                if (hexEdit->removePointersToOffsetUndoable(bytePos) > 0)
                    refreshPointersUi();
            }
        }
        else if (chosen == renameOffsetAct)
        {
            renameOffsetWithPrompt(bytePos);
        }
        else if (dropSelectionPtrsAct && chosen == dropSelectionPtrsAct)
        {
            if (hexEdit->removePointersUndoable(dropSelectionPtrs) > 0)
                refreshPointersUi();
        }
        return;
    }

    // 1) Right click on a pointer entry (full pointer length is clickable) —
    //    only if NOT also an offset target (offset takes priority).
    if (pointerStart >= 0 && hexEdit->showPointers() && !model->hasOffset(bytePos))
    {
        QMenu menu(this);
        QAction *quickSearchAct1 = menu.addAction(tr("Quick pointer search"));
        QAction *findPtrAct1     = menu.addAction(tr("Find pointers") + QString("..."));
        menu.addSeparator();
        QAction *jumpAct = menu.addAction(tr("Jump to offset"));

        // If this pointer byte is also a data target, allow jumping to the source pointer
        QAction *jumpToPointerFromPtrAct = nullptr;
        QList<QAction *> jumpToPointerFromPtrActs;
        QList<qint64> ptrsToCurrent;
        if (model->hasOffset(bytePos))
        {
            ptrsToCurrent = model->getPointers(bytePos);
            if (ptrsToCurrent.size() == 1)
            {
                jumpToPointerFromPtrAct = menu.addAction(
                    tr("Jump to pointer") + QStringLiteral(": 0x%1")
                        .arg(ptrsToCurrent[0], 8, 16, QChar('0')));
            }
            else
            {
                QMenu *subJmp = menu.addMenu(tr("Jump to pointer"));
                for (const qint64 ptr : ptrsToCurrent)
                {
                    QAction *a = subJmp->addAction(
                        QStringLiteral("0x%1").arg(ptr, 8, 16, QChar('0')));
                    jumpToPointerFromPtrActs.append(a);
                }
            }
        }

        QAction *dropAct = menu.addAction(tr("Drop pointer"));
        QAction *dropSelectionPtrsAct = nullptr;
        QVector<qint64> dropSelectionPtrs;
        if (hasSelection)
        {
            dropSelectionPtrs = selectedPointerOffsetsForDrop();
            dropSelectionPtrsAct = menu.addAction(tr("Drop pointers"));
            dropSelectionPtrsAct->setEnabled(!dropSelectionPtrs.isEmpty());
        }
        menu.addSeparator();
        QAction *editScriptAct1 = nullptr;
        QMap<QAction *, PointerLengthOption> addPointerActs;
        if (!hasSelection)
            addPointerActs = addPointerLengthMenu(menu, /*disableAll=*/true); // existing pointer — disable

        auto clipActs = addClipboardActions(menu, !clickedAscii);

        QAction *fillWithAct1 = nullptr;
        QAction *vfFormatAct1 = nullptr;
        QAction *vfRemoveAct1 = nullptr;
        QAction *addToSectionAct1 = nullptr;
        if (hasSelection) {
            menu.addSeparator();
            editScriptAct1 = menu.addAction(tr("Edit script..."));
            if (!isReadOnly)
                fillWithAct1 = menu.addAction(tr("Fill with") + "...");
            vfFormatAct1 = menu.addAction(tr("Virtually format") + "...");
            vfRemoveAct1 = menu.addAction(tr("Remove virtual formatting"));
            addToSectionAct1 = menu.addAction(tr("Add to section"));
            addToSectionAct1->setEnabled(canRemoveFromSection);
        }

        QAction *chosen = menu.exec(globalPos);
        if (!chosen)
            return;
        if (handleClipboardAction(chosen, clipActs))
            return;
        if (handleFindPtrAction(chosen, quickSearchAct1, findPtrAct1))
            return;

        if (chosen == jumpAct)
        {
            const qint64 targetOffset = model->getOffset(pointerStart);
            if (targetOffset >= 0)
            {
                hexEdit->setCursorPosition(targetOffset * 2);
                hexEdit->ensureVisible();
            }
            return;
        }

        if (jumpToPointerFromPtrAct && chosen == jumpToPointerFromPtrAct)
        {
            hexEdit->setCursorPosition(ptrsToCurrent[0] * 2);
            hexEdit->ensureVisible();
            return;
        }
        {
            const int idx = jumpToPointerFromPtrActs.indexOf(chosen);
            if (idx >= 0)
            {
                hexEdit->setCursorPosition(ptrsToCurrent[idx] * 2);
                hexEdit->ensureVisible();
                return;
            }
        }

        if (chosen == dropAct)
        {
            if (hexEdit->removePointerUndoable(pointerStart))
                refreshPointersUi();
            return;
        }

        if (dropSelectionPtrsAct && chosen == dropSelectionPtrsAct)
        {
            if (hexEdit->removePointersUndoable(dropSelectionPtrs) > 0)
                refreshPointersUi();
            return;
        }

        if (editScriptAct1 && chosen == editScriptAct1)
        {
            dumpScript();
            return;
        }

        if (fillWithAct1 && chosen == fillWithAct1)
        {
            const qint64 selBegin = hexEdit->getSelectionBegin();
            const qint64 selLen   = hexEdit->getSelectionEnd() - selBegin;
            const QVector<TableTab> tables = m_tablesDock ? m_tablesDock->allTables() : QVector<TableTab>();
            FillWithDialog dlg(selLen, tables, this);
            if (dlg.exec() == QDialog::Accepted) {
                const QByteArray unit = dlg.fillByte();
                const int len = dlg.fillLength();
                QByteArray fill;
                fill.reserve(len);
                for (int i = 0; i < len; i += unit.size())
                    fill.append(unit.left(qMin(unit.size(), len - i)));
                fill.truncate(len);
                hexEdit->replace(selBegin, len, fill);
                hexEdit->setCursorPosition(2 * (selBegin + len));
                hexEdit->ensureVisible();
            }
            return;
        }

        if (vfFormatAct1 && chosen == vfFormatAct1)
        {
            showVirtualFormatDialog(hexEdit->getSelectionBegin(), hexEdit->getSelectionEnd());
            return;
        }

        if (vfRemoveAct1 && chosen == vfRemoveAct1)
        {
            removeVirtualFormatting(hexEdit->getSelectionBegin(), hexEdit->getSelectionEnd());
            return;
        }

        if (addToSectionAct1 && chosen == addToSectionAct1)
        {
            addToSectionWithPrompt();
            return;
        }

        if (addPointerActs.contains(chosen))
        {
            const PointerLengthOption opt = addPointerActs.value(chosen);
            if (hexEdit->addPointerUndoable(bytePos, opt.target, opt.size))
                refreshPointersUi();
        }
        return;
    }

    // 3) Default hex-area context menu.
    QMenu menu(this);

    QAction *quickSearchAct = menu.addAction(tr("Quick pointer search"));
    QAction *findPtrAct     = menu.addAction(tr("Find pointers") + QString("..."));
    QMap<QAction *, PointerLengthOption> addPointerActs;
    if (!hasSelection)
        addPointerActs = addPointerLengthMenu(menu);
    QAction *dropSelectionPtrsAct = nullptr;
    QVector<qint64> dropSelectionPtrs;
    if (hasSelection)
    {
        dropSelectionPtrs = selectedPointerOffsetsForDrop();
        dropSelectionPtrsAct = menu.addAction(tr("Drop pointers"));
        dropSelectionPtrsAct->setEnabled(!dropSelectionPtrs.isEmpty());
    }

    QAction *saveAsDumpAct = nullptr;
    if (clickedAscii)
    {
        menu.addSeparator();
        saveAsDumpAct = menu.addAction(tr("Save as dump"));
    }

    auto clipActs = addClipboardActions(menu, !clickedAscii);  // Show hex labels only in hex area

    // "Revert to original" — shown when bytePos is in project originalBytes
    QAction *revertOrigAct = nullptr;
    if (bytePos >= 0 && m_document && !m_document->originalBytes.isEmpty())
    {
        for (const auto &entry : m_document->originalBytes)
        {
            const qint64 off = entry.first;
            const QByteArray &orig = entry.second;
            if (bytePos >= off && bytePos < off + orig.size())
            {
                const uint8_t origByte = static_cast<uint8_t>(orig.at(bytePos - off));
                menu.addSeparator();
                revertOrigAct = menu.addAction(
                    tr("Revert to original: %1")
                        .arg(QString::number(origByte, 16).toUpper().rightJustified(2, QLatin1Char('0'))));
                break;
            }
        }
    }

    QAction *editScriptAct3 = nullptr;
    QAction *fillWithAct3 = nullptr;
    QAction *vfFormatAct3 = nullptr;
    QAction *vfRemoveAct3 = nullptr;
    QAction *addToSectionAct3 = nullptr;
    if (hasSelection) {
        menu.addSeparator();
        editScriptAct3 = menu.addAction(tr("Edit script..."));
        if (!isReadOnly)
            fillWithAct3 = menu.addAction(tr("Fill with") + "...");
        vfFormatAct3 = menu.addAction(tr("Virtually format") + "...");
        vfRemoveAct3 = menu.addAction(tr("Remove virtual formatting"));
        addToSectionAct3 = menu.addAction(tr("Add to section"));
        addToSectionAct3->setEnabled(canRemoveFromSection);
    }

    // ── Audio actions ──
    const bool cursorInAudio = m_sectionModel
        && m_sectionModel->displayModeAtOffset(bytePos) == SectionDisplay_Audio;
    QAction *playAudioAct = nullptr;
    QAction *exportAudioAct = nullptr;
    QAction *importAudioAct = nullptr;
    if (cursorInAudio) {
        menu.addSeparator();
        playAudioAct = menu.addAction(tr("Play audio sample"));
        exportAudioAct = menu.addAction(tr("Export audio sample (WAV)"));
        if (!isReadOnly)
            importAudioAct = menu.addAction(tr("Import audio sample (WAV)"));
    }

    QAction *chosen = menu.exec(globalPos);
    if (handleClipboardAction(chosen, clipActs))
        return;

    if (revertOrigAct && chosen == revertOrigAct)
    {
        // Find the entry, write back original byte, then remove it from tracking
        for (int ei = 0; ei < m_document->originalBytes.size(); ++ei)
        {
            auto &entry = m_document->originalBytes[ei];
            const qint64 off = entry.first;
            QByteArray &orig = entry.second;
            if (bytePos >= off && bytePos < off + orig.size())
            {
                const char origByte = orig.at(static_cast<int>(bytePos - off));
                hexEdit->replace(bytePos, 1, QByteArray(1, origByte));

                // Remove this byte from the tracked range, splitting if needed
                const int localIdx = static_cast<int>(bytePos - off);
                if (orig.size() == 1) {
                    // Only byte in group — remove entire entry
                    m_document->originalBytes.removeAt(ei);
                } else if (localIdx == 0) {
                    // First byte — trim from front
                    entry.first += 1;
                    orig.remove(0, 1);
                } else if (localIdx == orig.size() - 1) {
                    // Last byte — trim from back
                    orig.chop(1);
                } else {
                    // Middle byte — split into two entries
                    const QByteArray tail = orig.mid(localIdx + 1);
                    orig.truncate(localIdx);
                    m_document->originalBytes.insert(ei + 1, {bytePos + 1, tail});
                }
                break;
            }
        }
        if (showChangesAct->isChecked())
            updateChangedBytesHighlight();
        updateActionStates();
    }
    else if (addPointerActs.contains(chosen))
    {
        const PointerLengthOption opt = addPointerActs.value(chosen);
        if (hexEdit->addPointerUndoable(bytePos, opt.target, opt.size))
            refreshPointersUi();
    }
    else if (dropSelectionPtrsAct && chosen == dropSelectionPtrsAct)
    {
        if (hexEdit->removePointersUndoable(dropSelectionPtrs) > 0)
            refreshPointersUi();
    }
    else if (saveAsDumpAct && chosen == saveAsDumpAct)
    {
        dumpScript();
    }
    else if (editScriptAct3 && chosen == editScriptAct3)
    {
        dumpScript();
    }
    else if (fillWithAct3 && chosen == fillWithAct3)
    {
        const qint64 selBegin = hexEdit->getSelectionBegin();
        const qint64 selLen   = hexEdit->getSelectionEnd() - selBegin;
        const QVector<TableTab> tables = m_tablesDock ? m_tablesDock->allTables() : QVector<TableTab>();
        FillWithDialog dlg(selLen, tables, this);
        if (dlg.exec() == QDialog::Accepted) {
            const QByteArray unit = dlg.fillByte();
            const int len = dlg.fillLength();
            QByteArray fill;
            fill.reserve(len);
            for (int i = 0; i < len; i += unit.size())
                fill.append(unit.left(qMin(unit.size(), len - i)));
            fill.truncate(len);
            hexEdit->replace(selBegin, len, fill);
            hexEdit->setCursorPosition(2 * (selBegin + len));
            hexEdit->ensureVisible();
        }
    }
    else if (vfFormatAct3 && chosen == vfFormatAct3)
    {
        showVirtualFormatDialog(hexEdit->getSelectionBegin(), hexEdit->getSelectionEnd());
    }
    else if (vfRemoveAct3 && chosen == vfRemoveAct3)
    {
        removeVirtualFormatting(hexEdit->getSelectionBegin(), hexEdit->getSelectionEnd());
    }
    else if (addToSectionAct3 && chosen == addToSectionAct3)
    {
        addToSectionWithPrompt();
    }
    else if (playAudioAct && chosen == playAudioAct)
    {
        playAudioAtCursor();
    }
    else if (exportAudioAct && chosen == exportAudioAct)
    {
        exportAudioSample();
    }
    else if (importAudioAct && chosen == importAudioAct)
    {
        importAudioSample();
    }
    else if (chosen == quickSearchAct)
    {
        if (!pointersDialog)
        {
            pointersDialog = new PointersDialog(hexEdit, this);
            connect(pointersDialog, &QDialog::accepted, this, &MainWindow::pointersUpdated);
            connect(pointersDialog, &PointersDialog::searchCompleted, this, &MainWindow::onQuickSearchCompleted);
            pointersDialog->setDock(m_pointersDock);
        }
        pointersDialog->setHexEdit(hexEdit);
        if (m_currentSession) {
            PointersDialog::State ps;
            ps.searchDir        = m_currentSession->ptrSearchDir;
            ps.excludeSelection = m_currentSession->ptrExcludeSelection;
            ps.alignedOnly      = m_currentSession->ptrAlignedOnly;
            ps.optimize         = m_currentSession->ptrOptimize;
            pointersDialog->setDialogState(ps);
        }
        m_pointersDock->show();
        pointersDialog->quickSearch(bytePos);
    }
    else if (chosen == findPtrAct)
    {
        showPointersDialog();
    }
}


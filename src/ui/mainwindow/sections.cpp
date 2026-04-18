// Auto-extracted from mainwindow.cpp
#include "mainwindow.h"
#include "internal.h"
using namespace MainWindowInternal;
#include <QInputDialog>
#include <QProgressDialog>
#include <QSet>
#include <QMessageBox>
#include <QApplication>
#include <QScrollBar>
#include <QStatusBar>
#include <algorithm>
#include "disassembler.h"
#include "SectionListModel.h"
#include "PointerListModel.h"
#include "AudioDockWidget.h"

namespace
{
    struct MdVectorDef
    {
        qint64 offset;
        const char *name;
        bool codeTarget;
    };

    const MdVectorDef kMdVectors[] = {
        {0x00, "Initial stack pointer", false},
        {0x04, "Entry point", true},
        {0x08, "Bus error", true},
        {0x0C, "Address error", true},
        {0x10, "Illegal instruction", true},
        {0x14, "Division by zero", true},
        {0x18, "CHK exception", true},
        {0x1C, "TRAPV exception", true},
        {0x20, "Privilege violation", true},
        {0x24, "Trace", true},
        {0x28, "Line 1010 emulator", true},
        {0x2C, "Line 1111 emulator", true},
        {0x30, "Reserved vector 12", true},
        {0x34, "Reserved vector 13", true},
        {0x38, "Reserved vector 14", true},
        {0x3C, "Uninitialized interrupt", true},
        {0x40, "Reserved vector 16", true},
        {0x44, "Reserved vector 17", true},
        {0x48, "Reserved vector 18", true},
        {0x4C, "Reserved vector 19", true},
        {0x50, "Reserved vector 20", true},
        {0x54, "Reserved vector 21", true},
        {0x58, "Reserved vector 22", true},
        {0x5C, "Reserved vector 23", true},
        {0x60, "Spurious interrupt", true},
        {0x64, "IRQ level 1", true},
        {0x68, "IRQ level 2", true},
        {0x6C, "IRQ level 3", true},
        {0x70, "IRQ level 4", true},
        {0x74, "IRQ level 5", true},
        {0x78, "IRQ level 6", true},
        {0x7C, "IRQ level 7", true},
        {0x80, "TRAP #0", true},
        {0x84, "TRAP #1", true},
        {0x88, "TRAP #2", true},
        {0x8C, "TRAP #3", true},
        {0x90, "TRAP #4", true},
        {0x94, "TRAP #5", true},
        {0x98, "TRAP #6", true},
        {0x9C, "TRAP #7", true},
        {0xA0, "TRAP #8", true},
        {0xA4, "TRAP #9", true},
        {0xA8, "TRAP #10", true},
        {0xAC, "TRAP #11", true},
        {0xB0, "TRAP #12", true},
        {0xB4, "TRAP #13", true},
        {0xB8, "TRAP #14", true},
        {0xBC, "TRAP #15", true},
        {0xC0, "User vector 0", true},
        {0xC4, "User vector 1", true},
        {0xC8, "User vector 2", true},
        {0xCC, "User vector 3", true},
        {0xD0, "User vector 4", true},
        {0xD4, "User vector 5", true},
        {0xD8, "User vector 6", true},
        {0xDC, "User vector 7", true},
        {0xE0, "User vector 8", true},
        {0xE4, "User vector 9", true},
        {0xE8, "User vector 10", true},
        {0xEC, "User vector 11", true},
        {0xF0, "User vector 12", true},
        {0xF4, "User vector 13", true},
        {0xF8, "User vector 14", true},
        {0xFC, "User vector 15", true},
    };

    quint32 readBe32(const QByteArray &data, qint64 offset)
    {
        if (offset < 0 || offset + 4 > data.size())
            return 0;
        const uchar *ptr = reinterpret_cast<const uchar *>(data.constData() + offset);
        return qFromBigEndian<quint32>(ptr);
    }

    qint64 findFunctionEndByReturnRun(const QByteArray &fileData, Disassembler &disasm,
                                      qint64 funcStart, qint64 nextFuncStart)
    {
        const qint64 fileSize = fileData.size();
        if (funcStart < 0 || funcStart >= fileSize)
            return funcStart;

        const qint64 preferredWindow = (nextFuncStart > funcStart)
            ? (nextFuncStart - funcStart)
            : qint64(0x2000);
        const qint64 scanBytes64 = qMin(fileSize - funcStart, qMax<qint64>(preferredWindow, 0x2000));
        const int scanBytes = static_cast<int>(qMin<qint64>(scanBytes64, INT_MAX));
        const QVector<DisasmInstruction> insns = disasm.disassemble(fileData, funcStart, scanBytes, 8192);

        // Find the LAST return instruction before nextFuncStart.
        // This avoids cutting functions short at early-return branches.
        qint64 lastRetEnd = -1;
        for (const auto &insn : insns) {
            if (nextFuncStart > funcStart && insn.fileOffset >= nextFuncStart)
                break;

            if (insn.isReturn)
                lastRetEnd = insn.fileOffset + insn.size;
        }

        if (lastRetEnd > funcStart)
            return lastRetEnd;
        if (nextFuncStart > funcStart)
            return nextFuncStart;
        return qMin(fileSize, funcStart + qint64(2));
    }
}

// ═══════════════════════════════════════════════════════════════════
//  Section splitting
// ═══════════════════════════════════════════════════════════════════

void MainWindow::splitSection(int sectionIndex, const QVector<qint64> &sizes)
{
    if (!hexEdit || !m_sectionModel)
        return;
    if (sectionIndex < 0 || sectionIndex >= m_sectionModel->count())
        return;
    if (sizes.isEmpty())
        return;

    const qint64 fileSize = hexEdit->dataSize();
    const Section &orig = m_sectionModel->at(sectionIndex);
    const qint64 secStart = orig.startOffset;
    const qint64 secEnd   = m_sectionModel->endOffsetOf(sectionIndex, fileSize);
    const qint64 secSize  = secEnd - secStart;
    if (secSize <= 0)
        return;

    // Build new section offsets from sizes
    QVector<qint64> partOffsets;
    qint64 offset = secStart;
    for (qint64 sz : sizes) {
        offset += sz;
        if (offset >= secEnd)
            break;
        partOffsets.append(offset);
    }
    if (partOffsets.isEmpty())
        return;

    QUndoStack *stack = hexEdit->undoStack();
    if (stack)
        stack->beginMacro(tr("Split section"));

    QVector<Section> allSections = m_sectionModel->sections();
    QVector<qint64> allBreaks = hexEdit->lineBreaks();
    auto ensureBreaks = [&](qint64 off) {
        if (off <= 0) return;
        const qint64 pos = off - 1;
        const int existing = static_cast<int>(std::count(allBreaks.begin(), allBreaks.end(), pos));
        for (int i = existing; i < 2; ++i)
            allBreaks.append(pos);
    };

    int partNum = 2;
    for (qint64 partOff : partOffsets) {
        Section s;
        s.name = QStringLiteral("%1 (%2)").arg(orig.name).arg(partNum++);
        s.startOffset = partOff;
        s.color = orig.color;
        s.displayMode = orig.displayMode;
        s.disasmCpu = orig.disasmCpu;
        s.groupId = orig.groupId;
        allSections.append(s);
        ensureBreaks(partOff);
    }

    std::sort(allBreaks.begin(), allBreaks.end());
    m_sectionModel->applySections(allSections, tr("Split section"));
    hexEdit->setLineBreaks(allBreaks);

    if (stack)
        stack->endMacro();

    if (m_document)
        m_document->markDirty();
    hexEdit->viewport()->update();
}

// ═══════════════════════════════════════════════════════════════════
//  Section addition
// ═══════════════════════════════════════════════════════════════════

void MainWindow::addSectionFromSelection(int /*parentIdx*/)
{
    if (!hexEdit || !m_sectionModel)
        return;

    const qint64 selBegin = hexEdit->getSelectionBegin();
    const qint64 selEnd   = hexEdit->getSelectionEnd();
    if (selEnd - selBegin < 1)
        return;

    const qint64 fileSize = hexEdit->dataSize();
    const int existingIdx = m_sectionModel->sectionIndexAtOffset(selBegin);

    // Check if selBegin lands exactly on an existing section's startOffset
    const bool startsAtExisting = (existingIdx >= 0
        && m_sectionModel->at(existingIdx).startOffset == selBegin);

    // Find the next section after selBegin
    const qint64 nextSectionStart = (existingIdx >= 0)
        ? m_sectionModel->endOffsetOf(existingIdx, fileSize)
        : fileSize;

    // Find which sections are overlapped by the selection
    // (sections whose startOffset is > selBegin and < selEnd)
    QVector<int> overlappedIndices;
    for (int i = 0; i < m_sectionModel->count(); ++i) {
        const qint64 so = m_sectionModel->at(i).startOffset;
        if (so > selBegin && so < selEnd)
            overlappedIndices.append(i);
    }

    const int n = m_sectionModel->count() + 1;
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Add section"),
        tr("Section name:"), QLineEdit::Normal,
        tr("Section %1").arg(n), &ok);
    if (!ok || name.isEmpty())
        return;

    QUndoStack *stack = hexEdit->undoStack();
    if (stack)
        stack->beginMacro(tr("Add section"));

    QVector<Section> allSections = m_sectionModel->sections();

    auto ensureBreaks = [&](qint64 offset) {
        if (offset <= 0) return;
        const qint64 pos = offset - 1;
        auto lb = hexEdit->lineBreaks();
        int cnt = static_cast<int>(std::count(lb.begin(), lb.end(), pos));
        for (int i = cnt; i < 2; ++i)
            hexEdit->addLineBreak(pos);
    };

    if (startsAtExisting && overlappedIndices.isEmpty() && selEnd < nextSectionStart) {
        // Case 1: Selection starts at an existing section and ends before
        // the next section.  Create a new section at selEnd, transfer the
        // old section's name to it, and rename the old section to what the
        // user just typed.
        const Section &oldSec = m_sectionModel->at(existingIdx);
        Section newSec;
        newSec.name = oldSec.name;                // inherit old name
        newSec.startOffset = selEnd;
        newSec.color = oldSec.color;
        newSec.displayMode = oldSec.displayMode;
        newSec.groupId = oldSec.groupId;

        // Rename old section to user's name
        for (auto &s : allSections) {
            if (s.startOffset == selBegin) {
                s.name = name;
                break;
            }
        }
        allSections.append(newSec);

        m_sectionModel->applySections(allSections, tr("Add section"));
        ensureBreaks(selEnd);
    } else if (!overlappedIndices.isEmpty()) {
        // Case 2: Selection overlaps other sections.  Create a new section
        // at selBegin (or rename the existing one) and move the last
        // overlapping section's start to selEnd.
        if (startsAtExisting) {
            // Rename the existing section at selBegin
            for (auto &s : allSections) {
                if (s.startOffset == selBegin) {
                    s.name = name;
                    break;
                }
            }
        } else {
            // Create a new section at selBegin
            Section newSec;
            newSec.name = name;
            newSec.startOffset = selBegin;
            newSec.color = SectionListModel::randomPastelColor();
            allSections.append(newSec);
        }

        // Move the last overlapping section's startOffset to selEnd
        const int lastOverlapped = overlappedIndices.last();
        const qint64 oldStart = m_sectionModel->at(lastOverlapped).startOffset;
        for (auto &s : allSections) {
            if (s.startOffset == oldStart) {
                s.startOffset = selEnd;
                break;
            }
        }

        // Remove sections that are fully inside selection (between selBegin and
        // the last overlapping one, exclusive — those are the ones that got
        // swallowed by the selection).
        for (int i = overlappedIndices.size() - 2; i >= 0; --i) {
            const qint64 rmStart = m_sectionModel->at(overlappedIndices[i]).startOffset;
            for (int j = allSections.size() - 1; j >= 0; --j) {
                if (allSections[j].startOffset == rmStart) {
                    allSections.removeAt(j);
                    break;
                }
            }
        }

        m_sectionModel->applySections(allSections, tr("Add section"));
        ensureBreaks(selBegin);
        ensureBreaks(selEnd);
    } else {
        // Default: just add a new section at selBegin
        Section s;
        s.name = name;
        s.startOffset = selBegin;
        s.color = SectionListModel::randomPastelColor();
        allSections.append(s);

        // Check for duplicate
        bool hasDup = false;
        for (int i = 0; i < allSections.size() - 1; ++i) {
            if (allSections[i].startOffset == selBegin) { hasDup = true; break; }
        }
        if (hasDup) {
            if (stack)
                stack->endMacro();
            QMessageBox::information(this, tr("Add section"),
                tr("A section already exists at offset 0x%1.")
                    .arg(selBegin, 0, 16));
            return;
        }

        m_sectionModel->applySections(allSections, tr("Add section"));
        ensureBreaks(selBegin);
    }

    if (stack)
        stack->endMacro();

    if (m_document)
        m_document->markDirty();
    hexEdit->viewport()->update();
}

// ═══════════════════════════════════════════════════════════════════
//  Parsing
// ═══════════════════════════════════════════════════════════════════

void MainWindow::parseSections()
{
    if (m_sectionModel && m_sectionModel->count() > 0) {
        auto reply = QMessageBox::warning(this, tr("Parse Sections"),
            tr("All existing sections will be replaced. Continue?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes)
            return;
        m_sectionModel->clear();
    }
    parseHeaderSections();
    detectFunctions();
}

int MainWindow::deepestSectionIndexForRange(qint64 selBegin, qint64 /*selEnd*/) const
{
    if (!m_sectionModel)
        return -1;
    return m_sectionModel->sectionIndexAtOffset(selBegin);
}

bool MainWindow::canRemoveSelectionFromSection() const
{
    // In the new model, removing part of a section means adding a new section
    // at the cut boundary. Always possible if there's a selection inside a section.
    if (!hexEdit || !m_sectionModel)
        return false;
    const qint64 selBegin = hexEdit->getSelectionBegin();
    const qint64 selEnd = hexEdit->getSelectionEnd();
    if (selEnd - selBegin < 1)
        return false;
    const int idx = m_sectionModel->sectionIndexAtOffset(selBegin);
    if (idx < 0)
        return false;
    const Section &s = m_sectionModel->at(idx);
    const qint64 fileSize = hexEdit->dataSize();
    const qint64 secEnd = m_sectionModel->endOffsetOf(idx, fileSize);
    // Only if the selection start is strictly inside (not at the section start boundary)
    return selBegin > s.startOffset && selBegin < secEnd;
}

void MainWindow::removeSelectionFromSection(const QString &newSectionName)
{
    if (!canRemoveSelectionFromSection())
        return;

    const qint64 selBegin = hexEdit->getSelectionBegin();

    // In the new model, "removing from section" = creating a new section at selBegin
    // which effectively splits the existing section at that point.
    Section newSec;
    const QString trimmedName = newSectionName.trimmed();
    if (!trimmedName.isEmpty())
        newSec.name = trimmedName;
    else
        newSec.name = tr("Section %1").arg(m_sectionModel->count() + 1);
    newSec.startOffset = selBegin;
    newSec.color = SectionListModel::randomPastelColor();
    m_sectionModel->addSection(newSec);

    if (m_document)
        m_document->markDirty();
    hexEdit->viewport()->update();
}

void MainWindow::parseHeaderSections()
{
    parseHeaderSectionsImpl(true);
}

void MainWindow::parseHeaderSectionsImpl(bool pushToUndo)
{
    if (!hexEdit || !m_sectionModel)
        return;

    const RomType rom = m_detectedRomType;
    if (rom == RomType::Unknown)
        return;

    QVector<Section> sections;
    QVector<SectionGroup> groups;
    QVector<QPair<qint64, qint64>> ptrBatch;

    // Helper to add a section (only by startOffset).
    auto addSection = [&](const QString &name, qint64 start,
                          int groupId = -1, int dispMode = SectionDisplay_Default) -> int {
        const qint64 fileSize = hexEdit->dataSize();
        start = qMin(start, fileSize);
        if (start < 0) return -1;
        // Skip duplicate offsets
        for (const auto &existing : sections) {
            if (existing.startOffset == start)
                return -1;
        }
        Section s;
        s.name = name;
        s.startOffset = start;
        s.color = SectionListModel::randomPastelColor();
        s.groupId = groupId;
        s.displayMode = dispMode;
        int idx = sections.size();
        sections.append(s);
        return idx;
    };

    auto addGroup = [&](const QString &name) -> int {
        SectionGroup g;
        g.name = name;
        g.color = SectionListModel::randomPastelColor();
        int idx = groups.size();
        groups.append(g);
        return idx;
    };

    auto ensureBreaksBefore = [this](qint64 offset) {
        if (offset <= 0)
            return;
        const qint64 pos = offset - 1;
        auto breaks = hexEdit->lineBreaks();
        const int existing = static_cast<int>(std::count(breaks.begin(), breaks.end(), pos));
        for (int i = existing; i < 2; ++i)
            hexEdit->addLineBreakDirect(pos);
    };

    if (rom == RomType::MD || rom == RomType::X32) {
        const QByteArray fileData = hexEdit->dataAt(0, hexEdit->dataSize());
        const qint64 fileSize = fileData.size();
        if (fileSize <= 0)
            return;

        const qint64 headerSize = qMin<qint64>(SectionListModel::romHeaderSize(rom), fileSize);

        // Header group
        const int headerGrp = addGroup(tr("Header"));

        addSection(tr("Stack pointer"), 0, headerGrp, SectionDisplay_Raw);
        if (fileSize > 4) addSection(tr("Entry point"), 4, headerGrp, SectionDisplay_Raw);
        if (fileSize > 8) addSection(tr("Vector table"), 8, headerGrp, SectionDisplay_Raw);
        if (fileSize > 0x100) addSection(tr("System header"), 0x100, headerGrp, SectionDisplay_Raw);
        if (fileSize > headerSize) addSection(tr("ROM data"), headerSize);

        ensureBreaksBefore(4);
        ensureBreaksBefore(8);
        ensureBreaksBefore(0x100);
        ensureBreaksBefore(headerSize);

        // Collect vector target pointers
        QHash<qint64, QString> nameByTarget;
        for (const auto &vec : kMdVectors) {
            if (!vec.codeTarget)
                continue;
            const qint64 target = static_cast<qint64>(readBe32(fileData, vec.offset));
            if (target < headerSize || target >= fileSize)
                continue;
            ptrBatch.append({vec.offset, PointerListModel::encodePtrValue(target, 4)});
            if (!nameByTarget.contains(target))
                nameByTarget.insert(target, tr(vec.name));
            else if (QString::fromLatin1(vec.name) == QLatin1String("Entry point"))
                nameByTarget[target] = tr(vec.name);
        }
        m_vectorFunctionNames = nameByTarget;

        if (!sections.isEmpty()) {
            if (pushToUndo)
                m_sectionModel->applySections(sections, groups, tr("Parse header"));
            else
                m_sectionModel->setSectionsAndGroups(sections, groups);
        }

        if (!ptrBatch.isEmpty()) {
            hexEdit->pointers()->addPointersBatch(ptrBatch);
            showPointersAct->setEnabled(!hexEdit->pointers()->empty());
        }
    } else {
        const qint64 hdrSize = SectionListModel::romHeaderSize(rom);
        if (hdrSize <= 0)
            goto finalize;

        // Skip header creation if already parsed
        for (int i = 0; i < m_sectionModel->count(); ++i) {
            if (m_sectionModel->at(i).name == tr("Header")
                && m_sectionModel->at(i).startOffset == 0)
                goto finalize;
        }

        {
            const QByteArray fileData = hexEdit->dataAt(0, hexEdit->dataSize());
            const qint64 fileSize = fileData.size();
            if (fileSize <= 0)
                goto finalize;

            const qint64 headerSize = qMin<qint64>(hdrSize, fileSize);
            const int headerGrp = addGroup(tr("Header"));

            if (rom == RomType::NES) {
                addSection(tr("Magic"), 0x00, headerGrp, SectionDisplay_Raw);
                if (fileSize > 4)  addSection(tr("PRG/CHR size"), 0x04, headerGrp, SectionDisplay_Raw);
                if (fileSize > 6)  addSection(tr("Flags"), 0x06, headerGrp, SectionDisplay_Raw);
                if (fileSize > 0xB) addSection(tr("Padding"), 0x0B, headerGrp, SectionDisplay_Raw);

                ensureBreaksBefore(0x04);
                ensureBreaksBefore(0x06);
                ensureBreaksBefore(headerSize);

                const int prgSize16k = (headerSize <= 4 || fileSize <= 4)
                    ? 0 : static_cast<unsigned char>(fileData.at(4));
                const int chrSize8k  = (headerSize <= 5 || fileSize <= 5)
                    ? 0 : static_cast<unsigned char>(fileData.at(5));
                const qint64 prgStart = headerSize;
                const qint64 prgEnd   = qMin<qint64>(prgStart + prgSize16k * 0x4000, fileSize);
                const qint64 chrStart = prgEnd;

                if (prgEnd > prgStart) {
                    addSection(tr("PRG-ROM"), prgStart);
                    ensureBreaksBefore(prgStart);
                }
                if (chrStart < fileSize && chrSize8k > 0) {
                    addSection(tr("CHR-ROM"), chrStart, -1, SectionDisplay_Raw);
                    ensureBreaksBefore(chrStart);
                }

                if (fileSize >= 0x104) {
                    const quint16 jpTarget = static_cast<quint16>(
                        static_cast<unsigned char>(fileData.at(0x102)) << 8
                        | static_cast<unsigned char>(fileData.at(0x101)));
                    if (jpTarget >= headerSize && jpTarget < fileSize) {
                        QHash<qint64, QString> nameByTarget;
                        nameByTarget.insert(jpTarget, tr("Entry point"));
                        m_vectorFunctionNames = nameByTarget;
                    }
                }
            } else if (rom == RomType::GB || rom == RomType::GBC) {
                addSection(tr("RST / Interrupt vectors"), 0x00, headerGrp, SectionDisplay_Raw);
                if (fileSize > 0x100) addSection(tr("Entry point"), 0x100, headerGrp, SectionDisplay_Raw);
                if (fileSize > 0x104) addSection(tr("Nintendo logo"), 0x104, headerGrp, SectionDisplay_Raw);
                if (fileSize > 0x134) addSection(tr("Title"), 0x134, headerGrp, SectionDisplay_Raw);
                if (fileSize > 0x144) addSection(tr("Cartridge info"), 0x144, headerGrp, SectionDisplay_Raw);
                if (fileSize > headerSize) addSection(tr("ROM data"), headerSize);

                ensureBreaksBefore(0x100);
                ensureBreaksBefore(0x104);
                ensureBreaksBefore(0x134);
                ensureBreaksBefore(0x144);
                ensureBreaksBefore(headerSize);

                if (fileSize >= 0x104) {
                    const quint16 jpTarget = static_cast<quint16>(
                        static_cast<unsigned char>(fileData.at(0x102)) << 8
                        | static_cast<unsigned char>(fileData.at(0x101)));
                    if (jpTarget >= headerSize && jpTarget < fileSize) {
                        QHash<qint64, QString> nameByTarget;
                        nameByTarget.insert(jpTarget, tr("Entry point"));
                        m_vectorFunctionNames = nameByTarget;
                    }
                }
            } else if (rom == RomType::GBA) {
                addSection(tr("Entry point"), 0x00, headerGrp, SectionDisplay_Raw);
                if (fileSize > 0x04) addSection(tr("Nintendo logo"), 0x04, headerGrp, SectionDisplay_Raw);
                if (fileSize > 0xA0) addSection(tr("Game title"), 0xA0, headerGrp, SectionDisplay_Raw);
                if (fileSize > 0xAC) addSection(tr("Game code"), 0xAC, headerGrp, SectionDisplay_Raw);
                if (fileSize > 0xB0) addSection(tr("System info"), 0xB0, headerGrp, SectionDisplay_Raw);
                if (fileSize > headerSize) addSection(tr("ROM data"), headerSize);

                ensureBreaksBefore(0x04);
                ensureBreaksBefore(0xA0);
                ensureBreaksBefore(0xB0);
                ensureBreaksBefore(headerSize);
            } else if (rom == RomType::SNES_SMC || rom == RomType::SNES_HIROM_SMC) {
                addSection(tr("ROM size"), 0x00, headerGrp, SectionDisplay_Raw);
                if (fileSize > 2) addSection(tr("Flags"), 0x02, headerGrp, SectionDisplay_Raw);
                if (fileSize > 3) addSection(tr("Reserved"), 0x03, headerGrp, SectionDisplay_Raw);
                if (fileSize > headerSize) addSection(tr("ROM data"), headerSize);

                ensureBreaksBefore(0x03);
                ensureBreaksBefore(headerSize);
            } else if (rom == RomType::N64 || rom == RomType::N64_LE || rom == RomType::N64_V64) {
                addSection(tr("PI config"), 0x00, headerGrp, SectionDisplay_Raw);
                if (fileSize > 4)  addSection(tr("Clock rate"), 0x04, headerGrp, SectionDisplay_Raw);
                if (fileSize > 8)  addSection(tr("Entry point"), 0x08, headerGrp, SectionDisplay_Raw);
                if (fileSize > 0x0C) addSection(tr("Release"), 0x0C, headerGrp, SectionDisplay_Raw);
                if (fileSize > 0x10) addSection(tr("CRC"), 0x10, headerGrp, SectionDisplay_Raw);
                if (fileSize > 0x18) addSection(tr("Reserved"), 0x18, headerGrp, SectionDisplay_Raw);
                if (fileSize > 0x20) addSection(tr("Name"), 0x20, headerGrp, SectionDisplay_Raw);
                if (fileSize > 0x34) addSection(tr("Cartridge info"), 0x34, headerGrp, SectionDisplay_Raw);
                if (fileSize > headerSize) addSection(tr("ROM data"), headerSize);

                ensureBreaksBefore(0x08);
                ensureBreaksBefore(0x18);
                ensureBreaksBefore(0x20);
                ensureBreaksBefore(0x34);
                ensureBreaksBefore(headerSize);
            } else {
                addSection(tr("Header"), 0, headerGrp, SectionDisplay_Raw);
                if (fileSize > hdrSize) addSection(tr("ROM data"), hdrSize);
                ensureBreaksBefore(headerSize);
            }

            if (!sections.isEmpty()) {
                if (pushToUndo)
                    m_sectionModel->applySections(sections, groups, tr("Parse header"));
                else
                    m_sectionModel->setSectionsAndGroups(sections, groups);
            }
        }
    }

finalize:
    if (m_sectionsDock) {
        m_sectionsDock->setRomTypeName(QString::fromLatin1(romTypeName(rom)));
        m_sectionsDock->setCurrentRomType(rom);
        m_sectionsDock->setFileSize(hexEdit->dataSize());
    }
    if (m_audioDock)
        m_audioDock->setRomType(rom);

    if (pushToUndo && m_document)
        m_document->markDirty();
}

// ═══════════════════════════════════════════════════════════════════
//  Function detection
// ═══════════════════════════════════════════════════════════════════

void MainWindow::detectFunctions()
{
    if (!hexEdit || !m_sectionModel)
        return;

    const RomType romType = m_detectedRomType;
    if (!Disassembler::isSupported(romType)) {
        QMessageBox::warning(this, tr("Detect functions"),
                             tr("Disassembly is not supported for the current ROM type."));
        return;
    }

    const qint64 fileSize = hexEdit->dataSize();
    if (fileSize <= 0)
        return;

    const qint64 headerSize = SectionListModel::romHeaderSize(romType);
    const qint64 codeStart  = headerSize;
    const qint64 codeLen    = fileSize - codeStart;
    if (codeLen <= 0)
        return;

    QByteArray romData = hexEdit->dataAt(0, fileSize);
    if (romData.size() < fileSize)
        return;

    Disassembler disasm;
    if (!disasm.setRomType(romType)) {
        QMessageBox::warning(this, tr("Detect functions"),
                             tr("Failed to initialize disassembler."));
        return;
    }

    QProgressDialog progress(tr("Detecting functions..."), tr("Cancel"), 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);
    bool cancelled = false;

    QVector<CallPointer> callPointers;
    QVector<DetectedFunction> functions = disasm.scanFunctions(
        romData, codeStart, static_cast<int>(codeLen),
        [&](int percent) {
            if (cancelled) return;
            progress.setValue(percent * 80 / 100);
            QApplication::processEvents();
            if (progress.wasCanceled())
                cancelled = true;
        },
        &callPointers);

    if (cancelled) { progress.close(); return; }

    // Inject entry point for MD/X32
    if (romType == RomType::MD || romType == RomType::X32) {
        const qint64 entryTarget = static_cast<qint64>(readBe32(romData, 0x04));
        if (entryTarget >= codeStart && entryTarget < fileSize) {
            bool alreadyFound = false;
            for (const auto &f : functions)
                if (f.startOffset == entryTarget) { alreadyFound = true; break; }
            if (!alreadyFound) {
                std::sort(functions.begin(), functions.end(),
                          [](const DetectedFunction &a, const DetectedFunction &b) {
                              return a.startOffset < b.startOffset; });
                qint64 nextStart = fileSize;
                for (const auto &f : functions)
                    if (f.startOffset > entryTarget) { nextStart = f.startOffset; break; }
                const qint64 funcEnd = findFunctionEndByReturnRun(romData, disasm, entryTarget, nextStart);
                DetectedFunction df;
                df.startOffset = entryTarget;
                df.endOffset   = qBound(entryTarget + 1, funcEnd, qMax(entryTarget + 1, nextStart));
                df.cpuAddress  = static_cast<quint64>(entryTarget);
                functions.append(df);
            }
            m_vectorFunctionNames[entryTarget] = tr("Entry point");
        }
    }

    // Inject vector targets
    {
        std::sort(functions.begin(), functions.end(),
                  [](const DetectedFunction &a, const DetectedFunction &b) {
                      return a.startOffset < b.startOffset; });

        QSet<qint64> existingStarts;
        for (const auto &f : functions)
            existingStarts.insert(f.startOffset);

        for (auto it = m_vectorFunctionNames.constBegin();
             it != m_vectorFunctionNames.constEnd(); ++it) {
            const qint64 addr = it.key();
            if (addr < codeStart || addr >= fileSize || existingStarts.contains(addr))
                continue;
            qint64 nextStart = fileSize;
            for (const auto &f : functions)
                if (f.startOffset > addr) { nextStart = f.startOffset; break; }
            const qint64 funcEnd = findFunctionEndByReturnRun(romData, disasm, addr, nextStart);
            DetectedFunction df;
            df.startOffset = addr;
            df.endOffset   = qBound(addr + 1, funcEnd, qMax(addr + 1, nextStart));
            df.cpuAddress  = static_cast<quint64>(addr);
            functions.append(df);
        }

        std::sort(functions.begin(), functions.end(),
                  [](const DetectedFunction &a, const DetectedFunction &b) {
                      return a.startOffset < b.startOffset; });
    }

    if (functions.isEmpty()) {
        progress.close();
        QMessageBox::information(this, tr("Detect functions"), tr("No functions detected."));
        return;
    }

    progress.setLabelText(tr("Building sections..."));
    progress.setValue(82);
    QApplication::processEvents();

    // Start from existing sections; remove old Code/Data sections from previous parse
    QVector<Section> allSections;
    QVector<SectionGroup> allGroups = m_sectionModel->groups();
    {
        const auto &existing = m_sectionModel->sections();
        // Keep only non-disasm, non "Data-*" sections from within code area
        for (const auto &s : existing) {
            if (s.startOffset >= codeStart
                && (s.displayMode == SectionDisplay_Disasm
                    || s.name.startsWith(QLatin1String("Data"))))
                continue;
            allSections.append(s);
        }
    }

    // Create Code and Data groups
    int codeGrp = -1, dataGrp = -1;
    // Check if these groups already exist
    for (int gi = 0; gi < allGroups.size(); ++gi) {
        if (allGroups[gi].name == tr("Code")) codeGrp = gi;
        if (allGroups[gi].name == tr("Data")) dataGrp = gi;
    }
    if (codeGrp < 0) {
        SectionGroup g; g.name = tr("Code"); g.color = SectionListModel::randomPastelColor();
        codeGrp = allGroups.size(); allGroups.append(g);
    }
    if (dataGrp < 0) {
        SectionGroup g; g.name = tr("Data"); g.color = SectionListModel::randomPastelColor();
        dataGrp = allGroups.size(); allGroups.append(g);
    }

    QVector<qint64> allBreaks = hexEdit->lineBreaks();
    auto ensureBreaks = [&](qint64 offset) {
        if (offset <= 0) return;
        const qint64 pos = offset - 1;
        const int existing = static_cast<int>(std::count(allBreaks.begin(), allBreaks.end(), pos));
        for (int i = existing; i < 2; ++i)
            allBreaks.append(pos);
    };

    ensureBreaks(codeStart);

    // Collect used offsets
    QSet<qint64> usedOffsets;
    for (const auto &s : allSections)
        usedOffsets.insert(s.startOffset);

    // Build function sections and data gap sections
    int dataSectionCounter = 0;
    qint64 prevEnd = codeStart;

    for (int fi = 0; fi < functions.size(); ++fi) {
        const DetectedFunction &df = functions[fi];

        // Data gap
        if (df.startOffset > prevEnd && !usedOffsets.contains(prevEnd)) {
            ++dataSectionCounter;
            Section dataSec;
            dataSec.name = (dataSectionCounter == 1) ? tr("Data") : tr("Data-%1").arg(dataSectionCounter);
            dataSec.startOffset = prevEnd;
            dataSec.color = SectionListModel::randomPastelColor();
            dataSec.groupId = dataGrp;
            dataSec.displayMode = SectionDisplay_Raw;
            allSections.append(dataSec);
            usedOffsets.insert(prevEnd);
            ensureBreaks(prevEnd);
        }

        // Function section
        if (!usedOffsets.contains(df.startOffset)) {
            QString funcName = m_vectorFunctionNames.value(df.startOffset);
            if (funcName.isEmpty())
                funcName = QStringLiteral("sub_%1").arg(df.cpuAddress, 0, 16, QLatin1Char('0')).toUpper();

            Section funcSection;
            funcSection.name = funcName;
            funcSection.startOffset = df.startOffset;
            funcSection.color = SectionListModel::randomPastelColor();
            funcSection.groupId = codeGrp;
            funcSection.displayMode = SectionDisplay_Disasm;
            allSections.append(funcSection);
            usedOffsets.insert(df.startOffset);
            ensureBreaks(df.startOffset);
        }

        prevEnd = df.endOffset;
    }

    // Trailing data gap
    if (prevEnd < fileSize && !usedOffsets.contains(prevEnd)) {
        ++dataSectionCounter;
        Section dataSec;
        dataSec.name = (dataSectionCounter == 1) ? tr("Data") : tr("Data-%1").arg(dataSectionCounter);
        dataSec.startOffset = prevEnd;
        dataSec.color = SectionListModel::randomPastelColor();
        dataSec.groupId = dataGrp;
        dataSec.displayMode = SectionDisplay_Raw;
        allSections.append(dataSec);
        ensureBreaks(prevEnd);
    }

    progress.setValue(90);
    QApplication::processEvents();

    std::sort(allBreaks.begin(), allBreaks.end());
    m_sectionModel->applySections(allSections, allGroups, tr("Detect functions"));
    hexEdit->setLineBreaks(allBreaks);

    // Add call pointers
    if (!callPointers.isEmpty()) {
        QSet<qint64> funcStarts;
        for (const auto &f : functions)
            funcStarts.insert(f.startOffset);

        QVector<QPair<qint64, qint64>> ptrBatch;
        for (const auto &cp : callPointers) {
            if (funcStarts.contains(cp.targetOffset))
                ptrBatch.append({cp.ptrFileOffset,
                                 PointerListModel::encodePtrValue(cp.targetOffset, cp.ptrSize)});
        }
        if (!ptrBatch.isEmpty())
            hexEdit->pointers()->addPointersBatch(ptrBatch);
    }

    progress.setValue(100);
    progress.close();

    if (m_document)
        m_document->markDirty();
    hexEdit->viewport()->update();

    statusBar()->showMessage(tr("Detected %1 functions").arg(functions.size()), 5000);
}

void MainWindow::detectFunctionsInRange(qint64 rangeStart, qint64 rangeEnd)
{
    if (!hexEdit || !m_sectionModel)
        return;

    const RomType romType = m_detectedRomType;
    if (!Disassembler::isSupported(romType)) {
        QMessageBox::warning(this, tr("Detect functions"),
                             tr("Disassembly is not supported for the current ROM type."));
        return;
    }

    const qint64 fileSize = hexEdit->dataSize();
    if (rangeEnd <= rangeStart || rangeStart < 0 || rangeEnd > fileSize)
        return;

    const qint64 codeLen = rangeEnd - rangeStart;
    if (codeLen <= 0)
        return;

    QByteArray romData = hexEdit->dataAt(0, fileSize);
    if (romData.size() < fileSize)
        return;

    Disassembler disasm;
    if (!disasm.setRomType(romType)) {
        QMessageBox::warning(this, tr("Detect functions"),
                             tr("Failed to initialize disassembler."));
        return;
    }

    QProgressDialog progress(tr("Detecting functions in section..."), tr("Cancel"), 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);
    bool cancelled = false;

    QVector<CallPointer> callPointers;
    QVector<DetectedFunction> functions = disasm.scanFunctions(
        romData, rangeStart, static_cast<int>(codeLen),
        [&](int percent) {
            if (cancelled) return;
            progress.setValue(percent * 80 / 100);
            QApplication::processEvents();
            if (progress.wasCanceled())
                cancelled = true;
        },
        &callPointers);

    if (cancelled) { progress.close(); return; }

    // Sort and refine function boundaries
    std::sort(functions.begin(), functions.end(),
              [](const DetectedFunction &a, const DetectedFunction &b) {
                  return a.startOffset < b.startOffset; });

    for (int i = 0; i < functions.size(); ++i) {
        const qint64 nextStart = (i + 1 < functions.size())
                                     ? functions[i + 1].startOffset
                                     : rangeEnd;
        const qint64 funcEnd = findFunctionEndByReturnRun(romData, disasm,
                                   functions[i].startOffset, nextStart);
        functions[i].endOffset = qBound(functions[i].startOffset + 1, funcEnd,
                                        qMax(functions[i].startOffset + 1, nextStart));
    }

    if (functions.isEmpty()) {
        progress.close();
        QMessageBox::information(this, tr("Detect functions"), tr("No functions detected in section."));
        return;
    }

    progress.setLabelText(tr("Building sections..."));
    progress.setValue(82);
    QApplication::processEvents();

    QVector<Section> allSections = m_sectionModel->sections();

    // Ensure Code and Data groups exist
    QVector<SectionGroup> allGroups = m_sectionModel->groups();
    int codeGrp = -1, dataGrp = -1;
    for (int gi = 0; gi < allGroups.size(); ++gi) {
        if (allGroups[gi].name == tr("Code")) codeGrp = gi;
        if (allGroups[gi].name == tr("Data")) dataGrp = gi;
    }
    if (codeGrp < 0) {
        SectionGroup g; g.name = tr("Code"); g.color = SectionListModel::randomPastelColor();
        codeGrp = allGroups.size(); allGroups.append(g);
    }
    if (dataGrp < 0) {
        SectionGroup g; g.name = tr("Data"); g.color = SectionListModel::randomPastelColor();
        dataGrp = allGroups.size(); allGroups.append(g);
    }

    QVector<qint64> allBreaks = hexEdit->lineBreaks();
    auto ensureBreaks = [&](qint64 offset) {
        if (offset <= 0) return;
        const qint64 pos = offset - 1;
        const int existing = static_cast<int>(std::count(allBreaks.begin(), allBreaks.end(), pos));
        for (int i = existing; i < 2; ++i)
            allBreaks.append(pos);
    };

    QSet<qint64> usedOffsets;
    for (const auto &s : allSections)
        usedOffsets.insert(s.startOffset);

    int dataSectionCounter = m_sectionModel->count();
    qint64 prevEnd = rangeStart;

    for (int fi = 0; fi < functions.size(); ++fi) {
        const DetectedFunction &df = functions[fi];

        // Data gap
        if (df.startOffset > prevEnd && !usedOffsets.contains(prevEnd)) {
            ++dataSectionCounter;
            Section dataSec;
            dataSec.name = tr("Data-%1").arg(dataSectionCounter);
            dataSec.startOffset = prevEnd;
            dataSec.color = SectionListModel::randomPastelColor();
            dataSec.groupId = dataGrp;
            dataSec.displayMode = SectionDisplay_Raw;
            allSections.append(dataSec);
            usedOffsets.insert(prevEnd);
            ensureBreaks(prevEnd);
        }

        // Function section
        if (!usedOffsets.contains(df.startOffset)) {
            const QString funcName = QStringLiteral("sub_%1")
                .arg(df.cpuAddress, 0, 16, QLatin1Char('0')).toUpper();

            Section funcSection;
            funcSection.name = funcName;
            funcSection.startOffset = df.startOffset;
            funcSection.color = SectionListModel::randomPastelColor();
            funcSection.groupId = codeGrp;
            funcSection.displayMode = SectionDisplay_Disasm;
            allSections.append(funcSection);
            usedOffsets.insert(df.startOffset);
            ensureBreaks(df.startOffset);
        }

        prevEnd = df.endOffset;
    }

    // Trailing data gap
    if (prevEnd < rangeEnd && !usedOffsets.contains(prevEnd)) {
        ++dataSectionCounter;
        Section dataSec;
        dataSec.name = tr("Data-%1").arg(dataSectionCounter);
        dataSec.startOffset = prevEnd;
        dataSec.color = SectionListModel::randomPastelColor();
        dataSec.groupId = dataGrp;
        dataSec.displayMode = SectionDisplay_Raw;
        allSections.append(dataSec);
        ensureBreaks(prevEnd);
    }

    progress.setValue(90);
    QApplication::processEvents();

    std::sort(allBreaks.begin(), allBreaks.end());
    m_sectionModel->applySections(allSections, allGroups, tr("Detect functions"));
    hexEdit->setLineBreaks(allBreaks);

    // Add call pointers
    if (!callPointers.isEmpty()) {
        QSet<qint64> funcStarts;
        for (const auto &f : functions)
            funcStarts.insert(f.startOffset);

        QVector<QPair<qint64, qint64>> ptrBatch;
        for (const auto &cp : callPointers) {
            if (funcStarts.contains(cp.targetOffset))
                ptrBatch.append({cp.ptrFileOffset,
                                 PointerListModel::encodePtrValue(cp.targetOffset, cp.ptrSize)});
        }
        if (!ptrBatch.isEmpty())
            hexEdit->pointers()->addPointersBatch(ptrBatch);
    }

    progress.setValue(100);
    progress.close();

    if (m_document)
        m_document->markDirty();
    hexEdit->viewport()->update();

    statusBar()->showMessage(tr("Detected %1 functions in section").arg(functions.size()), 5000);
}

void MainWindow::detectFunctionPointersOnly()
{
    if (!hexEdit)
        return;

    const RomType romType = m_detectedRomType;
    if (!Disassembler::isSupported(romType))
        return;

    const qint64 fileSize = hexEdit->dataSize();
    if (fileSize <= 0)
        return;

    const qint64 codeStart  = SectionListModel::romHeaderSize(romType);
    const qint64 codeLen    = fileSize - codeStart;
    if (codeLen <= 0)
        return;

    const QByteArray romData = hexEdit->dataAt(0, fileSize);
    if (romData.size() < fileSize)
        return;

    Disassembler disasm;
    if (!disasm.setRomType(romType))
        return;

    QVector<CallPointer> callPointers;
    const QVector<DetectedFunction> functions = disasm.scanFunctions(
        romData, codeStart, static_cast<int>(codeLen), nullptr, &callPointers);

    if (callPointers.isEmpty() || functions.isEmpty())
        return;

    QSet<qint64> funcStarts;
    for (const auto &f : functions)
        funcStarts.insert(f.startOffset);

    QVector<QPair<qint64, qint64>> ptrBatch;
    ptrBatch.reserve(callPointers.size());
    for (const auto &cp : callPointers) {
        if (funcStarts.contains(cp.targetOffset))
            ptrBatch.append({cp.ptrFileOffset,
                             PointerListModel::encodePtrValue(cp.targetOffset, cp.ptrSize)});
    }

    if (!ptrBatch.isEmpty()) {
        hexEdit->pointers()->addPointersBatch(ptrBatch);
        showPointersAct->setEnabled(!hexEdit->pointers()->empty());
        if (m_document)
            m_document->markDirty();
    }
}

void MainWindow::showPointersDialog()
{
    if (!pointersDialog)
    {
        pointersDialog = new PointersDialog(hexEdit, this);
        connect(pointersDialog, &QDialog::accepted, this, &MainWindow::pointersUpdated);
        connect(pointersDialog, &PointersDialog::searchCompleted, this, &MainWindow::onQuickSearchCompleted);
        pointersDialog->setDock(m_pointersDock);
    }
    pointersDialog->setHexEdit(hexEdit);
    m_pointersDock->show();
    m_pointersDock->raise();
    pointersDialog->show();
    pointersDialog->setRomProfile(m_detectedRomType, m_pointerOffset);

    if (m_currentSession) {
        PointersDialog::State ps;
        ps.searchDir        = m_currentSession->ptrSearchDir;
        ps.excludeSelection = m_currentSession->ptrExcludeSelection;
        ps.alignedOnly      = m_currentSession->ptrAlignedOnly;
        ps.optimize         = m_currentSession->ptrOptimize;
        pointersDialog->setDialogState(ps);
    }
}

// ═══════════════════════════════════════════════════════════════════
//  Audio sample detection and playback
// ═══════════════════════════════════════════════════════════════════

#include "audiodetector.h"
#include "audioplayer.h"
#include <QFileDialog>

void MainWindow::detectAudioSamples()
{
    if (!hexEdit || !m_sectionModel)
        return;

    const qint64 fileSize = hexEdit->dataSize();
    if (fileSize <= 0)
        return;

    const QByteArray data = hexEdit->dataAt(0, fileSize);

    QProgressDialog progress(tr("Detecting audio samples..."), tr("Cancel"), 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setValue(10);
    QApplication::processEvents();

    AudioDetector detector;
    detector.setMinSampleBytes(64);
    detector.setMinConfidence(0.40f);

    const auto samples = detector.detect(data, m_detectedRomType);
    progress.setValue(100);
    progress.close();

    if (samples.isEmpty()) {
        QMessageBox::information(this, tr("Detect Audio Samples"),
                                 tr("No audio samples detected in this ROM."));
        return;
    }

    const QString msg = tr("Detected %1 audio sample(s). Create sections for them?")
                            .arg(samples.size());
    if (QMessageBox::question(this, tr("Detect Audio Samples"), msg,
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    QUndoStack *stack = hexEdit->undoStack();
    if (stack)
        stack->beginMacro(tr("Detect audio samples"));

    QVector<Section> newSections = m_sectionModel->sections();

    // Collect already-used offsets
    QSet<qint64> usedOffsets;
    for (const auto &s : newSections)
        usedOffsets.insert(s.startOffset);

    for (const DetectedSample &s : samples) {
        if (usedOffsets.contains(s.offset))
            continue;   // skip duplicates
        Section sec;
        sec.name = s.name;
        sec.startOffset = s.offset;
        sec.color = QColor(0x40, 0xA0, 0xFF, 0x40);
        sec.displayMode = SectionDisplay_Audio;
        newSections.append(sec);
        usedOffsets.insert(s.offset);
    }

    m_sectionModel->applySections(newSections, tr("Detect audio samples"));

    // Add visual line breaks
    for (const DetectedSample &s : samples) {
        if (s.offset > 0) {
            const qint64 pos = s.offset - 1;
            auto lb = hexEdit->lineBreaks();
            int cnt = static_cast<int>(std::count(lb.begin(), lb.end(), pos));
            for (int i = cnt; i < 2; ++i)
                hexEdit->addLineBreak(pos);
        }
    }

    if (stack)
        stack->endMacro();

    if (m_document)
        m_document->markDirty();

    hexEdit->viewport()->update();

    statusBar()->showMessage(tr("Found %1 audio sample(s)").arg(samples.size()), 5000);
}

void MainWindow::detectAudioSamplesInRange(qint64 rangeStart, qint64 rangeEnd)
{
    if (!hexEdit || !m_sectionModel)
        return;

    const qint64 fileSize = hexEdit->dataSize();
    if (rangeEnd <= rangeStart || rangeStart < 0 || rangeEnd > fileSize)
        return;

    const QByteArray data = hexEdit->dataAt(rangeStart, rangeEnd - rangeStart);

    QProgressDialog progress(tr("Detecting audio samples..."), tr("Cancel"), 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setValue(10);
    QApplication::processEvents();

    AudioDetector detector;
    detector.setMinSampleBytes(64);
    detector.setMinConfidence(0.40f);

    auto samples = detector.detect(data, m_detectedRomType);
    progress.setValue(100);
    progress.close();

    // Adjust offsets — detector sees data starting at 0, actual file offset is rangeStart
    for (auto &s : samples)
        s.offset += rangeStart;

    if (samples.isEmpty()) {
        QMessageBox::information(this, tr("Detect Audio Samples"),
                                 tr("No audio samples detected in this section."));
        return;
    }

    const QString msg = tr("Detected %1 audio sample(s). Create sections for them?")
                            .arg(samples.size());
    if (QMessageBox::question(this, tr("Detect Audio Samples"), msg,
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return;

    QUndoStack *stack = hexEdit->undoStack();
    if (stack)
        stack->beginMacro(tr("Detect audio samples"));

    QVector<Section> newSections = m_sectionModel->sections();

    QSet<qint64> usedOffsets;
    for (const auto &s : newSections)
        usedOffsets.insert(s.startOffset);

    for (const DetectedSample &s : samples) {
        if (usedOffsets.contains(s.offset))
            continue;
        Section sec;
        sec.name = s.name;
        sec.startOffset = s.offset;
        sec.color = QColor(0x40, 0xA0, 0xFF, 0x40);
        sec.displayMode = SectionDisplay_Audio;
        newSections.append(sec);
        usedOffsets.insert(s.offset);
    }

    m_sectionModel->applySections(newSections, tr("Detect audio samples"));

    for (const DetectedSample &s : samples) {
        if (s.offset > 0) {
            const qint64 pos = s.offset - 1;
            auto lb = hexEdit->lineBreaks();
            int cnt = static_cast<int>(std::count(lb.begin(), lb.end(), pos));
            for (int i = cnt; i < 2; ++i)
                hexEdit->addLineBreak(pos);
        }
    }

    if (stack)
        stack->endMacro();

    if (m_document)
        m_document->markDirty();

    hexEdit->viewport()->update();

    statusBar()->showMessage(tr("Found %1 audio sample(s) in section").arg(samples.size()), 5000);
}

void MainWindow::playAudioAtCursor()
{
    if (!hexEdit || !m_sectionModel)
        return;

    const qint64 curPos = hexEdit->cursorPosition() / 2;
    const qint64 fileSize = hexEdit->dataSize();
    const int idx = m_sectionModel->sectionIndexAtOffset(curPos);
    if (idx < 0)
        return;

    const Section &sec = m_sectionModel->at(idx);
    if (sec.displayMode != SectionDisplay_Audio)
        return;

    const qint64 secEnd = m_sectionModel->endOffsetOf(idx, fileSize);
    const qint64 playOffset = qMax(curPos, sec.startOffset);
    const qint64 playLen = secEnd - playOffset;
    if (playLen <= 0) return;

    const QByteArray sampleData = hexEdit->dataAt(playOffset, playLen);

    AudioSampleFormat fmt = AudioSampleFormat::Unknown;
    if (m_audioDock)
        fmt = m_audioDock->selectedFormat();

    if (fmt == AudioSampleFormat::Unknown) {
        if (sec.name.contains(QStringLiteral("BRR"), Qt::CaseInsensitive))
            fmt = AudioSampleFormat::SNES_BRR;
        else if (sec.name.contains(QStringLiteral("UMK3"), Qt::CaseInsensitive)
                 || sec.name.contains(QStringLiteral("IMA ADPCM"), Qt::CaseInsensitive)
                 || sec.name.contains(QStringLiteral("DPCM4"), Qt::CaseInsensitive)
                 || sec.name.contains(QStringLiteral("4-bit DPCM"), Qt::CaseInsensitive))
            fmt = AudioSampleFormat::MD_DPCM4_6500;
        else if (sec.name.contains(QStringLiteral("OKI"), Qt::CaseInsensitive)
                 || sec.name.contains(QStringLiteral("Dialogic"), Qt::CaseInsensitive))
            fmt = AudioSampleFormat::MD_ADPCM_OKI;
        else if (sec.name.contains(QStringLiteral("DPCM"), Qt::CaseInsensitive))
            fmt = (m_detectedRomType == RomType::MD || m_detectedRomType == RomType::X32)
                ? AudioSampleFormat::MD_DPCM4_6500
                : AudioSampleFormat::NES_DPCM;
        else if (sec.name.contains(QStringLiteral("ulaw"), Qt::CaseInsensitive)
                 || sec.name.contains(QStringLiteral("µ-law"), Qt::CaseInsensitive)
                 || sec.name.contains(QStringLiteral("mu-law"), Qt::CaseInsensitive))
            fmt = AudioSampleFormat::MD_ULAW;
        else if (sec.name.contains(QStringLiteral("Signed PCM"), Qt::CaseInsensitive)
                 || sec.name.contains(QStringLiteral("Signed 8"), Qt::CaseInsensitive))
            fmt = AudioSampleFormat::MD_PCM8_Signed;
        else if (sec.name.contains(QStringLiteral("DAC"), Qt::CaseInsensitive))
            fmt = AudioSampleFormat::MD_DAC_PCM;
        else if (sec.name.contains(QStringLiteral("GBA"), Qt::CaseInsensitive))
            fmt = AudioSampleFormat::GBA_PCM8;
        else {
            switch (m_detectedRomType) {
            case RomType::SNES: case RomType::SNES_SMC:
            case RomType::SNES_HIROM: case RomType::SNES_HIROM_SMC:
                fmt = AudioSampleFormat::SNES_BRR; break;
            case RomType::NES:
                fmt = AudioSampleFormat::NES_DPCM; break;
            case RomType::MD: case RomType::X32:
                fmt = AudioSampleFormat::MD_DAC_PCM; break;
            case RomType::GBA:
                fmt = AudioSampleFormat::GBA_PCM8; break;
            default:
                fmt = AudioSampleFormat::Raw_PCM8_Unsigned; break;
            }
        }
    }

    if (!m_audioPlayer) {
        m_audioPlayer = new AudioPlayer(this);
        connect(m_audioPlayer, &AudioPlayer::playbackStopped, this, [this]() {
            statusBar()->showMessage(tr("Playback stopped"), 2000);
        });
    }

    m_audioPlayer->loadFromRaw(sampleData, fmt);
    const int rateOverride = m_audioDock ? m_audioDock->selectedSampleRate() : 0;
    const double speed = m_audioDock ? m_audioDock->playbackSpeed() : 1.0;
    m_audioPlayer->play(rateOverride, speed);

    statusBar()->showMessage(tr("Playing sample: %1 (%2 ms)")
                                 .arg(sec.name).arg(m_audioPlayer->durationMs()), 5000);
}

void MainWindow::stopAudioPlayback()
{
    if (m_audioPlayer && m_audioPlayer->isPlaying())
        m_audioPlayer->stop();
}

void MainWindow::exportAudioSample()
{
    if (!hexEdit || !m_sectionModel)
        return;

    const qint64 curPos = hexEdit->cursorPosition() / 2;
    const qint64 fileSize = hexEdit->dataSize();
    const int idx = m_sectionModel->sectionIndexAtOffset(curPos);
    if (idx < 0) return;

    const Section &sec = m_sectionModel->at(idx);
    if (sec.displayMode != SectionDisplay_Audio) {
        statusBar()->showMessage(tr("No audio section at cursor position"), 3000);
        return;
    }

    const qint64 secEnd = m_sectionModel->endOffsetOf(idx, fileSize);
    const QByteArray sampleData = hexEdit->dataAt(sec.startOffset, secEnd - sec.startOffset);

    AudioSampleFormat fmt = AudioSampleFormat::Raw_PCM8_Unsigned;
    if (sec.name.contains(QStringLiteral("BRR"))) fmt = AudioSampleFormat::SNES_BRR;
    else if (sec.name.contains(QStringLiteral("UMK3"), Qt::CaseInsensitive)
             || sec.name.contains(QStringLiteral("IMA ADPCM"), Qt::CaseInsensitive)
             || sec.name.contains(QStringLiteral("DPCM4"), Qt::CaseInsensitive)
             || sec.name.contains(QStringLiteral("4-bit DPCM"), Qt::CaseInsensitive)) fmt = AudioSampleFormat::MD_DPCM4_6500;
    else if (sec.name.contains(QStringLiteral("OKI"), Qt::CaseInsensitive)
             || sec.name.contains(QStringLiteral("Dialogic"), Qt::CaseInsensitive)) fmt = AudioSampleFormat::MD_ADPCM_OKI;
    else if (sec.name.contains(QStringLiteral("DPCM")))
        fmt = (m_detectedRomType == RomType::MD || m_detectedRomType == RomType::X32)
            ? AudioSampleFormat::MD_DPCM4_6500
            : AudioSampleFormat::NES_DPCM;
    else if (sec.name.contains(QStringLiteral("ulaw"), Qt::CaseInsensitive)
             || sec.name.contains(QStringLiteral("µ-law"), Qt::CaseInsensitive)) fmt = AudioSampleFormat::MD_ULAW;
    else if (sec.name.contains(QStringLiteral("Signed PCM"), Qt::CaseInsensitive)
             || sec.name.contains(QStringLiteral("Signed 8"), Qt::CaseInsensitive)) fmt = AudioSampleFormat::MD_PCM8_Signed;
    else if (sec.name.contains(QStringLiteral("DAC"))) fmt = AudioSampleFormat::MD_DAC_PCM;
    else if (sec.name.contains(QStringLiteral("GBA"))) fmt = AudioSampleFormat::GBA_PCM8;

    int rate = 0;
    const auto pcm = AudioDetector::decodeToPCM16(sampleData, fmt, &rate);
    if (pcm.isEmpty()) {
        QMessageBox::warning(this, tr("Export Audio"), tr("Failed to decode sample data."));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Audio Sample"), sec.name + QStringLiteral(".wav"), tr("WAV files (*.wav)"));
    if (path.isEmpty()) return;

    const QByteArray wav = AudioPlayer::createWavData(pcm, rate);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Export Audio"), tr("Cannot write file: %1").arg(f.errorString()));
        return;
    }
    f.write(wav);
    statusBar()->showMessage(tr("Exported: %1").arg(path), 5000);
}

void MainWindow::importAudioSample()
{
    if (!hexEdit || !m_sectionModel)
        return;

    const qint64 curPos = hexEdit->cursorPosition() / 2;
    const qint64 fileSize = hexEdit->dataSize();
    const int idx = m_sectionModel->sectionIndexAtOffset(curPos);
    if (idx < 0) return;

    const Section &sec = m_sectionModel->at(idx);
    if (sec.displayMode != SectionDisplay_Audio) {
        statusBar()->showMessage(tr("No audio section at cursor position"), 3000);
        return;
    }

    const qint64 secEnd = m_sectionModel->endOffsetOf(idx, fileSize);

    AudioSampleFormat fmt = AudioSampleFormat::Raw_PCM8_Unsigned;
    int targetRate = 8000;
    if (sec.name.contains(QStringLiteral("BRR"))) { fmt = AudioSampleFormat::SNES_BRR; targetRate = 32000; }
    else if (sec.name.contains(QStringLiteral("UMK3"), Qt::CaseInsensitive)
             || sec.name.contains(QStringLiteral("IMA ADPCM"), Qt::CaseInsensitive)
             || sec.name.contains(QStringLiteral("DPCM4"), Qt::CaseInsensitive)
             || sec.name.contains(QStringLiteral("4-bit DPCM"), Qt::CaseInsensitive)) { fmt = AudioSampleFormat::MD_DPCM4_6500; targetRate = 6500; }
    else if (sec.name.contains(QStringLiteral("OKI"), Qt::CaseInsensitive)
             || sec.name.contains(QStringLiteral("Dialogic"), Qt::CaseInsensitive)) { fmt = AudioSampleFormat::MD_ADPCM_OKI; targetRate = 7575; }
    else if (sec.name.contains(QStringLiteral("DPCM"))) {
        if (m_detectedRomType == RomType::MD || m_detectedRomType == RomType::X32) {
            fmt = AudioSampleFormat::MD_DPCM4_6500;
            targetRate = 6500;
        } else {
            fmt = AudioSampleFormat::NES_DPCM;
            targetRate = 8363;
        }
    }
    else if (sec.name.contains(QStringLiteral("ulaw"), Qt::CaseInsensitive)
             || sec.name.contains(QStringLiteral("µ-law"), Qt::CaseInsensitive)) { fmt = AudioSampleFormat::MD_ULAW; targetRate = 8000; }
    else if (sec.name.contains(QStringLiteral("Signed PCM"), Qt::CaseInsensitive)
             || sec.name.contains(QStringLiteral("Signed 8"), Qt::CaseInsensitive)) { fmt = AudioSampleFormat::MD_PCM8_Signed; targetRate = 8000; }
    else if (sec.name.contains(QStringLiteral("DAC"))) { fmt = AudioSampleFormat::MD_DAC_PCM; targetRate = 8000; }
    else if (sec.name.contains(QStringLiteral("GBA"))) { fmt = AudioSampleFormat::GBA_PCM8; targetRate = 13379; }

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Audio Sample"), QString(), tr("WAV files (*.wav);;All files (*)"));
    if (path.isEmpty()) return;

    const qint64 maxLen = secEnd - sec.startOffset;
    int sampleCount = 0;
    QByteArray encoded = AudioPlayer::importWav(path, fmt, targetRate, &sampleCount);
    if (encoded.isEmpty()) {
        QMessageBox::warning(this, tr("Import Audio"), tr("Failed to import WAV file."));
        return;
    }

    if (encoded.size() > maxLen) {
        const QString msg2 = tr("Imported data (%1 bytes) exceeds section size (%2 bytes). Truncate?")
                                .arg(encoded.size()).arg(maxLen);
        if (QMessageBox::question(this, tr("Import Audio"), msg2) != QMessageBox::Yes)
            return;
        encoded.truncate(static_cast<int>(maxLen));
    } else if (encoded.size() < maxLen) {
        const char silenceByte = (fmt == AudioSampleFormat::MD_DAC_PCM
                                  || fmt == AudioSampleFormat::Raw_PCM8_Unsigned
                                  || fmt == AudioSampleFormat::MD_ULAW)
                                     ? static_cast<char>(0x80) : '\0';
        encoded.append(QByteArray(static_cast<int>(maxLen) - encoded.size(), silenceByte));
    }

    hexEdit->replace(sec.startOffset, encoded.size(), encoded);
    statusBar()->showMessage(tr("Imported audio into section '%1'").arg(sec.name), 5000);
}

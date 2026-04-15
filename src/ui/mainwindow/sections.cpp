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

        qint64 endAfterReturnRun = -1;
        bool insideReturnRun = false;
        for (const auto &insn : insns) {
            if (nextFuncStart > funcStart && insn.fileOffset >= nextFuncStart)
                break;

            if (!insideReturnRun) {
                if (insn.isReturn) {
                    insideReturnRun = true;
                    endAfterReturnRun = insn.fileOffset + insn.size;
                }
                continue;
            }

            if (!insn.isReturn)
                break;

            endAfterReturnRun = insn.fileOffset + insn.size;
        }

        if (endAfterReturnRun > funcStart)
            return endAfterReturnRun;
        if (nextFuncStart > funcStart)
            return nextFuncStart;
        return qMin(fileSize, funcStart + qint64(2));
    }

    bool endsWithJumpToTarget(const QByteArray &fileData, Disassembler &disasm,
                              qint64 start, qint64 end, qint64 target)
    {
        if (end <= start)
            return false;

        const int scanBytes = static_cast<int>(qMin<qint64>(end - start, INT_MAX));
        const QVector<DisasmInstruction> insns = disasm.disassemble(fileData, start, scanBytes, 4096);
        for (int i = insns.size() - 1; i >= 0; --i) {
            const DisasmInstruction &insn = insns[i];
            if (insn.fileOffset + insn.size > end)
                continue;
            return insn.mnemonic.compare(QStringLiteral("JMP"), Qt::CaseInsensitive) == 0
                && insn.branchTarget == target;
        }
        return false;
    }

}

void MainWindow::addSectionFromSelection(int parentIdx)
{
    if (!hexEdit || !m_sectionModel)
        return;

    const qint64 selBegin = hexEdit->getSelectionBegin();
    const qint64 selEnd   = hexEdit->getSelectionEnd();
    if (selEnd - selBegin < 1)
        return;

    // parentIdx >= 0: explicit parent set by context menu "Add subsection"
    // parentIdx == -1: auto-detect by finding the deepest section that
    //                  contains the selection start
    if (parentIdx < 0) {
        int maxDepth = -1;
        for (int i = 0; i < m_sectionModel->count(); ++i) {
            const Section &sec = m_sectionModel->at(i);
            if (selBegin >= sec.startOffset && selBegin < sec.endOffset) {
                int d = 0;
                for (int pi = sec.parentIndex; pi >= 0; pi = m_sectionModel->at(pi).parentIndex)
                    ++d;
                if (d > maxDepth) { maxDepth = d; parentIdx = i; }
            }
        }
    }

    const int n = m_sectionModel->count() + 1;
    const QString title = (parentIdx < 0) ? tr("Add section") : tr("Add subsection");
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, title,
        tr("Section name:"), QLineEdit::Normal,
        tr("Section %1").arg(n), &ok);
    if (!ok || name.isEmpty())
        return;

    Section s;
    s.name = name;
    s.startOffset = selBegin;
    s.endOffset = selEnd;
    s.color = SectionListModel::randomPastelColor();
    s.parentIndex = parentIdx;

    QUndoStack *stack = hexEdit->undoStack();
    if (stack)
        stack->beginMacro((parentIdx < 0) ? tr("Add section") : tr("Add subsection"));

    m_sectionModel->addSection(s);

    // Add up to 2 blank visual rows before and after the section,
    // but only add line breaks that weren't already there.
    // Skip each boundary if it touches the start or end of the file.
    const qint64 fileSize = hexEdit->dataSize();
    if (selBegin > 0) {
        const qint64 pos = selBegin - 1;
        auto lb = hexEdit->lineBreaks();
        int cnt = static_cast<int>(std::count(lb.begin(), lb.end(), pos));
        for (int i = cnt; i < 2; ++i)
            hexEdit->addLineBreak(pos);
    }
    if (selEnd < fileSize) {
        const qint64 pos = selEnd - 1;
        auto lb = hexEdit->lineBreaks();
        int cnt = static_cast<int>(std::count(lb.begin(), lb.end(), pos));
        for (int i = cnt; i < 2; ++i)
            hexEdit->addLineBreak(pos);
    }

    if (stack)
        stack->endMacro();

    if (m_document)
        m_document->markDirty();
    hexEdit->viewport()->update();
}

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

int MainWindow::deepestSectionIndexForRange(qint64 selBegin, qint64 selEnd) const
{
    if (!m_sectionModel || selEnd <= selBegin)
        return -1;

    int bestIdx = -1;
    int bestDepth = -1;
    for (int i = 0; i < m_sectionModel->count(); ++i) {
        const Section &s = m_sectionModel->at(i);
        if (selBegin < s.startOffset || selEnd > s.endOffset)
            continue;

        int depth = 0;
        for (int pi = s.parentIndex; pi >= 0; pi = m_sectionModel->at(pi).parentIndex)
            ++depth;

        if (depth > bestDepth) {
            bestDepth = depth;
            bestIdx = i;
        }
    }

    return bestIdx;
}

bool MainWindow::canRemoveSelectionFromSection() const
{
    if (!hexEdit || !m_sectionModel)
        return false;

    const qint64 selBegin = hexEdit->getSelectionBegin();
    const qint64 selEnd = hexEdit->getSelectionEnd();
    if (selEnd - selBegin < 1)
        return false;

    const int idx = deepestSectionIndexForRange(selBegin, selEnd);
    if (idx < 0)
        return false;

    // Keep operation simple/safe: only for leaf sections.
    for (int i = 0; i < m_sectionModel->count(); ++i) {
        if (m_sectionModel->at(i).parentIndex == idx)
            return false;
    }

    const Section &s = m_sectionModel->at(idx);
    const qint64 cutStart = qBound(s.startOffset, selBegin, s.endOffset);
    const qint64 cutEnd = qBound(s.startOffset, selEnd, s.endOffset);
    if (cutStart >= cutEnd)
        return false;

    // "Part of section" only: full-section selection is excluded.
    return !(cutStart <= s.startOffset && cutEnd >= s.endOffset);
}

void MainWindow::removeSelectionFromSection()
{
    if (!canRemoveSelectionFromSection())
        return;

    const qint64 selBegin = hexEdit->getSelectionBegin();
    const qint64 selEnd = hexEdit->getSelectionEnd();
    const int idx = deepestSectionIndexForRange(selBegin, selEnd);
    if (idx < 0)
        return;

    QVector<Section> next = m_sectionModel->sections();
    Section s = next.at(idx);

    const qint64 cutStart = qBound(s.startOffset, selBegin, s.endOffset);
    const qint64 cutEnd = qBound(s.startOffset, selEnd, s.endOffset);
    if (cutStart >= cutEnd)
        return;

    if (cutStart <= s.startOffset) {
        next[idx].startOffset = cutEnd;
    } else if (cutEnd >= s.endOffset) {
        next[idx].endOffset = cutStart;
    } else {
        next[idx].endOffset = cutStart;

        Section tail = s;
        tail.name = s.name + QStringLiteral("-2");
        tail.startOffset = cutEnd;
        tail.endOffset = s.endOffset;
        next.append(tail);
    }

    m_sectionModel->applySections(next, tr("Remove from section"));
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
    QVector<QPair<qint64, qint64>> ptrBatch;

    if (rom == RomType::MD || rom == RomType::X32) {
        const QByteArray fileData = hexEdit->dataAt(0, hexEdit->dataSize());
        const qint64 fileSize = fileData.size();
        if (fileSize <= 0)
            return;

        const qint64 headerSize = qMin<qint64>(SectionListModel::romHeaderSize(rom), fileSize);
        const qint64 stackEnd = qMin<qint64>(4, fileSize);
        const qint64 entryStart = qMin<qint64>(4, fileSize);
        const qint64 entryEnd = qMin<qint64>(8, fileSize);
        const qint64 vectorStart = qMin<qint64>(8, fileSize);
        const qint64 vectorEnd = qMin<qint64>(0x100, fileSize);

        // ── "Header" parent branch (0x00 – headerSize) ──
        const int headerParentIdx = sections.size();
        {
            Section headerParent;
            headerParent.name = tr("Header");
            headerParent.startOffset = 0;
            headerParent.endOffset = headerSize;
            headerParent.color = SectionListModel::randomPastelColor();
            sections.append(headerParent);
        }

        if (stackEnd > 0) {
            Section stack;
            stack.name = tr("Stack pointer");
            stack.startOffset = 0;
            stack.endOffset = stackEnd;
            stack.color = SectionListModel::randomPastelColor();
            stack.parentIndex = headerParentIdx;
            stack.displayMode = SectionDisplay_Raw;
            sections.append(stack);
        }

        if (entryEnd > entryStart) {
            Section entry;
            entry.name = tr("Entry point");
            entry.startOffset = entryStart;
            entry.endOffset = entryEnd;
            entry.color = SectionListModel::randomPastelColor();
            entry.parentIndex = headerParentIdx;
            entry.displayMode = SectionDisplay_Raw;
            sections.append(entry);
        }

        int vectorParentIdx = -1;
        if (vectorEnd > vectorStart) {
            Section vectors;
            vectors.name = tr("Vector table");
            vectors.startOffset = vectorStart;
            vectors.endOffset = vectorEnd;
            vectors.color = SectionListModel::randomPastelColor();
            vectors.parentIndex = headerParentIdx;
            vectors.displayMode = SectionDisplay_Raw;
            vectorParentIdx = sections.size();
            sections.append(vectors);
        }

        if (vectorParentIdx >= 0) {
            for (const auto &vec : kMdVectors) {
                if (vec.offset < vectorStart || vec.offset + 4 > vectorEnd)
                    continue;

                Section vectorEntry;
                vectorEntry.name = tr(vec.name);
                vectorEntry.startOffset = vec.offset;
                vectorEntry.endOffset = vec.offset + 4;
                vectorEntry.color = SectionListModel::randomPastelColor();
                vectorEntry.parentIndex = vectorParentIdx;
                vectorEntry.displayMode = SectionDisplay_Raw;
                sections.append(vectorEntry);
            }
        }

        // System header (0x100-0x200): ROM metadata, publisher, version, etc.
        const qint64 sysHeaderStart = qMin<qint64>(0x100, fileSize);
        const qint64 sysHeaderEnd = qMin<qint64>(headerSize, fileSize);
        if (sysHeaderEnd > sysHeaderStart) {
            Section sysHeader;
            sysHeader.name = tr("System header");
            sysHeader.startOffset = sysHeaderStart;
            sysHeader.endOffset = sysHeaderEnd;
            sysHeader.color = SectionListModel::randomPastelColor();
            sysHeader.displayMode = SectionDisplay_Raw;
            sysHeader.parentIndex = headerParentIdx;
            sections.append(sysHeader);
        }

        // ── Collect vector target pointers and names for detectFunctions() ──
        QHash<qint64, QString> nameByTarget;
        auto addVectorTarget = [&](qint64 tableOffset, const QString &name) {
            const qint64 target = static_cast<qint64>(readBe32(fileData, tableOffset));
            if (target < headerSize || target >= fileSize)
                return;

            ptrBatch.append({tableOffset, PointerListModel::encodePtrValue(target, 4)});

            if (!nameByTarget.contains(target)) {
                nameByTarget.insert(target, name);
            } else if (name == tr("Entry point")) {
                nameByTarget[target] = name;
            }
        };

        for (const auto &vec : kMdVectors) {
            if (!vec.codeTarget)
                continue;
            addVectorTarget(vec.offset, tr(vec.name));
        }

        // Store vector names so detectFunctions() can label known entry points.
        m_vectorFunctionNames = nameByTarget;

        if (!sections.isEmpty()) {
            if (pushToUndo)
                m_sectionModel->applySections(sections, tr("Parse header"));
            else
                m_sectionModel->setSections(sections);
        }

        if (!ptrBatch.isEmpty()) {
            hexEdit->pointers()->addPointersBatch(ptrBatch);
            showPointersAct->setEnabled(!hexEdit->pointers()->empty());
        }

        auto ensureBreaksBefore = [this](qint64 offset) {
            if (offset <= 0)
                return;
            const qint64 pos = offset - 1;
            auto breaks = hexEdit->lineBreaks();
            const int existing = static_cast<int>(std::count(breaks.begin(), breaks.end(), pos));
            for (int i = existing; i < 2; ++i)
                hexEdit->addLineBreakDirect(pos);
        };

        ensureBreaksBefore(entryStart);
        ensureBreaksBefore(vectorStart);
        ensureBreaksBefore(sysHeaderStart);
        ensureBreaksBefore(headerSize);
    } else {
        const qint64 hdrSize = SectionListModel::romHeaderSize(rom);
        if (hdrSize <= 0)
            goto finalize;

        // Skip header creation if already parsed (preserve user sections).
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

            auto addSection = [&](const QString &name, qint64 start, qint64 end,
                                  int parent = -1, int dispMode = SectionDisplay_Default) {
                start = qMin(start, fileSize);
                end = qMin(end, fileSize);
                if (end <= start)
                    return -1;
                Section s;
                s.name = name;
                s.startOffset = start;
                s.endOffset = end;
                s.color = SectionListModel::randomPastelColor();
                s.parentIndex = parent;
                s.displayMode = dispMode;
                int idx = sections.size();
                sections.append(s);
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

            // ── NES (iNES / NES 2.0) ─────────────────────────────────
            if (rom == RomType::NES) {
                const int hp = addSection(tr("Header"), 0, headerSize);
                addSection(tr("Magic"), 0x00, 0x04, hp, SectionDisplay_Raw);
                addSection(tr("PRG/CHR size"), 0x04, 0x06, hp, SectionDisplay_Raw);
                addSection(tr("Flags"), 0x06, 0x0B, hp, SectionDisplay_Raw);
                addSection(tr("Padding"), 0x0B, headerSize, hp, SectionDisplay_Raw);
                ensureBreaksBefore(0x04);
                ensureBreaksBefore(0x06);
                ensureBreaksBefore(headerSize);

                // ── NES PRG-ROM / CHR-ROM split ──
                const int prgSize16k = (headerSize <= 4 || fileSize <= 4)
                    ? 0 : static_cast<unsigned char>(fileData.at(4));
                const int chrSize8k  = (headerSize <= 5 || fileSize <= 5)
                    ? 0 : static_cast<unsigned char>(fileData.at(5));
                const qint64 prgStart = headerSize;
                const qint64 prgEnd   = qMin<qint64>(prgStart + prgSize16k * 0x4000, fileSize);
                const qint64 chrStart = prgEnd;
                const qint64 chrEnd   = qMin<qint64>(chrStart + chrSize8k * 0x2000, fileSize);

                if (prgEnd > prgStart) {
                    addSection(tr("PRG-ROM"), prgStart, prgEnd);
                    ensureBreaksBefore(prgStart);
                }
                if (chrEnd > chrStart) {
                    addSection(tr("CHR-ROM"), chrStart, chrEnd, -1, SectionDisplay_Raw);
                    ensureBreaksBefore(chrStart);
                }
            }

            // ── Game Boy / Game Boy Color ─────────────────────────────
            else if (rom == RomType::GB || rom == RomType::GBC) {
                const int hp = addSection(tr("Header"), 0, headerSize);
                addSection(tr("RST / Interrupt vectors"), 0x00, 0x100, hp, SectionDisplay_Raw);
                addSection(tr("Entry point"), 0x100, 0x104, hp, SectionDisplay_Raw);
                addSection(tr("Nintendo logo"), 0x104, 0x134, hp, SectionDisplay_Raw);
                addSection(tr("Title"), 0x134, 0x144, hp, SectionDisplay_Raw);
                addSection(tr("Cartridge info"), 0x144, headerSize, hp, SectionDisplay_Raw);
                ensureBreaksBefore(0x100);
                ensureBreaksBefore(0x104);
                ensureBreaksBefore(0x134);
                ensureBreaksBefore(0x144);
                ensureBreaksBefore(headerSize);

                // Store entry point target for future disassembly
                if (fileSize >= 0x104) {
                    // GB entry point at 0x100 is typically: NOP + JP nn
                    // The jump target is at bytes 0x101..0x102 (little-endian 16-bit)
                    const quint16 jpTarget = static_cast<quint16>(
                        static_cast<unsigned char>(fileData.at(0x102)) << 8
                        | static_cast<unsigned char>(fileData.at(0x101)));
                    if (jpTarget >= headerSize && jpTarget < fileSize) {
                        QHash<qint64, QString> nameByTarget;
                        nameByTarget.insert(jpTarget, tr("Entry point"));
                        m_vectorFunctionNames = nameByTarget;
                    }
                }
            }

            // ── Game Boy Advance ──────────────────────────────────────
            else if (rom == RomType::GBA) {
                const int hp = addSection(tr("Header"), 0, headerSize);
                addSection(tr("Entry point"), 0x00, 0x04, hp, SectionDisplay_Raw);
                addSection(tr("Nintendo logo"), 0x04, 0xA0, hp, SectionDisplay_Raw);
                addSection(tr("Game title"), 0xA0, 0xAC, hp, SectionDisplay_Raw);
                addSection(tr("Game code"), 0xAC, 0xB0, hp, SectionDisplay_Raw);
                addSection(tr("System info"), 0xB0, headerSize, hp, SectionDisplay_Raw);
                ensureBreaksBefore(0x04);
                ensureBreaksBefore(0xA0);
                ensureBreaksBefore(0xB0);
                ensureBreaksBefore(headerSize);
            }

            // ── SNES with copier header ───────────────────────────────
            else if (rom == RomType::SNES_SMC || rom == RomType::SNES_HIROM_SMC) {
                const int hp = addSection(tr("Header"), 0, headerSize);
                addSection(tr("ROM size"), 0x00, 0x02, hp, SectionDisplay_Raw);
                addSection(tr("Flags"), 0x02, 0x03, hp, SectionDisplay_Raw);
                addSection(tr("Reserved"), 0x03, headerSize, hp, SectionDisplay_Raw);
                ensureBreaksBefore(0x03);
                ensureBreaksBefore(headerSize);
            }

            // ── Nintendo 64 ──────────────────────────────────────────
            else if (rom == RomType::N64 || rom == RomType::N64_LE || rom == RomType::N64_V64) {
                const int hp = addSection(tr("Header"), 0, headerSize);
                addSection(tr("PI config"), 0x00, 0x04, hp, SectionDisplay_Raw);
                addSection(tr("Clock rate"), 0x04, 0x08, hp, SectionDisplay_Raw);
                addSection(tr("Entry point"), 0x08, 0x0C, hp, SectionDisplay_Raw);
                addSection(tr("Release"), 0x0C, 0x10, hp, SectionDisplay_Raw);
                addSection(tr("CRC"), 0x10, 0x18, hp, SectionDisplay_Raw);
                addSection(tr("Reserved"), 0x18, 0x20, hp, SectionDisplay_Raw);
                addSection(tr("Name"), 0x20, 0x34, hp, SectionDisplay_Raw);
                addSection(tr("Cartridge info"), 0x34, headerSize, hp, SectionDisplay_Raw);
                ensureBreaksBefore(0x08);
                ensureBreaksBefore(0x18);
                ensureBreaksBefore(0x20);
                ensureBreaksBefore(0x34);
                ensureBreaksBefore(headerSize);
            }

            // ── Fallback: single "Header" section ─────────────────────
            else {
                addSection(tr("Header"), 0, headerSize, -1, SectionDisplay_Raw);
                ensureBreaksBefore(headerSize);
            }

            if (!sections.isEmpty()) {
                if (pushToUndo)
                    m_sectionModel->applySections(sections, tr("Parse header"));
                else
                    m_sectionModel->setSections(sections);
            }
        }
    }

finalize:
    if (m_sectionsDock) {
        m_sectionsDock->setRomTypeName(QString::fromLatin1(romTypeName(rom)));
        m_sectionsDock->setCurrentRomType(rom);
    }

    if (pushToUndo && m_document)
        m_document->markDirty();
}

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

    // Read the ROM data
    QByteArray romData = hexEdit->dataAt(0, fileSize);
    if (romData.size() < fileSize)
        return;

    // Disassemble and detect functions
    Disassembler disasm;
    if (!disasm.setRomType(romType)) {
        QMessageBox::warning(this, tr("Detect functions"),
                             tr("Failed to initialize disassembler."));
        return;
    }

    // Progress dialog
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
            // Reserve 0-80% for scanning; 80-100% for section building.
            progress.setValue(percent * 80 / 100);
            QApplication::processEvents();
            if (progress.wasCanceled())
                cancelled = true;
        },
        &callPointers);

    if (cancelled) {
        progress.close();
        return;
    }

    // For MD/X32 ROMs, always inject the entry point function from the
    // 32-bit BE address at offset 0x04 in the ROM header.
    if (romType == RomType::MD || romType == RomType::X32) {
        const qint64 entryTarget = static_cast<qint64>(readBe32(romData, 0x04));
        if (entryTarget >= codeStart && entryTarget < fileSize) {
            // Check if scanFunctions already found a function at this offset.
            bool alreadyFound = false;
            for (const auto &f : functions) {
                if (f.startOffset == entryTarget) {
                    alreadyFound = true;
                    break;
                }
            }
            if (!alreadyFound) {
                // Sort first so the next-start search is correct.
                std::sort(functions.begin(), functions.end(),
                          [](const DetectedFunction &a, const DetectedFunction &b) {
                              return a.startOffset < b.startOffset;
                          });
                qint64 nextStart = fileSize;
                for (const auto &f : functions) {
                    if (f.startOffset > entryTarget) {
                        nextStart = f.startOffset;
                        break;
                    }
                }
                const qint64 funcEnd = findFunctionEndByReturnRun(romData, disasm, entryTarget, nextStart);
                DetectedFunction df;
                df.startOffset = entryTarget;
                df.endOffset   = qBound(entryTarget + 1, funcEnd, qMax(entryTarget + 1, nextStart));
                df.cpuAddress  = static_cast<quint64>(entryTarget);
                functions.append(df);
            }
            // Ensure it's always named "Entry point" in the lookup table.
            m_vectorFunctionNames[entryTarget] = tr("Entry point");
        }
    }

    // Inject remaining vector-table targets that scanFunctions may have missed.
    {
        std::sort(functions.begin(), functions.end(),
                  [](const DetectedFunction &a, const DetectedFunction &b) {
                      return a.startOffset < b.startOffset;
                  });

        QSet<qint64> existingStarts;
        for (const auto &f : functions)
            existingStarts.insert(f.startOffset);

        for (auto it = m_vectorFunctionNames.constBegin();
             it != m_vectorFunctionNames.constEnd(); ++it) {
            const qint64 addr = it.key();
            if (addr < codeStart || addr >= fileSize)
                continue;
            if (existingStarts.contains(addr))
                continue;
            qint64 nextStart = fileSize;
            for (const auto &f : functions) {
                if (f.startOffset > addr) {
                    nextStart = f.startOffset;
                    break;
                }
            }
            const qint64 funcEnd = findFunctionEndByReturnRun(romData, disasm, addr, nextStart);

            DetectedFunction df;
            df.startOffset = addr;
            df.endOffset   = qBound(addr + 1, funcEnd, qMax(addr + 1, nextStart));
            df.cpuAddress  = static_cast<quint64>(addr);
            functions.append(df);
        }

        std::sort(functions.begin(), functions.end(),
                  [](const DetectedFunction &a, const DetectedFunction &b) {
                      return a.startOffset < b.startOffset;
                  });
    }

    if (functions.isEmpty()) {
        progress.close();
        QMessageBox::information(this, tr("Detect functions"),
                                 tr("No functions detected."));
        return;
    }

    progress.setLabelText(tr("Building sections..."));
    progress.setValue(82);
    QApplication::processEvents();

    // ── Build all sections and line breaks in bulk, then apply once ──

    // Remove old auto-detected Code parent + its children so re-parse
    // doesn't duplicate sections.  Non-disasm user sections are preserved.
    QVector<Section> allSections;
    {
        const auto &existing = m_sectionModel->sections();
        int oldCodeIdx = -1;
        for (int i = 0; i < existing.size(); ++i) {
            if (existing[i].parentIndex < 0
                && existing[i].displayMode == SectionDisplay_Disasm
                && existing[i].startOffset == codeStart) {
                oldCodeIdx = i;
                break;
            }
        }
        QSet<int> toRemove;
        if (oldCodeIdx >= 0) {
            toRemove.insert(oldCodeIdx);
            bool changed = true;
            while (changed) {
                changed = false;
                for (int i = 0; i < existing.size(); ++i) {
                    if (toRemove.contains(i)) continue;
                    if (existing[i].parentIndex >= 0 && toRemove.contains(existing[i].parentIndex)) {
                        toRemove.insert(i);
                        changed = true;
                    }
                }
            }
        }
        QHash<int, int> remap;
        int newIdx = 0;
        for (int i = 0; i < existing.size(); ++i) {
            if (!toRemove.contains(i))
                remap[i] = newIdx++;
        }
        for (int i = 0; i < existing.size(); ++i) {
            if (toRemove.contains(i)) continue;
            Section s = existing[i];
            if (s.parentIndex >= 0)
                s.parentIndex = remap.value(s.parentIndex, -1);
            allSections.append(s);
        }
    }

    const int codeParentIdx = allSections.size();

    Section codeSection;
    codeSection.name        = tr("Code");
    codeSection.startOffset = codeStart;
    codeSection.endOffset   = fileSize;
    codeSection.color       = SectionListModel::randomPastelColor();
    codeSection.parentIndex = -1;
    codeSection.displayMode = SectionDisplay_Disasm;
    codeSection.disasmCpu   = RomType::Unknown;
    allSections.append(codeSection);

    const int dataParentIdx = allSections.size();

    Section dataRootSection;
    dataRootSection.name        = tr("Data");
    dataRootSection.startOffset = codeStart;
    dataRootSection.endOffset   = fileSize;
    dataRootSection.color       = SectionListModel::randomPastelColor();
    dataRootSection.parentIndex = -1;
    dataRootSection.displayMode = SectionDisplay_Raw;
    dataRootSection.disasmCpu   = RomType::Unknown;
    allSections.append(dataRootSection);

    QVector<qint64> allBreaks = hexEdit->lineBreaks();

    // Helper: ensure 2 breaks at (offset - 1) so there is an empty header row.
    auto ensureBreaks = [&](qint64 offset) {
        if (offset <= 0) return;
        const qint64 pos = offset - 1;
        const int existing = static_cast<int>(std::count(allBreaks.begin(), allBreaks.end(), pos));
        for (int i = existing; i < 2; ++i)
            allBreaks.append(pos);
    };

    // Break before the Code section.
    ensureBreaks(codeStart);

    // Build function sections and data gaps.
    int dataSectionCounter = 0;
    qint64 prevEnd = codeStart; // tracks the end of previous section for gap detection

    for (int fi = 0; fi < functions.size(); ++fi) {
        const DetectedFunction &df = functions[fi];

        // Data gap BEFORE this function
        if (df.startOffset > prevEnd) {
            ++dataSectionCounter;
            Section dataSec;
            dataSec.name        = (dataSectionCounter == 1)
                                    ? tr("Data")
                                    : tr("Data-%1").arg(dataSectionCounter);
            dataSec.startOffset = prevEnd;
            dataSec.endOffset   = df.startOffset;
            dataSec.color       = SectionListModel::randomPastelColor();
            dataSec.parentIndex = dataParentIdx;
            dataSec.displayMode = SectionDisplay_Raw;
            dataSec.disasmCpu   = RomType::Unknown;
            allSections.append(dataSec);
            ensureBreaks(dataSec.startOffset);
        }

        // Apply vector table name if this function matches a known entry point.
        QString funcName = m_vectorFunctionNames.value(df.startOffset);
        if (funcName.isEmpty()) {
            funcName = QStringLiteral("sub_%1")
                .arg(df.cpuAddress, 0, 16, QLatin1Char('0')).toUpper();
        }

        Section funcSection;
        funcSection.name        = funcName;
        funcSection.startOffset = df.startOffset;
        funcSection.endOffset   = df.endOffset;
        funcSection.color       = SectionListModel::randomPastelColor();
        funcSection.parentIndex = codeParentIdx;
        funcSection.displayMode = SectionDisplay_Disasm;
        funcSection.disasmCpu   = RomType::Unknown;
        allSections.append(funcSection);
        ensureBreaks(funcSection.startOffset);

        prevEnd = df.endOffset;
    }

    // Trailing data gap after the last function
    if (prevEnd < fileSize) {
        ++dataSectionCounter;
        Section dataSec;
        dataSec.name        = (dataSectionCounter == 1)
                                ? tr("Data")
                                : tr("Data-%1").arg(dataSectionCounter);
        dataSec.startOffset = prevEnd;
        dataSec.endOffset   = fileSize;
        dataSec.color       = SectionListModel::randomPastelColor();
        dataSec.parentIndex = dataParentIdx;
        dataSec.displayMode = SectionDisplay_Raw;
        dataSec.disasmCpu   = RomType::Unknown;
        allSections.append(dataSec);
        ensureBreaks(dataSec.startOffset);
    }

    progress.setValue(90);
    QApplication::processEvents();

    std::sort(allBreaks.begin(), allBreaks.end());
    m_sectionModel->applySections(allSections, tr("Detect functions"));
    hexEdit->setLineBreaks(allBreaks);

    // Add call pointers (absolute-address JSR/JMP references) to the pointer list.
    // Only keep pointers whose target matches a detected function entry.
    if (!callPointers.isEmpty()) {
        QSet<qint64> funcStarts;
        for (const auto &f : functions)
            funcStarts.insert(f.startOffset);

        QVector<QPair<qint64, qint64>> ptrBatch;
        for (const auto &cp : callPointers) {
            if (funcStarts.contains(cp.targetOffset)) {
                ptrBatch.append({cp.ptrFileOffset,
                                 PointerListModel::encodePtrValue(cp.targetOffset, cp.ptrSize)});
            }
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
        if (funcStarts.contains(cp.targetOffset)) {
            ptrBatch.append({cp.ptrFileOffset,
                             PointerListModel::encodePtrValue(cp.targetOffset, cp.ptrSize)});
        }
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
    // Always sync to the active tab's editor and ROM profile before showing
    pointersDialog->setHexEdit(hexEdit);
    m_pointersDock->show();
    m_pointersDock->raise();
    pointersDialog->show();
    // Override profile AFTER show so it takes effect regardless of _profileInitialized
    pointersDialog->setRomProfile(m_detectedRomType, m_pointerOffset);

    // Restore per-tab "where" and "text optimization" options
    if (m_currentSession) {
        PointersDialog::State ps;
        ps.searchDir        = m_currentSession->ptrSearchDir;
        ps.excludeSelection = m_currentSession->ptrExcludeSelection;
        ps.alignedOnly      = m_currentSession->ptrAlignedOnly;
        ps.optimize         = m_currentSession->ptrOptimize;
        pointersDialog->setDialogState(ps);
    }
}


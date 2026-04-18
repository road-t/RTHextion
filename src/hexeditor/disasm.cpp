#include "internal.h"

// ═══════════════════════════════════════════════════════════════════════════
// HexEditor disassembly mode, section layout
// ═══════════════════════════════════════════════════════════════════════════

bool HexEditor::showDisasm() const
{
    return _showDisasm;
}


void HexEditor::setShowDisasm(bool mode)
{
    if (_showDisasm == mode)
        return;

    _showDisasm = mode;

    if (_showDisasm) {
        // Preserve the project's/manual line breaks once, then switch to the
        // full-file disassembly layout.
        if (!_savedLineBreaksValid) {
            _savedLineBreaks = _lineBreaks;
            _savedLineBreaksValid = true;
        }
        rebuildDisasmLayout();
    } else {
        // Return to the project layout, then re-apply section-specific disasm
        // wraps if any section still requests Disassembly mode.
        _disasmCache.clear();
        _disasmCacheStart = _disasmCacheEnd = -1;
        if (_savedLineBreaksValid)
            _lineBreaks = _savedLineBreaks;
        rebuildSectionAwareLayout();
    }

    emit disasmModeChanged(_showDisasm);
}


void HexEditor::setDisasmRomType(RomType type)
{
    if (_disasmRomType == type)
        return;
    _disasmRomType = type;

    if (!_disasm) {
        _disasm = new Disassembler();
    }
    bool ok = _disasm->setRomType(type);
    (void)ok;

    if (_showDisasm)
        rebuildDisasmLayout();
    else if (hasSectionDisasmMode())
        rebuildSectionAwareLayout();
}


void HexEditor::rebuildDisasmLayout()
{
    _disasmBoundaries.clear();
    _disasmCache.clear();
    _disasmCacheStart = _disasmCacheEnd = -1;

    if (!_disasm || !Disassembler::isSupported(_disasmRomType)) {
        _lineBreaks.clear();
        adjust();
        viewport()->update();
        emit lineBreaksChanged();
        return;
    }

    const QByteArray fileData = data();
    if (fileData.isEmpty()) {
        _lineBreaks.clear();
        adjust();
        viewport()->update();
        emit lineBreaksChanged();
        return;
    }

    // Lightweight boundary scan — only collects (offset, size), no strings
    _disasmBoundaries = _disasm->scanBoundaries(fileData, 0, fileData.size());

    // Build line breaks: each instruction ends at (fileOffset + size - 1)
    QVector<qint64> breaks;
    breaks.reserve(_disasmBoundaries.size());
    for (const auto &b : _disasmBoundaries) {
        const qint64 lastByte = b.offset + b.size - 1;
        if (lastByte < fileData.size() - 1)
            breaks.append(lastByte);
    }

    _lineBreaks = breaks;
    adjust();
    viewport()->update();
    emit lineBreaksChanged();
}


void HexEditor::ensureDisasmBoundaries()
{
    if (!_disasm || !Disassembler::isSupported(_disasmRomType))
        return;
    if (!_disasmBoundaries.isEmpty())
        return;

    const QByteArray fileData = data();
    if (fileData.isEmpty())
        return;

    // Per-section scanning when disasm sections exist, so that instruction
    // boundaries align to each section start rather than to file offset 0.
    if (_sectionModel && hasSectionDisasmMode()) {
        const qint64 fSize = fileData.size();
        for (int si = 0; si < _sectionModel->count(); ++si) {
            const auto &s = _sectionModel->at(si);
            if (s.displayMode != SectionDisplay_Disasm)
                continue;
            const qint64 start = qBound<qint64>(0, s.startOffset, fSize);
            const qint64 end = qBound<qint64>(0, _sectionModel->endOffsetOf(si, fSize), fSize);
            if (start < end) {
                const auto bndrs = _disasm->scanBoundaries(
                    fileData, start, static_cast<int>(end - start));
                _disasmBoundaries.append(bndrs);
            }
        }
    } else {
        _disasmBoundaries = _disasm->scanBoundaries(fileData, 0, fileData.size());
    }

    _disasmCache.clear();
    _disasmCacheStart = _disasmCacheEnd = -1;
}


bool HexEditor::hasSectionDisasmMode() const
{
    if (!_sectionModel)
        return false;
    for (const auto &s : _sectionModel->sections()) {
        if (s.displayMode == SectionDisplay_Disasm)
            return true;
    }
    return false;
}


bool HexEditor::isDisasmAt(qint64 offset) const
{
    if (_showDisasm)
        return true;
    return _sectionModel
        && _sectionModel->displayModeAtOffset(offset) == SectionDisplay_Disasm;
}

bool HexEditor::isAudioAt(qint64 offset) const
{
    return _sectionModel
        && _sectionModel->displayModeAtOffset(offset) == SectionDisplay_Audio;
}

bool HexEditor::isGraphicsAt(qint64 offset) const
{
    if (_showGraphicsPanel)
        return true;
    return _sectionModel
        && _sectionModel->displayModeAtOffset(offset) == SectionDisplay_Graphics;
}

// ── Global graphics panel mode ─────────────────────────────────

bool HexEditor::showGraphicsPanel() const { return _showGraphicsPanel; }

void HexEditor::setShowGraphicsPanel(bool mode)
{
    if (_showGraphicsPanel == mode)
        return;
    _showGraphicsPanel = mode;
    viewport()->update();
}

void HexEditor::setGlobalTileCodec(TileCodec codec)
{
    _globalTileCodec = codec;
    if (_showGraphicsPanel)
        viewport()->update();
}

void HexEditor::setGlobalTileCols(int cols)
{
    _globalTileCols = qMax(1, cols);
    if (_showGraphicsPanel)
        viewport()->update();
}

TileCodec HexEditor::globalTileCodec() const { return _globalTileCodec; }
int       HexEditor::globalTileCols()  const { return _globalTileCols; }


quint64 HexEditor::computeLayoutFingerprint() const
{
    if (!_sectionModel) return 0;
    const int count = _sectionModel->count();
    if (count == 0) return 0;
    const qint64 fileSize = _chunks ? _chunks->size() : 0;
    quint64 fp = quint64(count);
    // Layout depends on bytes-per-line (row count and graphics tail padding).
    fp = fp * 131 + quint64(qMax(1, _bytesPerLine));
    for (int i = 0; i < count; ++i) {
        const auto &s = _sectionModel->at(i);
        fp = fp * 131 + quint64(s.startOffset);
        fp = fp * 131 + quint64(_sectionModel->endOffsetOf(i, fileSize));
        fp = fp * 131 + quint64(s.displayMode);
    }
    return fp;
}


void HexEditor::rebuildSectionAwareLayout()
{
    // Global disassembly already owns the entire layout.
    if (_showDisasm) {
        rebuildDisasmLayout();
        return;
    }

    // Fast path: if sections + collapse state haven't changed, reuse cached layout.
    // Skip fast path when _savedLineBreaksValid — the user may have added/removed
    // individual breaks that need to be merged into _lineBreaks.
    const quint64 fp = computeLayoutFingerprint();
    if (fp != 0 && fp == _layoutFingerprint && !_lineBreaks.isEmpty()
        && !_savedLineBreaksValid) {
        readBuffers();
        viewport()->update();
        return;
    }

    QVector<qint64> baseBreaks = _savedLineBreaksValid ? _savedLineBreaks : _lineBreaks;
    const qint64 fileSize = _chunks ? _chunks->size() : 0;

    // Strip stale section-header double-breaks.  After a merge the header
    // doubles of removed sections remain in baseBreaks; remove them so a
    // merged section appears contiguous.
    if (_sectionModel && _sectionModel->count() > 0) {
        QSet<qint64> curSecBreaks;
        for (int si = 0; si < _sectionModel->count(); ++si) {
            const auto &sec = _sectionModel->at(si);
            curSecBreaks.insert(sec.startOffset == 0 ? qint64(-1) : sec.startOffset - 1);
        }
        QHash<qint64, int> freq;
        for (qint64 b : std::as_const(baseBreaks))
            freq[b]++;
        baseBreaks.erase(std::remove_if(baseBreaks.begin(), baseBreaks.end(),
            [&](qint64 b) { return freq[b] >= 2 && !curSecBreaks.contains(b); }),
            baseBreaks.end());
    }

    if (!hasSectionDisasmMode()) {
        // Even without disasm sections we must apply section-header gaps
        // and collapse logic to the base breaks.
        QVector<qint64> breaks = baseBreaks;

        // ── Ensure double breaks at every section start for header rows. ──
        if (_sectionModel) {
            QHash<qint64, int> breakFreq;
            breakFreq.reserve(breaks.size());
            for (qint64 b : std::as_const(breaks))
                breakFreq[b]++;

            for (int si = 0; si < _sectionModel->count(); ++si) {
                const auto &sec = _sectionModel->at(si);
                if (sec.startOffset == 0) {
                    // One -1 break = one empty row for the section header.
                    const int existing = breakFreq.value(-1, 0);
                    for (int j = existing; j < 1; ++j) {
                        breaks.append(-1);
                        breakFreq[-1]++;
                    }
                } else {
                    const qint64 pos = sec.startOffset - 1;
                    const int existing = breakFreq.value(pos, 0);
                    for (int j = existing; j < 2; ++j) {
                        breaks.append(pos);
                        breakFreq[pos]++;
                    }
                }
            }

            // Graphics sections: pad with empty rows so visible data rows are
            // aligned to full 8-pixel tile height.
            if (_bytesPerLine > 0) {
                for (int si = 0; si < _sectionModel->count(); ++si) {
                    const auto &sec = _sectionModel->at(si);
                    if (sec.displayMode != SectionDisplay_Graphics)
                        continue;

                    const qint64 start = qBound<qint64>(0, sec.startOffset, fileSize);
                    const qint64 end = qBound<qint64>(0, _sectionModel->endOffsetOf(si, fileSize), fileSize);
                    if (end <= start)
                        continue;

                    const qint64 bytes = end - start;
                    const int dataRows = static_cast<int>((bytes + _bytesPerLine - 1) / _bytesPerLine);

                    const int bpt = tileCodecBytesPerTile(sec.tileCodec);
                    const int tileCols = graphicsAutoTileCols(sec.tileCodec);
                    int padRows = 0;
                    if (bpt > 0 && tileCols > 0) {
                        const int totalTiles = static_cast<int>((bytes + bpt - 1) / bpt);
                        const int tileRows = (totalTiles + tileCols - 1) / tileCols;
                        const int virtualRows = tileRows * 8;
                        padRows = qMax(0, virtualRows - dataRows);
                    }
                    if (padRows <= 0)
                        continue;

                    const qint64 padPos = end - 1;
                    for (int j = 0; j < padRows; ++j)
                        breaks.append(padPos);
                }
            }
        }

        std::sort(breaks.begin(), breaks.end());

        if (_lineBreaks != breaks) {
            _lineBreaks = breaks;
            adjust();
            viewport()->update();
            emit lineBreaksChanged();
        } else {
            readBuffers();
            viewport()->update();
        }
        if (!_showDisasm) {
            _savedLineBreaks.clear();
            _savedLineBreaksValid = false;
        }
        _layoutFingerprint = fp;
        return;
    }

    if (!_savedLineBreaksValid) {
        _savedLineBreaks = _lineBreaks;
        _savedLineBreaksValid = true;
    }

    QVector<qint64> points;
    points.reserve((_sectionModel ? _sectionModel->count() : 0) * 2 + 2);
    points.append(0);
    points.append(fileSize);

    if (_sectionModel) {
        for (int si = 0; si < _sectionModel->count(); ++si) {
            const auto &s = _sectionModel->at(si);
            const qint64 start = qBound<qint64>(0, s.startOffset, fileSize);
            const qint64 end   = qBound<qint64>(0, _sectionModel->endOffsetOf(si, fileSize), fileSize);
            if (start < end) {
                points.append(start);
                points.append(end);
            }
        }
    }

    std::sort(points.begin(), points.end());
    points.erase(std::unique(points.begin(), points.end()), points.end());

    QVector<QPair<qint64, qint64>> disasmRanges;
    for (int i = 0; i + 1 < points.size(); ++i) {
        const qint64 start = points[i];
        const qint64 end = points[i + 1];
        if (start >= end)
            continue;
        if (_sectionModel && _sectionModel->displayModeAtOffset(start) == SectionDisplay_Disasm)
            disasmRanges.append({start, end});
    }

    // Scan instruction boundaries per-section so the disassembler starts
    // fresh at each section start (avoids bytes before a section being
    // swallowed into a multi-byte instruction that crosses the boundary).
    _disasmBoundaries.clear();
    _disasmCache.clear();
    _disasmCacheStart = _disasmCacheEnd = -1;
    if (_disasm && Disassembler::isSupported(_disasmRomType)) {
        const QByteArray fileData = data();
        if (!fileData.isEmpty()) {
            for (const auto &range : disasmRanges) {
                const auto bndrs = _disasm->scanBoundaries(
                    fileData, range.first,
                    static_cast<int>(range.second - range.first));
                _disasmBoundaries.append(bndrs);
            }
        }
    }

    QVector<qint64> breaks;
    breaks.reserve(baseBreaks.size() + _disasmBoundaries.size());

    // Keep manual/user breaks only outside section-disasm ranges.
    int rangeIdx = 0;
    for (qint64 brk : baseBreaks) {
        while (rangeIdx < disasmRanges.size() && brk >= disasmRanges[rangeIdx].second)
            ++rangeIdx;
        const bool insideDisasmRange = (rangeIdx < disasmRanges.size()
                                     && brk >= disasmRanges[rangeIdx].first
                                     && brk < disasmRanges[rangeIdx].second);
        if (!insideDisasmRange)
            breaks.append(brk);
    }

    // Add virtual line breaks so each disasm section behaves like global disasm.
    int instrIdx = 0;
    for (const auto &range : disasmRanges) {
        const qint64 start = range.first;
        const qint64 end   = range.second;
        if (start > 0)
            breaks.append(start - 1); // section starts on a fresh visual row

        while (instrIdx < _disasmBoundaries.size()
               && (_disasmBoundaries[instrIdx].offset + _disasmBoundaries[instrIdx].size) <= start)
            ++instrIdx;

        for (int i = instrIdx; i < _disasmBoundaries.size(); ++i) {
            const auto &b = _disasmBoundaries[i];
            const qint64 insnStart = b.offset;
            const qint64 insnEnd = b.offset + b.size;
            if (insnStart >= end)
                break;

            const qint64 clippedEnd = qMin(insnEnd, end);
            const qint64 lastByte = clippedEnd - 1;
            if (lastByte >= start && lastByte < fileSize - 1)
                breaks.append(lastByte);
        }

        if (end > 0 && end < fileSize)
            breaks.append(end - 1); // next non-disasm bytes also start on a fresh row
    }

    // ── Ensure double breaks at every section start for header rows. ──
    // Use a frequency set so we don't do O(n) std::count per section.
    if (_sectionModel) {
        QHash<qint64, int> breakFreq;
        breakFreq.reserve(breaks.size());
        for (qint64 b : std::as_const(breaks))
            breakFreq[b]++;

        for (int si = 0; si < _sectionModel->count(); ++si) {
            const auto &sec = _sectionModel->at(si);
            if (sec.startOffset == 0) {
                // One -1 break = one empty row for the section header.
                const int existing = breakFreq.value(-1, 0);
                for (int j = existing; j < 1; ++j) {
                    breaks.append(-1);
                    breakFreq[-1]++;
                }
            } else {
                const qint64 pos = sec.startOffset - 1;
                const int existing = breakFreq.value(pos, 0);
                for (int j = existing; j < 2; ++j) {
                    breaks.append(pos);
                    breakFreq[pos]++;
                }
            }
        }

        // Graphics sections: pad with empty rows so visible data rows are
        // aligned to full 8-pixel tile height.
        if (_bytesPerLine > 0) {
            for (int si = 0; si < _sectionModel->count(); ++si) {
                const auto &sec = _sectionModel->at(si);
                if (sec.displayMode != SectionDisplay_Graphics)
                    continue;

                const qint64 start = qBound<qint64>(0, sec.startOffset, fileSize);
                const qint64 end = qBound<qint64>(0, _sectionModel->endOffsetOf(si, fileSize), fileSize);
                if (end <= start)
                    continue;

                const qint64 bytes = end - start;
                const int dataRows = static_cast<int>((bytes + _bytesPerLine - 1) / _bytesPerLine);

                const int bpt = tileCodecBytesPerTile(sec.tileCodec);
                const int tileCols = graphicsAutoTileCols(sec.tileCodec);
                int padRows = 0;
                if (bpt > 0 && tileCols > 0) {
                    const int totalTiles = static_cast<int>((bytes + bpt - 1) / bpt);
                    const int tileRows = (totalTiles + tileCols - 1) / tileCols;
                    const int virtualRows = tileRows * 8;
                    padRows = qMax(0, virtualRows - dataRows);
                }
                if (padRows <= 0)
                    continue;

                const qint64 padPos = end - 1;
                for (int j = 0; j < padRows; ++j)
                    breaks.append(padPos);
            }
        }
    }

    std::sort(breaks.begin(), breaks.end());

    if (_lineBreaks != breaks) {
        _lineBreaks = breaks;
        adjust();
        viewport()->update();
        emit lineBreaksChanged();
    } else {
        readBuffers();
        viewport()->update();
    }
    _layoutFingerprint = fp;
}


int HexEditor::disasmBoundaryIndex(qint64 fileOffset) const
{
    if (_disasmBoundaries.isEmpty())
        return -1;
    int lo = 0, hi = _disasmBoundaries.size() - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        const auto &b = _disasmBoundaries[mid];
        if (fileOffset < b.offset)
            hi = mid - 1;
        else if (fileOffset >= b.offset + b.size)
            lo = mid + 1;
        else
            return mid;
    }
    return -1;
}


const DisasmInstruction *HexEditor::disasmInstructionAtOffset(qint64 fileOffset) const
{
    // Return from cache if available
    if (fileOffset >= _disasmCacheStart && fileOffset < _disasmCacheEnd) {
        for (const auto &instr : _disasmCache) {
            if (fileOffset >= instr.fileOffset && fileOffset < instr.fileOffset + instr.size)
                return &instr;
        }
    }

    // Find the boundary index for this offset
    const int idx = const_cast<HexEditor*>(this)->disasmBoundaryIndex(fileOffset);
    if (idx < 0 || !_disasm)
        return nullptr;

    // Disassemble a window of ~256 instructions around the target,
    // clipped to the current section to avoid misalignment.
    const int margin = 128;
    int startIdx = qMax(0, idx - margin);
    int endIdx = qMin(_disasmBoundaries.size() - 1, idx + margin);

    // Clip the cache window to the section that contains fileOffset so
    // the continuous disassembly does not cross a section boundary.
    if (_sectionModel) {
        const int secIdx = _sectionModel->sectionIndexAtOffset(fileOffset);
        if (secIdx >= 0) {
            const qint64 secStart = _sectionModel->at(secIdx).startOffset;
            const qint64 secEnd = _sectionModel->endOffsetOf(secIdx, _chunks->size());
            while (startIdx < idx && _disasmBoundaries[startIdx].offset < secStart)
                ++startIdx;
            while (endIdx > idx && _disasmBoundaries[endIdx].offset >= secEnd)
                --endIdx;
        }
    }

    const qint64 startOfs = _disasmBoundaries[startIdx].offset;
    const auto &lastB = _disasmBoundaries[endIdx];
    const qint64 endOfs = lastB.offset + lastB.size;
    const int bytes = static_cast<int>(endOfs - startOfs);

    // Disassemble against the real file offsets so Capstone formats
    // PC-relative branch targets (BNE/BEQ/etc.) correctly in the operand text.
    const QByteArray fileData = _chunks->data(0, _chunks->size());
    _disasmCache = _disasm->disassemble(fileData, startOfs, bytes);
    _disasmCacheStart = startOfs;
    _disasmCacheEnd = endOfs;

    // Search the freshly populated cache
    for (const auto &instr : _disasmCache) {
        if (fileOffset >= instr.fileOffset && fileOffset < instr.fileOffset + instr.size)
            return &instr;
    }
    return nullptr;
}


QString HexEditor::disasmDisplayText(const DisasmInstruction *instr) const
{
    if (!instr)
        return QString();

    QString displayOps = instr->operands;
    if (instr->isBranch && instr->branchTarget >= 0
        && instr->branchTarget < _chunks->size() && _sectionModel) {
        const QString label = _sectionModel->sectionNameAtStartOffset(instr->branchTarget);
        if (!label.isEmpty())
            displayOps = label;
    }

    if (displayOps.isEmpty())
        return instr->mnemonic;
    return instr->mnemonic + QStringLiteral(" ") + displayOps;
}


void HexEditor::setShowSections(bool show)
{
    _showSections = show;
    viewport()->update();
}


bool HexEditor::showSections()
{
    return _showSections;
}


void HexEditor::setSectionModel(SectionListModel *model)
{
    if (_sectionModel)
        disconnect(_sectionModel, nullptr, this, nullptr);

    _sectionModel = model;

    if (_sectionModel) {
        connect(_sectionModel, &SectionListModel::sectionsChanged, this, [this]() {
            rebuildSectionAwareLayout();
        });
    }

    rebuildSectionAwareLayout();
}


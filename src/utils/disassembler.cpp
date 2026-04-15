#include "disassembler.h"

#include <capstone/capstone.h>
#include <QSet>
#include <algorithm>

Disassembler::Disassembler() = default;

Disassembler::~Disassembler()
{
    close();
}

void Disassembler::close()
{
    if (m_open) {
        csh h = static_cast<csh>(m_handle);
        cs_close(&h);
        m_open = false;
        m_handle = 0;
    }
}

bool Disassembler::isSupported(RomType type)
{
    switch (type) {
    // 6502-based
    case RomType::NES:
    case RomType::Atari2600:
    case RomType::Atari7800:
        return true;

    // ARM (GBA)
    case RomType::GBA:
        return true;

    // M68K (Genesis/MD, 32X)
    case RomType::MD:
    case RomType::X32:
        return true;

    // MIPS (N64)
    case RomType::N64:
    case RomType::N64_LE:
    case RomType::N64_V64:
        return true;

    // x86-16 (WonderSwan — NEC V30MZ is 80186-compatible)
    case RomType::WonderSwan:
    case RomType::WonderSwanColor:
        return true;

    // SH-2 (32X — main CPUs)
    // Capstone has SH support but 32X uses the M68K for main code,
    // SH-2 is supplementary. We support M68K disassembly for 32X.

    // Not yet supported (need separate decoders):
    // Z80: GB, GBC, SMS, GG, SG1000, ColecoVision
    // 65816: SNES variants
    // 6502 (Atari 5200 uses 6502C — same as NES)
    case RomType::Atari5200:
        return true;

    default:
        return false;
    }
}

bool Disassembler::setRomType(RomType type)
{
    close();
    m_romType = type;

    cs_arch arch;
    cs_mode mode;

    switch (type) {
    // 6502-based platforms
    case RomType::NES:
    case RomType::Atari2600:
    case RomType::Atari5200:
    case RomType::Atari7800:
        arch = CS_ARCH_MOS65XX;
        mode = CS_MODE_MOS65XX_6502;
        break;

    // ARM — GBA uses ARM7TDMI (ARMv4T), mostly Thumb code
    case RomType::GBA:
        arch = CS_ARCH_ARM;
        mode = CS_MODE_THUMB;  // Default to Thumb; most GBA code is Thumb
        break;

    // Motorola 68000
    case RomType::MD:
    case RomType::X32:
        arch = CS_ARCH_M68K;
        mode = CS_MODE_M68K_000;
        break;

    // MIPS R4300i (N64)
    case RomType::N64:
        arch = CS_ARCH_MIPS;
        mode = static_cast<cs_mode>(CS_MODE_MIPS64 | CS_MODE_BIG_ENDIAN);
        break;
    case RomType::N64_LE:
        arch = CS_ARCH_MIPS;
        mode = static_cast<cs_mode>(CS_MODE_MIPS64 | CS_MODE_LITTLE_ENDIAN);
        break;
    case RomType::N64_V64:
        // V64 is byte-swapped; we'd need to unswap before disassembly.
        // For now treat as big-endian (caller should unswap pairs).
        arch = CS_ARCH_MIPS;
        mode = static_cast<cs_mode>(CS_MODE_MIPS64 | CS_MODE_BIG_ENDIAN);
        break;

    // NEC V30MZ (x86-16 compatible)
    case RomType::WonderSwan:
    case RomType::WonderSwanColor:
        arch = CS_ARCH_X86;
        mode = CS_MODE_16;
        break;

    default:
        return false;
    }

    csh h = 0;
    cs_err err = cs_open(arch, mode, &h);
    if (err != CS_ERR_OK)
        return false;

    // Enable detail mode for branch detection
    cs_option(h, CS_OPT_DETAIL, CS_OPT_ON);

    // Enable SKIPDATA so Capstone emits .byte pseudo-instructions for
    // undecipherable bytes instead of stopping at the first invalid opcode.
    cs_option(h, CS_OPT_SKIPDATA, CS_OPT_ON);

    m_handle = static_cast<size_t>(h);
    m_arch = static_cast<int>(arch);
    m_open = true;

    // Set base address (CPU address = file offset + base)
    m_baseAddress = -defaultPointerOffset(type);

    return true;
}

qint64 Disassembler::resolveTarget(quint64 cpuTarget) const
{
    // Convert CPU address back to file offset
    qint64 fileOffset = static_cast<qint64>(cpuTarget) - m_baseAddress;
    return fileOffset;
}

/// Check if an instruction is a branch/jump type
static bool isBranchInstruction(csh handle, const cs_insn *insn, cs_arch arch)
{
    if (!insn->detail)
        return false;

    // Check instruction groups for jump/branch/call
    for (uint8_t i = 0; i < insn->detail->groups_count; ++i) {
        uint8_t grp = insn->detail->groups[i];
        if (grp == CS_GRP_JUMP || grp == CS_GRP_CALL)
            return true;
    }

    // Architecture-specific fallback: detect common branch mnemonics
    const QString mn = QString::fromLatin1(insn->mnemonic).toLower();

    switch (arch) {
    case CS_ARCH_MOS65XX:
        if (mn == "jmp" || mn == "jsr" || mn == "bcc" || mn == "bcs" ||
            mn == "beq" || mn == "bne" || mn == "bmi" || mn == "bpl" ||
            mn == "bvc" || mn == "bvs")
            return true;
        break;

    case CS_ARCH_M68K:
        if (mn.startsWith("bra") || mn.startsWith("bsr") || mn.startsWith("bcc") ||
            mn.startsWith("bcs") || mn.startsWith("beq") || mn.startsWith("bne") ||
            mn.startsWith("bge") || mn.startsWith("bgt") || mn.startsWith("ble") ||
            mn.startsWith("blt") || mn.startsWith("bhi") || mn.startsWith("bls") ||
            mn.startsWith("bmi") || mn.startsWith("bpl") || mn.startsWith("bvc") ||
            mn.startsWith("bvs") || mn == "jmp" || mn == "jsr" || mn == "dbra" ||
            mn.startsWith("dbf") || mn.startsWith("dbeq") || mn.startsWith("dbne") ||
            mn.startsWith("dbcc") || mn.startsWith("dbcs") || mn.startsWith("dbhi") ||
            mn.startsWith("dbls") || mn.startsWith("dbge") || mn.startsWith("dbgt") ||
            mn.startsWith("dble") || mn.startsWith("dblt") || mn.startsWith("dbmi") ||
            mn.startsWith("dbpl") || mn.startsWith("dbvc") || mn.startsWith("dbvs"))
            return true;
        break;

    case CS_ARCH_MIPS:
        if (mn.startsWith("b") || mn == "j" || mn == "jr" ||
            mn == "jal" || mn == "jalr")
            return true;
        break;

    case CS_ARCH_ARM:
        if (mn == "b" || mn == "bl" || mn == "bx" || mn == "blx" ||
            mn.startsWith("b.") || mn == "cbz" || mn == "cbnz")
            return true;
        break;

    case CS_ARCH_X86:
        if (mn.startsWith("j") || mn == "call" || mn == "loop" ||
            mn == "loope" || mn == "loopne")
            return true;
        break;

    default:
        break;
    }

    (void)handle;
    return false;
}

/// Check if an instruction is a subroutine call (JSR, CALL, BL, JAL, BSR, etc.)
static bool isCallInstruction(csh handle, const cs_insn *insn)
{
    if (!insn->detail)
        return false;

    // Check Capstone CS_GRP_CALL group
    for (uint8_t i = 0; i < insn->detail->groups_count; ++i) {
        if (insn->detail->groups[i] == CS_GRP_CALL)
            return true;
    }

    // Architecture-specific fallback
    const QString mn = QString::fromLatin1(insn->mnemonic).toLower();

    // 6502: JSR
    if (mn == "jsr") return true;
    // M68K: JSR, BSR
    if (mn == "jsr" || mn.startsWith("bsr")) return true;
    // MIPS: JAL, JALR
    if (mn == "jal" || mn == "jalr") return true;
    // ARM/Thumb: BL, BLX
    if (mn == "bl" || mn == "blx") return true;
    // x86-16: CALL
    if (mn == "call") return true;

    (void)handle;
    return false;
}

/// Check if an instruction is a return (RTS, RTI, RET, BX LR, JR RA, etc.)
static bool isReturnInstruction(csh handle, const cs_insn *insn)
{
    if (!insn->detail)
        return false;

    // Check Capstone CS_GRP_RET / CS_GRP_IRET groups
    for (uint8_t i = 0; i < insn->detail->groups_count; ++i) {
        uint8_t grp = insn->detail->groups[i];
        if (grp == CS_GRP_RET || grp == CS_GRP_IRET)
            return true;
    }

    const QString mn = QString::fromLatin1(insn->mnemonic).toLower();

    // 6502: RTS, RTI
    if (mn == "rts" || mn == "rti") return true;
    // M68K: RTS, RTE, RTR
    if (mn == "rts" || mn == "rte" || mn == "rtr") return true;
    // MIPS: JR $RA (Capstone: "jr" with operand "$ra")
    if (mn == "jr") {
        const QString ops = QString::fromLatin1(insn->op_str).trimmed().toLower();
        if (ops == "$ra" || ops == "ra") return true;
    }
    // ARM/Thumb: BX LR, POP {…, PC}
    if (mn == "bx") {
        const QString ops = QString::fromLatin1(insn->op_str).trimmed().toLower();
        if (ops == "lr") return true;
    }
    if (mn == "pop") {
        const QString ops = QString::fromLatin1(insn->op_str).trimmed().toLower();
        if (ops.contains("pc")) return true;
    }
    // x86-16: RET, RETF, IRET
    if (mn == "ret" || mn == "retf" || mn == "iret") return true;

    (void)handle;
    return false;
}

/// Extract the numeric branch/call target as a CPU address.
/// Uses Capstone detail operands for the specific architecture first,
/// falls back to string parsing.
static qint64 extractBranchTarget(const cs_insn *insn, cs_arch arch)
{
    if (!insn->detail)
        return -1;

    // ── Capstone architecture-specific operand extraction ──
    // IMPORTANT: insn->detail is a union — only access the struct for the
    // current architecture, otherwise we read garbage.

#if CS_API_MAJOR >= 4
    switch (arch) {
    case CS_ARCH_M68K: {
        const cs_m68k *m68k = &insn->detail->m68k;
        for (uint8_t i = 0; i < m68k->op_count; ++i) {
            const cs_m68k_op &op = m68k->operands[i];
            if (op.type == M68K_OP_IMM)
                return static_cast<qint64>(op.imm);
            if (op.type == M68K_OP_BR_DISP)
                return static_cast<qint64>(insn->address) + 2 + op.br_disp.disp;
            // For M68K_OP_MEM (JSR/JMP absolute), mem.disp is int16_t which
            // is too narrow for 32-bit addresses.  Fall through to string
            // parsing which handles both short and long absolute reliably.
        }
        break;
    }
    case CS_ARCH_ARM: {
        const cs_arm *arm = &insn->detail->arm;
        for (uint8_t i = 0; i < arm->op_count; ++i) {
            if (arm->operands[i].type == ARM_OP_IMM)
                return static_cast<qint64>(arm->operands[i].imm);
        }
        break;
    }
    case CS_ARCH_X86: {
        const cs_x86 *x86 = &insn->detail->x86;
        for (uint8_t i = 0; i < x86->op_count; ++i) {
            if (x86->operands[i].type == X86_OP_IMM)
                return static_cast<qint64>(x86->operands[i].imm);
        }
        break;
    }
    case CS_ARCH_MIPS: {
        const cs_mips *mips = &insn->detail->mips;
        for (uint8_t i = 0; i < mips->op_count; ++i) {
            if (mips->operands[i].type == MIPS_OP_IMM)
                return static_cast<qint64>(mips->operands[i].imm);
        }
        break;
    }
    default:
        break;
    }
#else
    (void)arch;
#endif

    // ── Fallback: parse the operand string ──
    const QString ops = QString::fromLatin1(insn->op_str).trimmed();
    if (ops.isEmpty())
        return -1;

    auto isHex = [](QChar c) {
        return (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
            || (c >= QLatin1Char('a') && c <= QLatin1Char('f'))
            || (c >= QLatin1Char('A') && c <= QLatin1Char('F'));
    };

    auto parseNumericToken = [&](QString tok, qint64 &out) -> bool {
        tok = tok.trimmed();
        if (tok.isEmpty())
            return false;

        if (tok.startsWith(QLatin1Char('#')))
            tok = tok.mid(1).trimmed();

        // Normalize wrappers/suffixes like: ($EDFA).W, (0x1234), *$FF00
        tok.remove(QLatin1Char('('));
        tok.remove(QLatin1Char(')'));
        tok.remove(QLatin1Char('['));
        tok.remove(QLatin1Char(']'));
        tok.remove(QLatin1Char('*'));

        const QString tl = tok.toLower();
        if (tl.endsWith(QLatin1String(".w")) || tl.endsWith(QLatin1String(".l"))
            || tl.endsWith(QLatin1String(".b"))) {
            tok.chop(2);
        }

        bool ok = false;

        // $HEX (possibly not at token start)
        int pos = tok.indexOf(QLatin1Char('$'));
        if (pos >= 0) {
            int i = pos + 1;
            int j = i;
            while (j < tok.size() && isHex(tok[j])) ++j;
            if (j > i) {
                out = tok.mid(i, j - i).toLongLong(&ok, 16);
                if (ok) return true;
            }
        }

        // 0xHEX (possibly not at token start)
        pos = tok.indexOf(QLatin1String("0x"), 0, Qt::CaseInsensitive);
        if (pos >= 0) {
            int i = pos + 2;
            int j = i;
            while (j < tok.size() && isHex(tok[j])) ++j;
            if (j > i) {
                out = tok.mid(i, j - i).toLongLong(&ok, 16);
                if (ok) return true;
            }
        }

        out = tok.toLongLong(&ok, 16);
        if (ok) return true;
        out = tok.toLongLong(&ok, 10);
        return ok;
    };

    // For multi-operand instructions (e.g., DBRA d0, $1234), try each
    // comma-separated token from last to first, looking for a numeric literal.
    const QStringList parts = ops.split(QLatin1Char(','));
    for (int p = parts.size() - 1; p >= 0; --p) {
        qint64 val = -1;
        if (parseNumericToken(parts[p], val))
            return val;
    }

    return -1;
}

QVector<DisasmInstruction> Disassembler::disassemble(const QByteArray &data, qint64 offset,
                                                      int maxBytes, int maxInstr)
{
    QVector<DisasmInstruction> result;
    if (!m_open || offset < 0 || offset >= data.size())
        return result;

    const int available = qMin(maxBytes, static_cast<int>(data.size() - offset));
    if (available <= 0)
        return result;

    const uint8_t *code = reinterpret_cast<const uint8_t *>(data.constData() + offset);
    size_t codeSize = static_cast<size_t>(available);
    quint64 addr = static_cast<quint64>(offset + m_baseAddress);

    csh h = static_cast<csh>(m_handle);

    // Use cs_disasm_iter for memory-efficient one-at-a-time disassembly.
    // With CS_OPT_SKIPDATA enabled, undecipherable bytes become .byte pseudo-ops.
    cs_insn *insn = cs_malloc(h);
    if (!insn)
        return result;

    const int limit = (maxInstr > 0) ? maxInstr : INT_MAX;

    while (codeSize > 0 && result.size() < limit) {
        if (!cs_disasm_iter(h, &code, &codeSize, &addr, insn))
            break;

        DisasmInstruction di;
        di.fileOffset = static_cast<qint64>(insn->address) - m_baseAddress;
        di.address = insn->address;
        di.size = static_cast<int>(insn->size);
        di.mnemonic = QString::fromLatin1(insn->mnemonic).toUpper();
        di.operands = QString::fromLatin1(insn->op_str);

        // Opcode size heuristic: for M68K the operation word is always 2 bytes;
        // for fixed-width ISAs (MIPS, ARM) the opcode is the full word;
        // for variable-length ISAs (x86, 6502) we default to full size.
        switch (m_romType) {
        case RomType::MD:
        case RomType::X32:
            di.opcodeSize = qMin(2, di.size);
            break;
        default:
            di.opcodeSize = di.size;
            break;
        }

        // Format raw bytes
        QString bytesHex;
        for (int b = 0; b < di.size; ++b) {
            if (b > 0) bytesHex += QLatin1Char(' ');
            bytesHex += QString::number(insn->bytes[b], 16)
                            .toUpper().rightJustified(2, QLatin1Char('0'));
        }
        di.bytes = bytesHex;

        di.isBranch = isBranchInstruction(h, insn, static_cast<cs_arch>(m_arch));
        di.isCall   = isCallInstruction(h, insn);
        di.isReturn = isReturnInstruction(h, insn);

        if (di.isBranch) {
            qint64 target = extractBranchTarget(insn, static_cast<cs_arch>(m_arch));
            if (target >= 0) {
                di.branchTarget = resolveTarget(static_cast<quint64>(target));
            } else {
                di.branchTarget = -1;
            }
        } else {
            di.branchTarget = -1;
        }

        result.append(di);
    }

    cs_free(insn, 1);
    return result;
}

QVector<InsnBoundary> Disassembler::scanBoundaries(
    const QByteArray &data, qint64 offset, int maxBytes)
{
    QVector<InsnBoundary> result;
    if (!m_open || offset < 0 || offset >= data.size())
        return result;

    const int available = qMin(maxBytes, static_cast<int>(data.size() - offset));
    if (available <= 0)
        return result;

    const uint8_t *code = reinterpret_cast<const uint8_t *>(data.constData() + offset);
    size_t codeSize = static_cast<size_t>(available);
    quint64 addr = static_cast<quint64>(offset + m_baseAddress);

    csh h = static_cast<csh>(m_handle);
    cs_insn *insn = cs_malloc(h);
    if (!insn)
        return result;

    // Estimate: M68K averages ~3 bytes/insn, 6502 ~2, ARM ~4
    result.reserve(available / 2);

    while (codeSize > 0) {
        if (!cs_disasm_iter(h, &code, &codeSize, &addr, insn))
            break;
        const qint64 fOfs = static_cast<qint64>(insn->address) - m_baseAddress;
        result.append({fOfs, static_cast<int>(insn->size)});
    }

    cs_free(insn, 1);
    return result;
}

QVector<DetectedFunction> Disassembler::scanFunctions(
    const QByteArray &data, qint64 offset, int maxBytes,
    std::function<void(int)> progressCb,
    QVector<CallPointer> *outCallPointers)
{
    QVector<DetectedFunction> funcs;
    if (!m_open || offset < 0 || offset >= data.size())
        return funcs;

    const int available = qMin(maxBytes, static_cast<int>(data.size() - offset));
    if (available <= 0)
        return funcs;

    const uint8_t *codeBase = reinterpret_cast<const uint8_t *>(data.constData() + offset);
    const uint8_t *code = codeBase;
    size_t codeSize = static_cast<size_t>(available);
    quint64 addr = static_cast<quint64>(offset + m_baseAddress);
    const qint64 fileStart = offset;
    const qint64 fileEnd   = offset + available;

    csh h = static_cast<csh>(m_handle);
    cs_insn *insn = cs_malloc(h);
    if (!insn)
        return funcs;

    // Phase 1: single pass — collect CALL targets (absolute addresses within ROM)
    //          and RET positions (file offset of the RET instruction itself)
    QSet<qint64> callTargets;
    QVector<qint64> retEndOffsets;  // file offset one past each RET

    int bytesProcessed = 0;
    const int progressStep = qMax(1, available / 100);
    int nextProgress = progressStep;

    while (codeSize > 0) {
        if (!cs_disasm_iter(h, &code, &codeSize, &addr, insn))
            break;

        // Progress callback
        bytesProcessed = available - static_cast<int>(codeSize);
        if (progressCb && bytesProcessed >= nextProgress) {
            progressCb(bytesProcessed * 50 / available);  // 0-50% for phase 1
            nextProgress = bytesProcessed + progressStep;
        }

        if (isCallInstruction(h, insn)) {
            qint64 cpuTarget = extractBranchTarget(insn, static_cast<cs_arch>(m_arch));
            if (cpuTarget >= 0) {
                qint64 targetFileOfs = resolveTarget(static_cast<quint64>(cpuTarget));
                if (targetFileOfs >= fileStart && targetFileOfs < fileEnd) {
                    // Collect pointer info for calls with embedded absolute
                    // addresses. For M68K, only these are treated as function
                    // entry evidence (filters local BSR label noise).
                    const qint64 instrFileOfs = static_cast<qint64>(insn->address) - m_baseAddress;
                    int ptrOfs = -1, ptrSize = -1;
                    bool hasEmbeddedPtr = false;
#if CS_API_MAJOR >= 4
                    switch (static_cast<cs_arch>(m_arch)) {
                    case CS_ARCH_M68K: {
                        const QString mn = QString::fromLatin1(insn->mnemonic).toLower();
                        if (mn == "jsr" || mn == "jmp") {
                            const cs_m68k *m68k = &insn->detail->m68k;
                            for (uint8_t j = 0; j < m68k->op_count; ++j) {
                                if (m68k->operands[j].address_mode == M68K_AM_ABSOLUTE_DATA_LONG) {
                                    ptrOfs = 2;
                                    ptrSize = 4;
                                    hasEmbeddedPtr = true;
                                    break;
                                } else if (m68k->operands[j].address_mode == M68K_AM_ABSOLUTE_DATA_SHORT) {
                                    ptrOfs = 2;
                                    ptrSize = 2;
                                    hasEmbeddedPtr = true;
                                    break;
                                }
                            }
                        }
                        break;
                    }
                    case CS_ARCH_MOS65XX: {
                        const QString mn = QString::fromLatin1(insn->mnemonic).toLower();
                        if (mn == "jsr" && insn->size == 3) {
                            ptrOfs = 1;
                            ptrSize = 2;
                            hasEmbeddedPtr = true;
                        }
                        break;
                    }
                    default:
                        break;
                    }
#endif

                    const cs_arch arch = static_cast<cs_arch>(m_arch);
                    const bool acceptAsFunctionEntry = (arch == CS_ARCH_M68K)
                        ? hasEmbeddedPtr
                        : true;

                    if (acceptAsFunctionEntry)
                        callTargets.insert(targetFileOfs);

                    if (outCallPointers && hasEmbeddedPtr && ptrOfs >= 0 && ptrSize > 0)
                        outCallPointers->append({instrFileOfs + ptrOfs, targetFileOfs, ptrSize});
                }
            }
        }

        if (isReturnInstruction(h, insn)) {
            qint64 fOfs = static_cast<qint64>(insn->address) - m_baseAddress;
            retEndOffsets.append(fOfs + insn->size);
        }
    }

    cs_free(insn, 1);

    if (callTargets.isEmpty())
        return funcs;

    // Phase 2: build functions from call targets + RET endings
    QVector<qint64> sortedTargets(callTargets.begin(), callTargets.end());
    std::sort(sortedTargets.begin(), sortedTargets.end());
    std::sort(retEndOffsets.begin(), retEndOffsets.end());

    if (progressCb)
        progressCb(55);

    for (int i = 0; i < sortedTargets.size(); ++i) {
        const qint64 funcStart = sortedTargets[i];
        const qint64 nextFuncStart = (i + 1 < sortedTargets.size())
                                         ? sortedTargets[i + 1]
                                         : fileEnd;

        // Find the first RET-end after funcStart — RTS ends the function.
        auto it = std::lower_bound(retEndOffsets.begin(), retEndOffsets.end(), funcStart + 1);
        if (it == retEndOffsets.end())
            continue;

        qint64 funcEnd = *it;

        DetectedFunction df;
        df.startOffset = funcStart;
        df.endOffset   = funcEnd;
        df.cpuAddress  = static_cast<quint64>(funcStart + m_baseAddress);
        funcs.append(df);

        if (progressCb)
            progressCb(55 + (i + 1) * 45 / sortedTargets.size());
    }

    return funcs;
}

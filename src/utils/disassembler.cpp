#include "disassembler.h"

#include <capstone/capstone.h>

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

    m_handle = static_cast<size_t>(h);
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
static bool isBranchInstruction(csh handle, const cs_insn *insn)
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

    // 6502
    if (mn == "jmp" || mn == "jsr" || mn == "bcc" || mn == "bcs" ||
        mn == "beq" || mn == "bne" || mn == "bmi" || mn == "bpl" ||
        mn == "bvc" || mn == "bvs")
        return true;

    // M68K
    if (mn.startsWith("bra") || mn.startsWith("bsr") || mn.startsWith("bcc") ||
        mn.startsWith("bcs") || mn.startsWith("beq") || mn.startsWith("bne") ||
        mn.startsWith("bge") || mn.startsWith("bgt") || mn.startsWith("ble") ||
        mn.startsWith("blt") || mn.startsWith("bhi") || mn.startsWith("bls") ||
        mn.startsWith("bmi") || mn.startsWith("bpl") || mn.startsWith("bvc") ||
        mn.startsWith("bvs") || mn == "jmp" || mn == "jsr" || mn == "dbra" ||
        mn.startsWith("db"))
        return true;

    // MIPS
    if (mn.startsWith("b") || mn == "j" || mn == "jr" ||
        mn == "jal" || mn == "jalr")
        return true;

    // ARM/Thumb
    if (mn == "b" || mn == "bl" || mn == "bx" || mn == "blx" ||
        mn.startsWith("b.") || mn == "cbz" || mn == "cbnz")
        return true;

    // x86-16
    if (mn.startsWith("j") || mn == "call" || mn == "loop" ||
        mn == "loope" || mn == "loopne")
        return true;

    (void)handle;
    return false;
}

/// Try to extract the numeric branch target from the operand string.
static qint64 extractBranchTarget(const cs_insn *insn)
{
    if (!insn->detail)
        return -1;

    // For x86: check immediate operands
    // For ARM: check immediate operands
    // For MIPS: check immediate operands
    // For M68K: check immediate operands
    // Generic approach: try to parse the operand string as a hex number

    const QString ops = QString::fromLatin1(insn->op_str).trimmed();
    if (ops.isEmpty())
        return -1;

    // Try parsing as "0xNNNN" or "$NNNN" or plain number
    QString numStr = ops;

    // Remove leading # for ARM immediate
    if (numStr.startsWith('#'))
        numStr = numStr.mid(1);

    // Remove $ prefix (6502 style)
    if (numStr.startsWith('$'))
        numStr = numStr.mid(1);

    // Remove 0x prefix
    bool ok = false;
    qint64 val = -1;

    if (numStr.startsWith("0x", Qt::CaseInsensitive)) {
        val = numStr.mid(2).toLongLong(&ok, 16);
    } else {
        // Try hex first (without prefix)
        val = numStr.toLongLong(&ok, 16);
        if (!ok) {
            // Try decimal
            val = numStr.toLongLong(&ok, 10);
        }
    }

    return ok ? val : -1;
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
    const quint64 startAddr = static_cast<quint64>(offset + m_baseAddress);

    csh h = static_cast<csh>(m_handle);
    cs_insn *insn = nullptr;

    size_t count = cs_disasm(h, code, static_cast<size_t>(available), startAddr,
                             maxInstr > 0 ? static_cast<size_t>(maxInstr) : 0, &insn);

    if (count == 0)
        return result;

    result.reserve(static_cast<int>(count));

    for (size_t i = 0; i < count; ++i) {
        DisasmInstruction di;
        di.fileOffset = static_cast<qint64>(insn[i].address) - m_baseAddress;
        di.address = insn[i].address;
        di.size = static_cast<int>(insn[i].size);
        di.mnemonic = QString::fromLatin1(insn[i].mnemonic).toUpper();
        di.operands = QString::fromLatin1(insn[i].op_str);

        // Format raw bytes
        QString bytesHex;
        for (int b = 0; b < di.size; ++b) {
            if (b > 0) bytesHex += QLatin1Char(' ');
            bytesHex += QString::number(insn[i].bytes[b], 16)
                            .toUpper().rightJustified(2, QLatin1Char('0'));
        }
        di.bytes = bytesHex;

        di.isBranch = isBranchInstruction(h, &insn[i]);

        if (di.isBranch) {
            qint64 target = extractBranchTarget(&insn[i]);
            if (target >= 0) {
                di.branchTarget = resolveTarget(static_cast<quint64>(target));
            } else {
                di.branchTarget = -1;  // Indirect or unparseable
            }
        } else {
            di.branchTarget = -1;
        }

        result.append(di);
    }

    cs_free(insn, count);
    return result;
}

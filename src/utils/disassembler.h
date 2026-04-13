#ifndef DISASSEMBLER_H
#define DISASSEMBLER_H

#include <QString>
#include <QByteArray>
#include <QVector>
#include <cstdint>
#include <functional>

#include "romdetect.h"

// ── CPU enumeration helpers for disassembly UI ─────────────────

/// Information about a disassembly-capable CPU for UI menus.
struct DisasmCpuEntry {
    RomType representativeRom;  ///< canonical RomType for this CPU
    const char *cpuName;        ///< e.g. "MOS 6502", "Motorola 68000"
};

/// Returns the display name for the CPU used by the given RomType's disassembler,
/// or nullptr if that RomType is not supported for disassembly.
inline const char *disasmCpuName(RomType type)
{
    switch (type) {
    case RomType::NES:
    case RomType::Atari2600:
    case RomType::Atari5200:
    case RomType::Atari7800:
        return "MOS 6502";
    case RomType::GBA:
        return "ARM7 (Thumb)";
    case RomType::MD:
    case RomType::X32:
        return "Motorola 68000";
    case RomType::N64:
    case RomType::N64_LE:
    case RomType::N64_V64:
        return "MIPS R4300i";
    case RomType::WonderSwan:
    case RomType::WonderSwanColor:
        return "x86-16 (V30MZ)";
    default:
        return nullptr;
    }
}

/// Maps a RomType to the canonical representative RomType for its CPU architecture.
/// Used so that e.g. NES / Atari2600 / Atari7800 all map to the same "6502" CPU.
inline RomType disasmCanonicalRom(RomType type)
{
    switch (type) {
    case RomType::NES: case RomType::Atari2600:
    case RomType::Atari5200: case RomType::Atari7800:
        return RomType::NES;
    case RomType::GBA:
        return RomType::GBA;
    case RomType::MD: case RomType::X32:
        return RomType::MD;
    case RomType::N64: case RomType::N64_LE: case RomType::N64_V64:
        return RomType::N64;
    case RomType::WonderSwan: case RomType::WonderSwanColor:
        return RomType::WonderSwan;
    default:
        return RomType::Unknown;
    }
}

/// All unique CPU entries available for disassembly, for menu population.
inline QVector<DisasmCpuEntry> disasmSupportedCpus()
{
    return {
        { RomType::NES,        "MOS 6502" },
        { RomType::GBA,        "ARM7 (Thumb)" },
        { RomType::MD,         "Motorola 68000" },
        { RomType::N64,        "MIPS R4300i" },
        { RomType::WonderSwan, "x86-16 (V30MZ)" },
    };
}

/// One disassembled instruction.
struct DisasmInstruction {
    qint64  fileOffset;   ///< Offset in the file
    quint64 address;      ///< CPU address (file offset + base)
    int     size;         ///< Instruction size in bytes
    int     opcodeSize;   ///< Bytes for the opcode/mnemonic part (remainder is operand encoding)
    QString mnemonic;     ///< e.g. "LDA", "JMP", "MOV"
    QString operands;     ///< e.g. "#$40", "$C000", "R0, R1"
    QString bytes;        ///< Raw bytes as hex string
    bool    isBranch;     ///< Is this a branch/jump instruction?
    bool    isCall;       ///< Is this a subroutine call (JSR, CALL, BL, JAL, etc.)?
    bool    isReturn;     ///< Is this a return instruction (RTS, RET, BX LR, JR RA, etc.)?
    qint64  branchTarget; ///< File offset of branch target (-1 if not a branch or unresolvable)
};

/// Lightweight instruction boundary for fast full-file scanning.
struct InsnBoundary { qint64 offset; int size; };

/// Detected function entry with its start/end file offsets.
struct DetectedFunction {
    qint64  startOffset;  ///< File offset of the first instruction
    qint64  endOffset;    ///< File offset one past the last byte (exclusive)
    quint64 cpuAddress;   ///< CPU address of the entry point
};

/// A reference from a call/jump instruction to its target,
/// where the target address is embedded as absolute bytes in the instruction.
struct CallPointer {
    qint64 ptrFileOffset; ///< File offset of the embedded address bytes
    qint64 targetOffset;  ///< File offset of the call target
    int    ptrSize;       ///< Byte size of the embedded address (2 or 4)
};

/// Disassembler wrapper around Capstone.
/// Supports architectures via RomType mapping.
class Disassembler
{
public:
    Disassembler();
    ~Disassembler();

    /// Returns true if the given ROM type can be disassembled.
    static bool isSupported(RomType type);

    /// Set the ROM type to disassemble for. Initialises Capstone engine.
    /// Returns true on success.
    bool setRomType(RomType type);

    /// Disassemble a region of data.
    /// @param data      Raw file bytes
    /// @param offset    Starting file offset
    /// @param maxBytes  Maximum number of bytes to disassemble
    /// @param maxInstr  Maximum number of instructions (0 = unlimited)
    /// @return Vector of disassembled instructions
    QVector<DisasmInstruction> disassemble(const QByteArray &data, qint64 offset,
                                           int maxBytes, int maxInstr = 0);

    /// Lightweight scan: returns only (fileOffset, size) pairs for instruction
    /// boundaries without expensive QString formatting.
    /// Used by the hex editor to build line breaks for the entire file.
    QVector<InsnBoundary> scanBoundaries(const QByteArray &data, qint64 offset,
                                         int maxBytes);

    /// Scan the data for function boundaries by finding CALL targets and RET instructions.
    /// Returns detected functions sorted by start offset.
    /// progressCb receives 0-100 percent progress.
    /// If outCallPointers is non-null, absolute-address call references are appended.
    QVector<DetectedFunction> scanFunctions(const QByteArray &data, qint64 offset,
                                            int maxBytes,
                                            std::function<void(int)> progressCb = nullptr,
                                            QVector<CallPointer> *outCallPointers = nullptr);

    /// Returns the current ROM type.
    RomType romType() const { return m_romType; }

    /// Returns the CPU base address (for converting file offset <-> CPU address).
    qint64 baseAddress() const { return m_baseAddress; }

private:
    void close();
    qint64 resolveTarget(quint64 target) const;

    RomType  m_romType = RomType::Unknown;
    qint64   m_baseAddress = 0;  ///< CPU base address offset from file offset
    size_t   m_handle = 0;       ///< Capstone handle (csh)
    int      m_arch = 0;         ///< Capstone cs_arch for this ROM type
    bool     m_open = false;
};

#endif // DISASSEMBLER_H

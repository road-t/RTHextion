#ifndef DISASSEMBLER_H
#define DISASSEMBLER_H

#include <QString>
#include <QByteArray>
#include <QVector>
#include <cstdint>

#include "romdetect.h"

/// One disassembled instruction.
struct DisasmInstruction {
    qint64  fileOffset;   ///< Offset in the file
    quint64 address;      ///< CPU address (file offset + base)
    int     size;         ///< Instruction size in bytes
    QString mnemonic;     ///< e.g. "LDA", "JMP", "MOV"
    QString operands;     ///< e.g. "#$40", "$C000", "R0, R1"
    QString bytes;        ///< Raw bytes as hex string
    bool    isBranch;     ///< Is this a branch/jump instruction?
    qint64  branchTarget; ///< File offset of branch target (-1 if not a branch or unresolvable)
};

/// Lightweight instruction boundary for fast full-file scanning.
struct InsnBoundary { qint64 offset; int size; };

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

    /// Returns the current ROM type.
    RomType romType() const { return m_romType; }

private:
    void close();
    qint64 resolveTarget(quint64 target) const;

    RomType  m_romType = RomType::Unknown;
    qint64   m_baseAddress = 0;  ///< CPU base address offset from file offset
    size_t   m_handle = 0;       ///< Capstone handle (csh)
    bool     m_open = false;
};

#endif // DISASSEMBLER_H

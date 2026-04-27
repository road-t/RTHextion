#include "disassembler.h"

#ifdef HAVE_CAPSTONE
#if __has_include(<capstone/capstone.h>)
#include <capstone/capstone.h>
#elif __has_include(<capstone.h>)
#include <capstone.h>
#else
#error "HAVE_CAPSTONE is set but Capstone headers were not found"
#endif

#if (defined(CS_API_MAJOR) && (CS_API_MAJOR >= 5)) \
    || __has_include(<capstone/mos65xx.h>) \
    || __has_include(<mos65xx.h>)
#define HAVE_CAPSTONE_MOS65XX 1
#endif
#endif

#include <QMap>
#include <QSet>
#include <QDebug>
#include <algorithm>

namespace {

#ifdef HAVE_CAPSTONE
inline bool capstoneArchAvailable(cs_arch arch)
{
    return cs_support(static_cast<int>(arch));
}
#endif

inline bool isZ80RomType(RomType type)
{
    switch (type) {
    case RomType::GB:
    case RomType::GBC:
    case RomType::SMS:
    case RomType::GG:
    case RomType::SG1000:
    case RomType::ColecoVision:
        return true;
    default:
        return false;
    }
}

inline QString hex8(quint8 v)
{
    return QStringLiteral("$%1").arg(v, 2, 16, QLatin1Char('0')).toUpper();
}

inline QString hex16(quint16 v)
{
    return QStringLiteral("$%1").arg(v, 4, 16, QLatin1Char('0')).toUpper();
}

struct Z80Decoded {
    int size = 1;
    QString mnemonic;
    QString operands;
    bool isBranch = false;
    bool isCall = false;
    bool isReturn = false;
    bool hasFallthrough = true;
    qint64 cpuTarget = -1;
};

enum class Z80IndexMode {
    None,
    IX,
    IY,
};

inline QString z80IndexName(Z80IndexMode mode)
{
    switch (mode) {
    case Z80IndexMode::IX: return QStringLiteral("IX");
    case Z80IndexMode::IY: return QStringLiteral("IY");
    default: return QStringLiteral("HL");
    }
}

inline QString z80BaseReg8Name(int reg)
{
    static const char *reg8[] = {"B", "C", "D", "E", "H", "L", "(HL)", "A"};
    return QString::fromLatin1(reg8[reg & 0x7]);
}

inline QString z80ConditionName(int cond)
{
    static const char *cc[] = {"NZ", "Z", "NC", "C", "PO", "PE", "P", "M"};
    return QString::fromLatin1(cc[cond & 0x7]);
}

inline QString z80RotateName(int op)
{
    static const char *rot[] = {"RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SRL"};
    return QString::fromLatin1(rot[op & 0x7]);
}

inline QString z80AluName(int op)
{
    static const char *alu[] = {"ADD", "ADC", "SUB", "SBC", "AND", "XOR", "OR", "CP"};
    return QString::fromLatin1(alu[op & 0x7]);
}

inline QString z80MiscName(int op)
{
    static const char *misc[] = {"RLCA", "RRCA", "RLA", "RRA", "DAA", "CPL", "SCF", "CCF"};
    return QString::fromLatin1(misc[op & 0x7]);
}

inline QString z80PairName(int pair, Z80IndexMode indexMode)
{
    static const char *pairs[] = {"BC", "DE", "HL", "SP"};
    if (indexMode != Z80IndexMode::None && (pair & 0x3) == 2)
        return z80IndexName(indexMode);
    return QString::fromLatin1(pairs[pair & 0x3]);
}

inline QString z80PushPairName(int pair, Z80IndexMode indexMode)
{
    static const char *pairs[] = {"BC", "DE", "HL", "AF"};
    if (indexMode != Z80IndexMode::None && (pair & 0x3) == 2)
        return z80IndexName(indexMode);
    return QString::fromLatin1(pairs[pair & 0x3]);
}

inline QString z80IndexedMemoryOperand(Z80IndexMode mode, qint8 disp)
{
    if (mode == Z80IndexMode::None)
        return QStringLiteral("(HL)");

    const QString index = z80IndexName(mode);
    if (disp == 0)
        return QStringLiteral("(") + index + QStringLiteral(")");

    const QString sign = (disp < 0) ? QStringLiteral("-") : QStringLiteral("+");
    const quint8 mag = static_cast<quint8>(qAbs(static_cast<int>(disp)));
    return QStringLiteral("(") + index + sign + hex8(mag) + QStringLiteral(")");
}

inline QString z80Reg8Name(int reg, Z80IndexMode indexMode = Z80IndexMode::None,
                           bool useIndexedMemory = false, qint8 disp = 0)
{
    if (indexMode != Z80IndexMode::None) {
        switch (reg & 0x7) {
        case 4: return (indexMode == Z80IndexMode::IX) ? QStringLiteral("IXH") : QStringLiteral("IYH");
        case 5: return (indexMode == Z80IndexMode::IX) ? QStringLiteral("IXL") : QStringLiteral("IYL");
        case 6:
            if (useIndexedMemory)
                return z80IndexedMemoryOperand(indexMode, disp);
            break;
        default:
            break;
        }
    }
    return z80BaseReg8Name(reg);
}

inline Z80Decoded decodeZ80Instruction(const uint8_t *code, size_t size, quint64 cpuAddr);

inline qint64 z80CpuToFileOffset(qint64 cpuTarget, qint64 baseAddress)
{
    return cpuTarget - baseAddress;
}

static QVector<qint64> z80SeedFileOffsets(const QByteArray &data, RomType romType,
                                          bool seedPlatformEntries, bool seedRangeStart,
                                          qint64 rangeBegin)
{
    QVector<qint64> seeds;
    auto addSeed = [&](qint64 seed) {
        if (seed < 0 || seed >= data.size() || seeds.contains(seed))
            return;
        seeds.append(seed);
    };

    if (seedPlatformEntries) {
        switch (romType) {
        case RomType::GB:
        case RomType::GBC:
            if (data.size() > 0x100)
                addSeed(0x100);
            break;
        case RomType::SMS:
        case RomType::GG:
        case RomType::SG1000:
        case RomType::ColecoVision:
            addSeed(0);
            break;
        default:
            break;
        }
    }

    if (seedRangeStart || seeds.isEmpty())
        addSeed(rangeBegin);

    return seeds;
}

static QMap<qint64, int> z80ReachableInstructions(const QByteArray &data, qint64 offset,
                                                  int available, qint64 baseAddress,
                                                  RomType romType, bool seedPlatformEntries,
                                                  bool seedRangeStart)
{
    QMap<qint64, int> reachable;
    if (available <= 0 || offset < 0 || offset >= data.size())
        return reachable;

    const qint64 rangeBegin = offset;
    const qint64 rangeEnd = offset + available;
    const qint64 analysisBegin = seedPlatformEntries ? 0 : rangeBegin;
    const qint64 analysisEnd = seedPlatformEntries ? data.size() : rangeEnd;
    QVector<qint64> worklist;
    QSet<qint64> queued;
    QSet<qint64> visited;
    const QVector<qint64> seeds = z80SeedFileOffsets(data, romType, seedPlatformEntries,
                                                     seedRangeStart, rangeBegin);
    for (qint64 seed : seeds) {
        worklist.append(seed);
        queued.insert(seed);
    }

    while (!worklist.isEmpty()) {
        qint64 pos = worklist.takeLast();
        while (pos >= analysisBegin && pos < analysisEnd) {
            if (visited.contains(pos))
                break;

            const int rem = static_cast<int>(analysisEnd - pos);
            Z80Decoded z = decodeZ80Instruction(
                reinterpret_cast<const uint8_t *>(data.constData() + pos),
                static_cast<size_t>(rem),
                static_cast<quint64>(pos + baseAddress));
            const int size = qMax(1, qMin(z.size, rem));

            if (z.mnemonic.isEmpty())
                break;

            visited.insert(pos);
            if (pos >= rangeBegin && pos < rangeEnd)
                reachable.insert(pos, qMin(size, static_cast<int>(rangeEnd - pos)));

            if (z.cpuTarget >= 0) {
                const qint64 targetOfs = z80CpuToFileOffset(z.cpuTarget, baseAddress);
                if (targetOfs >= analysisBegin && targetOfs < analysisEnd
                    && !visited.contains(targetOfs) && !queued.contains(targetOfs)) {
                    worklist.append(targetOfs);
                    queued.insert(targetOfs);
                }
            }

            if (!z.hasFallthrough)
                break;

            pos += size;
        }
    }

    return reachable;
}

static int z80DataRunSize(const QMap<qint64, int> &reachable, qint64 pos, qint64 rangeEnd,
                          int maxBytesPerDirective = 8)
{
    auto it = reachable.upperBound(pos);
    const qint64 nextReachable = (it != reachable.end()) ? it.key() : rangeEnd;
    const qint64 rawRun = qMax<qint64>(1, nextReachable - pos);
    return static_cast<int>(qMin<qint64>(rawRun, maxBytesPerDirective));
}

inline int z80BaseLen(quint8 op)
{
    const int x = (op >> 6) & 0x3;
    const int y = (op >> 3) & 0x7;
    const int z = op & 0x7;
    const int p = y >> 1;
    const int q = y & 0x1;

    if (x == 0 && z == 0) {
        if (y >= 2 && y <= 7)
            return 2; // djnz/jr/jr cc,e
        return 1;
    }
    if (x == 0 && z == 1)
        return (q == 0) ? 3 : 1;
    if (x == 0 && z == 2) {
        if (p >= 2)
            return 3;
        return 1;
    }
    if (x == 0 && z == 6)
        return 2;

    if (x == 3 && z == 2)
        return 3; // jp cc,nn
    if (x == 3 && z == 3) {
        if (y == 0)
            return 3; // jp nn
        if (y == 2 || y == 3)
            return 2; // out (n),a / in a,(n)
        return 1;
    }
    if (x == 3 && z == 4)
        return 3; // call cc,nn
    if (x == 3 && z == 5 && q == 1 && p == 0)
        return 3; // call nn
    if (x == 3 && z == 6)
        return 2; // alu n

    return 1;
}

inline bool z80BaseNeedsDispForIndex(quint8 op)
{
    if (op == 0x76)
        return false; // HALT

    const int x = (op >> 6) & 0x3;
    const int y = (op >> 3) & 0x7;
    const int z = op & 0x7;

    if (x == 0 && (z == 4 || z == 5) && y == 6)
        return true; // inc/dec (hl)
    if (x == 0 && z == 6 && y == 6)
        return true; // ld (hl),n
    if (x == 1 && (y == 6 || z == 6))
        return true; // ld r,(hl)/(hl),r
    if (x == 2 && z == 6)
        return true; // alu (hl)

    return false;
}

inline int z80InstructionSize(const uint8_t *code, size_t size)
{
    if (!code || size == 0)
        return 1;

    const quint8 op = code[0];

    if (op == 0xCB)
        return (size >= 2) ? 2 : 1;
    if (op == 0xED)
        return (size >= 2) ? 2 : 1;
    if (op == 0xDD || op == 0xFD) {
        if (size < 2)
            return 1;
        const quint8 op2 = code[1];
        if (op2 == 0xCB)
            return (size >= 4) ? 4 : static_cast<int>(size);
        if (op2 == 0xDD || op2 == 0xED || op2 == 0xFD)
            return 2;
        int len = 1 + z80BaseLen(op2);
        if (z80BaseNeedsDispForIndex(op2))
            ++len;
        return qMin(len, static_cast<int>(size));
    }

    return qMin(z80BaseLen(op), static_cast<int>(size));
}

inline Z80Decoded decodeZ80Instruction(const uint8_t *code, size_t size, quint64 cpuAddr)
{
    Z80Decoded d;
    if (!code || size == 0) {
        return d;
    }

    d.size = z80InstructionSize(code, size);

    const quint8 prefix = code[0];
    Z80IndexMode indexMode = Z80IndexMode::None;
    int opcodePos = 0;

    if (prefix == 0xDD || prefix == 0xFD) {
        indexMode = (prefix == 0xDD) ? Z80IndexMode::IX : Z80IndexMode::IY;
        opcodePos = 1;
        if (size < 2) {
            d.size = 1;
            return d;
        }
        if (code[1] == 0xCB) {
            if (size < 4) {
                d.size = static_cast<int>(size);
                return d;
            }

            const qint8 disp = static_cast<qint8>(code[2]);
            const quint8 cbOp = code[3];
            const int x = (cbOp >> 6) & 0x3;
            const int y = (cbOp >> 3) & 0x7;
            const int z = cbOp & 0x7;
            const QString mem = z80IndexedMemoryOperand(indexMode, disp);

            if (x == 0) {
                d.mnemonic = z80RotateName(y);
                d.operands = (z == 6) ? mem : (mem + QStringLiteral(", ") + z80BaseReg8Name(z));
                return d;
            }
            if (x == 1) {
                d.mnemonic = QStringLiteral("BIT");
                d.operands = QString::number(y) + QStringLiteral(", ") + mem;
                return d;
            }

            d.mnemonic = (x == 2) ? QStringLiteral("RES") : QStringLiteral("SET");
            d.operands = QString::number(y) + QStringLiteral(", ") + mem;
            if (z != 6)
                d.operands += QStringLiteral(", ") + z80BaseReg8Name(z);
            return d;
        }

        if (code[1] == 0xED || code[1] == 0xDD || code[1] == 0xFD) {
            d.size = 1;
            return d;
        }
    }

    const quint8 op = code[opcodePos];
    const bool indexed = (indexMode != Z80IndexMode::None);
    const bool hasIndexedMemory = indexed && z80BaseNeedsDispForIndex(op);
    const qint8 disp = (hasIndexedMemory && (opcodePos + 1) < static_cast<int>(size))
        ? static_cast<qint8>(code[opcodePos + 1])
        : 0;
    const int firstImmPos = opcodePos + 1;
    const int imm8Pos = opcodePos + (hasIndexedMemory ? 2 : 1);

    auto relTarget = [&](int relPos) -> qint64 {
        if (relPos >= static_cast<int>(size))
            return -1;
        const qint8 rel = static_cast<qint8>(code[relPos]);
        return static_cast<qint64>(cpuAddr) + d.size + rel;
    };
    auto imm16 = [&](int pos) -> qint64 {
        if (pos + 1 >= static_cast<int>(size))
            return -1;
        return static_cast<qint64>(static_cast<quint16>(code[pos] | (code[pos + 1] << 8)));
    };

    if (opcodePos == 0 && op == 0xCB) {
        if (size < 2) {
            d.size = 1;
            return d;
        }

        const quint8 cbOp = code[1];
        const int x = (cbOp >> 6) & 0x3;
        const int y = (cbOp >> 3) & 0x7;
        const int z = cbOp & 0x7;
        const QString target = z80BaseReg8Name(z);

        if (x == 0) {
            d.mnemonic = z80RotateName(y);
            d.operands = target;
        } else if (x == 1) {
            d.mnemonic = QStringLiteral("BIT");
            d.operands = QString::number(y) + QStringLiteral(", ") + target;
        } else if (x == 2) {
            d.mnemonic = QStringLiteral("RES");
            d.operands = QString::number(y) + QStringLiteral(", ") + target;
        } else {
            d.mnemonic = QStringLiteral("SET");
            d.operands = QString::number(y) + QStringLiteral(", ") + target;
        }
        return d;
    }

    if (opcodePos == 0 && op == 0xED) {
        if (size < 2) {
            d.size = 1;
            return d;
        }

        const quint8 edOp = code[1];
        const int x = (edOp >> 6) & 0x3;
        const int y = (edOp >> 3) & 0x7;
        const int z = edOp & 0x7;
        const int p = y >> 1;
        const int q = y & 0x1;

        if (x == 1) {
            switch (z) {
            case 0:
                d.mnemonic = QStringLiteral("IN");
                d.operands = (y == 6)
                    ? QStringLiteral("(C)")
                    : (z80BaseReg8Name(y) + QStringLiteral(", (C)"));
                return d;
            case 1:
                d.mnemonic = QStringLiteral("OUT");
                d.operands = (y == 6)
                    ? QStringLiteral("(C), 0")
                    : (QStringLiteral("(C), ") + z80BaseReg8Name(y));
                return d;
            case 2:
                d.mnemonic = q ? QStringLiteral("ADC") : QStringLiteral("SBC");
                d.operands = QStringLiteral("HL, ") + z80PairName(p, Z80IndexMode::None);
                return d;
            case 3:
                d.mnemonic = QStringLiteral("LD");
                if (const qint64 v = imm16(2); v >= 0) {
                    if (q == 0) {
                        d.operands = QStringLiteral("(") + hex16(static_cast<quint16>(v))
                                   + QStringLiteral("), ") + z80PairName(p, Z80IndexMode::None);
                    } else {
                        d.operands = z80PairName(p, Z80IndexMode::None) + QStringLiteral(", (")
                                   + hex16(static_cast<quint16>(v)) + QStringLiteral(")");
                    }
                }
                return d;
            case 4:
                d.mnemonic = QStringLiteral("NEG");
                return d;
            case 5:
                d.mnemonic = (edOp == 0x4D) ? QStringLiteral("RETI") : QStringLiteral("RETN");
                d.isReturn = true;
                d.isBranch = true;
                d.hasFallthrough = false;
                return d;
            case 6: {
                static const int imModes[] = {0, 0, 1, 2, 0, 0, 1, 2};
                d.mnemonic = QStringLiteral("IM");
                d.operands = QString::number(imModes[y]);
                return d;
            }
            case 7:
                switch (y) {
                case 0: d.mnemonic = QStringLiteral("LD"); d.operands = QStringLiteral("I, A"); return d;
                case 1: d.mnemonic = QStringLiteral("LD"); d.operands = QStringLiteral("R, A"); return d;
                case 2: d.mnemonic = QStringLiteral("LD"); d.operands = QStringLiteral("A, I"); return d;
                case 3: d.mnemonic = QStringLiteral("LD"); d.operands = QStringLiteral("A, R"); return d;
                case 4: d.mnemonic = QStringLiteral("RRD"); return d;
                case 5: d.mnemonic = QStringLiteral("RLD"); return d;
                default: return d;
                }
            }
        }

        if (x == 2 && z <= 3 && y >= 4) {
            static const char *blockOps[4][4] = {
                {"LDI", "CPI", "INI", "OUTI"},
                {"LDD", "CPD", "IND", "OUTD"},
                {"LDIR", "CPIR", "INIR", "OTIR"},
                {"LDDR", "CPDR", "INDR", "OTDR"},
            };
            d.mnemonic = QString::fromLatin1(blockOps[y - 4][z]);
            return d;
        }

        return d;
    }

    const int x = (op >> 6) & 0x3;
    const int y = (op >> 3) & 0x7;
    const int z = op & 0x7;
    const int p = y >> 1;
    const int q = y & 0x1;

    if (x == 0) {
        if (z == 0) {
            switch (y) {
            case 0:
                d.mnemonic = QStringLiteral("NOP");
                return d;
            case 1:
                d.mnemonic = QStringLiteral("EX");
                d.operands = QStringLiteral("AF, AF'");
                return d;
            case 2:
                d.mnemonic = QStringLiteral("DJNZ");
                d.isBranch = true;
                d.cpuTarget = relTarget(firstImmPos);
                if (d.cpuTarget >= 0)
                    d.operands = hex16(static_cast<quint16>(d.cpuTarget));
                return d;
            case 3:
                d.mnemonic = QStringLiteral("JR");
                d.isBranch = true;
                d.hasFallthrough = false;
                d.cpuTarget = relTarget(firstImmPos);
                if (d.cpuTarget >= 0)
                    d.operands = hex16(static_cast<quint16>(d.cpuTarget));
                return d;
            default: {
                d.mnemonic = QStringLiteral("JR");
                d.isBranch = true;
                d.cpuTarget = relTarget(firstImmPos);
                const QString cond = z80ConditionName(y - 4);
                d.operands = (d.cpuTarget >= 0)
                    ? (cond + QStringLiteral(", ") + hex16(static_cast<quint16>(d.cpuTarget)))
                    : cond;
                return d;
            }
            }
        }

        if (z == 1) {
            if (q == 0) {
                d.mnemonic = QStringLiteral("LD");
                d.operands = z80PairName(p, indexMode);
                if (const qint64 v = imm16(firstImmPos); v >= 0)
                    d.operands += QStringLiteral(", ") + hex16(static_cast<quint16>(v));
            } else {
                d.mnemonic = QStringLiteral("ADD");
                d.operands = z80IndexName(indexMode) + QStringLiteral(", ") + z80PairName(p, indexMode);
            }
            return d;
        }

        if (z == 2) {
            if (q == 0) {
                d.mnemonic = QStringLiteral("LD");
                switch (p) {
                case 0: d.operands = QStringLiteral("(BC), A"); return d;
                case 1: d.operands = QStringLiteral("(DE), A"); return d;
                case 2:
                    if (const qint64 v = imm16(firstImmPos); v >= 0)
                        d.operands = QStringLiteral("(") + hex16(static_cast<quint16>(v)) + QStringLiteral("), ")
                                   + z80PairName(2, indexMode);
                    return d;
                case 3:
                    if (const qint64 v = imm16(firstImmPos); v >= 0)
                        d.operands = QStringLiteral("(") + hex16(static_cast<quint16>(v)) + QStringLiteral("), A");
                    return d;
                }
            } else {
                d.mnemonic = QStringLiteral("LD");
                switch (p) {
                case 0: d.operands = QStringLiteral("A, (BC)"); return d;
                case 1: d.operands = QStringLiteral("A, (DE)"); return d;
                case 2:
                    if (const qint64 v = imm16(firstImmPos); v >= 0)
                        d.operands = z80PairName(2, indexMode) + QStringLiteral(", (")
                                   + hex16(static_cast<quint16>(v)) + QStringLiteral(")");
                    return d;
                case 3:
                    if (const qint64 v = imm16(firstImmPos); v >= 0)
                        d.operands = QStringLiteral("A, (") + hex16(static_cast<quint16>(v)) + QStringLiteral(")");
                    return d;
                }
            }
        }

        if (z == 3) {
            d.mnemonic = (q == 0) ? QStringLiteral("INC") : QStringLiteral("DEC");
            d.operands = z80PairName(p, indexMode);
            return d;
        }

        if (z == 4) {
            d.mnemonic = QStringLiteral("INC");
            d.operands = z80Reg8Name(y, indexMode, indexed && y == 6, disp);
            return d;
        }

        if (z == 5) {
            d.mnemonic = QStringLiteral("DEC");
            d.operands = z80Reg8Name(y, indexMode, indexed && y == 6, disp);
            return d;
        }

        if (z == 6) {
            d.mnemonic = QStringLiteral("LD");
            d.operands = z80Reg8Name(y, indexMode, indexed && y == 6, disp);
            if (imm8Pos < static_cast<int>(size))
                d.operands += QStringLiteral(", ") + hex8(code[imm8Pos]);
            return d;
        }

        if (z == 7) {
            d.mnemonic = z80MiscName(y);
            return d;
        }
    }

    if (x == 1) {
        if (op == 0x76) {
            d.mnemonic = QStringLiteral("HALT");
            return d;
        }
        d.mnemonic = QStringLiteral("LD");
        d.operands = z80Reg8Name(y, indexMode, indexed && y == 6, disp)
                   + QStringLiteral(", ")
                   + z80Reg8Name(z, indexMode, indexed && z == 6, disp);
        return d;
    }

    if (x == 2) {
        d.mnemonic = z80AluName(y);
        d.operands = z80Reg8Name(z, indexMode, indexed && z == 6, disp);
        return d;
    }

    if (x == 3) {
        if (z == 0) {
            d.mnemonic = QStringLiteral("RET");
            d.isBranch = true;
            d.isReturn = true;
            d.operands = z80ConditionName(y);
            return d;
        }

        if (z == 1) {
            if (q == 0) {
                d.mnemonic = QStringLiteral("POP");
                d.operands = z80PushPairName(p, indexMode);
            } else {
                switch (p) {
                case 0:
                    d.mnemonic = QStringLiteral("RET");
                    d.isBranch = true;
                    d.isReturn = true;
                    d.hasFallthrough = false;
                    break;
                case 1:
                    d.mnemonic = QStringLiteral("EXX");
                    break;
                case 2:
                    d.mnemonic = QStringLiteral("JP");
                    d.operands = QStringLiteral("(") + z80IndexName(indexMode) + QStringLiteral(")");
                    d.isBranch = true;
                    d.hasFallthrough = false;
                    break;
                case 3:
                    d.mnemonic = QStringLiteral("LD");
                    d.operands = QStringLiteral("SP, ") + z80IndexName(indexMode);
                    break;
                }
            }
            return d;
        }

        if (z == 2) {
            d.mnemonic = QStringLiteral("JP");
            d.isBranch = true;
            d.cpuTarget = imm16(firstImmPos);
            const QString cond = z80ConditionName(y);
            d.operands = (d.cpuTarget >= 0)
                ? (cond + QStringLiteral(", ") + hex16(static_cast<quint16>(d.cpuTarget)))
                : cond;
            return d;
        }

        if (z == 3) {
            switch (y) {
            case 0:
                d.mnemonic = QStringLiteral("JP");
                d.isBranch = true;
                d.hasFallthrough = false;
                d.cpuTarget = imm16(firstImmPos);
                if (d.cpuTarget >= 0)
                    d.operands = hex16(static_cast<quint16>(d.cpuTarget));
                return d;
            case 2:
                d.mnemonic = QStringLiteral("OUT");
                if (firstImmPos < static_cast<int>(size))
                    d.operands = QStringLiteral("(") + hex8(code[firstImmPos]) + QStringLiteral("), A");
                return d;
            case 3:
                d.mnemonic = QStringLiteral("IN");
                if (firstImmPos < static_cast<int>(size))
                    d.operands = QStringLiteral("A, (") + hex8(code[firstImmPos]) + QStringLiteral(")");
                return d;
            case 4:
                d.mnemonic = QStringLiteral("EX");
                d.operands = QStringLiteral("(SP), ") + z80IndexName(indexMode);
                return d;
            case 5:
                d.mnemonic = QStringLiteral("EX");
                d.operands = QStringLiteral("DE, HL");
                return d;
            case 6:
                d.mnemonic = QStringLiteral("DI");
                return d;
            case 7:
                d.mnemonic = QStringLiteral("EI");
                return d;
            default:
                return d;
            }
        }

        if (z == 4) {
            d.mnemonic = QStringLiteral("CALL");
            d.isBranch = true;
            d.isCall = true;
            d.cpuTarget = imm16(firstImmPos);
            const QString cond = z80ConditionName(y);
            d.operands = (d.cpuTarget >= 0)
                ? (cond + QStringLiteral(", ") + hex16(static_cast<quint16>(d.cpuTarget)))
                : cond;
            return d;
        }

        if (z == 5) {
            if (q == 0) {
                d.mnemonic = QStringLiteral("PUSH");
                d.operands = z80PushPairName(p, indexMode);
                return d;
            }
            if (p == 0) {
                d.mnemonic = QStringLiteral("CALL");
                d.isBranch = true;
                d.isCall = true;
                d.cpuTarget = imm16(firstImmPos);
                if (d.cpuTarget >= 0)
                    d.operands = hex16(static_cast<quint16>(d.cpuTarget));
                return d;
            }
            return d;
        }

        if (z == 6) {
            d.mnemonic = z80AluName(y);
            if (firstImmPos < static_cast<int>(size))
                d.operands = hex8(code[firstImmPos]);
            return d;
        }

        if (z == 7) {
            d.mnemonic = QStringLiteral("RST");
            d.isBranch = true;
            d.isCall = true;
            d.cpuTarget = static_cast<qint64>(op & 0x38);
            d.operands = hex8(static_cast<quint8>(op & 0x38));
            return d;
        }
    }

    return d;
}

} // namespace

Disassembler::Disassembler() = default;

Disassembler::~Disassembler()
{
    close();
}

void Disassembler::close()
{
    if (m_open) {
#ifdef HAVE_CAPSTONE
        if (m_handle != 0) {
            csh h = static_cast<csh>(m_handle);
            cs_close(&h);
        }
#endif
        m_open = false;
        m_handle = 0;
    }
}

bool Disassembler::isSupported(RomType type)
{
    if (isZ80RomType(type))
        return true;

#ifndef HAVE_CAPSTONE
    return false;
#else
    switch (type) {
    // 6502-based
#if defined(HAVE_CAPSTONE_MOS65XX)
    case RomType::NES:
    case RomType::Atari2600:
    case RomType::Atari5200:
    case RomType::Atari7800:
        return capstoneArchAvailable(CS_ARCH_MOS65XX);
#endif

    // ARM (GBA)
    case RomType::GBA:
        return capstoneArchAvailable(CS_ARCH_ARM);

    // M68K (Genesis/MD, 32X)
    case RomType::MD:
    case RomType::X32:
        return capstoneArchAvailable(CS_ARCH_M68K);

    // MIPS (N64)
    case RomType::N64:
    case RomType::N64_LE:
    case RomType::N64_V64:
        return capstoneArchAvailable(CS_ARCH_MIPS);

    // x86-16 (WonderSwan — NEC V30MZ is 80186-compatible)
    case RomType::WonderSwan:
    case RomType::WonderSwanColor:
        return capstoneArchAvailable(CS_ARCH_X86);

    // SH-2 (32X — main CPUs)
    // Capstone has SH support but 32X uses the M68K for main code,
    // SH-2 is supplementary. We support M68K disassembly for 32X.

    // Not yet supported (need separate decoders):
    // 65816: SNES variants
    // 6502 variants when Capstone is built without MOS65XX backend.

    default:
        return false;
    }
#endif
}

bool Disassembler::setRomType(RomType type)
{
    close();
    m_romType = type;

    // Z80 path: handled by internal decoder (Capstone in this tree has no Z80 backend).
    if (isZ80RomType(type)) {
        m_open = true;
        m_handle = 0;
        m_arch = 0;
        m_baseAddress = -defaultPointerOffset(type);
        return true;
    }

#ifndef HAVE_CAPSTONE
    return false;
#else
    cs_arch arch;
    cs_mode mode;

    switch (type) {
    // 6502-based platforms
    case RomType::NES:
    case RomType::Atari2600:
    case RomType::Atari5200:
    case RomType::Atari7800:
#if defined(HAVE_CAPSTONE_MOS65XX)
        arch = CS_ARCH_MOS65XX;
        mode = CS_MODE_MOS65XX_6502;
        break;
#else
        return false;
#endif

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

    if (!capstoneArchAvailable(arch))
        return false;

    csh h = 0;
    cs_err err = cs_open(arch, mode, &h);
    if (err != CS_ERR_OK) {
        qWarning() << "Capstone initialization failed"
                   << "romType=" << static_cast<int>(type)
                   << "arch=" << static_cast<int>(arch)
                   << "mode=0x" << QString::number(static_cast<qulonglong>(mode), 16)
                   << "error=" << cs_strerror(err);
        return false;
    }

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
#endif
}

qint64 Disassembler::resolveTarget(quint64 cpuTarget) const
{
    // Convert CPU address back to file offset
    qint64 fileOffset = static_cast<qint64>(cpuTarget) - m_baseAddress;
    return fileOffset;
}

#ifdef HAVE_CAPSTONE
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
#if defined(HAVE_CAPSTONE_MOS65XX)
    case CS_ARCH_MOS65XX:
        if (mn == "jmp" || mn == "jsr" || mn == "bcc" || mn == "bcs" ||
            mn == "beq" || mn == "bne" || mn == "bmi" || mn == "bpl" ||
            mn == "bvc" || mn == "bvs")
            return true;
        break;
#endif

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

struct EmbeddedReference
{
    qint64 targetCpu = -1;
    int ptrOfs = -1;
    int ptrSize = -1;
    bool targetMustBeFunction = true;
};

static bool extractEmbeddedReference(const cs_insn *insn, cs_arch arch, EmbeddedReference *out)
{
    if (!insn || !out)
        return false;

#if CS_API_MAJOR >= 4
    switch (arch) {
    case CS_ARCH_M68K: {
        const QString mn = QString::fromLatin1(insn->mnemonic).toLower();
        const bool isJumpCall = mn.startsWith(QLatin1String("jsr"))
            || mn.startsWith(QLatin1String("jmp"));
        const bool isPea = mn.startsWith(QLatin1String("pea"));
        if (insn->detail) {
            const cs_m68k *m68k = &insn->detail->m68k;
            for (uint8_t i = 0; i < m68k->op_count; ++i) {
                const cs_m68k_op &op = m68k->operands[i];
                if (isJumpCall
                    && op.address_mode == M68K_AM_ABSOLUTE_DATA_LONG) {
                    out->targetCpu = extractBranchTarget(insn, arch);
                    out->ptrOfs = 2;
                    out->ptrSize = 4;
                    out->targetMustBeFunction = true;
                    return out->targetCpu >= 0;
                }
                if (isJumpCall
                    && op.address_mode == M68K_AM_ABSOLUTE_DATA_SHORT) {
                    out->targetCpu = extractBranchTarget(insn, arch);
                    out->ptrOfs = 2;
                    out->ptrSize = 2;
                    out->targetMustBeFunction = true;
                    return out->targetCpu >= 0;
                }
                if (isPea && op.address_mode == M68K_AM_PCI_DISP) {
                    out->targetCpu = static_cast<qint64>(insn->address) + 2 + op.mem.disp;
                    out->ptrOfs = 2;
                    out->ptrSize = 2;
                    out->targetMustBeFunction = false;
                    return true;
                }
            }
        }

        // Capstone's M68K operand detail is inconsistent for some PC-relative PEA
        // forms, but the opcode word 0x487A unambiguously encodes PEA (d16, PC).
        if (isPea && insn->size >= 4
            && insn->bytes[0] == 0x48 && insn->bytes[1] == 0x7A) {
            const qint16 disp = static_cast<qint16>((static_cast<quint16>(insn->bytes[2]) << 8)
                                                    | static_cast<quint16>(insn->bytes[3]));
            out->targetCpu = static_cast<qint64>(insn->address) + 2 + disp;
            out->ptrOfs = 2;
            out->ptrSize = 2;
            out->targetMustBeFunction = false;
            return true;
        }

        break;
    }
#if defined(HAVE_CAPSTONE_MOS65XX)
    case CS_ARCH_MOS65XX: {
        const QString mn = QString::fromLatin1(insn->mnemonic).toLower();
        if (mn == QLatin1String("jsr") && insn->size == 3) {
            out->targetCpu = extractBranchTarget(insn, arch);
            out->ptrOfs = 1;
            out->ptrSize = 2;
            out->targetMustBeFunction = true;
            return out->targetCpu >= 0;
        }
        break;
    }
#endif
    default:
        break;
    }
#else
    Q_UNUSED(insn);
    Q_UNUSED(arch);
    Q_UNUSED(out);
#endif

    return false;
}
#endif

QVector<DisasmInstruction> Disassembler::disassemble(const QByteArray &data, qint64 offset,
                                                      int maxBytes, int maxInstr)
{
    QVector<DisasmInstruction> result;
    if (!m_open || offset < 0 || offset >= data.size())
        return result;

    if (isZ80RomType(m_romType)) {
        const int available = qMin(maxBytes, static_cast<int>(data.size() - offset));
        if (available <= 0)
            return result;

        const bool usePlatformSeeds = (m_romType == RomType::GB || m_romType == RomType::GBC)
            && offset == 0 && available == data.size() && data.size() >= 0x150;
        const uint8_t *code = reinterpret_cast<const uint8_t *>(data.constData() + offset);
        int pos = 0;
        const int limit = (maxInstr > 0) ? maxInstr : INT_MAX;
        const QMap<qint64, int> reachable = usePlatformSeeds
            ? z80ReachableInstructions(data, offset, available, m_baseAddress,
                                       m_romType, true, false)
            : QMap<qint64, int>();
        while (pos < available && result.size() < limit) {
            DisasmInstruction di;
            di.fileOffset = offset + pos;
            di.address = static_cast<quint64>(di.fileOffset + m_baseAddress);

            const auto it = usePlatformSeeds ? reachable.constFind(di.fileOffset) : reachable.constEnd();
            if (usePlatformSeeds && it == reachable.constEnd()) {
                di.size = z80DataRunSize(reachable, di.fileOffset, offset + available);
                di.opcodeSize = di.size;
                di.mnemonic = QStringLiteral("DC.B");
                QStringList values;
                values.reserve(di.size);
                for (int b = 0; b < di.size; ++b)
                    values.append(hex8(code[pos + b]));
                di.operands = values.join(QStringLiteral(","));
                di.isBranch = false;
                di.isCall = false;
                di.isReturn = false;
                di.branchTarget = -1;
            } else {
                const int rem = available - pos;
                Z80Decoded z = decodeZ80Instruction(code + pos, static_cast<size_t>(rem),
                                                    static_cast<quint64>(offset + pos + m_baseAddress));
                z.size = usePlatformSeeds
                    ? qMax(1, qMin(it.value(), rem))
                    : qMax(1, qMin(z.size, rem));
                di.size = z.size;
                di.opcodeSize = z.size;
                if (z.mnemonic.isEmpty()) {
                    di.mnemonic = QStringLiteral("DC.B");
                    QStringList values;
                    values.reserve(di.size);
                    for (int b = 0; b < di.size; ++b)
                        values.append(hex8(code[pos + b]));
                    di.operands = values.join(QStringLiteral(","));
                    di.isBranch = false;
                    di.isCall = false;
                    di.isReturn = false;
                    di.branchTarget = -1;
                } else {
                    di.mnemonic = z.mnemonic;
                    di.operands = z.operands;
                    di.isBranch = z.isBranch;
                    di.isCall = z.isCall;
                    di.isReturn = z.isReturn;
                    di.branchTarget = (z.cpuTarget >= 0)
                        ? resolveTarget(static_cast<quint64>(z.cpuTarget))
                        : -1;
                }
            }

            QString bytesHex;
            for (int b = 0; b < di.size; ++b) {
                if (b > 0)
                    bytesHex += QLatin1Char(' ');
                bytesHex += QString::number(code[pos + b], 16)
                                .toUpper().rightJustified(2, QLatin1Char('0'));
            }
            di.bytes = bytesHex;

            result.append(di);
            pos += di.size;
        }

        return result;
    }

#ifndef HAVE_CAPSTONE
    Q_UNUSED(maxBytes);
    Q_UNUSED(maxInstr);
    return result;
#else
    const int available = qMin(maxBytes, static_cast<int>(data.size() - offset));
    if (available <= 0)
        return result;

    const uint8_t *code = reinterpret_cast<const uint8_t *>(data.constData() + offset);
    size_t codeSize = static_cast<size_t>(available);
    uint64_t addr = static_cast<uint64_t>(offset + m_baseAddress);

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
#endif
}

QVector<InsnBoundary> Disassembler::scanBoundaries(
    const QByteArray &data, qint64 offset, int maxBytes)
{
    QVector<InsnBoundary> result;
    if (!m_open || offset < 0 || offset >= data.size())
        return result;

    if (isZ80RomType(m_romType)) {
        const int available = qMin(maxBytes, static_cast<int>(data.size() - offset));
        if (available <= 0)
            return result;

        const bool usePlatformSeeds = (m_romType == RomType::GB || m_romType == RomType::GBC)
            && offset == 0 && available == data.size() && data.size() >= 0x150;
        int pos = 0;
        result.reserve(available / 2);
        const QMap<qint64, int> reachable = usePlatformSeeds
            ? z80ReachableInstructions(data, offset, available, m_baseAddress,
                                       m_romType, true, false)
            : QMap<qint64, int>();
        while (pos < available) {
            const qint64 fileOffset = offset + pos;
            int sz = 1;
            bool isData = false;
            if (usePlatformSeeds) {
                const auto it = reachable.constFind(fileOffset);
                if (it == reachable.constEnd()) {
                    sz = z80DataRunSize(reachable, fileOffset, offset + available);
                    isData = true;
                } else {
                    sz = qMax(1, qMin(it.value(), available - pos));
                }
            } else {
                const int rem = available - pos;
                Z80Decoded z = decodeZ80Instruction(
                    reinterpret_cast<const uint8_t *>(data.constData() + fileOffset),
                    static_cast<size_t>(rem),
                    static_cast<quint64>(fileOffset + m_baseAddress));
                sz = qMax(1, qMin(z.size, rem));
                isData = z.mnemonic.isEmpty();
            }
            result.append({offset + pos, sz, isData});
            pos += sz;
        }
        return result;
    }

#ifndef HAVE_CAPSTONE
    Q_UNUSED(maxBytes);
    return result;
#else
    const int available = qMin(maxBytes, static_cast<int>(data.size() - offset));
    if (available <= 0)
        return result;

    const uint8_t *code = reinterpret_cast<const uint8_t *>(data.constData() + offset);
    size_t codeSize = static_cast<size_t>(available);
    uint64_t addr = static_cast<uint64_t>(offset + m_baseAddress);

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
#endif
}

QVector<CallPointer> Disassembler::scanEmbeddedPointers(
    const QByteArray &data, qint64 offset, int maxBytes)
{
    QVector<CallPointer> pointers;
    if (!m_open || offset < 0 || offset >= data.size())
        return pointers;

    if (isZ80RomType(m_romType)) {
        const int available = qMin(maxBytes, static_cast<int>(data.size() - offset));
        if (available <= 0)
            return pointers;

        qint64 pos = offset;
        const qint64 end = offset + available;
        while (pos < end) {
            const int rem = static_cast<int>(end - pos);
            Z80Decoded z = decodeZ80Instruction(
                reinterpret_cast<const uint8_t *>(data.constData() + pos),
                static_cast<size_t>(rem),
                static_cast<quint64>(pos + m_baseAddress));
            const int size = qMax(1, qMin(z.size, rem));

            if (!z.mnemonic.isEmpty() && z.isCall && z.cpuTarget >= 0 && size >= 3) {
                const qint64 targetFile = resolveTarget(static_cast<quint64>(z.cpuTarget));
                if (targetFile >= 0 && targetFile < data.size())
                    pointers.append({pos + size - 2, targetFile, 2, true});
            }

            pos += size;
        }

        return pointers;
    }

#ifndef HAVE_CAPSTONE
    Q_UNUSED(maxBytes);
    return pointers;
#else
    const int available = qMin(maxBytes, static_cast<int>(data.size() - offset));
    if (available <= 0)
        return pointers;

    const uint8_t *code = reinterpret_cast<const uint8_t *>(data.constData() + offset);
    size_t codeSize = static_cast<size_t>(available);
    uint64_t addr = static_cast<uint64_t>(offset + m_baseAddress);

    csh h = static_cast<csh>(m_handle);
    cs_insn *insn = cs_malloc(h);
    if (!insn)
        return pointers;

    while (codeSize > 0) {
        if (!cs_disasm_iter(h, &code, &codeSize, &addr, insn))
            break;

        const cs_arch arch = static_cast<cs_arch>(m_arch);
        EmbeddedReference embeddedRef;
        if (!extractEmbeddedReference(insn, arch, &embeddedRef)
            || embeddedRef.targetCpu < 0
            || embeddedRef.ptrOfs < 0
            || embeddedRef.ptrSize <= 0) {
            continue;
        }

        const qint64 targetFileOfs = resolveTarget(static_cast<quint64>(embeddedRef.targetCpu));
        if (targetFileOfs < 0 || targetFileOfs >= data.size())
            continue;

        const qint64 instrFileOfs = static_cast<qint64>(insn->address) - m_baseAddress;
        pointers.append({instrFileOfs + embeddedRef.ptrOfs,
                         targetFileOfs,
                         embeddedRef.ptrSize,
                         embeddedRef.targetMustBeFunction});
    }

    cs_free(insn, 1);
    return pointers;
#endif
}

QVector<DetectedFunction> Disassembler::scanFunctions(
    const QByteArray &data, qint64 offset, int maxBytes,
    std::function<void(int)> progressCb,
    QVector<CallPointer> *outCallPointers)
{
    QVector<DetectedFunction> funcs;
    if (!m_open || offset < 0 || offset >= data.size())
        return funcs;

    if (isZ80RomType(m_romType)) {
        const int available = qMin(maxBytes, static_cast<int>(data.size() - offset));
        if (available <= 0)
            return funcs;

        const qint64 fileStart = offset;
        const qint64 fileEnd = offset + available;

        QSet<qint64> callTargets;
        QVector<qint64> retEndOffsets;

        const bool usePlatformSeeds = (m_romType == RomType::GB || m_romType == RomType::GBC);
        const QMap<qint64, int> reachable = z80ReachableInstructions(
            data, offset, available, m_baseAddress, m_romType, usePlatformSeeds, !usePlatformSeeds);

        int processed = 0;
        const int totalReachable = qMax(1, reachable.size());
        for (auto it = reachable.constBegin(); it != reachable.constEnd(); ++it) {
            const qint64 fileOffset = it.key();
            const int rem = static_cast<int>(fileEnd - fileOffset);
            Z80Decoded z = decodeZ80Instruction(
                reinterpret_cast<const uint8_t *>(data.constData() + fileOffset),
                static_cast<size_t>(rem),
                static_cast<quint64>(fileOffset + m_baseAddress));
            z.size = qMax(1, qMin(it.value(), rem));

            if (!z.mnemonic.isEmpty() && z.isCall && z.cpuTarget >= 0) {
                const qint64 targetFile = resolveTarget(static_cast<quint64>(z.cpuTarget));
                if (targetFile >= fileStart && targetFile < fileEnd) {
                    callTargets.insert(targetFile);
                    if (outCallPointers && z.size >= 3)
                        outCallPointers->append({fileOffset + z.size - 2, targetFile, 2});
                }
            }
            if (!z.mnemonic.isEmpty() && z.isReturn)
                retEndOffsets.append(fileOffset + z.size);

            ++processed;
            if (progressCb)
                progressCb(processed * 50 / totalReachable);
        }

        if (callTargets.isEmpty())
            return funcs;

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

            auto hi = std::lower_bound(retEndOffsets.begin(), retEndOffsets.end(), nextFuncStart);
            if (hi != retEndOffsets.begin())
                --hi;
            else
                hi = retEndOffsets.end();

            if (hi == retEndOffsets.end() || *hi <= funcStart) {
                auto lo = std::lower_bound(retEndOffsets.begin(), retEndOffsets.end(), funcStart + 1);
                if (lo == retEndOffsets.end())
                    continue;
                hi = lo;
            }

            DetectedFunction df;
            df.startOffset = funcStart;
            df.endOffset = *hi;
            df.cpuAddress = static_cast<quint64>(funcStart + m_baseAddress);
            funcs.append(df);

            if (progressCb)
                progressCb(55 + (i + 1) * 45 / sortedTargets.size());
        }

        return funcs;
    }

#ifndef HAVE_CAPSTONE
    Q_UNUSED(maxBytes);
    Q_UNUSED(progressCb);
    Q_UNUSED(outCallPointers);
    return funcs;
#else
    const int available = qMin(maxBytes, static_cast<int>(data.size() - offset));
    if (available <= 0)
        return funcs;

    const uint8_t *codeBase = reinterpret_cast<const uint8_t *>(data.constData() + offset);
    const uint8_t *code = codeBase;
    size_t codeSize = static_cast<size_t>(available);
    uint64_t addr = static_cast<uint64_t>(offset + m_baseAddress);
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

        const cs_arch arch = static_cast<cs_arch>(m_arch);
        EmbeddedReference embeddedRef;
        const bool hasEmbeddedRef = extractEmbeddedReference(insn, arch, &embeddedRef);

        if (isCallInstruction(h, insn)) {
            qint64 cpuTarget = extractBranchTarget(insn, arch);
            if (cpuTarget >= 0) {
                qint64 targetFileOfs = resolveTarget(static_cast<quint64>(cpuTarget));
                if (targetFileOfs >= fileStart && targetFileOfs < fileEnd) {
                    // For M68K, only calls with embedded absolute addresses are
                    // treated as function entry evidence; local BSR labels are ignored.
                    const bool acceptAsFunctionEntry = (arch == CS_ARCH_M68K)
                        ? hasEmbeddedRef
                        : true;

                    if (acceptAsFunctionEntry)
                        callTargets.insert(targetFileOfs);
                }
            }
        }

        if (outCallPointers && hasEmbeddedRef && embeddedRef.targetCpu >= 0
            && embeddedRef.ptrOfs >= 0 && embeddedRef.ptrSize > 0) {
            const qint64 targetFileOfs = resolveTarget(static_cast<quint64>(embeddedRef.targetCpu));
            if (targetFileOfs >= 0 && targetFileOfs < data.size()) {
                const qint64 instrFileOfs = static_cast<qint64>(insn->address) - m_baseAddress;
                outCallPointers->append({instrFileOfs + embeddedRef.ptrOfs,
                                         targetFileOfs,
                                         embeddedRef.ptrSize,
                                         embeddedRef.targetMustBeFunction});
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

        // Find the LAST RTS-end that is still before the next function.
        // This avoids cutting a function short at an early-return branch
        // while the main body continues further down.
        auto hi = std::lower_bound(retEndOffsets.begin(), retEndOffsets.end(), nextFuncStart);
        // hi points to the first retEnd >= nextFuncStart; step back to last < nextFuncStart
        if (hi != retEndOffsets.begin())
            --hi;
        else
            hi = retEndOffsets.end(); // nothing before nextFuncStart

        if (hi == retEndOffsets.end() || *hi <= funcStart) {
            // Fallback: use the first RTS after funcStart (old behaviour)
            auto lo = std::lower_bound(retEndOffsets.begin(), retEndOffsets.end(), funcStart + 1);
            if (lo == retEndOffsets.end())
                continue;
            hi = lo;
        }

        const qint64 funcEnd = *hi;

        DetectedFunction df;
        df.startOffset = funcStart;
        df.endOffset   = funcEnd;
        df.cpuAddress  = static_cast<quint64>(funcStart + m_baseAddress);
        funcs.append(df);

        if (progressCb)
            progressCb(55 + (i + 1) * 45 / sortedTargets.size());
    }

    return funcs;
#endif
}

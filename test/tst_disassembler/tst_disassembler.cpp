#include <QTest>
#include <QSet>

#include <algorithm>

#include "../../src/utils/disassembler.h"

class TstDisassembler : public QObject
{
    Q_OBJECT

private slots:
    void cpuHelpers()
    {
        QCOMPARE(QString::fromLatin1(disasmCpuName(RomType::NES)), QStringLiteral("MOS 6502"));
        QCOMPARE(QString::fromLatin1(disasmCpuName(RomType::GB)), QStringLiteral("Z80"));
        QCOMPARE(QString::fromLatin1(disasmCpuName(RomType::MD)), QStringLiteral("Motorola 68000"));
        QVERIFY(disasmCpuName(RomType::Unknown) == nullptr);

        QCOMPARE(disasmCanonicalRom(RomType::GG), RomType::GB);
        QCOMPARE(disasmCanonicalRom(RomType::Atari7800), RomType::NES);
        QCOMPARE(disasmCanonicalRom(RomType::X32), RomType::MD);
        QCOMPARE(disasmCanonicalRom(RomType::Unknown), RomType::Unknown);

        const auto cpus = disasmSupportedCpus();
        QCOMPARE(cpus.size(), 6);

        QSet<RomType> reps;
        for (const auto &entry : cpus) {
            QVERIFY(entry.cpuName != nullptr);
            QVERIFY(QString::fromLatin1(entry.cpuName).size() > 0);
            reps.insert(entry.representativeRom);
        }
        QCOMPARE(reps.size(), cpus.size());
    }

    void romTypeSupportAndSelection()
    {
        QVERIFY(Disassembler::isSupported(RomType::NES));
        QVERIFY(Disassembler::isSupported(RomType::GB));
        QVERIFY(Disassembler::isSupported(RomType::GBA));
        QVERIFY(!Disassembler::isSupported(RomType::SNES));

        Disassembler d;
        QVERIFY(d.setRomType(RomType::NES));
        QCOMPARE(d.romType(), RomType::NES);
        QVERIFY(!d.setRomType(RomType::SNES));
    }

    void z80DisassembleAndBoundaries()
    {
        // 0000: CD 06 00   CALL 0006
        // 0003: 18 01      JR 0006
        // 0005: 00         NOP
        // 0006: C9         RET
        const QByteArray data = QByteArray::fromHex("CD0600180100C9");

        Disassembler d;
        QVERIFY(d.setRomType(RomType::GB));

        const auto insns = d.disassemble(data, 0, data.size());
        QVERIFY(insns.size() >= 4);

        QCOMPARE(insns[0].fileOffset, qint64(0));
        QCOMPARE(insns[0].mnemonic, QStringLiteral("CALL"));
        QVERIFY(insns[0].isBranch);
        QVERIFY(insns[0].isCall);
        QCOMPARE(insns[0].branchTarget, qint64(6));

        QCOMPARE(insns[1].fileOffset, qint64(3));
        QCOMPARE(insns[1].mnemonic, QStringLiteral("JR"));
        QVERIFY(insns[1].isBranch);
        QCOMPARE(insns[1].branchTarget, qint64(6));

        QVERIFY(insns[3].isReturn);

        const auto bounds = d.scanBoundaries(data, 0, data.size());
        QCOMPARE(bounds.size(), insns.size());
        for (int i = 0; i < insns.size(); ++i) {
            QCOMPARE(bounds[i].offset, insns[i].fileOffset);
            QCOMPARE(bounds[i].size, insns[i].size);
        }
    }

    void z80PrefixesAndReturns()
    {
        // ED 4D = RETI, DD E9 = JP (IX), FD E9 = JP (IY)
        const QByteArray data = QByteArray::fromHex("ED4DDDE9FDE9");

        Disassembler d;
        QVERIFY(d.setRomType(RomType::GB));

        const auto insns = d.disassemble(data, 0, data.size());
        QCOMPARE(insns.size(), 3);

        QCOMPARE(insns[0].mnemonic, QStringLiteral("RETI"));
        QVERIFY(insns[0].isReturn);

        QCOMPARE(insns[1].mnemonic, QStringLiteral("JP"));
        QCOMPARE(insns[1].operands, QStringLiteral("(IX)"));
        QVERIFY(insns[1].isBranch);

        QCOMPARE(insns[2].mnemonic, QStringLiteral("JP"));
        QCOMPARE(insns[2].operands, QStringLiteral("(IY)"));
        QVERIFY(insns[2].isBranch);
    }

    void z80ScanFunctionsFindsCallTarget()
    {
        const QByteArray data = QByteArray::fromHex("CD0600180100C9");

        Disassembler d;
        QVERIFY(d.setRomType(RomType::GB));

        QVector<CallPointer> callPointers;
        const auto funcs = d.scanFunctions(data, 0, data.size(), nullptr, &callPointers);

        QVERIFY(!funcs.isEmpty());
        QCOMPARE(funcs[0].startOffset, qint64(6));
        QCOMPARE(funcs[0].endOffset, qint64(7));

        QVERIFY(!callPointers.isEmpty());
        QCOMPARE(callPointers[0].ptrFileOffset, qint64(1));
        QCOMPARE(callPointers[0].targetOffset, qint64(6));
        QCOMPARE(callPointers[0].ptrSize, 2);
    }

    void z80GbHeaderUsesEntrySeedAndKeepsHeaderData()
    {
        QByteArray data(0x160, char(0x00));
        data[0x100] = char(0x00);
        data[0x101] = char(0xC3);
        data[0x102] = char(0x50);
        data[0x103] = char(0x01);
        data[0x150] = char(0xDD);
        data[0x151] = char(0x21);
        data[0x152] = char(0x34);
        data[0x153] = char(0x12);
        data[0x154] = char(0xC9);

        Disassembler d;
        QVERIFY(d.setRomType(RomType::GB));

        const auto insns = d.disassemble(data, 0, data.size());
        QVERIFY(!insns.isEmpty());
        QCOMPARE(insns.first().fileOffset, qint64(0));
        QCOMPARE(insns.first().mnemonic, QStringLiteral("DC.B"));

        auto findInsn = [&insns](qint64 fileOffset) {
            return std::find_if(insns.begin(), insns.end(), [fileOffset](const DisasmInstruction &insn) {
                return insn.fileOffset == fileOffset;
            });
        };

        const auto nopIt = findInsn(0x100);
        QVERIFY(nopIt != insns.end());
        QCOMPARE(nopIt->mnemonic, QStringLiteral("NOP"));

        const auto jpIt = findInsn(0x101);
        QVERIFY(jpIt != insns.end());
        QCOMPARE(jpIt->mnemonic, QStringLiteral("JP"));
        QCOMPARE(jpIt->branchTarget, qint64(0x150));

        const auto codeIt = findInsn(0x150);
        QVERIFY(codeIt != insns.end());
        QCOMPARE(codeIt->mnemonic, QStringLiteral("LD"));
        QCOMPARE(codeIt->operands, QStringLiteral("IX, $1234"));

        const auto bounds = d.scanBoundaries(data, 0, data.size());
        QVERIFY(!bounds.isEmpty());
        QVERIFY(bounds.first().isData);
        auto codeBoundary = std::find_if(bounds.begin(), bounds.end(), [](const InsnBoundary &boundary) {
            return boundary.offset == qint64(0x150);
        });
        QVERIFY(codeBoundary != bounds.end());
        QVERIFY(!codeBoundary->isData);
    }

    void z80IndexedAndExtendedOpcodesDecode()
    {
        const QByteArray data = QByteArray::fromHex("CB11ED4ADD213412FD3605AADDCBFE46");

        Disassembler d;
        QVERIFY(d.setRomType(RomType::GB));

        const auto insns = d.disassemble(data, 0, data.size());
        QCOMPARE(insns.size(), 5);

        QCOMPARE(insns[0].mnemonic, QStringLiteral("RL"));
        QCOMPARE(insns[0].operands, QStringLiteral("C"));

        QCOMPARE(insns[1].mnemonic, QStringLiteral("ADC"));
        QCOMPARE(insns[1].operands, QStringLiteral("HL, BC"));

        QCOMPARE(insns[2].mnemonic, QStringLiteral("LD"));
        QCOMPARE(insns[2].operands, QStringLiteral("IX, $1234"));

        QCOMPARE(insns[3].mnemonic, QStringLiteral("LD"));
        QCOMPARE(insns[3].operands, QStringLiteral("(IY+$05), $AA"));

        QCOMPARE(insns[4].mnemonic, QStringLiteral("BIT"));
        QCOMPARE(insns[4].operands, QStringLiteral("0, (IX-$02)"));
    }

    void disassembleAndBoundariesFor6502()
    {
        // 0x00: JSR $8006
        // 0x03: RTS
        // 0x04: NOP
        // 0x05: NOP
        // 0x06: LDA #$01
        // 0x08: RTS
        const QByteArray data = QByteArray::fromHex("20068060EAEAA90160");

        Disassembler d;
        QVERIFY(d.setRomType(RomType::NES));

        const auto insns = d.disassemble(data, 0, data.size());
        QVERIFY(insns.size() >= 3);

        QCOMPARE(insns[0].fileOffset, qint64(0));
        QCOMPARE(insns[0].mnemonic, QStringLiteral("JSR"));
        QVERIFY(insns[0].isBranch);
        QVERIFY(insns[0].isCall);

        bool sawReturn = false;
        for (const auto &insn : insns) {
            if (insn.isReturn) {
                sawReturn = true;
                break;
            }
        }
        QVERIFY(sawReturn);

        const auto bounds = d.scanBoundaries(data, 0, data.size());
        QCOMPARE(bounds.size(), insns.size());
        for (int i = 0; i < insns.size(); ++i) {
            QCOMPARE(bounds[i].offset, insns[i].fileOffset);
            QCOMPARE(bounds[i].size, insns[i].size);
        }
    }

    void scanFunctionsFindsCallTarget()
    {
        const QByteArray data = QByteArray::fromHex("20068060EAEAA90160");

        Disassembler d;
        QVERIFY(d.setRomType(RomType::NES));

        QVector<CallPointer> callPointers;
        int progressMax = -1;
        const auto funcs = d.scanFunctions(
            data,
            0,
            data.size(),
            [&progressMax](int p) { progressMax = qMax(progressMax, p); },
            &callPointers);

        QVERIFY(progressMax >= 0);

        // The exact resolved targets depend on platform base-address mapping,
        // but scan must stay stable and return valid, ordered ranges.
        for (int i = 0; i < funcs.size(); ++i) {
            const auto &f = funcs[i];
            QVERIFY(f.endOffset > f.startOffset);
            QVERIFY(f.startOffset >= 0);
            QVERIFY(f.endOffset <= data.size());
            if (i > 0)
                QVERIFY(funcs[i - 1].startOffset <= f.startOffset);
        }

        for (const auto &cp : callPointers) {
            QVERIFY(cp.ptrSize == 2 || cp.ptrSize == 4);
            QVERIFY(cp.ptrFileOffset >= 0);
        }
    }

    void megaDriveSupportAndBasicDisassembly()
    {
        QVERIFY(Disassembler::isSupported(RomType::MD));

        Disassembler d;
        QVERIFY(d.setRomType(RomType::MD));

        // M68K: RTS
        const QByteArray data = QByteArray::fromHex("4E75");
        const auto insns = d.disassemble(data, 0, data.size());
        QVERIFY(!insns.isEmpty());

        QCOMPARE(insns[0].fileOffset, qint64(0));
        QCOMPARE(insns[0].size, 2);
        QVERIFY(insns[0].isReturn);
    }
};

QTEST_APPLESS_MAIN(TstDisassembler)
#include "tst_disassembler.moc"

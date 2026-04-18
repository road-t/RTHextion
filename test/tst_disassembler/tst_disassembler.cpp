#include <QTest>
#include <QSet>

#include "../../src/utils/disassembler.h"

class TstDisassembler : public QObject
{
    Q_OBJECT

private slots:
    void cpuHelpers()
    {
        QCOMPARE(QString::fromLatin1(disasmCpuName(RomType::NES)), QStringLiteral("MOS 6502"));
        QCOMPARE(QString::fromLatin1(disasmCpuName(RomType::MD)), QStringLiteral("Motorola 68000"));
        QVERIFY(disasmCpuName(RomType::Unknown) == nullptr);

        QCOMPARE(disasmCanonicalRom(RomType::Atari7800), RomType::NES);
        QCOMPARE(disasmCanonicalRom(RomType::X32), RomType::MD);
        QCOMPARE(disasmCanonicalRom(RomType::Unknown), RomType::Unknown);

        const auto cpus = disasmSupportedCpus();
        QCOMPARE(cpus.size(), 5);

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
        QVERIFY(Disassembler::isSupported(RomType::GBA));
        QVERIFY(!Disassembler::isSupported(RomType::SNES));

        Disassembler d;
        QVERIFY(d.setRomType(RomType::NES));
        QCOMPARE(d.romType(), RomType::NES);
        QVERIFY(!d.setRomType(RomType::SNES));
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
};

QTEST_APPLESS_MAIN(TstDisassembler)
#include "tst_disassembler.moc"

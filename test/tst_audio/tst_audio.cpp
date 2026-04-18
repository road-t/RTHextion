#include <QTest>

#include "../../src/audio/audiodetector.h"

class TstAudio : public QObject
{
    Q_OBJECT

private slots:
    void decodeUnsigned8MapsToPcm16()
    {
        QByteArray raw;
        raw.append(char(0x00));
        raw.append(char(0x80));
        raw.append(char(0xFF));

        const auto pcm = AudioDetector::decodeUnsigned8(raw);
        QCOMPARE(pcm.size(), 3);
        QCOMPARE(pcm[0], qint16(-32768));
        QCOMPARE(pcm[1], qint16(0));
        QCOMPARE(pcm[2], qint16(32512));
    }

    void decodeSigned8MapsToPcm16()
    {
        QByteArray raw;
        raw.append(char(0x80));
        raw.append(char(0x00));
        raw.append(char(0x7F));

        const auto pcm = AudioDetector::decodeSigned8(raw);
        QCOMPARE(pcm.size(), 3);
        QCOMPARE(pcm[0], qint16(-32768));
        QCOMPARE(pcm[1], qint16(0));
        QCOMPARE(pcm[2], qint16(32512));
    }

    void decodeDpcmExpandsBitstream()
    {
        const QByteArray dpcm(1, char(0x00));
        const auto pcm = AudioDetector::decodeDPCM(dpcm);

        QCOMPARE(pcm.size(), 8);
        for (int i = 1; i < pcm.size(); ++i)
            QVERIFY(pcm[i] < pcm[i - 1]);
    }

    void detectBrrFindsSimpleSample()
    {
        AudioDetector detector;
        detector.setMinSampleBytes(18);
        detector.setMinConfidence(0.3f);

        QByteArray brr;
        // Block 1 header: range=0, filter=0, no loop/end.
        brr.append(char(0x00));
        brr.append(QByteArray(8, char(0x11)));
        // Block 2 header: END flag set.
        brr.append(char(0x01));
        brr.append(QByteArray(8, char(0x22)));

        const auto samples = detector.detectBRR(brr);
        QCOMPARE(samples.size(), 1);
        QCOMPARE(samples[0].offset, qint64(0));
        QCOMPARE(samples[0].length, qint64(18));
        QCOMPARE(samples[0].format, AudioSampleFormat::SNES_BRR);
        QVERIFY(!samples[0].hasLoop);
        QVERIFY(samples[0].confidence >= 0.3f);
    }

    void decodeDispatcherSetsExpectedRate()
    {
        int sampleRate = 0;
        const QByteArray raw = QByteArray::fromHex("0080FF");
        const auto pcm = AudioDetector::decodeToPCM16(
            raw,
            AudioSampleFormat::Raw_PCM8_Unsigned,
            &sampleRate);

        QCOMPARE(sampleRate, 8000);
        QCOMPARE(pcm.size(), raw.size());
        QCOMPARE(pcm[1], qint16(0));
    }

    void detectDispatcherUsesRomType()
    {
        AudioDetector detector;
        detector.setMinSampleBytes(18);
        detector.setMinConfidence(0.3f);

        QByteArray brr;
        brr.append(char(0x00));
        brr.append(QByteArray(8, char(0x11)));
        brr.append(char(0x01));
        brr.append(QByteArray(8, char(0x22)));

        const auto samples = detector.detect(brr, RomType::SNES);
        QCOMPARE(samples.size(), 1);
        QCOMPARE(samples[0].format, AudioSampleFormat::SNES_BRR);
    }
};

QTEST_APPLESS_MAIN(TstAudio)
#include "tst_audio.moc"

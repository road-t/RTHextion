#include "audioplayer.h"

#ifdef HAVE_QT_MULTIMEDIA
#include <QAudioSink>
#include <QMediaDevices>
#include <QAudioDevice>
#endif

#include <QFile>
#include <QDataStream>
#include <QtCore/QCoreApplication>
#include <QtEndian>
#include <cstring>

void AudioSinkDeleter::operator()(QAudioSink *sink) const
{
#ifdef HAVE_QT_MULTIMEDIA
    if (!sink)
        return;
    if (QCoreApplication::instance())
        sink->deleteLater();
    else
        delete sink;
#else
    Q_UNUSED(sink);
#endif
}

AudioPlayer::AudioPlayer(QObject *parent)
    : QObject(parent)
{
}

AudioPlayer::~AudioPlayer()
{
    stop();
}

// ─── Load / Build ──────────────────────────────────────────────────

void AudioPlayer::loadSample(const QVector<int16_t> &pcm16, int sampleRate)
{
    stop();
    m_sampleRate = sampleRate;
    m_playbackSampleRate = sampleRate;
    m_sampleCount = pcm16.size();
    buildWavData(pcm16, sampleRate);
}

void AudioPlayer::loadFromRaw(const QByteArray &rawData, AudioSampleFormat format)
{
    int rate = 0;
    const auto pcm = AudioDetector::decodeToPCM16(rawData, format, &rate);
    if (pcm.isEmpty())
        return;
    loadSample(pcm, rate);
}

void AudioPlayer::buildWavData(const QVector<int16_t> &pcm16, int sampleRate)
{
    m_wavData = createWavData(pcm16, sampleRate);

    // Build raw PCM payload for QAudioSink (no WAV header)
    m_pcmPayload.resize(pcm16.size() * 2);
    for (int i = 0; i < pcm16.size(); ++i)
        qToLittleEndian<int16_t>(pcm16[i], reinterpret_cast<uchar *>(m_pcmPayload.data()) + i * 2);
}

// ─── Playback ──────────────────────────────────────────────────────

void AudioPlayer::play()
{
    play(0, 1.0);
}

void AudioPlayer::play(int sampleRateOverride, double speedMultiplier)
{
#ifndef HAVE_QT_MULTIMEDIA
    Q_UNUSED(sampleRateOverride);
    Q_UNUSED(speedMultiplier);
    return;
#else
    stop();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    if (m_pcmPayload.isEmpty() || m_sampleRate <= 0)
        return;

    const int baseRate = (sampleRateOverride > 0) ? sampleRateOverride : m_sampleRate;
    const int effectiveRate = qBound(1000, static_cast<int>(baseRate * speedMultiplier), 192000);
    m_playbackSampleRate = effectiveRate;

    QAudioFormat fmt;
    fmt.setSampleRate(effectiveRate);
    fmt.setChannelCount(1);
    fmt.setSampleFormat(QAudioFormat::Int16);

    const QAudioDevice defaultDev = QMediaDevices::defaultAudioOutput();
    if (!defaultDev.isFormatSupported(fmt))
        return;

    m_audioSink.reset(new QAudioSink(defaultDev, fmt, this));
    QAudioSink *sink = m_audioSink.get();
    connect(sink, &QAudioSink::stateChanged, this, [this, sink](QAudio::State state) {
        if (sink != m_audioSink.get())
            return;
        if (state == QAudio::IdleState || state == QAudio::StoppedState) {
            emit playbackStopped();
        }
    });

    m_audioBuffer.setBuffer(&m_pcmPayload);
    m_audioBuffer.open(QIODevice::ReadOnly);
    sink->start(&m_audioBuffer);
    emit playbackStarted();
#endif
}

void AudioPlayer::stop()
{
#ifdef HAVE_QT_MULTIMEDIA
    if (m_audioSink) {
        disconnect(m_audioSink.get(), nullptr, this, nullptr);
        m_audioSink->stop();
        m_audioSink.reset();
    }
    if (m_audioBuffer.isOpen())
        m_audioBuffer.close();
#endif
}

bool AudioPlayer::isPlaying() const
{
#ifdef HAVE_QT_MULTIMEDIA
    return m_audioSink && m_audioSink->state() == QAudio::ActiveState;
#else
    return false;
#endif
}

int AudioPlayer::durationMs() const
{
    if (m_sampleRate <= 0 || m_sampleCount <= 0)
        return 0;
    return static_cast<int>((static_cast<qint64>(m_sampleCount) * 1000) / m_sampleRate);
}

int AudioPlayer::playbackDurationMs() const
{
    if (m_playbackSampleRate <= 0 || m_sampleCount <= 0)
        return 0;
    return static_cast<int>((static_cast<qint64>(m_sampleCount) * 1000) / m_playbackSampleRate);
}

int AudioPlayer::playbackPositionMs() const
{
#ifdef HAVE_QT_MULTIMEDIA
    if (!m_audioSink)
        return 0;
    return static_cast<int>(m_audioSink->processedUSecs() / 1000);
#else
    return 0;
#endif
}

// ─── WAV Export ────────────────────────────────────────────────────

QByteArray AudioPlayer::createWavData(const QVector<int16_t> &pcm16, int sampleRate)
{
    const int numSamples = pcm16.size();
    const int bitsPerSample = 16;
    const int numChannels = 1;
    const int byteRate = sampleRate * numChannels * (bitsPerSample / 8);
    const int blockAlign = numChannels * (bitsPerSample / 8);
    const int dataSize = numSamples * blockAlign;

    QByteArray wav;
    wav.reserve(44 + dataSize);

    // RIFF header
    wav.append("RIFF", 4);
    uint32_t chunkSize = qToLittleEndian<uint32_t>(36 + dataSize);
    wav.append(reinterpret_cast<const char *>(&chunkSize), 4);
    wav.append("WAVE", 4);

    // fmt subchunk
    wav.append("fmt ", 4);
    uint32_t subchunk1Size = qToLittleEndian<uint32_t>(16);
    wav.append(reinterpret_cast<const char *>(&subchunk1Size), 4);
    uint16_t audioFormat = qToLittleEndian<uint16_t>(1);  // PCM
    wav.append(reinterpret_cast<const char *>(&audioFormat), 2);
    uint16_t channels = qToLittleEndian<uint16_t>(numChannels);
    wav.append(reinterpret_cast<const char *>(&channels), 2);
    uint32_t rate = qToLittleEndian<uint32_t>(sampleRate);
    wav.append(reinterpret_cast<const char *>(&rate), 4);
    uint32_t br = qToLittleEndian<uint32_t>(byteRate);
    wav.append(reinterpret_cast<const char *>(&br), 4);
    uint16_t ba = qToLittleEndian<uint16_t>(blockAlign);
    wav.append(reinterpret_cast<const char *>(&ba), 2);
    uint16_t bps = qToLittleEndian<uint16_t>(bitsPerSample);
    wav.append(reinterpret_cast<const char *>(&bps), 2);

    // data subchunk
    wav.append("data", 4);
    uint32_t ds = qToLittleEndian<uint32_t>(dataSize);
    wav.append(reinterpret_cast<const char *>(&ds), 4);

    // PCM data
    for (int i = 0; i < numSamples; ++i) {
        int16_t s = qToLittleEndian<int16_t>(pcm16[i]);
        wav.append(reinterpret_cast<const char *>(&s), 2);
    }

    return wav;
}

bool AudioPlayer::exportWav(const QString &filePath) const
{
    if (m_wavData.isEmpty())
        return false;

    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(m_wavData);
    return true;
}

// ─── WAV Import ────────────────────────────────────────────────────

QByteArray AudioPlayer::importWav(const QString &filePath,
                                   AudioSampleFormat targetFormat,
                                   int targetSampleRate,
                                   int *outSampleCount)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return {};

    const QByteArray wav = f.readAll();
    if (wav.size() < 44)
        return {};

    // Parse WAV header
    if (wav.left(4) != "RIFF" || wav.mid(8, 4) != "WAVE")
        return {};

    // Find "fmt " chunk
    int fmtPos = -1;
    for (int i = 12; i + 8 <= wav.size(); ) {
        const int chunkSize = qFromLittleEndian<int32_t>(wav.constData() + i + 4);
        if (wav.mid(i, 4) == "fmt ") {
            fmtPos = i + 8;
            break;
        }
        i += 8 + chunkSize;
    }
    if (fmtPos < 0)
        return {};

    const uint16_t audioFmt = qFromLittleEndian<uint16_t>(wav.constData() + fmtPos);
    const uint16_t channels = qFromLittleEndian<uint16_t>(wav.constData() + fmtPos + 2);
    const uint32_t srcRate = qFromLittleEndian<uint32_t>(wav.constData() + fmtPos + 4);
    const uint16_t bitsPerSample = qFromLittleEndian<uint16_t>(wav.constData() + fmtPos + 14);

    if (audioFmt != 1)  // only PCM supported
        return {};

    // Find "data" chunk
    int dataPos = -1;
    int dataSize = 0;
    for (int i = 12; i + 8 <= wav.size(); ) {
        const int chunkSize = qFromLittleEndian<int32_t>(wav.constData() + i + 4);
        if (wav.mid(i, 4) == "data") {
            dataPos = i + 8;
            dataSize = chunkSize;
            break;
        }
        i += 8 + chunkSize;
    }
    if (dataPos < 0 || dataPos + dataSize > wav.size())
        return {};

    // Decode to mono 16-bit PCM
    QVector<int16_t> pcm;
    const int bytesPerSample = bitsPerSample / 8;
    const int frameSize = channels * bytesPerSample;
    const int numFrames = dataSize / frameSize;
    pcm.reserve(numFrames);

    for (int i = 0; i < numFrames; ++i) {
        const int pos = dataPos + i * frameSize;
        int32_t mix = 0;
        for (int ch = 0; ch < channels; ++ch) {
            const int chPos = pos + ch * bytesPerSample;
            if (bitsPerSample == 16) {
                mix += qFromLittleEndian<int16_t>(wav.constData() + chPos);
            } else if (bitsPerSample == 8) {
                mix += (static_cast<int>(static_cast<uint8_t>(wav[chPos])) - 128) * 256;
            }
        }
        pcm.append(static_cast<int16_t>(qBound(-32768, mix / channels, 32767)));
    }

    // Simple linear resampling if rates differ
    if (static_cast<int>(srcRate) != targetSampleRate && targetSampleRate > 0) {
        const double ratio = static_cast<double>(targetSampleRate) / srcRate;
        const int newLen = static_cast<int>(pcm.size() * ratio);
        QVector<int16_t> resampled(newLen);
        for (int i = 0; i < newLen; ++i) {
            const double srcIdx = i / ratio;
            const int idx0 = qMin(static_cast<int>(srcIdx), pcm.size() - 1);
            const int idx1 = qMin(idx0 + 1, pcm.size() - 1);
            const double frac = srcIdx - idx0;
            resampled[i] = static_cast<int16_t>(pcm[idx0] * (1.0 - frac) + pcm[idx1] * frac);
        }
        pcm = resampled;
    }

    if (outSampleCount)
        *outSampleCount = pcm.size();

    // Re-encode to target format
    switch (targetFormat) {
    case AudioSampleFormat::SNES_BRR:
        return encodeToBRR(pcm);
    case AudioSampleFormat::NES_DPCM:
        return encodeToDPCM(pcm);
    case AudioSampleFormat::MD_DPCM4_6500: {
        // IMA ADPCM encoder — matches the IMA decoder in audiodetector.cpp.
        static constexpr int kIndexTable[16] = {
            -1, -1, -1, -1, 2, 4, 6, 8,
            -1, -1, -1, -1, 2, 4, 6, 8
        };
        static constexpr int kStepTable[89] = {
                7,     8,     9,    10,    11,    12,    13,    14,    16,    17,
               19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
               50,    55,    60,    66,    73,    80,    88,    97,   107,   118,
              130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
              337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
              876,   963,  1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
             2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
             5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
            15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
        };

        QByteArray out((pcm.size() + 1) / 2, 0);
        int predictor = 0;
        int stepIndex = 0;

        for (int i = 0; i < pcm.size(); ++i) {
            const int sample = pcm[i];
            int diff = sample - predictor;
            int nibble = 0;
            const int step = kStepTable[stepIndex];

            if (diff < 0) {
                nibble = 8;
                diff = -diff;
            }
            if (diff >= step)         { nibble |= 4; diff -= step; }
            if (diff >= (step >> 1))  { nibble |= 2; diff -= step >> 1; }
            if (diff >= (step >> 2))  { nibble |= 1; }

            // Reconstruct decoder output to track predictor exactly
            int reconDiff = step >> 3;
            if (nibble & 1) reconDiff += step >> 2;
            if (nibble & 2) reconDiff += step >> 1;
            if (nibble & 4) reconDiff += step;
            if (nibble & 8) reconDiff = -reconDiff;

            predictor = qBound(-32768, predictor + reconDiff, 32767);
            stepIndex = qBound(0, stepIndex + kIndexTable[nibble], 88);

            // Low nibble first, high nibble second (standard IMA packing)
            if ((i & 1) == 0)
                out[i / 2] = static_cast<char>(nibble & 0x0F);
            else
                out[i / 2] = static_cast<char>((static_cast<uint8_t>(out[i / 2]) & 0x0F) | ((nibble & 0x0F) << 4));
        }
        return out;
    }
    case AudioSampleFormat::MD_DAC_PCM:
    case AudioSampleFormat::Raw_PCM8_Unsigned: {
        QByteArray out(pcm.size(), 0);
        for (int i = 0; i < pcm.size(); ++i)
            out[i] = static_cast<char>(static_cast<uint8_t>((pcm[i] / 256) + 128));
        return out;
    }
    case AudioSampleFormat::MD_PCM8_Signed:
    case AudioSampleFormat::GBA_PCM8:
    case AudioSampleFormat::Raw_PCM8_Signed: {
        QByteArray out(pcm.size(), 0);
        for (int i = 0; i < pcm.size(); ++i)
            out[i] = static_cast<char>(static_cast<int8_t>(pcm[i] / 256));
        return out;
    }
    case AudioSampleFormat::MD_ULAW: {
        // µ-law encoder: 16-bit PCM → 8-bit µ-law
        static constexpr int BIAS = 0x84;
        static constexpr int MAX_VAL = 32635;
        QByteArray out(pcm.size(), 0);
        for (int i = 0; i < pcm.size(); ++i) {
            int sample = pcm[i];
            int sign = 0;
            if (sample < 0) { sign = 0x80; sample = -sample; }
            if (sample > MAX_VAL) sample = MAX_VAL;
            sample += BIAS;
            int exponent = 7;
            for (int mask = 0x4000; !(sample & mask) && exponent > 0; --exponent, mask >>= 1)
                ;
            int mantissa = (sample >> (exponent + 3)) & 0x0F;
            out[i] = static_cast<char>(~(sign | (exponent << 4) | mantissa));
        }
        return out;
    }
    case AudioSampleFormat::MD_ADPCM_OKI: {
        // OKI/Dialogic ADPCM encoder
        static constexpr int kStepTable[49] = {
             16,   17,   19,   21,   23,   25,   28,   31,   34,   37,
             41,   45,   50,   55,   60,   66,   73,   80,   88,   97,
            107,  118,  130,  143,  157,  173,  190,  209,  230,  253,
            279,  307,  337,  371,  408,  449,  494,  544,  598,  658,
            724,  796,  876,  963, 1060, 1166, 1282, 1411, 1552
        };
        static constexpr int kIndexTable[8] = {
            -1, -1, -1, -1, 2, 4, 6, 8
        };

        // OKI works in 12-bit range, so scale input
        QByteArray out((pcm.size() + 1) / 2, 0);
        int predictor = 0;
        int stepIndex = 0;

        for (int i = 0; i < pcm.size(); ++i) {
            const int sample = pcm[i] / 16;  // 16-bit → 12-bit scale
            int diff = sample - predictor;
            int nibble = 0;
            const int step = kStepTable[stepIndex];

            if (diff < 0) { nibble = 8; diff = -diff; }
            if (diff >= step)         { nibble |= 4; diff -= step; }
            if (diff >= (step >> 1))  { nibble |= 2; diff -= step >> 1; }
            if (diff >= (step >> 2))  { nibble |= 1; }

            int reconDiff = step >> 3;
            if (nibble & 1) reconDiff += step >> 2;
            if (nibble & 2) reconDiff += step >> 1;
            if (nibble & 4) reconDiff += step;
            if (nibble & 8) reconDiff = -reconDiff;

            predictor = qBound(-2048, predictor + reconDiff, 2047);
            stepIndex = qBound(0, stepIndex + kIndexTable[nibble & 7], 48);

            // High nibble first, low nibble second (OKI packing)
            if ((i & 1) == 0)
                out[i / 2] = static_cast<char>((nibble & 0x0F) << 4);
            else
                out[i / 2] = static_cast<char>((static_cast<uint8_t>(out[i / 2]) & 0xF0) | (nibble & 0x0F));
        }
        return out;
    }
    default:
        break;
    }

    return {};
}

// ─── BRR Encoder (simple, non-optimal) ─────────────────────────────

QByteArray AudioPlayer::encodeToBRR(const QVector<int16_t> &pcm16)
{
    const int numSamples = pcm16.size();
    const int numBlocks = (numSamples + 15) / 16;
    QByteArray brr(numBlocks * 9, 0);

    int old1 = 0, old2 = 0;
    Q_UNUSED(old2)  // used only when filter > 0 (future enhancement)

    for (int block = 0; block < numBlocks; ++block) {
        const int sampleStart = block * 16;
        const bool isLast = (block == numBlocks - 1);

        // Find best range: try to minimize quantization error
        int bestRange = 0;
        double bestError = 1e18;

        for (int range = 0; range <= 12; ++range) {
            double error = 0;
            for (int s = 0; s < 16; ++s) {
                const int idx = sampleStart + s;
                const int16_t target = (idx < numSamples) ? pcm16[idx] : 0;
                int shifted = target;
                if (range > 0)
                    shifted >>= (range - 1);
                // Clamp to 4-bit signed
                shifted = qBound(-8, shifted, 7);
                int reconstructed = (range <= 12) ? (shifted << range) >> 1 : 0;
                double diff = target - reconstructed;
                error += diff * diff;
            }
            if (error < bestError) {
                bestError = error;
                bestRange = range;
            }
        }

        // Header: range | filter=0 | loop=0 | end
        uint8_t header = static_cast<uint8_t>((bestRange << 4) | (isLast ? 0x01 : 0x00));
        brr[block * 9] = static_cast<char>(header);

        // Encode 16 nibbles
        for (int i = 0; i < 8; ++i) {
            uint8_t dataByte = 0;
            for (int n = 0; n < 2; ++n) {
                const int sIdx = sampleStart + i * 2 + n;
                const int16_t target = (sIdx < numSamples) ? pcm16[sIdx] : 0;
                int prediction = 0;  // filter 0: no prediction

                int residual = target - prediction;
                int nibble;
                if (bestRange > 0)
                    nibble = residual >> (bestRange - 1);
                else
                    nibble = residual;

                nibble = qBound(-8, nibble, 7);

                if (n == 0)
                    dataByte = static_cast<uint8_t>((nibble & 0x0F) << 4);
                else
                    dataByte |= static_cast<uint8_t>(nibble & 0x0F);

                int decoded;
                if (bestRange <= 12)
                    decoded = (nibble << bestRange) >> 1;
                else
                    decoded = (nibble < 0) ? -2048 : 0;

                old2 = old1;
                old1 = decoded;
            }
            brr[block * 9 + 1 + i] = static_cast<char>(dataByte);
        }
    }

    return brr;
}

// ─── DPCM Encoder ──────────────────────────────────────────────────

QByteArray AudioPlayer::encodeToDPCM(const QVector<int16_t> &pcm16)
{
    // Convert 16-bit samples to 7-bit DAC levels, then delta encode
    const int numBits = pcm16.size();
    const int numBytes = (numBits + 7) / 8;
    QByteArray dpcm(numBytes, 0);

    int output = 64;  // midpoint

    for (int i = 0; i < numBits; ++i) {
        const int target = qBound(0, static_cast<int>((pcm16[i] / 256) + 64), 127);
        const int byteIdx = i / 8;
        const int bitIdx = i % 8;

        if (target >= output) {
            // Step up
            dpcm[byteIdx] = static_cast<char>(static_cast<uint8_t>(dpcm[byteIdx]) | (1 << bitIdx));
            output = qMin(output + 2, 127);
        } else {
            // Step down (bit stays 0)
            output = qMax(output - 2, 0);
        }
    }

    return dpcm;
}

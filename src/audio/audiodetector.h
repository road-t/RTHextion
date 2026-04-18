#ifndef AUDIODETECTOR_H
#define AUDIODETECTOR_H

#include <QByteArray>
#include <QVector>
#include <QString>
#include "romdetect.h"

/// Describes the encoding of a detected audio sample.
enum class AudioSampleFormat {
    Unknown = 0,
    SNES_BRR,       ///< SNES Bit Rate Reduction (9-byte blocks, 16 samples each)
    NES_DPCM,       ///< NES 1-bit delta modulation
    MD_DAC_PCM,     ///< Mega Drive unsigned 8-bit PCM (center 0x80)
    MD_PCM8_Signed, ///< Mega Drive signed 8-bit PCM (center 0x00)
    MD_ULAW,        ///< Mega Drive µ-law compressed 8-bit
    MD_DPCM4_6500,  ///< Mega Drive / UMK3 4-bit IMA ADPCM (~6500 Hz)
    MD_ADPCM_OKI,   ///< OKI/Dialogic ADPCM 4-bit (MSM6295-style)
    GBA_PCM8,       ///< GBA signed 8-bit PCM
    GB_Wave4bit,    ///< Game Boy 4-bit wave patterns (16 bytes = 32 samples)
    Raw_PCM8_Signed,    ///< Generic signed 8-bit PCM
    Raw_PCM8_Unsigned,  ///< Generic unsigned 8-bit PCM
};

/// A single detected audio sample region in ROM.
struct DetectedSample {
    qint64 offset = 0;       ///< byte offset in file
    qint64 length = 0;       ///< length in bytes
    AudioSampleFormat format = AudioSampleFormat::Unknown;
    int sampleRate = 0;      ///< estimated playback sample rate (Hz)
    bool hasLoop = false;     ///< true if sample has a loop point
    qint64 loopOffset = 0;   ///< byte offset of loop start (relative to sample start)
    QString name;             ///< auto-generated name
    float confidence = 0.0f;  ///< detection confidence [0..1]
};

/// Encapsulates audio-sample detection heuristics for various ROM formats.
class AudioDetector
{
public:
    AudioDetector();

    /// Detect audio samples in the given ROM data.
    /// \param data     Full ROM data (or a large enough window)
    /// \param romType  The detected ROM platform type
    /// \return List of detected sample regions, sorted by offset
    QVector<DetectedSample> detect(const QByteArray &data, RomType romType) const;

    // ─── Per-format detectors ───

    /// Scan for SNES BRR sample blocks (9-byte aligned blocks with valid headers).
    QVector<DetectedSample> detectBRR(const QByteArray &data) const;

    /// Scan for NES DPCM samples (in the last 16KB of PRG banks).
    QVector<DetectedSample> detectDPCM(const QByteArray &data) const;

    /// Scan for Mega Drive DAC PCM data (unsigned 8-bit, centered around 0x80).
    QVector<DetectedSample> detectMD_DAC(const QByteArray &data) const;

    /// Scan for Mega Drive signed 8-bit PCM data (centered around 0x00).
    QVector<DetectedSample> detectMD_Signed8(const QByteArray &data) const;

    /// Scan for UMK3-style Mega Drive 4-bit IMA ADPCM samples (~6500 Hz).
    QVector<DetectedSample> detectMD_DPCM4(const QByteArray &data) const;

    /// Scan for GBA signed 8-bit PCM samples (Sappy/M4A engine structures).
    QVector<DetectedSample> detectGBA_PCM(const QByteArray &data) const;

    /// Decode a detected sample to signed 16-bit PCM at its native sample rate.
    /// Returns empty on failure.
    static QVector<int16_t> decodeToPCM16(const QByteArray &rawData,
                                           AudioSampleFormat format,
                                           int *outSampleRate = nullptr);

    /// Decode SNES BRR blocks to 16-bit PCM.
    static QVector<int16_t> decodeBRR(const QByteArray &brrData);

    /// Decode NES DPCM to 16-bit PCM.
    static QVector<int16_t> decodeDPCM(const QByteArray &dpcmData);

    /// Decode unsigned 8-bit PCM (MD DAC) to 16-bit signed PCM.
    static QVector<int16_t> decodeUnsigned8(const QByteArray &data);

    /// Decode Mega Drive 4-bit IMA ADPCM to 16-bit signed PCM.
    static QVector<int16_t> decodeMD_DPCM4(const QByteArray &data);

    /// Decode µ-law compressed 8-bit to 16-bit signed PCM.
    static QVector<int16_t> decodeMD_ULAW(const QByteArray &data);

    /// Decode OKI/Dialogic ADPCM 4-bit to 16-bit signed PCM.
    static QVector<int16_t> decodeMD_OKI_ADPCM(const QByteArray &data);

    /// Decode signed 8-bit PCM (GBA) to 16-bit signed PCM.
    static QVector<int16_t> decodeSigned8(const QByteArray &data);

    // ─── Settings ───
    int minSampleBytes() const { return m_minSampleBytes; }
    void setMinSampleBytes(int n) { m_minSampleBytes = n; }

    float minConfidence() const { return m_minConfidence; }
    void setMinConfidence(float c) { m_minConfidence = c; }

private:
    void addMDSample(QVector<DetectedSample> &results,
                     const uint8_t *d, int offset, int length) const;

    int m_minSampleBytes = 64;     ///< minimum sample size to consider
    float m_minConfidence = 0.5f;  ///< minimum confidence threshold
};

#endif // AUDIODETECTOR_H

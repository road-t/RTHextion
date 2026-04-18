#ifndef AUDIOPLAYER_H
#define AUDIOPLAYER_H

#include <QObject>
#include <QByteArray>
#include <QVector>
#include <QBuffer>
#include <QAudioFormat>
#include <memory>
#include "audiodetector.h"

class QAudioSink;

/// Simple audio player for decoded PCM samples.
/// Wraps Qt Multimedia QAudioSink and provides play/stop/state.
class AudioPlayer : public QObject
{
    Q_OBJECT

public:
    explicit AudioPlayer(QObject *parent = nullptr);
    ~AudioPlayer();

    /// Load a decoded sample into the player.
    /// \param pcm16    16-bit signed PCM samples
    /// \param sampleRate  playback sample rate in Hz
    void loadSample(const QVector<int16_t> &pcm16, int sampleRate);

    /// Load directly from raw ROM data + format (decodes internally).
    void loadFromRaw(const QByteArray &rawData, AudioSampleFormat format);

    bool isPlaying() const;
    bool isLoaded() const { return !m_wavData.isEmpty(); }

    /// Duration in milliseconds.
    int durationMs() const;

    /// Export the currently loaded sample as a WAV file.
    bool exportWav(const QString &filePath) const;

    /// Create WAV data in memory from PCM16 samples.
    static QByteArray createWavData(const QVector<int16_t> &pcm16, int sampleRate);

    /// Import a WAV file and return raw PCM data for the target format.
    /// Resamples + re-encodes as needed.
    static QByteArray importWav(const QString &filePath,
                                AudioSampleFormat targetFormat,
                                int targetSampleRate,
                                int *outSampleCount = nullptr);

    /// Encode signed 16-bit PCM to BRR format.
    static QByteArray encodeToBRR(const QVector<int16_t> &pcm16);

    /// Encode signed 16-bit PCM to NES DPCM format.
    static QByteArray encodeToDPCM(const QVector<int16_t> &pcm16);

public slots:
    void play();
    void play(int sampleRateOverride, double speedMultiplier);
    void stop();

signals:
    void playbackStarted();
    void playbackStopped();

private:
    void buildWavData(const QVector<int16_t> &pcm16, int sampleRate);

    QAudioSink *m_audioSink = nullptr;
    QBuffer m_audioBuffer;
    QByteArray m_wavData;     // raw WAV file bytes (for export and playback)
    QByteArray m_pcmPayload;  // raw PCM bytes (for QAudioSink)
    int m_sampleRate = 0;
    int m_sampleCount = 0;
};

#endif // AUDIOPLAYER_H

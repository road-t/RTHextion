#ifndef AUDIODOCKWIDGET_H
#define AUDIODOCKWIDGET_H

#include "BaseDockWidget.h"
#include "audiodetector.h"
#include "romdetect.h"

#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLabel>

class AudioDockWidget : public BaseDockWidget
{
    Q_OBJECT

public:
    explicit AudioDockWidget(QWidget *parent = nullptr);
    ~AudioDockWidget() override;

    /// Update the format combo to show only formats relevant for this ROM type.
    void setRomType(RomType romType);

    /// Selected audio format (may be Unknown for "auto-detect").
    AudioSampleFormat selectedFormat() const;

    /// Selected sample rate in Hz.
    int selectedSampleRate() const;

    /// Playback speed multiplier (1.0 = normal).
    double playbackSpeed() const;

    /// Get/set format combo index for per-tab persistence.
    int formatIndex() const;
    void setFormatIndex(int index);

    /// Get/set sample rate text for per-tab persistence.
    QString sampleRateText() const;
    void setSampleRateText(const QString &text);

    /// Set playback speed for per-tab persistence.
    void setPlaybackSpeed(double speed);

    void retranslateUi() override;

signals:
    void formatChanged(AudioSampleFormat fmt);
    void sampleRateChanged(int rate);
    void playbackSpeedChanged(double speed);

protected:
    void onPaletteChanged() override;

private:
    void populateFormats(RomType romType);

    QComboBox *m_formatCombo = nullptr;
    QComboBox *m_rateCombo = nullptr;
    QDoubleSpinBox *m_speedSpin = nullptr;
    QLabel *m_formatLabel = nullptr;
    QLabel *m_rateLabel = nullptr;
    QLabel *m_speedLabel = nullptr;
};

#endif // AUDIODOCKWIDGET_H

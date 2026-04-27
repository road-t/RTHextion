#include "AudioDockWidget.h"
#include "DockTitleBar.h"

#include <QVBoxLayout>
#include <QFormLayout>

namespace {

QVector<AudioSampleFormat> allAudioFormats()
{
    return {
        AudioSampleFormat::Unknown,
        AudioSampleFormat::SNES_BRR,
        AudioSampleFormat::NES_DPCM,
        AudioSampleFormat::MD_DAC_PCM,
        AudioSampleFormat::MD_PCM8_Signed,
        AudioSampleFormat::MD_ULAW,
        AudioSampleFormat::MD_DPCM4_6500,
        AudioSampleFormat::MD_ADPCM_OKI,
        AudioSampleFormat::GBA_PCM8,
        AudioSampleFormat::GB_Wave4bit,
        AudioSampleFormat::Raw_PCM8_Unsigned,
        AudioSampleFormat::Raw_PCM8_Signed,
    };
}

QVector<AudioSampleFormat> preferredAudioFormatsForRom(RomType rom)
{
    switch (rom) {
    case RomType::SNES:
    case RomType::SNES_SMC:
    case RomType::SNES_HIROM:
    case RomType::SNES_HIROM_SMC:
        return {AudioSampleFormat::SNES_BRR};
    case RomType::NES:
        return {AudioSampleFormat::NES_DPCM};
    case RomType::MD:
    case RomType::X32:
        return {
            AudioSampleFormat::MD_DAC_PCM,
            AudioSampleFormat::MD_PCM8_Signed,
            AudioSampleFormat::MD_ULAW,
            AudioSampleFormat::MD_DPCM4_6500,
            AudioSampleFormat::MD_ADPCM_OKI,
        };
    case RomType::GBA:
        return {AudioSampleFormat::GBA_PCM8};
    case RomType::GB:
    case RomType::GBC:
        return {AudioSampleFormat::GB_Wave4bit};
    default:
        return {};
    }
}

QString audioFormatLabel(AudioSampleFormat fmt)
{
    switch (fmt) {
    case AudioSampleFormat::Unknown:           return QObject::tr("Auto");
    case AudioSampleFormat::SNES_BRR:          return QStringLiteral("SNES BRR");
    case AudioSampleFormat::NES_DPCM:          return QStringLiteral("NES DPCM");
    case AudioSampleFormat::MD_DAC_PCM:        return QStringLiteral("MD DAC PCM (unsigned 8-bit)");
    case AudioSampleFormat::MD_PCM8_Signed:    return QStringLiteral("MD Signed 8-bit PCM");
    case AudioSampleFormat::MD_ULAW:           return QStringLiteral("MD µ-law");
    case AudioSampleFormat::MD_DPCM4_6500:     return QStringLiteral("MD IMA ADPCM 4-bit (~6500 Hz)");
    case AudioSampleFormat::MD_ADPCM_OKI:      return QStringLiteral("OKI/Dialogic ADPCM 4-bit");
    case AudioSampleFormat::GBA_PCM8:          return QStringLiteral("GBA PCM8");
    case AudioSampleFormat::GB_Wave4bit:       return QStringLiteral("GB Wave 4-bit");
    case AudioSampleFormat::Raw_PCM8_Unsigned: return QStringLiteral("Raw PCM8 Unsigned");
    case AudioSampleFormat::Raw_PCM8_Signed:   return QStringLiteral("Raw PCM8 Signed");
    }
    return QObject::tr("Auto");
}

} // namespace

AudioDockWidget::AudioDockWidget(QWidget *parent)
    : BaseDockWidget(tr("Audio"), parent)
{
    setWindowTitle(tr("Audio"));
    setObjectName(QStringLiteral("AudioDockWidget"));

    auto *container = new QWidget(this);
    m_contentWidget = container;
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    auto *form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(4);

    // ── Format ──
    m_formatLabel = new QLabel(tr("Format") + ":", this);
    m_formatCombo = new QComboBox(this);
    m_formatCombo->addItem(tr("Auto"), static_cast<int>(AudioSampleFormat::Unknown));
    form->addRow(m_formatLabel, m_formatCombo);

    // ── Sample rate ──
    m_rateLabel = new QLabel(tr("Sample rate") + ":", this);
    m_rateCombo = new QComboBox(this);
    m_rateCombo->setEditable(true);
    m_rateCombo->addItems({
        QStringLiteral("4000"),
        QStringLiteral("8000"),
        QStringLiteral("8363"),
        QStringLiteral("11025"),
        QStringLiteral("13379"),
        QStringLiteral("16000"),
        QStringLiteral("22050"),
        QStringLiteral("32000"),
        QStringLiteral("44100"),
    });
    m_rateCombo->setCurrentText(QStringLiteral("8000"));
    form->addRow(m_rateLabel, m_rateCombo);

    // ── Playback speed ──
    m_speedLabel = new QLabel(tr("Speed") + QStringLiteral(":"), this);
    m_speedSpin = new QDoubleSpinBox(this);
    m_speedSpin->setRange(0.25, 4.0);
    m_speedSpin->setSingleStep(0.25);
    m_speedSpin->setValue(1.0);
    m_speedSpin->setSuffix(QStringLiteral("x"));
    m_speedSpin->setDecimals(2);
    form->addRow(m_speedLabel, m_speedSpin);

    layout->addLayout(form);
    layout->addStretch();
    setWidget(container);
    initTitleBar();

    applySectionActiveState();

    connect(m_formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        emit formatChanged(selectedFormat());
    });
    connect(m_rateCombo, &QComboBox::currentTextChanged, this, [this](const QString &) {
        bool ok = false;
        const int rate = m_rateCombo->currentText().toInt(&ok);
        if (ok && rate > 0)
            emit sampleRateChanged(rate);
    });
    connect(m_speedSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double val) {
        emit playbackSpeedChanged(val);
    });
}

AudioDockWidget::~AudioDockWidget() = default;

void AudioDockWidget::setRomType(RomType romType)
{
    m_currentRomType = romType;
    populateFormats(romType);
}

void AudioDockWidget::setSectionActive(bool active)
{
    if (m_sectionActive == active)
        return;
    m_sectionActive = active;
    applySectionActiveState();
}

AudioSampleFormat AudioDockWidget::selectedFormat() const
{
    return static_cast<AudioSampleFormat>(m_formatCombo->currentData().toInt());
}

int AudioDockWidget::selectedSampleRate() const
{
    bool ok = false;
    const int rate = m_rateCombo->currentText().toInt(&ok);
    return (ok && rate > 0) ? rate : 8000;
}

double AudioDockWidget::playbackSpeed() const
{
    return m_speedSpin->value();
}

int AudioDockWidget::formatIndex() const
{
    return m_formatCombo->currentIndex();
}

void AudioDockWidget::setFormatIndex(int index)
{
    if (index >= 0 && index < m_formatCombo->count()) {
        const QSignalBlocker blocker(m_formatCombo);
        m_formatCombo->setCurrentIndex(index);
    }
}

void AudioDockWidget::setSelectedFormat(AudioSampleFormat format)
{
    const int wanted = static_cast<int>(format);
    int idx = 0; // default to Auto
    for (int i = 0; i < m_formatCombo->count(); ++i) {
        if (m_formatCombo->itemData(i).toInt() == wanted) {
            idx = i;
            break;
        }
    }
    const QSignalBlocker blocker(m_formatCombo);
    m_formatCombo->setCurrentIndex(idx);
}

QString AudioDockWidget::sampleRateText() const
{
    return m_rateCombo->currentText();
}

void AudioDockWidget::setSampleRateText(const QString &text)
{
    const QSignalBlocker blocker(m_rateCombo);
    m_rateCombo->setCurrentText(text);
}

void AudioDockWidget::setPlaybackSpeed(double speed)
{
    const QSignalBlocker blocker(m_speedSpin);
    m_speedSpin->setValue(speed);
}

void AudioDockWidget::retranslateUi()
{
    setWindowTitle(tr("Audio"));
    m_formatLabel->setText(tr("Format") + ":");
    m_rateLabel->setText(tr("Sample rate") + ":");
    m_speedLabel->setText(tr("Speed") + ":");
}

void AudioDockWidget::onPaletteChanged()
{
    // Nothing special needed
}

void AudioDockWidget::populateFormats(RomType romType)
{
    const AudioSampleFormat current = selectedFormat();
    const QSignalBlocker blocker(m_formatCombo);
    m_formatCombo->clear();

    const QVector<AudioSampleFormat> preferred = preferredAudioFormatsForRom(romType);
    const QVector<AudioSampleFormat> all = allAudioFormats();

    auto appendFmt = [this](AudioSampleFormat fmt) {
        m_formatCombo->addItem(audioFormatLabel(fmt), static_cast<int>(fmt));
    };

    for (AudioSampleFormat fmt : preferred)
        appendFmt(fmt);

    if (romType != RomType::Unknown && !preferred.isEmpty())
        m_formatCombo->insertSeparator(m_formatCombo->count());

    for (AudioSampleFormat fmt : all) {
        if (preferred.contains(fmt))
            continue;
        appendFmt(fmt);
    }

    int bestIndex = 0;
    for (int i = 0; i < m_formatCombo->count(); ++i) {
        if (m_formatCombo->itemData(i).toInt() == static_cast<int>(current)) {
            bestIndex = i;
            break;
        }
    }
    m_formatCombo->setCurrentIndex(bestIndex);

    switch (romType) {
    case RomType::SNES:
    case RomType::SNES_SMC:
    case RomType::SNES_HIROM:
    case RomType::SNES_HIROM_SMC:
        m_rateCombo->setCurrentText(QStringLiteral("32000"));
        break;
    case RomType::NES:
        m_rateCombo->setCurrentText(QStringLiteral("8363"));
        break;
    case RomType::GBA:
        m_rateCombo->setCurrentText(QStringLiteral("13379"));
        break;
    default:
        break;
    }
}

void AudioDockWidget::applySectionActiveState()
{
    if (m_contentWidget)
        m_contentWidget->setEnabled(m_sectionActive);
}

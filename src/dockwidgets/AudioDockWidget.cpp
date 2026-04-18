#include "AudioDockWidget.h"
#include "DockTitleBar.h"

#include <QVBoxLayout>
#include <QFormLayout>

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
    m_formatLabel = new QLabel(tr("Format:"), this);
    m_formatCombo = new QComboBox(this);
    m_formatCombo->addItem(tr("Auto"), static_cast<int>(AudioSampleFormat::Unknown));
    form->addRow(m_formatLabel, m_formatCombo);

    // ── Sample rate ──
    m_rateLabel = new QLabel(tr("Sample rate:"), this);
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
    m_speedLabel = new QLabel(tr("Speed:"), this);
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
    populateFormats(romType);
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
    m_formatLabel->setText(tr("Format:"));
    m_rateLabel->setText(tr("Sample rate:"));
    m_speedLabel->setText(tr("Speed:"));
}

void AudioDockWidget::onPaletteChanged()
{
    // Nothing special needed
}

void AudioDockWidget::populateFormats(RomType romType)
{
    const QSignalBlocker blocker(m_formatCombo);
    m_formatCombo->clear();
    m_formatCombo->addItem(tr("Auto"), static_cast<int>(AudioSampleFormat::Unknown));

    switch (romType) {
    case RomType::SNES:
    case RomType::SNES_SMC:
    case RomType::SNES_HIROM:
    case RomType::SNES_HIROM_SMC:
        m_formatCombo->addItem(QStringLiteral("SNES BRR"), static_cast<int>(AudioSampleFormat::SNES_BRR));
        m_rateCombo->setCurrentText(QStringLiteral("32000"));
        break;
    case RomType::NES:
        m_formatCombo->addItem(QStringLiteral("NES DPCM"), static_cast<int>(AudioSampleFormat::NES_DPCM));
        m_rateCombo->setCurrentText(QStringLiteral("8363"));
        break;
    case RomType::MD:
    case RomType::X32:
        m_formatCombo->addItem(QStringLiteral("MD DAC PCM (unsigned 8-bit)"), static_cast<int>(AudioSampleFormat::MD_DAC_PCM));
        m_formatCombo->addItem(QStringLiteral("MD Signed 8-bit PCM"), static_cast<int>(AudioSampleFormat::MD_PCM8_Signed));
        m_formatCombo->addItem(QStringLiteral("MD µ-law"), static_cast<int>(AudioSampleFormat::MD_ULAW));
        m_formatCombo->addItem(QStringLiteral("IMA ADPCM 4-bit (UMK3)"), static_cast<int>(AudioSampleFormat::MD_DPCM4_6500));
        m_formatCombo->addItem(QStringLiteral("OKI/Dialogic ADPCM 4-bit"), static_cast<int>(AudioSampleFormat::MD_ADPCM_OKI));
        m_rateCombo->setCurrentText(QStringLiteral("8000"));
        break;
    case RomType::GBA:
        m_formatCombo->addItem(QStringLiteral("GBA PCM8"), static_cast<int>(AudioSampleFormat::GBA_PCM8));
        m_rateCombo->setCurrentText(QStringLiteral("13379"));
        break;
    default:
        m_formatCombo->addItem(QStringLiteral("Raw PCM8 Unsigned"), static_cast<int>(AudioSampleFormat::Raw_PCM8_Unsigned));
        m_formatCombo->addItem(QStringLiteral("Raw PCM8 Signed"), static_cast<int>(AudioSampleFormat::Raw_PCM8_Signed));
        break;
    }
}

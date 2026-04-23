#ifndef MAINWINDOW_INTERNAL_H
#define MAINWINDOW_INTERNAL_H

#include <QString>
#include <QFileInfo>
#include "appsettings.h"
#include <QUrl>
#include <QInputDialog>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>

#include "mainwindow.h"
#include "translationtable.h"

namespace MainWindowInternal
{

inline const char *kLastFileDirKey = "Paths/LastFileDir";
inline const char *kLastTableDirKey = "Paths/LastTableDir";
inline const char *kLastDumpDirKey = "Paths/LastDumpDir";
inline const char *kMainWindowStateKey = "MainWindow/State";
inline const char *kRecentFilesKey = "RecentFiles";
inline const char *kRecentTablesKey = "RecentTables";
inline const char *kRecentProjectsKey = "RecentProjects";
inline constexpr int kMaxRecentFiles = 10;
inline constexpr int kMaxRecentTables = 10;
inline constexpr int kMaxRecentProjects = 10;

inline QString projectUiSettingsPrefix(const QString &projectPath)
{
    QString normalized = QFileInfo(projectPath).canonicalFilePath();
    if (normalized.isEmpty())
        normalized = QFileInfo(projectPath).absoluteFilePath();
    return QStringLiteral("ProjectUi/%1")
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(normalized)));
}

inline bool isTableLikeFilePath(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == QStringLiteral("tbl")
        || ext == QStringLiteral("tab")
        || ext == QStringLiteral("table");
}

inline QString chooseTableImportEncoding(QWidget *parent, const QString &fileName, bool *accepted = nullptr)
{
    if (accepted)
        *accepted = true;

    QFile rawFile(fileName);
    if (!rawFile.open(QIODevice::ReadOnly))
        return QString();

    const QByteArray raw = rawFile.readAll();
    rawFile.close();

    if (!TranslationTable::hasNonAsciiValueBytes(raw))
        return QString();

    const QStringList encodings = TranslationTable::supportedImportEncodings();
    if (encodings.isEmpty())
        return QString();

    const QString guessed = TranslationTable::guessImportEncoding(raw);
    const int defaultIndex = qMax(0, encodings.indexOf(guessed));

    bool ok = false;
    const QString selected = QInputDialog::getItem(
        parent,
        MainWindow::tr("Table encoding"),
        MainWindow::tr("Select encoding for imported table:"),
        encodings,
        defaultIndex,
        false,
        &ok);

    if (accepted)
        *accepted = ok;

    return ok ? selected : QString();
}

inline QChar readSingleCharSetting(const AppSettings &settings, const char *key, const QChar &fallback)
{
    const QString value = settings.value(key, QString(fallback)).toString();
    return value.isEmpty() ? fallback : value.at(0);
}

inline QMap<QString, QString> parseSectionOptions(const QString &raw)
{
    QMap<QString, QString> out;
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty())
        return out;

    // Backward compatibility: old projects stored JSON in options.
    if (trimmed.startsWith(QLatin1Char('{'))) {
        const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8());
        if (doc.isObject()) {
            const QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                const QString key = it.key().trimmed().toLower();
                if (key.isEmpty())
                    continue;
                const QJsonValue v = it.value();
                QString value;
                if (v.isString())
                    value = v.toString().trimmed();
                else if (v.isDouble())
                    value = QString::number(v.toDouble(), 'g', 15);
                else if (v.isBool())
                    value = v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
                else
                    value = QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
                if (!value.isEmpty())
                    out.insert(key, value);
            }
            return out;
        }
    }

    const QStringList pairs = trimmed.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &pairRaw : pairs) {
        const QString pair = pairRaw.trimmed();
        if (pair.isEmpty())
            continue;
        const int eq = pair.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString key = pair.left(eq).trimmed().toLower();
        const QString value = pair.mid(eq + 1).trimmed();
        if (!key.isEmpty() && !value.isEmpty())
            out.insert(key, value);
    }
    return out;
}

inline QString serializeSectionOptions(const QMap<QString, QString> &opts)
{
    QStringList parts;
    for (auto it = opts.constBegin(); it != opts.constEnd(); ++it) {
        const QString key = it.key().trimmed().toLower();
        const QString value = it.value().trimmed();
        if (key.isEmpty() || value.isEmpty())
            continue;
        parts.append(key + QStringLiteral("=") + value);
    }
    return parts.join(QLatin1Char(';'));
}

inline QString audioSubtypeMnemonicFromFormat(AudioSampleFormat format)
{
    switch (format) {
    case AudioSampleFormat::SNES_BRR: return QStringLiteral("snes-brr");
    case AudioSampleFormat::NES_DPCM: return QStringLiteral("nes-dpcm");
    case AudioSampleFormat::MD_DAC_PCM: return QStringLiteral("md-dac-pcm");
    case AudioSampleFormat::MD_PCM8_Signed: return QStringLiteral("md-pcm8-signed");
    case AudioSampleFormat::MD_ULAW: return QStringLiteral("md-ulaw");
    case AudioSampleFormat::MD_DPCM4_6500: return QStringLiteral("md-dpcm4-6500");
    case AudioSampleFormat::MD_ADPCM_OKI: return QStringLiteral("md-adpcm-oki");
    case AudioSampleFormat::GBA_PCM8: return QStringLiteral("gba-pcm8");
    case AudioSampleFormat::GB_Wave4bit: return QStringLiteral("gb-wave4bit");
    case AudioSampleFormat::Raw_PCM8_Unsigned: return QStringLiteral("raw-pcm8-unsigned");
    case AudioSampleFormat::Raw_PCM8_Signed: return QStringLiteral("raw-pcm8-signed");
    case AudioSampleFormat::Unknown:
    default:
        return QStringLiteral("auto");
    }
}

inline QString audioSubtypeGuessFromName(const QString &name, RomType romType)
{
    const QString n = name.toLower();
    if (n.contains(QStringLiteral("brr")))
        return QStringLiteral("snes-brr");
    if (n.contains(QStringLiteral("umk3"))
        || n.contains(QStringLiteral("ima adpcm"))
        || n.contains(QStringLiteral("dpcm4"))
        || n.contains(QStringLiteral("4-bit dpcm")))
        return QStringLiteral("md-dpcm4-6500");
    if (n.contains(QStringLiteral("oki")) || n.contains(QStringLiteral("dialogic")))
        return QStringLiteral("md-adpcm-oki");
    if (n.contains(QStringLiteral("dpcm"))) {
        if (romType == RomType::MD || romType == RomType::X32)
            return QStringLiteral("md-dpcm4-6500");
        return QStringLiteral("nes-dpcm");
    }
    if (n.contains(QStringLiteral("ulaw")) || n.contains(QStringLiteral("µ-law")) || n.contains(QStringLiteral("mu-law")))
        return QStringLiteral("md-ulaw");
    if (n.contains(QStringLiteral("signed pcm")) || n.contains(QStringLiteral("signed 8")))
        return QStringLiteral("md-pcm8-signed");
    if (n.contains(QStringLiteral("dac")))
        return QStringLiteral("md-dac-pcm");
    if (n.contains(QStringLiteral("gba")))
        return QStringLiteral("gba-pcm8");

    switch (romType) {
    case RomType::SNES:
    case RomType::SNES_SMC:
    case RomType::SNES_HIROM:
    case RomType::SNES_HIROM_SMC:
        return QStringLiteral("snes-brr");
    case RomType::NES:
        return QStringLiteral("nes-dpcm");
    case RomType::MD:
    case RomType::X32:
        return QStringLiteral("md-dac-pcm");
    case RomType::GBA:
        return QStringLiteral("gba-pcm8");
    default:
        return QStringLiteral("raw-pcm8-unsigned");
    }
}

inline AudioSampleFormat audioFormatFromSubtypeMnemonic(const QString &mnemonic)
{
    const QString m = mnemonic.trimmed().toLower();
    if (m == QLatin1String("snes-brr")) return AudioSampleFormat::SNES_BRR;
    if (m == QLatin1String("nes-dpcm")) return AudioSampleFormat::NES_DPCM;
    if (m == QLatin1String("md-dac-pcm")) return AudioSampleFormat::MD_DAC_PCM;
    if (m == QLatin1String("md-pcm8-signed")) return AudioSampleFormat::MD_PCM8_Signed;
    if (m == QLatin1String("md-ulaw")) return AudioSampleFormat::MD_ULAW;
    if (m == QLatin1String("md-dpcm4-6500")) return AudioSampleFormat::MD_DPCM4_6500;
    if (m == QLatin1String("md-adpcm-oki")) return AudioSampleFormat::MD_ADPCM_OKI;
    if (m == QLatin1String("gba-pcm8")) return AudioSampleFormat::GBA_PCM8;
    if (m == QLatin1String("gb-wave4bit")) return AudioSampleFormat::GB_Wave4bit;
    if (m == QLatin1String("raw-pcm8-signed")) return AudioSampleFormat::Raw_PCM8_Signed;
    if (m == QLatin1String("raw-pcm8-unsigned")) return AudioSampleFormat::Raw_PCM8_Unsigned;
    return AudioSampleFormat::Unknown;
}

inline int defaultSampleRateForAudioFormat(AudioSampleFormat fmt)
{
    switch (fmt) {
    case AudioSampleFormat::SNES_BRR: return 32000;
    case AudioSampleFormat::NES_DPCM: return 8363;
    case AudioSampleFormat::MD_DPCM4_6500: return 6500;
    case AudioSampleFormat::MD_ADPCM_OKI: return 7575;
    case AudioSampleFormat::GBA_PCM8: return 13379;
    default: return 8000;
    }
}

inline QString sectionAudioSubtypeMnemonic(const Section &sec, RomType romType)
{
    const auto opts = parseSectionOptions(sec.options);
    const QString t = opts.value(QStringLiteral("type")).trimmed().toLower();
    if (!t.isEmpty())
        return t;
    return audioSubtypeGuessFromName(sec.name, romType);
}

inline int sectionAudioSampleRate(const Section &sec, RomType romType)
{
    const auto opts = parseSectionOptions(sec.options);
    bool ok = false;
    const int rate = opts.value(QStringLiteral("sample_rate")).toInt(&ok);
    if (ok && rate > 0)
        return rate;

    const AudioSampleFormat fmt = audioFormatFromSubtypeMnemonic(sectionAudioSubtypeMnemonic(sec, romType));
    return defaultSampleRateForAudioFormat(fmt);
}

inline double sectionAudioSpeed(const Section &sec)
{
    const auto opts = parseSectionOptions(sec.options);
    bool ok = false;
    const double speed = opts.value(QStringLiteral("speed")).toDouble(&ok);
    if (ok && speed > 0.0)
        return speed;
    return 1.0;
}

} // namespace MainWindowInternal

#endif // MAINWINDOW_INTERNAL_H

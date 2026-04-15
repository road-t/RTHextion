#ifndef MAINWINDOW_INTERNAL_H
#define MAINWINDOW_INTERNAL_H

#include <QString>
#include <QFileInfo>
#include "appsettings.h"
#include <QUrl>
#include <QInputDialog>
#include <QFile>

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

} // namespace MainWindowInternal

#endif // MAINWINDOW_INTERNAL_H

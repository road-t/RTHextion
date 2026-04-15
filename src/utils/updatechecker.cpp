#include "updatechecker.h"
#include "appinfo.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include "appsettings.h"
#include <QDateTime>
#include <QVersionNumber>

static const char *kApiUrl =
    "https://api.github.com/repos/road-t/RTHextion/releases/latest";
static const char *kSettingsLastCheck = "Updates/lastCheckTime";

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    connect(m_nam, &QNetworkAccessManager::finished,
            this, &UpdateChecker::onReplyFinished);
}

void UpdateChecker::check(bool silent)
{
    m_silent = silent;
    QNetworkRequest req(QUrl(QString::fromLatin1(kApiUrl)));
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("RTHextion/%1").arg(AppInfo::Version));
    req.setRawHeader("Accept", "application/vnd.github+json");
    m_nam->get(req);
}

bool UpdateChecker::shouldAutoCheck()
{
    auto &settings = AppSettings::instance();
    QDateTime last = settings.value(kSettingsLastCheck).toDateTime();
    if (!last.isValid())
        return true;
    return last.secsTo(QDateTime::currentDateTimeUtc()) >= 86400;
}

void UpdateChecker::markChecked()
{
    auto &settings = AppSettings::instance();
    settings.setValue(kSettingsLastCheck, QDateTime::currentDateTimeUtc());
}

void UpdateChecker::onReplyFinished(QNetworkReply *reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        if (!m_silent)
            emit checkFailed(reply->errorString());
        return;
    }

    const QByteArray data = reply->readAll();
    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
    if (doc.isNull()) {
        if (!m_silent)
            emit checkFailed(parseErr.errorString());
        return;
    }

    QJsonObject obj = doc.object();
    QString tagName = obj.value(QLatin1String("tag_name")).toString();
    QString htmlUrl = obj.value(QLatin1String("html_url")).toString();
    QString body    = obj.value(QLatin1String("body")).toString();

    // Strip leading 'v' from tag
    QString remoteVer = tagName;
    if (remoteVer.startsWith(QLatin1Char('v')) || remoteVer.startsWith(QLatin1Char('V')))
        remoteVer = remoteVer.mid(1);

    QVersionNumber remote = QVersionNumber::fromString(remoteVer);
    QVersionNumber local  = QVersionNumber::fromString(QString::fromLatin1(AppInfo::Version));

    markChecked();

    if (remote > local)
        emit updateAvailable(remoteVer, htmlUrl, body);
    else if (!m_silent)
        emit upToDate();
}

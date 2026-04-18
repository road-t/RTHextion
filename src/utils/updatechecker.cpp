#include "updatechecker.h"
#include "appinfo.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QByteArray>
#include "appsettings.h"
#include <QDateTime>
#include <QVersionNumber>
#include <QString>

static const char *kApiUrl =
    "https://api.github.com/repos/road-t/RTHextion/releases/latest";
static const char *kSettingsLastCheck = "Updates/lastCheckTime";
static const char *kSettingsGithubToken = "Updates/githubToken";

static QString readGithubToken()
{
    auto &settings = AppSettings::instance();
    const QString settingsToken = settings.value(QString::fromLatin1(kSettingsGithubToken)).toString().trimmed();
    if (!settingsToken.isEmpty())
        return settingsToken;

    const QString rtHexToken = qEnvironmentVariable("RTHEX_GITHUB_TOKEN").trimmed();
    if (!rtHexToken.isEmpty())
        return rtHexToken;

    const QString ghToken = qEnvironmentVariable("GITHUB_TOKEN").trimmed();
    if (!ghToken.isEmpty())
        return ghToken;

    const QString ghCliToken = qEnvironmentVariable("GH_TOKEN").trimmed();
    if (!ghCliToken.isEmpty())
        return ghCliToken;

    return QString();
}

static QByteArray authHeaderValueForToken(const QString &token)
{
    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty())
        return QByteArray();

    // Keep explicit prefixes if user provided them.
    if (trimmed.startsWith(QStringLiteral("Bearer "), Qt::CaseInsensitive)
        || trimmed.startsWith(QStringLiteral("token "), Qt::CaseInsensitive))
        return trimmed.toUtf8();

    return QByteArray("Bearer ") + trimmed.toUtf8();
}

static QString githubErrorMessageFromPayload(const QByteArray &payload)
{
    QJsonParseError parseErr;
    const QJsonDocument errDoc = QJsonDocument::fromJson(payload, &parseErr);
    if (errDoc.isNull() || !errDoc.isObject())
        return QString();

    const QJsonObject obj = errDoc.object();
    const QString message = obj.value(QLatin1String("message")).toString();
    const QString docUrl = obj.value(QLatin1String("documentation_url")).toString();

    if (message.isEmpty())
        return QString();
    if (docUrl.isEmpty())
        return message;

    return message + QStringLiteral("\n") + docUrl;
}

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
    req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");

    const QString token = readGithubToken();
    const QByteArray authHeader = authHeaderValueForToken(token);
    if (!authHeader.isEmpty())
        req.setRawHeader("Authorization", authHeader);

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

    const QVariant statusVar = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const int statusCode = statusVar.isValid() ? statusVar.toInt() : 0;
    const QByteArray data = reply->readAll();
    const QString payloadError = githubErrorMessageFromPayload(data);

    if (reply->error() != QNetworkReply::NoError) {
        if (!m_silent) {
            QString err = payloadError;
            if (err.isEmpty())
                err = reply->errorString();

            if (statusCode == 403 && err.contains(QStringLiteral("rate limit"), Qt::CaseInsensitive)) {
                err += QStringLiteral("\n\nSet a GitHub token via environment variable RTHEX_GITHUB_TOKEN (or GITHUB_TOKEN / GH_TOKEN) ")
                     + QStringLiteral("or in settings key Updates/githubToken to use authenticated requests.");
            }

            emit checkFailed(err);
        }
        return;
    }

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

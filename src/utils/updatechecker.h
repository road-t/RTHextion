#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#include <QObject>

class QNetworkAccessManager;
class QNetworkReply;

class UpdateChecker : public QObject
{
    Q_OBJECT
public:
    explicit UpdateChecker(QObject *parent = nullptr);

    /// Start an async check. If \a silent is true, no dialog is shown when
    /// the app is already up-to-date (used for the automatic daily check).
    void check(bool silent = false);

    /// Returns true if we should run the automatic daily check right now.
    static bool shouldAutoCheck();
    /// Records the current timestamp so the next auto-check happens 24 h later.
    static void markChecked();

signals:
    void updateAvailable(const QString &latestVersion, const QString &releaseUrl,
                         const QString &releaseNotes);
    void upToDate();
    void checkFailed(const QString &errorString);

private slots:
    void onReplyFinished(QNetworkReply *reply);

private:
    QNetworkAccessManager *m_nam = nullptr;
    bool m_silent = false;
};

#endif // UPDATECHECKER_H

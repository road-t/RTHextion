#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QMap>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QVariant>

/// Drop-in replacement for QSettings that stores data in a human-readable
/// YAML file.  Provides a singleton accessed via instance().
///
/// File lookup order:
///   1. ./settings.yaml  (working directory of the process)
///   2. System config dir (macOS: ~/Library/Application Support/<App>,
///      Linux: ~/.config/<App>, Windows: AppData/Local/<App>)
///
/// On first run the class migrates data from the legacy QSettings store.
class AppSettings
{
public:
    static AppSettings &instance();

    // ---- QSettings-compatible API ----
    QVariant value(const QString &key, const QVariant &defaultValue = QVariant()) const;
    void setValue(const QString &key, const QVariant &value);
    void remove(const QString &key);
    bool contains(const QString &key) const;
    QStringList allKeys() const;
    QStringList childGroups() const;
    QStringList childKeys() const;
    void beginGroup(const QString &prefix);
    void endGroup();
    QString group() const;
    void sync();

    QString filePath() const;
    bool isUsingLocalConfig() const;

private:
    AppSettings();
    ~AppSettings();
    AppSettings(const AppSettings &) = delete;
    AppSettings &operator=(const AppSettings &) = delete;

    void load();
    void save() const;
    void migrateFromQSettings();
    QString resolvedKey(const QString &key) const;

    // ---- Tree ↔ flat-map conversion ----
    static QVariantMap buildTree(const QMap<QString, QVariant> &flat);
    static void flattenTree(const QVariantMap &tree, const QString &prefix,
                            QMap<QString, QVariant> &out);

    // ---- YAML I/O ----
    static QString emitYaml(const QVariantMap &tree, int indent = 0);
    static QVariantMap parseYaml(const QString &text);

    QMap<QString, QVariant> m_data;
    QStringList m_groupStack;
    QString m_filePath;
    bool m_usingLocalConfig = false;
    mutable bool m_dirty = false;
    mutable QMutex m_mutex;
};

#endif // APPSETTINGS_H

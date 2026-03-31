#ifndef LANGTRANSLATOR_H
#define LANGTRANSLATOR_H

#include <QTranslator>
#include <QHash>

class LangTranslator : public QTranslator
{
    Q_OBJECT

public:
    explicit LangTranslator(QObject *parent = nullptr);

    bool load(const QString &filename);
    bool isEmpty() const override;
    QString translate(const char *context, const char *sourceText,
                      const char *disambiguation = nullptr, int n = -1) const override;

    static QString currentLanguage();
    static void setCurrentLanguage(const QString &lang);

private:
    QHash<QString, QString> m_translations;

    static QString s_currentLanguage;
};

#endif // LANGTRANSLATOR_H

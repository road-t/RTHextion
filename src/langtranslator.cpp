#include "langtranslator.h"
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QtGlobal>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#endif

QString LangTranslator::s_currentLanguage = QStringLiteral("en");

LangTranslator::LangTranslator(QObject *parent)
    : QTranslator(parent)
{
}

bool LangTranslator::load(const QString &filename)
{
    m_translations.clear();

    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qWarning() << "LangTranslator: cannot open" << filename;
        return false;
    }

    QTextStream in(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    in.setEncoding(QStringConverter::Utf8);
#else
    in.setCodec("UTF-8");
#endif

    while (!in.atEnd())
    {
        const QString line = in.readLine().trimmed();

        if (line.isEmpty() || line.startsWith('#'))
            continue;

        const int sep = line.indexOf('=');
        if (sep <= 0)
            continue;

        QString key = line.left(sep);
        QString value = line.mid(sep + 1);

        // Unescape \n back to real newlines
        key.replace(QLatin1String("\\n"), QLatin1String("\n"));
        value.replace(QLatin1String("\\n"), QLatin1String("\n"));

        m_translations.insert(key, value);
    }

    return !m_translations.isEmpty();
}

bool LangTranslator::isEmpty() const
{
    return m_translations.isEmpty();
}

QString LangTranslator::translate(const char *context, const char *sourceText,
                                  const char *disambiguation, int n) const
{
    Q_UNUSED(context)
    Q_UNUSED(disambiguation)
    Q_UNUSED(n)

    const QString key = QString::fromUtf8(sourceText);
    auto it = m_translations.constFind(key);
    if (it != m_translations.constEnd())
        return it.value();

    // No translation found — return empty string so Qt falls back to source text
    return {};
}

QString LangTranslator::currentLanguage()
{
    return s_currentLanguage;
}

void LangTranslator::setCurrentLanguage(const QString &lang)
{
    s_currentLanguage = lang;
}

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QLocale>
#include <QSettings>
#include <QFile>
#include <QStringList>
#include <QTimer>
#include <QByteArray>

#include "appinfo.h"
#include "langtranslator.h"
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    Q_INIT_RESOURCE(rthextion);

#ifdef Q_OS_MACOS
    // Avoid long startup stalls in Cocoa state restoration path.
    qputenv("ApplePersistenceIgnoreState", QByteArray("YES"));
#endif

    QApplication app(argc, argv);
    app.setApplicationName(AppInfo::Name);
    app.setApplicationDisplayName(QStringLiteral("RTHextion"));
    app.setApplicationVersion(AppInfo::Version);
    app.setOrganizationName(AppInfo::Name);
    
    app.setWindowIcon(QIcon(":/images/tj.png")
    QString locale = settings.value("Language", QLocale::system().name()).toString();
    const QString languageShort = locale.left(2);

    LangTranslator *translator = new LangTranslator(&app);
    QStringList candidates;
    if (!languageShort.isEmpty() && languageShort != locale)
        candidates << languageShort;

    bool loaded = false;
    for (const QString &candidate : candidates)
    {
        const QString path = QStringLiteral(":/translations/") + candidate + QStringLiteral(".lang");
        if (!QFile::exists(path))
            continue;
        if (translator->load(path))
        {
            loaded = true;
            break;
        }
    }

    if (loaded)
    {
        app.installTranslator(translator);
    }
    LangTranslator::setCurrentLanguage(locale);

    QCommandLineParser parser;
    parser.addPositionalArgument("file", "File to open");
    parser.addHelpOption();


    MainWindow *mainWin = new MainWindow
    {
        mainWin->loadFile(parser.positionalArguments().at(0));
        mainWin->loadFile(parser.positionalArguments().at(0));rofiler::instance().checkpoint("Scheduling mainWin->show()");
    QTimer::singleShot(0, mainWin, &QWidget::show);

    return app.exec();
}
QTimer::singleShot(0, mainWin, &QWidget::show);

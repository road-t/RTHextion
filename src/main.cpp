#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QLocale>
#include <QSettings>

#include "appinfo.h"
#include "langtranslator.h"
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    Q_INIT_RESOURCE(rthextion);
    QApplication app(argc, argv);
    app.setApplicationName(AppInfo::Name);
    app.setApplicationDisplayName(QStringLiteral("RTHextion"));
    app.setApplicationVersion(AppInfo::Version);
    app.setOrganizationName(AppInfo::Name);
    app.setWindowIcon(QIcon(":/images/tj.png"));

    // Load language from settings (default to system locale)
    QSettings settings;
    QString locale = settings.value("Language", QLocale::system().name()).toString();
    const QString languageShort = locale.left(2);

    LangTranslator *translator = new LangTranslator(&app);
    if (translator->load(QStringLiteral(":/translations/") + locale + QStringLiteral(".lang")) ||
        translator->load(QStringLiteral(":/translations/") + languageShort + QStringLiteral(".lang")))
    {
        app.installTranslator(translator);
    }
    LangTranslator::setCurrentLanguage(locale);

    QCommandLineParser parser;
    parser.addPositionalArgument("file", "File to open");
    parser.addHelpOption();
    parser.process(app);
    MainWindow *mainWin = new MainWindow;

    if(!parser.positionalArguments().isEmpty())
        mainWin->loadFile(parser.positionalArguments().at(0));

    mainWin->show();

    return app.exec();
}

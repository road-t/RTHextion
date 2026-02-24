#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

#include "mainwindow.h"

int main(int argc, char *argv[])
{
    Q_INIT_RESOURCE(rthextion);
    QApplication app(argc, argv);
    app.setApplicationName("RTHextion");
    app.setOrganizationName("RTHextion");
    app.setWindowIcon(QIcon(":images/rthextion.ico"));

    // Identify locale and load translation if available
    QSettings settings;
    QString locale = settings.value("Language", QLocale::system().name().left(2)).toString();
    QTranslator translator;
    if (!translator.load(QString(":/translations/qhexedit_") + locale))
        translator.load(QString("qhexedit_") + locale); // fallback: look on disk
    app.installTranslator(&translator);

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

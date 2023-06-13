#include <QApplication>
#include <QIcon>

#include "mainwindow.h"

int main(int argc, char *argv[])
{
    Q_INIT_RESOURCE(rthextion);
    QApplication app(argc, argv);
    app.setApplicationName("RTHextion");
    app.setOrganizationName("RTHextion");
    app.setWindowIcon(QIcon(":images/rthextion.ico"));

    // Identify locale and load translation if available
    QString locale = QLocale::system().name();
    QTranslator translator;
    translator.load(QString("qhexedit_") + locale);
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

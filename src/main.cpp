#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QLocale>
#include <QSettings>
#include <QFile>
#include <QStringList>
#include <QTimer>
#include <QByteArray>
#include <QPalette>

#include "appinfo.h"
#include "langtranslator.h"
#include "mainwindow.h"

namespace
{
    // Ensure all application settings have default values on first launch
    void initializeDefaultSettings()
    {
        QSettings settings(AppInfo::Name, AppInfo::Name);

        // Initialize boolean settings
        if (!settings.contains("AddressArea"))
            settings.setValue("AddressArea", true);
        if (!settings.contains("AsciiArea"))
            settings.setValue("AsciiArea", true);
        if (!settings.contains("Highlighting"))
            settings.setValue("Highlighting", true);
        if (!settings.contains("ShowHexGrid"))
            settings.setValue("ShowHexGrid", true);
        if (!settings.contains("Autosize"))
            settings.setValue("Autosize", true);
        if (!settings.contains("AutoLoadRecentFile"))
            settings.setValue("AutoLoadRecentFile", true);
        if (!settings.contains("DetectEndianness"))
            settings.setValue("DetectEndianness", true);
        if (!settings.contains("HexCaps"))
            settings.setValue("HexCaps", true);
        if (!settings.contains("OverwriteMode"))
            settings.setValue("OverwriteMode", true);

        // Initialize numeric settings
        if (!settings.contains("AddressAreaWidth"))
            settings.setValue("AddressAreaWidth", 8);
        if (!settings.contains("BytesPerLine"))
            settings.setValue("BytesPerLine", 32);

        // Initialize character settings
        if (!settings.contains("NonPrintableNoTableChar"))
            settings.setValue("NonPrintableNoTableChar", QString(QChar(0x25AA)));
        if (!settings.contains("NotInTableChar"))
            settings.setValue("NotInTableChar", QString(QChar(0x25A1)));

        // Initialize color settings
        if (!settings.contains("HighlightingColor"))
            settings.setValue("HighlightingColor", QColor(0xff, 0xff, 0x99, 0xff));
        if (!settings.contains("PointedColor"))
            settings.setValue("PointedColor", QColor(0xc0, 0x80, 0x00, 0xff));
        if (!settings.contains("PointedFontColor"))
            settings.setValue("PointedFontColor", QColor(Qt::black));
        if (!settings.contains("PointerFontColor"))
            settings.setValue("PointerFontColor", QColor(Qt::black));
        if (!settings.contains("PointerFrameColor"))
            settings.setValue("PointerFrameColor", QColor(0x00, 0x00, 0xFF));
        if (!settings.contains("PointerFrameBgColor"))
            settings.setValue("PointerFrameBgColor", QColor(0x00, 0xFF, 0x00, 0x80));
        if (!settings.contains("HexAreaBackgroundColor"))
            settings.setValue("HexAreaBackgroundColor", QColor(Qt::white));
        if (!settings.contains("HexAreaGridColor"))
            settings.setValue("HexAreaGridColor", QColor(0x99, 0x99, 0x99));
        if (!settings.contains("CursorCharColor"))
            settings.setValue("CursorCharColor", QColor(0x00, 0x60, 0xFF, 0x80));
        if (!settings.contains("CursorFrameColor"))
            settings.setValue("CursorFrameColor", QColor(Qt::black));
        if (!settings.contains("ScrollMapPtrBgColor"))
            settings.setValue("ScrollMapPtrBgColor", QColor(0xd0, 0xd0, 0xd0));
        if (!settings.contains("ScrollMapTargetBgColor"))
            settings.setValue("ScrollMapTargetBgColor", QColor(0xd0, 0xd0, 0xd0));

        // Palette-dependent colors - use system defaults if not set
        if (!settings.contains("AddressAreaColor"))
            settings.setValue("AddressAreaColor", QApplication::palette().alternateBase().color());
        if (!settings.contains("SelectionColor"))
            settings.setValue("SelectionColor", QApplication::palette().highlight().color());
        if (!settings.contains("AddressFontColor"))
            settings.setValue("AddressFontColor", QApplication::palette().color(QPalette::WindowText));
        if (!settings.contains("AsciiAreaColor"))
            settings.setValue("AsciiAreaColor", QApplication::palette().alternateBase().color());
        if (!settings.contains("AsciiFontColor"))
            settings.setValue("AsciiFontColor", QApplication::palette().color(QPalette::WindowText));
        if (!settings.contains("HexFontColor"))
            settings.setValue("HexFontColor", QApplication::palette().color(QPalette::WindowText));

        // Font setting
        if (!settings.contains("WidgetFont"))
        {
#ifdef Q_OS_WIN32
            QFont defaultFont("Courier", 14);
#else
            QFont defaultFont("Courier New", 14);
#endif
            settings.setValue("WidgetFont", defaultFont);
        }

        // Persist all settings to disk
        settings.sync();
    }
}

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
    
    app.setWindowIcon(QIcon(":/images/tj.png"));

    // Initialize default settings on first launch
    initializeDefaultSettings();

    QSettings settings(AppInfo::Name, AppInfo::Name);
    QString locale = settings.value("Language", QStringLiteral("en")).toString();
    const QString languageShort = locale.left(2);

    LangTranslator *translator = new LangTranslator(&app);
    QStringList candidates;
    candidates << locale;
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

    parser.process(app);

    MainWindow *mainWin = new MainWindow();
    if (parser.positionalArguments().size() > 0)
    {
        mainWin->loadFile(parser.positionalArguments().at(0));
    }

    QTimer::singleShot(0, mainWin, &QWidget::show);

    return app.exec();
}

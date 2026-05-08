#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QLocale>
#include <QScreen>
#include "appsettings.h"
#include <QFile>
#include <QStringList>
#include <QTimer>
#include <QByteArray>
#include <QPalette>
#include <QFileOpenEvent>

#include "appinfo.h"
#include "langtranslator.h"
#include "mainwindow.h"

namespace
{
    class RTHextionApplication : public QApplication
    {
    public:
        using QApplication::QApplication;

        void setFileOpenHandler(const std::function<void(const QString &)> &handler)
        {
            m_fileOpenHandler = handler;
            if (!m_fileOpenHandler)
                return;

            const auto pending = m_pendingFileOpenEvents;
            m_pendingFileOpenEvents.clear();
            for (const QString &path : pending)
                m_fileOpenHandler(path);
        }

    protected:
        bool event(QEvent *event) override
        {
            if (event && event->type() == QEvent::FileOpen) {
                auto *foe = static_cast<QFileOpenEvent *>(event);
                const QString path = foe ? foe->file() : QString();
                if (!path.isEmpty()) {
                    if (m_fileOpenHandler)
                        m_fileOpenHandler(path);
                    else
                        m_pendingFileOpenEvents.append(path);
                    event->accept();
                    return true;
                }
            }
            return QApplication::event(event);
        }

    private:
        std::function<void(const QString &)> m_fileOpenHandler;
        QStringList m_pendingFileOpenEvents;
    };

    // Ensure all application settings have default values on first launch
    void initializeDefaultSettings()
    {
        auto &settings = AppSettings::instance();

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
        if (!settings.contains("DetectEncoding"))
            settings.setValue("DetectEncoding", true);
        if (!settings.contains("HexCaps"))
            settings.setValue("HexCaps", true);
        if (!settings.contains("OverwriteMode"))
            settings.setValue("OverwriteMode", true);

        // Initialize numeric settings
        if (!settings.contains("AddressAreaWidth"))
            settings.setValue("AddressAreaWidth", 8);
        if (!settings.contains("BytesPerLine"))
            settings.setValue("BytesPerLine", 32);
        if (!settings.contains("ScrollMapWidth"))
            settings.setValue("ScrollMapWidth", 12);

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
        if (!settings.contains("MultibyteFrameColor"))
            settings.setValue("MultibyteFrameColor", QColor(0x20, 0x20, 0x20));
        if (!settings.contains("CursorCharColor"))
            settings.setValue("CursorCharColor", QColor(0x00, 0x60, 0xFF, 0x80));
        if (!settings.contains("CursorFrameColor"))
            settings.setValue("CursorFrameColor", QColor(Qt::black));
        if (!settings.contains("ScrollMapPtrBgColor"))
            settings.setValue("ScrollMapPtrBgColor", QColor(0xd0, 0xd0, 0xd0));
        if (!settings.contains("ScrollMapTargetBgColor"))
            settings.setValue("ScrollMapTargetBgColor", QColor(0xd0, 0xd0, 0xd0));
        if (!settings.contains("SectionHeaderBgColor"))
            settings.setValue("SectionHeaderBgColor", QColor(0xD8, 0xD8, 0xD8, 0x90));

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
        if (!settings.contains("SectionHeaderFontColor"))
            settings.setValue("SectionHeaderFontColor", QApplication::palette().color(QPalette::WindowText));
        if (!settings.contains("ShowColumnNumbers"))
            settings.setValue("ShowColumnNumbers", false);
        if (!settings.contains("ColumnNumbersFontColor"))
            settings.setValue("ColumnNumbersFontColor", QApplication::palette().color(QPalette::WindowText));
        if (!settings.contains("ColumnNumbersBackgroundColor"))
            settings.setValue("ColumnNumbersBackgroundColor", QApplication::palette().alternateBase().color());

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
        if (!settings.contains("SectionHeaderFont"))
        {
            QFont sectionHeaderFont = settings.value("WidgetFont").value<QFont>();
            sectionHeaderFont.setBold(true);
            settings.setValue("SectionHeaderFont", sectionHeaderFont);
        }
        if (!settings.contains("ColumnNumbersFont"))
            settings.setValue("ColumnNumbersFont", settings.value("WidgetFont").value<QFont>());

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

    RTHextionApplication app(argc, argv);
    app.setApplicationName(AppInfo::Name);
    app.setApplicationDisplayName(QStringLiteral("RTHextion"));
    app.setApplicationVersion(AppInfo::Version);
    app.setOrganizationName(AppInfo::Name);
#ifdef Q_OS_LINUX
    QGuiApplication::setDesktopFileName(QStringLiteral("RTHextion"));
#endif
    
    app.setWindowIcon(QIcon(":/images/tj.png"));

    // Initialize default settings on first launch
    initializeDefaultSettings();

    auto &settings = AppSettings::instance();
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

    app.setFileOpenHandler([mainWin](const QString &filePath) {
        if (filePath.isEmpty())
            return;
        if (!mainWin->isVisible()) {
            mainWin->loadFile(filePath);
            return;
        }
        mainWin->loadFileInNewTab(filePath);
    });

    if (parser.positionalArguments().size() > 0)
    {
        mainWin->loadFile(parser.positionalArguments().at(0));
    }

    const bool firstLaunch = !settings.contains(QStringLiteral("WindowGeometry"));
    if (firstLaunch) {
#ifdef Q_OS_WIN
        QTimer::singleShot(0, mainWin, &QWidget::showMaximized);
#else
        QTimer::singleShot(0, mainWin, [mainWin]() {
            if (QScreen *screen = QApplication::primaryScreen()) {
                mainWin->setGeometry(screen->availableGeometry());
            }
            mainWin->show();
        });
#endif
    } else {
        QTimer::singleShot(0, mainWin, &QWidget::show);
    }

    return app.exec();
}

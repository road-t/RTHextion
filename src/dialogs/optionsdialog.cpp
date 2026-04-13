#include <QColorDialog>
#include <QFontDialog>
#include <QEvent>
#include <QSettings>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QScrollArea>
#include <QKeySequenceEdit>
#include <QCoreApplication>
#include <QListWidget>
#include <QCheckBox>
#include <QGroupBox>
#include <QGridLayout>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QJsonDocument>

#include "theme.h"

namespace
{
    const QChar kDefaultNonPrintableNoTableChar(0x25AA); // ▪
    const QChar kDefaultNotInTableChar(0x25A1);          // □

    constexpr int kBplValues[] = {4,8,16,20,24,28,32,36,40,44,48,52,56,60,64,68,72,76,80};
    constexpr int kBplCount = 19;

    QString sanitizeSingleChar(const QString &text, const QChar &fallback)
    {
        if (text.isEmpty())
            return QString(fallback);
        return text.left(1);
    }

    QColor currentSwatchColor(const QLabel *label)
    {
        return label->palette().color(label->backgroundRole());
    }

    void applyPlaceholderFieldFont(QLineEdit *nonPrintableField, QLineEdit *notInTableField, const QFont &font)
    {
        nonPrintableField->setFont(font);
        notInTableField->setFont(font);

        const int auxFieldWidth = QFontMetrics(font).horizontalAdvance(QStringLiteral("MM")) + 12;
        nonPrintableField->setFixedWidth(auxFieldWidth);
        notInTableField->setFixedWidth(auxFieldWidth);
    }
}

#include "optionsdialog.h"
#include "ui_optionsdialog.h"
#include "mainwindow.h"

OptionsDialog::OptionsDialog(QWidget *parent) : QDialog(parent), ui(new Ui::OptionsDialog)
{
    ui->setupUi(this);
    m_suppressUpdate = true;

    initHotkeysTab();
    initThemesTab();

    resize(qMax(width(), 602), 620);

    // Connect signals for area enable/disable logic
    connect(ui->cbAddressArea, QOverload<int>::of(&QCheckBox::stateChanged),
            this, &OptionsDialog::on_cbAddressArea_stateChanged);
    connect(ui->cbAsciiArea, QOverload<int>::of(&QCheckBox::stateChanged),
            this, &OptionsDialog::on_cbAsciiArea_stateChanged);

    // Connect signals for preview (real-time changes)
    connect(ui->cbAddressArea, &QCheckBox::toggled, this, &OptionsDialog::on_checkBoxToggled);
    connect(ui->cbAsciiArea, &QCheckBox::toggled, this, &OptionsDialog::on_checkBoxToggled);
    // cbDynamicSize replaced by m_cbBytesPerLine combo
    connect(ui->cbShowHexGrid, &QCheckBox::toggled, this, &OptionsDialog::on_checkBoxToggled);
    connect(ui->cbShowMultibyteFrame, &QCheckBox::toggled, this, &OptionsDialog::on_checkBoxToggled);
    connect(ui->cbAutoLoadRecentFile, &QCheckBox::toggled, this, &OptionsDialog::on_checkBoxToggled);
    connect(ui->cbDetectEndianness, &QCheckBox::toggled, this, &OptionsDialog::on_checkBoxToggled);
    connect(ui->cbDetectEncoding, &QCheckBox::toggled, this, &OptionsDialog::on_checkBoxToggled);
    connect(ui->cbResetTableOnClose, &QCheckBox::toggled, this, &OptionsDialog::on_checkBoxToggled);
    connect(ui->cbResetEncodingOnClose, &QCheckBox::toggled, this, &OptionsDialog::on_checkBoxToggled);
    connect(ui->cbAutoFixChecksums, &QCheckBox::toggled, this, &OptionsDialog::on_checkBoxToggled);
    connect(ui->cbDefaultEncoding, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int){ updateSettings(); });
    // Populate default encoding combo
    {
        const QStringList encs = {
            "ASCII", "UTF-8", "UTF-16 LE", "UTF-16 BE", "UTF-32 LE", "UTF-32 BE",
            "Shift-JIS", "EUC-JP", "ISO-2022-JP", "GB2312", "GBK", "GB18030", "EUC-KR",
            "Windows-1251", "KOI8-R", "KOI8-U", "CP-866", "Mac Cyrillic", "ISO-8859-5",
            "ISO-8859-1", "ISO-8859-2", "ISO-8859-3", "ISO-8859-4", "ISO-8859-7",
            "ISO-8859-9", "ISO-8859-10", "ISO-8859-13", "ISO-8859-14", "ISO-8859-15",
            "ISO-8859-16", "Windows-1252",
            "ISO-8859-6", "ISO-8859-8", "ISO-8859-11"
        };
        for (const QString &e : encs)
            ui->cbDefaultEncoding->addItem(e);
    }
    // sbAddressAreaWidth and sbBytesPerLine replaced by combos in initThemesTab
    connect(ui->leNonPrintableNoTableChar, &QLineEdit::textChanged, this, [this]()
            {
        // Only trim to 1 char while editing; don't fill empty — that's done on focus loss
        const QString t = ui->leNonPrintableNoTableChar->text();
        if (t.length() > 1) {
            ui->leNonPrintableNoTableChar->setText(t.left(1));
            return;
        }
        updateSettings(); });
    connect(ui->leNonPrintableNoTableChar, &QLineEdit::editingFinished, this, [this]()
            {
        if (ui->leNonPrintableNoTableChar->text().isEmpty())
            ui->leNonPrintableNoTableChar->setText(QString(kDefaultNonPrintableNoTableChar)); });
    connect(ui->leNotInTableChar, &QLineEdit::textChanged, this, [this]()
            {
        const QString t = ui->leNotInTableChar->text();
        if (t.length() > 1) {
            ui->leNotInTableChar->setText(t.left(1));
            return;
        }
        updateSettings(); });
    connect(ui->leNotInTableChar, &QLineEdit::editingFinished, this, [this]()
            {
        if (ui->leNotInTableChar->text().isEmpty())
            ui->leNotInTableChar->setText(QString(kDefaultNotInTableChar)); });

    // Connect Default button
    connect(ui->pbDefault, &QPushButton::clicked, this, &OptionsDialog::on_pbDefault_clicked);

    // lbBytesPerLine and cbDynamicSize replaced by m_cbBytesPerLine combo

    ui->leNonPrintableNoTableChar->setMaxLength(1);
    ui->leNotInTableChar->setMaxLength(1);
    ui->leNonPrintableNoTableChar->setAlignment(Qt::AlignCenter);
    ui->leNotInTableChar->setAlignment(Qt::AlignCenter);

    ui->cbAddressArea->hide();
    ui->cbAsciiArea->hide();

    applyPlaceholderFieldFont(ui->leNonPrintableNoTableChar, ui->leNotInTableChar, ui->pbWidgetFont->font());

    readSettings();
    writeSettings();
    updateAreaControls();
    m_suppressUpdate = false;

    setModal(true);
}

OptionsDialog::~OptionsDialog()
{
    delete ui;
}

void OptionsDialog::show()
{
    m_suppressUpdate = true;
    readSettings();
    readHotkeySettings();
    // Sync dark mode checkbox with current setting
    QSettings s;
    if (m_cbDarkMode) {
        const bool dark = s.value(QStringLiteral("DarkTheme"), false).toBool();
        m_cbDarkMode->setChecked(dark);
        m_originalSettings.darkMode = dark;
    }
    m_originalThemeId = s.value(QStringLiteral("CurrentTheme"), QStringLiteral("__builtin_light__")).toString();
    m_currentThemeId = m_originalThemeId;

    saveCurrentSettings(); // Save original settings for potential rollback
    updateAreaControls();
    m_suppressUpdate = false;
    QWidget::show();
}

void OptionsDialog::accept()
{
    updateSettings();
    QSettings s;
    if (!m_currentThemeId.isEmpty())
        s.setValue(QStringLiteral("CurrentTheme"), m_currentThemeId);
    s.sync();
    QDialog::accept();
}

void OptionsDialog::reject()
{
    restoreSettings(); // Restore original settings on cancel
    QSettings s;
    if (!m_originalThemeId.isEmpty())
        s.setValue(QStringLiteral("CurrentTheme"), m_originalThemeId);
    s.sync();
    if (m_themeList) {
        for (int i = 0; i < m_themeList->count(); ++i) {
            auto *item = m_themeList->item(i);
            if (item && item->data(Qt::UserRole).toString() == m_originalThemeId) {
                m_themeList->setCurrentItem(item);
                break;
            }
        }
    }
    QDialog::reject();
}

void OptionsDialog::saveCurrentSettings()
{
    m_originalSettings.addressArea = ui->cbAddressArea->isChecked();
    m_originalSettings.addressAreaWidth = 4; // managed by hex editor drag
    m_originalSettings.asciiArea = ui->cbAsciiArea->isChecked();
    m_originalSettings.hexGridShow = ui->cbShowHexGrid->isChecked();
    m_originalSettings.autosize = m_cbBytesPerLine ? (m_cbBytesPerLine->currentIndex() == 0) : true;
    m_originalSettings.autoLoadRecentFile = ui->cbAutoLoadRecentFile->isChecked();
    {
        const int bplIdx = m_cbBytesPerLine ? m_cbBytesPerLine->currentIndex() : 0;
        m_originalSettings.bytesPerLine =
            (bplIdx >= 1 && bplIdx <= kBplCount) ? kBplValues[bplIdx - 1] : 32;
    }
    m_originalSettings.highlightingColor = ui->lbHighlightingColor->palette().color(ui->lbHighlightingColor->backgroundRole());
    m_originalSettings.addressAreaColor = ui->lbAddressAreaColor->palette().color(ui->lbAddressAreaColor->backgroundRole());
    m_originalSettings.addressFontColor = ui->lbAddressFontColor->palette().color(ui->lbAddressFontColor->backgroundRole());
    m_originalSettings.addressZeroByteFontColor = ui->lbAddressZeroByteFontColor->palette().color(ui->lbAddressZeroByteFontColor->backgroundRole());
    m_originalSettings.asciiAreaColor = ui->lbAsciiAreaColor->palette().color(ui->lbAsciiAreaColor->backgroundRole());
    m_originalSettings.asciiFontColor = ui->lbAsciiFontColor->palette().color(ui->lbAsciiFontColor->backgroundRole());
    m_originalSettings.pointedColor = ui->lbPointedColor->palette().color(ui->lbPointedColor->backgroundRole());
    m_originalSettings.pointedFontColor = ui->lbPointedFontColor->palette().color(ui->lbPointedFontColor->backgroundRole());
    m_originalSettings.pointerFontColor = ui->lbPointerFontColor->palette().color(ui->lbPointerFontColor->backgroundRole());
    m_originalSettings.pointerFrameColor = ui->lbPointerFrameColor->palette().color(ui->lbPointerFrameColor->backgroundRole());
    m_originalSettings.pointerFrameBgColor = ui->lbPointerFrameBgColor->palette().color(ui->lbPointerFrameBgColor->backgroundRole());
    m_originalSettings.selectionColor = ui->lbSelectionColor->palette().color(ui->lbSelectionColor->backgroundRole());
    m_originalSettings.hexFontColor = ui->lbHexFontColor->palette().color(ui->lbHexFontColor->backgroundRole());
    m_originalSettings.hexAreaBgColor = ui->lbHexAreaBackground->palette().color(ui->lbHexAreaBackground->backgroundRole());
    m_originalSettings.hexAreaGridColor = ui->lbHexAreaGrid->palette().color(ui->lbHexAreaGrid->backgroundRole());
    m_originalSettings.multibyteFrameColor = ui->lbMultibyteFrameColor->palette().color(ui->lbMultibyteFrameColor->backgroundRole());
    m_originalSettings.showMultibyteFrame = ui->cbShowMultibyteFrame->isChecked();
    m_originalSettings.cursorCharColor = ui->lbCursorCharColor->palette().color(ui->lbCursorCharColor->backgroundRole());
    m_originalSettings.cursorFrameColor = ui->lbCursorFrameColor->palette().color(ui->lbCursorFrameColor->backgroundRole());
    m_originalSettings.zeroByteFontColor = ui->lbZeroByteFontColor->palette().color(ui->lbZeroByteFontColor->backgroundRole());
    m_originalSettings.changesColor = ui->lbChangesColor->palette().color(ui->lbChangesColor->backgroundRole());
    m_originalSettings.scrollMapPtrBgColor = ui->lbScrollMapPtrBgColor->palette().color(ui->lbScrollMapPtrBgColor->backgroundRole());
    m_originalSettings.scrollMapTargetBgColor = ui->lbScrollMapTargetBgColor->palette().color(ui->lbScrollMapTargetBgColor->backgroundRole());
    m_originalSettings.sectionHeaderFontColor = m_lbSectionHeaderFontColor ? currentSwatchColor(m_lbSectionHeaderFontColor) : QColor(Qt::black);
    m_originalSettings.sectionHeaderBgColor = m_lbSectionHeaderBgColor ? currentSwatchColor(m_lbSectionHeaderBgColor) : QColor(0xD8, 0xD8, 0xD8, 0x90);
    m_originalSettings.widgetFont = ui->pbWidgetFont->font();
    m_originalSettings.sectionHeaderFont = m_pbSectionHeaderFont ? m_pbSectionHeaderFont->font() : ui->pbWidgetFont->font();
    m_originalSettings.nonPrintableNoTableChar = sanitizeSingleChar(ui->leNonPrintableNoTableChar->text(), kDefaultNonPrintableNoTableChar);
    m_originalSettings.notInTableChar = sanitizeSingleChar(ui->leNotInTableChar->text(), kDefaultNotInTableChar);
    m_originalSettings.detectEndianness = ui->cbDetectEndianness->isChecked();
    m_originalSettings.detectEncoding = ui->cbDetectEncoding->isChecked();
    m_originalSettings.resetTableOnClose = ui->cbResetTableOnClose->isChecked();
    m_originalSettings.resetEncodingOnClose = ui->cbResetEncodingOnClose->isChecked();
    m_originalSettings.autoFixChecksums = ui->cbAutoFixChecksums->isChecked();
    m_originalSettings.darkMode = m_cbDarkMode ? m_cbDarkMode->isChecked() : false;
    m_originalSettings.defaultEncoding = ui->cbDefaultEncoding->currentText();

    m_originalHotkeys.clear();
    for (const auto &e : m_hotkeys) {
        if (e.editor)
            m_originalHotkeys[e.settingsKey] = e.editor->keySequence();
    }
}

void OptionsDialog::restoreSettings()
{
    m_suppressUpdate = true;
    ui->cbAddressArea->setChecked(m_originalSettings.addressArea);
    // addressAreaWidth managed by hex editor drag
    ui->cbAsciiArea->setChecked(m_originalSettings.asciiArea);
    ui->cbShowHexGrid->setChecked(m_originalSettings.hexGridShow);
    if (m_cbBytesPerLine) {
        if (m_originalSettings.autosize) {
            m_cbBytesPerLine->setCurrentIndex(0);
        } else {
            int idx = 0;
            for (int i = 0; i < kBplCount; ++i) {
                if (kBplValues[i] == m_originalSettings.bytesPerLine) { idx = i + 1; break; }
            }
            m_cbBytesPerLine->setCurrentIndex(idx);
        }
    }
    ui->cbAutoLoadRecentFile->setChecked(m_originalSettings.autoLoadRecentFile);
    setColor(ui->lbHighlightingColor, m_originalSettings.highlightingColor);
    setColor(ui->lbAddressAreaColor, m_originalSettings.addressAreaColor);
    setColor(ui->lbAddressFontColor, m_originalSettings.addressFontColor);
    setColor(ui->lbAddressZeroByteFontColor, m_originalSettings.addressZeroByteFontColor);
    setColor(ui->lbAsciiAreaColor, m_originalSettings.asciiAreaColor);
    setColor(ui->lbAsciiFontColor, m_originalSettings.asciiFontColor);
    setColor(ui->lbPointedColor, m_originalSettings.pointedColor);
    setColor(ui->lbPointedFontColor, m_originalSettings.pointedFontColor);
    setColor(ui->lbPointerFontColor, m_originalSettings.pointerFontColor);
    setColor(ui->lbPointerFrameColor, m_originalSettings.pointerFrameColor);
    setColor(ui->lbPointerFrameBgColor, m_originalSettings.pointerFrameBgColor);
    setColor(ui->lbSelectionColor, m_originalSettings.selectionColor);
    setColor(ui->lbHexFontColor, m_originalSettings.hexFontColor);
    setColor(ui->lbHexAreaBackground, m_originalSettings.hexAreaBgColor);
    setColor(ui->lbHexAreaGrid, m_originalSettings.hexAreaGridColor);
    setColor(ui->lbMultibyteFrameColor, m_originalSettings.multibyteFrameColor);
    ui->cbShowMultibyteFrame->setChecked(m_originalSettings.showMultibyteFrame);
    setColor(ui->lbCursorCharColor, m_originalSettings.cursorCharColor);
    setColor(ui->lbCursorFrameColor, m_originalSettings.cursorFrameColor);
    setColor(ui->lbZeroByteFontColor, m_originalSettings.zeroByteFontColor);
    setColor(ui->lbChangesColor, m_originalSettings.changesColor);
    setColor(ui->lbScrollMapPtrBgColor, m_originalSettings.scrollMapPtrBgColor);
    setColor(ui->lbScrollMapTargetBgColor, m_originalSettings.scrollMapTargetBgColor);
    if (m_lbSectionHeaderFontColor)
        setColor(m_lbSectionHeaderFontColor, m_originalSettings.sectionHeaderFontColor);
    if (m_lbSectionHeaderBgColor)
        setColor(m_lbSectionHeaderBgColor, m_originalSettings.sectionHeaderBgColor);
    if (m_cbDarkMode)
        m_cbDarkMode->setChecked(m_originalSettings.darkMode);
    ui->pbWidgetFont->setFont(m_originalSettings.widgetFont);
    updateFontButtonText(m_originalSettings.widgetFont);
    if (m_pbSectionHeaderFont) {
        m_pbSectionHeaderFont->setFont(m_originalSettings.sectionHeaderFont);
        m_pbSectionHeaderFont->setText(
            QStringLiteral("%1, %2pt").arg(m_originalSettings.sectionHeaderFont.family()).arg(m_originalSettings.sectionHeaderFont.pointSize()));
    }
    ui->leNonPrintableNoTableChar->setText(sanitizeSingleChar(m_originalSettings.nonPrintableNoTableChar, kDefaultNonPrintableNoTableChar));
    ui->leNotInTableChar->setText(sanitizeSingleChar(m_originalSettings.notInTableChar, kDefaultNotInTableChar));
    ui->cbDetectEndianness->setChecked(m_originalSettings.detectEndianness);
    ui->cbDetectEncoding->setChecked(m_originalSettings.detectEncoding);
    ui->cbResetTableOnClose->setChecked(m_originalSettings.resetTableOnClose);
    ui->cbResetEncodingOnClose->setChecked(m_originalSettings.resetEncodingOnClose);
    ui->cbAutoFixChecksums->setChecked(m_originalSettings.autoFixChecksums);
    ui->cbDefaultEncoding->setCurrentText(m_originalSettings.defaultEncoding);

    QSettings s;
    for (auto &e : m_hotkeys) {
        if (e.editor && m_originalHotkeys.contains(e.settingsKey)) {
            e.editor->setKeySequence(m_originalHotkeys[e.settingsKey]);
            s.setValue(e.settingsKey, m_originalHotkeys[e.settingsKey]);
        }
    }
    s.sync();
    auto *mw = qobject_cast<MainWindow*>(parent());
    if (mw) mw->applyShortcutsFromSettings();

    m_suppressUpdate = false;
    updateSettings(); // Apply restored settings
}

void OptionsDialog::updateSettings()
{
    if (m_suppressUpdate)
        return;
    writeSettings();
    applySettings();
}

void OptionsDialog::applySettings()
{
    // Apply settings directly to hex editor without triggering file reload
    // This is called for live preview and doesn't emit accepted() signal
    MainWindow *mainWin = qobject_cast<MainWindow *>(parent());
    if (mainWin)
    {
        QSettings settings;
        mainWin->updateHexEditorSettings();
    }
}

void OptionsDialog::readSettings()
{
    m_suppressUpdate = true;
    QSettings settings;

    // Set combos before other widgets (so any signals see correct combo state)
    if (m_cbBytesPerLine) {
        if (settings.value("Autosize", true).toBool()) {
            m_cbBytesPerLine->setCurrentIndex(0);
        } else {
            const int bpl = settings.value("BytesPerLine", 32).toInt();
            int idx = 0;
            for (int i = 0; i < kBplCount; ++i) {
                if (kBplValues[i] == bpl) { idx = i + 1; break; }
            }
            m_cbBytesPerLine->setCurrentIndex(idx);
        }
    }

    ui->cbAddressArea->setChecked(settings.value("AddressArea", true).toBool());
    ui->cbAsciiArea->setChecked(settings.value("AsciiArea", true).toBool());
    // cbDynamicSize replaced by m_cbBytesPerLine combo
    ui->cbShowHexGrid->setChecked(settings.value("ShowHexGrid", true).toBool());
    ui->cbShowMultibyteFrame->setChecked(settings.value("ShowMultibyteFrame", true).toBool());
    ui->cbAutoLoadRecentFile->setChecked(settings.value("AutoLoadRecentFile", true).toBool());
    ui->cbDetectEndianness->setChecked(settings.value("DetectEndianness", true).toBool());
    ui->cbDetectEncoding->setChecked(settings.value("DetectEncoding", true).toBool());
    ui->cbResetTableOnClose->setChecked(settings.value("ResetTableOnClose", false).toBool());
    ui->cbResetEncodingOnClose->setChecked(settings.value("ResetEncodingOnClose", false).toBool());
    ui->cbAutoFixChecksums->setChecked(settings.value("AutoFixChecksums", false).toBool());
    ui->cbDefaultEncoding->setCurrentText(settings.value("DefaultEncoding", QStringLiteral("ASCII")).toString());

    setColor(ui->lbHighlightingColor, settings.value("HighlightingColor", QColor(0xff, 0xff, 0x99, 0xff)).value<QColor>());
    setColor(ui->lbAddressAreaColor, settings.value("AddressAreaColor", this->palette().alternateBase().color()).value<QColor>());
    setColor(ui->lbPointedColor, settings.value("PointedColor", QColor(0xc0, 0x80, 0x00, 0xff)).value<QColor>());
    setColor(ui->lbPointedFontColor, settings.value("PointedFontColor", QColor(Qt::black)).value<QColor>());
    setColor(ui->lbPointerFontColor, settings.value("PointerFontColor", QColor(Qt::black)).value<QColor>());
    setColor(ui->lbPointerFrameColor, settings.value("PointerFrameColor", QColor(0x00, 0x00, 0xFF)).value<QColor>());
    setColor(ui->lbPointerFrameBgColor, settings.value("PointerFrameBgColor", QColor(0x00, 0xFF, 0x00, 0x80)).value<QColor>());
    setColor(ui->lbSelectionColor, settings.value("SelectionColor", this->palette().highlight().color()).value<QColor>());
    setColor(ui->lbAddressFontColor, settings.value("AddressFontColor", QPalette::WindowText).value<QColor>());
    setColor(ui->lbAddressZeroByteFontColor, settings.value("AddressZeroByteFontColor", settings.value("AddressFontColor", QPalette::WindowText).value<QColor>()).value<QColor>());
    setColor(ui->lbAsciiAreaColor, settings.value("AsciiAreaColor", this->palette().alternateBase().color()).value<QColor>());
    setColor(ui->lbAsciiFontColor, settings.value("AsciiFontColor", QPalette::WindowText).value<QColor>());
    setColor(ui->lbHexFontColor, settings.value("HexFontColor", QPalette::WindowText).value<QColor>());
    setColor(ui->lbHexAreaBackground, settings.value("HexAreaBackgroundColor", QColor(Qt::white)).value<QColor>());
    setColor(ui->lbHexAreaGrid, settings.value("HexAreaGridColor", QColor(0x99, 0x99, 0x99)).value<QColor>());
    setColor(ui->lbMultibyteFrameColor, settings.value("MultibyteFrameColor", QColor(0x20, 0x20, 0x20)).value<QColor>());
    setColor(ui->lbCursorCharColor, settings.value("CursorCharColor", QColor(0x00, 0x60, 0xFF, 0x80)).value<QColor>());
    setColor(ui->lbCursorFrameColor, settings.value("CursorFrameColor", QColor(Qt::black)).value<QColor>());
    setColor(ui->lbZeroByteFontColor, settings.value("ZeroByteFontColor", QColor(0xCC, 0xCC, 0xCC)).value<QColor>());
    setColor(ui->lbChangesColor, settings.value("ChangesColor", QColor(0x99, 0xff, 0x99, 0xff)).value<QColor>());
    setColor(ui->lbScrollMapPtrBgColor, settings.value("ScrollMapPtrBgColor", QColor(0xd0, 0xd0, 0xd0)).value<QColor>());
    setColor(ui->lbScrollMapTargetBgColor, settings.value("ScrollMapTargetBgColor", QColor(0xd0, 0xd0, 0xd0)).value<QColor>());
    if (m_lbSectionHeaderFontColor)
        setColor(m_lbSectionHeaderFontColor, settings.value("SectionHeaderFontColor", palette().color(QPalette::WindowText)).value<QColor>());
    if (m_lbSectionHeaderBgColor)
        setColor(m_lbSectionHeaderBgColor, settings.value("SectionHeaderBgColor", QColor(0xD8, 0xD8, 0xD8, 0x90)).value<QColor>());

#ifdef Q_OS_WIN32
    QFont defaultFont("Courier", 14);
#else
    QFont defaultFont("Courier New", 14);
#endif
    QFont selectedFont = settings.value("WidgetFont", defaultFont).value<QFont>();
    ui->pbWidgetFont->setFont(selectedFont);
    updateFontButtonText(selectedFont);
    if (m_pbSectionHeaderFont) {
        QFont sectionHeaderFont = settings.value("SectionHeaderFont", selectedFont).value<QFont>();
        m_pbSectionHeaderFont->setFont(sectionHeaderFont);
        m_pbSectionHeaderFont->setText(
            QStringLiteral("%1, %2pt").arg(sectionHeaderFont.family()).arg(sectionHeaderFont.pointSize()));
    }
    applyPlaceholderFieldFont(ui->leNonPrintableNoTableChar, ui->leNotInTableChar, selectedFont);

    ui->leNonPrintableNoTableChar->setText(sanitizeSingleChar(settings.value("NonPrintableNoTableChar", QString(kDefaultNonPrintableNoTableChar)).toString(), kDefaultNonPrintableNoTableChar));
    ui->leNotInTableChar->setText(sanitizeSingleChar(settings.value("NotInTableChar", QString(kDefaultNotInTableChar)).toString(), kDefaultNotInTableChar));

    // m_cbBytesPerLine already set above
    // AddressArea and AddressAreaWidth are managed by hex editor drag
    m_suppressUpdate = false;
    updateAreaControls();
    updateSettings();
}

void OptionsDialog::writeSettings()
{
    QSettings settings;
    
    // Write all boolean settings
    // AddressArea and AddressAreaWidth managed by hex editor drag (not written here)
    settings.setValue("AsciiArea", ui->cbAsciiArea->isChecked());
    if (m_cbBytesPerLine) {
        const int bplIdx = m_cbBytesPerLine->currentIndex();
        settings.setValue("Autosize", bplIdx == 0);
        settings.setValue("BytesPerLine",
            (bplIdx >= 1 && bplIdx <= kBplCount) ? kBplValues[bplIdx - 1] : 32);
    } else {
        settings.setValue("Autosize", true);
    }
    settings.setValue("ShowHexGrid", ui->cbShowHexGrid->isChecked());
    settings.setValue("ShowMultibyteFrame", ui->cbShowMultibyteFrame->isChecked());
    settings.setValue("AutoLoadRecentFile", ui->cbAutoLoadRecentFile->isChecked());
    settings.setValue("DetectEndianness", ui->cbDetectEndianness->isChecked());
    settings.setValue("DetectEncoding", ui->cbDetectEncoding->isChecked());
    settings.setValue("ResetTableOnClose", ui->cbResetTableOnClose->isChecked());
    settings.setValue("ResetEncodingOnClose", ui->cbResetEncodingOnClose->isChecked());
    settings.setValue("AutoFixChecksums", ui->cbAutoFixChecksums->isChecked());
    settings.setValue("DefaultEncoding", ui->cbDefaultEncoding->currentText());

    // Write all color settings
    settings.setValue("HighlightingColor", ui->lbHighlightingColor->palette().color(ui->lbHighlightingColor->backgroundRole()));
    settings.setValue("AddressAreaColor", ui->lbAddressAreaColor->palette().color(ui->lbAddressAreaColor->backgroundRole()));
    settings.setValue("PointedColor", ui->lbPointedColor->palette().color(ui->lbPointedColor->backgroundRole()));
    settings.setValue("PointedFontColor", ui->lbPointedFontColor->palette().color(ui->lbPointedFontColor->backgroundRole()));
    settings.setValue("PointerFontColor", ui->lbPointerFontColor->palette().color(ui->lbPointerFontColor->backgroundRole()));
    settings.setValue("PointerFrameColor", ui->lbPointerFrameColor->palette().color(ui->lbPointerFrameColor->backgroundRole()));
    settings.setValue("PointerFrameBgColor", ui->lbPointerFrameBgColor->palette().color(ui->lbPointerFrameBgColor->backgroundRole()));
    settings.setValue("SelectionColor", ui->lbSelectionColor->palette().color(ui->lbSelectionColor->backgroundRole()));
    settings.setValue("AddressFontColor", ui->lbAddressFontColor->palette().color(ui->lbAddressFontColor->backgroundRole()));
    settings.setValue("AddressZeroByteFontColor", ui->lbAddressZeroByteFontColor->palette().color(ui->lbAddressZeroByteFontColor->backgroundRole()));
    settings.setValue("AsciiAreaColor", ui->lbAsciiAreaColor->palette().color(ui->lbAsciiAreaColor->backgroundRole()));
    settings.setValue("AsciiFontColor", ui->lbAsciiFontColor->palette().color(ui->lbAsciiFontColor->backgroundRole()));
    settings.setValue("HexFontColor", ui->lbHexFontColor->palette().color(ui->lbHexFontColor->backgroundRole()));
    settings.setValue("HexAreaBackgroundColor", ui->lbHexAreaBackground->palette().color(ui->lbHexAreaBackground->backgroundRole()));
    settings.setValue("HexAreaGridColor", ui->lbHexAreaGrid->palette().color(ui->lbHexAreaGrid->backgroundRole()));
    settings.setValue("MultibyteFrameColor", ui->lbMultibyteFrameColor->palette().color(ui->lbMultibyteFrameColor->backgroundRole()));
    settings.setValue("CursorCharColor", ui->lbCursorCharColor->palette().color(ui->lbCursorCharColor->backgroundRole()));
    settings.setValue("CursorFrameColor", ui->lbCursorFrameColor->palette().color(ui->lbCursorFrameColor->backgroundRole()));
    settings.setValue("ZeroByteFontColor", ui->lbZeroByteFontColor->palette().color(ui->lbZeroByteFontColor->backgroundRole()));
    settings.setValue("ChangesColor", ui->lbChangesColor->palette().color(ui->lbChangesColor->backgroundRole()));
    settings.setValue("ScrollMapPtrBgColor", ui->lbScrollMapPtrBgColor->palette().color(ui->lbScrollMapPtrBgColor->backgroundRole()));
    settings.setValue("ScrollMapTargetBgColor", ui->lbScrollMapTargetBgColor->palette().color(ui->lbScrollMapTargetBgColor->backgroundRole()));
    if (m_lbSectionHeaderFontColor)
        settings.setValue("SectionHeaderFontColor", currentSwatchColor(m_lbSectionHeaderFontColor));
    if (m_lbSectionHeaderBgColor)
        settings.setValue("SectionHeaderBgColor", currentSwatchColor(m_lbSectionHeaderBgColor));
    
    // Write other settings
    settings.setValue("WidgetFont", ui->pbWidgetFont->font());
    if (m_pbSectionHeaderFont)
        settings.setValue("SectionHeaderFont", m_pbSectionHeaderFont->font());
    settings.setValue("NonPrintableNoTableChar", sanitizeSingleChar(ui->leNonPrintableNoTableChar->text(), kDefaultNonPrintableNoTableChar));
    settings.setValue("NotInTableChar", sanitizeSingleChar(ui->leNotInTableChar->text(), kDefaultNotInTableChar));
    // AddressAreaWidth and BytesPerLine written above from combos
    
    // Ensure settings are persisted to disk
    settings.sync();
}

void OptionsDialog::setColor(QWidget *widget, QColor color)
{
    QPalette palette = widget->palette();
    palette.setColor(widget->backgroundRole(), color);
    widget->setPalette(palette);
    widget->setAutoFillBackground(true);
}

void OptionsDialog::on_pbHighlightingColor_clicked()
{
    QColor color = QColorDialog::getColor(currentSwatchColor(ui->lbHighlightingColor), this);

    if (color.isValid())
    {
        setColor(ui->lbHighlightingColor, color);
        updateSettings();
    }
}

void OptionsDialog::on_pbAddressAreaColor_clicked()
{
    QColor color = QColorDialog::getColor(currentSwatchColor(ui->lbAddressAreaColor), this);

    if (color.isValid())
    {
        setColor(ui->lbAddressAreaColor, color);
        updateSettings();
    }
}

void OptionsDialog::on_pbAddressFontColor_clicked()
{
    QColor color = QColorDialog::getColor(currentSwatchColor(ui->lbAddressFontColor), this);

    if (color.isValid())
    {
        setColor(ui->lbAddressFontColor, color);
        updateSettings();
    }
}

void OptionsDialog::on_pbAddressZeroByteFontColor_clicked()
{
    QColor color = QColorDialog::getColor(currentSwatchColor(ui->lbAddressZeroByteFontColor), this);

    if (color.isValid())
    {
        setColor(ui->lbAddressZeroByteFontColor, color);
        updateSettings();
    }
}

void OptionsDialog::on_pbAsciiAreaColor_clicked()
{
    QColor color = QColorDialog::getColor(currentSwatchColor(ui->lbAsciiAreaColor), this);

    if (color.isValid())
    {
        setColor(ui->lbAsciiAreaColor, color);
        updateSettings();
    }
}

void OptionsDialog::on_pbAsciiFontColor_clicked()
{
    QColor color = QColorDialog::getColor(currentSwatchColor(ui->lbAsciiFontColor), this);

    if (color.isValid())
    {
        setColor(ui->lbAsciiFontColor, color);
        updateSettings();
    }
}

void OptionsDialog::on_pbHexFontColor_clicked()
{
    QColor color = QColorDialog::getColor(currentSwatchColor(ui->lbHexFontColor), this);

    if (color.isValid())
    {
        setColor(ui->lbHexFontColor, color);
        updateSettings();
    }
}

void OptionsDialog::on_pbSelectionColor_clicked()
{
    QColor color = QColorDialog::getColor(currentSwatchColor(ui->lbSelectionColor), this);

    if (color.isValid())
    {
        setColor(ui->lbSelectionColor, color);
        updateSettings();
    }
}

void OptionsDialog::on_pbWidgetFont_clicked()
{
    bool ok;

    QFont font = QFontDialog::getFont(&ok, ui->pbWidgetFont->font(), this);

    if (ok)
    {
        ui->pbWidgetFont->setFont(font);
        updateFontButtonText(font);
        applyPlaceholderFieldFont(ui->leNonPrintableNoTableChar, ui->leNotInTableChar, font);
        updateSettings();
    }

    QWidget::show();
}

void OptionsDialog::on_pbPointedColor_clicked()
{
    QColor color = QColorDialog::getColor(currentSwatchColor(ui->lbPointedColor), this);

    if (color.isValid())
    {
        setColor(ui->lbPointedColor, color);
        updateSettings();
    }
}

void OptionsDialog::on_pbPointedFontColor_clicked()
{
    QColor color = QColorDialog::getColor(currentSwatchColor(ui->lbPointedFontColor), this);

    if (color.isValid())
    {
        setColor(ui->lbPointedFontColor, color);
        updateSettings();
    }
}

void OptionsDialog::on_pbPointerFontColor_clicked()
{
    QColor color = QColorDialog::getColor(currentSwatchColor(ui->lbPointerFontColor), this);

    if (color.isValid())
    {
        setColor(ui->lbPointerFontColor, color);
        updateSettings();
    }
}

void OptionsDialog::on_pbPointerFrameColor_clicked()
{
    QColor color = QColorDialog::getColor(currentSwatchColor(ui->lbPointerFrameColor), this);

    if (color.isValid())
    {
        setColor(ui->lbPointerFrameColor, color);
        updateSettings();
    }
}

void OptionsDialog::on_pbPointerFrameBgColor_clicked()
{
    QColor color = QColorDialog::getColor(currentSwatchColor(ui->lbPointerFrameBgColor), this, QString(), QColorDialog::ShowAlphaChannel);

    if (color.isValid())
    {
        setColor(ui->lbPointerFrameBgColor, color);
        updateSettings();
    }
}

void OptionsDialog::on_pbHexAreaBackground_clicked()
{
    QColor color = QColorDialog::getColor(currentSwatchColor(ui->lbHexAreaBackground), this);

    if (color.isValid())
    {
        setColor(ui->lbHexAreaBackground, color);
        updateSettings();
    }
}

void OptionsDialog::on_pbHexAreaGrid_clicked()
{
    QColor color = QColorDialog::getColor(currentSwatchColor(ui->lbHexAreaGrid), this);

    if (color.isValid())
    {
        setColor(ui->lbHexAreaGrid, color);
        updateSettings();
    }
}

void OptionsDialog::on_pbMultibyteFrameColor_clicked()
{
    QColor color = QColorDialog::getColor(currentSwatchColor(ui->lbMultibyteFrameColor), this);

    if (color.isValid())
    {
        setColor(ui->lbMultibyteFrameColor, color);
        updateSettings();
    }
}

void OptionsDialog::on_pbZeroByteFontColor_clicked()
{
    QColor color = QColorDialog::getColor(currentSwatchColor(ui->lbZeroByteFontColor), this);

    if (color.isValid())
    {
        setColor(ui->lbZeroByteFontColor, color);
        updateSettings();
    }
}

void OptionsDialog::on_pbScrollMapPtrBgColor_clicked()
{
    QColor color = QColorDialog::getColor(currentSwatchColor(ui->lbScrollMapPtrBgColor), this);

    if (color.isValid())
    {
        setColor(ui->lbScrollMapPtrBgColor, color);
        updateSettings();
    }
}

void OptionsDialog::on_pbScrollMapTargetBgColor_clicked()
{
    QColor color = QColorDialog::getColor(currentSwatchColor(ui->lbScrollMapTargetBgColor), this);

    if (color.isValid())
    {
        setColor(ui->lbScrollMapTargetBgColor, color);
        updateSettings();
    }
}

void OptionsDialog::on_pbCursorCharColor_clicked()
{
    QColor color = QColorDialog::getColor(currentSwatchColor(ui->lbCursorCharColor), this, QString(), QColorDialog::ShowAlphaChannel);

    if (color.isValid())
    {
        setColor(ui->lbCursorCharColor, color);
        updateSettings();
    }
}

void OptionsDialog::on_pbCursorFrameColor_clicked()
{
    QColor color = QColorDialog::getColor(currentSwatchColor(ui->lbCursorFrameColor), this);

    if (color.isValid())
    {
        setColor(ui->lbCursorFrameColor, color);
        updateSettings();
    }
}

void OptionsDialog::on_pbChangesColor_clicked()
{
    QColor color = QColorDialog::getColor(currentSwatchColor(ui->lbChangesColor), this, QString(), QColorDialog::ShowAlphaChannel);

    if (color.isValid())
    {
        setColor(ui->lbChangesColor, color);
        updateSettings();
    }
}

void OptionsDialog::on_cbShowHexGrid_stateChanged(int)
{
    updateAreaControls();
    updateSettings();
}

void OptionsDialog::on_cbShowMultibyteFrame_stateChanged(int)
{
    updateAreaControls();
    updateSettings();
}

void OptionsDialog::on_cbAddressArea_stateChanged(int)
{
    updateAreaControls();
    updateSettings();
}

void OptionsDialog::on_cbAsciiArea_stateChanged(int)
{
    updateAreaControls();
    updateSettings();
}

void OptionsDialog::updateFontButtonText(const QFont &font)
{
    QString fontInfo = QStringLiteral("%1, %2pt").arg(font.family()).arg(font.pointSize());
    ui->pbWidgetFont->setText(fontInfo);
}

void OptionsDialog::updateAreaControls()
{
    // Enable/disable Address Area controls based on checkbox
    const bool addressEnabled = ui->cbAddressArea->isChecked();
    ui->pbAddressAreaColor->setEnabled(addressEnabled);
    ui->pbAddressFontColor->setEnabled(addressEnabled);
    ui->pbAddressZeroByteFontColor->setEnabled(addressEnabled);

    // Enable/disable ASCII Area controls based on checkbox
    bool asciiEnabled = ui->cbAsciiArea->isChecked();
    ui->pbAsciiAreaColor->setEnabled(asciiEnabled);
    ui->pbAsciiFontColor->setEnabled(asciiEnabled);

    // Enable/disable hex grid color based on checkbox
    bool gridEnabled = ui->cbShowHexGrid->isChecked();
    ui->pbHexAreaGrid->setEnabled(gridEnabled);
    ui->lbHexAreaGrid->setEnabled(gridEnabled);

    // Enable/disable multibyte frame color based on checkbox
    bool frameEnabled = ui->cbShowMultibyteFrame->isChecked();
    ui->pbMultibyteFrameColor->setEnabled(frameEnabled);
    ui->lbMultibyteFrameColor->setEnabled(frameEnabled);

    // Bytes per line label replaced by m_cbBytesPerLine combo
}

void OptionsDialog::on_pbDefault_clicked()
{
    if (ui->tabWidget->currentWidget() == ui->pageHotkeys)
        resetHotkeysToDefaults();
    else if (ui->tabWidget->currentWidget() == m_pageThemes) {
        EditorTheme light = EditorTheme::defaultLight();
        applyThemeToUi(light);
    } else
        resetToDefaults();
}

void OptionsDialog::resetToDefaults()
{
    m_suppressUpdate = true;
    ui->cbAddressArea->setChecked(true);
    // addressAreaWidth managed by hex editor drag
    ui->cbAsciiArea->setChecked(true);
    ui->cbShowHexGrid->setChecked(true);
    ui->cbShowMultibyteFrame->setChecked(true);
    if (m_cbBytesPerLine) m_cbBytesPerLine->setCurrentIndex(0); // Auto
    ui->cbAutoLoadRecentFile->setChecked(true);

    setColor(ui->lbHighlightingColor, QColor(0xff, 0xff, 0x99, 0xff));
    setColor(ui->lbAddressAreaColor, this->palette().alternateBase().color());
    setColor(ui->lbPointedColor, QColor(0xc0, 0x80, 0x00, 0xff));
    setColor(ui->lbPointedFontColor, QColor(Qt::black));
    setColor(ui->lbPointerFontColor, QColor(Qt::black));
    setColor(ui->lbPointerFrameColor, QColor(0x00, 0x00, 0xFF));
    setColor(ui->lbPointerFrameBgColor, QColor(0x00, 0xFF, 0x00, 0x80));
    setColor(ui->lbSelectionColor, this->palette().highlight().color());
    setColor(ui->lbAddressFontColor, this->palette().color(QPalette::WindowText));
    setColor(ui->lbAsciiAreaColor, this->palette().alternateBase().color());
    setColor(ui->lbAsciiFontColor, this->palette().color(QPalette::WindowText));
    setColor(ui->lbHexFontColor, this->palette().color(QPalette::WindowText));
    setColor(ui->lbHexAreaBackground, QColor(Qt::white));
    setColor(ui->lbHexAreaGrid, QColor(0x99, 0x99, 0x99));
    setColor(ui->lbMultibyteFrameColor, QColor(0x20, 0x20, 0x20));
    setColor(ui->lbCursorCharColor, QColor(0x00, 0x60, 0xFF, 0x80));
    setColor(ui->lbCursorFrameColor, QColor(Qt::black));
    setColor(ui->lbZeroByteFontColor, QColor(0xCC, 0xCC, 0xCC));
    setColor(ui->lbChangesColor, QColor(0x99, 0xff, 0x99, 0xff));
    setColor(ui->lbScrollMapPtrBgColor, QColor(0xd0, 0xd0, 0xd0));
    setColor(ui->lbScrollMapTargetBgColor, QColor(0xd0, 0xd0, 0xd0));
    if (m_lbSectionHeaderFontColor)
        setColor(m_lbSectionHeaderFontColor, this->palette().color(QPalette::WindowText));
    if (m_lbSectionHeaderBgColor)
        setColor(m_lbSectionHeaderBgColor, QColor(0xD8, 0xD8, 0xD8, 0x90));

#ifdef Q_OS_WIN32
    QFont defaultFont("Courier", 14);
#else
    QFont defaultFont("Courier New", 14);
#endif
    ui->pbWidgetFont->setFont(defaultFont);
    updateFontButtonText(defaultFont);
    if (m_pbSectionHeaderFont) {
        QFont sectionHeaderFont = defaultFont;
        sectionHeaderFont.setBold(true);
        m_pbSectionHeaderFont->setFont(sectionHeaderFont);
        m_pbSectionHeaderFont->setText(
            QStringLiteral("%1, %2pt").arg(sectionHeaderFont.family()).arg(sectionHeaderFont.pointSize()));
    }
    applyPlaceholderFieldFont(ui->leNonPrintableNoTableChar, ui->leNotInTableChar, defaultFont);
    ui->leNonPrintableNoTableChar->setText(QString(kDefaultNonPrintableNoTableChar));
    ui->leNotInTableChar->setText(QString(kDefaultNotInTableChar));
    ui->cbDetectEndianness->setChecked(true);
    ui->cbDetectEncoding->setChecked(true);
    ui->cbResetTableOnClose->setChecked(false);
    ui->cbResetEncodingOnClose->setChecked(false);
    ui->cbDefaultEncoding->setCurrentText(QStringLiteral("ASCII"));

    updateAreaControls();
    m_suppressUpdate = false;
    updateSettings();
}

void OptionsDialog::on_spinBoxValueChanged(int)
{
    updateSettings();
}

void OptionsDialog::on_checkBoxToggled(bool)
{
    updateSettings();
}

void OptionsDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
        retranslateHotkeys();
    }
    QDialog::changeEvent(event);
}

void OptionsDialog::initHotkeysTab()
{
    m_hotkeys = {
        {"New file",           "hotkey_New",            QKeySequence(Qt::CTRL | Qt::Key_T)},
        {"Open...",           "hotkey_Open",          QKeySequence(QKeySequence::Open)},
        {"Save",              "hotkey_Save",           QKeySequence(QKeySequence::Save)},
        {"Save As...",        "hotkey_SaveAs",         QKeySequence(QKeySequence::SaveAs)},
        {"Close",             "hotkey_Close",          QKeySequence(QKeySequence::Close)},
        {"Undo",              "hotkey_Undo",           QKeySequence(QKeySequence::Undo)},
        {"Redo",              "hotkey_Redo",           QKeySequence(QKeySequence::Redo)},
        {"Cut",               "hotkey_Cut",            QKeySequence(QKeySequence::Cut)},
        {"Copy",              "hotkey_Copy",           QKeySequence(QKeySequence::Copy)},
        {"Paste",             "hotkey_Paste",          QKeySequence(QKeySequence::Paste)},
        {"Find/Replace",      "hotkey_Find",           QKeySequence(QKeySequence::Find)},
        {"Jump to offset",    "hotkey_Goto",           QKeySequence(QKeySequence::FindNext)},   
        {"Find pointers",     "hotkey_FindPointers",   QKeySequence(QKeySequence::New)},
        {"Edit script",       "hotkey_EditScript",    QKeySequence(Qt::CTRL | Qt::Key_E)},
        {"Previous position", "hotkey_PrevPos",        QKeySequence(Qt::CTRL | Qt::Key_BracketLeft)},
        {"Next position",     "hotkey_NextPos",        QKeySequence(Qt::CTRL | Qt::Key_BracketRight)},
    };

    auto *scroll = new QScrollArea(ui->pageHotkeys);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *inner = new QWidget();
    auto *form  = new QFormLayout(inner);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);

    // Conflict notice label (hidden until a conflict occurs)
    m_conflictLabel = new QLabel();
    m_conflictLabel->setWordWrap(true);
    m_conflictLabel->setStyleSheet(QStringLiteral(
        "color: #b04000; background: #fff3cd; border: 1px solid #e0b000;"
        "border-radius: 4px; padding: 4px 8px;"));
    m_conflictLabel->setVisible(false);
    form->addRow(m_conflictLabel);

    QSettings settings;
    for (auto &e : m_hotkeys) {
        e.label  = new QLabel(QCoreApplication::translate("MainWindow", e.displayKey));
        e.editor = new QKeySequenceEdit(
            settings.value(e.settingsKey, e.defaultSeq).value<QKeySequence>());
        e.editor->setFixedWidth(73);

        // Capture by value to avoid dangling references to QList elements
        QString key    = e.settingsKey;
        QKeySequenceEdit *editor = e.editor;
        connect(editor, &QKeySequenceEdit::keySequenceChanged, this, [this, key, editor]() {
            const QKeySequence seq = editor->keySequence();
            if (!seq.isEmpty())
                resolveShortcutConflict(key, seq);
            QSettings s;
            s.setValue(key, seq);
            s.sync();
            auto *mw = qobject_cast<MainWindow*>(parent());
            if (mw) mw->applyShortcutsFromSettings();
        });

        form->addRow(e.label, e.editor);
    }

    scroll->setWidget(inner);
    auto *vl = new QVBoxLayout(ui->pageHotkeys);
    vl->setContentsMargins(8, 8, 8, 8);
    vl->addWidget(scroll);
}

void OptionsDialog::readHotkeySettings()
{
    QSettings settings;
    for (auto &e : m_hotkeys) {
        if (e.editor)
            e.editor->setKeySequence(
                settings.value(e.settingsKey, e.defaultSeq).value<QKeySequence>());
    }
}

void OptionsDialog::resetHotkeysToDefaults()
{
    QSettings s;
    for (auto &e : m_hotkeys) {
        if (e.editor) {
            e.editor->setKeySequence(e.defaultSeq);
            s.setValue(e.settingsKey, e.defaultSeq);
        }
    }
    s.sync();
    auto *mw = qobject_cast<MainWindow*>(parent());
    if (mw) mw->applyShortcutsFromSettings();
}

void OptionsDialog::retranslateHotkeys()
{
    for (auto &e : m_hotkeys) {
        if (e.label)
            e.label->setText(QCoreApplication::translate("MainWindow", e.displayKey));
    }
}

void OptionsDialog::resolveShortcutConflict(const QString &sourceKey, const QKeySequence &seq)
{
    for (auto &e : m_hotkeys) {
        if (e.settingsKey == sourceKey || !e.editor)
            continue;
        if (e.editor->keySequence() == seq) {
            // Show notice before clearing, so we can reference the label text
            const QString actionName = e.label
                ? e.label->text()
                : QString::fromUtf8(e.displayKey);
            if (m_conflictLabel) {
                m_conflictLabel->setText(
                    tr("Shortcut %1 was removed from \"%2\"")
                        .arg(seq.toString(QKeySequence::NativeText), actionName));
                m_conflictLabel->setVisible(true);
            }
            // Clear conflicting entry
            e.editor->blockSignals(true);
            e.editor->setKeySequence(QKeySequence());
            e.editor->blockSignals(false);
            QSettings s;
            s.setValue(e.settingsKey, QKeySequence());
            s.sync();
            return;
        }
    }
    // No conflict — hide the notice
    if (m_conflictLabel)
        m_conflictLabel->setVisible(false);
}

// ---------------------------------------------------------------------------
// Themes tab
// ---------------------------------------------------------------------------

void OptionsDialog::initThemesTab()
{
    // --- Restructure the Appearance tab ---
    // Extract pbWidgetFont and non-printable char fields from gbAuxiliary
    auto *auxGrid = qobject_cast<QGridLayout*>(ui->gbAuxiliary->layout());
    if (auxGrid) {
        auxGrid->removeWidget(ui->pbWidgetFont);
        auxGrid->removeWidget(ui->lbNonPrintableNoTableChar);
        auxGrid->removeWidget(ui->leNonPrintableNoTableChar);
        auxGrid->removeWidget(ui->lbNotInTableChar);
        auxGrid->removeWidget(ui->leNotInTableChar);
    }
    ui->gbAuxiliary->hide();

    // Delete the old Appearance tab layout
    if (auto *hl = ui->pageFontsColors->layout()) {
        QLayoutItem *child;
        while ((child = hl->takeAt(0)) != nullptr) {
            if (child->layout()) {
                QLayoutItem *inner;
                while ((inner = child->layout()->takeAt(0)) != nullptr) {
                    if (inner->widget() == nullptr)
                        delete inner;
                }
            }
            if (child->widget() == nullptr)
                delete child;
        }
        delete hl;
    }

    // Create address area combo: Hidden, 1 byte, 2 bytes … 8 bytes — removed (controlled by dragging)

    // Create bytes-per-line combo: Auto, 4, 8, 16 … 80
    m_cbBytesPerLine = new QComboBox();
    m_cbBytesPerLine->addItem(tr("Auto"));
    for (int v : kBplValues)
        m_cbBytesPerLine->addItem(QString::number(v));

    // Build "Common" QGroupBox with two-column FormLayout
    auto *commonGroup = new QGroupBox(tr("Common"));
    commonGroup->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
    auto *formLayout = new QFormLayout(commonGroup);
    formLayout->setContentsMargins(8, 8, 8, 8);
    formLayout->setSpacing(6);
    formLayout->addRow(tr("Bytes per line"), m_cbBytesPerLine);
    formLayout->addRow(ui->lbNonPrintableNoTableChar, ui->leNonPrintableNoTableChar);
    formLayout->addRow(ui->lbNotInTableChar, ui->leNotInTableChar);

    // Build Appearance tab layout
    auto *appLayout = new QVBoxLayout(ui->pageFontsColors);
    appLayout->setContentsMargins(8, 8, 8, 8);
    appLayout->setSpacing(0);
    appLayout->addWidget(commonGroup);
    appLayout->addStretch(1);

    // Connect combos for live preview
    connect(m_cbBytesPerLine, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updateSettings(); });

    // --- Create the Themes tab ---
    m_pageThemes = new QWidget();
    ui->tabWidget->insertTab(2, m_pageThemes, tr("Themes"));

    auto *mainLayout = new QHBoxLayout(m_pageThemes);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    // LEFT: Presets panel
    auto *presetsGroup = new QGroupBox(tr("Presets"));
    presetsGroup->setMaximumWidth(200);
    auto *presetsVL = new QVBoxLayout(presetsGroup);

    m_themeList = new QListWidget();
    presetsVL->addWidget(m_themeList);

    auto *btnApply = new QPushButton(tr("Apply"));
    presetsVL->addWidget(btnApply);

    auto *btnRow = new QHBoxLayout();
    auto *btnImport = new QPushButton(tr("Import") + QStringLiteral("..."));
    m_btnExport = new QPushButton(tr("Export") + QStringLiteral("..."));
    btnRow->addWidget(btnImport);
    btnRow->addWidget(m_btnExport);
    presetsVL->addLayout(btnRow);

    auto *btnRow2 = new QHBoxLayout();
    auto *btnSave = new QPushButton(tr("Save") + QStringLiteral("..."));
    m_btnDelete = new QPushButton(tr("Delete"));
    btnRow2->addWidget(btnSave);
    btnRow2->addWidget(m_btnDelete);
    presetsVL->addLayout(btnRow2);

    mainLayout->addWidget(presetsGroup);

    // RIGHT: scroll area with all color/font controls
    auto *scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto *scrollInner = new QWidget();
    auto *rightVL = new QVBoxLayout(scrollInner);
    rightVL->setContentsMargins(4, 4, 4, 4);
    rightVL->setSpacing(6);

    // Dark mode checkbox
    m_cbDarkMode = new QCheckBox(tr("Dark mode"));
    {
        QSettings s;
        m_cbDarkMode->setChecked(s.value(QStringLiteral("DarkTheme"), false).toBool());
    }
    rightVL->addWidget(m_cbDarkMode);

    // Font group
    auto *fontGroup = new QGroupBox(tr("Font"));
    fontGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto *fontHL = new QHBoxLayout(fontGroup);
    fontHL->addWidget(ui->pbWidgetFont);
    rightVL->addWidget(fontGroup);

    // Sections group
    auto *sectionsGroup = new QGroupBox(tr("Sections"));
    sectionsGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto *sectionsGrid = new QGridLayout(sectionsGroup);

    m_pbSectionHeaderFont = new QPushButton(tr("Header font"));
    m_pbSectionHeaderFontColor = new QPushButton(tr("Header text color"));
    m_pbSectionHeaderBgColor = new QPushButton(tr("Header background"));

    m_lbSectionHeaderFontColor = new QLabel();
    m_lbSectionHeaderBgColor = new QLabel();
    for (QLabel *swatch : {m_lbSectionHeaderFontColor, m_lbSectionHeaderBgColor}) {
        swatch->setMinimumSize(20, 20);
        swatch->setMaximumSize(20, 20);
        swatch->setFrameShape(QFrame::Panel);
        swatch->setFrameShadow(QFrame::Sunken);
        swatch->setAutoFillBackground(true);
    }

    sectionsGrid->addWidget(m_pbSectionHeaderFont, 0, 0, 1, 2);
    sectionsGrid->addWidget(m_pbSectionHeaderFontColor, 1, 0);
    sectionsGrid->addWidget(m_lbSectionHeaderFontColor, 1, 1);
    sectionsGrid->addWidget(m_pbSectionHeaderBgColor, 2, 0);
    sectionsGrid->addWidget(m_lbSectionHeaderBgColor, 2, 1);
    rightVL->addWidget(sectionsGroup);

    connect(m_pbSectionHeaderFont, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const QFont chosen = QFontDialog::getFont(&ok,
            m_pbSectionHeaderFont ? m_pbSectionHeaderFont->font() : ui->pbWidgetFont->font(), this);
        if (!ok)
            return;
        m_pbSectionHeaderFont->setFont(chosen);
        m_pbSectionHeaderFont->setText(
            QStringLiteral("%1, %2pt").arg(chosen.family()).arg(chosen.pointSize()));
        updateSettings();
    });

    connect(m_pbSectionHeaderFontColor, &QPushButton::clicked, this, [this]() {
        if (!m_lbSectionHeaderFontColor)
            return;
        const QColor c = QColorDialog::getColor(currentSwatchColor(m_lbSectionHeaderFontColor), this);
        if (!c.isValid())
            return;
        setColor(m_lbSectionHeaderFontColor, c);
        updateSettings();
    });

    connect(m_pbSectionHeaderBgColor, &QPushButton::clicked, this, [this]() {
        if (!m_lbSectionHeaderBgColor)
            return;
        const QColor c = QColorDialog::getColor(currentSwatchColor(m_lbSectionHeaderBgColor), this,
                                                QString(), QColorDialog::ShowAlphaChannel);
        if (!c.isValid())
            return;
        setColor(m_lbSectionHeaderBgColor, c);
        updateSettings();
    });

    // Reparent color groups from old Appearance tab
    rightVL->addWidget(ui->gbAddressArea);
    rightVL->addWidget(ui->gbHexArea);
    rightVL->addWidget(ui->gbAsciiArea);
    rightVL->addWidget(ui->gbColors);
    rightVL->addWidget(ui->gbPointers);
    rightVL->addWidget(ui->gbMaps);
    rightVL->addStretch(1);

    scrollArea->setWidget(scrollInner);
    mainLayout->addWidget(scrollArea, 1);

    // Connect preset buttons
    connect(btnApply, &QPushButton::clicked, this, &OptionsDialog::applySelectedTheme);
    connect(btnSave, &QPushButton::clicked, this, &OptionsDialog::saveCurrentAsTheme);
    connect(btnImport, &QPushButton::clicked, this, &OptionsDialog::importTheme);
    connect(m_btnExport, &QPushButton::clicked, this, &OptionsDialog::exportTheme);
    connect(m_btnDelete, &QPushButton::clicked, this, &OptionsDialog::deleteSelectedTheme);

    // Connect dark mode checkbox
    connect(m_cbDarkMode, &QCheckBox::toggled, this, [this](bool checked) {
        auto *mw = qobject_cast<MainWindow*>(parent());
        if (mw)
            mw->toggleDarkTheme(checked);
    });

    // Disable delete/export for built-in themes
    connect(m_themeList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *current, QListWidgetItem *) {
        const bool builtin = !current ||
            current->data(Qt::UserRole).toString().startsWith(QLatin1String("__builtin_"));
        if (m_btnDelete) m_btnDelete->setEnabled(!builtin);
        if (m_btnExport) m_btnExport->setEnabled(!builtin);
    });

    // Apply theme on double-click
    connect(m_themeList, &QListWidget::itemDoubleClicked, this,
            &OptionsDialog::applySelectedTheme);

    populateThemeList();
}

void OptionsDialog::populateThemeList()
{
    m_themeList->clear();

    // Built-in presets
    auto *lightItem = new QListWidgetItem(tr("Default Light"));
    lightItem->setData(Qt::UserRole, QStringLiteral("__builtin_light__"));
    m_themeList->addItem(lightItem);

    auto *darkItem = new QListWidgetItem(tr("Default Dark"));
    darkItem->setData(Qt::UserRole, QStringLiteral("__builtin_dark__"));
    m_themeList->addItem(darkItem);

    // User presets
    const QStringList userNames = EditorTheme::userPresetNames();
    for (const QString &name : userNames) {
        auto *item = new QListWidgetItem(name);
        item->setData(Qt::UserRole, name);
        m_themeList->addItem(item);
    }

    // Select the current theme
    QSettings settings;
    const QString currentThemeId = settings.value(QStringLiteral("CurrentTheme"), QStringLiteral("__builtin_light__")).toString();
    
    for (int i = 0; i < m_themeList->count(); ++i) {
        auto *item = m_themeList->item(i);
        if (item->data(Qt::UserRole).toString() == currentThemeId) {
            m_themeList->setCurrentItem(item);
            break;
        }
    }
    
    // Fallback to first item if current theme not found
    if (!m_themeList->currentItem() && m_themeList->count() > 0)
        m_themeList->setCurrentRow(0);
}

void OptionsDialog::applySelectedTheme()
{
    auto *item = m_themeList->currentItem();
    if (!item) return;

    const QString id = item->data(Qt::UserRole).toString();
    EditorTheme theme;
    if (id == QLatin1String("__builtin_light__"))
        theme = EditorTheme::defaultLight();
    else if (id == QLatin1String("__builtin_dark__"))
        theme = EditorTheme::defaultDark();
    else
        theme = EditorTheme::loadUserPreset(id);

    applyThemeToUi(theme);
    m_currentThemeId = id;
}

void OptionsDialog::saveCurrentAsTheme()
{
    static const QStringList kBuiltinNames = { tr("Default Light"), tr("Default Dark") };

    bool ok = false;
    QString name = QInputDialog::getText(this, tr("Save Theme"),
                                         tr("Theme name") + ":", QLineEdit::Normal,
                                         QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    name = name.trimmed();

    // Prevent overwriting built-in presets
    if (kBuiltinNames.contains(name, Qt::CaseSensitive)) {
        QMessageBox::warning(this, tr("Save Theme"),
            tr("\"%1\" is a built-in theme and cannot be overwritten.\nChoose a different name.").arg(name));
        return;
    }

    // Warn before overwriting an existing user preset
    const QStringList existing = EditorTheme::userPresetNames();
    if (existing.contains(name)) {
        auto btn = QMessageBox::question(this, tr("Save Theme"),
            tr("Theme \"%1\" already exists. Replace it?").arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (btn != QMessageBox::Yes) return;
    }

    EditorTheme theme = captureThemeFromUi();
    theme.name = name;
    EditorTheme::saveUserPreset(theme);
    populateThemeList();
}

void OptionsDialog::importTheme()
{
    static const QStringList kBuiltinNames = { tr("Default Light"), tr("Default Dark") };

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Theme"), QString(),
        tr("RTHextion Theme (*.rtheme);;JSON Files (*.json);;All Files (*)"));
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (doc.isNull()) return;

    EditorTheme theme = EditorTheme::fromJson(doc.object());

    // Block overwriting built-in preset names
    if (kBuiltinNames.contains(theme.name, Qt::CaseSensitive)) {
        QMessageBox::warning(this, tr("Import Theme"),
            tr("\"%1\" is a built-in theme and cannot be overwritten.\nRename the theme in the file and try again.").arg(theme.name));
        return;
    }

    // Warn before overwriting an existing user preset — then save and apply
    const QStringList existing = EditorTheme::userPresetNames();
    if (existing.contains(theme.name)) {
        auto btn = QMessageBox::question(this, tr("Import Theme"),
            tr("Theme \"%1\" already exists. Replace it?").arg(theme.name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (btn != QMessageBox::Yes) return;
    }

    EditorTheme::saveUserPreset(theme);
    populateThemeList();
    applyThemeToUi(theme);
}

void OptionsDialog::exportTheme()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Theme"), QString(),
        tr("RTHextion Theme (*.rtheme);;JSON Files (*.json)"));
    if (path.isEmpty()) return;

    EditorTheme theme = captureThemeFromUi();
    QJsonDocument doc(theme.toJson());

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(doc.toJson(QJsonDocument::Indented));
    f.close();
}

void OptionsDialog::deleteSelectedTheme()
{
    auto *item = m_themeList->currentItem();
    if (!item) return;

    const QString id = item->data(Qt::UserRole).toString();
    // Don't allow deleting built-in presets
    if (id.startsWith(QLatin1String("__builtin_"))) return;

    EditorTheme::deleteUserPreset(id);
    populateThemeList();
}

void OptionsDialog::applyThemeToUi(const EditorTheme &theme)
{
    m_suppressUpdate = true;

    // Dark mode
    if (m_cbDarkMode)
        m_cbDarkMode->setChecked(theme.darkMode);

    // Font
    ui->pbWidgetFont->setFont(theme.hexFont);
    updateFontButtonText(theme.hexFont);
    applyPlaceholderFieldFont(ui->leNonPrintableNoTableChar, ui->leNotInTableChar, theme.hexFont);

    // Booleans
    ui->cbShowHexGrid->setChecked(theme.showHexGrid);
    ui->cbShowMultibyteFrame->setChecked(theme.showMultibyteFrame);

    // Colors
    setColor(ui->lbHighlightingColor, theme.highlightingColor);
    setColor(ui->lbSelectionColor, theme.selectionColor);
    setColor(ui->lbChangesColor, theme.changesColor);
    setColor(ui->lbCursorCharColor, theme.cursorCharColor);
    setColor(ui->lbCursorFrameColor, theme.cursorFrameColor);
    setColor(ui->lbAddressAreaColor, theme.addressAreaColor);
    setColor(ui->lbAddressFontColor, theme.addressFontColor);
    setColor(ui->lbHexAreaBackground, theme.hexAreaBgColor);
    setColor(ui->lbHexFontColor, theme.hexFontColor);
    setColor(ui->lbZeroByteFontColor, theme.zeroByteFontColor);
    setColor(ui->lbHexAreaGrid, theme.hexAreaGridColor);
    setColor(ui->lbMultibyteFrameColor, theme.multibyteFrameColor);
    setColor(ui->lbAsciiAreaColor, theme.asciiAreaColor);
    setColor(ui->lbAsciiFontColor, theme.asciiFontColor);
    setColor(ui->lbPointedColor, theme.pointedColor);
    setColor(ui->lbPointedFontColor, theme.pointedFontColor);
    setColor(ui->lbPointerFontColor, theme.pointerFontColor);
    setColor(ui->lbPointerFrameColor, theme.pointerFrameColor);
    setColor(ui->lbPointerFrameBgColor, theme.pointerFrameBgColor);
    setColor(ui->lbScrollMapPtrBgColor, theme.scrollMapPtrBgColor);
    setColor(ui->lbScrollMapTargetBgColor, theme.scrollMapTargetBgColor);
    if (m_lbSectionHeaderFontColor)
        setColor(m_lbSectionHeaderFontColor, theme.sectionHeaderFontColor);
    if (m_lbSectionHeaderBgColor)
        setColor(m_lbSectionHeaderBgColor, theme.sectionHeaderBgColor);
    if (m_pbSectionHeaderFont) {
        m_pbSectionHeaderFont->setFont(theme.sectionHeaderFont);
        m_pbSectionHeaderFont->setText(
            QStringLiteral("%1, %2pt").arg(theme.sectionHeaderFont.family()).arg(theme.sectionHeaderFont.pointSize()));
    }

    updateAreaControls();
    m_suppressUpdate = false;
    updateSettings();
}

EditorTheme OptionsDialog::captureThemeFromUi() const
{
    EditorTheme t;
    t.name = QStringLiteral("Current");
    t.darkMode = m_cbDarkMode ? m_cbDarkMode->isChecked() : false;
    t.hexFont = ui->pbWidgetFont->font();
    t.highlighting = true;
    t.showHexGrid = ui->cbShowHexGrid->isChecked();
    t.showMultibyteFrame = ui->cbShowMultibyteFrame->isChecked();

    t.highlightingColor = currentSwatchColor(ui->lbHighlightingColor);
    t.selectionColor = currentSwatchColor(ui->lbSelectionColor);
    t.changesColor = currentSwatchColor(ui->lbChangesColor);
    t.cursorCharColor = currentSwatchColor(ui->lbCursorCharColor);
    t.cursorFrameColor = currentSwatchColor(ui->lbCursorFrameColor);
    t.addressAreaColor = currentSwatchColor(ui->lbAddressAreaColor);
    t.addressFontColor = currentSwatchColor(ui->lbAddressFontColor);
    t.hexAreaBgColor = currentSwatchColor(ui->lbHexAreaBackground);
    t.hexFontColor = currentSwatchColor(ui->lbHexFontColor);
    t.zeroByteFontColor = currentSwatchColor(ui->lbZeroByteFontColor);
    t.hexAreaGridColor = currentSwatchColor(ui->lbHexAreaGrid);
    t.multibyteFrameColor = currentSwatchColor(ui->lbMultibyteFrameColor);
    t.asciiAreaColor = currentSwatchColor(ui->lbAsciiAreaColor);
    t.asciiFontColor = currentSwatchColor(ui->lbAsciiFontColor);
    t.pointedColor = currentSwatchColor(ui->lbPointedColor);
    t.pointedFontColor = currentSwatchColor(ui->lbPointedFontColor);
    t.pointerFontColor = currentSwatchColor(ui->lbPointerFontColor);
    t.pointerFrameColor = currentSwatchColor(ui->lbPointerFrameColor);
    t.pointerFrameBgColor = currentSwatchColor(ui->lbPointerFrameBgColor);
    t.scrollMapPtrBgColor = currentSwatchColor(ui->lbScrollMapPtrBgColor);
    t.scrollMapTargetBgColor = currentSwatchColor(ui->lbScrollMapTargetBgColor);
    t.sectionHeaderFontColor = m_lbSectionHeaderFontColor ? currentSwatchColor(m_lbSectionHeaderFontColor) : QColor(Qt::black);
    t.sectionHeaderBgColor = m_lbSectionHeaderBgColor ? currentSwatchColor(m_lbSectionHeaderBgColor) : QColor(0xD8, 0xD8, 0xD8, 0x90);
    t.sectionHeaderFont = m_pbSectionHeaderFont ? m_pbSectionHeaderFont->font() : t.hexFont;
    return t;
}

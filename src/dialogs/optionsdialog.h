#ifndef OPTIONSDIALOG_H
#define OPTIONSDIALOG_H

#include <QtCore>
#include <QDialog>
#include <QKeySequence>
#include <QList>
#include <QMap>

class QLabel;
class QKeySequenceEdit;
class QListWidget;
class QPushButton;
class QCheckBox;
class QComboBox;
class QSpinBox;

namespace Ui {
    class OptionsDialog;
}

class OptionsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OptionsDialog(QWidget *parent = 0);
    ~OptionsDialog();
    Ui::OptionsDialog *ui;
    void show();

protected:
    void changeEvent(QEvent *event) override;

public slots:
    void accept() override;
    void reject() override;

private slots:
    void on_pbHighlightingColor_clicked();
    void on_pbAddressAreaColor_clicked();
    void on_pbAddressFontColor_clicked();
    void on_pbAddressZeroByteFontColor_clicked();
    void on_pbAsciiAreaColor_clicked();
    void on_pbAsciiFontColor_clicked();
    void on_pbHexFontColor_clicked();
    void on_pbSelectionColor_clicked();
    void on_pbWidgetFont_clicked();
    void on_pbPointedColor_clicked();
    void on_pbPointedFontColor_clicked();
    void on_pbPointerFontColor_clicked();
    void on_pbPointerFrameColor_clicked();
    void on_pbPointerFrameBgColor_clicked();
    void on_pbCursorCharColor_clicked();
    void on_pbCursorFrameColor_clicked();
    void on_pbChangesColor_clicked();
    void on_pbHexAreaBackground_clicked();
    void on_pbHexAreaGrid_clicked();
    void on_cbShowMultibyteFrame_stateChanged(int);
    void on_pbMultibyteFrameColor_clicked();
    void on_pbZeroByteFontColor_clicked();
    void on_pbScrollMapPtrBgColor_clicked();
    void on_pbScrollMapTargetBgColor_clicked();
    void on_cbShowHexGrid_stateChanged(int);
    void on_cbAddressArea_stateChanged(int);
    void on_cbAsciiArea_stateChanged(int);
    void on_pbDefault_clicked();
    void on_spinBoxValueChanged(int);
    void on_checkBoxToggled(bool);

private:
    void updateSettings();
    void readSettings();
    void writeSettings();
    void setColor(QWidget *widget, QColor color);
    void updateFontButtonText(const QFont &font);
    void updateAreaControls();
    void applySettings();
    void saveCurrentSettings();
    void restoreSettings();
    void resetToDefaults();

    // Hotkeys tab
    struct HotkeyEntry {
        const char *displayKey;   // English string (same as used in tr() in MainWindow)
        QString     settingsKey;  // QSettings key, e.g. "hotkey_Open"
        QKeySequence defaultSeq;
        QLabel          *label  = nullptr;
        QKeySequenceEdit *editor = nullptr;
    };
    void initHotkeysTab();
    void readHotkeySettings();
    void resetHotkeysToDefaults();
    void retranslateHotkeys();
    void resolveShortcutConflict(const QString &sourceKey, const QKeySequence &seq);

    // Themes tab
    void initThemesTab();
    void populateThemeList();
    void applySelectedTheme();
    void saveCurrentAsTheme();
    void importTheme();
    void exportTheme();
    void deleteSelectedTheme();
    void applyThemeToUi(const struct EditorTheme &theme);
    struct EditorTheme captureThemeFromUi() const;

    QWidget     *m_pageThemes = nullptr;
    QListWidget *m_themeList = nullptr;
    QCheckBox   *m_cbDarkMode = nullptr;
    QComboBox   *m_cbBytesPerLine = nullptr;
    QSpinBox    *m_sbScrollMapWidth = nullptr;
    QPushButton *m_btnDelete = nullptr;
    QPushButton *m_btnExport = nullptr;
    QPushButton *m_pbSectionHeaderFont = nullptr;
    QPushButton *m_pbSectionHeaderFontColor = nullptr;
    QPushButton *m_pbSectionHeaderBgColor = nullptr;
    QLabel      *m_lbSectionHeaderFontColor = nullptr;
    QLabel      *m_lbSectionHeaderBgColor = nullptr;

    QList<HotkeyEntry>          m_hotkeys;
    QMap<QString, QKeySequence> m_originalHotkeys;
    QLabel                     *m_conflictLabel = nullptr;
    QString                      m_originalThemeId;
    QString                      m_currentThemeId;

    struct SettingsSnapshot
    {
        bool addressArea;
        int addressAreaWidth;
        bool asciiArea;
        bool hexGridShow;
        bool autosize;
        bool autoLoadRecentFile;
        int bytesPerLine;
        QColor highlightingColor;
        QColor addressAreaColor;
        QColor addressFontColor;
        QColor addressZeroByteFontColor;
        QColor asciiAreaColor;
        QColor asciiFontColor;
        QColor pointedColor;
        QColor pointedFontColor;
        QColor pointerFontColor;
        QColor pointerFrameColor;
        QColor pointerFrameBgColor;
        QColor selectionColor;
        QColor hexFontColor;
        QColor hexAreaBgColor;
        QColor hexAreaGridColor;
        bool showMultibyteFrame;
        QColor multibyteFrameColor;
        QColor cursorCharColor;
        QColor cursorFrameColor;
        QColor zeroByteFontColor;
        QColor changesColor;
        QColor scrollMapPtrBgColor;
        QColor scrollMapTargetBgColor;
        int scrollMapWidth = 12;
        QColor sectionHeaderFontColor;
        QColor sectionHeaderBgColor;
        QFont widgetFont;
        QFont sectionHeaderFont;
        QString nonPrintableNoTableChar;
        QString notInTableChar;
        bool detectEndianness = true;
        bool detectEncoding = true;
        bool resetTableOnClose = false;
        bool resetEncodingOnClose = false;
        bool autoFixChecksums = false;
        bool darkMode = false;
        QString defaultEncoding = QStringLiteral("ASCII");
    };
    SettingsSnapshot m_originalSettings, m_currentSettings;
    bool m_suppressUpdate = false;
};

#endif // OPTIONSDIALOG_H

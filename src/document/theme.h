#ifndef EDITORTHEME_H
#define EDITORTHEME_H

#include <QColor>
#include <QFont>
#include <QJsonObject>
#include <QString>
#include <QVector>

struct EditorTheme
{
    QString name;
    bool darkMode = false;
    QFont hexFont;

    // Highlighting
    bool highlighting = true;
    QColor highlightingColor;
    QColor selectionColor;
    QColor changesColor;
    QColor cursorCharColor;
    QColor cursorFrameColor;

    // Address area
    QColor addressAreaColor;
    QColor addressFontColor;
    QColor addressZeroByteFontColor;

    // Hex area
    QColor hexAreaBgColor;
    QColor hexFontColor;
    QColor zeroByteFontColor;
    bool showHexGrid = true;
    QColor hexAreaGridColor;
    bool showMultibyteFrame = true;
    QColor multibyteFrameColor;

    // ASCII area
    QColor asciiAreaColor;
    QColor asciiFontColor;

    // Pointers
    QColor pointedColor;
    QColor pointedFontColor;
    QColor pointerFontColor;
    QColor pointerFrameColor;
    QColor pointerFrameBgColor;

    // Maps
    QColor scrollMapPtrBgColor;
    QColor scrollMapTargetBgColor;

    // Serialization
    QJsonObject toJson() const;
    static EditorTheme fromJson(const QJsonObject &obj);

    // Apply this theme to QSettings (does NOT call sync)
    void applyToSettings() const;

    // Capture current theme from QSettings
    static EditorTheme fromCurrentSettings();

    // Built-in presets
    static EditorTheme defaultLight();
    static EditorTheme defaultDark();

    // User preset management (stored in QSettings)
    static QStringList userPresetNames();
    static void saveUserPreset(const EditorTheme &theme);
    static void deleteUserPreset(const QString &name);
    static EditorTheme loadUserPreset(const QString &name);
};

#endif // EDITORTHEME_H

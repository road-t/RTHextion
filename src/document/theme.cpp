#include "theme.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QPalette>
#include "appsettings.h"
#include <QGuiApplication>

namespace {

QJsonValue colorToJson(const QColor &c)
{
    return QString::fromLatin1("#%1").arg(c.rgba(), 8, 16, QLatin1Char('0'));
}

QColor colorFromJson(const QJsonValue &v, const QColor &fallback)
{
    if (!v.isString())
        return fallback;
    const QString s = v.toString();
    if (s.startsWith(QLatin1Char('#')) && s.length() == 9) {
        bool ok;
        uint rgba = s.mid(1).toUInt(&ok, 16);
        if (ok)
            return QColor::fromRgba(rgba);
    }
    QColor c(s);
    return c.isValid() ? c : fallback;
}

QJsonValue fontToJson(const QFont &f)
{
    return f.toString();
}

QFont fontFromJson(const QJsonValue &v, const QFont &fallback)
{
    if (!v.isString())
        return fallback;
    QFont f;
    if (f.fromString(v.toString()))
        return f;
    return fallback;
}

#ifdef Q_OS_WIN32
QFont defaultEditorFont() { return QFont(QStringLiteral("Courier"), 14); }
#else
QFont defaultEditorFont() { return QFont(QStringLiteral("Courier New"), 14); }
#endif

} // namespace

// ---------------------------------------------------------------------------
// Built-in presets
// ---------------------------------------------------------------------------

EditorTheme EditorTheme::defaultLight()
{
    EditorTheme t;
    t.name = QStringLiteral("Default Light");
    t.darkMode = false;
    t.hexFont = defaultEditorFont();
    t.highlighting = true;
    t.highlightingColor = QColor(0xff, 0xff, 0x99);
    t.selectionColor = QColor(42, 130, 218);
    t.changesColor = QColor(0x99, 0xff, 0x99);
    t.cursorCharColor = QColor(0x00, 0x60, 0xFF, 0x80);
    t.cursorFrameColor = QColor(Qt::black);
    t.addressAreaColor = QColor(240, 240, 240);
    t.addressFontColor = QColor(Qt::black);
    t.addressZeroByteFontColor = QColor(Qt::black);
    t.hexAreaBgColor = QColor(Qt::white);
    t.hexFontColor = QColor(Qt::black);
    t.zeroByteFontColor = QColor(0xCC, 0xCC, 0xCC);
    t.showHexGrid = true;
    t.hexAreaGridColor = QColor(0x99, 0x99, 0x99);
    t.showMultibyteFrame = true;
    t.multibyteFrameColor = QColor(0x20, 0x20, 0x20);
    t.asciiAreaColor = QColor(240, 240, 240);
    t.asciiFontColor = QColor(Qt::black);
    t.pointedColor = QColor(0xc0, 0x80, 0x00);
    t.pointedFontColor = QColor(Qt::black);
    t.pointerFontColor = QColor(Qt::black);
    t.pointerFrameColor = QColor(0x00, 0x00, 0xFF);
    t.pointerFrameBgColor = QColor(0x00, 0xFF, 0x00, 0x80);
    t.sectionHeaderFontColor = QColor(Qt::black);
    t.sectionHeaderBgColor = QColor(0xD8, 0xD8, 0xD8, 0x90);
    t.sectionHeaderFont = t.hexFont;
    t.sectionHeaderFont.setBold(true);
    t.scrollMapPtrBgColor = QColor(0xd0, 0xd0, 0xd0);
    t.scrollMapTargetBgColor = QColor(0xd0, 0xd0, 0xd0);
    return t;
}

EditorTheme EditorTheme::defaultDark()
{
    EditorTheme t;
    t.name = QStringLiteral("Default Dark");
    t.darkMode = true;
    t.hexFont = defaultEditorFont();
    t.highlighting = true;
    t.highlightingColor = QColor(0x66, 0x66, 0x00);
    t.selectionColor = QColor(42, 130, 218);
    t.changesColor = QColor(0x00, 0x66, 0x00, 0xCC);
    t.cursorCharColor = QColor(0x40, 0x80, 0xFF, 0x80);
    t.cursorFrameColor = QColor(0xAA, 0xAA, 0xAA);
    t.addressAreaColor = QColor(45, 45, 45);
    t.addressFontColor = QColor(0xCC, 0xCC, 0xCC);
    t.addressZeroByteFontColor = QColor(0x55, 0x55, 0x55);
    t.hexAreaBgColor = QColor(35, 35, 35);
    t.hexFontColor = QColor(0xDD, 0xDD, 0xDD);
    t.zeroByteFontColor = QColor(0x55, 0x55, 0x55);
    t.showHexGrid = true;
    t.hexAreaGridColor = QColor(0x55, 0x55, 0x55);
    t.showMultibyteFrame = true;
    t.multibyteFrameColor = QColor(0x88, 0x88, 0x88);
    t.asciiAreaColor = QColor(45, 45, 45);
    t.asciiFontColor = QColor(0xCC, 0xCC, 0xCC);
    t.pointedColor = QColor(0xA0, 0x60, 0x00);
    t.pointedFontColor = QColor(0xEE, 0xEE, 0xEE);
    t.pointerFontColor = QColor(0xEE, 0xEE, 0xEE);
    t.pointerFrameColor = QColor(0x50, 0x80, 0xFF);
    t.pointerFrameBgColor = QColor(0x00, 0x80, 0x00, 0x60);
    t.sectionHeaderFontColor = QColor(0xDD, 0xDD, 0xDD);
    t.sectionHeaderBgColor = QColor(0x66, 0x66, 0x66, 0xA0);
    t.sectionHeaderFont = t.hexFont;
    t.sectionHeaderFont.setBold(true);
    t.scrollMapPtrBgColor = QColor(0x40, 0x40, 0x40);
    t.scrollMapTargetBgColor = QColor(0x40, 0x40, 0x40);
    return t;
}

// ---------------------------------------------------------------------------
// JSON serialization
// ---------------------------------------------------------------------------

QJsonObject EditorTheme::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("darkMode")] = darkMode;
    o[QStringLiteral("hexFont")] = fontToJson(hexFont);
    o[QStringLiteral("highlighting")] = highlighting;
    o[QStringLiteral("showHexGrid")] = showHexGrid;
    o[QStringLiteral("showMultibyteFrame")] = showMultibyteFrame;

    QJsonObject c;
    c[QStringLiteral("highlightingColor")] = colorToJson(highlightingColor);
    c[QStringLiteral("selectionColor")] = colorToJson(selectionColor);
    c[QStringLiteral("changesColor")] = colorToJson(changesColor);
    c[QStringLiteral("cursorCharColor")] = colorToJson(cursorCharColor);
    c[QStringLiteral("cursorFrameColor")] = colorToJson(cursorFrameColor);
    c[QStringLiteral("addressAreaColor")] = colorToJson(addressAreaColor);
    c[QStringLiteral("addressFontColor")] = colorToJson(addressFontColor);
    c[QStringLiteral("addressZeroByteFontColor")] = colorToJson(addressZeroByteFontColor);
    c[QStringLiteral("hexAreaBgColor")] = colorToJson(hexAreaBgColor);
    c[QStringLiteral("hexFontColor")] = colorToJson(hexFontColor);
    c[QStringLiteral("zeroByteFontColor")] = colorToJson(zeroByteFontColor);
    c[QStringLiteral("hexAreaGridColor")] = colorToJson(hexAreaGridColor);
    c[QStringLiteral("multibyteFrameColor")] = colorToJson(multibyteFrameColor);
    c[QStringLiteral("asciiAreaColor")] = colorToJson(asciiAreaColor);
    c[QStringLiteral("asciiFontColor")] = colorToJson(asciiFontColor);
    c[QStringLiteral("pointedColor")] = colorToJson(pointedColor);
    c[QStringLiteral("pointedFontColor")] = colorToJson(pointedFontColor);
    c[QStringLiteral("pointerFontColor")] = colorToJson(pointerFontColor);
    c[QStringLiteral("pointerFrameColor")] = colorToJson(pointerFrameColor);
    c[QStringLiteral("pointerFrameBgColor")] = colorToJson(pointerFrameBgColor);
    c[QStringLiteral("sectionHeaderFontColor")] = colorToJson(sectionHeaderFontColor);
    c[QStringLiteral("sectionHeaderBgColor")] = colorToJson(sectionHeaderBgColor);
    c[QStringLiteral("scrollMapPtrBgColor")] = colorToJson(scrollMapPtrBgColor);
    c[QStringLiteral("scrollMapTargetBgColor")] = colorToJson(scrollMapTargetBgColor);
    o[QStringLiteral("sectionHeaderFont")] = fontToJson(sectionHeaderFont);
    o[QStringLiteral("colors")] = c;
    return o;
}

EditorTheme EditorTheme::fromJson(const QJsonObject &o)
{
    EditorTheme def = defaultLight();
    EditorTheme t;
    t.name = o.value(QStringLiteral("name")).toString(QStringLiteral("Untitled"));
    t.darkMode = o.value(QStringLiteral("darkMode")).toBool(false);
    t.hexFont = fontFromJson(o.value(QStringLiteral("hexFont")), def.hexFont);
    t.highlighting = o.value(QStringLiteral("highlighting")).toBool(true);
    t.showHexGrid = o.value(QStringLiteral("showHexGrid")).toBool(true);
    t.showMultibyteFrame = o.value(QStringLiteral("showMultibyteFrame")).toBool(true);

    QJsonObject c = o.value(QStringLiteral("colors")).toObject();
    t.highlightingColor = colorFromJson(c.value(QStringLiteral("highlightingColor")), def.highlightingColor);
    t.selectionColor = colorFromJson(c.value(QStringLiteral("selectionColor")), def.selectionColor);
    t.changesColor = colorFromJson(c.value(QStringLiteral("changesColor")), def.changesColor);
    t.cursorCharColor = colorFromJson(c.value(QStringLiteral("cursorCharColor")), def.cursorCharColor);
    t.cursorFrameColor = colorFromJson(c.value(QStringLiteral("cursorFrameColor")), def.cursorFrameColor);
    t.addressAreaColor = colorFromJson(c.value(QStringLiteral("addressAreaColor")), def.addressAreaColor);
    t.addressFontColor = colorFromJson(c.value(QStringLiteral("addressFontColor")), def.addressFontColor);
    t.addressZeroByteFontColor = colorFromJson(c.value(QStringLiteral("addressZeroByteFontColor")), def.addressZeroByteFontColor);
    t.hexAreaBgColor = colorFromJson(c.value(QStringLiteral("hexAreaBgColor")), def.hexAreaBgColor);
    t.hexFontColor = colorFromJson(c.value(QStringLiteral("hexFontColor")), def.hexFontColor);
    t.zeroByteFontColor = colorFromJson(c.value(QStringLiteral("zeroByteFontColor")), def.zeroByteFontColor);
    t.hexAreaGridColor = colorFromJson(c.value(QStringLiteral("hexAreaGridColor")), def.hexAreaGridColor);
    t.multibyteFrameColor = colorFromJson(c.value(QStringLiteral("multibyteFrameColor")), def.multibyteFrameColor);
    t.asciiAreaColor = colorFromJson(c.value(QStringLiteral("asciiAreaColor")), def.asciiAreaColor);
    t.asciiFontColor = colorFromJson(c.value(QStringLiteral("asciiFontColor")), def.asciiFontColor);
    t.pointedColor = colorFromJson(c.value(QStringLiteral("pointedColor")), def.pointedColor);
    t.pointedFontColor = colorFromJson(c.value(QStringLiteral("pointedFontColor")), def.pointedFontColor);
    t.pointerFontColor = colorFromJson(c.value(QStringLiteral("pointerFontColor")), def.pointerFontColor);
    t.pointerFrameColor = colorFromJson(c.value(QStringLiteral("pointerFrameColor")), def.pointerFrameColor);
    t.pointerFrameBgColor = colorFromJson(c.value(QStringLiteral("pointerFrameBgColor")), def.pointerFrameBgColor);
    t.sectionHeaderFontColor = colorFromJson(c.value(QStringLiteral("sectionHeaderFontColor")), def.sectionHeaderFontColor);
    t.sectionHeaderBgColor = colorFromJson(c.value(QStringLiteral("sectionHeaderBgColor")), def.sectionHeaderBgColor);
    t.scrollMapPtrBgColor = colorFromJson(c.value(QStringLiteral("scrollMapPtrBgColor")), def.scrollMapPtrBgColor);
    t.scrollMapTargetBgColor = colorFromJson(c.value(QStringLiteral("scrollMapTargetBgColor")), def.scrollMapTargetBgColor);
    t.sectionHeaderFont = fontFromJson(o.value(QStringLiteral("sectionHeaderFont")), def.sectionHeaderFont);
    return t;
}

// ---------------------------------------------------------------------------
// QSettings integration
// ---------------------------------------------------------------------------

void EditorTheme::applyToSettings() const
{
    auto &s = AppSettings::instance();
    s.setValue(QStringLiteral("DarkTheme"), darkMode);
    s.setValue(QStringLiteral("WidgetFont"), hexFont);
    s.setValue(QStringLiteral("Highlighting"), true);
    s.setValue(QStringLiteral("ShowHexGrid"), showHexGrid);
    s.setValue(QStringLiteral("ShowMultibyteFrame"), showMultibyteFrame);

    s.setValue(QStringLiteral("HighlightingColor"), highlightingColor);
    s.setValue(QStringLiteral("SelectionColor"), selectionColor);
    s.setValue(QStringLiteral("ChangesColor"), changesColor);
    s.setValue(QStringLiteral("CursorCharColor"), cursorCharColor);
    s.setValue(QStringLiteral("CursorFrameColor"), cursorFrameColor);
    s.setValue(QStringLiteral("AddressAreaColor"), addressAreaColor);
    s.setValue(QStringLiteral("AddressFontColor"), addressFontColor);
    s.setValue(QStringLiteral("AddressZeroByteFontColor"), addressZeroByteFontColor);
    s.setValue(QStringLiteral("HexAreaBackgroundColor"), hexAreaBgColor);
    s.setValue(QStringLiteral("HexFontColor"), hexFontColor);
    s.setValue(QStringLiteral("ZeroByteFontColor"), zeroByteFontColor);
    s.setValue(QStringLiteral("HexAreaGridColor"), hexAreaGridColor);
    s.setValue(QStringLiteral("MultibyteFrameColor"), multibyteFrameColor);
    s.setValue(QStringLiteral("AsciiAreaColor"), asciiAreaColor);
    s.setValue(QStringLiteral("AsciiFontColor"), asciiFontColor);
    s.setValue(QStringLiteral("PointedColor"), pointedColor);
    s.setValue(QStringLiteral("PointedFontColor"), pointedFontColor);
    s.setValue(QStringLiteral("PointerFontColor"), pointerFontColor);
    s.setValue(QStringLiteral("PointerFrameColor"), pointerFrameColor);
    s.setValue(QStringLiteral("PointerFrameBgColor"), pointerFrameBgColor);
    s.setValue(QStringLiteral("SectionHeaderFontColor"), sectionHeaderFontColor);
    s.setValue(QStringLiteral("SectionHeaderBgColor"), sectionHeaderBgColor);
    s.setValue(QStringLiteral("SectionHeaderFont"), sectionHeaderFont);
    s.setValue(QStringLiteral("ScrollMapPtrBgColor"), scrollMapPtrBgColor);
    s.setValue(QStringLiteral("ScrollMapTargetBgColor"), scrollMapTargetBgColor);
}

EditorTheme EditorTheme::fromCurrentSettings()
{
    EditorTheme def = defaultLight();
    EditorTheme t;
    auto &s = AppSettings::instance();
    t.name = QStringLiteral("Current");
    t.darkMode = s.value(QStringLiteral("DarkTheme"), false).toBool();
    t.hexFont = s.value(QStringLiteral("WidgetFont"), def.hexFont).value<QFont>();
    t.highlighting = s.value(QStringLiteral("Highlighting"), true).toBool();
    t.showHexGrid = s.value(QStringLiteral("ShowHexGrid"), true).toBool();
    t.showMultibyteFrame = s.value(QStringLiteral("ShowMultibyteFrame"), true).toBool();

    t.highlightingColor = s.value(QStringLiteral("HighlightingColor"), def.highlightingColor).value<QColor>();
    t.selectionColor = s.value(QStringLiteral("SelectionColor"), def.selectionColor).value<QColor>();
    t.changesColor = s.value(QStringLiteral("ChangesColor"), def.changesColor).value<QColor>();
    t.cursorCharColor = s.value(QStringLiteral("CursorCharColor"), def.cursorCharColor).value<QColor>();
    t.cursorFrameColor = s.value(QStringLiteral("CursorFrameColor"), def.cursorFrameColor).value<QColor>();
    t.addressAreaColor = s.value(QStringLiteral("AddressAreaColor"), def.addressAreaColor).value<QColor>();
    t.addressFontColor = s.value(QStringLiteral("AddressFontColor"), def.addressFontColor).value<QColor>();
    t.addressZeroByteFontColor = s.value(QStringLiteral("AddressZeroByteFontColor"), def.addressZeroByteFontColor).value<QColor>();
    t.hexAreaBgColor = s.value(QStringLiteral("HexAreaBackgroundColor"), def.hexAreaBgColor).value<QColor>();
    t.hexFontColor = s.value(QStringLiteral("HexFontColor"), def.hexFontColor).value<QColor>();
    t.zeroByteFontColor = s.value(QStringLiteral("ZeroByteFontColor"), def.zeroByteFontColor).value<QColor>();
    t.hexAreaGridColor = s.value(QStringLiteral("HexAreaGridColor"), def.hexAreaGridColor).value<QColor>();
    t.multibyteFrameColor = s.value(QStringLiteral("MultibyteFrameColor"), def.multibyteFrameColor).value<QColor>();
    t.asciiAreaColor = s.value(QStringLiteral("AsciiAreaColor"), def.asciiAreaColor).value<QColor>();
    t.asciiFontColor = s.value(QStringLiteral("AsciiFontColor"), def.asciiFontColor).value<QColor>();
    t.pointedColor = s.value(QStringLiteral("PointedColor"), def.pointedColor).value<QColor>();
    t.pointedFontColor = s.value(QStringLiteral("PointedFontColor"), def.pointedFontColor).value<QColor>();
    t.pointerFontColor = s.value(QStringLiteral("PointerFontColor"), def.pointerFontColor).value<QColor>();
    t.pointerFrameColor = s.value(QStringLiteral("PointerFrameColor"), def.pointerFrameColor).value<QColor>();
    t.pointerFrameBgColor = s.value(QStringLiteral("PointerFrameBgColor"), def.pointerFrameBgColor).value<QColor>();
    t.sectionHeaderFontColor = s.value(QStringLiteral("SectionHeaderFontColor"), def.sectionHeaderFontColor).value<QColor>();
    t.sectionHeaderBgColor = s.value(QStringLiteral("SectionHeaderBgColor"), def.sectionHeaderBgColor).value<QColor>();
    t.sectionHeaderFont = s.value(QStringLiteral("SectionHeaderFont"), def.sectionHeaderFont).value<QFont>();
    t.scrollMapPtrBgColor = s.value(QStringLiteral("ScrollMapPtrBgColor"), def.scrollMapPtrBgColor).value<QColor>();
    t.scrollMapTargetBgColor = s.value(QStringLiteral("ScrollMapTargetBgColor"), def.scrollMapTargetBgColor).value<QColor>();
    return t;
}

// ---------------------------------------------------------------------------
// User preset management
// ---------------------------------------------------------------------------

static const QLatin1String kUserThemesGroup("UserThemes");

QStringList EditorTheme::userPresetNames()
{
    auto &s = AppSettings::instance();
    s.beginGroup(kUserThemesGroup);
    QStringList names = s.childKeys();
    s.endGroup();
    return names;
}

void EditorTheme::saveUserPreset(const EditorTheme &theme)
{
    auto &s = AppSettings::instance();
    QJsonDocument doc(theme.toJson());
    s.setValue(kUserThemesGroup + QLatin1Char('/') + theme.name,
              QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
}

void EditorTheme::deleteUserPreset(const QString &name)
{
    auto &s = AppSettings::instance();
    s.remove(kUserThemesGroup + QLatin1Char('/') + name);
}

EditorTheme EditorTheme::loadUserPreset(const QString &name)
{
    auto &s = AppSettings::instance();
    const QString json = s.value(kUserThemesGroup + QLatin1Char('/') + name).toString();
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (doc.isNull())
        return defaultLight();
    return fromJson(doc.object());
}

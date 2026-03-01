#include <QColorDialog>
#include <QFontDialog>
#include <QEvent>
#include <QSettings>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace
{
    const QChar kDefaultNonPrintableNoTableChar(0x25AA); // ▪
    const QChar kDefaultNotInTableChar(0x25A1);          // □

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

    {
        auto topGroupsContainer = new QWidget(this);
        auto *topGroupsLayout = new QHBoxLayout(topGroupsContainer);
        topGroupsLayout->setContentsMargins(0, 0, 0, 0);
        topGroupsLayout->setSpacing(12);

        auto *leftColumn = new QVBoxLayout();
        auto *rightColumn = new QVBoxLayout();

        leftColumn->addWidget(ui->gbAddressArea);
        leftColumn->addWidget(ui->gbHexArea);
        leftColumn->addWidget(ui->gbAsciiArea);

        rightColumn->addWidget(ui->gbColors);
        rightColumn->addWidget(ui->gbAuxiliary);

        topGroupsLayout->addLayout(leftColumn, 1);
        topGroupsLayout->addLayout(rightColumn, 1);

        ui->verticalLayout->insertWidget(0, topGroupsContainer);
        resize(qMax(width(), 860), 620);
    }

    // Connect signals for area enable/disable logic
    connect(ui->cbAddressArea, QOverload<int>::of(&QCheckBox::stateChanged),
            this, &OptionsDialog::on_cbAddressArea_stateChanged);
    connect(ui->cbAsciiArea, QOverload<int>::of(&QCheckBox::stateChanged),
            this, &OptionsDialog::on_cbAsciiArea_stateChanged);

    // Connect signals for preview (real-time changes)
    connect(ui->cbAddressArea, &QCheckBox::toggled, this, &OptionsDialog::on_checkBoxToggled);
    connect(ui->cbAsciiArea, &QCheckBox::toggled, this, &OptionsDialog::on_checkBoxToggled);
    connect(ui->cbHighlighting, &QCheckBox::toggled, this, &OptionsDialog::on_checkBoxToggled);
    connect(ui->cbDynamicSize, &QCheckBox::toggled, this, &OptionsDialog::on_checkBoxToggled);
    connect(ui->cbShowHexGrid, &QCheckBox::toggled, this, &OptionsDialog::on_checkBoxToggled);
    connect(ui->cbAutoLoadRecentFile, &QCheckBox::toggled, this, &OptionsDialog::on_checkBoxToggled);
    connect(ui->sbAddressAreaWidth, QOverload<int>::of(&QSpinBox::valueChanged), this, &OptionsDialog::on_spinBoxValueChanged);
    connect(ui->sbBytesPerLine, QOverload<int>::of(&QSpinBox::valueChanged), this, &OptionsDialog::on_spinBoxValueChanged);
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

    // Connect Bytes per Line label disable
    connect(ui->cbDynamicSize, &QCheckBox::toggled, ui->lbBytesPerLine, &QLabel::setDisabled);

    ui->leNonPrintableNoTableChar->setMaxLength(1);
    ui->leNotInTableChar->setMaxLength(1);
    ui->leNonPrintableNoTableChar->setAlignment(Qt::AlignCenter);
    ui->leNotInTableChar->setAlignment(Qt::AlignCenter);

    ui->cbAddressArea->hide();
    ui->cbAsciiArea->hide();
    ui->cbShowHexGrid->hide();

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
    saveCurrentSettings(); // Save original settings for potential rollback
    updateAreaControls();
    m_suppressUpdate = false;
    QWidget::show();
}

void OptionsDialog::accept()
{
    updateSettings();
    QDialog::accept();
}

void OptionsDialog::reject()
{
    restoreSettings(); // Restore original settings on cancel
    QDialog::reject();
}

void OptionsDialog::saveCurrentSettings()
{
    m_originalSettings.addressArea = ui->cbAddressArea->isChecked();
    m_originalSettings.addressAreaWidth = ui->sbAddressAreaWidth->value();
    m_originalSettings.asciiArea = ui->cbAsciiArea->isChecked();
    m_originalSettings.hexGridShow = ui->cbShowHexGrid->isChecked();
    m_originalSettings.highlighting = ui->cbHighlighting->isChecked();
    m_originalSettings.autosize = ui->cbDynamicSize->isChecked();
    m_originalSettings.autoLoadRecentFile = ui->cbAutoLoadRecentFile->isChecked();
    m_originalSettings.bytesPerLine = ui->sbBytesPerLine->value();
    m_originalSettings.highlightingColor = ui->lbHighlightingColor->palette().color(ui->lbHighlightingColor->backgroundRole());
    m_originalSettings.addressAreaColor = ui->lbAddressAreaColor->palette().color(ui->lbAddressAreaColor->backgroundRole());
    m_originalSettings.addressFontColor = ui->lbAddressFontColor->palette().color(ui->lbAddressFontColor->backgroundRole());
    m_originalSettings.asciiAreaColor = ui->lbAsciiAreaColor->palette().color(ui->lbAsciiAreaColor->backgroundRole());
    m_originalSettings.asciiFontColor = ui->lbAsciiFontColor->palette().color(ui->lbAsciiFontColor->backgroundRole());
    m_originalSettings.pointerColor = ui->lbPointerColor->palette().color(ui->lbPointerColor->backgroundRole());
    m_originalSettings.pointedColor = ui->lbPointedColor->palette().color(ui->lbPointedColor->backgroundRole());
    m_originalSettings.selectionColor = ui->lbSelectionColor->palette().color(ui->lbSelectionColor->backgroundRole());
    m_originalSettings.hexFontColor = ui->lbHexFontColor->palette().color(ui->lbHexFontColor->backgroundRole());
    m_originalSettings.hexAreaBgColor = ui->lbHexAreaBackground->palette().color(ui->lbHexAreaBackground->backgroundRole());
    m_originalSettings.hexAreaGridColor = ui->lbHexAreaGrid->palette().color(ui->lbHexAreaGrid->backgroundRole());
    m_originalSettings.cursorCharColor = ui->lbCursorCharColor->palette().color(ui->lbCursorCharColor->backgroundRole());
    m_originalSettings.cursorFrameColor = ui->lbCursorFrameColor->palette().color(ui->lbCursorFrameColor->backgroundRole());
    m_originalSettings.widgetFont = ui->pbWidgetFont->font();
    m_originalSettings.nonPrintableNoTableChar = sanitizeSingleChar(ui->leNonPrintableNoTableChar->text(), kDefaultNonPrintableNoTableChar);
    m_originalSettings.notInTableChar = sanitizeSingleChar(ui->leNotInTableChar->text(), kDefaultNotInTableChar);
}

void OptionsDialog::restoreSettings()
{
    m_suppressUpdate = true;
    ui->cbAddressArea->setChecked(m_originalSettings.addressArea);
    ui->sbAddressAreaWidth->setValue(m_originalSettings.addressAreaWidth);
    ui->cbAsciiArea->setChecked(m_originalSettings.asciiArea);
    ui->cbShowHexGrid->setChecked(m_originalSettings.hexGridShow);
    ui->cbHighlighting->setChecked(m_originalSettings.highlighting);
    ui->cbDynamicSize->setChecked(m_originalSettings.autosize);
    ui->cbAutoLoadRecentFile->setChecked(m_originalSettings.autoLoadRecentFile);
    ui->sbBytesPerLine->setValue(m_originalSettings.bytesPerLine);
    setColor(ui->lbHighlightingColor, m_originalSettings.highlightingColor);
    setColor(ui->lbAddressAreaColor, m_originalSettings.addressAreaColor);
    setColor(ui->lbAddressFontColor, m_originalSettings.addressFontColor);
    setColor(ui->lbAsciiAreaColor, m_originalSettings.asciiAreaColor);
    setColor(ui->lbAsciiFontColor, m_originalSettings.asciiFontColor);
    setColor(ui->lbPointerColor, m_originalSettings.pointerColor);
    setColor(ui->lbPointedColor, m_originalSettings.pointedColor);
    setColor(ui->lbSelectionColor, m_originalSettings.selectionColor);
    setColor(ui->lbHexFontColor, m_originalSettings.hexFontColor);
    setColor(ui->lbHexAreaBackground, m_originalSettings.hexAreaBgColor);
    setColor(ui->lbHexAreaGrid, m_originalSettings.hexAreaGridColor);
    setColor(ui->lbCursorCharColor, m_originalSettings.cursorCharColor);
    setColor(ui->lbCursorFrameColor, m_originalSettings.cursorFrameColor);
    ui->pbWidgetFont->setFont(m_originalSettings.widgetFont);
    updateFontButtonText(m_originalSettings.widgetFont);
    ui->leNonPrintableNoTableChar->setText(sanitizeSingleChar(m_originalSettings.nonPrintableNoTableChar, kDefaultNonPrintableNoTableChar));
    ui->leNotInTableChar->setText(sanitizeSingleChar(m_originalSettings.notInTableChar, kDefaultNotInTableChar));
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
    QSettings settings;

    ui->cbAddressArea->setChecked(settings.value("AddressArea", true).toBool());
    ui->cbAsciiArea->setChecked(settings.value("AsciiArea", true).toBool());
    ui->cbHighlighting->setChecked(settings.value("Highlighting", true).toBool());
    ui->cbDynamicSize->setChecked(settings.value("Autosize", true).toBool());
    ui->cbShowHexGrid->setChecked(settings.value("ShowHexGrid", true).toBool());
    ui->cbAutoLoadRecentFile->setChecked(settings.value("AutoLoadRecentFile", true).toBool());

    setColor(ui->lbHighlightingColor, settings.value("HighlightingColor", QColor(0xff, 0xff, 0x99, 0xff)).value<QColor>());
    setColor(ui->lbAddressAreaColor, settings.value("AddressAreaColor", this->palette().alternateBase().color()).value<QColor>());
    setColor(ui->lbPointerColor, settings.value("PointerColor", QColor(0xa0, 0xc0, 0x00, 0xff)).value<QColor>());
    setColor(ui->lbPointedColor, settings.value("PointedColor", QColor(0xc0, 0x80, 0x00, 0xff)).value<QColor>());
    setColor(ui->lbSelectionColor, settings.value("SelectionColor", this->palette().highlight().color()).value<QColor>());
    setColor(ui->lbAddressFontColor, settings.value("AddressFontColor", QPalette::WindowText).value<QColor>());
    setColor(ui->lbAsciiAreaColor, settings.value("AsciiAreaColor", this->palette().alternateBase().color()).value<QColor>());
    setColor(ui->lbAsciiFontColor, settings.value("AsciiFontColor", QPalette::WindowText).value<QColor>());
    setColor(ui->lbHexFontColor, settings.value("HexFontColor", QPalette::WindowText).value<QColor>());
    setColor(ui->lbHexAreaBackground, settings.value("HexAreaBackgroundColor", QColor(Qt::white)).value<QColor>());
    setColor(ui->lbHexAreaGrid, settings.value("HexAreaGridColor", QColor(0x99, 0x99, 0x99)).value<QColor>());
    setColor(ui->lbCursorCharColor, settings.value("CursorCharColor", QColor(0x00, 0x60, 0xFF, 0x80)).value<QColor>());
    setColor(ui->lbCursorFrameColor, settings.value("CursorFrameColor", QColor(Qt::black)).value<QColor>());

#ifdef Q_OS_WIN32
    QFont defaultFont("Courier", 14);
#else
    QFont defaultFont("Courier New", 14);
#endif
    QFont selectedFont = settings.value("WidgetFont", defaultFont).value<QFont>();
    ui->pbWidgetFont->setFont(selectedFont);
    updateFontButtonText(selectedFont);
    applyPlaceholderFieldFont(ui->leNonPrintableNoTableChar, ui->leNotInTableChar, selectedFont);

    ui->leNonPrintableNoTableChar->setText(sanitizeSingleChar(settings.value("NonPrintableNoTableChar", QString(kDefaultNonPrintableNoTableChar)).toString(), kDefaultNonPrintableNoTableChar));
    ui->leNotInTableChar->setText(sanitizeSingleChar(settings.value("NotInTableChar", QString(kDefaultNotInTableChar)).toString(), kDefaultNotInTableChar));

    ui->sbAddressAreaWidth->setValue(settings.value("AddressAreaWidth", 8).toInt() / 2);
    ui->sbBytesPerLine->setValue(settings.value("BytesPerLine", 32).toInt());
}

void OptionsDialog::writeSettings()
{
    QSettings settings;
    settings.setValue("Highlighting", ui->cbHighlighting->isChecked());
    settings.setValue("Autosize", ui->cbDynamicSize->isChecked());
    settings.setValue("AutoLoadRecentFile", ui->cbAutoLoadRecentFile->isChecked());

    settings.setValue("HighlightingColor", ui->lbHighlightingColor->palette().color(ui->lbHighlightingColor->backgroundRole()));
    settings.setValue("AddressAreaColor", ui->lbAddressAreaColor->palette().color(ui->lbAddressAreaColor->backgroundRole()));
    settings.setValue("PointersColor", ui->lbPointerColor->palette().color(ui->lbPointerColor->backgroundRole()));
    settings.setValue("PointedColor", ui->lbPointedColor->palette().color(ui->lbPointedColor->backgroundRole()));
    settings.setValue("SelectionColor", ui->lbSelectionColor->palette().color(ui->lbSelectionColor->backgroundRole()));
    settings.setValue("AddressFontColor", ui->lbAddressFontColor->palette().color(ui->lbAddressFontColor->backgroundRole()));
    settings.setValue("AsciiAreaColor", ui->lbAsciiAreaColor->palette().color(ui->lbAsciiAreaColor->backgroundRole()));
    settings.setValue("AsciiFontColor", ui->lbAsciiFontColor->palette().color(ui->lbAsciiFontColor->backgroundRole()));
    settings.setValue("HexFontColor", ui->lbHexFontColor->palette().color(ui->lbHexFontColor->backgroundRole()));
    settings.setValue("HexAreaBackgroundColor", ui->lbHexAreaBackground->palette().color(ui->lbHexAreaBackground->backgroundRole()));
    settings.setValue("HexAreaGridColor", ui->lbHexAreaGrid->palette().color(ui->lbHexAreaGrid->backgroundRole()));
    settings.setValue("CursorCharColor", ui->lbCursorCharColor->palette().color(ui->lbCursorCharColor->backgroundRole()));
    settings.setValue("CursorFrameColor", ui->lbCursorFrameColor->palette().color(ui->lbCursorFrameColor->backgroundRole()));
    settings.setValue("WidgetFont", ui->pbWidgetFont->font());
    settings.setValue("NonPrintableNoTableChar", sanitizeSingleChar(ui->leNonPrintableNoTableChar->text(), kDefaultNonPrintableNoTableChar));
    settings.setValue("NotInTableChar", sanitizeSingleChar(ui->leNotInTableChar->text(), kDefaultNotInTableChar));

    settings.setValue("AddressAreaWidth", ui->sbAddressAreaWidth->value() * 2);
    settings.setValue("BytesPerLine", ui->sbBytesPerLine->value());
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

void OptionsDialog::on_pbPointerColor_clicked()
{
    QColor color = QColorDialog::getColor(currentSwatchColor(ui->lbPointerColor), this);

    if (color.isValid())
    {
        setColor(ui->lbPointerColor, color);
        updateSettings();
    }
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

void OptionsDialog::on_cbDynamicSize_stateChanged(int)
{
    ui->sbBytesPerLine->setDisabled(ui->cbDynamicSize->isChecked());
    updateSettings();
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

void OptionsDialog::on_cbShowHexGrid_stateChanged(int)
{
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
    bool addressEnabled = ui->cbAddressArea->isChecked();
    ui->sbAddressAreaWidth->setEnabled(addressEnabled);
    ui->pbAddressAreaColor->setEnabled(addressEnabled);
    ui->pbAddressFontColor->setEnabled(addressEnabled);
    ui->lbAddressAreaWidthLabel->setEnabled(addressEnabled);

    // Enable/disable ASCII Area controls based on checkbox
    bool asciiEnabled = ui->cbAsciiArea->isChecked();
    ui->pbAsciiAreaColor->setEnabled(asciiEnabled);
    ui->pbAsciiFontColor->setEnabled(asciiEnabled);

    // Disable Bytes per Line label if autosize is checked
    ui->lbBytesPerLine->setDisabled(ui->cbDynamicSize->isChecked());
}

void OptionsDialog::on_pbDefault_clicked()
{
    resetToDefaults();
}

void OptionsDialog::resetToDefaults()
{
    m_suppressUpdate = true;
    ui->cbAddressArea->setChecked(true);
    ui->sbAddressAreaWidth->setValue(4);
    ui->cbAsciiArea->setChecked(true);
    ui->cbShowHexGrid->setChecked(true);
    ui->cbHighlighting->setChecked(true);
    ui->cbDynamicSize->setChecked(true);
    ui->cbAutoLoadRecentFile->setChecked(true);
    ui->sbBytesPerLine->setValue(16);

    setColor(ui->lbHighlightingColor, QColor(0xff, 0xff, 0x99, 0xff));
    setColor(ui->lbAddressAreaColor, this->palette().alternateBase().color());
    setColor(ui->lbPointerColor, QColor(0xa0, 0xc0, 0x00, 0xff));
    setColor(ui->lbPointedColor, QColor(0xc0, 0x80, 0x00, 0xff));
    setColor(ui->lbSelectionColor, this->palette().highlight().color());
    setColor(ui->lbAddressFontColor, this->palette().color(QPalette::WindowText));
    setColor(ui->lbAsciiAreaColor, this->palette().alternateBase().color());
    setColor(ui->lbAsciiFontColor, this->palette().color(QPalette::WindowText));
    setColor(ui->lbHexFontColor, this->palette().color(QPalette::WindowText));
    setColor(ui->lbHexAreaBackground, QColor(Qt::white));
    setColor(ui->lbHexAreaGrid, QColor(0x99, 0x99, 0x99));
    setColor(ui->lbCursorCharColor, QColor(0x00, 0x60, 0xFF, 0x80));
    setColor(ui->lbCursorFrameColor, QColor(Qt::black));

#ifdef Q_OS_WIN32
    QFont defaultFont("Courier", 14);
#else
    QFont defaultFont("Courier New", 14);
#endif
    ui->pbWidgetFont->setFont(defaultFont);
    updateFontButtonText(defaultFont);
    applyPlaceholderFieldFont(ui->leNonPrintableNoTableChar, ui->leNotInTableChar, defaultFont);
    ui->leNonPrintableNoTableChar->setText(QString(kDefaultNonPrintableNoTableChar));
    ui->leNotInTableChar->setText(QString(kDefaultNotInTableChar));

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
    if (event->type() == QEvent::LanguageChange)
        ui->retranslateUi(this);
    QDialog::changeEvent(event);
}


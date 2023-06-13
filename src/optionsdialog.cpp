#include <QColorDialog>
#include <QFontDialog>

#include "optionsdialog.h"
#include "ui_optionsdialog.h"

OptionsDialog::OptionsDialog(QWidget *parent) : QDialog(parent), ui(new Ui::OptionsDialog)
{
    ui->setupUi(this);
    readSettings();
    writeSettings();

    setModal(true);
}

OptionsDialog::~OptionsDialog()
{
    delete ui;
}

void OptionsDialog::show()
{
    readSettings();
    QWidget::show();
}

void OptionsDialog::accept()
{
    updateSettings();
    QDialog::hide();
}

void OptionsDialog::updateSettings()
{
    writeSettings();
    emit accepted();
}

void OptionsDialog::readSettings()
{
    QSettings settings;

    ui->cbAddressArea->setChecked(settings.value("AddressArea", true).toBool());
    ui->cbAsciiArea->setChecked(settings.value("AsciiArea", true).toBool());
    ui->cbHighlighting->setChecked(settings.value("Highlighting", true).toBool());
    ui->cbDynamicSize->setChecked(settings.value("Autosize", true).toBool());

    setColor(ui->lbHighlightingColor, settings.value("HighlightingColor", QColor(0xff, 0xff, 0x99, 0xff)).value<QColor>());
    setColor(ui->lbAddressAreaColor, settings.value("AddressAreaColor", this->palette().alternateBase().color()).value<QColor>());
    setColor(ui->lbPointerColor, settings.value("PointerColor", QColor(0xa0, 0xc0, 0x00, 0xff)).value<QColor>());
    setColor(ui->lbPointedColor, settings.value("PointedColor", QColor(0xc0, 0x80, 0x00, 0xff)).value<QColor>());
    setColor(ui->lbSelectionColor, settings.value("SelectionColor", this->palette().highlight().color()).value<QColor>());
    setColor(ui->lbAddressFontColor, settings.value("AddressFontColor", QPalette::WindowText).value<QColor>());
    setColor(ui->lbAsciiAreaColor, settings.value("AsciiAreaColor", this->palette().alternateBase().color()).value<QColor>());
    setColor(ui->lbAsciiFontColor, settings.value("AsciiFontColor", QPalette::WindowText).value<QColor>());
    setColor(ui->lbHexFontColor, settings.value("HexFontColor", QPalette::WindowText).value<QColor>());

#ifdef Q_OS_WIN32
    ui->leWidgetFont->setFont(settings.value("WidgetFont", QFont("Courier", 14)).value<QFont>());
#else
    ui->leWidgetFont->setFont(settings.value("WidgetFont", QFont("Monospace", 14)).value<QFont>());
#endif

    ui->sbAddressAreaWidth->setValue(settings.value("AddressAreaWidth", 8).toInt() / 2);
    ui->sbBytesPerLine->setValue(settings.value("BytesPerLine", 32).toInt());
}

void OptionsDialog::writeSettings()
{
    QSettings settings;
    settings.setValue("AddressArea", ui->cbAddressArea->isChecked());
    settings.setValue("AsciiArea", ui->cbAsciiArea->isChecked());
    settings.setValue("Highlighting", ui->cbHighlighting->isChecked());
    settings.setValue("Autosize", ui->cbDynamicSize->isChecked());

    settings.setValue("HighlightingColor", ui->lbHighlightingColor->palette().color(ui->lbHighlightingColor->backgroundRole()));
    settings.setValue("AddressAreaColor", ui->lbAddressAreaColor->palette().color(ui->lbAddressAreaColor->backgroundRole()));
    settings.setValue("PointersColor", ui->lbPointerColor->palette().color(ui->lbPointerColor->backgroundRole()));
    settings.setValue("PointedColor", ui->lbPointedColor->palette().color(ui->lbPointedColor->backgroundRole()));
    settings.setValue("SelectionColor", ui->lbSelectionColor->palette().color(ui->lbSelectionColor->backgroundRole()));
    settings.setValue("AddressFontColor", ui->lbAddressFontColor->palette().color(ui->lbAddressFontColor->backgroundRole()));
    settings.setValue("AsciiAreaColor", ui->lbAsciiAreaColor->palette().color(ui->lbAsciiAreaColor->backgroundRole()));
    settings.setValue("AsciiFontColor", ui->lbAsciiFontColor->palette().color(ui->lbAsciiFontColor->backgroundRole()));
    settings.setValue("HexFontColor", ui->lbHexFontColor->palette().color(ui->lbHexFontColor->backgroundRole()));
    settings.setValue("WidgetFont", ui->leWidgetFont->font());

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
    QColor color = QColorDialog::getColor(ui->lbHighlightingColor->palette().color(QPalette::Base), this);

    if (color.isValid())
    {
        setColor(ui->lbHighlightingColor, color);
        updateSettings();
    }
}

void OptionsDialog::on_pbAddressAreaColor_clicked()
{
    QColor color = QColorDialog::getColor(ui->lbAddressAreaColor->palette().color(QPalette::Base), this);

    if (color.isValid())
    {
        setColor(ui->lbAddressAreaColor, color);
        updateSettings();
    }
}

void OptionsDialog::on_pbAddressFontColor_clicked()
{
    QColor color = QColorDialog::getColor(ui->lbAddressFontColor->palette().color(QPalette::WindowText), this);

    if (color.isValid())
    {
        setColor(ui->lbAddressFontColor, color);
        updateSettings();
    }
}

void OptionsDialog::on_pbAsciiAreaColor_clicked()
{
    QColor color = QColorDialog::getColor(ui->lbAsciiAreaColor->palette().color(QPalette::Base), this);

    if (color.isValid())
    {
        setColor(ui->lbAsciiAreaColor, color);
        updateSettings();
    }
}

void OptionsDialog::on_pbAsciiFontColor_clicked()
{
    QColor color = QColorDialog::getColor(ui->lbAsciiFontColor->palette().color(QPalette::WindowText), this);

    if (color.isValid())
    {
        setColor(ui->lbAsciiFontColor, color);
        updateSettings();
    }
}

void OptionsDialog::on_pbHexFontColor_clicked()
{
    QColor color = QColorDialog::getColor(ui->lbHexFontColor->palette().color(QPalette::WindowText), this);

    if (color.isValid())
    {
        setColor(ui->lbHexFontColor, color);
        updateSettings();
    }
}

void OptionsDialog::on_pbSelectionColor_clicked()
{
    QColor color = QColorDialog::getColor(ui->lbSelectionColor->palette().color(QPalette::Base), this);

    if (color.isValid())
    {
        setColor(ui->lbSelectionColor, color);
        updateSettings();
    }
}

void OptionsDialog::on_pbWidgetFont_clicked()
{
    bool ok;

    QFont font = QFontDialog::getFont(&ok, ui->leWidgetFont->font(), this);

    if (ok)
    {
        ui->leWidgetFont->setFont(font);
        updateSettings();
    }

    QWidget::show();
}

void OptionsDialog::on_pbPointerColor_clicked()
{
    QColor color = QColorDialog::getColor(ui->lbPointerColor->palette().color(QPalette::Base), this);

    if (color.isValid())
    {
        setColor(ui->lbPointerColor, color);
        updateSettings();
    }
}


void OptionsDialog::on_pbPointedColor_clicked()
{
    QColor color = QColorDialog::getColor(ui->lbPointedColor->palette().color(QPalette::Base), this);

    if (color.isValid())
    {
        setColor(ui->lbPointedColor, color);
        updateSettings();
    }
}


void OptionsDialog::on_cbDynamicSize_stateChanged(int arg1)
{
    ui->sbBytesPerLine->setDisabled(arg1);
}


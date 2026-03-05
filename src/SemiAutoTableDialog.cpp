#include "SemiAutoTableDialog.h"
#include "qhexedit/qhexedit.h"
#include "translationtable.h"

#include <QLineEdit>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QEvent>

SemiAutoTableDialog::SemiAutoTableDialog(QHexEdit *hexEdit, TranslationTable **tb, QWidget *parent)
    : QDialog(parent), _hexEdit(hexEdit), _tb(tb)
{
    setWindowTitle(tr("Semi-auto table generation"));
    setModal(true);

    auto *mainLayout = new QVBoxLayout(this);

    auto *label = new QLabel(tr("Enter a known text fragment (Latin letters):"));
    mainLayout->addWidget(label);

    _leSearch = new QLineEdit;
    _leSearch->setMaxLength(20);
    // Width for ~20 characters
    QFontMetrics fm(_leSearch->font());
    _leSearch->setFixedWidth(fm.averageCharWidth() * 24 + 16);
    mainLayout->addWidget(_leSearch);

    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch();

    _pbFind = new QPushButton(tr("Find"));
    _pbFind->setDefault(true);
    buttonLayout->addWidget(_pbFind);

    _pbCancel = new QPushButton(tr("Cancel"));
    buttonLayout->addWidget(_pbCancel);

    mainLayout->addLayout(buttonLayout);

    setLayout(mainLayout);
    setFixedSize(sizeHint());

    connect(_pbFind, &QPushButton::clicked, this, &SemiAutoTableDialog::onFind);
    connect(_pbCancel, &QPushButton::clicked, this, &QDialog::reject);
}

void SemiAutoTableDialog::onFind()
{
    auto text = _leSearch->text();

    if (text.isEmpty())
        return;

    // Convert the input text to a byte array (Latin1)
    QByteArray needle = text.toLatin1();

    // Perform a relative search from position 0
    qint64 pos = _hexEdit->relativeSearch(needle, 0);

    if (pos < 0)
    {
        QMessageBox::information(this, tr("Not found"),
            tr("No relative match found for \"%1\".\nTry a different string, possibly shorter.").arg(text));
        return;
    }

    // Found! Read the actual bytes at the found position
    QByteArray foundBytes = _hexEdit->dataAt(pos, needle.size());

    // Warn if a table already exists
    if (*_tb)
    {
        auto res = QMessageBox::warning(this, tr("Table already exists"),
                                        tr("A translation table is already loaded. Creating a new one will discard it.\nIf you have unsaved changes, save the table first.\n\nContinue?"),
                                        QMessageBox::Yes | QMessageBox::No);

        if (res != QMessageBox::Yes)
            return;

        _hexEdit->removeTranslationTable();
        delete *_tb;
        *_tb = nullptr;
    }

    *_tb = new TranslationTable();

    // generateTable expects: input = raw bytes from ROM, value = the known ASCII text
    (*_tb)->generateTable(QString::fromLatin1(foundBytes.constData(), foundBytes.size()), text);

    if ((*_tb)->size() == 0)
    {
        QMessageBox::warning(
            this,
            tr("Generation failed"),
            tr("Could not generate any table entries from the found match.\nTry a different string with more variety of letters."));

        delete *_tb;
        *_tb = nullptr;
        return;
    }

    QMessageBox::information(
        this,
        tr("Table generated"),
        tr("Generated %1 table entries.\nThe table editor will open so you can review and adjust.").arg((*_tb)->size()));

    emit tableGenerated();
    accept();
}

void SemiAutoTableDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(event);
}

void SemiAutoTableDialog::retranslateUi()
{
    setWindowTitle(tr("Semi-auto table generation"));
    _pbFind->setText(tr("Find"));
    _pbCancel->setText(tr("Cancel"));
}

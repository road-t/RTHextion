#ifndef TRANSLATIONTABLE_H
#define TRANSLATIONTABLE_H

#include <QMap>
#include <QString>

class TranslationTable
{
public:
    TranslationTable();
    TranslationTable(QString fileName);
    QByteArray decode(QByteArray src);
    char decodeSymbol(QString src) const;
    QString encode(QByteArray src, bool keepCodes = false);
    QString encodeSymbol(const char symbol, bool keepCode = false);
    QMap<char, QString>* getItems();
    QPair<char, QString> item(QMap<char, QString>::iterator it);
    QPair<char, QString> next();
    QPair<char, QString> prev();
    void reset();
    uint32_t generateTable(QString input, QString value);
    uint32_t size();
    static QString escapeNonPrintable(QByteArray src);
    static QString charToHex(const char symbol);

protected:
    QMap<char, QString> encodeTable;
    QMap<QString, char> decodeTable;

private:
    QMap<char, QString>::Iterator it;
};

#endif // TRANSLATIONTABLE_H

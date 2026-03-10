#ifndef TRANSLATIONTABLE_H
#define TRANSLATIONTABLE_H

#include <QMap>
#include <QByteArray>
#include <QString>

class TranslationTable
{
public:
    TranslationTable();
    TranslationTable(QString fileName);
    QByteArray decode(QByteArray src);
    char decodeSymbol(QString src) const;
    // Returns the byte sequence for a text symbol: checks multi-byte table first, then single-byte.
    // Returns empty QByteArray if not found in either table.
    QByteArray decodeToBytes(const QString &text) const;
    QString encode(QByteArray src, bool keepCodes = false);
    QString encodeSymbol(const char symbol, bool keepCode = false) const;
    // Multi-byte aware encode: tries to match 2+ byte sequences first, then single byte
    QString encodeBytes(const QByteArray &src, int pos, int &bytesConsumed, bool keepCode = false) const;
    QMap<char, QString>* getItems();
    const QMap<QByteArray, QString>& getMultiByteItems() const { return multiByteEncodeTable; }
    bool hasMultiByteEntries() const { return !multiByteEncodeTable.isEmpty(); }
    int maxKeyLength() const { return _maxKeyLen; }
    QPair<char, QString> item(QMap<char, QString>::iterator it);
    QPair<char, QString> next();
    QPair<char, QString> prev();
    void reset();
    void setItem(uint8_t key, const QString& value);
    void setMultiByteItem(const QByteArray &key, const QString& value);
    void removeItem(uint8_t key);
    void removeMultiByteItem(const QByteArray &key);
    void clearItems();
    uint32_t generateTable(QString input, QString value);
    bool save(const QString &fileName) const;
    uint32_t size();
    static QString escapeNonPrintable(QByteArray src);
    static QString charToHex(const char symbol);

protected:
    QMap<char, QString> encodeTable;              // single-byte: byte → text
    QMap<QString, char> decodeTable;              // single-byte: text → byte
    QMap<QByteArray, QString> multiByteEncodeTable; // multi-byte: byte sequence → text
    QMap<QString, QByteArray> multiByteDecodeTable; // multi-byte: text → byte sequence
    int _maxKeyLen = 1;                           // longest key length in bytes

private:
    QMap<char, QString>::Iterator it;
};

#endif // TRANSLATIONTABLE_H

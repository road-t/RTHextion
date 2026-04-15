// encoding.h — Encoding decode/encode helpers extracted from hexeditor.cpp
#ifndef HEXEDITOR_ENCODING_H
#define HEXEDITOR_ENCODING_H

#include <QString>
#include <QByteArray>
#include <QChar>
#include <QVector>

class TranslationTable;

bool isSingleByteEncoding(const QString &enc);
QVector<QByteArray> codecCandidates(const QString &enc);
QChar decodeSingleByte(uint8_t byte, const QString &encoding);

const char *iconvCodecName(const QString &enc);
int iconvSeqLen(const QByteArray &data, int pos, const QString &enc);
QString decodeWithIconv(const QByteArray &data, const QString &encoding);
QByteArray encodeWithIconv(const QString &text, const QString &encoding);

QString decodeTextWithEncoding(const QByteArray &data, const QString &encoding);
QByteArray encodeTextWithEncoding(const QString &text, const QString &encoding);

QVector<QString> decodeBufferWithEncoding(const QByteArray &data,
                                          const QString &encoding);

void decodeBufferWithTable(const QByteArray &data,
                           const TranslationTable *tb,
                           QVector<QString> &outChars,
                           QVector<int> &outSpan);

#endif // HEXEDITOR_ENCODING_H

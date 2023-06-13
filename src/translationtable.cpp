#include <QIODevice>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>

#include "translationtable.h"

TranslationTable::TranslationTable()
{
    reset();
}

TranslationTable::TranslationTable(QString fileName) : TranslationTable()
{
    QFile inputFile(fileName);

    if (inputFile.open(QIODevice::ReadOnly))
    {
       QTextStream in(&inputFile);

       while (!in.atEnd())
       {
          auto line = in.readLine().split('=');

          // skip weird lines
          if (line.size() == 2)
          {
              bool success;

              auto val = line[0].toUInt(&success, 16);

              if (success)
              {
                  encodeTable.insert(val, line[1]);
                  decodeTable[line[1]] = val;
              }
          }
       }

       inputFile.close();

       // fill empty bytes in decode table with precompiled byte sequences
       for (uint16_t i = 0; i < 0x100; i++)
       {
           if (!encodeTable.contains(i))
           {
               auto btSequence = QString("{%1}").arg(i, 2, 16, QChar('0'));

               decodeTable[btSequence] = i;

               // for hex sequences that contain letters we'd better add uppercased version
               if (i > 0x9F || (i & 0xF) > 9)
                   decodeTable[btSequence.toUpper()] = i;
           }
       }
    }
}

uint32_t TranslationTable::size()
{
    return encodeTable.size();
}

QByteArray TranslationTable::decode(QByteArray src)
{
    auto result = QByteArray();
    auto text = QString(src);

    for (auto i = decodeTable.begin(); i != decodeTable.end(); i++)
    {
        text.replace(i.key(), QChar(i.value()));
    }

    result = text.toLatin1();

    return result;
}

// WILL WORK ONLY FOR SYMBOLS
char TranslationTable::decodeSymbol(QString src) const
{
    return encodeTable.key(src, -1);
}

QString TranslationTable::encode(QByteArray src, bool keepCodes)
{
    auto result = QString();

    for (auto i = 0; i < src.size(); i++)
    {
        result += encodeSymbol(src[i], keepCodes);
    }

    return result;
}

QString TranslationTable::encodeSymbol(const char symbol, bool keepCode)
{
    if (encodeTable.contains(static_cast<uint8_t>(symbol)))
        return encodeTable.value(static_cast<uint8_t>(symbol));
    else
        return keepCode ? charToHex(symbol) : "#";
}

QString TranslationTable::escapeNonPrintable(QByteArray src)
{
    auto result = QString();

    for (auto i = 0; i < src.size(); i++)
    {
        auto ch = QChar(src[i]);
        result += ch.isPrint() ? ch : charToHex(src[i]);
    }

    return result;
}

QString TranslationTable::charToHex(const char symbol)
{
    return QString("{%1}").arg(QString::number(static_cast<uint8_t>(symbol), 16), 2, '0').toUpper();
}

QMap<char, QString>* TranslationTable::getItems()
{
    return &encodeTable;
}

QPair<char, QString> TranslationTable::item(QMap<char, QString>::iterator it)
{
    return QPair<int32_t, QString>(it.key(), it.value());
}

QPair<char, QString> TranslationTable::next()
{
    if (++it == encodeTable.end())
        it = encodeTable.begin();

    return item(it);
}

QPair<char, QString> TranslationTable::prev()
{
    if (--it == encodeTable.begin())
        it = encodeTable.end();

    return item(it);
}

void TranslationTable::reset()
{
    it = encodeTable.begin();
}

uint32_t TranslationTable::generateTable(QString input, QString value)
{
   static const auto ucRE = QRegularExpression("([A-Z]+)");
   static const auto lcRE = QRegularExpression("([a-z]+)");
   static const auto dRE = QRegularExpression("([0-9]+)");

   auto capitals = value.indexOf(ucRE, 0);
   auto lowerCased = value.indexOf(lcRE, 0);
   auto digits = value.indexOf(dRE, 0);

   encodeTable.clear();

   if (capitals != -1)
   {
       uint8_t capitalsDistanceToASCII = input[capitals].toLatin1() - value[capitals].toLatin1();

       for (uint8_t c = ('A' + capitalsDistanceToASCII); c <= 'Z' + capitalsDistanceToASCII; c++)
       {
           encodeTable.insert(c, QChar::fromLatin1(c - capitalsDistanceToASCII));
       }
   }

   if (lowerCased != -1)
   {
       int16_t lcDistanceToASCII = input[lowerCased].toLatin1() - value[lowerCased].toLatin1();

       for (char c = ('a' + lcDistanceToASCII); c <= 'z' + lcDistanceToASCII; c++)
       {
           encodeTable.insert(c, QChar::fromLatin1(c - lcDistanceToASCII));
       }
   }

   if (digits != -1)
   {
       int16_t digitsDistanceToASCII = input[digits].toLatin1() - value[digits].toLatin1();

       for (char c = ('0' + digitsDistanceToASCII); c <= '9' + digitsDistanceToASCII; c++)
       {
           encodeTable.insert(c, QChar::fromLatin1(c - digitsDistanceToASCII));
       }
   }

   for (auto it = encodeTable.begin(); it != encodeTable.end(); it++)
   {
       qDebug() << QString::number(it.key(), 16) << ": " << encodeTable.value(it.key());
   }

    return encodeTable.size();
}

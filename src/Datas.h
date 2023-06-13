#ifndef DATAS_H
#define DATAS_H


#include "QtCore/qendian.h"

union Datas // bad English, yup
{
    const char chr[4];
    const uint32_t dword;
    qint8   sByte;
    quint8  uByte;
    quint16_le  leWord;
    quint16_be beWord;
    quint32_le leDword;
    quint32_be beDword;
};

#endif // DATAS_H

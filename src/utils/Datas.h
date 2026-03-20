#ifndef DATAS_H
#define DATAS_H


#include "QtCore/qendian.h"

enum class ByteOrder : int {
    LittleEndian = 0,
    BigEndian    = 1,
    SwappedBytes = 2   // word-swapped: each 16-bit word has its bytes reversed
                       // e.g. 0xABCDEF12 → bytes CD AB 12 EF
};

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

/// Helper: Decode a 16-bit swapped value (bytes reversed within the word).
/// Input bytes [0][1] → output 0x[1][0] (i.e., 0x8037 stored as 37 80).
inline quint16 decodeSwappedWord(const uchar *ptr)
{
    return (static_cast<quint16>(ptr[1]) << 8) | static_cast<quint16>(ptr[0]);
}

/// Helper: Decode a 32-bit swapped value (each 16-bit word's bytes are reversed).
/// Input bytes [0][1][2][3] → bytes [1][0] form hi-word, [3][2] form lo-word.
inline quint32 decodeSwappedDword(const uchar *ptr)
{
    const quint16 hi = decodeSwappedWord(ptr);     // [0][1] → 0x[1][0]
    const quint16 lo = decodeSwappedWord(ptr + 2); // [2][3] → 0x[3][2]
    return (static_cast<quint32>(hi) << 16) | lo;
}

/// Read a 16-bit value using the given byte order.
inline quint16 readWord(const Datas &d, ByteOrder order)
{
    switch (order) {
    case ByteOrder::BigEndian:
        return static_cast<quint16>(d.beWord);
    case ByteOrder::SwappedBytes:
        return decodeSwappedWord(reinterpret_cast<const uchar*>(d.chr));
    default:
        return static_cast<quint16>(d.leWord);
    }
}

/// Read a 32-bit value using the given byte order.
/// SwappedBytes: each 16-bit word's bytes are reversed.
/// e.g. bytes on disk: CD AB 12 EF → value 0xABCDEF12
inline quint32 readDword(const Datas &d, ByteOrder order)
{
    switch (order) {
    case ByteOrder::BigEndian:
        return static_cast<quint32>(d.beDword);
    case ByteOrder::SwappedBytes:
        return decodeSwappedDword(reinterpret_cast<const uchar*>(d.chr));
    default:
        return static_cast<quint32>(d.leDword);
    }
}

/// Decode a pointer value from raw bytes with the given byte order and size.
inline quint64 decodePointer(const uchar *ptr, int pointerSize, ByteOrder order)
{
    if (pointerSize == 2) {
        switch (order) {
        case ByteOrder::BigEndian:
            return qFromBigEndian<quint16>(ptr);
        case ByteOrder::SwappedBytes:
            return decodeSwappedWord(ptr);
        default:
            return qFromLittleEndian<quint16>(ptr);
        }
    } else { // 4 bytes (3-byte pointers are no longer supported)
        switch (order) {
        case ByteOrder::BigEndian:
            return qFromBigEndian<quint32>(ptr);
        case ByteOrder::SwappedBytes:
            return decodeSwappedDword(ptr);
        default:
            return qFromLittleEndian<quint32>(ptr);
        }
    }
}

#endif // DATAS_H

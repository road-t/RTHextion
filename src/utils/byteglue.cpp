#include <cstring>
#include <string>
#include <sstream>

#include "byteglue.h"

namespace byteglue
{

bool hexStringToByteArray(std::string hexString, byte* byteArray)
{
    size_t length = hexString.size();

    if (length % 2)
        return false;

    for (size_t i = 0; i < length; i += 2)
    {
        byte hi = charToByte(hexString[i]) << 4;
        byte lo = charToByte(hexString[i + 1]);
        byteArray[i / 2] = hi | lo;
    }

    return true;
}

void shiftByteArray(const byte *ptr, byte *buf, byte length, bool bigEndian, byte toBits)
{
    union qWordBytes data = {.qWord = 0};

    if (bigEndian)
    {
        for (int i = 7; i >= 0; i--)
        {
            data.bytes[7 - i] = *(ptr + i);
        }

        data.qWord <<= toBits;

        for (int i = 7; i >= 0; i--)
        {
            *(buf + i) = data.bytes[7 - i];
        }
    }
    else
    {
        memcpy(data.bytes, ptr, length);

        data.qWord <<= toBits;

        memcpy(buf, data.bytes, length);
    }
}

std::string byteArrayToHex(const byte* input, size_t len)
{
    static const char* lut = "0123456789ABCDEF";

    std::stringstream ss;

    for (size_t i = 0; i < len; i++)
    {
        const unsigned char c = input[i];
        ss.put(lut[c >> 0x4]);
        ss.put(lut[c & 0xF]);
    }

    return ss.str();
}

std::string byteArrayToString(const byte* input, size_t length)
{
    std::stringstream ss;
    for (auto i = 0; i < length; i++)
    {
        ss.put(input[i]);
    }

    return ss.str();
}

};

#pragma once
#include <string>

#include "Types.h"

namespace byteglue
{
    union qWordBytes
    {
        byte    bytes[8];
        qword   qWord;
    };

    inline byte charToByte(const char ch)
    {
        return ch < 0x3A ? ch - 0x30 : (ch < 0x47 ? ch - 0x37 : ch - 0x57);
    }

    inline byte bcdToByte(byte bcd)
    {
        return (bcd >> 4) * 10 + (bcd & 0xF);
    }

    bool hexStringToByteArray(std::string hexString, byte* byteArray);

    void shiftByteArray(const byte* ptr, byte* buf, byte length, bool bigEndian = false, byte toBits = 3);

    /*
        reads up to 8 bytes from any pointer into a variable of appropriate type
    */

    template <typename T>
    T readBytes(const byte *ptr, bool bigEndian = false, byte length = 0)
    {
        if (length > sizeof(T) || length < 1) length = sizeof(T);

        T result = 0;

        byte offset = 0;
        char multiplier = -1;

        if (bigEndian)
        {
            offset = length * 8 - 8;
            multiplier = 1;
        }

        for (byte i = 0; i < length; i++)
            result |= static_cast<T>(*(ptr + i)) << (offset - (i * 8 * multiplier));

        return result;
    }

    template <typename T>
    T bcdArrayToInt(const byte* input, byte length = 2)
    {
        T result = 0;
    
        while (length--)
        {
            result = result * 100 + (*input >> 4) * 10 + (*input & 0xF);
            input++;
        }
    
        return result;
    }

    std::string stringToHex(const std::string& input);
    std::string byteArrayToHex(const byte* input, size_t len);
    std::string byteArrayToString(const byte* input, size_t length);
};
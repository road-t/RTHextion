#include <QTest>

#include "Datas.h"
#include "byteglue.h"

class TstDatas : public QObject
{
    Q_OBJECT

private slots:

    // ---- ByteOrder: readWord ----

    void readWordLittleEndian()
    {
        Datas d = {};
        // LE 0x0102 => bytes: 0x02, 0x01
        const_cast<char *>(d.chr)[0] = 0x02;
        const_cast<char *>(d.chr)[1] = 0x01;
        QCOMPARE(readWord(d, ByteOrder::LittleEndian), quint16(0x0102));
    }

    void readWordBigEndian()
    {
        Datas d = {};
        const_cast<char *>(d.chr)[0] = 0x01;
        const_cast<char *>(d.chr)[1] = 0x02;
        QCOMPARE(readWord(d, ByteOrder::BigEndian), quint16(0x0102));
    }

    void readWordSwappedBytes()
    {
        Datas d = {};
        // SwappedBytes: bytes [0][1] → 0x[1][0]
        const_cast<char *>(d.chr)[0] = 0x37;
        const_cast<char *>(d.chr)[1] = 0x80;
        QCOMPARE(readWord(d, ByteOrder::SwappedBytes), quint16(0x8037));
    }

    // ---- ByteOrder: readDword ----

    void readDwordLittleEndian()
    {
        Datas d = {};
        // LE 0x04030201 => bytes: 01 02 03 04
        const_cast<char *>(d.chr)[0] = 0x01;
        const_cast<char *>(d.chr)[1] = 0x02;
        const_cast<char *>(d.chr)[2] = 0x03;
        const_cast<char *>(d.chr)[3] = 0x04;
        QCOMPARE(readDword(d, ByteOrder::LittleEndian), quint32(0x04030201));
    }

    void readDwordBigEndian()
    {
        Datas d = {};
        const_cast<char *>(d.chr)[0] = 0x01;
        const_cast<char *>(d.chr)[1] = 0x02;
        const_cast<char *>(d.chr)[2] = 0x03;
        const_cast<char *>(d.chr)[3] = 0x04;
        QCOMPARE(readDword(d, ByteOrder::BigEndian), quint32(0x01020304));
    }

    void readDwordSwappedBytes()
    {
        Datas d = {};
        // SwappedBytes: bytes CD AB 12 EF → value 0xABCDEF12
        const_cast<char *>(d.chr)[0] = char(0xCD);
        const_cast<char *>(d.chr)[1] = char(0xAB);
        const_cast<char *>(d.chr)[2] = char(0x12);
        const_cast<char *>(d.chr)[3] = char(0xEF);
        QCOMPARE(readDword(d, ByteOrder::SwappedBytes), quint32(0xABCDEF12));
    }

    // ---- decodePointer ----

    void decodePointer16LE()
    {
        uchar bytes[] = {0x34, 0x12};
        QCOMPARE(decodePointer(bytes, 2, ByteOrder::LittleEndian), quint64(0x1234));
    }

    void decodePointer16BE()
    {
        uchar bytes[] = {0x12, 0x34};
        QCOMPARE(decodePointer(bytes, 2, ByteOrder::BigEndian), quint64(0x1234));
    }

    void decodePointer32LE()
    {
        uchar bytes[] = {0x78, 0x56, 0x34, 0x12};
        QCOMPARE(decodePointer(bytes, 4, ByteOrder::LittleEndian), quint64(0x12345678));
    }

    void decodePointer32BE()
    {
        uchar bytes[] = {0x12, 0x34, 0x56, 0x78};
        QCOMPARE(decodePointer(bytes, 4, ByteOrder::BigEndian), quint64(0x12345678));
    }

    // ---- byteglue::charToByte ----

    void charToByteDigits()
    {
        QCOMPARE(byteglue::charToByte('0'), byte(0));
        QCOMPARE(byteglue::charToByte('9'), byte(9));
    }

    void charToByteLowerHex()
    {
        QCOMPARE(byteglue::charToByte('a'), byte(10));
        QCOMPARE(byteglue::charToByte('f'), byte(15));
    }

    void charToByteUpperHex()
    {
        QCOMPARE(byteglue::charToByte('A'), byte(10));
        QCOMPARE(byteglue::charToByte('F'), byte(15));
    }

    // ---- byteglue::hexStringToByteArray ----

    void hexStringToByteArray()
    {
        byte output[3];
        bool ok = byteglue::hexStringToByteArray("FF00AB", output);
        QVERIFY(ok);
        QCOMPARE(output[0], byte(0xFF));
        QCOMPARE(output[1], byte(0x00));
        QCOMPARE(output[2], byte(0xAB));
    }

    void hexStringOddLengthFails()
    {
        byte output[2];
        bool ok = byteglue::hexStringToByteArray("FFA", output);
        QVERIFY(!ok);
    }

    // ---- byteglue::readBytes ----

    void readBytes16LE()
    {
        byte data[] = {0x34, 0x12};
        auto val = byteglue::readBytes<uint16_t>(data, false);
        QCOMPARE(val, uint16_t(0x1234));
    }

    void readBytes16BE()
    {
        byte data[] = {0x12, 0x34};
        auto val = byteglue::readBytes<uint16_t>(data, true);
        QCOMPARE(val, uint16_t(0x1234));
    }

    void readBytes32LE()
    {
        byte data[] = {0x78, 0x56, 0x34, 0x12};
        auto val = byteglue::readBytes<uint32_t>(data, false);
        QCOMPARE(val, uint32_t(0x12345678));
    }

    // ---- byteglue::bcdToByte ----

    void bcdToByte()
    {
        QCOMPARE(byteglue::bcdToByte(0x42), byte(42));
        QCOMPARE(byteglue::bcdToByte(0x99), byte(99));
        QCOMPARE(byteglue::bcdToByte(0x00), byte(0));
    }

    // ---- byteglue::bcdArrayToInt ----

    void bcdArrayToInt()
    {
        byte data[] = {0x12, 0x34};
        auto val = byteglue::bcdArrayToInt<uint32_t>(data, 2);
        QCOMPARE(val, uint32_t(1234));
    }

    // ---- byteglue::byteArrayToHex ----

    void byteArrayToHex()
    {
        byte data[] = {0xFF, 0x00, 0xAB};
        std::string result = byteglue::byteArrayToHex(data, 3);
        QCOMPARE(result, std::string("FF00AB"));
    }
};

QTEST_APPLESS_MAIN(TstDatas)
#include "tst_datas.moc"

#ifndef ROMDETECT_H
#define ROMDETECT_H

#include <QString>
#include <QByteArray>
#include <QFileInfo>
#include <QHash>
#include "Datas.h"

/// Detected ROM platform type (extensible for future header parsing).
enum class RomType : int {
    Unknown = 0,
    NES,        // iNES / NES 2.0
    SNES,       // Super Nintendo
    GB,         // Game Boy
    GBC,        // Game Boy Color
    GBA,        // Game Boy Advance
    MD,         // Mega Drive / Genesis
    X32,        // Sega 32X
    N64,        // Nintendo 64 (.z64 big-endian)
    N64_LE,     // Nintendo 64 (.n64 little-endian)
    N64_V64,    // Nintendo 64 (.v64 byte-swapped)
    SMS,        // Sega Master System / Mark III
    GG,         // Sega Game Gear
    SG1000,     // Sega SG-1000 / Othello Multivision
    WonderSwan, // Bandai WonderSwan
    WonderSwanColor, // Bandai WonderSwan Color
    ColecoVision, // ColecoVision
    Atari2600,  // Atari 2600
    Atari5200,  // Atari 5200
    Atari7800,  // Atari 7800
};

enum class RomExt : int {
    Unknown = 0,
    NES, UNF, UNIF,
    SMC, SFC, SWC, FIG,
    GB, GBC, CGB,
    GBA, AGB,
    MD, GEN, SMD,
    BIN,
    X32,
    Z64, V64, N64,
    SMS, GG, SG, MV,
    WS, WSC, PC2,
    COL, CV,
    A26, A52, A78,
    XFD, ATR, ATX, CDM, CAS, XEX,
    ROM,
};

inline RomExt classifyRomExtension(const QString &ext)
{
    static const QHash<QString, RomExt> kExtMap = {
        {"nes", RomExt::NES}, {"unf", RomExt::UNF}, {"unif", RomExt::UNIF},
        {"smc", RomExt::SMC}, {"sfc", RomExt::SFC}, {"swc", RomExt::SWC}, {"fig", RomExt::FIG},
        {"gb", RomExt::GB}, {"gbc", RomExt::GBC}, {"cgb", RomExt::CGB},
        {"gba", RomExt::GBA}, {"agb", RomExt::AGB},
        {"md", RomExt::MD}, {"gen", RomExt::GEN}, {"smd", RomExt::SMD},
        {"bin", RomExt::BIN},
        {"32x", RomExt::X32},
        {"z64", RomExt::Z64}, {"v64", RomExt::V64}, {"n64", RomExt::N64},
        {"sms", RomExt::SMS}, {"gg", RomExt::GG}, {"sg", RomExt::SG}, {"mv", RomExt::MV},
        {"ws", RomExt::WS}, {"wsc", RomExt::WSC}, {"pc2", RomExt::PC2},
        {"col", RomExt::COL}, {"cv", RomExt::CV},
        {"a26", RomExt::A26}, {"a52", RomExt::A52}, {"a78", RomExt::A78},
        {"xfd", RomExt::XFD}, {"atr", RomExt::ATR}, {"atx", RomExt::ATX}, {"cdm", RomExt::CDM}, {"cas", RomExt::CAS}, {"xex", RomExt::XEX},
        {"rom", RomExt::ROM},
    };

    return kExtMap.value(ext, RomExt::Unknown);
}

inline RomType detectN64ByHeader(const QByteArray &header)
{
    if (header.size() < 4)
        return RomType::Unknown;

    const uchar *h = reinterpret_cast<const uchar *>(header.constData());

    // z64: big-endian
    if (h[0] == 0x80 && h[1] == 0x37 && h[2] == 0x12 && h[3] == 0x40)
        return RomType::N64;

    // v64: byte-swapped
    if (h[0] == 0x37 && h[1] == 0x80 && h[2] == 0x40 && h[3] == 0x12)
        return RomType::N64_V64;

    // n64: little-endian
    if (h[0] == 0x40 && h[1] == 0x12 && h[2] == 0x37 && h[3] == 0x80)
        return RomType::N64_LE;

    return RomType::Unknown;
}

/// Detect ROM type from filename extension and (optionally) header bytes.
/// @param filePath  Full path to file being opened.
/// @param header    First ≥ 512 bytes of the file (as much as available).
/// @return Detected ROM type, or RomType::Unknown if unrecognised.
inline RomType detectRomType(const QString &filePath, const QByteArray &header)
{
    const QString ext = QFileInfo(filePath).suffix().toLower();
    const RomExt extType = classifyRomExtension(ext);

    switch (extType) {
    case RomExt::NES:
    case RomExt::UNF:
    case RomExt::UNIF:
        return RomType::NES;

    case RomExt::SMC:
    case RomExt::SFC:
    case RomExt::SWC:
    case RomExt::FIG:
        return RomType::SNES;

    case RomExt::GB:
        return RomType::GB;
    case RomExt::GBC:
    case RomExt::CGB:
        return RomType::GBC;

    case RomExt::GBA:
    case RomExt::AGB:
        return RomType::GBA;

    case RomExt::MD:
    case RomExt::GEN:
    case RomExt::SMD:
        return RomType::MD;

    case RomExt::X32:
        return RomType::X32;

    case RomExt::Z64: {
        const RomType byHeader = detectN64ByHeader(header);
        return (byHeader == RomType::Unknown) ? RomType::N64 : byHeader;
    }

    case RomExt::V64: {
        const RomType byHeader = detectN64ByHeader(header);
        return (byHeader == RomType::Unknown) ? RomType::N64_V64 : byHeader;
    }

    case RomExt::N64: {
        const RomType byHeader = detectN64ByHeader(header);
        return (byHeader == RomType::Unknown) ? RomType::N64_LE : byHeader;
    }

    case RomExt::SMS:
        return RomType::SMS;
    case RomExt::GG:
        return RomType::GG;
    case RomExt::SG:
    case RomExt::MV:
        return RomType::SG1000;

    case RomExt::WS:
        return RomType::WonderSwan;
    case RomExt::WSC:
    case RomExt::PC2:
        return RomType::WonderSwanColor;

    case RomExt::COL:
    case RomExt::CV:
        return RomType::ColecoVision;

    case RomExt::A26:
        return RomType::Atari2600;
    case RomExt::A52:
        return RomType::Atari5200;
    case RomExt::A78:
        return RomType::Atari7800;

    case RomExt::XFD:
    case RomExt::ATR:
    case RomExt::ATX:
    case RomExt::CDM:
    case RomExt::CAS:
    case RomExt::XEX:
        return RomType::Atari5200;

    case RomExt::BIN: {
        const RomType n64Type = detectN64ByHeader(header);
        if (n64Type != RomType::Unknown)
            return n64Type;

        if (header.size() >= 0x104 && header.mid(0x100, 4) == "SEGA")
            return RomType::MD;

        return RomType::Unknown;
    }

    case RomExt::ROM: {
        if (header.size() >= 0x104 && header.mid(0x100, 4) == "SEGA")
            return RomType::MD;

        return RomType::Unknown;
    }

    default:
        return RomType::Unknown;
    }
}

/// Return the default/expected byte order for the given ROM type.
inline ByteOrder defaultByteOrder(RomType type)
{
    switch (type) {
    case RomType::NES:
    case RomType::SNES:
    case RomType::GB:
    case RomType::GBC:
    case RomType::GBA:
    case RomType::N64_LE:
    case RomType::SMS:
    case RomType::GG:
    case RomType::SG1000:
    case RomType::WonderSwan:
    case RomType::WonderSwanColor:
    case RomType::ColecoVision:
    case RomType::Atari2600:
    case RomType::Atari5200:
    case RomType::Atari7800:
        return ByteOrder::LittleEndian;

    case RomType::MD:
    case RomType::X32:
    case RomType::N64:
        return ByteOrder::BigEndian;

    case RomType::N64_V64:
        return ByteOrder::SwappedBytes;

    default:
        return ByteOrder::LittleEndian;
    }
}

#endif // ROMDETECT_H

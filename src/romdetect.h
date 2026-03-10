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
    NES,             // iNES / NES 2.0
    SNES,            // Super Nintendo LoROM, headerless (.sfc)
    SNES_SMC,        // SNES LoROM + 512-byte copier header (.smc/.swc/.fig)
    SNES_HIROM,      // SNES HiROM, headerless (.sfc)
    SNES_HIROM_SMC,  // SNES HiROM + 512-byte copier header (.smc/.swc/.fig)
    GB,              // Game Boy
    GBC,             // Game Boy Color
    GBA,             // Game Boy Advance
    MD,              // Mega Drive / Genesis
    X32,             // Sega 32X
    N64,             // Nintendo 64 (.z64 big-endian)
    N64_LE,          // Nintendo 64 (.n64 little-endian)
    N64_V64,         // Nintendo 64 (.v64 byte-swapped)
    SMS,             // Sega Master System / Mark III
    GG,              // Sega Game Gear
    SG1000,          // Sega SG-1000 / Othello Multivision
    WonderSwan,      // Bandai WonderSwan
    WonderSwanColor, // Bandai WonderSwan Color
    ColecoVision,    // ColecoVision
    Atari2600,       // Atari 2600
    Atari5200,       // Atari 5200
    Atari7800,       // Atari 7800
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
    case RomExt::FIG: {
        // A 512-byte copier header is present when file size % 1024 == 512
        const qint64 fileSize = QFileInfo(filePath).size();
        if (fileSize > 512 && (fileSize % 1024) == 512)
            return RomType::SNES_SMC;
        return RomType::SNES;
    }

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

/// Number of distinct RomType values (including Unknown).
constexpr int kRomTypeCount = static_cast<int>(RomType::Atari7800) + 1;

/// Human-readable short name for the ROM type (not translated — used as identifier).
inline const char *romTypeName(RomType type)
{
    switch (type) {
    case RomType::NES:              return "NES";
    case RomType::SNES:             return "SNES";
    case RomType::SNES_SMC:         return "SNES (Headered)";
    case RomType::SNES_HIROM:       return "SNES HiROM";
    case RomType::SNES_HIROM_SMC:   return "SNES HiROM (Headered)";
    case RomType::GB:               return "Game Boy";
    case RomType::GBC:              return "Game Boy Color";
    case RomType::GBA:              return "Game Boy Advance";
    case RomType::MD:               return "Mega Drive";
    case RomType::X32:              return "Sega 32X";
    case RomType::N64:              return "N64 (z64)";
    case RomType::N64_LE:           return "N64 (n64)";
    case RomType::N64_V64:          return "N64 (v64)";
    case RomType::SMS:              return "Master System";
    case RomType::GG:               return "Game Gear";
    case RomType::SG1000:           return "SG-1000";
    case RomType::WonderSwan:       return "WonderSwan";
    case RomType::WonderSwanColor:  return "WonderSwan Color";
    case RomType::ColecoVision:     return "ColecoVision";
    case RomType::Atari2600:        return "Atari 2600";
    case RomType::Atari5200:        return "Atari 5200";
    case RomType::Atari7800:        return "Atari 7800";
    default:                        return "Unknown";
    }
}

/// Default pointer size in bytes for the ROM type.
///  - 2 for 8/16-bit platforms (NES, SNES, GB, SMS, etc.)
///  - 4 for 32-bit platforms (GBA, MD, N64, 32X, etc.)
inline int defaultPointerSize(RomType type)
{
    switch (type) {
    case RomType::NES:
    case RomType::SNES:
    case RomType::SNES_SMC:
    case RomType::SNES_HIROM:
    case RomType::SNES_HIROM_SMC:
    case RomType::GB:
    case RomType::GBC:
    case RomType::SMS:
    case RomType::GG:
    case RomType::SG1000:
    case RomType::WonderSwan:
    case RomType::WonderSwanColor:
    case RomType::ColecoVision:
    case RomType::Atari2600:
        return 2;

    case RomType::GBA:
    case RomType::MD:
    case RomType::X32:
    case RomType::N64:
    case RomType::N64_LE:
    case RomType::N64_V64:
    case RomType::Atari5200:
    case RomType::Atari7800:
        return 4;

    default:
        return 4;
    }
}

/// Default pointer offset for the ROM type.
///
/// When a pointer is stored in a ROM, the value it contains typically refers to
/// an address in the CPU's memory map, not the file offset.  The *pointer offset*
/// is the signed value that must be added to a decoded pointer value to obtain the
/// file offset:   file_offset = decoded_pointer + pointer_offset
///
/// Common examples:
///  NES (iNES):  CPU address space puts PRG-ROM at $8000.  The iNES header is
///               16 ($10) bytes, so  file_offset = ptr - $8000 + $10 → offset = -$7FF0
///  SNES LoROM:  Bank $00–$7D, $80–$FF with ROM starting at $8000 in each bank.
///               For a headerless LoROM image:  offset = -$8000  (simplified, ignoring banks).
///  GBA:         ROM is mapped at $08000000. offset = -$08000000
///  Mega Drive:  ROM is mapped at $000000 — no offset needed.
///  GB/GBC:      ROM bank 0 at $0000.  Pointer == file offset. offset = 0.
///  N64 (z64):   ROM image mapped at $B0000000 (PI), but text pointers commonly
///               use offsets relative to the start of the ROM, so offset = 0.
inline qint64 defaultPointerOffset(RomType type)
{
    switch (type) {
    case RomType::NES:              return -0x7FF0;  // -($8000 - $10) — iNES 16-byte header
    case RomType::SNES:             return -0x8000;    // LoROM headerless (.sfc)
    case RomType::SNES_SMC:         return -0x7E00;    // LoROM + 512-byte copier header (-$8000 + $200)
    case RomType::SNES_HIROM:       return -0xC00000LL; // HiROM at $C0:0000, headerless
    case RomType::SNES_HIROM_SMC:   return -0xBFE00LL;  // HiROM + 512-byte copier header (-$C00000 + $200)
    case RomType::GB:               return 0;
    case RomType::GBC:              return 0;
    case RomType::GBA:              return -0x08000000LL;
    case RomType::MD:               return 0;
    case RomType::X32:              return 0;
    // N64: ROM is copied to KSEG0 RAM at $80000000; absolute pointer offset = -$80000000.
    // However many games use ROM-relative or segment-relative references, so 0 is the safest default.
    case RomType::N64:              return 0;
    case RomType::N64_LE:           return 0;
    case RomType::N64_V64:          return 0;
    case RomType::SMS:              return 0;
    case RomType::GG:               return 0;
    case RomType::SG1000:           return 0;
    case RomType::WonderSwan:       return 0;
    case RomType::WonderSwanColor:  return 0;
    case RomType::ColecoVision:     return -0x8000;
    case RomType::Atari2600:        return -0xF000;  // 4K ROM at $F000
    case RomType::Atari5200:        return 0;
    case RomType::Atari7800:        return 0;
    default:                        return 0;
    }
}

/// Return the default/expected byte order for the given ROM type.
inline ByteOrder defaultByteOrder(RomType type)
{
    switch (type) {
    case RomType::NES:
    case RomType::SNES:
    case RomType::SNES_SMC:
    case RomType::SNES_HIROM:
    case RomType::SNES_HIROM_SMC:
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

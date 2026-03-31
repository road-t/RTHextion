#ifndef ROMCHECKSUM_H
#define ROMCHECKSUM_H

#include <QByteArray>
#include <QString>
#include <QCoreApplication>
#include <cstdint>
#include "romdetect.h"

/// Result status for a checksum fix operation.
enum class ChecksumFixStatus {
    OK,             ///< Checksum was already correct; no bytes changed.
    Fixed,          ///< Checksum was wrong; bytes in \a data have been corrected.
    NotApplicable,  ///< This ROM type has no standard checksum (e.g. NES, N64).
    TooSmall,       ///< The data buffer is too small to contain the checksum field.
};

struct ChecksumFixResult {
    ChecksumFixStatus status;
    QString message;
};

// ─────────────────────────────────────────────────────────────────────────────
// Mega Drive / Genesis
// Checksum: big-endian uint16 at 0x018E–0x018F
// Computed : sum of all big-endian words from 0x0200 to end of ROM
// ─────────────────────────────────────────────────────────────────────────────
inline ChecksumFixResult fixMegaDriveChecksum(QByteArray &data)
{
    if (data.size() < 0x0200)
        return {ChecksumFixStatus::TooSmall,
                QCoreApplication::translate("romchecksum", "ROM too small for Mega Drive checksum (need ≥ 512 bytes)")};

    uint16_t cs = 0;
    const int len = data.size();
    for (int i = 0x0200; i + 1 < len; i += 2)
        cs += (static_cast<uint16_t>(static_cast<uint8_t>(data[i])) << 8)
              | static_cast<uint8_t>(data[i + 1]);
    // Trailing odd byte counts as the high byte of a half-word
    if ((len & 1) != 0)
        cs += static_cast<uint16_t>(static_cast<uint8_t>(data[len - 1])) << 8;

    const uint16_t stored =
        (static_cast<uint16_t>(static_cast<uint8_t>(data[0x018E])) << 8)
        | static_cast<uint8_t>(data[0x018F]);

    if (stored == cs)
        return {ChecksumFixStatus::OK,
                QCoreApplication::translate("romchecksum", "Mega Drive checksum is already correct")};

    data[0x018E] = static_cast<char>((cs >> 8) & 0xFF);
    data[0x018F] = static_cast<char>(cs & 0xFF);
    return {ChecksumFixStatus::Fixed,
            QCoreApplication::translate("romchecksum", "Mega Drive checksum fixed")};
}

// ─────────────────────────────────────────────────────────────────────────────
// Master System / Game Gear
// The Sega header "TMR SEGA" is located at 0x1FF0 (8 KB), 0x3FF0 (16 KB),
// or 0x7FF0 (32 KB+). The little-endian checksum lives at header+0x0A–0x0B
// and covers all bytes from 0x0000 to (headerOffset − 1).
// ─────────────────────────────────────────────────────────────────────────────
inline ChecksumFixResult fixSMSChecksum(QByteArray &data)
{
    static const char kTmrSega[] = "TMR SEGA";  // 8 bytes, no null needed
    static const int kPositions[] = {0x1FF0, 0x3FF0, 0x7FF0};

    int headerOffset = -1;
    for (int pos : kPositions) {
        if (data.size() >= pos + 16
            && data.mid(pos, 8) == QByteArray(kTmrSega, 8)) {
            headerOffset = pos;   // keep the last (highest) match
        }
    }
    if (headerOffset < 0)
        return {ChecksumFixStatus::NotApplicable,
                QCoreApplication::translate("romchecksum", "Sega ROM header (\"TMR SEGA\") not found")};

    uint16_t cs = 0;
    for (int i = 0; i < headerOffset; ++i)
        cs += static_cast<uint8_t>(data[i]);

    const uint16_t stored =
        static_cast<uint8_t>(data[headerOffset + 0x0A])
        | (static_cast<uint16_t>(static_cast<uint8_t>(data[headerOffset + 0x0B])) << 8);

    if (stored == cs)
        return {ChecksumFixStatus::OK,
                QCoreApplication::translate("romchecksum", "SMS/GG checksum is already correct")};

    data[headerOffset + 0x0A] = static_cast<char>(cs & 0xFF);
    data[headerOffset + 0x0B] = static_cast<char>((cs >> 8) & 0xFF);
    return {ChecksumFixStatus::Fixed,
            QCoreApplication::translate("romchecksum", "SMS/GG checksum fixed")};
}

// ─────────────────────────────────────────────────────────────────────────────
// Game Boy / Game Boy Color
// Header checksum : byte at 0x014D
//   x = 0; for i in 0x0134..0x014C: x -= (ROM[i] + 1); result = x & 0xFF
// Global checksum : big-endian uint16 at 0x014E–0x014F
//   Sum of all bytes except 0x014E and 0x014F
// ─────────────────────────────────────────────────────────────────────────────
inline ChecksumFixResult fixGameBoyChecksum(QByteArray &data)
{
    if (data.size() < 0x0150)
        return {ChecksumFixStatus::TooSmall,
                QCoreApplication::translate("romchecksum", "ROM too small for Game Boy checksum (need ≥ 0x150 bytes)")};

    // Header checksum
    int x = 0;
    for (int i = 0x0134; i <= 0x014C; ++i)
        x = x - static_cast<uint8_t>(data[i]) - 1;
    const uint8_t headerCs = static_cast<uint8_t>(x & 0xFF);

    bool anyFixed = false;
    if (static_cast<uint8_t>(data[0x014D]) != headerCs) {
        data[0x014D] = static_cast<char>(headerCs);
        anyFixed = true;
    }

    // Global checksum
    uint16_t globalCs = 0;
    for (int i = 0; i < data.size(); ++i) {
        if (i == 0x014E || i == 0x014F) continue;
        globalCs += static_cast<uint8_t>(data[i]);
    }
    const uint16_t storedGlobal =
        (static_cast<uint16_t>(static_cast<uint8_t>(data[0x014E])) << 8)
        | static_cast<uint8_t>(data[0x014F]);
    if (storedGlobal != globalCs) {
        data[0x014E] = static_cast<char>((globalCs >> 8) & 0xFF);
        data[0x014F] = static_cast<char>(globalCs & 0xFF);
        anyFixed = true;
    }

    if (!anyFixed)
        return {ChecksumFixStatus::OK,
                QCoreApplication::translate("romchecksum", "Game Boy checksums are already correct")};
    return {ChecksumFixStatus::Fixed,
            QCoreApplication::translate("romchecksum", "Game Boy checksum(s) fixed")};
}

// ─────────────────────────────────────────────────────────────────────────────
// Game Boy Advance
// Header checksum : byte at 0x00BD
//   sum = Σ bytes[0xA0..0xBC]; checksum = (~sum - 0x19) & 0xFF
// (No global ROM checksum in GBA format.)
// ─────────────────────────────────────────────────────────────────────────────
inline ChecksumFixResult fixGBAChecksum(QByteArray &data)
{
    if (data.size() < 0x00BE)
        return {ChecksumFixStatus::TooSmall,
                QCoreApplication::translate("romchecksum", "ROM too small for GBA header checksum (need ≥ 0xBE bytes)")};

    int sum = 0;
    for (int i = 0x00A0; i <= 0x00BC; ++i)
        sum += static_cast<uint8_t>(data[i]);
    const uint8_t cs = static_cast<uint8_t>((~sum - 0x19) & 0xFF);

    if (static_cast<uint8_t>(data[0x00BD]) == cs)
        return {ChecksumFixStatus::OK,
                QCoreApplication::translate("romchecksum", "GBA header checksum is already correct")};

    data[0x00BD] = static_cast<char>(cs);
    return {ChecksumFixStatus::Fixed,
            QCoreApplication::translate("romchecksum", "GBA header checksum fixed")};
}

// ─────────────────────────────────────────────────────────────────────────────
// SNES (LoROM / HiROM, with or without 512-byte copier header)
// Internal header location:
//   LoROM:  0x7FDC–0x7FDF  (relative to ROM data start, after copier header)
//   HiROM:  0xFFDC–0xFFDF
// Layout: [complement lo][complement hi][checksum lo][checksum hi]
// checksum  = Σ all ROM bytes, treating the 4 checksum bytes as 0xFF,0xFF,0x00,0x00
// complement = checksum XOR 0xFFFF
// ─────────────────────────────────────────────────────────────────────────────
inline ChecksumFixResult fixSNESChecksum(QByteArray &data, RomType romType)
{
    const bool hasCopierHeader = (romType == RomType::SNES_SMC
                                  || romType == RomType::SNES_HIROM_SMC);
    const bool isHiROM         = (romType == RomType::SNES_HIROM
                                  || romType == RomType::SNES_HIROM_SMC);
    const int  romBase = hasCopierHeader ? 512 : 0;

    if (data.size() - romBase < 0x8000)
        return {ChecksumFixStatus::TooSmall,
                QCoreApplication::translate("romchecksum", "ROM data too small for SNES checksum")};

    const int csOff = romBase + (isHiROM ? 0xFFDC : 0x7FDC);
    if (data.size() < csOff + 4)
        return {ChecksumFixStatus::TooSmall,
                QCoreApplication::translate("romchecksum", "ROM too small to contain SNES checksum fields")};

    // Sum all ROM bytes with the 4 checksum bytes substituted
    // (complement = 0xFF,0xFF  checksum = 0x00,0x00)
    uint16_t cs = 0;
    for (int i = romBase; i < data.size(); ++i) {
        if (i == csOff || i == csOff + 1)
            cs += 0xFF;
        else if (i == csOff + 2 || i == csOff + 3)
            cs += 0x00;
        else
            cs += static_cast<uint8_t>(data[i]);
    }
    const uint16_t comp = cs ^ 0xFFFF;

    const uint16_t storedComp =
        static_cast<uint8_t>(data[csOff])
        | (static_cast<uint16_t>(static_cast<uint8_t>(data[csOff + 1])) << 8);
    const uint16_t storedCs =
        static_cast<uint8_t>(data[csOff + 2])
        | (static_cast<uint16_t>(static_cast<uint8_t>(data[csOff + 3])) << 8);

    if (storedComp == comp && storedCs == cs)
        return {ChecksumFixStatus::OK,
                QCoreApplication::translate("romchecksum", "SNES checksum is already correct")};

    data[csOff + 0] = static_cast<char>(comp & 0xFF);
    data[csOff + 1] = static_cast<char>((comp >> 8) & 0xFF);
    data[csOff + 2] = static_cast<char>(cs & 0xFF);
    data[csOff + 3] = static_cast<char>((cs >> 8) & 0xFF);
    return {ChecksumFixStatus::Fixed,
            QCoreApplication::translate("romchecksum", "SNES checksum fixed")};
}

// ─────────────────────────────────────────────────────────────────────────────
// Dispatcher: pick the right fixer for the detected ROM type.
// Returns NotApplicable for types with no standard checksum (NES, N64, etc.).
// ─────────────────────────────────────────────────────────────────────────────
inline ChecksumFixResult tryFixChecksum(QByteArray &data, RomType romType)
{
    switch (romType) {
    case RomType::MD:
    case RomType::X32:
        return fixMegaDriveChecksum(data);

    case RomType::SMS:
    case RomType::GG:
        return fixSMSChecksum(data);

    case RomType::GB:
    case RomType::GBC:
        return fixGameBoyChecksum(data);

    case RomType::GBA:
        return fixGBAChecksum(data);

    case RomType::SNES:
    case RomType::SNES_SMC:
    case RomType::SNES_HIROM:
    case RomType::SNES_HIROM_SMC:
        return fixSNESChecksum(data, romType);

    default:
        return {ChecksumFixStatus::NotApplicable,
                QCoreApplication::translate("romchecksum", "No standard checksum defined for this ROM type")};
    }
}

#endif // ROMCHECKSUM_H

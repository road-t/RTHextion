#include "audiodetector.h"
#include <QtMath>
#include <algorithm>
#include <cstring>

AudioDetector::AudioDetector() {}

// ─── Main dispatcher ───────────────────────────────────────────────

QVector<DetectedSample> AudioDetector::detect(const QByteArray &data, RomType romType) const
{
    QVector<DetectedSample> results;

    switch (romType) {
    case RomType::SNES:
    case RomType::SNES_SMC:
    case RomType::SNES_HIROM:
    case RomType::SNES_HIROM_SMC:
        results = detectBRR(data);
        break;

    case RomType::NES:
        results = detectDPCM(data);
        break;

    case RomType::MD:
    case RomType::X32:
        results = detectMD_DAC(data);
        results += detectMD_Signed8(data);
        results += detectMD_DPCM4(data);

        // Remove strongly-overlapping duplicates, keep higher confidence.
        if (results.size() > 1) {
            std::sort(results.begin(), results.end(),
                      [](const DetectedSample &a, const DetectedSample &b) {
                          if (a.offset != b.offset)
                              return a.offset < b.offset;
                          return a.confidence > b.confidence;
                      });

            QVector<DetectedSample> merged;
            for (const auto &cand : std::as_const(results)) {
                bool replaced = false;
                for (auto &keep : merged) {
                    const qint64 a0 = keep.offset;
                    const qint64 a1 = keep.offset + keep.length;
                    const qint64 b0 = cand.offset;
                    const qint64 b1 = cand.offset + cand.length;
                    const qint64 ov = qMax<qint64>(0, qMin(a1, b1) - qMax(a0, b0));
                    if (ov <= 0)
                        continue;
                    const qint64 minLen = qMin(keep.length, cand.length);
                    if (minLen <= 0)
                        continue;
                    const double ratio = static_cast<double>(ov) / static_cast<double>(minLen);
                    if (ratio >= 0.80) {
                        if (cand.confidence > keep.confidence)
                            keep = cand;
                        replaced = true;
                        break;
                    }
                }
                if (!replaced)
                    merged.append(cand);
            }
            results = merged;
        }
        break;

    case RomType::GBA:
        results = detectGBA_PCM(data);
        break;

    default:
        // Try generic PCM detection for unknown types
        results = detectMD_DAC(data);
        break;
    }

    // Sort by offset
    std::sort(results.begin(), results.end(),
              [](const DetectedSample &a, const DetectedSample &b) {
                  return a.offset < b.offset;
              });

    return results;
}

// ═══════════════════════════════════════════════════════════════════
//  SNES BRR Detection
// ═══════════════════════════════════════════════════════════════════

QVector<DetectedSample> AudioDetector::detectBRR(const QByteArray &data) const
{
    QVector<DetectedSample> results;
    const int size = data.size();
    if (size < 9)
        return results;

    const uint8_t *d = reinterpret_cast<const uint8_t *>(data.constData());

    // Track which offsets we've already claimed as part of a sample
    QVector<bool> claimed(size, false);

    // Scan for contiguous runs of valid 9-byte BRR blocks
    for (int pos = 0; pos + 9 <= size; ) {
        // Check if this could be a BRR block header
        const uint8_t header = d[pos];
        const int range = (header >> 4) & 0x0F;

        if (range > 12 || claimed[pos]) {
            ++pos;
            continue;
        }

        // Try to walk a chain of valid BRR blocks from this position
        int blockCount = 0;
        int chainEnd = pos;
        bool foundEnd = false;
        bool hasLoop = false;

        for (int bp = pos; bp + 9 <= size; bp += 9) {
            const uint8_t hdr = d[bp];
            const int r = (hdr >> 4) & 0x0F;
            if (r > 12)
                break;  // invalid range

            ++blockCount;
            chainEnd = bp + 9;

            if (hdr & 0x01) {
                // END flag set
                foundEnd = true;
                hasLoop = (hdr & 0x02) != 0;
                break;
            }
        }

        const int chainBytes = chainEnd - pos;

        // Minimum: need at least a few blocks to be a real sample
        const int minBlocks = qMax(2, m_minSampleBytes / 9);
        if (blockCount < minBlocks) {
            pos += 9;
            continue;
        }

        // Calculate confidence based on chain length and end flag
        float confidence = 0.0f;
        if (foundEnd)
            confidence += 0.4f;
        if (blockCount >= 8)
            confidence += 0.2f;
        if (blockCount >= 32)
            confidence += 0.2f;

        // Validate that nibble data looks audio-like: check filter distribution
        int filterCounts[4] = {0, 0, 0, 0};
        for (int bp = pos; bp < chainEnd; bp += 9) {
            filterCounts[(d[bp] >> 2) & 0x03]++;
        }
        // Real audio tends to use filters 1-3 (predictive); pure zero often uses filter 0
        if (blockCount > 4) {
            const float filterDiv = (filterCounts[1] + filterCounts[2] + filterCounts[3])
                                    / static_cast<float>(blockCount);
            if (filterDiv > 0.3f)
                confidence += 0.2f;  // uses predictive filters → likely real audio
        }

        if (confidence < m_minConfidence) {
            pos += 9;
            continue;
        }

        // Mark as claimed
        for (int i = pos; i < chainEnd && i < size; ++i)
            claimed[i] = true;

        DetectedSample sample;
        sample.offset = pos;
        sample.length = chainBytes;
        sample.format = AudioSampleFormat::SNES_BRR;
        sample.sampleRate = 32000;  // SNES native rate
        sample.hasLoop = hasLoop;
        sample.confidence = confidence;
        sample.name = QStringLiteral("BRR Sample @ 0x%1 (%2 blocks)")
                          .arg(pos, 0, 16).arg(blockCount);
        results.append(sample);

        pos = chainEnd;
    }

    return results;
}

// ═══════════════════════════════════════════════════════════════════
//  NES DPCM Detection
// ═══════════════════════════════════════════════════════════════════

QVector<DetectedSample> AudioDetector::detectDPCM(const QByteArray &data) const
{
    QVector<DetectedSample> results;
    const int size = data.size();
    if (size < 16)
        return results;

    // NES DPCM samples: address = 0xC000 + N*64, length = N*16 + 1
    // They reside in the last 16KB of each PRG bank.
    // We'll look for plausible DPCM regions.

    // iNES header: first 16 bytes; PRG ROM follows
    int prgStart = 0;
    int prgSize = size;
    const uint8_t *d = reinterpret_cast<const uint8_t *>(data.constData());

    // Check for iNES header
    if (size >= 16 && d[0] == 'N' && d[1] == 'E' && d[2] == 'S' && d[3] == 0x1A) {
        const int prgBanks = d[4];
        prgStart = 16;  // after header
        if (d[6] & 0x04)
            prgStart += 512;  // trainer
        prgSize = prgBanks * 16384;
    }

    // DPCM lives in the address range $C000-$FFFF of the CPU address space
    // which maps to the last 16KB of the PRG ROM (or each PRG bank)
    const int bankSize = 16384;  // 16KB
    const int numBanks = qMax(1, prgSize / bankSize);

    for (int bank = 0; bank < numBanks; ++bank) {
        const int bankStart = prgStart + bank * bankSize;
        if (bankStart + bankSize > size)
            break;

        // DPCM is in the second half of each 32KB block or last 16KB bank
        // CPU $C000 maps to PRG offset bankStart + 0x4000 (for 32KB) or bankStart (for 16KB)
        // Scan from start of bank for DPCM-like data

        // Heuristic: look for 64-byte-aligned blocks of data with balanced bit distributions
        for (int offset = 0; offset < bankSize; offset += 64) {
            const int filePos = bankStart + offset;

            // Try multiple DPCM lengths: (N*16 + 1) bytes
            for (int lenIdx = 1; lenIdx <= 255; ++lenIdx) {
                const int sampleLen = lenIdx * 16 + 1;
                if (filePos + sampleLen > size)
                    break;
                if (sampleLen < m_minSampleBytes)
                    continue;

                // Count bit balance across the region
                int ones = 0, zeros = 0;
                for (int i = 0; i < sampleLen; ++i) {
                    const uint8_t b = d[filePos + i];
                    for (int bit = 0; bit < 8; ++bit) {
                        if (b & (1 << bit)) ++ones;
                        else ++zeros;
                    }
                }

                const int totalBits = ones + zeros;
                const float balance = qMin(ones, zeros) / static_cast<float>(totalBits);

                // Good DPCM audio has roughly balanced bits (0.3-0.5)
                if (balance < 0.30f || balance > 0.50f)
                    continue;

                // Check entropy: should not be too uniform (all same byte)
                int uniqueBytes = 0;
                bool seen[256] = {};
                for (int i = 0; i < sampleLen; ++i) {
                    if (!seen[d[filePos + i]]) {
                        seen[d[filePos + i]] = true;
                        ++uniqueBytes;
                    }
                }
                if (uniqueBytes < 16)
                    continue;  // too repetitive

                float confidence = 0.3f + (balance - 0.3f) * 2.0f;
                confidence = qBound(0.0f, confidence, 1.0f);
                if (sampleLen >= 256)
                    confidence += 0.1f;

                if (confidence < m_minConfidence)
                    continue;

                DetectedSample sample;
                sample.offset = filePos;
                sample.length = sampleLen;
                sample.format = AudioSampleFormat::NES_DPCM;
                sample.sampleRate = 8363;  // common DPCM rate
                sample.confidence = qMin(confidence, 1.0f);
                sample.name = QStringLiteral("DPCM @ 0x%1 (%2 bytes)")
                                  .arg(filePos, 0, 16).arg(sampleLen);
                results.append(sample);

                // skip past this candidate
                offset += ((sampleLen + 63) / 64) * 64 - 64;
                break;
            }
        }
    }

    return results;
}

// ═══════════════════════════════════════════════════════════════════
//  Mega Drive DAC PCM Detection
// ═══════════════════════════════════════════════════════════════════

// Three-phase approach:
//   Phase 1 — score overlapping windows on audio-likeness
//   Phase 2 — group adjacent high-score windows into audio spans
//             (bridging short non-audio gaps inside a sample)
//   Phase 3 — convert spans to byte ranges; split multi-sample spans
//             on sustained silence while keeping the natural decay tail

QVector<DetectedSample> AudioDetector::detectMD_DAC(const QByteArray &data) const
{
    QVector<DetectedSample> results;
    const int size = data.size();
    if (size < 512)
        return results;

    const uint8_t *d = reinterpret_cast<const uint8_t *>(data.constData());

    // ── Phase 1: score every overlapping window ─────────────────
    static constexpr int kWin  = 512;
    static constexpr int kStep = 256;       // 50 % overlap
    static constexpr int kHdrSkip = 0x200;  // skip MD vector-table area

    const int nWin = qMax(0, (size - kWin) / kStep + 1);
    QVector<float> wScore(nWin, 0.0f);

    for (int wi = 0; wi < nWin; ++wi) {
        const int p = wi * kStep;
        if (p < kHdrSkip)
            continue;

        // ── statistics ──
        double sum = 0;
        for (int i = 0; i < kWin; ++i)
            sum += d[p + i];
        const double mean = sum / kWin;
        const double meanDist = qAbs(mean - 128.0);
        if (meanDist > 56.0)
            continue;   // way off centre — not PCM

        double varSum = 0;
        for (int i = 0; i < kWin; ++i) {
            const double dd = d[p + i] - mean;
            varSum += dd * dd;
        }
        const double variance = varSum / kWin;

        int crossings = 0;
        double diffAcc = 0;
        int monoRun = 1, monoMax = 1;
        int prevDir = 0;                    // -1 / 0 / +1

        for (int i = 1; i < kWin; ++i) {
            const int cur = d[p + i], prev = d[p + i - 1];
            if ((cur > mean) != (prev > mean))
                ++crossings;
            diffAcc += qAbs(cur - prev);

            const int dir = (cur > prev) ? 1 : (cur < prev) ? -1 : 0;
            if (dir != 0 && dir == prevDir) {
                ++monoRun;
                if (monoRun > monoMax) monoMax = monoRun;
            } else {
                monoRun = 1;
            }
            if (dir != 0) prevDir = dir;
        }
        const float xRate   = crossings / static_cast<float>(kWin);
        const double avgDiff = diffAcc / (kWin - 1);

        // ── silence / sample-tail shortcut ──
        // Very low variance + mean ≈ 0x80: this is the natural decay at the
        // end of a sample.  Give it a moderate score so it doesn't break a run.
        if (variance < 16.0 && meanDist < 12.0) {
            wScore[wi] = 0.42f;
            continue;
        }

        // ── component scoring (0 .. 1) ──
        float s = 0.0f;

        // (a) mean centering  [0 .. 0.20]
        if      (meanDist <  8) s += 0.20f;
        else if (meanDist < 20) s += 0.16f;
        else if (meanDist < 38) s += 0.10f;
        else                    s += 0.03f;

        // (b) variance        [0 .. 0.20]
        if (variance < 4.0 || variance > 6000.0) continue;
        if (variance >= 15 && variance < 3500) s += 0.20f;
        else                                    s += 0.08f;

        // (c) zero-crossing   [0 .. 0.20]
        if (xRate < 0.02f || xRate > 0.49f) continue;
        if (xRate >= 0.04f && xRate <= 0.42f) s += 0.20f;
        else                                   s += 0.08f;

        // (d) smoothness (avg sample-to-sample diff)  [0 .. 0.20]
        if (avgDiff < 0.3 || avgDiff > 65.0) continue;
        if (avgDiff >= 0.8 && avgDiff < 35.0) s += 0.20f;
        else                                    s += 0.08f;

        // (e) monotonic-run length  [0 .. 0.20]
        // Real waveforms have longer rising / falling slopes than code bytes.
        if      (monoMax >= 8) s += 0.20f;
        else if (monoMax >= 5) s += 0.12f;
        else if (monoMax >= 3) s += 0.04f;

        wScore[wi] = s;
    }

    // ── Phase 2: group windows into audio spans (with gap bridging) ─
    const float kThr    = 0.40f;   // min score to be "audio"
    const int kMaxGap   = 3;       // bridge up to 3 low-score windows (~768 B)

    struct Span { int first, last; };       // inclusive window indices
    QVector<Span> spans;
    {
        int first = -1, gap = 0;
        for (int wi = 0; wi < nWin; ++wi) {
            if (wScore[wi] >= kThr) {
                if (first < 0) first = wi;
                gap = 0;
            } else if (first >= 0) {
                if (++gap > kMaxGap) {
                    const int last = wi - gap;
                    if (last >= first)
                        spans.append({first, last});
                    first = -1; gap = 0;
                }
            }
        }
        if (first >= 0) {
            int last = nWin - 1;
            while (last > first && wScore[last] < kThr) --last;
            spans.append({first, last});
        }
    }

    // ── Phase 3: byte-level refinement + silence-based splitting ──
    //
    // Sustained silence = 300+ consecutive bytes within ±6 of 0x80 (≈37 ms
    // at 8 kHz).  This separates distinct samples that were packed together.
    // The natural decay tail (< 300 bytes of near-silence) stays with the
    // sample that produced it.

    const int kMinSample    = qMax(m_minSampleBytes, 256);
    const int kSilenceSplit = 300;

    for (const Span &sp : spans) {
        int bStart = sp.first * kStep;
        int bEnd   = qMin(sp.last * kStep + kWin, size);
        if (bEnd - bStart < kMinSample)
            continue;

        // Trim leading silence only (trailing silence is kept as decay tail)
        while (bStart < bEnd - kMinSample
               && qAbs(static_cast<int>(d[bStart]) - 128) <= 4)
            ++bStart;

        // Walk the region, splitting on sustained silence
        int sampleStart = bStart;
        int silRun = 0;

        for (int pos = bStart; pos < bEnd; ++pos) {
            const bool quiet = qAbs(static_cast<int>(d[pos]) - 128) <= 6;
            if (quiet) {
                ++silRun;
            } else {
                // A sustained silence just ended — check if we should split
                if (silRun >= kSilenceSplit
                    && pos - silRun - sampleStart >= kMinSample) {
                    // Include ~40 % of the silence as the natural decay tail
                    const int tail = silRun * 2 / 5;
                    const int sEnd = (pos - silRun) + tail;
                    addMDSample(results, d, sampleStart, sEnd - sampleStart);
                    sampleStart = pos;      // next sample starts here
                }
                silRun = 0;
            }
        }

        // Trailing piece of the span
        if (bEnd - sampleStart >= kMinSample)
            addMDSample(results, d, sampleStart, bEnd - sampleStart);
    }

    return results;
}

/// Push a MD DAC sample.  No trailing-silence trimming — the natural decay
/// tail is an integral part of the sample.
void AudioDetector::addMDSample(QVector<DetectedSample> &results,
                                 const uint8_t * /*d*/, int offset, int length) const
{
    if (length < qMax(m_minSampleBytes, 256))
        return;

    float confidence = 0.45f;
    if (length >= 1024)
        confidence += 0.10f;
    if (length >= 2048)
        confidence += 0.10f;
    if (length >= 4096)
        confidence += 0.10f;
    if (length >= 8192)
        confidence += 0.10f;

    DetectedSample sample;
    sample.offset = offset;
    sample.length = length;
    sample.format = AudioSampleFormat::MD_DAC_PCM;
    sample.sampleRate = 8000;
    sample.confidence = qMin(confidence, 1.0f);
    sample.name = QStringLiteral("DAC PCM @ 0x%1 (%2 bytes)")
                      .arg(offset, 0, 16).arg(length);
    results.append(sample);
}

// ═══════════════════════════════════════════════════════════════════
//  Mega Drive Signed 8-bit PCM Detection
// ═══════════════════════════════════════════════════════════════════

QVector<DetectedSample> AudioDetector::detectMD_Signed8(const QByteArray &data) const
{
    // Detect signed 8-bit PCM (centered around 0x00 = byte values
    // clustered near 0x00 and 0xFF).  Used by some MD drivers and Sega CD.
    QVector<DetectedSample> results;
    const int size = data.size();
    if (size < 512)
        return results;

    const uint8_t *d = reinterpret_cast<const uint8_t *>(data.constData());

    static constexpr int kWin  = 512;
    static constexpr int kStep = 256;
    static constexpr int kHdrSkip = 0x200;

    const int nWin = qMax(0, (size - kWin) / kStep + 1);
    QVector<float> wScore(nWin, 0.0f);

    for (int wi = 0; wi < nWin; ++wi) {
        const int p = wi * kStep;
        if (p < kHdrSkip)
            continue;

        // Interpret bytes as signed for mean calculation
        double sum = 0;
        for (int i = 0; i < kWin; ++i)
            sum += static_cast<int8_t>(d[p + i]);
        const double mean = sum / kWin;
        const double meanDist = qAbs(mean);
        if (meanDist > 56.0)
            continue;   // way off centre — not signed PCM

        double varSum = 0;
        for (int i = 0; i < kWin; ++i) {
            const double dd = static_cast<int8_t>(d[p + i]) - mean;
            varSum += dd * dd;
        }
        const double variance = varSum / kWin;

        int crossings = 0;
        double diffAcc = 0;
        int monoRun = 1, monoMax = 1;
        int prevDir = 0;

        for (int i = 1; i < kWin; ++i) {
            const int cur = static_cast<int8_t>(d[p + i]);
            const int prev = static_cast<int8_t>(d[p + i - 1]);
            if ((cur > mean) != (prev > mean))
                ++crossings;
            diffAcc += qAbs(cur - prev);

            const int dir = (cur > prev) ? 1 : (cur < prev) ? -1 : 0;
            if (dir != 0 && dir == prevDir) {
                ++monoRun;
                if (monoRun > monoMax) monoMax = monoRun;
            } else {
                monoRun = 1;
            }
            if (dir != 0) prevDir = dir;
        }
        const float xRate   = crossings / static_cast<float>(kWin);
        const double avgDiff = diffAcc / (kWin - 1);

        // Silence: low variance near zero
        if (variance < 16.0 && meanDist < 12.0) {
            wScore[wi] = 0.42f;
            continue;
        }

        float s = 0.0f;

        // (a) mean centering (near 0)
        if      (meanDist <  8) s += 0.20f;
        else if (meanDist < 20) s += 0.16f;
        else if (meanDist < 38) s += 0.10f;
        else                    s += 0.03f;

        // (b) variance
        if (variance < 4.0 || variance > 6000.0) continue;
        if (variance >= 15 && variance < 3500) s += 0.20f;
        else                                    s += 0.08f;

        // (c) zero-crossing
        if (xRate < 0.02f || xRate > 0.49f) continue;
        if (xRate >= 0.04f && xRate <= 0.42f) s += 0.20f;
        else                                   s += 0.08f;

        // (d) smoothness
        if (avgDiff < 0.3 || avgDiff > 65.0) continue;
        if (avgDiff >= 0.8 && avgDiff < 35.0) s += 0.20f;
        else                                    s += 0.08f;

        // (e) monotonic-run length
        if      (monoMax >= 8) s += 0.20f;
        else if (monoMax >= 5) s += 0.12f;
        else if (monoMax >= 3) s += 0.04f;

        wScore[wi] = s;
    }

    // Group windows into spans with gap bridging
    const float kThr    = 0.40f;
    const int kMaxGap   = 3;

    struct Span { int first, last; };
    QVector<Span> spans;
    {
        int first = -1, gap = 0;
        for (int wi = 0; wi < nWin; ++wi) {
            if (wScore[wi] >= kThr) {
                if (first < 0) first = wi;
                gap = 0;
            } else if (first >= 0) {
                if (++gap > kMaxGap) {
                    const int last = wi - gap;
                    if (last >= first)
                        spans.append({first, last});
                    first = -1; gap = 0;
                }
            }
        }
        if (first >= 0) {
            int last = nWin - 1;
            while (last > first && wScore[last] < kThr) --last;
            spans.append({first, last});
        }
    }

    const int kMinSample    = qMax(m_minSampleBytes, 256);
    const int kSilenceSplit = 300;

    for (const Span &sp : spans) {
        int bStart = sp.first * kStep;
        int bEnd   = qMin(sp.last * kStep + kWin, size);
        if (bEnd - bStart < kMinSample)
            continue;

        // Trim leading silence (near 0x00)
        while (bStart < bEnd - kMinSample
               && qAbs(static_cast<int8_t>(d[bStart])) <= 4)
            ++bStart;

        // Walk the region, splitting on sustained silence
        int sampleStart = bStart;
        int silRun = 0;

        for (int pos = bStart; pos < bEnd; ++pos) {
            const bool quiet = qAbs(static_cast<int8_t>(d[pos])) <= 6;
            if (quiet) {
                ++silRun;
            } else {
                if (silRun >= kSilenceSplit
                    && pos - silRun - sampleStart >= kMinSample) {
                    const int tail = silRun * 2 / 5;
                    const int sEnd = (pos - silRun) + tail;
                    const int len = sEnd - sampleStart;
                    if (len >= kMinSample) {
                        float confidence = 0.42f;
                        if (len >= 1024) confidence += 0.10f;
                        if (len >= 2048) confidence += 0.10f;
                        if (len >= 4096) confidence += 0.10f;
                        if (len >= 8192) confidence += 0.10f;
                        DetectedSample sample;
                        sample.offset = sampleStart;
                        sample.length = len;
                        sample.format = AudioSampleFormat::MD_PCM8_Signed;
                        sample.sampleRate = 8000;
                        sample.confidence = qMin(confidence, 1.0f);
                        sample.name = QStringLiteral("Signed PCM @ 0x%1 (%2 bytes)")
                                          .arg(sampleStart, 0, 16).arg(len);
                        results.append(sample);
                    }
                    sampleStart = pos;
                }
                silRun = 0;
            }
        }

        // Trailing piece
        const int len = bEnd - sampleStart;
        if (len >= kMinSample) {
            float confidence = 0.42f;
            if (len >= 1024) confidence += 0.10f;
            if (len >= 2048) confidence += 0.10f;
            if (len >= 4096) confidence += 0.10f;
            if (len >= 8192) confidence += 0.10f;
            DetectedSample sample;
            sample.offset = sampleStart;
            sample.length = len;
            sample.format = AudioSampleFormat::MD_PCM8_Signed;
            sample.sampleRate = 8000;
            sample.confidence = qMin(confidence, 1.0f);
            sample.name = QStringLiteral("Signed PCM @ 0x%1 (%2 bytes)")
                              .arg(sampleStart, 0, 16).arg(len);
            results.append(sample);
        }
    }

    return results;
}

QVector<DetectedSample> AudioDetector::detectMD_DPCM4(const QByteArray &data) const
{
    // Detect IMA ADPCM encoded audio (4-bit, ~6500 Hz, UMK3-style).
    // IMA ADPCM has characteristic nibble distribution: small deltas (0-3)
    // dominate, large deltas (4-7) are rarer, and the step index stays
    // moderate for voiced audio.
    static constexpr int kIndexTable[16] = {
        -1, -1, -1, -1, 2, 4, 6, 8,
        -1, -1, -1, -1, 2, 4, 6, 8
    };
    static constexpr int kStepTable[89] = {
            7,     8,     9,    10,    11,    12,    13,    14,    16,    17,
           19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
           50,    55,    60,    66,    73,    80,    88,    97,   107,   118,
          130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
          337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
          876,   963,  1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
         2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
         5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
        15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
    };

    QVector<DetectedSample> results;
    const int size = data.size();
    if (size < 512)
        return results;

    const uint8_t *d = reinterpret_cast<const uint8_t *>(data.constData());
    static constexpr int kHdrSkip = 0x200;
    static constexpr int kWin = 256;
    static constexpr int kStep = 128;

    const int nWin = qMax(0, (size - kWin) / kStep + 1);
    QVector<float> scores(nWin, 0.0f);

    for (int wi = 0; wi < nWin; ++wi) {
        const int pos = wi * kStep;
        if (pos < kHdrSkip)
            continue;

        // Nibble magnitude histogram: count how many nibbles have magnitude 0-3 vs 4-7
        int smallDelta = 0;  // |delta| <= 3 (index decreases)
        int largeDelta = 0;  // |delta| >= 4 (index increases)
        int nibbleHist[16] = {0};

        // Decode the window with IMA ADPCM to check predictor behaviour
        int predictor = 0;
        int stepIndex = 0;
        int crossings = 0;
        int prev = 0;
        double sumAbs = 0.0;
        int maxAbsPred = 0;

        for (int i = 0; i < kWin; ++i) {
            const uint8_t b = d[pos + i];
            const int nibs[2] = { b & 0x0F, (b >> 4) & 0x0F };
            for (int ni = 0; ni < 2; ++ni) {
                const int nibble = nibs[ni];
                nibbleHist[nibble]++;

                const int mag = nibble & 0x07;
                if (mag <= 3) ++smallDelta;
                else          ++largeDelta;

                const int step = kStepTable[stepIndex];
                int diff = step >> 3;
                if (nibble & 1) diff += step >> 2;
                if (nibble & 2) diff += step >> 1;
                if (nibble & 4) diff += step;
                if (nibble & 8) diff = -diff;

                predictor = qBound(-32768, predictor + diff, 32767);
                stepIndex = qBound(0, stepIndex + kIndexTable[nibble], 88);

                if ((predictor > 0) != (prev > 0) && prev != 0)
                    ++crossings;

                sumAbs += qAbs(predictor);
                if (qAbs(predictor) > maxAbsPred)
                    maxAbsPred = qAbs(predictor);
                prev = predictor;
            }
        }

        const int sampleCount = kWin * 2;
        const float smallRatio = smallDelta / static_cast<float>(sampleCount);
        const float crossRate = crossings / static_cast<float>(sampleCount);
        const double avgAbs = sumAbs / sampleCount;

        // --- Scoring ---
        float score = 0.0f;

        // IMA ADPCM with audio content: small deltas dominate (60-90% typically)
        if (smallRatio >= 0.50f && smallRatio <= 0.95f) score += 0.25f;
        else if (smallRatio >= 0.40f && smallRatio <= 0.98f) score += 0.10f;
        else continue;

        // Predictor should show reasonable activity (not constant, not railed)
        if (avgAbs >= 200.0 && avgAbs <= 28000.0) score += 0.20f;
        else if (avgAbs >= 50.0 && avgAbs <= 30000.0) score += 0.08f;
        else continue;

        // Max predictor should not be railed at ±32768 for too many windows
        if (maxAbsPred < 32700) score += 0.10f;
        else score += 0.02f;

        // Crossings — some zero-crossings indicate signal, not DC or noise
        if (crossRate >= 0.01f && crossRate <= 0.40f) score += 0.15f;
        else if (crossRate >= 0.005f && crossRate <= 0.50f) score += 0.06f;

        // Nibble diversity — should use a variety of nibble values
        int usedNibbles = 0;
        int maxNibCount = 0;
        for (int n = 0; n < 16; ++n) {
            if (nibbleHist[n] > 0) ++usedNibbles;
            if (nibbleHist[n] > maxNibCount) maxNibCount = nibbleHist[n];
        }
        const double maxNibRatio = maxNibCount / static_cast<double>(sampleCount);

        if (usedNibbles >= 10) score += 0.15f;
        else if (usedNibbles >= 7) score += 0.08f;
        else if (usedNibbles >= 5) score += 0.03f;

        if (maxNibRatio <= 0.35) score += 0.10f;
        else if (maxNibRatio <= 0.50) score += 0.04f;

        // Step index should settle into a moderate range for real audio
        if (stepIndex >= 5 && stepIndex <= 70) score += 0.05f;

        scores[wi] = score;
    }

    // Build spans with short-gap bridging.
    static constexpr float kScoreThr = 0.42f;
    static constexpr int kMaxGapWins = 2;
    QVector<QPair<int, int>> spans;
    int spanStart = -1;
    int gap = 0;

    for (int wi = 0; wi < nWin; ++wi) {
        if (scores[wi] >= kScoreThr) {
            if (spanStart < 0)
                spanStart = wi;
            gap = 0;
        } else if (spanStart >= 0) {
            if (++gap > kMaxGapWins) {
                const int spanEnd = wi - gap;
                if (spanEnd >= spanStart)
                    spans.append({spanStart, spanEnd});
                spanStart = -1;
                gap = 0;
            }
        }
    }
    if (spanStart >= 0) {
        int spanEnd = nWin - 1;
        while (spanEnd > spanStart && scores[spanEnd] < kScoreThr)
            --spanEnd;
        spans.append({spanStart, spanEnd});
    }

    const int minBytes = qMax(m_minSampleBytes, 192);
    for (const auto &sp : std::as_const(spans)) {
        int start = sp.first * kStep;
        int end = qMin(sp.second * kStep + kWin, size);

        if (end - start < minBytes)
            continue;

        // Trim obvious zero-delta/silence padding around the candidate.
        // For IMA ADPCM, quiet bytes have both nibble magnitudes ≤ 1 (i.e. 0x00, 0x11, 0x10, etc.)
        auto isQuietByte = [d](int idx) {
            const uint8_t b = d[idx];
            const int mag0 = (b >> 4) & 0x07;
            const int mag1 = b & 0x07;
            return mag0 <= 1 && mag1 <= 1;
        };

        while (start < end - minBytes && isQuietByte(start))
            ++start;
        while (end > start + minBytes && isQuietByte(end - 1))
            --end;

        if (end - start < minBytes)
            continue;

        DetectedSample sample;
        sample.offset = start;
        sample.length = end - start;
        sample.format = AudioSampleFormat::MD_DPCM4_6500;
        sample.sampleRate = 6500;
        sample.confidence = qMin(0.95f, 0.45f + static_cast<float>((end - start) / 4096.0));
        sample.name = QStringLiteral("UMK3 DPCM4 @ 0x%1 (%2 bytes)")
                          .arg(start, 0, 16).arg(end - start);
        results.append(sample);
    }

    return results;
}

// ═══════════════════════════════════════════════════════════════════
//  GBA PCM8 Detection (Sappy/M4A engine)
// ═══════════════════════════════════════════════════════════════════

QVector<DetectedSample> AudioDetector::detectGBA_PCM(const QByteArray &data) const
{
    QVector<DetectedSample> results;
    const int size = data.size();
    if (size < 0x20)
        return results;

    const uint8_t *d = reinterpret_cast<const uint8_t *>(data.constData());

    // Look for Sappy/M4A sample headers with structure:
    // Offset 0x00: u16 flags (bit 14 = loop, bit 15 = compressed)
    //              (commonly 0x0000 for non-looping, 0x4000 for looping)
    // Offset 0x02: u16 unused (often 0)
    // Offset 0x04: u32 pitch_adjust
    // Offset 0x08: u32 loop_start (in samples)
    // Offset 0x0C: u32 length (in samples)
    // Offset 0x10: signed 8-bit PCM data

    for (int pos = 0; pos + 0x14 <= size; pos += 4) {
        // Read potential header
        uint16_t flags = static_cast<uint16_t>(d[pos]) | (static_cast<uint16_t>(d[pos + 1]) << 8);

        // Valid flags: 0x0000 (normal), 0x4000 (looped), 0x0040 (compressed)
        if (flags != 0x0000 && flags != 0x4000 && flags != 0x0040 && flags != 0x4040)
            continue;

        // Unused field should be 0
        uint16_t unused = static_cast<uint16_t>(d[pos + 2]) | (static_cast<uint16_t>(d[pos + 3]) << 8);
        if (unused != 0)
            continue;

        // Read length
        uint32_t sampleLen = static_cast<uint32_t>(d[pos + 0x0C])
                           | (static_cast<uint32_t>(d[pos + 0x0D]) << 8)
                           | (static_cast<uint32_t>(d[pos + 0x0E]) << 16)
                           | (static_cast<uint32_t>(d[pos + 0x0F]) << 24);

        // Sanity check length
        if (sampleLen < 32 || sampleLen > 0x100000)  // max ~1MB
            continue;

        if (pos + 0x10 + static_cast<int>(sampleLen) > size)
            continue;

        // Read loop start
        uint32_t loopStart = static_cast<uint32_t>(d[pos + 0x08])
                           | (static_cast<uint32_t>(d[pos + 0x09]) << 8)
                           | (static_cast<uint32_t>(d[pos + 0x0A]) << 16)
                           | (static_cast<uint32_t>(d[pos + 0x0B]) << 24);

        if (loopStart >= sampleLen)
            continue;

        // Validate PCM data: check that it looks like signed 8-bit audio
        const int checkLen = qMin(static_cast<int>(sampleLen), 256);
        double sum = 0;
        for (int i = 0; i < checkLen; ++i)
            sum += static_cast<int8_t>(d[pos + 0x10 + i]);
        const double mean = sum / checkLen;

        // Signed audio should be centered around 0
        if (qAbs(mean) > 40)
            continue;

        float confidence = 0.6f;
        if (sampleLen >= 256)
            confidence += 0.15f;
        if (sampleLen >= 2048)
            confidence += 0.1f;
        if (flags == 0x4000)
            confidence += 0.05f;

        DetectedSample sample;
        sample.offset = pos;
        sample.length = 0x10 + sampleLen;  // header + data
        sample.format = AudioSampleFormat::GBA_PCM8;
        sample.sampleRate = 13379;  // common M4A mixer rate
        sample.hasLoop = (flags & 0x4000) != 0;
        sample.loopOffset = loopStart;
        sample.confidence = qMin(confidence, 1.0f);
        sample.name = QStringLiteral("GBA PCM @ 0x%1 (%2 samples)")
                          .arg(pos, 0, 16).arg(sampleLen);
        results.append(sample);

        pos += 0x10 + sampleLen - 4;
    }

    return results;
}

// ═══════════════════════════════════════════════════════════════════
//  Decoders: BRR → PCM16
// ═══════════════════════════════════════════════════════════════════

QVector<int16_t> AudioDetector::decodeBRR(const QByteArray &brrData)
{
    const int size = brrData.size();
    if (size < 9)
        return {};

    const uint8_t *d = reinterpret_cast<const uint8_t *>(brrData.constData());
    QVector<int16_t> pcm;
    pcm.reserve((size / 9) * 16);

    int old1 = 0, old2 = 0;

    for (int pos = 0; pos + 9 <= size; pos += 9) {
        const uint8_t header = d[pos];
        const int range = (header >> 4) & 0x0F;
        const int filter = (header >> 2) & 0x03;

        for (int i = 0; i < 8; ++i) {
            const uint8_t dataByte = d[pos + 1 + i];

            for (int nibIdx = 0; nibIdx < 2; ++nibIdx) {
                int nibble = (nibIdx == 0) ? ((dataByte >> 4) & 0x0F) : (dataByte & 0x0F);

                // Sign-extend 4-bit to 32-bit
                if (nibble >= 8)
                    nibble -= 16;

                int sample;
                if (range <= 12)
                    sample = (nibble << range) >> 1;
                else
                    sample = (nibble < 0) ? -2048 : 0;  // range 13-15: clipped

                // Apply IIR filter
                switch (filter) {
                case 0:
                    break;
                case 1:
                    sample += old1 + ((-old1) >> 4);  // old1 * 15/16
                    break;
                case 2:
                    sample += (old1 << 1) + ((-old1 * 3) >> 5)  // old1 * 61/32
                              - old2 + ((old2) >> 4);            // - old2 * 15/16
                    break;
                case 3:
                    sample += (old1 << 1) + ((-old1 * 13) >> 6)  // old1 * 115/64
                              - old2 + ((old2 * 3) >> 4);        // - old2 * 13/16
                    break;
                }

                // Clamp to 16-bit signed
                sample = qBound(-32768, sample, 32767);

                old2 = old1;
                old1 = sample;

                pcm.append(static_cast<int16_t>(sample));
            }
        }

        // Stop at END flag
        if (header & 0x01)
            break;
    }

    return pcm;
}

// ═══════════════════════════════════════════════════════════════════
//  Decoders: NES DPCM → PCM16
// ═══════════════════════════════════════════════════════════════════

QVector<int16_t> AudioDetector::decodeDPCM(const QByteArray &dpcmData)
{
    QVector<int16_t> pcm;
    pcm.reserve(dpcmData.size() * 8);

    int output = 64;  // midpoint of 7-bit DAC

    for (int i = 0; i < dpcmData.size(); ++i) {
        const uint8_t byte = static_cast<uint8_t>(dpcmData[i]);

        for (int bit = 0; bit < 8; ++bit) {
            if (byte & (1 << bit))
                output = qMin(output + 2, 127);
            else
                output = qMax(output - 2, 0);

            // Scale 7-bit (0-127) to 16-bit signed
            pcm.append(static_cast<int16_t>((output - 64) * 256));
        }
    }

    return pcm;
}

// ═══════════════════════════════════════════════════════════════════
//  Decoders: Unsigned 8-bit → PCM16
// ═══════════════════════════════════════════════════════════════════

QVector<int16_t> AudioDetector::decodeUnsigned8(const QByteArray &data)
{
    QVector<int16_t> pcm;
    pcm.reserve(data.size());

    for (int i = 0; i < data.size(); ++i) {
        const int8_t s = static_cast<int8_t>(static_cast<uint8_t>(data[i]) - 128);
        pcm.append(static_cast<int16_t>(s) * 256);
    }

    return pcm;
}

// ═══════════════════════════════════════════════════════════════════
//  Decoders: Signed 8-bit → PCM16
// ═══════════════════════════════════════════════════════════════════

QVector<int16_t> AudioDetector::decodeSigned8(const QByteArray &data)
{
    QVector<int16_t> pcm;
    pcm.reserve(data.size());

    for (int i = 0; i < data.size(); ++i) {
        const int8_t s = static_cast<int8_t>(data[i]);
        pcm.append(static_cast<int16_t>(s) * 256);
    }

    return pcm;
}

QVector<int16_t> AudioDetector::decodeMD_DPCM4(const QByteArray &data)
{
    // IMA ADPCM (Intel/DVI ADPCM) decoder — standard 4-bit adaptive step.
    static constexpr int kIndexTable[16] = {
        -1, -1, -1, -1, 2, 4, 6, 8,
        -1, -1, -1, -1, 2, 4, 6, 8
    };
    static constexpr int kStepTable[89] = {
            7,     8,     9,    10,    11,    12,    13,    14,    16,    17,
           19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
           50,    55,    60,    66,    73,    80,    88,    97,   107,   118,
          130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
          337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
          876,   963,  1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
         2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
         5894,  6484,  7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
        15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
    };

    QVector<int16_t> pcm;
    pcm.reserve(data.size() * 2);

    int predictor = 0;
    int stepIndex = 0;

    for (int i = 0; i < data.size(); ++i) {
        const uint8_t b = static_cast<uint8_t>(data[i]);
        // Low nibble first, then high nibble (standard IMA packing)
        const int nibbles[2] = { b & 0x0F, (b >> 4) & 0x0F };
        for (int ni = 0; ni < 2; ++ni) {
            const int nibble = nibbles[ni];
            const int step = kStepTable[stepIndex];

            // Compute difference: diff = step/8 + step/4 * (nibble & 1) + step/2 * (bit1) + step * (bit2)
            int diff = step >> 3;
            if (nibble & 1) diff += step >> 2;
            if (nibble & 2) diff += step >> 1;
            if (nibble & 4) diff += step;
            if (nibble & 8) diff = -diff;

            predictor = qBound(-32768, predictor + diff, 32767);
            stepIndex = qBound(0, stepIndex + kIndexTable[nibble], 88);

            pcm.append(static_cast<int16_t>(predictor));
        }
    }

    return pcm;
}

// ═══════════════════════════════════════════════════════════════════
//  Decoder: µ-law 8-bit → PCM16
// ═══════════════════════════════════════════════════════════════════

QVector<int16_t> AudioDetector::decodeMD_ULAW(const QByteArray &data)
{
    QVector<int16_t> pcm;
    pcm.reserve(data.size());

    for (int i = 0; i < data.size(); ++i) {
        uint8_t u = ~static_cast<uint8_t>(data[i]);
        const int sign = (u & 0x80) ? -1 : 1;
        const int exponent = (u >> 4) & 0x07;
        const int mantissa = u & 0x0F;
        int magnitude = ((mantissa << 3) + 0x84) << exponent;
        magnitude -= 0x84;
        pcm.append(static_cast<int16_t>(qBound(-32768, sign * magnitude, 32767)));
    }

    return pcm;
}

// ═══════════════════════════════════════════════════════════════════
//  Decoder: OKI/Dialogic ADPCM 4-bit → PCM16
// ═══════════════════════════════════════════════════════════════════

QVector<int16_t> AudioDetector::decodeMD_OKI_ADPCM(const QByteArray &data)
{
    static constexpr int kStepTable[49] = {
         16,   17,   19,   21,   23,   25,   28,   31,   34,   37,
         41,   45,   50,   55,   60,   66,   73,   80,   88,   97,
        107,  118,  130,  143,  157,  173,  190,  209,  230,  253,
        279,  307,  337,  371,  408,  449,  494,  544,  598,  658,
        724,  796,  876,  963, 1060, 1166, 1282, 1411, 1552
    };
    static constexpr int kIndexTable[8] = {
        -1, -1, -1, -1, 2, 4, 6, 8
    };

    QVector<int16_t> pcm;
    pcm.reserve(data.size() * 2);

    int predictor = 0;
    int stepIndex = 0;

    for (int i = 0; i < data.size(); ++i) {
        const uint8_t b = static_cast<uint8_t>(data[i]);
        // High nibble first, then low nibble (OKI standard packing)
        const int nibbles[2] = { (b >> 4) & 0x0F, b & 0x0F };
        for (int ni = 0; ni < 2; ++ni) {
            const int nibble = nibbles[ni];
            const int step = kStepTable[stepIndex];

            int diff = step >> 3;
            if (nibble & 4) diff += step;
            if (nibble & 2) diff += step >> 1;
            if (nibble & 1) diff += step >> 2;
            if (nibble & 8) diff = -diff;

            predictor = qBound(-2048, predictor + diff, 2047);
            stepIndex = qBound(0, stepIndex + kIndexTable[nibble & 7], 48);

            // OKI ADPCM is 12-bit; scale to 16-bit
            pcm.append(static_cast<int16_t>(predictor * 16));
        }
    }

    return pcm;
}

// ═══════════════════════════════════════════════════════════════════
//  Generic decoder dispatch
// ═══════════════════════════════════════════════════════════════════

QVector<int16_t> AudioDetector::decodeToPCM16(const QByteArray &rawData,
                                               AudioSampleFormat format,
                                               int *outSampleRate)
{
    int rate = 8000;
    QVector<int16_t> pcm;

    switch (format) {
    case AudioSampleFormat::SNES_BRR:
        pcm = decodeBRR(rawData);
        rate = 32000;
        break;
    case AudioSampleFormat::NES_DPCM:
        pcm = decodeDPCM(rawData);
        rate = 8363;
        break;
    case AudioSampleFormat::MD_DAC_PCM:
    case AudioSampleFormat::Raw_PCM8_Unsigned:
        pcm = decodeUnsigned8(rawData);
        rate = 8000;
        break;
    case AudioSampleFormat::MD_PCM8_Signed:
        pcm = decodeSigned8(rawData);
        rate = 8000;
        break;
    case AudioSampleFormat::MD_ULAW:
        pcm = decodeMD_ULAW(rawData);
        rate = 8000;
        break;
    case AudioSampleFormat::MD_DPCM4_6500:
        pcm = decodeMD_DPCM4(rawData);
        rate = 6500;
        break;
    case AudioSampleFormat::MD_ADPCM_OKI:
        pcm = decodeMD_OKI_ADPCM(rawData);
        rate = 7575;
        break;
    case AudioSampleFormat::GBA_PCM8:
    case AudioSampleFormat::Raw_PCM8_Signed:
        pcm = decodeSigned8(rawData);
        rate = 13379;
        break;
    case AudioSampleFormat::GB_Wave4bit:
        // 32 samples from 16 bytes
        pcm.reserve(rawData.size() * 2);
        for (int i = 0; i < rawData.size(); ++i) {
            const uint8_t b = static_cast<uint8_t>(rawData[i]);
            pcm.append(static_cast<int16_t>(((b >> 4) - 8) * 2048));
            pcm.append(static_cast<int16_t>(((b & 0x0F) - 8) * 2048));
        }
        rate = 8192;
        break;
    default:
        break;
    }

    if (outSampleRate)
        *outSampleRate = rate;
    return pcm;
}

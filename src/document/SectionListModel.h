#ifndef SECTIONLISTMODEL_H
#define SECTIONLISTMODEL_H

#include <QObject>
#include <QString>
#include <QColor>
#include <QVector>
#include <QMetaType>
#include <climits>
#include "romdetect.h"

class QUndoStack;

/// Display-mode constants for sections.
/// Values > 0 are 1-based table indices (Table 1 … Table N).
enum {
    SectionDisplay_Default  = 0,   ///< follow global view mode
    SectionDisplay_Raw      = -2,  ///< raw file encoding, ignore tables
    SectionDisplay_Disasm   = -1,  ///< disassembly view
    SectionDisplay_Audio    = -3,  ///< audio waveform / histogram view
    SectionDisplay_Graphics = -4,  ///< tile graphics view
    SectionDisplay_Palette  = -5   ///< palette swatch view
};

/// Tile codec — how bytes in ROM map to 8×8 pixel tiles.
enum class TileCodec : int {
    Linear1bpp       = 0,   ///< 1bpp, 8 bytes/tile — each byte = 8 pixels
    Linear2bpp       = 1,   ///< 2bpp planar, 16 bytes/tile (NES: bp0[8] bp1[8])
    Interleaved2bpp  = 2,   ///< 2bpp interleaved, 16 bytes/tile (GB/SNES: bp0,bp1 per row)
    Planar3bpp       = 3,   ///< 3bpp SNES, 24 bytes/tile (16 interleaved + 8 planar)
    Interleaved4bpp  = 4,   ///< 4bpp SNES, 32 bytes/tile (interleaved pairs)
    Linear4bpp       = 5,   ///< 4bpp GBA linear, 32 bytes/tile (low nibble = left pixel)
    SegaMD4bpp       = 6,   ///< 4bpp Sega Genesis/MD, 32 bytes/tile (high nibble = left pixel)
    SegaSMS4bpp      = 7,   ///< 4bpp Sega SMS/GG, 32 bytes/tile (4 bitplanes per row)
    Linear8bpp       = 8,   ///< 8bpp linear, 64 bytes/tile (each byte = 1 pixel)
};

int  tileCodecBpp(TileCodec codec);
int  tileCodecBytesPerTile(TileCodec codec);
const char *tileCodecName(TileCodec codec);

/// A named group that organises sections visually in the tree.
/// Groups are NOT sections – they have no file range or display mode.
struct SectionGroup
{
    QString name;
    QColor  color;
    int     treeOrder     = 0;   ///< display position among siblings (lower = higher up)
    int     parentGroupId = -1;  ///< -1 = root level; >= 0 = child of that group
};

inline bool operator==(const SectionGroup &a, const SectionGroup &b)
{
    return a.name == b.name && a.color == b.color
        && a.treeOrder == b.treeOrder && a.parentGroupId == b.parentGroupId;
}

Q_DECLARE_METATYPE(SectionGroup)

/// A single named section of the file.
/// Sections are defined by their start offset only.
/// The end of a section is the start of the next section (by offset order)
/// or the end of the file.  Two sections cannot share the same startOffset.
struct Section
{
    QString name;
    qint64  startOffset = 0;
    QColor  color;

    // New schema fields for persisted display settings.
    // Main display mnemonic: auto, raw, txt, gfx, pal, snd, asm.
    QString display = QStringLiteral("auto");
    // Options payload for sub-type and per-mode settings (key=value;... format).
    QString options;

    // Legacy runtime fields (kept for compatibility with existing rendering code).
    int     displayMode = SectionDisplay_Default;
    RomType disasmCpu   = RomType::Unknown;
    int     groupId     = -1;   // -1 = ungrouped; >=0 = index into groups vector

    // ── Per-section graphics settings ──
    TileCodec tileCodec = TileCodec::Linear2bpp;
    int       tileCols  = 16;   ///< tiles per row in graphics view (legacy; now auto-computed)
    QVector<QRgb> palette;      ///< custom palette; if empty, use default for codec
};

inline bool operator==(const Section &lhs, const Section &rhs)
{
    return lhs.name == rhs.name
        && lhs.startOffset == rhs.startOffset
        && lhs.color == rhs.color
        && lhs.display == rhs.display
        && lhs.options == rhs.options
        && lhs.displayMode == rhs.displayMode
        && lhs.disasmCpu == rhs.disasmCpu
        && lhs.groupId == rhs.groupId
        && lhs.tileCodec == rhs.tileCodec
        && lhs.tileCols == rhs.tileCols
        && lhs.palette == rhs.palette;
}

Q_DECLARE_METATYPE(Section)

class SectionListModel : public QObject
{
    Q_OBJECT

public:
    explicit SectionListModel(QObject *parent = nullptr);

    // ── Section access ──
    int count() const { return m_sections.size(); }
    const Section &at(int index) const { return m_sections.at(index); }
    const QVector<Section> &sections() const { return m_sections; }

    /// Exclusive end offset for section at @p index (next section start, or fileSize).
    qint64 endOffsetOf(int index, qint64 fileSize) const;

    /// Index of the section whose range contains @p offset, or -1.
    int sectionIndexAtOffset(qint64 offset) const;

    // ── Group access ──
    int groupCount() const { return m_groups.size(); }
    const SectionGroup &groupAt(int index) const { return m_groups.at(index); }
    const QVector<SectionGroup> &groups() const { return m_groups; }

    // ── Mutations (undo-aware) ──
    void setUndoStack(QUndoStack *stack) { m_undoStack = stack; }

    bool addSection(const Section &s);       ///< rejects duplicate startOffset
    void removeSection(int index);
    void renameSection(int index, const QString &name);
    void recolorSection(int index, const QColor &color);
    void updateSection(int index, const Section &s);
    void clear();

    void applySections(const QVector<Section> &sections,
                       const QVector<SectionGroup> &groups,
                       const QString &text = QString());
    void applySections(const QVector<Section> &sections,
                       const QString &text = QString());

    void mergeSections(QVector<int> indices);

    // ── Group mutations ──
    int  addGroup(const SectionGroup &g);
    void removeGroup(int groupId);
    void renameGroup(int groupId, const QString &name);
    void moveGroup(int groupIdA, int groupIdB); ///< swap treeOrder of two groups

    // ── Queries ──
    QColor  colorAtOffset(qint64 offset) const;
    int     displayModeAtOffset(qint64 offset) const;
    QString sectionNameAtStartOffset(qint64 offset) const;
    int     sectionIndexAtStartOffset(qint64 offset) const;

    // ── Bulk set (no undo) ──
    void setSections(const QVector<Section> &sections);
    void setSectionsAndGroups(const QVector<Section> &sections,
                              const QVector<SectionGroup> &groups);

    // ── Utilities ──
    static QColor randomPastelColor();
    static qint64 romHeaderSize(RomType type);

signals:
    void sectionsChanged();
    void groupsChanged();  ///< emitted when only group data changed (no layout impact)

private:
    void setSectionsDirect(const QVector<Section> &sections,
                           const QVector<SectionGroup> &groups);
    void commitChange(const QVector<Section> &sections,
                      const QVector<SectionGroup> &groups,
                      const QString &text);
    static void sortSections(QVector<Section> &sections);

    QVector<Section>      m_sections;
    QVector<SectionGroup> m_groups;
    QUndoStack           *m_undoStack = nullptr;

    friend class SectionSnapshotCommand;
};

#endif // SECTIONLISTMODEL_H

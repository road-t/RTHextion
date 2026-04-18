#include "SectionListModel.h"
#include <QRandomGenerator>
#include <QUndoCommand>
#include <algorithm>

// ── TileCodec helpers ─────────────────────────────────────────────

int tileCodecBpp(TileCodec codec)
{
    switch (codec) {
    case TileCodec::Linear1bpp:       return 1;
    case TileCodec::Linear2bpp:
    case TileCodec::Interleaved2bpp:  return 2;
    case TileCodec::Planar3bpp:       return 3;
    case TileCodec::Interleaved4bpp:
    case TileCodec::Linear4bpp:
    case TileCodec::SegaMD4bpp:
    case TileCodec::SegaSMS4bpp:      return 4;
    case TileCodec::Linear8bpp:       return 8;
    }
    return 2;
}

int tileCodecBytesPerTile(TileCodec codec)
{
    return tileCodecBpp(codec) * 8; // 8×8 tile
}

const char *tileCodecName(TileCodec codec)
{
    switch (codec) {
    case TileCodec::Linear1bpp:       return "1bpp Linear";
    case TileCodec::Linear2bpp:       return "2bpp Planar (NES)";
    case TileCodec::Interleaved2bpp:  return "2bpp Interleaved (GB/SNES)";
    case TileCodec::Planar3bpp:       return "3bpp Planar (SNES)";
    case TileCodec::Interleaved4bpp:  return "4bpp Interleaved (SNES)";
    case TileCodec::Linear4bpp:       return "4bpp Linear (GBA)";
    case TileCodec::SegaMD4bpp:       return "4bpp Sega MD";
    case TileCodec::SegaSMS4bpp:      return "4bpp Sega SMS/GG";
    case TileCodec::Linear8bpp:       return "8bpp Linear";
    }
    return "Unknown";
}

// ── Undo command ──────────────────────────────────────────────────

class SectionSnapshotCommand : public QUndoCommand
{
public:
    SectionSnapshotCommand(SectionListModel *model,
                           const QVector<Section> &secBefore,
                           const QVector<SectionGroup> &grpBefore,
                           const QVector<Section> &secAfter,
                           const QVector<SectionGroup> &grpAfter,
                           const QString &text)
        : QUndoCommand(text), m_model(model)
        , m_secBefore(secBefore), m_secAfter(secAfter)
        , m_grpBefore(grpBefore), m_grpAfter(grpAfter) {}

    void undo() override { if (m_model) m_model->setSectionsDirect(m_secBefore, m_grpBefore); }
    void redo() override { if (m_model) m_model->setSectionsDirect(m_secAfter,  m_grpAfter);  }

private:
    SectionListModel *m_model = nullptr;
    QVector<Section>      m_secBefore, m_secAfter;
    QVector<SectionGroup> m_grpBefore, m_grpAfter;
};

// ── Helpers ───────────────────────────────────────────────────────

void SectionListModel::sortSections(QVector<Section> &sections)
{
    std::stable_sort(sections.begin(), sections.end(),
                     [](const Section &a, const Section &b) {
                         return a.startOffset < b.startOffset;
                     });
}

// ── Construction ──────────────────────────────────────────────────

SectionListModel::SectionListModel(QObject *parent)
    : QObject(parent)
{
}

// ── Internal plumbing ─────────────────────────────────────────────

void SectionListModel::setSectionsDirect(const QVector<Section> &sections,
                                         const QVector<SectionGroup> &groups)
{
    if (m_sections == sections && m_groups == groups)
        return;
    const bool secsChanged = (m_sections != sections);
    m_sections = sections;
    m_groups   = groups;
    if (secsChanged)
        emit sectionsChanged();
    else
        emit groupsChanged();
}

void SectionListModel::commitChange(const QVector<Section> &sections,
                                    const QVector<SectionGroup> &groups,
                                    const QString &text)
{
    if (sections == m_sections && groups == m_groups)
        return;
    if (m_undoStack)
        m_undoStack->push(new SectionSnapshotCommand(
            this, m_sections, m_groups, sections, groups, text));
    else
        setSectionsDirect(sections, groups);
}

// ── Computed end offset ───────────────────────────────────────────

qint64 SectionListModel::endOffsetOf(int index, qint64 fileSize) const
{
    if (index < 0 || index >= m_sections.size())
        return fileSize;
    if (index + 1 < m_sections.size())
        return m_sections[index + 1].startOffset;
    return fileSize;
}

int SectionListModel::sectionIndexAtOffset(qint64 offset) const
{
    // Binary search: find the last section whose startOffset <= offset.
    if (m_sections.isEmpty())
        return -1;
    int lo = 0, hi = m_sections.size() - 1, best = -1;
    while (lo <= hi) {
        const int mid = lo + (hi - lo) / 2;
        if (m_sections[mid].startOffset <= offset) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return best;
}

// ── Section mutations ─────────────────────────────────────────────

bool SectionListModel::addSection(const Section &s)
{
    // Reject duplicate startOffset
    for (const auto &existing : m_sections) {
        if (existing.startOffset == s.startOffset)
            return false;
    }
    QVector<Section> next = m_sections;
    Section ns = s;
    next.append(ns);
    sortSections(next);
    commitChange(next, m_groups, tr("Add section"));
    return true;
}

void SectionListModel::removeSection(int index)
{
    if (index < 0 || index >= m_sections.size())
        return;
    QVector<Section> next = m_sections;
    next.removeAt(index);
    commitChange(next, m_groups, tr("Delete section"));
}

void SectionListModel::renameSection(int index, const QString &name)
{
    if (index < 0 || index >= m_sections.size())
        return;
    QVector<Section> next = m_sections;
    next[index].name = name;
    commitChange(next, m_groups, tr("Rename section"));
}

void SectionListModel::recolorSection(int index, const QColor &color)
{
    if (index < 0 || index >= m_sections.size())
        return;
    QVector<Section> next = m_sections;
    next[index].color = color;
    commitChange(next, m_groups, tr("Recolor section"));
}

void SectionListModel::updateSection(int index, const Section &s)
{
    if (index < 0 || index >= m_sections.size())
        return;
    QVector<Section> next = m_sections;
    next[index] = s;
    sortSections(next);
    commitChange(next, m_groups, tr("Edit section"));
}

void SectionListModel::clear()
{
    if (!m_sections.isEmpty() || !m_groups.isEmpty())
        commitChange({}, {}, tr("Clear sections"));
}

void SectionListModel::applySections(const QVector<Section> &sections,
                                     const QVector<SectionGroup> &groups,
                                     const QString &text)
{
    QVector<Section> sorted = sections;
    sortSections(sorted);
    commitChange(sorted, groups, text.isEmpty() ? tr("Update sections") : text);
}

void SectionListModel::applySections(const QVector<Section> &sections,
                                     const QString &text)
{
    applySections(sections, m_groups, text);
}

void SectionListModel::mergeSections(QVector<int> indices)
{
    if (indices.size() < 2)
        return;
    std::sort(indices.begin(), indices.end());

    QVector<Section> next = m_sections;

    // Keep the first section (lowest offset), remove the rest
    for (int i = indices.size() - 1; i >= 1; --i) {
        const int ri = indices[i];
        if (ri >= 0 && ri < next.size())
            next.removeAt(ri);
    }

    // Fix group ids that may reference removed groups — they stay valid
    // because groups are separate from sections.
    sortSections(next);
    commitChange(next, m_groups, tr("Merge sections"));
}

// ── Group mutations ───────────────────────────────────────────────

int SectionListModel::addGroup(const SectionGroup &g)
{
    QVector<SectionGroup> nextG = m_groups;
    SectionGroup ng = g;
    // Assign treeOrder just after the current maximum among siblings
    int maxOrder = -1;
    for (const auto &eg : nextG)
        if (eg.parentGroupId == ng.parentGroupId && eg.treeOrder > maxOrder)
            maxOrder = eg.treeOrder;
    ng.treeOrder = maxOrder + 1;
    nextG.append(ng);
    const int newId = nextG.size() - 1;
    commitChange(m_sections, nextG, tr("Add group"));
    return newId;
}

void SectionListModel::removeGroup(int groupId)
{
    if (groupId < 0 || groupId >= m_groups.size())
        return;

    QVector<SectionGroup> nextG = m_groups;
    nextG.removeAt(groupId);

    // Fix section groupId references
    QVector<Section> nextS = m_sections;
    for (auto &s : nextS) {
        if (s.groupId == groupId)
            s.groupId = -1;
        else if (s.groupId > groupId)
            --s.groupId;
    }

    // Fix sub-group parentGroupId references: promote orphaned sub-groups to root level
    for (auto &g : nextG) {
        if (g.parentGroupId == groupId)
            g.parentGroupId = -1;
        else if (g.parentGroupId > groupId)
            --g.parentGroupId;
    }

    commitChange(nextS, nextG, tr("Delete group"));
}

void SectionListModel::renameGroup(int groupId, const QString &name)
{
    if (groupId < 0 || groupId >= m_groups.size())
        return;
    QVector<SectionGroup> nextG = m_groups;
    nextG[groupId].name = name;
    commitChange(m_sections, nextG, tr("Rename group"));
}

void SectionListModel::moveGroup(int groupIdA, int groupIdB)
{
    if (groupIdA < 0 || groupIdA >= m_groups.size()) return;
    if (groupIdB < 0 || groupIdB >= m_groups.size()) return;
    if (groupIdA == groupIdB) return;
    QVector<SectionGroup> nextG = m_groups;
    std::swap(nextG[groupIdA].treeOrder, nextG[groupIdB].treeOrder);
    commitChange(m_sections, nextG, tr("Move group"));
}

// ── Queries ───────────────────────────────────────────────────────

QColor SectionListModel::colorAtOffset(qint64 offset) const
{
    const int idx = sectionIndexAtOffset(offset);
    if (idx >= 0)
        return m_sections[idx].color;
    return QColor();
}

int SectionListModel::displayModeAtOffset(qint64 offset) const
{
    const int idx = sectionIndexAtOffset(offset);
    if (idx >= 0)
        return m_sections[idx].displayMode;
    return SectionDisplay_Default;
}

QString SectionListModel::sectionNameAtStartOffset(qint64 offset) const
{
    for (const auto &s : m_sections) {
        if (s.startOffset == offset)
            return s.name;
    }
    return QString();
}

int SectionListModel::sectionIndexAtStartOffset(qint64 offset) const
{
    for (int i = 0; i < m_sections.size(); ++i) {
        if (m_sections[i].startOffset == offset)
            return i;
    }
    return -1;
}

// ── Bulk set (no undo) ────────────────────────────────────────────

void SectionListModel::setSections(const QVector<Section> &sections)
{
    QVector<Section> sorted = sections;
    sortSections(sorted);
    setSectionsDirect(sorted, m_groups);
}

void SectionListModel::setSectionsAndGroups(const QVector<Section> &sections,
                                            const QVector<SectionGroup> &groups)
{
    QVector<Section> sorted = sections;
    sortSections(sorted);
    // If no group has a non-zero treeOrder (old project), assign defaults
    // so groups appear in their definition order.
    bool anyNonZero = false;
    for (const auto &g : groups) if (g.treeOrder != 0) { anyNonZero = true; break; }
    if (!anyNonZero && !groups.isEmpty()) {
        QVector<SectionGroup> grps = groups;
        for (int i = 0; i < grps.size(); ++i)
            grps[i].treeOrder = i;
        setSectionsDirect(sorted, grps);
        return;
    }
    setSectionsDirect(sorted, groups);
}

// ── Utilities ─────────────────────────────────────────────────────

QColor SectionListModel::randomPastelColor()
{
    const int h = QRandomGenerator::global()->bounded(360);
    return QColor::fromHsl(h, 110, 230);
}

qint64 SectionListModel::romHeaderSize(RomType type)
{
    switch (type) {
    case RomType::NES:              return 0x10;
    case RomType::SNES_SMC:
    case RomType::SNES_HIROM_SMC:  return 0x200;
    case RomType::GB:
    case RomType::GBC:             return 0x150;
    case RomType::GBA:             return 0xC0;
    case RomType::MD:
    case RomType::X32:             return 0x200;
    case RomType::N64:
    case RomType::N64_LE:
    case RomType::N64_V64:         return 0x40;
    default:                       return 0;
    }
}

#include "SectionListModel.h"
#include <QRandomGenerator>
#include <algorithm>

SectionListModel::SectionListModel(QObject *parent)
    : QObject(parent)
{
}

void SectionListModel::addSection(const Section &s)
{
    m_sections.append(s);
    emit sectionsChanged();
}

void SectionListModel::removeSection(int index)
{
    if (index < 0 || index >= m_sections.size())
        return;

    // Collect the section itself plus all direct children, sorted descending
    QVector<int> toRemove;
    toRemove.append(index);
    for (int i = 0; i < m_sections.size(); ++i) {
        if (i != index && m_sections[i].parentIndex == index)
            toRemove.append(i);
    }
    std::sort(toRemove.begin(), toRemove.end(), std::greater<int>());

    for (int ri : toRemove) {
        m_sections.removeAt(ri);
        // Fix up parentIndex for all remaining sections
        for (auto &s : m_sections) {
            if (s.parentIndex > ri)
                --s.parentIndex;
        }
    }

    emit sectionsChanged();
}

void SectionListModel::renameSection(int index, const QString &name)
{
    if (index >= 0 && index < m_sections.size()) {
        m_sections[index].name = name;
        emit sectionsChanged();
    }
}

void SectionListModel::recolorSection(int index, const QColor &color)
{
    if (index >= 0 && index < m_sections.size()) {
        m_sections[index].color = color;
        emit sectionsChanged();
    }
}

void SectionListModel::updateSection(int index, const Section &s)
{
    if (index >= 0 && index < m_sections.size()) {
        m_sections[index] = s;
        emit sectionsChanged();
    }
}

void SectionListModel::clear()
{
    if (!m_sections.isEmpty()) {
        m_sections.clear();
        emit sectionsChanged();
    }
}

QColor SectionListModel::colorAtOffset(qint64 offset) const
{
    // Pass 1: prefer subsections (parentIndex >= 0) — they are more specific
    for (const auto &s : m_sections) {
        if (s.parentIndex >= 0 && offset >= s.startOffset && offset < s.endOffset)
            return s.color;
    }
    // Pass 2: fall back to root sections
    for (const auto &s : m_sections) {
        if (s.parentIndex < 0 && offset >= s.startOffset && offset < s.endOffset)
            return s.color;
    }
    return QColor();  // invalid — no section
}

void SectionListModel::setSections(const QVector<Section> &sections)
{
    m_sections = sections;
    emit sectionsChanged();
}

QColor SectionListModel::randomPastelColor()
{
    const int h = QRandomGenerator::global()->bounded(360);
    return QColor::fromHsl(h, 110, 230);  // lighter pastel
}

qint64 SectionListModel::romHeaderSize(RomType type)
{
    switch (type) {
    case RomType::NES:              return 0x10;   // 16-byte iNES header
    case RomType::SNES_SMC:
    case RomType::SNES_HIROM_SMC:  return 0x200;  // 512-byte copier header
    case RomType::GB:
    case RomType::GBC:             return 0x150;   // GB header ends at $014F
    case RomType::GBA:             return 0xC0;    // 192 bytes
    case RomType::MD:
    case RomType::X32:             return 0x200;   // 512 bytes
    case RomType::N64:
    case RomType::N64_LE:
    case RomType::N64_V64:         return 0x40;    // 64 bytes
    default:                       return 0;
    }
}

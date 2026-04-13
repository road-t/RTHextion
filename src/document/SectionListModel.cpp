#include "SectionListModel.h"
#include <QRandomGenerator>
#include <QUndoCommand>
#include <algorithm>
#include <functional>

class SectionSnapshotCommand : public QUndoCommand
{
public:
    SectionSnapshotCommand(SectionListModel *model,
                           const QVector<Section> &before,
                           const QVector<Section> &after,
                           const QString &text)
        : QUndoCommand(text)
        , m_model(model)
        , m_before(before)
        , m_after(after)
    {
    }

    void undo() override
    {
        if (m_model)
            m_model->setSectionsDirect(m_before);
    }

    void redo() override
    {
        if (m_model)
            m_model->setSectionsDirect(m_after);
    }

private:
    SectionListModel *m_model = nullptr;
    QVector<Section> m_before;
    QVector<Section> m_after;
};

SectionListModel::SectionListModel(QObject *parent)
    : QObject(parent)
{
}

void SectionListModel::setSectionsDirect(const QVector<Section> &sections)
{
    if (m_sections == sections)
        return;

    m_sections = sections;
    emit sectionsChanged();
}

void SectionListModel::commitSectionsChange(const QVector<Section> &sections, const QString &text)
{
    if (sections == m_sections)
        return;

    if (m_undoStack)
        m_undoStack->push(new SectionSnapshotCommand(this, m_sections, sections, text));
    else
        setSectionsDirect(sections);
}

void SectionListModel::addSection(const Section &s)
{
    QVector<Section> next = m_sections;
    next.append(s);
    commitSectionsChange(next, tr("Add section"));
}

void SectionListModel::removeSection(int index)
{
    if (index < 0 || index >= m_sections.size())
        return;

    QVector<Section> next = m_sections;

    // Collect the section itself plus all descendants, sorted descending.
    QVector<int> toRemove;
    std::function<void(int)> collect = [&](int idx) {
        toRemove.append(idx);
        for (int i = 0; i < next.size(); ++i) {
            if (i != idx && next[i].parentIndex == idx)
                collect(i);
        }
    };
    collect(index);

    std::sort(toRemove.begin(), toRemove.end(), std::greater<int>());
    toRemove.erase(std::unique(toRemove.begin(), toRemove.end()), toRemove.end());

    for (int ri : toRemove) {
        next.removeAt(ri);
        for (auto &s : next) {
            if (s.parentIndex == ri)
                s.parentIndex = -1;
            else if (s.parentIndex > ri)
                --s.parentIndex;
        }
    }

    commitSectionsChange(next, tr("Delete section"));
}

void SectionListModel::renameSection(int index, const QString &name)
{
    if (index >= 0 && index < m_sections.size()) {
        QVector<Section> next = m_sections;
        next[index].name = name;
        commitSectionsChange(next, tr("Rename section"));
    }
}

void SectionListModel::recolorSection(int index, const QColor &color)
{
    if (index >= 0 && index < m_sections.size()) {
        QVector<Section> next = m_sections;
        next[index].color = color;
        commitSectionsChange(next, tr("Recolor section"));
    }
}

void SectionListModel::updateSection(int index, const Section &s)
{
    if (index >= 0 && index < m_sections.size()) {
        QVector<Section> next = m_sections;
        next[index] = s;
        commitSectionsChange(next, tr("Edit section"));
    }
}

void SectionListModel::clear()
{
    if (!m_sections.isEmpty())
        commitSectionsChange(QVector<Section>{}, tr("Clear sections"));
}

void SectionListModel::applySections(const QVector<Section> &sections, const QString &text)
{
    commitSectionsChange(sections, text.isEmpty() ? tr("Reorder sections") : text);
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

int SectionListModel::displayModeAtOffset(qint64 offset) const
{
    // Same 2-pass logic as colorAtOffset: prefer subsections.
    for (const auto &s : m_sections) {
        if (s.parentIndex >= 0 && offset >= s.startOffset && offset < s.endOffset)
            return s.displayMode;
    }
    for (const auto &s : m_sections) {
        if (s.parentIndex < 0 && offset >= s.startOffset && offset < s.endOffset)
            return s.displayMode;
    }
    return SectionDisplay_Default;
}

QString SectionListModel::sectionNameAtStartOffset(qint64 offset) const
{
    for (const auto &s : m_sections) {
        if (s.parentIndex >= 0 && s.startOffset == offset)
            return s.name;
    }
    for (const auto &s : m_sections) {
        if (s.parentIndex < 0 && s.startOffset == offset)
            return s.name;
    }
    return QString();
}

int SectionListModel::sectionIndexAtStartOffset(qint64 offset) const
{
    for (int i = 0; i < m_sections.size(); ++i) {
        if (m_sections[i].parentIndex >= 0 && m_sections[i].startOffset == offset)
            return i;
    }
    for (int i = 0; i < m_sections.size(); ++i) {
        if (m_sections[i].parentIndex < 0 && m_sections[i].startOffset == offset)
            return i;
    }
    return -1;
}

void SectionListModel::setSections(const QVector<Section> &sections)
{
    setSectionsDirect(sections);
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

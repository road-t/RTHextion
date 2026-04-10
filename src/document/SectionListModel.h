#ifndef SECTIONLISTMODEL_H
#define SECTIONLISTMODEL_H

#include <QObject>
#include <QString>
#include <QColor>
#include <QVector>
#include "romdetect.h"

/// A single named section of the file (e.g. header, data block, …).
struct Section
{
    QString name;
    qint64  startOffset = 0;
    qint64  endOffset   = 0;   // exclusive (one past last byte)
    QColor  color;
};

/// Lightweight container for file sections with paint-lookup support.
/// Emits sectionsChanged() whenever the list is mutated.
class SectionListModel : public QObject
{
    Q_OBJECT

public:
    explicit SectionListModel(QObject *parent = nullptr);

    int count() const { return m_sections.size(); }
    const Section &at(int index) const { return m_sections.at(index); }

    void addSection(const Section &s);
    void removeSection(int index);
    void renameSection(int index, const QString &name);
    void recolorSection(int index, const QColor &color);
    void clear();

    /// Returns the section colour for the given file offset, or an invalid
    /// QColor if the offset does not belong to any section.
    QColor colorAtOffset(qint64 offset) const;

    const QVector<Section> &sections() const { return m_sections; }
    void setSections(const QVector<Section> &sections);

    /// Generate a random pastel colour suitable for section backgrounds.
    static QColor randomPastelColor();

    /// Return the ROM-format header size in bytes (0 if unknown / no header).
    static qint64 romHeaderSize(RomType type);

signals:
    void sectionsChanged();

private:
    QVector<Section> m_sections;
};

#endif // SECTIONLISTMODEL_H

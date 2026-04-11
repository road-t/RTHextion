#ifndef SECTIONLISTMODEL_H
#define SECTIONLISTMODEL_H

#include <QObject>
#include <QString>
#include <QColor>
#include <QVector>
#include <QMetaType>
#include "romdetect.h"

class QUndoStack;

/// Display-mode constants for sections.
/// Values > 0 are 1-based table indices (Table 1 … Table N).
enum {
    SectionDisplay_Default = 0,   ///< follow global view mode
    SectionDisplay_Raw     = -2,  ///< raw file encoding, ignore tables
    SectionDisplay_Disasm  = -1   ///< disassembly view
};

/// A single named section of the file (e.g. header, data block, …).
struct Section
{
    QString name;
    qint64  startOffset = 0;
    qint64  endOffset   = 0;   // exclusive (one past last byte)
    QColor  color;
    int     parentIndex = -1;  // -1 = root section; >=0 = flat index of parent section
    int     displayMode = SectionDisplay_Default; // 0=Default, -2=Raw, -1=Disasm, 1..N=Table index
    RomType disasmCpu   = RomType::Unknown;       // CPU override for disasm mode (Unknown = platform default)
};

inline bool operator==(const Section &lhs, const Section &rhs)
{
    return lhs.name == rhs.name
        && lhs.startOffset == rhs.startOffset
        && lhs.endOffset == rhs.endOffset
        && lhs.color == rhs.color
        && lhs.parentIndex == rhs.parentIndex
        && lhs.displayMode == rhs.displayMode
        && lhs.disasmCpu == rhs.disasmCpu;
}

Q_DECLARE_METATYPE(Section)

/// Lightweight container for file sections with paint-lookup support.
/// Emits sectionsChanged() whenever the list is mutated.
class SectionListModel : public QObject
{
    Q_OBJECT

public:
    explicit SectionListModel(QObject *parent = nullptr);

    int count() const { return m_sections.size(); }
    const Section &at(int index) const { return m_sections.at(index); }

    void setUndoStack(QUndoStack *stack) { m_undoStack = stack; }

    void addSection(const Section &s);
    void removeSection(int index);   ///< removes the section and its descendants; fixes parent indices
    void renameSection(int index, const QString &name);
    void recolorSection(int index, const QColor &color);
    void updateSection(int index, const Section &s);
    void clear();
    void applySections(const QVector<Section> &sections, const QString &text = QString());

    /// Returns the section colour for the given file offset, or an invalid
    /// QColor if the offset does not belong to any section.
    QColor colorAtOffset(qint64 offset) const;

    /// Returns the display mode for the deepest section containing the offset.
    /// Falls back to SectionDisplay_Default if the offset is outside all sections.
    int displayModeAtOffset(qint64 offset) const;

    const QVector<Section> &sections() const { return m_sections; }
    void setSections(const QVector<Section> &sections);

    /// Generate a random pastel colour suitable for section backgrounds.
    static QColor randomPastelColor();

    /// Return the ROM-format header size in bytes (0 if unknown / no header).
    static qint64 romHeaderSize(RomType type);

signals:
    void sectionsChanged();

private:
    void setSectionsDirect(const QVector<Section> &sections);
    void commitSectionsChange(const QVector<Section> &sections, const QString &text);

    QVector<Section> m_sections;
    QUndoStack *m_undoStack = nullptr;

    friend class SectionSnapshotCommand;
};

#endif // SECTIONLISTMODEL_H

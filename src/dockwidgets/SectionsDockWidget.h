#ifndef SECTIONSDOCKWIDGET_H
#define SECTIONSDOCKWIDGET_H

#include "BaseDockWidget.h"
#include "romdetect.h"
#include <QPointer>
#include <QTreeWidget>
#include <QToolButton>

class SectionListModel;
class SectionTree;

class SectionsDockWidget : public BaseDockWidget
{
    Q_OBJECT

public:
    explicit SectionsDockWidget(QWidget *parent = nullptr);
    ~SectionsDockWidget() override;

    void setModel(SectionListModel *model);
    void setRomTypeName(const QString &name);
    void setCurrentRomType(RomType type);
    void setTableNames(const QStringList &names);
    void refresh();

    /// Suppress or allow rebuildTree() calls (useful during batch operations).
    void setSuppressRebuild(bool suppress) { m_suppressRebuild = suppress; }

    void setShowSectionsChecked(bool checked);

    void retranslateUi() override;

signals:
    void jumpToOffset(qint64 offset);
    void selectRangeRequested(qint64 startOffset, qint64 endOffset);
    void virtualFormattingRequested(qint64 startOffset, qint64 endOffset);
    void removeVirtualFormattingRequested(qint64 startOffset, qint64 endOffset);
    void showSectionsToggled(bool checked);
    // parentIndex == -1: add at ROM root; >= 0: add as child of that section
    void addSectionRequested(int parentIndex);
    void disasmCpuChanged(int sectionIdx, RomType cpu);
    void parseRequested();

public slots:
    void highlightOffset(qint64 offset);
    void startRenameSection(int sectionIndex);

protected:
    void onPaletteChanged() override;

private slots:
    void onItemClicked(QTreeWidgetItem *item, int column);
    void onTreeContextMenu(const QPoint &pos);
    void onDropped();
    void onItemChanged(QTreeWidgetItem *item, int column);

private:
    void rebuildTree();
    void syncModelFromTree();
    QIcon colorSwatchIcon(const QColor &color) const;

    QPointer<SectionTree> m_tree        = nullptr;
    QToolButton      *m_showSectionsBtn = nullptr;
    QToolButton      *m_addBtn          = nullptr;
    QPointer<SectionListModel> m_model  = nullptr;
    QString           m_romTypeName;
    QStringList       m_tableNames;
    RomType           m_currentRomType = RomType::Unknown;
    bool              m_suppressRebuild = false;
    bool              m_rebuildingTree = false;
};

#endif // SECTIONSDOCKWIDGET_H

#ifndef SECTIONSDOCKWIDGET_H
#define SECTIONSDOCKWIDGET_H

#include "BaseDockWidget.h"
#include "romdetect.h"
#include <QPointer>
#include <QSet>
#include <QTreeWidget>
#include <QToolButton>

class QDropEvent;
class SectionListModel;

class SectionsDockWidget : public BaseDockWidget
{
    Q_OBJECT

public:
    explicit SectionsDockWidget(QWidget *parent = nullptr);
    ~SectionsDockWidget() override;

    void setModel(SectionListModel *model);
    void setFileSize(qint64 size);
    void setRomTypeName(const QString &name);
    void setCurrentRomType(RomType type);
    void setTableNames(const QStringList &names);
    void refresh();

    void setSuppressRebuild(bool suppress) { m_suppressRebuild = suppress; }
    void setShowSectionsChecked(bool checked);
    QVector<int> expandedGroupIds() const;
    void setExpandedGroupIds(const QVector<int> &groupIds);

    void retranslateUi() override;

signals:
    void jumpToOffset(qint64 offset);
    void selectRangeRequested(qint64 startOffset, qint64 endOffset);
    void virtualFormattingRequested(qint64 startOffset, qint64 endOffset);
    void removeVirtualFormattingRequested(qint64 startOffset, qint64 endOffset);
    void showSectionsToggled(bool checked);
    void disasmCpuChanged(int sectionIdx, RomType cpu);
    void parseRequested();
    void detectAudioRequested();
    void findSamplesInSectionRequested(qint64 startOffset, qint64 endOffset);
    void findFunctionsInSectionRequested(qint64 startOffset, qint64 endOffset);
    void splitSectionRequested(int sectionIndex, const QVector<qint64> &sizes);
    void findPointersInSectionRequested(qint64 startOffset, qint64 endOffset);
    void dropPointersInSectionRequested(qint64 startOffset, qint64 endOffset);

public slots:
    void highlightOffset(qint64 offset);
    void startRenameSection(int sectionIndex);

protected:
    void onPaletteChanged() override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onItemClicked(QTreeWidgetItem *item, int column);
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);
    void onTreeContextMenu(const QPoint &pos);
    void onItemChanged(QTreeWidgetItem *item, int column);

private:
    void rebuildTree();
    void handleDrop(QDropEvent *event);
    QIcon colorSwatchIcon(const QColor &color) const;

    QPointer<QTreeWidget>       m_tree;
    QToolButton                *m_showSectionsBtn = nullptr;

    QPointer<SectionListModel>  m_model;
    QString                     m_romTypeName;
    QStringList                 m_tableNames;
    RomType                     m_currentRomType = RomType::Unknown;
    qint64                      m_fileSize = 0;
    qint64                      m_lastHighlightedOffset = -1;
    bool                        m_suppressRebuild = false;
    bool                        m_rebuildingTree  = false;
    QSet<int>                   m_forcedExpandedGroupIds;
    bool                        m_hasForcedExpandedGroupIds = false;
};

#endif // SECTIONSDOCKWIDGET_H

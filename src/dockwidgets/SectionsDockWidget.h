#ifndef SECTIONSDOCKWIDGET_H
#define SECTIONSDOCKWIDGET_H

#include "BaseDockWidget.h"
#include <QTreeWidget>
#include <QToolButton>

class SectionListModel;
class SectionTree;

class SectionsDockWidget : public BaseDockWidget
{
    Q_OBJECT

public:
    explicit SectionsDockWidget(QWidget *parent = nullptr);

    void setModel(SectionListModel *model);
    void setRomTypeName(const QString &name);
    void refresh();

    void setShowSectionsChecked(bool checked);

    void retranslateUi() override;

signals:
    void jumpToOffset(qint64 offset);
    void showSectionsToggled(bool checked);
    // parentIndex == -1: add at ROM root; >= 0: add as child of that section
    void addSectionRequested(int parentIndex);

public slots:
    void highlightOffset(qint64 offset);

protected:
    void onPaletteChanged() override;

private slots:
    void onItemClicked(QTreeWidgetItem *item, int column);
    void onTreeContextMenu(const QPoint &pos);
    void onDropped();

private:
    void rebuildTree();
    void syncModelFromTree();
    QIcon colorSwatchIcon(const QColor &color) const;

    SectionTree      *m_tree            = nullptr;
    QToolButton      *m_showSectionsBtn = nullptr;
    QToolButton      *m_addBtn          = nullptr;
    SectionListModel *m_model           = nullptr;
    QString           m_romTypeName;
    bool              m_suppressRebuild = false;
};

#endif // SECTIONSDOCKWIDGET_H

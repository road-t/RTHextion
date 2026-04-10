#ifndef SECTIONSDOCKWIDGET_H
#define SECTIONSDOCKWIDGET_H

#include "BaseDockWidget.h"
#include <QTreeWidget>
#include <QToolButton>

class SectionListModel;

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
    void addSectionRequested();

protected:
    void onPaletteChanged() override;

private slots:
    void onItemClicked(QTreeWidgetItem *item, int column);
    void onTreeContextMenu(const QPoint &pos);

private:
    void rebuildTree();
    QIcon colorSwatchIcon(const QColor &color) const;

    QTreeWidget *m_tree             = nullptr;
    QToolButton *m_showSectionsBtn  = nullptr;
    QToolButton *m_addBtn           = nullptr;
    SectionListModel *m_model       = nullptr;
    QString m_romTypeName;
};

#endif // SECTIONSDOCKWIDGET_H

#ifndef DISASSEMBLYDOCKWIDGET_H
#define DISASSEMBLYDOCKWIDGET_H

#include "BaseDockWidget.h"

#include <QTableWidget>
#include <QToolButton>
#include <QLabel>
#include <QPointer>

class HexEditor;
class Disassembler;
enum class RomType : int;

class DisassemblyDockWidget : public BaseDockWidget
{
    Q_OBJECT

public:
    explicit DisassemblyDockWidget(QWidget *parent = nullptr);
    ~DisassemblyDockWidget() override;

    /// Bind to a hex editor. Call when tab changes.
    void setHexEdit(HexEditor *hexEdit);

    /// Set the ROM type for disassembly.  Reconfigures the Capstone engine.
    void setRomType(RomType type);

    /// Refresh disassembly at the current cursor position.
    void refresh();

    /// Clear all rows.
    void clear();

    QByteArray saveColumnsState() const;
    void restoreColumnsState(const QByteArray &state);

    void retranslateUi() override;

signals:
    /// Emitted when user clicks a branch link → navigate hex editor there.
    void jumpToOffset(qint64 offset);

protected:
    void onPaletteChanged() override;

private slots:
    void onCursorPositionChanged(qint64 offset);
    void onCellClicked(int row, int column);

private:
    void disassembleAt(qint64 fileOffset);
    void updateSupportLabel();

    QPointer<HexEditor>  m_hexEdit;
    Disassembler        *m_disasm      = nullptr;
    QTableWidget        *m_table       = nullptr;
    QLabel              *m_statusLabel = nullptr;
    QToolButton         *m_followBtn   = nullptr;

    bool  m_followCursor = true; ///< Auto-update when cursor moves
    qint64 m_lastOffset  = -1;  ///< Last disassembled offset (avoid redundant refreshes)
    RomType m_romType;
};

#endif // DISASSEMBLYDOCKWIDGET_H

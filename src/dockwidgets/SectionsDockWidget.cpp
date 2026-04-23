#include "SectionsDockWidget.h"
#include "SectionListModel.h"
#include "DockTitleBar.h"
#include "disassembler.h"
#include "audiodetector.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QColorDialog>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QHash>
#include <QMap>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalBlocker>
#include <QDropEvent>
#include <QMimeData>

// Item data roles:
//   Qt::UserRole     = section startOffset (qint64), or -1 for non-section items
//   Qt::UserRole + 1 = flat section index (int), or -1 for root/group items
//   Qt::UserRole + 2 = group id (int), or -1 for non-group items

static constexpr int kRoleSectionOffset = Qt::UserRole;
static constexpr int kRoleSectionIndex  = Qt::UserRole + 1;
static constexpr int kRoleGroupId       = Qt::UserRole + 2;

static QString audioFormatLabel(AudioSampleFormat fmt)
{
    switch (fmt) {
    case AudioSampleFormat::Unknown:          return QObject::tr("Auto");
    case AudioSampleFormat::SNES_BRR:         return QStringLiteral("SNES BRR");
    case AudioSampleFormat::NES_DPCM:         return QStringLiteral("NES DPCM");
    case AudioSampleFormat::MD_DAC_PCM:       return QStringLiteral("MD DAC PCM (unsigned 8-bit)");
    case AudioSampleFormat::MD_PCM8_Signed:   return QStringLiteral("MD Signed 8-bit PCM");
    case AudioSampleFormat::MD_ULAW:          return QStringLiteral("MD µ-law");
    case AudioSampleFormat::MD_DPCM4_6500:    return QStringLiteral("IMA ADPCM 4-bit (UMK3)");
    case AudioSampleFormat::MD_ADPCM_OKI:     return QStringLiteral("OKI/Dialogic ADPCM 4-bit");
    case AudioSampleFormat::GBA_PCM8:         return QStringLiteral("GBA PCM8");
    case AudioSampleFormat::GB_Wave4bit:      return QStringLiteral("GB Wave 4-bit");
    case AudioSampleFormat::Raw_PCM8_Signed:  return QStringLiteral("Raw PCM8 Signed");
    case AudioSampleFormat::Raw_PCM8_Unsigned:return QStringLiteral("Raw PCM8 Unsigned");
    }
    return QObject::tr("Auto");
}

static QString audioFormatMnemonic(AudioSampleFormat fmt)
{
    switch (fmt) {
    case AudioSampleFormat::SNES_BRR:          return QStringLiteral("snes-brr");
    case AudioSampleFormat::NES_DPCM:          return QStringLiteral("nes-dpcm");
    case AudioSampleFormat::MD_DAC_PCM:        return QStringLiteral("md-dac-pcm");
    case AudioSampleFormat::MD_PCM8_Signed:    return QStringLiteral("md-pcm8-signed");
    case AudioSampleFormat::MD_ULAW:           return QStringLiteral("md-ulaw");
    case AudioSampleFormat::MD_DPCM4_6500:     return QStringLiteral("md-dpcm4-6500");
    case AudioSampleFormat::MD_ADPCM_OKI:      return QStringLiteral("md-adpcm-oki");
    case AudioSampleFormat::GBA_PCM8:          return QStringLiteral("gba-pcm8");
    case AudioSampleFormat::GB_Wave4bit:       return QStringLiteral("gb-wave4bit");
    case AudioSampleFormat::Raw_PCM8_Signed:   return QStringLiteral("raw-pcm8-signed");
    case AudioSampleFormat::Raw_PCM8_Unsigned: return QStringLiteral("raw-pcm8-unsigned");
    case AudioSampleFormat::Unknown:
    default:
        return QStringLiteral("auto");
    }
}

static QVector<AudioSampleFormat> allAudioFormats()
{
    return {
        AudioSampleFormat::Unknown,
        AudioSampleFormat::SNES_BRR,
        AudioSampleFormat::NES_DPCM,
        AudioSampleFormat::MD_DAC_PCM,
        AudioSampleFormat::MD_PCM8_Signed,
        AudioSampleFormat::MD_ULAW,
        AudioSampleFormat::MD_DPCM4_6500,
        AudioSampleFormat::MD_ADPCM_OKI,
        AudioSampleFormat::GBA_PCM8,
        AudioSampleFormat::GB_Wave4bit,
        AudioSampleFormat::Raw_PCM8_Unsigned,
        AudioSampleFormat::Raw_PCM8_Signed,
    };
}

static QVector<AudioSampleFormat> preferredAudioFormatsForRom(RomType rom)
{
    switch (rom) {
    case RomType::SNES:
    case RomType::SNES_SMC:
    case RomType::SNES_HIROM:
    case RomType::SNES_HIROM_SMC:
        return {AudioSampleFormat::SNES_BRR};
    case RomType::NES:
        return {AudioSampleFormat::NES_DPCM};
    case RomType::MD:
    case RomType::X32:
        return {
            AudioSampleFormat::MD_DAC_PCM,
            AudioSampleFormat::MD_PCM8_Signed,
            AudioSampleFormat::MD_ULAW,
            AudioSampleFormat::MD_DPCM4_6500,
            AudioSampleFormat::MD_ADPCM_OKI,
        };
    case RomType::GBA:
        return {AudioSampleFormat::GBA_PCM8};
    case RomType::GB:
    case RomType::GBC:
        return {AudioSampleFormat::GB_Wave4bit};
    default:
        return {};
    }
}

static QVector<TileCodec> allTileCodecs()
{
    QVector<TileCodec> codecs;
    for (int ci = 0; ci <= static_cast<int>(TileCodec::Linear8bpp); ++ci)
        codecs.append(static_cast<TileCodec>(ci));
    return codecs;
}

static QVector<TileCodec> preferredTileCodecsForRom(RomType rom)
{
    switch (rom) {
    case RomType::NES:
        return {TileCodec::Linear2bpp};
    case RomType::GB:
    case RomType::GBC:
        return {TileCodec::Interleaved2bpp};
    case RomType::SNES:
    case RomType::SNES_SMC:
    case RomType::SNES_HIROM:
    case RomType::SNES_HIROM_SMC:
        return {TileCodec::Interleaved4bpp, TileCodec::Planar3bpp, TileCodec::Interleaved2bpp};
    case RomType::GBA:
        return {TileCodec::Linear4bpp, TileCodec::Linear8bpp};
    case RomType::MD:
    case RomType::X32:
        return {TileCodec::SegaMD4bpp};
    case RomType::SMS:
    case RomType::GG:
    case RomType::SG1000:
    case RomType::ColecoVision:
        return {TileCodec::SegaSMS4bpp};
    default:
        return {};
    }
}

static QString displayMnemonicForMode(int mode)
{
    if (mode == SectionDisplay_Default)
        return QStringLiteral("auto");
    if (mode == SectionDisplay_Raw)
        return QStringLiteral("raw");
    if (mode == SectionDisplay_Graphics)
        return QStringLiteral("gfx");
    if (mode == SectionDisplay_Audio)
        return QStringLiteral("snd");
    if (mode == SectionDisplay_Disasm)
        return QStringLiteral("asm");
    if (mode > 0)
        return QStringLiteral("txt");
    return QStringLiteral("auto");
}

static QMap<QString, QString> parseSectionOptions(const QString &raw)
{
    QMap<QString, QString> out;
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty())
        return out;

    // Backward compatibility for older JSON-in-YAML projects.
    if (trimmed.startsWith(QLatin1Char('{'))) {
        const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8());
        if (doc.isObject()) {
            const QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                const QString key = it.key().trimmed().toLower();
                if (key.isEmpty())
                    continue;
                const QJsonValue value = it.value();
                QString text;
                if (value.isString())
                    text = value.toString().trimmed();
                else if (value.isDouble())
                    text = QString::number(value.toDouble(), 'g', 15);
                else if (value.isBool())
                    text = value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
                if (!text.isEmpty())
                    out.insert(key, text);
            }
            return out;
        }
    }

    const QStringList pairs = trimmed.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &pairRaw : pairs) {
        const QString pair = pairRaw.trimmed();
        if (pair.isEmpty())
            continue;
        const int eq = pair.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString key = pair.left(eq).trimmed().toLower();
        const QString val = pair.mid(eq + 1).trimmed();
        if (!key.isEmpty() && !val.isEmpty())
            out.insert(key, val);
    }
    return out;
}

static QString serializeSectionOptions(const QMap<QString, QString> &opts)
{
    QStringList parts;
    for (auto it = opts.constBegin(); it != opts.constEnd(); ++it) {
        const QString key = it.key().trimmed().toLower();
        const QString value = it.value().trimmed();
        if (key.isEmpty() || value.isEmpty())
            continue;
        parts.append(key + QStringLiteral("=") + value);
    }
    return parts.join(QLatin1Char(';'));
}

static AudioSampleFormat audioFormatFromMnemonic(const QString &mnemonic)
{
    const QString m = mnemonic.trimmed().toLower();
    if (m == QLatin1String("snes-brr")) return AudioSampleFormat::SNES_BRR;
    if (m == QLatin1String("nes-dpcm")) return AudioSampleFormat::NES_DPCM;
    if (m == QLatin1String("md-dac-pcm")) return AudioSampleFormat::MD_DAC_PCM;
    if (m == QLatin1String("md-pcm8-signed")) return AudioSampleFormat::MD_PCM8_Signed;
    if (m == QLatin1String("md-ulaw")) return AudioSampleFormat::MD_ULAW;
    if (m == QLatin1String("md-dpcm4-6500")) return AudioSampleFormat::MD_DPCM4_6500;
    if (m == QLatin1String("md-adpcm-oki")) return AudioSampleFormat::MD_ADPCM_OKI;
    if (m == QLatin1String("gba-pcm8")) return AudioSampleFormat::GBA_PCM8;
    if (m == QLatin1String("gb-wave4bit")) return AudioSampleFormat::GB_Wave4bit;
    if (m == QLatin1String("raw-pcm8-unsigned")) return AudioSampleFormat::Raw_PCM8_Unsigned;
    if (m == QLatin1String("raw-pcm8-signed")) return AudioSampleFormat::Raw_PCM8_Signed;
    return AudioSampleFormat::Unknown;
}

static QString currentAudioTypeForSection(const Section &s)
{
    const QMap<QString, QString> opts = parseSectionOptions(s.options);
    const QString t = opts.value(QStringLiteral("type")).trimmed().toLower();
    if (!t.isEmpty())
        return t;
    return QStringLiteral("auto");
}

static QString optionsWithAudioType(const QString &existingOptions, const QString &audioType)
{
    QMap<QString, QString> opts = parseSectionOptions(existingOptions);
    opts.insert(QStringLiteral("type"), audioType.trimmed().isEmpty() ? QStringLiteral("auto") : audioType.trimmed().toLower());
    if (!opts.contains(QStringLiteral("sample_rate")))
        opts.insert(QStringLiteral("sample_rate"), QStringLiteral("0"));
    if (!opts.contains(QStringLiteral("speed")))
        opts.insert(QStringLiteral("speed"), QStringLiteral("1.0"));
    return serializeSectionOptions(opts);
}

SectionsDockWidget::~SectionsDockWidget() = default;

SectionsDockWidget::SectionsDockWidget(QWidget *parent)
    : BaseDockWidget(tr("Sections"), parent)
{
    setWindowTitle(tr("Sections"));
    setObjectName(QStringLiteral("SectionsDockWidget"));

    auto *container = new QWidget(this);
    m_contentWidget = container;
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);

    // Toolbar
    auto *toolRow = new QHBoxLayout();
    toolRow->setContentsMargins(0, 0, 0, 0);
    toolRow->setSpacing(4);

    m_showSectionsBtn = new QToolButton(this);
    m_showSectionsBtn->setCheckable(true);
    m_showSectionsBtn->setChecked(true);
    m_showSectionsBtn->setAutoRaise(true);
    m_showSectionsBtn->setToolTip(tr("Show section colors"));
    m_showSectionsBtn->setIcon(makeEyeIcon(palette().color(QPalette::WindowText)));
    m_showSectionsBtn->setIconSize(QSize(16, 16));
    toolRow->addWidget(m_showSectionsBtn);

    toolRow->addStretch();
    layout->addLayout(toolRow);

    // Tree widget
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({tr("Name"), tr("Offset"), tr("Size")});
    m_tree->setColumnCount(3);
    m_tree->header()->setStretchLastSection(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setRootIsDecorated(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setDragEnabled(true);
    m_tree->setAcceptDrops(true);
    m_tree->setDropIndicatorShown(true);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    m_tree->viewport()->installEventFilter(this);
    layout->addWidget(m_tree);

    setWidget(container);
    initTitleBar();

    connect(m_showSectionsBtn, &QToolButton::toggled,
            this, &SectionsDockWidget::showSectionsToggled);
    connect(m_tree, &QTreeWidget::itemClicked,
            this, &SectionsDockWidget::onItemClicked);
    connect(m_tree, &QTreeWidget::itemDoubleClicked,
            this, &SectionsDockWidget::onItemDoubleClicked);
    connect(m_tree, &QTreeWidget::itemChanged,
            this, &SectionsDockWidget::onItemChanged);
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &SectionsDockWidget::onTreeContextMenu);
}

void SectionsDockWidget::setModel(SectionListModel *model)
{
    if (m_model)
        disconnect(m_model, nullptr, this, nullptr);
    m_model = model;
    if (m_model) {
        connect(m_model, &SectionListModel::sectionsChanged,
                this, &SectionsDockWidget::rebuildTree);
        connect(m_model, &SectionListModel::groupsChanged,
                this, &SectionsDockWidget::rebuildTree);
    }
    rebuildTree();
}

void SectionsDockWidget::setFileSize(qint64 size)
{
    if (m_fileSize != size) {
        m_fileSize = size;
        rebuildTree();
    }
}

void SectionsDockWidget::setRomTypeName(const QString &name)
{
    if (m_romTypeName != name) {
        m_romTypeName = name;
        rebuildTree();
    }
}

void SectionsDockWidget::setCurrentRomType(RomType type) { m_currentRomType = type; }
void SectionsDockWidget::setTableNames(const QStringList &names) { m_tableNames = names; }
void SectionsDockWidget::refresh() { rebuildTree(); }

void SectionsDockWidget::setShowSectionsChecked(bool checked)
{
    const QSignalBlocker blocker(m_showSectionsBtn);
    m_showSectionsBtn->setChecked(checked);
}

QVector<int> SectionsDockWidget::expandedGroupIds() const
{
    QVector<int> result;
    if (!m_tree)
        return result;

    std::function<void(QTreeWidgetItem *)> collect = [&](QTreeWidgetItem *item) {
        const int gid = item->data(0, kRoleGroupId).toInt();
        if (gid >= 0 && item->isExpanded())
            result.append(gid);
        for (int i = 0; i < item->childCount(); ++i)
            collect(item->child(i));
    };

    if (QTreeWidgetItem *root = m_tree->topLevelItem(0))
        collect(root);

    return result;
}

void SectionsDockWidget::setExpandedGroupIds(const QVector<int> &groupIds)
{
    m_forcedExpandedGroupIds.clear();
    for (int gid : groupIds)
        m_forcedExpandedGroupIds.insert(gid);
    m_hasForcedExpandedGroupIds = true;
}

void SectionsDockWidget::retranslateUi()
{
    setWindowTitle(tr("Sections"));
    m_showSectionsBtn->setToolTip(tr("Show section colors"));
    m_tree->setHeaderLabels({tr("Name"), tr("Offset"), tr("Size")});
}

void SectionsDockWidget::onPaletteChanged()
{
    const QColor fg = palette().color(QPalette::WindowText);
    m_showSectionsBtn->setIcon(makeEyeIcon(fg));
    rebuildTree();
}

void SectionsDockWidget::onItemClicked(QTreeWidgetItem *item, int /*column*/)
{
    const qint64 offset = item->data(0, kRoleSectionOffset).toLongLong();
    if (offset >= 0)
        emit jumpToOffset(offset);
}

void SectionsDockWidget::onItemDoubleClicked(QTreeWidgetItem *item, int /*column*/)
{
    const int sectionIdx = item->data(0, kRoleSectionIndex).toInt();
    if (sectionIdx < 0 || !m_model || sectionIdx >= m_model->count())
        return;
    const qint64 start = m_model->at(sectionIdx).startOffset;
    const qint64 end   = m_model->endOffsetOf(sectionIdx, m_fileSize);
    if (end > start)
        emit selectRangeRequested(start, end);
}

// ── Context menu ──────────────────────────────────────────────────

void SectionsDockWidget::onTreeContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_tree->itemAt(pos);
    if (!item || !m_model)
        return;

    const int sectionIdx = item->data(0, kRoleSectionIndex).toInt();
    const int groupId    = item->data(0, kRoleGroupId).toInt();

    auto addAudioActions = [this](QMenu *menu,
                                  bool checkable,
                                  const QString &checkedType,
                                  QHash<QAction *, QString> &actionToType)
    {
        const QVector<AudioSampleFormat> preferred = preferredAudioFormatsForRom(m_currentRomType);
        const QVector<AudioSampleFormat> all = allAudioFormats();

        auto appendFmt = [&](AudioSampleFormat fmt) {
            QAction *a = menu->addAction(audioFormatLabel(fmt));
            a->setCheckable(checkable);
            const QString t = audioFormatMnemonic(fmt);
            a->setChecked(checkable && checkedType == t);
            actionToType.insert(a, t);
        };

        for (AudioSampleFormat fmt : preferred)
            appendFmt(fmt);

        if (m_currentRomType != RomType::Unknown && !preferred.isEmpty())
            menu->addSeparator();

        for (AudioSampleFormat fmt : all) {
            if (preferred.contains(fmt))
                continue;
            appendFmt(fmt);
        }
    };

    auto addGfxCodecActions = [this](QMenu *menu,
                                     bool checkable,
                                     bool graphicsModeChecked,
                                     TileCodec checkedCodec,
                                     QHash<QAction *, TileCodec> &actionToCodec)
    {
        const QVector<TileCodec> preferred = preferredTileCodecsForRom(m_currentRomType);
        const QVector<TileCodec> all = allTileCodecs();

        auto appendCodec = [&](TileCodec tc) {
            QAction *a = menu->addAction(QString::fromLatin1(tileCodecName(tc)));
            a->setCheckable(checkable);
            a->setChecked(checkable && graphicsModeChecked && checkedCodec == tc);
            actionToCodec.insert(a, tc);
        };

        for (TileCodec tc : preferred)
            appendCodec(tc);

        if (m_currentRomType != RomType::Unknown && !preferred.isEmpty())
            menu->addSeparator();

        for (TileCodec tc : all) {
            if (preferred.contains(tc))
                continue;
            appendCodec(tc);
        }
    };

    auto addDisasmCpuActions = [this](QMenu *menu,
                                      bool checkable,
                                      bool disasmModeChecked,
                                      RomType checkedCpu,
                                      QHash<QAction *, RomType> &actionToCpu)
    {
        const QVector<DisasmCpuEntry> all = disasmSupportedCpus();
        const RomType platformCanon = disasmCanonicalRom(m_currentRomType);
        const RomType checkedCanon = disasmCanonicalRom(checkedCpu);
        const bool hasPreferred = (m_currentRomType != RomType::Unknown)
                               && (disasmCpuName(m_currentRomType) != nullptr)
                               && (platformCanon != RomType::Unknown);

        auto appendCpu = [&](const DisasmCpuEntry &cpu) {
            QAction *a = menu->addAction(QString::fromLatin1(cpu.cpuName));
            a->setCheckable(checkable);
            a->setChecked(checkable
                          && disasmModeChecked
                          && checkedCanon == disasmCanonicalRom(cpu.representativeRom));
            actionToCpu.insert(a, cpu.representativeRom);
        };

        if (hasPreferred) {
            for (const auto &cpu : all) {
                if (disasmCanonicalRom(cpu.representativeRom) == platformCanon)
                    appendCpu(cpu);
            }
            menu->addSeparator();
        }

        for (const auto &cpu : all) {
            if (hasPreferred && disasmCanonicalRom(cpu.representativeRom) == platformCanon)
                continue;
            appendCpu(cpu);
        }
    };

    // ── ROM root node ──
    if (sectionIdx < 0 && groupId < 0) {
        QMenu menu(this);
        QAction *parseAct = menu.addAction(tr("Parse"));
        QAction *detectAudioAct = menu.addAction(tr("Detect audio samples"));
        detectAudioAct->setEnabled(m_currentRomType != RomType::Unknown);
        menu.addSeparator();
        QAction *chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
        if (chosen == parseAct)
            emit parseRequested();
        else if (chosen == detectAudioAct)
            emit detectAudioRequested();
        return;
    }

    // ── Group node ──
    if (groupId >= 0 && sectionIdx < 0) {
        QSet<int> groupTreeIds;
        groupTreeIds.insert(groupId);
        bool expanded = true;
        while (expanded) {
            expanded = false;
            for (int i = 0; i < m_model->groupCount(); ++i) {
                if (groupTreeIds.contains(i))
                    continue;
                if (groupTreeIds.contains(m_model->groupAt(i).parentGroupId)) {
                    groupTreeIds.insert(i);
                    expanded = true;
                }
            }
        }

        QVector<int> groupSectionIndices;
        for (int si = 0; si < m_model->count(); ++si) {
            if (groupTreeIds.contains(m_model->at(si).groupId))
                groupSectionIndices.append(si);
        }

        // Find adjacent sibling groups (same parentGroupId) by treeOrder
        const int myOrder  = m_model->groupAt(groupId).treeOrder;
        const int myParent = m_model->groupAt(groupId).parentGroupId;
        int prevGroupId = -1, nextGroupId = -1;
        for (int i = 0; i < m_model->groupCount(); ++i) {
            if (i == groupId) continue;
            if (m_model->groupAt(i).parentGroupId != myParent) continue;
            const int to = m_model->groupAt(i).treeOrder;
            if (to < myOrder && (prevGroupId < 0 || to > m_model->groupAt(prevGroupId).treeOrder))
                prevGroupId = i;
            else if (to > myOrder && (nextGroupId < 0 || to < m_model->groupAt(nextGroupId).treeOrder))
                nextGroupId = i;
        }

        QMenu menu(this);
        QAction *renameAct      = menu.addAction(tr("Rename group"));
        QAction *subGroupAct    = menu.addAction(tr("Create sub-group"));
        menu.addSeparator();
        QAction *moveUpAct      = menu.addAction(tr("Move up"));
        QAction *moveDownAct    = menu.addAction(tr("Move down"));
        moveUpAct->setEnabled(prevGroupId >= 0);
        moveDownAct->setEnabled(nextGroupId >= 0);

        menu.addSeparator();
        QMenu *displayMenu = menu.addMenu(tr("Display mode"));
        displayMenu->setEnabled(!groupSectionIndices.isEmpty());

        QAction *actDefault = displayMenu->addAction(tr("Default"));
        actDefault->setData(SectionDisplay_Default);

        QMenu *textMenu = displayMenu->addMenu(tr("Text"));
        QAction *actRaw = textMenu->addAction(tr("Raw"));
        actRaw->setData(SectionDisplay_Raw);

        QSet<QAction *> textModeActions;
        textModeActions.insert(actRaw);
        if (!m_tableNames.isEmpty()) {
            textMenu->addSeparator();
            for (int ti = 0; ti < m_tableNames.size(); ++ti) {
                QAction *a = textMenu->addAction(m_tableNames[ti]);
                a->setData(ti + 1);
                textModeActions.insert(a);
            }
        }

        displayMenu->addSeparator();
        QMenu *audioMenu = displayMenu->addMenu(tr("Audio"));
        QHash<QAction *, QString> audioActions;
        addAudioActions(audioMenu, false, QString(), audioActions);

        displayMenu->addSeparator();
        QMenu *gfxMenu = displayMenu->addMenu(tr("Graphics"));
        QHash<QAction *, TileCodec> gfxActions;
        addGfxCodecActions(gfxMenu, false, false, TileCodec::Linear2bpp, gfxActions);

        displayMenu->addSeparator();
        QMenu *disasmMenu = displayMenu->addMenu(tr("Disassembly"));
        QHash<QAction *, RomType> disasmActions;
        addDisasmCpuActions(disasmMenu, false, false, RomType::Unknown, disasmActions);

        menu.addSeparator();
        QAction *deleteAct = menu.addAction(tr("Delete group"));
        QAction *chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
        if (chosen == renameAct) {
            bool ok = false;
            const QString name = QInputDialog::getText(
                this, tr("Rename group"), tr("Group name:"),
                QLineEdit::Normal, m_model->groupAt(groupId).name, &ok);
            if (ok && !name.isEmpty())
                m_model->renameGroup(groupId, name);
        } else if (chosen == subGroupAct) {
            bool ok = false;
            const QString name = QInputDialog::getText(
                this, tr("Create sub-group"), tr("Sub-group name:"),
                QLineEdit::Normal, tr("Sub-group"), &ok);
            if (ok && !name.isEmpty()) {
                SectionGroup ng;
                ng.name = name;
                ng.color = SectionListModel::randomPastelColor();
                ng.parentGroupId = groupId;
                m_model->addGroup(ng);
            }
        } else if (chosen == moveUpAct && prevGroupId >= 0) {
            m_model->moveGroup(groupId, prevGroupId);
        } else if (chosen == moveDownAct && nextGroupId >= 0) {
            m_model->moveGroup(groupId, nextGroupId);
        } else if (audioActions.contains(chosen)
                || gfxActions.contains(chosen)
                || disasmActions.contains(chosen)
                || chosen == actDefault
                || textModeActions.contains(chosen)) {
            QVector<Section> secs = m_model->sections();
            bool changed = false;

            for (int idx : groupSectionIndices) {
                if (idx < 0 || idx >= secs.size())
                    continue;
                Section s = secs[idx];

                if (disasmActions.contains(chosen)) {
                    const RomType cpu = disasmActions.value(chosen);
                    if (s.displayMode != SectionDisplay_Disasm || s.disasmCpu != cpu) {
                        s.displayMode = SectionDisplay_Disasm;
                        s.display = displayMnemonicForMode(s.displayMode);
                        s.disasmCpu = cpu;
                        changed = true;
                    }
                } else if (gfxActions.contains(chosen)) {
                    const TileCodec tc = gfxActions.value(chosen);
                    if (s.displayMode != SectionDisplay_Graphics || s.tileCodec != tc) {
                        s.displayMode = SectionDisplay_Graphics;
                        s.display = displayMnemonicForMode(s.displayMode);
                        s.tileCodec = tc;
                        if (s.color.alpha() > 200)
                            s.color = QColor(0x80, 0xFF, 0x80, 0x40);
                        changed = true;
                    }
                } else if (audioActions.contains(chosen)) {
                    const QString t = audioActions.value(chosen);
                    const QString newOptions = optionsWithAudioType(s.options, t);
                    if (s.displayMode != SectionDisplay_Audio || s.options != newOptions || s.display != QStringLiteral("snd")) {
                        s.displayMode = SectionDisplay_Audio;
                        s.display = QStringLiteral("snd");
                        s.options = newOptions;
                        s.color = QColor(0x40, 0xA0, 0xFF, 0x40);
                        changed = true;
                    }
                } else {
                    const int newMode = chosen->data().toInt();
                    if (s.displayMode != newMode) {
                        s.displayMode = newMode;
                        s.display = displayMnemonicForMode(s.displayMode);
                        changed = true;
                    }
                }

                secs[idx] = s;
            }

            if (changed)
                m_model->applySections(secs, tr("Set display mode for group"));
        } else if (chosen == deleteAct) {
            m_model->removeGroup(groupId);
        }
        return;
    }

    // ── Multi-selection batch ──
    const QList<QTreeWidgetItem *> selectedItems = m_tree->selectedItems();
    QVector<int> selectedIndices;
    for (QTreeWidgetItem *sel : selectedItems) {
        const int idx = sel->data(0, kRoleSectionIndex).toInt();
        if (idx >= 0 && idx < m_model->count())
            selectedIndices.append(idx);
    }

    if (selectedIndices.size() > 1) {
        QMenu menu(this);

        // Merge
        QAction *mergeAct = menu.addAction(tr("Merge %1 sections").arg(selectedIndices.size()));

        // Batch display mode
        QMenu *displayMenu = menu.addMenu(tr("Set display mode (%1 sections)").arg(selectedIndices.size()));
        QAction *actDefault = displayMenu->addAction(tr("Default"));
        actDefault->setData(SectionDisplay_Default);

        QMenu *textMenu = displayMenu->addMenu(tr("Text"));
        QAction *actRaw = textMenu->addAction(tr("Raw"));
        actRaw->setData(SectionDisplay_Raw);

        QSet<QAction *> textModeActions;
        textModeActions.insert(actRaw);
        if (!m_tableNames.isEmpty()) {
            textMenu->addSeparator();
            for (int ti = 0; ti < m_tableNames.size(); ++ti) {
                QAction *a = textMenu->addAction(m_tableNames[ti]);
                a->setData(ti + 1);
                textModeActions.insert(a);
            }
        }

        displayMenu->addSeparator();
        QMenu *audioMenu = displayMenu->addMenu(tr("Audio"));
        QHash<QAction *, QString> audioActions;
        addAudioActions(audioMenu, false, QString(), audioActions);

        displayMenu->addSeparator();
        QMenu *gfxMenu = displayMenu->addMenu(tr("Graphics"));
        QHash<QAction *, TileCodec> gfxActions;
        addGfxCodecActions(gfxMenu, false, false, TileCodec::Linear2bpp, gfxActions);

        displayMenu->addSeparator();
        QMenu *disasmMenu = displayMenu->addMenu(tr("Disassembly"));
        QHash<QAction *, RomType> disasmActions;
        addDisasmCpuActions(disasmMenu, false, false, RomType::Unknown, disasmActions);

        // Group
        QAction *groupAct = menu.addAction(tr("Group"));

        menu.addSeparator();
        QAction *deleteAct = menu.addAction(tr("Delete %1 sections").arg(selectedIndices.size()));

        QAction *chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
        if (!chosen) return;

        if (chosen == mergeAct) {
            m_model->mergeSections(selectedIndices);
        } else if (chosen == deleteAct) {
            std::sort(selectedIndices.begin(), selectedIndices.end(), std::greater<int>());
            for (int idx : selectedIndices)
                m_model->removeSection(idx);
        } else if (chosen == groupAct) {
            bool ok = false;
            const QString name = QInputDialog::getText(
                this, tr("Group"), tr("Group name:"),
                QLineEdit::Normal, tr("Group"), &ok);
            if (ok && !name.isEmpty()) {
                int parentGroupId = -1;
                if (!selectedIndices.isEmpty()) {
                    parentGroupId = m_model->at(selectedIndices.first()).groupId;
                    for (int idx : selectedIndices) {
                        if (m_model->at(idx).groupId != parentGroupId) {
                            parentGroupId = -1;
                            break;
                        }
                    }
                }
                SectionGroup ng;
                ng.name = name;
                ng.color = SectionListModel::randomPastelColor();
                ng.parentGroupId = parentGroupId;
                const int newGid = m_model->addGroup(ng);
                QVector<Section> secs = m_model->sections();
                for (int idx : selectedIndices)
                    secs[idx].groupId = newGid;
                m_model->applySections(secs, tr("Group sections"));
            }
        } else if (disasmActions.contains(chosen)) {
            const RomType cpu = disasmActions.value(chosen);
            for (int idx : selectedIndices) {
                Section s = m_model->at(idx);
                s.displayMode = SectionDisplay_Disasm;
                s.display = displayMnemonicForMode(s.displayMode);
                s.disasmCpu = cpu;
                m_model->updateSection(idx, s);
            }
        } else if (gfxActions.contains(chosen)) {
            const TileCodec tc = gfxActions.value(chosen);
            for (int idx : selectedIndices) {
                Section s = m_model->at(idx);
                s.displayMode = SectionDisplay_Graphics;
                s.display = displayMnemonicForMode(s.displayMode);
                s.tileCodec = tc;
                s.color = QColor(0x80, 0xFF, 0x80, 0x40);
                m_model->updateSection(idx, s);
            }
        } else if (audioActions.contains(chosen)) {
            const QString t = audioActions.value(chosen);
            for (int idx : selectedIndices) {
                Section s = m_model->at(idx);
                s.displayMode = SectionDisplay_Audio;
                s.display = QStringLiteral("snd");
                s.options = optionsWithAudioType(s.options, t);
                s.color = QColor(0x40, 0xA0, 0xFF, 0x40);
                m_model->updateSection(idx, s);
            }
        } else if (chosen == actDefault || textModeActions.contains(chosen)) {
            const int newMode = chosen->data().toInt();
            for (int idx : selectedIndices) {
                Section s = m_model->at(idx);
                s.displayMode = newMode;
                s.display = displayMnemonicForMode(s.displayMode);
                m_model->updateSection(idx, s);
            }
        }
        return;
    }

    // ── Single section ──
    if (sectionIdx < 0 || sectionIdx >= m_model->count())
        return;

    const Section &section = m_model->at(sectionIdx);

    QMenu menu(this);
    QAction *renameAct  = menu.addAction(tr("Rename"));
    QAction *recolorAct = menu.addAction(tr("Change color"));

    const qint64 secStart = section.startOffset;
    const qint64 secEnd   = m_model->endOffsetOf(sectionIdx, m_fileSize);
    QAction *vfFormatAct = menu.addAction(tr("Virtually format") + "...");
    QAction *vfRemoveAct = menu.addAction(tr("Remove virtual formatting"));
    menu.addSeparator();
    QAction *findSamplesAct = menu.addAction(tr("Find samples in section"));
    findSamplesAct->setEnabled(m_currentRomType != RomType::Unknown);
    QAction *findFunctionsAct = menu.addAction(tr("Find functions in section"));
    findFunctionsAct->setEnabled(m_currentRomType != RomType::Unknown);
    menu.addSeparator();

    // Display mode submenu
    QMenu *displayMenu = menu.addMenu(tr("Display mode"));
    const int curMode = section.displayMode;

    QAction *actDefault = displayMenu->addAction(tr("Default"));
    actDefault->setCheckable(true);
    actDefault->setChecked(curMode == SectionDisplay_Default);
    actDefault->setData(SectionDisplay_Default);

    QMenu *textMenu = displayMenu->addMenu(tr("Text"));
    QAction *actRaw = textMenu->addAction(tr("Raw"));
    actRaw->setCheckable(true);
    actRaw->setChecked(curMode == SectionDisplay_Raw);
    actRaw->setData(SectionDisplay_Raw);

    QSet<QAction *> textModeActions;
    textModeActions.insert(actRaw);
    if (!m_tableNames.isEmpty()) {
        textMenu->addSeparator();
        for (int ti = 0; ti < m_tableNames.size(); ++ti) {
            const int tableMode = ti + 1;
            QAction *a = textMenu->addAction(m_tableNames[ti]);
            a->setCheckable(true);
            a->setChecked(curMode == tableMode);
            a->setData(tableMode);
            textModeActions.insert(a);
        }
    }

    displayMenu->addSeparator();
    QMenu *audioMenu = displayMenu->addMenu(tr("Audio"));
    QHash<QAction *, QString> audioActions;
    addAudioActions(audioMenu,
                    true,
                    curMode == SectionDisplay_Audio ? currentAudioTypeForSection(section) : QString(),
                    audioActions);

    displayMenu->addSeparator();
    QMenu *gfxMenu = displayMenu->addMenu(tr("Graphics"));
    QHash<QAction *, TileCodec> gfxActions;
    addGfxCodecActions(gfxMenu,
                       true,
                       curMode == SectionDisplay_Graphics,
                       section.tileCodec,
                       gfxActions);

    QHash<QAction *, int> colsActions;
    // Tile columns setting (only visible when in graphics mode)
    if (curMode == SectionDisplay_Graphics) {
        gfxMenu->addSeparator();
        QMenu *colsMenu = gfxMenu->addMenu(tr("Tile columns: %1").arg(section.tileCols));
        for (int nc : {4, 8, 16, 24, 32}) {
            QAction *a = colsMenu->addAction(QString::number(nc));
            a->setCheckable(true);
            a->setChecked(section.tileCols == nc);
            a->setData(nc);
            colsActions.insert(a, nc);
        }
    }

    displayMenu->addSeparator();
    QMenu *disasmMenu = displayMenu->addMenu(tr("Disassembly"));
    QHash<QAction *, RomType> disasmActions;
    const RomType checkedCpu = (section.disasmCpu != RomType::Unknown) ? section.disasmCpu : m_currentRomType;
    addDisasmCpuActions(disasmMenu,
                        true,
                        curMode == SectionDisplay_Disasm,
                        checkedCpu,
                        disasmActions);

    // Group
    menu.addSeparator();
    QAction *groupAct = menu.addAction(tr("Group"));

    menu.addSeparator();
    QAction *splitAct = menu.addAction(tr("Split into parts") + "...");
    menu.addSeparator();
    QAction *deleteAct = menu.addAction(tr("Delete"));

    QAction *chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == renameAct) {
        startRenameSection(sectionIdx);
    } else if (chosen == recolorAct) {
        const QColor color = QColorDialog::getColor(section.color, this, tr("Section color"));
        if (color.isValid())
            m_model->recolorSection(sectionIdx, color);
    } else if (chosen == vfFormatAct) {
        emit virtualFormattingRequested(secStart, secEnd);
    } else if (chosen == vfRemoveAct) {
        emit removeVirtualFormattingRequested(secStart, secEnd);
    } else if (chosen == findSamplesAct) {
        emit findSamplesInSectionRequested(secStart, secEnd);
    } else if (chosen == findFunctionsAct) {
        emit findFunctionsInSectionRequested(secStart, secEnd);
    } else if (chosen == splitAct) {
        // Show dialog with text edit for entering part sizes
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Split into parts"));
        dlg.setModal(true);
        auto *layout = new QVBoxLayout(&dlg);
        auto *label = new QLabel(tr("Enter sizes (decimal or 0x hex), separated by spaces, commas or newlines:"));
        label->setWordWrap(true);
        layout->addWidget(label);
        auto *textEdit = new QPlainTextEdit;
        textEdit->setMinimumHeight(textEdit->fontMetrics().lineSpacing() * 6 + 10);
        layout->addWidget(textEdit);
        auto *sizeLabel = new QLabel(tr("Section size: %1 bytes (0x%2)")
            .arg(secEnd - secStart)
            .arg(secEnd - secStart, 0, 16, QLatin1Char('0')));
        layout->addWidget(sizeLabel);
        auto *btnLayout = new QHBoxLayout;
        btnLayout->addStretch();
        auto *okBtn = new QPushButton(tr("OK"));
        okBtn->setDefault(true);
        auto *cancelBtn = new QPushButton(tr("Cancel"));
        btnLayout->addWidget(okBtn);
        btnLayout->addWidget(cancelBtn);
        layout->addLayout(btnLayout);
        connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
        connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

        if (dlg.exec() != QDialog::Accepted)
            return;

        // Parse sizes from text
        QString text = textEdit->toPlainText();
        text.replace(',', ' ');
        text.replace('\n', ' ');
        text.replace('\r', ' ');
        const QStringList tokens = text.split(' ', Qt::SkipEmptyParts);
        QVector<qint64> sizes;
        for (const QString &tok : tokens) {
            bool ok = false;
            qint64 val = 0;
            const QString trimmed = tok.trimmed();
            if (trimmed.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
                val = trimmed.mid(2).toLongLong(&ok, 16);
            else
                val = trimmed.toLongLong(&ok, 10);
            if (ok && val > 0)
                sizes.append(val);
        }
        if (sizes.isEmpty())
            return;

        emit splitSectionRequested(sectionIdx, sizes);
    } else if (chosen == deleteAct) {
        m_model->removeSection(sectionIdx);
    } else if (chosen == groupAct) {
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, tr("Group"), tr("Group name:"),
            QLineEdit::Normal, tr("Group"), &ok);
        if (ok && !name.isEmpty()) {
            SectionGroup ng;
            ng.name = name;
            ng.color = SectionListModel::randomPastelColor();
            ng.parentGroupId = section.groupId;
            const int newGid = m_model->addGroup(ng);
            Section s = m_model->at(sectionIdx);
            s.groupId = newGid;
            m_model->updateSection(sectionIdx, s);
        }
    } else if (disasmActions.contains(chosen)) {
        const RomType cpu = disasmActions.value(chosen);
        Section s = m_model->at(sectionIdx);
        if (s.displayMode != SectionDisplay_Disasm || s.disasmCpu != cpu) {
            s.displayMode = SectionDisplay_Disasm;
            s.display = displayMnemonicForMode(s.displayMode);
            s.disasmCpu = cpu;
            m_model->updateSection(sectionIdx, s);
            emit disasmCpuChanged(sectionIdx, cpu);
        }
    } else if (gfxActions.contains(chosen)) {
        const TileCodec tc = gfxActions.value(chosen);
        Section s = m_model->at(sectionIdx);
        if (s.displayMode != SectionDisplay_Graphics || s.tileCodec != tc) {
            s.displayMode = SectionDisplay_Graphics;
            s.display = displayMnemonicForMode(s.displayMode);
            s.tileCodec = tc;
            if (s.color.alpha() > 200)
                s.color = QColor(0x80, 0xFF, 0x80, 0x40);
            m_model->updateSection(sectionIdx, s);
        }
    } else if (colsActions.contains(chosen)) {
        // Tile columns submenu
        const int newCols = colsActions.value(chosen);
        Section s = m_model->at(sectionIdx);
        if (s.tileCols != newCols) {
            s.tileCols = newCols;
            m_model->updateSection(sectionIdx, s);
        }
    } else if (audioActions.contains(chosen)) {
        const QString t = audioActions.value(chosen);
        Section s = m_model->at(sectionIdx);
        const QString newOptions = optionsWithAudioType(s.options, t);
        if (s.displayMode != SectionDisplay_Audio || s.options != newOptions || s.display != QStringLiteral("snd")) {
            s.displayMode = SectionDisplay_Audio;
            s.display = QStringLiteral("snd");
            s.options = newOptions;
            s.color = QColor(0x40, 0xA0, 0xFF, 0x40);
            m_model->updateSection(sectionIdx, s);
        }
    } else if (chosen == actDefault || textModeActions.contains(chosen)) {
        const int newMode = chosen->data().toInt();
        Section s = m_model->at(sectionIdx);
        if (s.displayMode != newMode) {
            s.displayMode = newMode;
            s.display = displayMnemonicForMode(s.displayMode);
            m_model->updateSection(sectionIdx, s);
        }
    }
}

// ── Tree building ─────────────────────────────────────────────────

void SectionsDockWidget::rebuildTree()
{
    if (m_suppressRebuild || m_rebuildingTree || !m_tree)
        return;

    m_rebuildingTree = true;
    const QSignalBlocker blocker(m_tree);

    // ── Save current expansion state before clearing ──────────────────────
    QSet<int> expandedGroupIds;
    std::function<void(QTreeWidgetItem *)> collectState = [&](QTreeWidgetItem *item) {
        const int gid = item->data(0, kRoleGroupId).toInt();
        if (gid >= 0) {
            if (item->isExpanded())
                expandedGroupIds.insert(gid);
        }
        for (int i = 0; i < item->childCount(); ++i)
            collectState(item->child(i));
    };
    if (QTreeWidgetItem *oldRoot = m_tree->topLevelItem(0))
        collectState(oldRoot);

    if (m_hasForcedExpandedGroupIds)
        expandedGroupIds = m_forcedExpandedGroupIds;

    m_tree->clear();

    const QString rootLabel = m_romTypeName.isEmpty()
        ? QStringLiteral("ROM")
        : QStringLiteral("ROM (%1)").arg(m_romTypeName);

    auto *romRoot = new QTreeWidgetItem(m_tree, {rootLabel});
    romRoot->setData(0, kRoleSectionOffset, qint64(-1));
    romRoot->setData(0, kRoleSectionIndex,  -1);
    romRoot->setData(0, kRoleGroupId,       -1);
    romRoot->setFlags(Qt::ItemIsEnabled | Qt::ItemIsDropEnabled);
    romRoot->setExpanded(true);

    if (!m_model) {
        m_rebuildingTree = false;
        return;
    }

    // Collect sections per group and ungrouped (already in offset order)
    QVector<QVector<int>> groupSections(m_model->groupCount());
    QVector<int> ungroupedSections;
    for (int i = 0; i < m_model->count(); ++i) {
        const int gid = m_model->at(i).groupId;
        if (gid >= 0 && gid < m_model->groupCount())
            groupSections[gid].append(i);
        else
            ungroupedSections.append(i);
    }

    auto addSectionItem = [&](QTreeWidgetItem *parent, int i) {
        const Section &s = m_model->at(i);
        const QString offsetStr = QStringLiteral("0x%1")
            .arg(s.startOffset, 0, 16, QLatin1Char('0'));
        qint64 sizeBytes = 0;
        if (i + 1 < m_model->count()) {
            const qint64 nextStart = m_model->at(i + 1).startOffset;
            sizeBytes = qMax<qint64>(0, nextStart - s.startOffset);
        } else {
            // Last section size is [fileSize - startOffset].
            sizeBytes = qMax<qint64>(0, m_fileSize - s.startOffset);
        }
        const QString sizeStr = QString::number(sizeBytes);

        // Build display name with mode suffix
        QString displayName = s.name;
        if (s.displayMode == SectionDisplay_Graphics)
            displayName += QStringLiteral(" [%1]").arg(QString::fromLatin1(tileCodecName(s.tileCodec)));
        else if (s.displayMode == SectionDisplay_Audio) {
            const AudioSampleFormat fmt = audioFormatFromMnemonic(currentAudioTypeForSection(s));
            displayName += QStringLiteral(" [Audio: %1]").arg(audioFormatLabel(fmt));
        }
        else if (s.displayMode == SectionDisplay_Disasm) {
            const RomType labelCpu = (s.disasmCpu != RomType::Unknown) ? s.disasmCpu : m_currentRomType;
            const char *cpuName = disasmCpuName(labelCpu);
            displayName += cpuName
                ? QStringLiteral(" [ASM: %1]").arg(QString::fromLatin1(cpuName))
                : QStringLiteral(" [ASM]");
        }
        else if (s.displayMode == SectionDisplay_Raw)
            displayName += QStringLiteral(" [Text: Raw]");
        else if (s.displayMode > 0)
            displayName += QStringLiteral(" [Text: %1]").arg(
                (s.displayMode - 1 >= 0 && s.displayMode - 1 < m_tableNames.size())
                    ? m_tableNames[s.displayMode - 1]
                    : tr("Table %1").arg(s.displayMode));

        auto *child = new QTreeWidgetItem(parent, {displayName, offsetStr, sizeStr});
        child->setData(0, kRoleSectionOffset, s.startOffset);
        child->setData(0, kRoleSectionIndex,  i);
        child->setData(0, kRoleGroupId,       -1);
        child->setIcon(0, colorSwatchIcon(s.color));
        child->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable
                        | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);
    };

    // Recursive: add child groups of parentGid (sorted by treeOrder), then their sections
    std::function<void(QTreeWidgetItem *, int)> addGroupItems =
        [&](QTreeWidgetItem *parentItem, int parentGid) {
        // Collect child group IDs for this parent
        QVector<int> childIds;
        for (int gi = 0; gi < m_model->groupCount(); ++gi)
            if (m_model->groupAt(gi).parentGroupId == parentGid)
                childIds.append(gi);
        std::stable_sort(childIds.begin(), childIds.end(), [&](int a, int b){
            return m_model->groupAt(a).treeOrder < m_model->groupAt(b).treeOrder;
        });

        for (int gi : childIds) {
            const SectionGroup &g = m_model->groupAt(gi);
            auto *gItem = new QTreeWidgetItem(parentItem, {g.name});
            gItem->setData(0, kRoleSectionOffset, qint64(-1));
            gItem->setData(0, kRoleSectionIndex,  -1);
            gItem->setData(0, kRoleGroupId,       gi);
            gItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable
                            | Qt::ItemIsDropEnabled | Qt::ItemIsDragEnabled);
            // Bold font, no color swatch icon
            QFont f = gItem->font(0);
            f.setBold(true);
            gItem->setFont(0, f);

            // Add sections of this group (offset-sorted)
            for (int si : groupSections[gi])
                addSectionItem(gItem, si);

            // Recursively add sub-groups
            addGroupItems(gItem, gi);

            // Restore expansion state (new groups start collapsed)
            gItem->setExpanded(expandedGroupIds.contains(gi));
        }
    };

    addGroupItems(romRoot, -1);

    // Add ungrouped sections (in offset order) after all groups
    for (int si : ungroupedSections)
        addSectionItem(romRoot, si);

    m_tree->resizeColumnToContents(0);
    m_hasForcedExpandedGroupIds = false;
    m_forcedExpandedGroupIds.clear();
    m_rebuildingTree = false;

    // Re-sync tree selection with the current cursor position so that
    // section focus is preserved after split / merge / undo / redo.
    if (m_lastHighlightedOffset >= 0)
        highlightOffset(m_lastHighlightedOffset);
}

QIcon SectionsDockWidget::colorSwatchIcon(const QColor &color) const
{
    QPixmap pix(16, 16);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRoundedRect(2, 2, 12, 12, 2, 2);
    return QIcon(pix);
}

// ── Highlight ─────────────────────────────────────────────────────

void SectionsDockWidget::highlightOffset(qint64 offset)
{
    m_lastHighlightedOffset = offset;

    if (!m_model) return;

    const int idx = m_model->sectionIndexAtOffset(offset);
    if (idx < 0) {
        m_tree->blockSignals(true);
        m_tree->clearSelection();
        m_tree->setCurrentItem(nullptr);
        m_tree->blockSignals(false);
        return;
    }

    // Find tree item with matching section index
    QTreeWidgetItem *romRoot = m_tree->topLevelItem(0);
    if (!romRoot) return;

    std::function<QTreeWidgetItem *(QTreeWidgetItem *)> findItem =
        [&](QTreeWidgetItem *parent) -> QTreeWidgetItem * {
            for (int i = 0; i < parent->childCount(); ++i) {
                auto *ch = parent->child(i);
                if (ch->data(0, kRoleSectionIndex).toInt() == idx)
                    return ch;
                if (auto *found = findItem(ch))
                    return found;
            }
            return nullptr;
        };

    QTreeWidgetItem *target = findItem(romRoot);
    if (target && target != m_tree->currentItem()) {
        m_tree->blockSignals(true);
        m_tree->setCurrentItem(target);
        m_tree->blockSignals(false);
    }
}

void SectionsDockWidget::startRenameSection(int sectionIndex)
{
    if (!m_model || sectionIndex < 0 || sectionIndex >= m_model->count())
        return;

    QTreeWidgetItem *romRoot = m_tree->topLevelItem(0);
    if (!romRoot) return;

    std::function<QTreeWidgetItem *(QTreeWidgetItem *)> findItem =
        [&](QTreeWidgetItem *parent) -> QTreeWidgetItem * {
            for (int i = 0; i < parent->childCount(); ++i) {
                auto *ch = parent->child(i);
                if (ch->data(0, kRoleSectionIndex).toInt() == sectionIndex)
                    return ch;
                if (auto *found = findItem(ch))
                    return found;
            }
            return nullptr;
        };

    QTreeWidgetItem *target = findItem(romRoot);
    if (!target) return;

    m_tree->setCurrentItem(target);
    m_tree->scrollToItem(target);
    m_tree->editItem(target, 0);
}

void SectionsDockWidget::onItemChanged(QTreeWidgetItem *item, int column)
{
    if (m_rebuildingTree || m_suppressRebuild)
        return;
    if (!item || !m_model)
        return;

    const int sectionIdx = item->data(0, kRoleSectionIndex).toInt();
    if (sectionIdx < 0 || sectionIdx >= m_model->count())
        return;

    const auto parseNumber = [](const QString &text, int defaultBase, bool *ok) -> qint64 {
        QString t = text.trimmed();
        if (t.isEmpty()) {
            *ok = false;
            return 0;
        }
        if (t.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
            return t.mid(2).toLongLong(ok, 16);
        return t.toLongLong(ok, defaultBase);
    };

    const auto setCellTextSafely = [this, item](int col, const QString &text) {
        QSignalBlocker blocker(m_tree);
        item->setText(col, text);
    };

    if (column == 0) {
        const QString newName = item->text(0).trimmed();
        const QString oldName = m_model->at(sectionIdx).name;

        if (newName.isEmpty()) {
            setCellTextSafely(0, oldName);
            return;
        }

        if (newName != oldName)
            m_model->renameSection(sectionIdx, newName);
        return;
    }

    QVector<Section> secs = m_model->sections();
    if (sectionIdx < 0 || sectionIdx >= secs.size())
        return;

    const qint64 fileEnd = qMax<qint64>(0, m_fileSize);
    const qint64 prevStart = (sectionIdx > 0) ? secs[sectionIdx - 1].startOffset : -1;
    const qint64 thisStart = secs[sectionIdx].startOffset;
    const qint64 thisEnd = m_model->endOffsetOf(sectionIdx, fileEnd);

    const qint64 nextEnd = (sectionIdx + 1 < secs.size())
        ? m_model->endOffsetOf(sectionIdx + 1, fileEnd)
        : fileEnd;

    // Allowed edit interval: [previous section start + 1, end of next section - 1].
    const qint64 minAllowed = qMax<qint64>(0, prevStart + 1);
    const qint64 maxAllowed = qMax(minAllowed, nextEnd - 1);

    if (column == 1) {
        bool ok = false;
        qint64 newStart = parseNumber(item->text(1), 16, &ok);
        if (!ok) {
            setCellTextSafely(1, QStringLiteral("0x%1").arg(thisStart, 0, 16, QLatin1Char('0')));
            return;
        }

        if (fileEnd > 0)
            newStart = qBound(minAllowed, newStart, maxAllowed);
        else
            newStart = thisStart;

        if (newStart == thisStart) {
            setCellTextSafely(1, QStringLiteral("0x%1").arg(thisStart, 0, 16, QLatin1Char('0')));
            return;
        }

        secs[sectionIdx].startOffset = newStart;
        m_model->applySections(secs, tr("Edit section offset"));
        return;
    }

    if (column == 2) {
        const qint64 currentSize = qMax<qint64>(0, thisEnd - thisStart);
        if (sectionIdx + 1 >= secs.size()) {
            const qint64 lastSize = qMax<qint64>(0, fileEnd - thisStart);
            setCellTextSafely(2, QString::number(lastSize));
            return;
        }

        bool ok = false;
        qint64 newSize = parseNumber(item->text(2), 10, &ok);
        if (!ok) {
            setCellTextSafely(2, QString::number(currentSize));
            return;
        }

        qint64 newNextStart = thisStart + newSize;
        const qint64 minNextStart = thisStart + 1;
        const qint64 maxNextStart = qMax(minNextStart, nextEnd - 1);
        newNextStart = qBound(minNextStart, newNextStart, maxNextStart);

        if (newNextStart == secs[sectionIdx + 1].startOffset) {
            setCellTextSafely(2, QString::number(currentSize));
            return;
        }

        secs[sectionIdx + 1].startOffset = newNextStart;
        m_model->applySections(secs, tr("Edit section size"));
    }
}

// ── Drag & drop ───────────────────────────────────────────────────

bool SectionsDockWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_tree->viewport() && event->type() == QEvent::Drop) {
        handleDrop(static_cast<QDropEvent *>(event));
        return true; // consume the event — we update the model, rebuildTree does the rest
    }
    return BaseDockWidget::eventFilter(watched, event);
}

void SectionsDockWidget::handleDrop(QDropEvent *event)
{
    if (!m_model)
        return;

    const QPoint dropPos = event->position().toPoint();
    QTreeWidgetItem *targetItem = m_tree->itemAt(dropPos);

    // Collect dragged items (sections and/or groups)
    const QList<QTreeWidgetItem *> selected = m_tree->selectedItems();
    QVector<int> draggedSectionIndices;
    QVector<int> draggedGroupIds;
    for (QTreeWidgetItem *item : selected) {
        const int sidx = item->data(0, kRoleSectionIndex).toInt();
        const int gid  = item->data(0, kRoleGroupId).toInt();
        if (sidx >= 0 && sidx < m_model->count())
            draggedSectionIndices.append(sidx);
        else if (gid >= 0 && gid < m_model->groupCount())
            draggedGroupIds.append(gid);
    }

    if (draggedSectionIndices.isEmpty() && draggedGroupIds.isEmpty())
        return;

    // ── Groups dragged ─────────────────────────────────────────────────────
    if (!draggedGroupIds.isEmpty()) {
        QVector<SectionGroup> grps = m_model->groups();

        // Helper: is 'possibleAncestor' an ancestor (or equal) of 'gid'?
        auto isAncestorOf = [&](int possibleAncestor, int gid) -> bool {
            int cur = gid;
            while (cur >= 0) {
                if (cur == possibleAncestor) return true;
                cur = grps[cur].parentGroupId;
            }
            return false;
        };

        if (targetItem) {
            const int targetGid = targetItem->data(0, kRoleGroupId).toInt();
            if (targetGid >= 0 && !draggedGroupIds.contains(targetGid)) {
                const QRect itemRect = m_tree->visualItemRect(targetItem);
                const int h   = itemRect.height();
                const int relY = dropPos.y() - itemRect.top();

                if (relY >= h / 3 && relY <= 2 * h / 3) {
                    // ——— Drop inside middle third: make dragged groups children of target ———
                    // Skip if any dragged group is an ancestor of target (cycle prevention)
                    bool cycle = false;
                    for (int gid : draggedGroupIds)
                        if (isAncestorOf(gid, targetGid)) { cycle = true; break; }
                    if (!cycle) {
                        for (int gid : draggedGroupIds)
                            grps[gid].parentGroupId = targetGid;
                        m_model->applySections(m_model->sections(), grps, tr("Move group into group"));
                        return;
                    }
                } else {
                    // ——— Drop above or below: reorder as siblings of target ———
                    const bool above       = (relY < h / 3);
                    const int targetParent = grps[targetGid].parentGroupId;

                    // Cycle check: dragged groups must not be ancestors of targetParent
                    bool cycle = false;
                    if (targetParent >= 0) {
                        for (int gid : draggedGroupIds)
                            if (isAncestorOf(gid, targetParent)) { cycle = true; break; }
                    }
                    if (!cycle) {
                        // Change parent of all dragged groups to match target's parent
                        for (int gid : draggedGroupIds)
                            grps[gid].parentGroupId = targetParent;

                        // Reorder siblings (same targetParent)
                        QVector<int> siblingIds;
                        for (int i = 0; i < grps.size(); ++i)
                            if (grps[i].parentGroupId == targetParent)
                                siblingIds.append(i);
                        std::stable_sort(siblingIds.begin(), siblingIds.end(), [&](int a, int b){
                            return grps[a].treeOrder < grps[b].treeOrder;
                        });
                        for (int gid : draggedGroupIds)
                            siblingIds.removeAll(gid);
                        const int pos      = siblingIds.indexOf(targetGid);
                        const int insertIdx = (pos >= 0) ? (above ? pos : pos + 1) : siblingIds.size();
                        for (int i = draggedGroupIds.size() - 1; i >= 0; --i)
                            siblingIds.insert(insertIdx, draggedGroupIds[i]);
                        for (int rank = 0; rank < siblingIds.size(); ++rank)
                            grps[siblingIds[rank]].treeOrder = rank;

                        m_model->applySections(m_model->sections(), grps, tr("Reorder groups"));
                        return;
                    }
                }
            }
        }

        // Fallback: drop on ROM root or above the first group → insert at position 0
        // (or append at end if target was null / below all groups)
        const bool onRomRoot = targetItem && targetItem->data(0, kRoleGroupId).toInt() < 0
                               && targetItem->data(0, kRoleSectionIndex).toInt() < 0;
        for (int gid : draggedGroupIds)
            grps[gid].parentGroupId = -1;
        // Build ordered list of root-level siblings excluding dragged
        QVector<int> rootSiblings;
        for (int i = 0; i < grps.size(); ++i)
            if (grps[i].parentGroupId == -1 && !draggedGroupIds.contains(i))
                rootSiblings.append(i);
        std::stable_sort(rootSiblings.begin(), rootSiblings.end(), [&](int a, int b){
            return grps[a].treeOrder < grps[b].treeOrder;
        });
        const int insertIdx = onRomRoot ? 0 : rootSiblings.size();
        for (int i = draggedGroupIds.size() - 1; i >= 0; --i)
            rootSiblings.insert(insertIdx, draggedGroupIds[i]);
        for (int rank = 0; rank < rootSiblings.size(); ++rank)
            grps[rootSiblings[rank]].treeOrder = rank;
        m_model->applySections(m_model->sections(), grps, tr("Reorder groups"));
        return;
    }

    // ── Sections dragged: change groupId only (position stays by offset) ──
    if (!draggedSectionIndices.isEmpty()) {
        // Determine target group (-1 = ungrouped)
        int targetGroupId = -1;
        if (targetItem) {
            const int tgid  = targetItem->data(0, kRoleGroupId).toInt();
            const int tsidx = targetItem->data(0, kRoleSectionIndex).toInt();
            if (tgid >= 0) {
                targetGroupId = tgid;
            } else if (tsidx >= 0) {
                QTreeWidgetItem *parentItem = targetItem->parent();
                if (parentItem)
                    targetGroupId = parentItem->data(0, kRoleGroupId).toInt();
            }
        }

        QVector<Section> secs = m_model->sections();
        bool changed = false;
        for (int idx : draggedSectionIndices) {
            if (secs[idx].groupId != targetGroupId) {
                secs[idx].groupId = targetGroupId;
                changed = true;
            }
        }
        if (changed)
            m_model->applySections(secs, tr("Move to group"));
    }
}

/****************************************************************************
** Meta object code from reading C++ file 'hexeditor.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.2.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../src/hexeditor/hexeditor.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'hexeditor.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.2.3. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_HexEditor_t {
    const uint offsetsAndSize[122];
    char stringdata0[804];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(offsetof(qt_meta_stringdata_HexEditor_t, stringdata0) + ofs), len 
static const qt_meta_stringdata_HexEditor_t qt_meta_stringdata_HexEditor = {
    {
QT_MOC_LITERAL(0, 9), // "HexEditor"
QT_MOC_LITERAL(10, 21), // "currentAddressChanged"
QT_MOC_LITERAL(32, 0), // ""
QT_MOC_LITERAL(33, 7), // "address"
QT_MOC_LITERAL(41, 18), // "currentSizeChanged"
QT_MOC_LITERAL(60, 4), // "size"
QT_MOC_LITERAL(65, 11), // "dataChanged"
QT_MOC_LITERAL(77, 13), // "dataChangedAt"
QT_MOC_LITERAL(91, 6), // "offset"
QT_MOC_LITERAL(98, 20), // "overwriteModeChanged"
QT_MOC_LITERAL(119, 5), // "state"
QT_MOC_LITERAL(125, 16), // "selectionChanged"
QT_MOC_LITERAL(142, 5), // "start"
QT_MOC_LITERAL(148, 3), // "end"
QT_MOC_LITERAL(152, 23), // "addressCollapsedChanged"
QT_MOC_LITERAL(176, 9), // "collapsed"
QT_MOC_LITERAL(186, 13), // "undoAvailable"
QT_MOC_LITERAL(200, 9), // "available"
QT_MOC_LITERAL(210, 13), // "redoAvailable"
QT_MOC_LITERAL(224, 20), // "contextMenuRequested"
QT_MOC_LITERAL(245, 9), // "globalPos"
QT_MOC_LITERAL(255, 7), // "bytePos"
QT_MOC_LITERAL(263, 4), // "redo"
QT_MOC_LITERAL(268, 4), // "undo"
QT_MOC_LITERAL(273, 6), // "adjust"
QT_MOC_LITERAL(280, 18), // "dataChangedPrivate"
QT_MOC_LITERAL(299, 3), // "idx"
QT_MOC_LITERAL(303, 7), // "refresh"
QT_MOC_LITERAL(311, 12), // "updateCursor"
QT_MOC_LITERAL(324, 15), // "updateScrollMap"
QT_MOC_LITERAL(340, 24), // "scheduleScrollMapCompute"
QT_MOC_LITERAL(365, 22), // "updateScrollMapMargins"
QT_MOC_LITERAL(388, 11), // "addressArea"
QT_MOC_LITERAL(400, 16), // "addressAreaColor"
QT_MOC_LITERAL(417, 16), // "addressFontColor"
QT_MOC_LITERAL(434, 14), // "asciiAreaColor"
QT_MOC_LITERAL(449, 14), // "asciiFontColor"
QT_MOC_LITERAL(464, 12), // "hexFontColor"
QT_MOC_LITERAL(477, 13), // "addressOffset"
QT_MOC_LITERAL(491, 12), // "addressWidth"
QT_MOC_LITERAL(504, 9), // "asciiArea"
QT_MOC_LITERAL(514, 12), // "bytesPerLine"
QT_MOC_LITERAL(527, 14), // "cursorPosition"
QT_MOC_LITERAL(542, 4), // "data"
QT_MOC_LITERAL(547, 7), // "hexCaps"
QT_MOC_LITERAL(555, 19), // "dynamicBytesPerLine"
QT_MOC_LITERAL(575, 12), // "highlighting"
QT_MOC_LITERAL(588, 17), // "highlightingColor"
QT_MOC_LITERAL(606, 13), // "overwriteMode"
QT_MOC_LITERAL(620, 14), // "selectionColor"
QT_MOC_LITERAL(635, 8), // "readOnly"
QT_MOC_LITERAL(644, 11), // "showHexGrid"
QT_MOC_LITERAL(656, 22), // "hexAreaBackgroundColor"
QT_MOC_LITERAL(679, 16), // "hexAreaGridColor"
QT_MOC_LITERAL(696, 18), // "showMultibyteFrame"
QT_MOC_LITERAL(715, 15), // "cursorCharColor"
QT_MOC_LITERAL(731, 16), // "cursorFrameColor"
QT_MOC_LITERAL(748, 17), // "zeroByteFontColor"
QT_MOC_LITERAL(766, 19), // "multibyteFrameColor"
QT_MOC_LITERAL(786, 12), // "changesColor"
QT_MOC_LITERAL(799, 4) // "font"

    },
    "HexEditor\0currentAddressChanged\0\0"
    "address\0currentSizeChanged\0size\0"
    "dataChanged\0dataChangedAt\0offset\0"
    "overwriteModeChanged\0state\0selectionChanged\0"
    "start\0end\0addressCollapsedChanged\0"
    "collapsed\0undoAvailable\0available\0"
    "redoAvailable\0contextMenuRequested\0"
    "globalPos\0bytePos\0redo\0undo\0adjust\0"
    "dataChangedPrivate\0idx\0refresh\0"
    "updateCursor\0updateScrollMap\0"
    "scheduleScrollMapCompute\0"
    "updateScrollMapMargins\0addressArea\0"
    "addressAreaColor\0addressFontColor\0"
    "asciiAreaColor\0asciiFontColor\0"
    "hexFontColor\0addressOffset\0addressWidth\0"
    "asciiArea\0bytesPerLine\0cursorPosition\0"
    "data\0hexCaps\0dynamicBytesPerLine\0"
    "highlighting\0highlightingColor\0"
    "overwriteMode\0selectionColor\0readOnly\0"
    "showHexGrid\0hexAreaBackgroundColor\0"
    "hexAreaGridColor\0showMultibyteFrame\0"
    "cursorCharColor\0cursorFrameColor\0"
    "zeroByteFontColor\0multibyteFrameColor\0"
    "changesColor\0font"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_HexEditor[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
      20,   14, // methods
      29,  178, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
      10,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,  134,    2, 0x06,   30 /* Public */,
       4,    1,  137,    2, 0x06,   32 /* Public */,
       6,    0,  140,    2, 0x06,   34 /* Public */,
       7,    1,  141,    2, 0x06,   35 /* Public */,
       9,    1,  144,    2, 0x06,   37 /* Public */,
      11,    2,  147,    2, 0x06,   39 /* Public */,
      14,    1,  152,    2, 0x06,   42 /* Public */,
      16,    1,  155,    2, 0x06,   44 /* Public */,
      18,    1,  158,    2, 0x06,   46 /* Public */,
      19,    2,  161,    2, 0x06,   48 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
      22,    0,  166,    2, 0x0a,   51 /* Public */,
      23,    0,  167,    2, 0x0a,   52 /* Public */,
      24,    0,  168,    2, 0x08,   53 /* Private */,
      25,    1,  169,    2, 0x08,   54 /* Private */,
      25,    0,  172,    2, 0x28,   56 /* Private | MethodCloned */,
      27,    0,  173,    2, 0x08,   57 /* Private */,
      28,    0,  174,    2, 0x08,   58 /* Private */,
      29,    0,  175,    2, 0x08,   59 /* Private */,
      30,    0,  176,    2, 0x08,   60 /* Private */,
      31,    0,  177,    2, 0x08,   61 /* Private */,

 // signals: parameters
    QMetaType::Void, QMetaType::LongLong,    3,
    QMetaType::Void, QMetaType::LongLong,    5,
    QMetaType::Void,
    QMetaType::Void, QMetaType::LongLong,    8,
    QMetaType::Void, QMetaType::Bool,   10,
    QMetaType::Void, QMetaType::LongLong, QMetaType::LongLong,   12,   13,
    QMetaType::Void, QMetaType::Bool,   15,
    QMetaType::Void, QMetaType::Bool,   17,
    QMetaType::Void, QMetaType::Bool,   17,
    QMetaType::Void, QMetaType::QPoint, QMetaType::LongLong,   20,   21,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   26,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

 // properties: name, type, flags
      32, QMetaType::Bool, 0x00015103, uint(-1), 0,
      33, QMetaType::QColor, 0x00015103, uint(-1), 0,
      34, QMetaType::QColor, 0x00015103, uint(-1), 0,
      35, QMetaType::QColor, 0x00015103, uint(-1), 0,
      36, QMetaType::QColor, 0x00015103, uint(-1), 0,
      37, QMetaType::QColor, 0x00015103, uint(-1), 0,
      38, QMetaType::LongLong, 0x00015103, uint(-1), 0,
      39, QMetaType::Int, 0x00015103, uint(-1), 0,
      40, QMetaType::Bool, 0x00015103, uint(-1), 0,
      41, QMetaType::Int, 0x00015103, uint(-1), 0,
      42, QMetaType::LongLong, 0x00015103, uint(-1), 0,
      43, QMetaType::QByteArray, 0x00015103, uint(2), 0,
      44, QMetaType::Bool, 0x00015103, uint(-1), 0,
      45, QMetaType::Bool, 0x00015103, uint(-1), 0,
      46, QMetaType::Bool, 0x00015103, uint(-1), 0,
      47, QMetaType::QColor, 0x00015103, uint(-1), 0,
      48, QMetaType::Bool, 0x00015103, uint(-1), 0,
      49, QMetaType::QColor, 0x00015103, uint(-1), 0,
      50, QMetaType::Bool, 0x00015103, uint(-1), 0,
      51, QMetaType::Bool, 0x00015103, uint(-1), 0,
      52, QMetaType::QColor, 0x00015103, uint(-1), 0,
      53, QMetaType::QColor, 0x00015103, uint(-1), 0,
      54, QMetaType::Bool, 0x00015103, uint(-1), 0,
      55, QMetaType::QColor, 0x00015103, uint(-1), 0,
      56, QMetaType::QColor, 0x00015103, uint(-1), 0,
      57, QMetaType::QColor, 0x00015103, uint(-1), 0,
      58, QMetaType::QColor, 0x00015103, uint(-1), 0,
      59, QMetaType::QColor, 0x00015103, uint(-1), 0,
      60, QMetaType::QFont, 0x00015103, uint(-1), 0,

       0        // eod
};

void HexEditor::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<HexEditor *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->currentAddressChanged((*reinterpret_cast< qint64(*)>(_a[1]))); break;
        case 1: _t->currentSizeChanged((*reinterpret_cast< qint64(*)>(_a[1]))); break;
        case 2: _t->dataChanged(); break;
        case 3: _t->dataChangedAt((*reinterpret_cast< qint64(*)>(_a[1]))); break;
        case 4: _t->overwriteModeChanged((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 5: _t->selectionChanged((*reinterpret_cast< qint64(*)>(_a[1])),(*reinterpret_cast< qint64(*)>(_a[2]))); break;
        case 6: _t->addressCollapsedChanged((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 7: _t->undoAvailable((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 8: _t->redoAvailable((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 9: _t->contextMenuRequested((*reinterpret_cast< const QPoint(*)>(_a[1])),(*reinterpret_cast< qint64(*)>(_a[2]))); break;
        case 10: _t->redo(); break;
        case 11: _t->undo(); break;
        case 12: _t->adjust(); break;
        case 13: _t->dataChangedPrivate((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 14: _t->dataChangedPrivate(); break;
        case 15: _t->refresh(); break;
        case 16: _t->updateCursor(); break;
        case 17: _t->updateScrollMap(); break;
        case 18: _t->scheduleScrollMapCompute(); break;
        case 19: _t->updateScrollMapMargins(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (HexEditor::*)(qint64 );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&HexEditor::currentAddressChanged)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (HexEditor::*)(qint64 );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&HexEditor::currentSizeChanged)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (HexEditor::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&HexEditor::dataChanged)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (HexEditor::*)(qint64 );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&HexEditor::dataChangedAt)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (HexEditor::*)(bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&HexEditor::overwriteModeChanged)) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (HexEditor::*)(qint64 , qint64 );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&HexEditor::selectionChanged)) {
                *result = 5;
                return;
            }
        }
        {
            using _t = void (HexEditor::*)(bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&HexEditor::addressCollapsedChanged)) {
                *result = 6;
                return;
            }
        }
        {
            using _t = void (HexEditor::*)(bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&HexEditor::undoAvailable)) {
                *result = 7;
                return;
            }
        }
        {
            using _t = void (HexEditor::*)(bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&HexEditor::redoAvailable)) {
                *result = 8;
                return;
            }
        }
        {
            using _t = void (HexEditor::*)(const QPoint & , qint64 );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&HexEditor::contextMenuRequested)) {
                *result = 9;
                return;
            }
        }
    }
#ifndef QT_NO_PROPERTIES
    else if (_c == QMetaObject::ReadProperty) {
        auto *_t = static_cast<HexEditor *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast< bool*>(_v) = _t->addressArea(); break;
        case 1: *reinterpret_cast< QColor*>(_v) = _t->addressAreaColor(); break;
        case 2: *reinterpret_cast< QColor*>(_v) = _t->addressFontColor(); break;
        case 3: *reinterpret_cast< QColor*>(_v) = _t->asciiAreaColor(); break;
        case 4: *reinterpret_cast< QColor*>(_v) = _t->asciiFontColor(); break;
        case 5: *reinterpret_cast< QColor*>(_v) = _t->hexFontColor(); break;
        case 6: *reinterpret_cast< qint64*>(_v) = _t->addressOffset(); break;
        case 7: *reinterpret_cast< int*>(_v) = _t->addressWidth(); break;
        case 8: *reinterpret_cast< bool*>(_v) = _t->asciiArea(); break;
        case 9: *reinterpret_cast< int*>(_v) = _t->bytesPerLine(); break;
        case 10: *reinterpret_cast< qint64*>(_v) = _t->cursorPosition(); break;
        case 11: *reinterpret_cast< QByteArray*>(_v) = _t->data(); break;
        case 12: *reinterpret_cast< bool*>(_v) = _t->hexCaps(); break;
        case 13: *reinterpret_cast< bool*>(_v) = _t->dynamicBytesPerLine(); break;
        case 14: *reinterpret_cast< bool*>(_v) = _t->highlighting(); break;
        case 15: *reinterpret_cast< QColor*>(_v) = _t->highlightingColor(); break;
        case 16: *reinterpret_cast< bool*>(_v) = _t->overwriteMode(); break;
        case 17: *reinterpret_cast< QColor*>(_v) = _t->selectionColor(); break;
        case 18: *reinterpret_cast< bool*>(_v) = _t->isReadOnly(); break;
        case 19: *reinterpret_cast< bool*>(_v) = _t->showHexGrid(); break;
        case 20: *reinterpret_cast< QColor*>(_v) = _t->hexAreaBackgroundColor(); break;
        case 21: *reinterpret_cast< QColor*>(_v) = _t->hexAreaGridColor(); break;
        case 22: *reinterpret_cast< bool*>(_v) = _t->showMultibyteFrame(); break;
        case 23: *reinterpret_cast< QColor*>(_v) = _t->cursorCharColor(); break;
        case 24: *reinterpret_cast< QColor*>(_v) = _t->cursorFrameColor(); break;
        case 25: *reinterpret_cast< QColor*>(_v) = _t->zeroByteFontColor(); break;
        case 26: *reinterpret_cast< QColor*>(_v) = _t->multibyteFrameColor(); break;
        case 27: *reinterpret_cast< QColor*>(_v) = _t->changesColor(); break;
        case 28: *reinterpret_cast< QFont*>(_v) = _t->font(); break;
        default: break;
        }
    } else if (_c == QMetaObject::WriteProperty) {
        auto *_t = static_cast<HexEditor *>(_o);
        (void)_t;
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setAddressArea(*reinterpret_cast< bool*>(_v)); break;
        case 1: _t->setAddressAreaColor(*reinterpret_cast< QColor*>(_v)); break;
        case 2: _t->setAddressFontColor(*reinterpret_cast< QColor*>(_v)); break;
        case 3: _t->setAsciiAreaColor(*reinterpret_cast< QColor*>(_v)); break;
        case 4: _t->setAsciiFontColor(*reinterpret_cast< QColor*>(_v)); break;
        case 5: _t->setHexFontColor(*reinterpret_cast< QColor*>(_v)); break;
        case 6: _t->setAddressOffset(*reinterpret_cast< qint64*>(_v)); break;
        case 7: _t->setAddressWidth(*reinterpret_cast< int*>(_v)); break;
        case 8: _t->setAsciiArea(*reinterpret_cast< bool*>(_v)); break;
        case 9: _t->setBytesPerLine(*reinterpret_cast< int*>(_v)); break;
        case 10: _t->setCursorPosition(*reinterpret_cast< qint64*>(_v)); break;
        case 11: _t->setData(*reinterpret_cast< QByteArray*>(_v)); break;
        case 12: _t->setHexCaps(*reinterpret_cast< bool*>(_v)); break;
        case 13: _t->setDynamicBytesPerLine(*reinterpret_cast< bool*>(_v)); break;
        case 14: _t->setHighlighting(*reinterpret_cast< bool*>(_v)); break;
        case 15: _t->setHighlightingColor(*reinterpret_cast< QColor*>(_v)); break;
        case 16: _t->setOverwriteMode(*reinterpret_cast< bool*>(_v)); break;
        case 17: _t->setSelectionColor(*reinterpret_cast< QColor*>(_v)); break;
        case 18: _t->setReadOnly(*reinterpret_cast< bool*>(_v)); break;
        case 19: _t->setShowHexGrid(*reinterpret_cast< bool*>(_v)); break;
        case 20: _t->setHexAreaBackgroundColor(*reinterpret_cast< QColor*>(_v)); break;
        case 21: _t->setHexAreaGridColor(*reinterpret_cast< QColor*>(_v)); break;
        case 22: _t->setShowMultibyteFrame(*reinterpret_cast< bool*>(_v)); break;
        case 23: _t->setCursorCharColor(*reinterpret_cast< QColor*>(_v)); break;
        case 24: _t->setCursorFrameColor(*reinterpret_cast< QColor*>(_v)); break;
        case 25: _t->setZeroByteFontColor(*reinterpret_cast< QColor*>(_v)); break;
        case 26: _t->setMultibyteFrameColor(*reinterpret_cast< QColor*>(_v)); break;
        case 27: _t->setChangesColor(*reinterpret_cast< QColor*>(_v)); break;
        case 28: _t->setFont(*reinterpret_cast< QFont*>(_v)); break;
        default: break;
        }
    } else if (_c == QMetaObject::ResetProperty) {
    } else if (_c == QMetaObject::BindableProperty) {
    }
#endif // QT_NO_PROPERTIES
}

const QMetaObject HexEditor::staticMetaObject = { {
    QMetaObject::SuperData::link<QAbstractScrollArea::staticMetaObject>(),
    qt_meta_stringdata_HexEditor.offsetsAndSize,
    qt_meta_data_HexEditor,
    qt_static_metacall,
    nullptr,
qt_incomplete_metaTypeArray<qt_meta_stringdata_HexEditor_t
, QtPrivate::TypeAndForceComplete<bool, std::true_type>, QtPrivate::TypeAndForceComplete<QColor, std::true_type>, QtPrivate::TypeAndForceComplete<QColor, std::true_type>, QtPrivate::TypeAndForceComplete<QColor, std::true_type>, QtPrivate::TypeAndForceComplete<QColor, std::true_type>, QtPrivate::TypeAndForceComplete<QColor, std::true_type>, QtPrivate::TypeAndForceComplete<qint64, std::true_type>, QtPrivate::TypeAndForceComplete<int, std::true_type>, QtPrivate::TypeAndForceComplete<bool, std::true_type>, QtPrivate::TypeAndForceComplete<int, std::true_type>, QtPrivate::TypeAndForceComplete<qint64, std::true_type>, QtPrivate::TypeAndForceComplete<QByteArray, std::true_type>, QtPrivate::TypeAndForceComplete<bool, std::true_type>, QtPrivate::TypeAndForceComplete<bool, std::true_type>, QtPrivate::TypeAndForceComplete<bool, std::true_type>, QtPrivate::TypeAndForceComplete<QColor, std::true_type>, QtPrivate::TypeAndForceComplete<bool, std::true_type>, QtPrivate::TypeAndForceComplete<QColor, std::true_type>, QtPrivate::TypeAndForceComplete<bool, std::true_type>, QtPrivate::TypeAndForceComplete<bool, std::true_type>, QtPrivate::TypeAndForceComplete<QColor, std::true_type>, QtPrivate::TypeAndForceComplete<QColor, std::true_type>, QtPrivate::TypeAndForceComplete<bool, std::true_type>, QtPrivate::TypeAndForceComplete<QColor, std::true_type>, QtPrivate::TypeAndForceComplete<QColor, std::true_type>, QtPrivate::TypeAndForceComplete<QColor, std::true_type>, QtPrivate::TypeAndForceComplete<QColor, std::true_type>, QtPrivate::TypeAndForceComplete<QColor, std::true_type>, QtPrivate::TypeAndForceComplete<QFont, std::true_type>, QtPrivate::TypeAndForceComplete<HexEditor, std::true_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<qint64, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<qint64, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<qint64, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<bool, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<qint64, std::false_type>, QtPrivate::TypeAndForceComplete<qint64, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<bool, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<bool, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<bool, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<const QPoint &, std::false_type>, QtPrivate::TypeAndForceComplete<qint64, std::false_type>
, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<int, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>, QtPrivate::TypeAndForceComplete<void, std::false_type>


>,
    nullptr
} };


const QMetaObject *HexEditor::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *HexEditor::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_HexEditor.stringdata0))
        return static_cast<void*>(this);
    return QAbstractScrollArea::qt_metacast(_clname);
}

int HexEditor::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QAbstractScrollArea::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 20)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 20;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 20)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 20;
    }
#ifndef QT_NO_PROPERTIES
    else if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 29;
    }
#endif // QT_NO_PROPERTIES
    return _id;
}

// SIGNAL 0
void HexEditor::currentAddressChanged(qint64 _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void HexEditor::currentSizeChanged(qint64 _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void HexEditor::dataChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void HexEditor::dataChangedAt(qint64 _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void HexEditor::overwriteModeChanged(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void HexEditor::selectionChanged(qint64 _t1, qint64 _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}

// SIGNAL 6
void HexEditor::addressCollapsedChanged(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 6, _a);
}

// SIGNAL 7
void HexEditor::undoAvailable(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 7, _a);
}

// SIGNAL 8
void HexEditor::redoAvailable(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 8, _a);
}

// SIGNAL 9
void HexEditor::contextMenuRequested(const QPoint & _t1, qint64 _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 9, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE

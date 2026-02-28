QT += core gui widgets
CONFIG += c++17

TEMPLATE = app
TARGET = RTHextion

macx {
    ICON = images/tj.icns
    QMAKE_BUNDLE = RTHextion
    QMAKE_INFO_PLIST = Info.plist
}

HEADERS = \
    appinfo.h \
    Datas.h \
    DumpScriptdialog.h \
    InsertScriptDialog.h \
    JumpToDialog.h \
    PointerListModel.h \
    SemiAutoTableDialog.h \
    TableEditDialog.h \
    langtranslator.h \
    mainwindow.h \
    optionsdialog.h \
    qhexedit/qhexedit.h \
    qhexedit/chunks.h \
    qhexedit/commands.h \
    pointersdialog.h \
    searchdialog.h \
    translationtable.h


SOURCES = \
    DumpScriptdialog.cpp \
    InsertScriptDialog.cpp \
    JumpToDialog.cpp \
    PointerListModel.cpp \
    SemiAutoTableDialog.cpp \
    TableEditDialog.cpp \
    langtranslator.cpp \
    main.cpp \
    mainwindow.cpp \
    optionsdialog.cpp \
    qhexedit/qhexedit.cpp \
    qhexedit/chunks.cpp \
    qhexedit/commands.cpp \
    pointersdialog.cpp \
    searchdialog.cpp \
    translationtable.cpp

RESOURCES = \
    RTHextion.qrc

FORMS += \
    DumpScriptdialog.ui \
    InsertScriptDialog.ui \
    JumpToDialog.ui \
    TableEditDialog.ui \
    optionsdialog.ui \
    pointersdialog.ui \
    searchdialog.ui

OTHER_FILES += \
    qhexedit/qhexedit.sip

DEFINES += QHEXEDIT_EXPORTS

DISTFILES +=

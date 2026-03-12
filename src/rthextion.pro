QT += core gui widgets concurrent
CONFIG += c++17

TEMPLATE = app
TARGET = RTHextion

macx {
    ICON = images/tj.icns
    QMAKE_BUNDLE = RTHextion
    QMAKE_INFO_PLIST = Info.plist
    LIBS += -liconv
}

# Windows: iconv path is passed via CI qmake arguments (INCLUDEPATH/LIBS)
# Linux:   iconv is part of glibc — no link flag needed

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
    qhexedit/hexscrollmap.h \
    pointersdialog.h \
    romdetect.h \
    searchdialog.h \
    translationtable.h \
    encodingdetect.h


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
    qhexedit/hexscrollmap.cpp \
    pointersdialog.cpp \
    searchdialog.cpp \
    translationtable.cpp

RESOURCES = \
    rthextion.qrc

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

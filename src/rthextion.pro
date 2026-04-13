QT += core gui widgets concurrent network
CONFIG += c++17

TEMPLATE = app
TARGET = RTHextion

# Capstone disassembly engine (static, embedded in project)
INCLUDEPATH += $$PWD/libs/capstone/include
LIBS += -L$$PWD/libs/capstone -lcapstone

macx {
    ICON = images/tj.icns
    QMAKE_BUNDLE = RTHextion
    QMAKE_INFO_PLIST = Info.plist
    LIBS += -liconv
    OBJECTIVE_SOURCES += utils/macostheme.mm
    HEADERS += utils/macostheme.h
}

# Windows: iconv path is passed via CI qmake arguments (INCLUDEPATH/LIBS)
# Linux:   iconv is part of glibc — no link flag needed

INCLUDEPATH += \
    $$PWD \
    $$PWD/ui \
    $$PWD/dialogs \
    $$PWD/dockwidgets \
    $$PWD/document \
    $$PWD/utils

HEADERS = \
    appinfo.h \
    ui/mainwindow.h \
    utils/langtranslator.h \
    # hexeditor
    hexeditor/hexeditor.h \
    hexeditor/chunks.h \
    hexeditor/commands.h \
    hexeditor/hexscrollmap.h \
    # document
    document/editorsession.h \
    document/hexdocument.h \
    document/translationtable.h \
    document/PointerListModel.h \
    document/SectionListModel.h \
    document/theme.h \
    # dialogs
    dialogs/DumpScriptdialog.h \
    dialogs/InsertScriptDialog.h \
    dialogs/JumpToDialog.h \
    dialogs/optionsdialog.h \
    dialogs/pointersdialog.h \
    dialogs/searchdialog.h \
    dialogs/FillWithDialog.h \
    dialogs/VirtualFormatDialog.h \
    dialogs/SemiAutoTableDialog.h \
    # dockwidgets
    dockwidgets/TablesDockWidget.h \
    dockwidgets/PointersDockWidget.h \
    dockwidgets/ChangesDockWidget.h \
    dockwidgets/SectionsDockWidget.h \
    dockwidgets/BaseDockWidget.h \
    # utils
    utils/Datas.h \
    utils/disassembler.h \
    utils/encodingdetect.h \
    utils/updatechecker.h \
    utils/romdetect.h

SOURCES = \
    main.cpp \
    ui/mainwindow.cpp \
    utils/langtranslator.cpp \
    # hexeditor
    hexeditor/hexeditor.cpp \
    hexeditor/chunks.cpp \
    hexeditor/commands.cpp \
    hexeditor/hexscrollmap.cpp \
    # document
    document/editorsession.cpp \
    document/hexdocument.cpp \
    document/translationtable.cpp \
    document/PointerListModel.cpp \
    document/SectionListModel.cpp \
    document/theme.cpp \
    # dialogs
    dialogs/DumpScriptdialog.cpp \
    dialogs/InsertScriptDialog.cpp \
    dialogs/JumpToDialog.cpp \
    dialogs/optionsdialog.cpp \
    dialogs/pointersdialog.cpp \
    dialogs/searchdialog.cpp \
    dialogs/FillWithDialog.cpp \
    dialogs/VirtualFormatDialog.cpp \
    dialogs/SemiAutoTableDialog.cpp \
    # dockwidgets
    dockwidgets/TablesDockWidget.cpp \
    dockwidgets/PointersDockWidget.cpp \
    dockwidgets/ChangesDockWidget.cpp \
    dockwidgets/SectionsDockWidget.cpp \
    dockwidgets/BaseDockWidget.cpp \
    # utils
    utils/byteglue.cpp \
    utils/disassembler.cpp \
    utils/updatechecker.cpp

RESOURCES = \
    rthextion.qrc

FORMS += \
    dialogs/DumpScriptdialog.ui \
    dialogs/InsertScriptDialog.ui \
    dialogs/JumpToDialog.ui \
    dialogs/optionsdialog.ui \
    dialogs/pointersdialog.ui

DEFINES += HEXEDITOR_EXPORTS

DISTFILES +=

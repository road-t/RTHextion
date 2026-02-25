QT += core gui widgets
CONFIG += c++17

TEMPLATE = app
TARGET = RTHextion

macx {
    ICON = images/tj.icns
}

HEADERS = \
    appinfo.h \
    Datas.h \
    DumpScriptdialog.h \
    InsertScriptDialog.h \
    JumpToDialog.h \
    PointerListModel.h \
    TableEditDialog.h \
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
    TableEditDialog.cpp \
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

TRANSLATIONS += \
    translations/qhexedit_ru.ts \
    translations/qhexedit_de.ts

# Auto-generate .qm files at qmake time when lrelease is available
isEmpty(QMAKE_LRELEASE) {
    QMAKE_LRELEASE = $$[QT_INSTALL_BINS]/lrelease
}
exists($$QMAKE_LRELEASE) {
    system($$QMAKE_LRELEASE $$_PRO_FILE_)
}

DEFINES += QHEXEDIT_EXPORTS

DISTFILES +=

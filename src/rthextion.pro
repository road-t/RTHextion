greaterThan(QT_MAJOR_VERSION, 5): QT += widgets

HEADERS = \
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
    translations/qhexedit_cs.ts \
    translations/qhexedit_de.ts

DEFINES += QHEXEDIT_EXPORTS

DISTFILES +=

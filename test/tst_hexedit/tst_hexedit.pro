QT += testlib widgets concurrent
CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = tst_hexedit

SOURCES += tst_hexedit.cpp \
    ../../src/hexeditor/hexeditor.cpp \
    ../../src/hexeditor/chunks.cpp \
    ../../src/hexeditor/commands.cpp \
    ../../src/hexeditor/hexscrollmap.cpp \
    ../../src/utils/disassembler.cpp \
    ../../src/document/SectionListModel.cpp \
    ../../src/document/translationtable.cpp \
    ../../src/document/PointerListModel.cpp

HEADERS += \
    ../../src/hexeditor/hexeditor.h \
    ../../src/hexeditor/chunks.h \
    ../../src/hexeditor/commands.h \
    ../../src/hexeditor/hexscrollmap.h \
    ../../src/utils/disassembler.h \
    ../../src/document/SectionListModel.h \
    ../../src/document/translationtable.h \
    ../../src/document/PointerListModel.h \
    ../../src/utils/Datas.h

macx: LIBS += -liconv
macx: LIBS += -L../../../src/libs/capstone -lcapstone

INCLUDEPATH += ../../src ../../src/document ../../src/utils ../../src/libs/capstone/include
DEFINES += HEXEDITOR_EXPORTS

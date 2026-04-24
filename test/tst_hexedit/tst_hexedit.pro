QT += testlib widgets concurrent
CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = tst_hexedit

SOURCES += tst_hexedit.cpp \
    ../../src/hexeditor/hexeditor.cpp \
    ../../src/hexeditor/encoding.cpp \
    ../../src/hexeditor/events.cpp \
    ../../src/hexeditor/disasm.cpp \
    ../../src/hexeditor/graphics.cpp \
    ../../src/hexeditor/pointers.cpp \
    ../../src/hexeditor/scrollmap.cpp \
    ../../src/hexeditor/layout.cpp \
    ../../src/hexeditor/chunks.cpp \
    ../../src/hexeditor/commands.cpp \
    ../../src/hexeditor/hexscrollmap.cpp \
    ../../src/utils/disassembler.cpp \
    ../../src/utils/appsettings.cpp \
    ../../src/audio/audiodetector.cpp \
    ../../src/document/SectionListModel.cpp \
    ../../src/document/translationtable.cpp \
    ../../src/document/PointerListModel.cpp

HEADERS += \
    ../../src/hexeditor/hexeditor.h \
    ../../src/hexeditor/internal.h \
    ../../src/hexeditor/encoding.h \
    ../../src/hexeditor/chunks.h \
    ../../src/hexeditor/commands.h \
    ../../src/hexeditor/hexscrollmap.h \
    ../../src/audio/audiodetector.h \
    ../../src/utils/disassembler.h \
    ../../src/document/SectionListModel.h \
    ../../src/document/translationtable.h \
    ../../src/document/PointerListModel.h \
    ../../src/utils/Datas.h

macx: LIBS += -liconv
macx: LIBS += -L../../../src/libs/capstone -lcapstone
macx: DEFINES += HAVE_CAPSTONE

INCLUDEPATH += ../../src ../../src/hexeditor ../../src/document ../../src/utils ../../src/audio ../../src/libs/capstone/include
DEFINES += HEXEDITOR_EXPORTS

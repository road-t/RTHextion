QT += testlib widgets concurrent
CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = tst_hexdocument

SOURCES += tst_hexdocument.cpp \
    ../../src/document/hexdocument.cpp \
    ../../src/document/translationtable.cpp \
    ../../src/document/PointerListModel.cpp \
    ../../src/hexeditor/hexeditor.cpp \
    ../../src/hexeditor/chunks.cpp \
    ../../src/hexeditor/commands.cpp \
    ../../src/hexeditor/hexscrollmap.cpp

HEADERS += \
    ../../src/document/hexdocument.h \
    ../../src/document/translationtable.h \
    ../../src/document/PointerListModel.h \
    ../../src/hexeditor/hexeditor.h \
    ../../src/hexeditor/chunks.h \
    ../../src/hexeditor/commands.h \
    ../../src/hexeditor/hexscrollmap.h \
    ../../src/utils/Datas.h \
    ../../src/utils/romdetect.h

macx: LIBS += -liconv

INCLUDEPATH += ../../src ../../src/document ../../src/utils
DEFINES += HEXEDITOR_EXPORTS

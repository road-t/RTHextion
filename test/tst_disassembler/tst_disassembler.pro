QT += testlib
CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = tst_disassembler

SOURCES += tst_disassembler.cpp \
    ../../src/utils/disassembler.cpp

HEADERS += \
    ../../src/utils/disassembler.h \
    ../../src/utils/romdetect.h

macx: LIBS += -liconv
macx: LIBS += -L../../../src/libs/capstone -lcapstone
macx: DEFINES += HAVE_CAPSTONE

INCLUDEPATH += ../../src ../../src/utils ../../src/libs/capstone/include

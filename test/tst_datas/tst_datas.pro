QT += testlib
QT -= gui
CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = tst_datas

SOURCES += tst_datas.cpp \
    ../../src/utils/byteglue.cpp

HEADERS += \
    ../../src/utils/Datas.h \
    ../../src/utils/Types.h \
    ../../src/utils/byteglue.h

INCLUDEPATH += ../../src/utils

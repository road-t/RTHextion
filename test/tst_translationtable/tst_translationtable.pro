QT += testlib
QT -= gui
CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = tst_translationtable

SOURCES += tst_translationtable.cpp \
    ../../src/document/translationtable.cpp

HEADERS += ../../src/document/translationtable.h

INCLUDEPATH += ../../src/document

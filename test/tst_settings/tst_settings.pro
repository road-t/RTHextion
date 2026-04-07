QT += testlib widgets
CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = tst_settings

SOURCES += tst_settings.cpp

INCLUDEPATH += ../../src ../../src/ui ../../src/document ../../src/utils
DEFINES += HEXEDITOR_EXPORTS

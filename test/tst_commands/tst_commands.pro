QT += testlib widgets
CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = tst_commands

SOURCES += tst_commands.cpp \
    ../../src/hexeditor/commands.cpp \
    ../../src/hexeditor/chunks.cpp

HEADERS += \
    ../../src/hexeditor/commands.h \
    ../../src/hexeditor/chunks.h

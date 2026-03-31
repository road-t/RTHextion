QT += testlib
QT -= gui
CONFIG += c++17 console
CONFIG -= app_bundle

DEFINES += MODUL_TEST

TARGET = tst_chunks

SOURCES += tst_chunks.cpp \
    ../../src/hexeditor/chunks.cpp

HEADERS += ../../src/hexeditor/chunks.h

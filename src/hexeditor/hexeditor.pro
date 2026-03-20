
greaterThan(QT_MAJOR_VERSION, 6): QT += widgets

QT += core gui
TEMPLATE = lib

VERSION = 6.2.0

DEFINES += HEXEDITOR_EXPORTS

HEADERS = \
    hexeditor.h \
    chunks.h \
    commands.h


SOURCES = \
    hexeditor.cpp \
    chunks.cpp \
    commands.cpp

Release:TARGET = hexeditor
Debug:TARGET = hexeditord


unix:DESTDIR = /usr/lib
win32:DESTDIR = ../lib

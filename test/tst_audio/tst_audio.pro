QT += testlib
CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = tst_audio

SOURCES += tst_audio.cpp \
    ../../src/audio/audiodetector.cpp

HEADERS += \
    ../../src/audio/audiodetector.h \
    ../../src/utils/romdetect.h

INCLUDEPATH += ../../src ../../src/audio ../../src/utils

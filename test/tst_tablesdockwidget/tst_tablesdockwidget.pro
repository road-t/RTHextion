QT += testlib widgets
CONFIG += c++17 console
CONFIG -= app_bundle

TARGET = tst_tablesdockwidget

SOURCES += tst_tablesdockwidget.cpp \
    ../../src/dockwidgets/TablesDockWidget.cpp \
    ../../src/dockwidgets/BaseDockWidget.cpp

HEADERS += \
    ../../src/dockwidgets/TablesDockWidget.h \
    ../../src/dockwidgets/BaseDockWidget.h \
    ../../src/dockwidgets/DockTitleBar.h \
    ../../src/document/translationtable.h

SOURCES += ../../src/document/translationtable.cpp

INCLUDEPATH += ../../src ../../src/dockwidgets ../../src/document

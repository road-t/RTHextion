QT += core gui widgets concurrent network
CONFIG += c++17

qtHaveModule(multimedia) {
    QT += multimedia
    DEFINES += HAVE_QT_MULTIMEDIA
} else {
    warning("Qt Multimedia module not found; audio playback will be disabled")
}

TEMPLATE = app
TARGET = RTHextion

# Capstone is mandatory for the application on all platforms.
# Resolution order:
# 1) Bundled static archive (when compatible with target architecture)
# 2) System package via pkg-config
# 3) macOS Homebrew fallback
# If none are available, stop configuration with a hard error.
contains(CONFIG, disable_capstone) {
    error("Capstone is mandatory; remove CONFIG+=disable_capstone")
}

capstone_enabled = false

contains(CONFIG, force_capstone) {
    capstone_enabled = true
    INCLUDEPATH += $$PWD/libs/capstone/include
    LIBS += -L$$PWD/libs/capstone -lcapstone
}

!equals(capstone_enabled, true):macx {
    capstone_archs = $$system("/usr/bin/lipo -archs $$shell_path($$PWD/libs/capstone/libcapstone.a) 2>/dev/null")
    capstone_target_archs = $$QMAKE_APPLE_DEVICE_ARCHS
    isEmpty(capstone_target_archs) {
        capstone_target_archs = $$system("/usr/bin/uname -m")
    }

    capstone_bundled_ok = true
    isEmpty(capstone_archs) {
        capstone_bundled_ok = false
    }
    equals(capstone_bundled_ok, true) {
        for(cap_arch, capstone_target_archs) {
            !contains(capstone_archs, $$cap_arch) {
                capstone_bundled_ok = false
            }
        }
    }

    equals(capstone_bundled_ok, true) {
        capstone_enabled = true
        INCLUDEPATH += $$PWD/libs/capstone/include
        LIBS += -L$$PWD/libs/capstone -lcapstone
    }
}

!equals(capstone_enabled, true) {
    capstone_pkg = $$system("pkg-config --exists capstone && echo yes")
    contains(capstone_pkg, yes) {
        capstone_enabled = true
        CONFIG += link_pkgconfig
        PKGCONFIG += capstone
    }
}

!equals(capstone_enabled, true):macx {
    capstone_brew_prefix = $$system("brew --prefix capstone 2>/dev/null")
    !isEmpty(capstone_brew_prefix) {
        capstone_enabled = true
        INCLUDEPATH += $$capstone_brew_prefix/include
        LIBS += -L$$capstone_brew_prefix/lib -lcapstone
    }
}

!equals(capstone_enabled, true) {
    error("Capstone is required but not found. Install system capstone (pkg-config), or provide a compatible $$PWD/libs/capstone/libcapstone.a for target architecture.")
}

DEFINES += HAVE_CAPSTONE

macx {
    ICON = images/tj.icns
    QMAKE_BUNDLE = RTHextion
    QMAKE_INFO_PLIST = Info.plist
    QMAKE_POST_LINK += /bin/cp -f $$shell_path($$PWD/Info.plist) $$shell_path($$OUT_PWD/$${TARGET}.app/Contents/Info.plist)
    LIBS += -liconv
    OBJECTIVE_SOURCES += utils/macostheme.mm
    HEADERS += utils/macostheme.h
}

# Windows: iconv path is passed via CI qmake arguments (INCLUDEPATH/LIBS)
# Linux:   iconv is part of glibc — no link flag needed

INCLUDEPATH += \
    $$PWD \
    $$PWD/ui \
    $$PWD/hexeditor \
    $$PWD/dialogs \
    $$PWD/dockwidgets \
    $$PWD/document \
    $$PWD/utils \
    $$PWD/audio

HEADERS = \
    appinfo.h \
    ui/mainwindow.h \
    ui/mainwindow/internal.h \
    utils/langtranslator.h \
    # hexeditor
    hexeditor/hexeditor.h \
    hexeditor/internal.h \
    hexeditor/encoding.h \
    hexeditor/chunks.h \
    hexeditor/commands.h \
    hexeditor/hexscrollmap.h \
    # document
    document/editorsession.h \
    document/hexdocument.h \
    document/translationtable.h \
    document/PointerListModel.h \
    document/SectionListModel.h \
    document/theme.h \
    # dialogs
    dialogs/DumpScriptdialog.h \
    dialogs/InsertScriptDialog.h \
    dialogs/JumpToDialog.h \
    dialogs/optionsdialog.h \
    dialogs/pointersdialog.h \
    dialogs/searchdialog.h \
    dialogs/FillWithDialog.h \
    dialogs/VirtualFormatDialog.h \
    dialogs/SemiAutoTableDialog.h \
    # dockwidgets
    dockwidgets/TablesDockWidget.h \
    dockwidgets/PointersDockWidget.h \
    dockwidgets/ChangesDockWidget.h \
    dockwidgets/SectionsDockWidget.h \
    dockwidgets/AudioDockWidget.h \
    dockwidgets/GraphicsDockWidget.h \
    dockwidgets/BaseDockWidget.h \
    # utils
    utils/Datas.h \
    utils/disassembler.h \
    utils/encodingdetect.h \
    utils/updatechecker.h \
    utils/appsettings.h \
    utils/romdetect.h \
    # audio
    audio/audiodetector.h \
    audio/audioplayer.h

SOURCES = \
    main.cpp \
    ui/mainwindow.cpp \
    ui/mainwindow/contextmenu.cpp \
    ui/mainwindow/sections.cpp \
    ui/mainwindow/actions.cpp \
    ui/mainwindow/session.cpp \
    ui/mainwindow/project.cpp \
    ui/mainwindow/settings.cpp \
    utils/langtranslator.cpp \
    # hexeditor
    hexeditor/hexeditor.cpp \
    hexeditor/encoding.cpp \
    hexeditor/events.cpp \
    hexeditor/disasm.cpp \
    hexeditor/graphics.cpp \
    hexeditor/pointers.cpp \
    hexeditor/scrollmap.cpp \
    hexeditor/layout.cpp \
    hexeditor/chunks.cpp \
    hexeditor/commands.cpp \
    hexeditor/hexscrollmap.cpp \
    # document
    document/editorsession.cpp \
    document/hexdocument.cpp \
    document/translationtable.cpp \
    document/PointerListModel.cpp \
    document/SectionListModel.cpp \
    document/theme.cpp \
    # dialogs
    dialogs/DumpScriptdialog.cpp \
    dialogs/InsertScriptDialog.cpp \
    dialogs/JumpToDialog.cpp \
    dialogs/optionsdialog.cpp \
    dialogs/pointersdialog.cpp \
    dialogs/searchdialog.cpp \
    dialogs/FillWithDialog.cpp \
    dialogs/VirtualFormatDialog.cpp \
    dialogs/SemiAutoTableDialog.cpp \
    # dockwidgets
    dockwidgets/TablesDockWidget.cpp \
    dockwidgets/PointersDockWidget.cpp \
    dockwidgets/ChangesDockWidget.cpp \
    dockwidgets/SectionsDockWidget.cpp \
    dockwidgets/AudioDockWidget.cpp \
    dockwidgets/GraphicsDockWidget.cpp \
    dockwidgets/BaseDockWidget.cpp \
    # utils
    utils/byteglue.cpp \
    utils/disassembler.cpp \
    utils/updatechecker.cpp \
    utils/appsettings.cpp \
    # audio
    audio/audiodetector.cpp \
    audio/audioplayer.cpp

RESOURCES = \
    rthextion.qrc

FORMS += \
    dialogs/DumpScriptdialog.ui \
    dialogs/InsertScriptDialog.ui \
    dialogs/JumpToDialog.ui \
    dialogs/optionsdialog.ui \
    dialogs/pointersdialog.ui

DEFINES += HEXEDITOR_EXPORTS

DISTFILES +=
